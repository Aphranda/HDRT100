#include "drv_rs485.h"

#include <string.h>

#include "hardware/gpio.h"
#include "pico/time.h"

#define RS485_ECHO_IDLE_US 20000u

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

uint32_t drv_rs485_read(uint8_t *buffer, uint32_t capacity)
{
    if (!s_ready || buffer == NULL || capacity == 0u) {
        return 0u;
    }
    uint32_t count = 0u;
    bool saw_byte = false;
    while (uart_is_readable(s_config.instance)) {
        const uint8_t byte = uart_getc(s_config.instance);
        ++s_rx_count;
        saw_byte = true;
        if (s_response_echo_pos < s_response_echo_len) {
            if (byte == s_response_echo[s_response_echo_pos]) {
                ++s_response_echo_pos;
                if (s_response_echo_pos == s_response_echo_len) {
                    s_response_echo_len = 0u;
                    s_response_echo_pos = 0u;
                }
                continue;
            }
            /* A host command can overtake a delayed local echo.  Keep the
             * expected response frame pending and parse this byte normally. */
            s_response_echo_pos = 0u;
        }
        if (s_echo_remaining > 0u && byte == s_echo_pattern) {
            /* Hold a possible echo run until the next byte proves it is not
             * contiguous.  This avoids dropping an isolated pattern byte in
             * a real SCPI command such as UART1 while removing 8x0x55. */
            ++s_echo_candidate_len;
            if (s_echo_candidate_len >= s_echo_remaining) {
                s_echo_remaining = 0u;
                s_echo_candidate_len = 0u;
                s_echo_discard = false;
            }
            continue;
        }
        while (s_echo_candidate_len > 0u && count < capacity) {
            buffer[count++] = s_echo_pattern;
            --s_echo_candidate_len;
        }
        if (s_echo_candidate_len != 0u) {
            break;
        }
        if (count < capacity) {
            buffer[count++] = byte;
        }
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
    s_ready = true;
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
