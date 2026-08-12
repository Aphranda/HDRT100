#include "scpi_config_commands.h"

scpi_result_t scpi_product_trigger_parameter_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 8u);
    SCPI_ResultUInt32(context, 2u);
    SCPI_ResultUInt32(context, 5u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 80u);
    SCPI_ResultUInt32(context, 0x11112222u);
    SCPI_ResultUInt32(context, 0x22223333u);
    SCPI_ResultUInt32(context, 0x33334444u);
    SCPI_ResultUInt32(context, 0x44445555u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_angle_sweep_q(scpi_t *context)
{
    SCPI_ResultInt32(context, -10);
    SCPI_ResultInt32(context, 370);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 381u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_angle_pulse_q(scpi_t *context)
{
    SCPI_ResultText(context, "RISING");
    SCPI_ResultUInt32(context, 10u);
    SCPI_ResultUInt32(context, 30000u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "OK");
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_angle_position_q(scpi_t *context)
{
    SCPI_ResultText(context, "DTC_SWEEP");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 381u);
    SCPI_ResultInt32(context, -10);
    SCPI_ResultInt32(context, -9);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_angle_breakpoint_q(scpi_t *context)
{
    SCPI_ResultInt32(context, 0);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_sequence_q(scpi_t *context)
{
    SCPI_ResultText(context, "PLAN_A");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0x02030405u);
    SCPI_ResultUInt32(context, 6u);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 2u);
    SCPI_ResultUInt32(context, 3u);
    SCPI_ResultUInt32(context, 4u);
    SCPI_ResultUInt32(context, 5u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_sequence_map_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_sequence_check_q(scpi_t *context)
{
    SCPI_ResultText(context, "PLAN_A");
    SCPI_ResultUInt32(context, 6u);
    SCPI_ResultUInt32(context, 0x02030405u);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_sequence_active_q(scpi_t *context)
{
    SCPI_ResultText(context, "PLAN_A");
    SCPI_ResultUInt32(context, 0x02030405u);
    SCPI_ResultUInt32(context, 6u);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultText(context, "PASS");
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_switch_q(scpi_t *context)
{
    int32_t numbers[1];
    SCPI_CommandNumbers(context, numbers, 1u, 1);
    SCPI_ResultInt32(context, numbers[0]);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}
