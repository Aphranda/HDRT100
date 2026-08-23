#ifndef SCPI_COMMUNICATION_UART_COMMANDS_H
#define SCPI_COMMUNICATION_UART_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_port_internal.h"

scpi_result_t scpi_cmd_uart_baud_q(scpi_t *context);
scpi_result_t scpi_cmd_uart_mode(scpi_t *context);
scpi_result_t scpi_cmd_uart_mode_q(scpi_t *context);
scpi_result_t scpi_cmd_uart_format_q(scpi_t *context);
scpi_result_t scpi_cmd_uart_state_q(scpi_t *context);
scpi_result_t scpi_cmd_uart_status_q(scpi_t *context);
scpi_result_t scpi_cmd_uart_tx_test(scpi_t *context);
scpi_result_t scpi_cmd_uart_tx_test_q(scpi_t *context);
scpi_result_t scpi_cmd_uart_rx_count_q(scpi_t *context);
scpi_result_t scpi_cmd_uart_rx_status_q(scpi_t *context);
scpi_result_t scpi_cmd_uart_error_q(scpi_t *context);
bool scpi_uart_mode_is_scpi(void);

#define SCPI_COMMUNICATION_UART_COMMANDS \
    {.pattern = "COMMunication:SERial:UART#:BAUD", .callback = scpi_port_result_accepted}, \
    {.pattern = "COMMunication:SERial:UART#:BAUD?", .callback = scpi_cmd_uart_baud_q}, \
    {.pattern = "COMMunication:SERial:UART#:MODE", .callback = scpi_cmd_uart_mode}, \
    {.pattern = "COMMunication:SERial:UART#:MODE?", .callback = scpi_cmd_uart_mode_q}, \
    {.pattern = "COMMunication:SERial:UART#:FORMat", .callback = scpi_port_result_accepted}, \
    {.pattern = "COMMunication:SERial:UART#:FORMat?", .callback = scpi_cmd_uart_format_q}, \
    {.pattern = "COMMunication:SERial:UART#:STATe", .callback = scpi_port_result_accepted}, \
    {.pattern = "COMMunication:SERial:UART#:STATe?", .callback = scpi_cmd_uart_state_q}, \
    {.pattern = "COMMunication:SERial:UART#:STATus?", .callback = scpi_cmd_uart_status_q}, \
    {.pattern = "COMMunication:SERial:UART#:TX:TEST", .callback = scpi_cmd_uart_tx_test}, \
    {.pattern = "COMMunication:SERial:UART#:TX:TEST?", .callback = scpi_cmd_uart_tx_test_q}, \
    {.pattern = "COMMunication:SERial:UART#:RX:COUNt?", .callback = scpi_cmd_uart_rx_count_q}, \
    {.pattern = "COMMunication:SERial:UART#:RX:STATus?", .callback = scpi_cmd_uart_rx_status_q}, \
    {.pattern = "COMMunication:SERial:UART#:ERRor?", .callback = scpi_cmd_uart_error_q}

#endif
