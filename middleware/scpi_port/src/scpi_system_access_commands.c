#include "scpi_system_access_commands.h"

scpi_result_t scpi_system_access_permission_q(scpi_t *context)
{
    SCPI_ResultText(context, "TEST");
    SCPI_ResultText(context, "*");
    SCPI_ResultText(context, "FRAMEWORK");
    SCPI_ResultText(context, "ALLOW");
    SCPI_ResultText(context, "IDLE,CONFIG,ARM,PAUSE,RUN");
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultText(context, "DEFAULT");
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_system_access_role_q(scpi_t *context)
{
    SCPI_ResultText(context, "TEST");
    SCPI_ResultText(context, "TEST<SERVICE<DEBUG<FACTORY");
    SCPI_ResultText(context, "TEST,SERVICE,DEBUG,FACTORY");
    SCPI_ResultText(context, "LOCAL");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "DEFAULT");
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}
