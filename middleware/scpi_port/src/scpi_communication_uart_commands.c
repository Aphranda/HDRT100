#include "scpi_communication_uart_commands.h"

#include <ctype.h>
#include <string.h>

#include "drv_rs485.h"
#include "rs485_communication.h"
#include "rs485_modbus.h"

typedef enum {
    SCPI_UART_MODE_SCPI = 0u,
    SCPI_UART_MODE_MODBUS = 1u,
} scpi_uart_mode_t;

static scpi_uart_mode_t s_uart_mode = SCPI_UART_MODE_SCPI;

static bool scpi_uart_text_equal(const char *value, size_t length,
                                 const char *expected)
{
    const size_t expected_length = strlen(expected);
    if (value == NULL || expected == NULL || length != expected_length) {
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        if (toupper((unsigned char)value[index]) !=
            toupper((unsigned char)expected[index])) {
            return false;
        }
    }
    return true;
}

static const char *scpi_uart_mode_text(void)
{
    return s_uart_mode == SCPI_UART_MODE_MODBUS ? "MODBUS" : "SCPI";
}

bool scpi_uart_mode_is_scpi(void)
{
    return s_uart_mode == SCPI_UART_MODE_SCPI;
}

bool scpi_uart_mode_is_modbus(void)
{
    return s_uart_mode == SCPI_UART_MODE_MODBUS;
}

static uint32_t scpi_uart_channel(scpi_t *context)
{
    int32_t numbers[1];
    if (SCPI_CommandNumbers(context, numbers, 1, 1) == 1) {
        return numbers[0] > 0 ? (uint32_t)numbers[0] : 1u;
    }
    return 1u;
}

scpi_result_t scpi_cmd_uart_baud_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultUInt32(context, rs485_communication_baud_hz());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_baud(scpi_t *context)
{
    uint32_t baud = 0u;
    if (scpi_uart_channel(context) != 1u ||
        SCPI_ParamUInt32(context, &baud, TRUE) != TRUE ||
        !rs485_communication_set_baud_hz(baud)) {
        return SCPI_RES_ERR;
    }
    return scpi_port_result_accepted(context);
}

scpi_result_t scpi_cmd_uart_mode(scpi_t *context)
{
    const char *value = NULL;
    size_t length = 0u;
    if (SCPI_ParamCharacters(context, &value, &length, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }
    scpi_uart_mode_t new_mode;
    if (scpi_uart_text_equal(value, length, "SCPI")) {
        new_mode = SCPI_UART_MODE_SCPI;
    } else if (scpi_uart_text_equal(value, length, "MODBUS")) {
        new_mode = SCPI_UART_MODE_MODBUS;
    } else {
        return SCPI_RES_ERR;
    }
    if (rs485_modbus_service_ready() &&
        !rs485_modbus_service_set_enabled(new_mode == SCPI_UART_MODE_MODBUS)) {
        return SCPI_RES_ERR;
    }
    s_uart_mode = new_mode;
    return scpi_port_result_accepted(context);
}

scpi_result_t scpi_cmd_uart_mode_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultText(context, scpi_uart_mode_text());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_format_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultUInt32(context, 8u);
    SCPI_ResultText(context, "NONE");
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_state_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultText(context, "DISABLED");
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_status_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultText(context, "READY");
    SCPI_ResultUInt32(context, drv_rs485_baud_hz());
    SCPI_ResultUInt32(context, 8u);
    SCPI_ResultText(context, "NONE");
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context,
                    drv_rs485_ready() && rs485_modbus_service_ready()
                        ? "READY"
                        : "PENDING_BACKEND");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_tx_test_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultUInt32(context, drv_rs485_tx_count());
    SCPI_ResultText(context, drv_rs485_ready() ? "READY" : "PENDING_BACKEND");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_tx_test(scpi_t *context)
{
    uint32_t count = 0u;
    uint32_t pattern = 0x55u;
    if (SCPI_ParamUInt32(context, &count, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &pattern, FALSE);
    if (pattern > 0xffu || scpi_uart_channel(context) != 1u ||
        s_uart_mode != SCPI_UART_MODE_SCPI ||
        !drv_rs485_write_test(count, (uint8_t)pattern)) {
        return SCPI_RES_ERR;
    }
    return scpi_port_result_accepted(context);
}

scpi_result_t scpi_cmd_uart_rx_count_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultUInt32(context, drv_rs485_rx_count());
    SCPI_ResultUInt32(context, drv_rs485_error_count());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_rx_status_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultText(context, drv_rs485_dma_enabled() ? "DMA_PINGPONG" :
                    "UART_FIFO");
    SCPI_ResultUInt32(context, drv_rs485_rx_count());
    SCPI_ResultUInt32(context, drv_rs485_dma_overrun_count());
    SCPI_ResultUInt32(context, drv_rs485_echo_pending_count());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_error_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultUInt32(context, drv_rs485_error_count());
    SCPI_ResultText(context, drv_rs485_ready() ? "NONE" : "PENDING_BACKEND");
    return SCPI_RES_OK;
}

