#include "drv_rs485.h"

#include <string.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/time.h"

#define RS485_ECHO_IDLE_US 2000u
#define RS485_DMA_BUFFER_SIZE 256u
#define RS485_FRAME_BITS 10u
#define RS485_ECHO_GUARD_MARGIN_US 4000u

static drv_rs485_config_t s_config;
static bool s_ready;
static uint32_t s_rx_count;
static uint32_t s_tx_count;
static uint32_t s_error_count;
static bool s_echo_discard;
static uint64_t s_echo_last_rx_us;
static uint8_t s_echo_pattern;
static uint32_t s_echo_remaining;
static uint32_t s_echo_candidate_len;
static uint8_t s_response_echo[256];
static uint32_t s_response_echo_len;
static uint32_t s_response_echo_pos;
static uint64_t s_tx_echo_guard_until_us;
static uint32_t s_tx_echo_guard_dropped;
static uint8_t s_dma_rx[2][RS485_DMA_BUFFER_SIZE];
static int s_dma_channel[2] = {-1, -1};
static volatile uint8_t s_dma_ready_mask;
static volatile uint32_t s_dma_overrun_count;
static uint32_t s_dma_read_index;
static uint32_t s_dma_read_offset;
static uint32_t s_dma_last_produced[2];
static uint64_t s_dma_last_activity_us;
static bool s_dma_enabled;

static void rs485_dma_irq_handler(void)
{
    const uint32_t interrupts = dma_hw->ints1;
    dma_hw->ints1 = interrupts;
    for (uint32_t index = 0u; index < 2u; ++index) {
        const uint32_t channel = (uint32_t)s_dma_channel[index];
        const uint32_t bit = 1u << channel;
        if ((interrupts & bit) == 0u) {
            continue;
        }
        const uint8_t mask = (uint8_t)(1u << index);
        if ((s_dma_ready_mask & mask) != 0u) {
            ++s_dma_overrun_count;
        }
        s_dma_ready_mask |= mask;
    }
}

