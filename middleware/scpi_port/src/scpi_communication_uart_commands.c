#include "scpi_communication_uart_commands.h"

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
