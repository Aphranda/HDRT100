#include "scpi_loop_engine_commands.h"

#include "loop_engine.h"

scpi_result_t scpi_cmd_loop_status_q(scpi_t *context)
{
    loop_engine_status_t status;
    loop_engine_get_status(&status);

    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.first_service_ms);
    SCPI_ResultUInt32(context, status.last_service_ms);
    return SCPI_RES_OK;
}