static bool rs485_dma_init(void)
{
    s_dma_channel[0] = (int)dma_claim_unused_channel(false);
    s_dma_channel[1] = (int)dma_claim_unused_channel(false);
    if (s_dma_channel[0] < 0 || s_dma_channel[1] < 0) {
        if (s_dma_channel[0] >= 0) {
            dma_channel_unclaim((uint)s_dma_channel[0]);
        }
        if (s_dma_channel[1] >= 0) {
            dma_channel_unclaim((uint)s_dma_channel[1]);
        }
        s_dma_channel[0] = -1;
        s_dma_channel[1] = -1;
        return false;
    }

    irq_add_shared_handler(DMA_IRQ_1, rs485_dma_irq_handler,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
    for (uint32_t index = 0u; index < 2u; ++index) {
        const uint32_t channel = (uint32_t)s_dma_channel[index];
        dma_channel_config config = dma_channel_get_default_config(channel);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
        channel_config_set_read_increment(&config, false);
        channel_config_set_write_increment(&config, true);
        channel_config_set_dreq(&config, uart_get_dreq(s_config.instance, false));
        channel_config_set_chain_to(&config,
                                    (uint)s_dma_channel[(index + 1u) & 1u]);
        dma_channel_configure(channel, &config, s_dma_rx[index],
                              &uart_get_hw(s_config.instance)->dr,
                              RS485_DMA_BUFFER_SIZE, false);
        dma_channel_set_irq1_enabled(channel, true);
    }
    dma_start_channel_mask(1u << (uint)s_dma_channel[0]);
    s_dma_ready_mask = 0u;
    s_dma_overrun_count = 0u;
    s_dma_read_index = 0u;
    s_dma_read_offset = 0u;
    s_dma_last_produced[0] = 0u;
    s_dma_last_produced[1] = 0u;
    s_dma_last_activity_us = time_us_64();
    s_dma_enabled = true;
    return true;
}

static uint32_t rs485_dma_partial_limit(uint32_t index)
{
    if (!s_dma_enabled || index > 1u ||
        (s_dma_ready_mask & (uint8_t)(1u << index)) != 0u) {
        return 0u;
    }
    const uintptr_t base = (uintptr_t)s_dma_rx[index];
    const uintptr_t address = dma_channel_hw_addr(
        (uint)s_dma_channel[index])->write_addr;
    if (address < base || address > base + RS485_DMA_BUFFER_SIZE) {
        return 0u;
    }
    const uint32_t produced = (uint32_t)(address - base);
    if (produced != s_dma_last_produced[index]) {
        s_dma_last_produced[index] = produced;
        s_dma_last_activity_us = time_us_64();
        return 0u;
    }
    if (produced <= s_dma_read_offset ||
        time_us_64() - s_dma_last_activity_us < RS485_ECHO_IDLE_US) {
        return 0u;
    }
    return produced;
}

static uint32_t rs485_process_rx_byte(uint8_t byte, uint8_t *buffer,
                                      uint32_t count, uint32_t capacity)
{
    if (s_response_echo_pos < s_response_echo_len) {
        if (byte == s_response_echo[s_response_echo_pos]) {
            ++s_response_echo_pos;
            if (s_response_echo_pos == s_response_echo_len) {
                s_response_echo_len = 0u;
                s_response_echo_pos = 0u;
            }
            return count;
        }
        s_response_echo_pos = 0u;
    }
    if (s_echo_remaining > 0u && byte == s_echo_pattern) {
        ++s_echo_candidate_len;
        if (s_echo_candidate_len >= s_echo_remaining) {
            s_echo_remaining = 0u;
            s_echo_candidate_len = 0u;
            s_echo_discard = false;
        }
        return count;
    }
    while (s_echo_candidate_len > 0u && count < capacity) {
        buffer[count++] = s_echo_pattern;
        --s_echo_candidate_len;
    }
    if (s_echo_candidate_len != 0u || count >= capacity) {
        return count;
    }
    buffer[count++] = byte;
    return count;
}

static void rs485_start_tx_echo_guard(uint32_t size)
{
    /* A two-wire loopback can return the local TX frame through RX after DE
     * is released.  A byte matcher is vulnerable to DMA chunking and
     * scheduler gaps, so discard only the bounded, baud-derived echo window. */
    const uint64_t baud = s_config.baud_hz != 0u ? s_config.baud_hz : 1u;
    const uint64_t frame_us =
        ((uint64_t)size * RS485_FRAME_BITS * 1000000u + baud - 1u) / baud;
    s_tx_echo_guard_until_us = time_us_64() + frame_us +
                               RS485_ECHO_GUARD_MARGIN_US;
}

static bool rs485_tx_echo_guard_active(void)
{
    if (s_tx_echo_guard_until_us == 0u) {
        return false;
    }
    if (time_us_64() < s_tx_echo_guard_until_us) {
        return true;
    }
    s_tx_echo_guard_until_us = 0u;
    s_echo_remaining = 0u;
    s_echo_candidate_len = 0u;
    s_response_echo_len = 0u;
    s_response_echo_pos = 0u;
    return false;
}

uint32_t drv_rs485_read(uint8_t *buffer, uint32_t capacity)
{
    if (!s_ready || buffer == NULL || capacity == 0u) {
        return 0u;
    }
    uint32_t count = 0u;
    bool saw_byte = false;
    while (count < capacity) {
        if (s_dma_enabled) {
            const uint8_t ready = s_dma_ready_mask;
            if ((ready & (uint8_t)(1u << s_dma_read_index)) == 0u) {
                const uint32_t partial = rs485_dma_partial_limit(s_dma_read_index);
                if (partial <= s_dma_read_offset) {
                    break;
                }
            }
            const uint8_t byte = s_dma_rx[s_dma_read_index][s_dma_read_offset++];
            ++s_rx_count;
            saw_byte = true;
            if (rs485_tx_echo_guard_active()) {
                ++s_tx_echo_guard_dropped;
                if (s_dma_read_offset == RS485_DMA_BUFFER_SIZE) {
                    s_dma_ready_mask &= (uint8_t)~(1u << s_dma_read_index);
                    s_dma_read_offset = 0u;
                    s_dma_last_produced[s_dma_read_index] = 0u;
                    s_dma_read_index ^= 1u;
                }
                continue;
            }
            count = rs485_process_rx_byte(byte, buffer, count, capacity);
            if (s_dma_read_offset == RS485_DMA_BUFFER_SIZE) {
                s_dma_ready_mask &= (uint8_t)~(1u << s_dma_read_index);
                s_dma_read_offset = 0u;
                s_dma_last_produced[s_dma_read_index] = 0u;
                s_dma_read_index ^= 1u;
            }
            continue;
        }
        if (!uart_is_readable(s_config.instance)) {
            break;
        }
        const uint8_t byte = uart_getc(s_config.instance);
        ++s_rx_count;
        saw_byte = true;
        if (rs485_tx_echo_guard_active()) {
            ++s_tx_echo_guard_dropped;
            continue;
        }
        count = rs485_process_rx_byte(byte, buffer, count, capacity);
    }
    if (s_echo_candidate_len > 0u && !saw_byte &&
        time_us_64() - s_echo_last_rx_us >= RS485_ECHO_IDLE_US) {
        while (s_echo_candidate_len > 0u && count < capacity) {
            buffer[count++] = s_echo_pattern;
            --s_echo_candidate_len;
        }
    }
    return count;
}

static bool drv_rs485_write_internal(const uint8_t *data, uint32_t size,
                                     bool discard_echo)
{
    if (!s_ready || data == NULL || size == 0u || size > 256u) {
        ++s_error_count;
        return false;
    }
    rs485_start_tx_echo_guard(size);
    if (discard_echo) {
        /* Only the explicit loopback diagnostic needs an idle-gap receive
         * guard.  Normal SCPI/Modbus replies must not suppress the next host
         * command while the line is being turned around. */
        s_echo_discard = true;
        s_echo_candidate_len = 0u;
    } else {
        memcpy(s_response_echo, data, size);
        s_response_echo_len = size;
        s_response_echo_pos = 0u;
    }
    gpio_put(s_config.de_pin, 1u);
    for (uint32_t index = 0u; index < size; ++index) {
        uart_putc_raw(s_config.instance, data[index]);
    }
    uart_tx_wait_blocking(s_config.instance);
    gpio_put(s_config.de_pin, 0u);
    if (discard_echo) {
        /* Start the idle window after the final stop bit, not at the start
         * of TX; otherwise a long diagnostic frame can age out its own echo
         * guard before the receiver sees the first returned byte. */
        s_echo_last_rx_us = time_us_64();
    } else {
        /* The baud-derived guard is authoritative for normal replies; do not
         * leave a stale response matcher armed after the guard expires. */
        s_response_echo_len = 0u;
        s_response_echo_pos = 0u;
    }
    s_tx_count += size;
    return true;
}

bool drv_rs485_write(const uint8_t *data, uint32_t size)
{
    return drv_rs485_write_internal(data, size, false);
}

bool drv_rs485_init(const drv_rs485_config_t *config)
{
    if (config == NULL || config->instance == NULL ||
        config->baud_hz == 0u || config->tx_pin > 47u ||
        config->rx_pin > 47u || config->de_pin > 47u) {
        return false;
    }
    s_config = *config;
    uart_init(s_config.instance, s_config.baud_hz);
    gpio_set_function(s_config.tx_pin, GPIO_FUNC_UART);
    gpio_set_function(s_config.rx_pin, GPIO_FUNC_UART);
    gpio_init(s_config.de_pin);
    gpio_set_dir(s_config.de_pin, GPIO_OUT);
    gpio_put(s_config.de_pin, 0u);
    uart_set_hw_flow(s_config.instance, false, false);
    s_rx_count = 0u;
    s_tx_count = 0u;
    s_error_count = 0u;
    s_echo_discard = false;
    s_echo_last_rx_us = 0u;
    s_echo_pattern = 0u;
    s_echo_remaining = 0u;
    s_echo_candidate_len = 0u;
    s_response_echo_len = 0u;
    s_response_echo_pos = 0u;
    s_tx_echo_guard_until_us = 0u;
    s_tx_echo_guard_dropped = 0u;
    s_dma_enabled = false;
    s_dma_ready_mask = 0u;
    s_dma_read_index = 0u;
    s_dma_read_offset = 0u;
    s_dma_last_produced[0] = 0u;
    s_dma_last_produced[1] = 0u;
    s_dma_last_activity_us = time_us_64();
    s_ready = true;
    (void)rs485_dma_init();
    return true;
}

bool drv_rs485_ready(void)
{
    return s_ready;
}

bool drv_rs485_write_test(uint32_t count, uint8_t pattern)
{
    if (!s_ready || count == 0u || count > 256u) {
        ++s_error_count;
        return false;
    }
    uint8_t buffer[256];
    for (uint32_t index = 0u; index < count; ++index) {
        buffer[index] = pattern;
    }
    s_echo_pattern = pattern;
    s_echo_remaining = count;
    s_echo_candidate_len = 0u;
    return drv_rs485_write_internal(buffer, count, true);
}

uint32_t drv_rs485_rx_count(void)
{
    uint8_t buffer[64];
    (void)drv_rs485_read(buffer, sizeof(buffer));
    return s_rx_count;
}

uint32_t drv_rs485_tx_count(void)
{
    return s_tx_count;
}

uint32_t drv_rs485_error_count(void)
{
    return s_error_count;
}

bool drv_rs485_dma_enabled(void)
{
    return s_dma_enabled;
}

uint32_t drv_rs485_dma_overrun_count(void)
{
    return s_dma_overrun_count;
}

uint32_t drv_rs485_echo_pending_count(void)
{
    return s_echo_remaining + s_echo_candidate_len +
           (s_response_echo_len - s_response_echo_pos);
}
