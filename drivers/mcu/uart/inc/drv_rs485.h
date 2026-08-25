#ifndef DRV_RS485_H
#define DRV_RS485_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/uart.h"

typedef struct {
    uart_inst_t *instance;
    uint32_t tx_pin;
    uint32_t rx_pin;
    uint32_t de_pin;
    uint32_t baud_hz;
} drv_rs485_config_t;

bool drv_rs485_init(const drv_rs485_config_t *config);
bool drv_rs485_set_baud_hz(uint32_t baud_hz);
uint32_t drv_rs485_baud_hz(void);
void drv_rs485_service(void);
bool drv_rs485_ready(void);
uint32_t drv_rs485_read(uint8_t *buffer, uint32_t capacity);
bool drv_rs485_write(const uint8_t *data, uint32_t size);
bool drv_rs485_write_test(uint32_t count, uint8_t pattern);
uint32_t drv_rs485_rx_count(void);
uint32_t drv_rs485_rx_count_snapshot(void);
uint32_t drv_rs485_tx_count(void);
uint32_t drv_rs485_error_count(void);
bool drv_rs485_dma_enabled(void);
uint32_t drv_rs485_dma_overrun_count(void);
uint32_t drv_rs485_echo_pending_count(void);
bool drv_rs485_idle_event_take(void);
uint32_t drv_rs485_modbus_frame_gap_us(void);

#endif
