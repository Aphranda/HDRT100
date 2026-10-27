#include "scpi_config_commands.h"

#include "trigger_sequence_service.h"

scpi_result_t scpi_config_trigger_parameter_q(scpi_t *context)
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

scpi_result_t scpi_config_angle_sweep_q(scpi_t *context)
{
    trigger_sequence_angle_t angle;
    trigger_sequence_service_get_angle(&angle);
    uint32_t count = 0u;
    if (angle.sweep_valid && angle.step_deg != 0.0f) {
        const float span = (angle.stop_deg - angle.start_deg) / angle.step_deg;
        if (span >= 0.0f && span < 1000000.0f) {
            count = (uint32_t)span + 1u;
        }
    }
    SCPI_ResultDouble(context, angle.start_deg);
    SCPI_ResultDouble(context, angle.stop_deg);
    SCPI_ResultDouble(context, angle.step_deg);
    SCPI_ResultUInt32(context, count);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_pulse_q(scpi_t *context)
{
    trigger_sequence_angle_t angle;
    trigger_sequence_service_get_angle(&angle);
    SCPI_ResultText(context, angle.pulse_edge == 0u ? "RISING" : "FALLING");
    SCPI_ResultUInt32(context, angle.pulse_width_us);
    SCPI_ResultUInt32(context, angle.pulse_timeout_ms);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, angle.pulse_valid ? "OK" : "UNCONFIGURED");
    SCPI_ResultBool(context, angle.pulse_valid ? TRUE : FALSE);
    SCPI_ResultDouble(context, angle.pulse_hz);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_position_q(scpi_t *context)
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

scpi_result_t scpi_config_angle_breakpoint_q(scpi_t *context)
{
    trigger_sequence_angle_t angle;
    trigger_sequence_service_get_angle(&angle);
    SCPI_ResultDouble(context, angle.breakpoint_deg);
    SCPI_ResultBool(context, angle.breakpoint_valid ? TRUE : FALSE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_q(scpi_t *context)
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

scpi_result_t scpi_config_sequence_map_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_check_q(scpi_t *context)
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

scpi_result_t scpi_config_sequence_active_q(scpi_t *context)
{
    SCPI_ResultText(context, "PLAN_A");
    SCPI_ResultUInt32(context, 0x02030405u);
    SCPI_ResultUInt32(context, 6u);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultText(context, "PASS");
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_switch_q(scpi_t *context)
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
