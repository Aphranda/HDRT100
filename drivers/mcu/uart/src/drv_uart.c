#include "drv_uart.h"

#include "hardware/gpio.h"

bool drv_uart_init(const drv_uart_config_t *config)
{
    if (config == NULL || config->instance == NULL) {
        return false;
    }

    uart_init(config->instance, config->baud_hz);
    gpio_set_function(config->tx_pin, GPIO_FUNC_UART);
    gpio_set_function(config->rx_pin, GPIO_FUNC_UART);

    return true;
}
