#ifndef DRV_UART_H
#define DRV_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/uart.h"

typedef struct {
    uart_inst_t *instance;
    uint32_t tx_pin;
    uint32_t rx_pin;
    uint32_t baud_hz;
} drv_uart_config_t;

bool drv_uart_init(const drv_uart_config_t *config);

#endif