static const char *scpi_uart_modbus_role_text(void)
{
    return rs485_modbus_service_role() == RS485_MODBUS_ROLE_MASTER ?
           "MASTER" : "SLAVE";
}

static const char *scpi_uart_modbus_state_text(void)
{
    switch (rs485_modbus_master_state()) {
    case RS485_MODBUS_MASTER_WAITING: return "WAITING";
    case RS485_MODBUS_MASTER_SUCCESS: return "SUCCESS";
    case RS485_MODBUS_MASTER_TIMEOUT: return "TIMEOUT";
    case RS485_MODBUS_MASTER_PROTOCOL_ERROR: return "PROTOCOL_ERROR";
    case RS485_MODBUS_MASTER_REJECTED: return "REJECTED";
    default: return "IDLE";
    }
}

scpi_result_t scpi_cmd_uart_modbus_role(scpi_t *context)
{
    const char *value = NULL;
    size_t length = 0u;
    if (scpi_uart_channel(context) != 1u ||
        SCPI_ParamCharacters(context, &value, &length, TRUE) != TRUE ||
        !rs485_modbus_service_ready()) {
        return SCPI_RES_ERR;
    }
    rs485_modbus_role_t role;
    if (scpi_uart_text_equal(value, length, "MASTER")) {
        role = RS485_MODBUS_ROLE_MASTER;
    } else if (scpi_uart_text_equal(value, length, "SLAVE")) {
        role = RS485_MODBUS_ROLE_SLAVE;
    } else {
        return SCPI_RES_ERR;
    }
    return rs485_modbus_service_set_role(role) ?
           scpi_port_result_accepted(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_uart_modbus_role_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultText(context, scpi_uart_modbus_role_text());
    SCPI_ResultText(context, rs485_modbus_service_ready() ? "READY" : "PENDING_BACKEND");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_modbus_master_read(scpi_t *context)
{
    uint32_t unit = 0u;
    uint32_t address = 0u;
    uint32_t quantity = 0u;
    if (scpi_uart_channel(context) != 1u ||
        SCPI_ParamUInt32(context, &unit, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &address, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &quantity, TRUE) != TRUE ||
        unit > 247u || address > 0xffffu || quantity > 0xffffu ||
        !rs485_modbus_master_read_holding((uint8_t)unit, (uint16_t)address,
                                           (uint16_t)quantity)) {
        return SCPI_RES_ERR;
    }
    return scpi_port_result_accepted(context);
}

scpi_result_t scpi_cmd_uart_modbus_master_write(scpi_t *context)
{
    uint32_t unit = 0u;
    uint32_t address = 0u;
    uint32_t value = 0u;
    if (scpi_uart_channel(context) != 1u ||
        SCPI_ParamUInt32(context, &unit, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &address, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &value, TRUE) != TRUE ||
        unit > 247u || address > 0xffffu || value > 0xffffu ||
        !rs485_modbus_master_write_single((uint8_t)unit, (uint16_t)address,
                                           (uint16_t)value)) {
        return SCPI_RES_ERR;
    }
    return scpi_port_result_accepted(context);
}

scpi_result_t scpi_cmd_uart_modbus_master_status_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultText(context, scpi_uart_modbus_role_text());
    SCPI_ResultText(context, scpi_uart_modbus_state_text());
    SCPI_ResultUInt32(context, rs485_modbus_master_retries_used());
    SCPI_ResultUInt32(context, rs485_modbus_master_error_count());
    return SCPI_RES_OK;
}
