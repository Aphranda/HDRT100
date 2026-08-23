#include "scpi_communication_uart_commands.h"

#include <ctype.h>
#include <string.h>

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
    SCPI_ResultUInt32(context, 115200u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_mode(scpi_t *context)
{
    const char *value = NULL;
    size_t length = 0u;
    if (SCPI_ParamCharacters(context, &value, &length, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }
    if (scpi_uart_text_equal(value, length, "SCPI")) {
        s_uart_mode = SCPI_UART_MODE_SCPI;
    } else if (scpi_uart_text_equal(value, length, "MODBUS")) {
        s_uart_mode = SCPI_UART_MODE_MODBUS;
    } else {
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
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
    SCPI_ResultUInt32(context, 115200u);
    SCPI_ResultUInt32(context, 8u);
    SCPI_ResultText(context, "NONE");
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "PENDING_BACKEND");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_tx_test_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "PENDING_BACKEND");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_rx_count_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_uart_error_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, scpi_uart_channel(context));
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}
