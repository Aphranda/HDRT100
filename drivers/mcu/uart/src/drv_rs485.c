#include "drv_rs485.h"

#include "hardware/gpio.h"

static drv_rs485_config_t s_config;
static bool s_ready;
static uint32_t s_rx_count;
static uint32_t s_tx_count;
static uint32_t s_error_count;

uint32_t drv_rs485_read(uint8_t *buffer, uint32_t capacity)
{
    if (!s_ready || buffer == NULL || capacity == 0u) {
        return 0u;
    }
    uint32_t count = 0u;
    while (count < capacity && uart_is_readable(s_config.instance)) {
        buffer[count++] = uart_getc(s_config.instance);
        ++s_rx_count;
    }
    return count;
}

bool drv_rs485_write(const uint8_t *data, uint32_t size)
{
    if (!s_ready || data == NULL || size == 0u || size > 256u) {
        ++s_error_count;
        return false;
    }
    gpio_put(s_config.de_pin, 1u);
    for (uint32_t index = 0u; index < size; ++index) {
        uart_putc_raw(s_config.instance, data[index]);
    }
    uart_tx_wait_blocking(s_config.instance);
    gpio_put(s_config.de_pin, 0u);
    s_tx_count += size;
    return true;
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
    return drv_rs485_write(buffer, count);
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
