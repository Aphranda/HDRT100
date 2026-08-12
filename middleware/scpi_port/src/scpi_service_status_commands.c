#include "scpi_service_status_commands.h"

#include "app.h"

scpi_result_t scpi_cmd_loop_status_q(scpi_t *context)
{
    app_loop_engine_status_t status;
    app_loop_engine_get_status(&status);

    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.first_service_ms);
    SCPI_ResultUInt32(context, status.last_service_ms);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_status_q(scpi_t *context)
{
    app_vdc_sync_status_t status;
    app_vdc_sync_get_status(&status);

    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.lock_state);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.first_service_ms);
    SCPI_ResultUInt32(context, status.last_service_ms);
    SCPI_ResultUInt32(context, status.sync_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_dpll_status_q(scpi_t *context)
{
    app_dpll_status_t status;
    app_dpll_get_status(&status);

    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.state);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.first_service_ms);
    SCPI_ResultUInt32(context, status.last_service_ms);
    SCPI_ResultUInt32(context, status.update_seq);
    return SCPI_RES_OK;
}
