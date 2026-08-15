#include "scpi_model_commands.h"

#include <stdint.h>

#include "distributed_config.h"
#include "distributed_refmem.h"
#include "model_turntable.h"
#include "scpi_port_internal.h"

static bool scpi_model_read_i32(scpi_t *context, int32_t *value)
{
    return SCPI_ParamInt32(context, value, TRUE) == TRUE;
}

scpi_result_t scpi_cmd_model_turntable_load(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t slot_id;
    uint32_t output_index;
    if (!scpi_port_read_u32(context, &slot_id) ||
        !scpi_port_read_u32(context, &output_index) ||
        !distributed_refmem_stage_model_turntable_load(slot_id, output_index)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_model_turntable_load_q(scpi_t *context)
{
    model_turntable_status_t status;
    model_turntable_get_status(&status);

    SCPI_ResultBool(context, status.loaded ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.slot_id);
    SCPI_ResultUInt32(context, status.output_index);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_model_turntable_trigger(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    model_turntable_trigger_config_t config;
    scpi_bool_t rising_edge;
    if (!scpi_port_read_u32(context, &config.dimension) ||
        SCPI_ParamDouble(context, &config.start, TRUE) != TRUE ||
        SCPI_ParamDouble(context, &config.stop, TRUE) != TRUE ||
        SCPI_ParamDouble(context, &config.step, TRUE) != TRUE ||
        !scpi_port_read_u32(context, &config.pulse_width_us) ||
        SCPI_ParamBool(context, &rising_edge, TRUE) != TRUE ||
        !scpi_model_read_i32(context, &config.timeout_ms)) {
        return SCPI_RES_ERR;
    }
    config.rising_edge = rising_edge == TRUE;

    return model_turntable_configure_trigger(&config) ? scpi_port_result_ok(context) :
                                                        SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_model_turntable_trigger_q(scpi_t *context)
{
    model_turntable_trigger_config_t config;
    model_turntable_get_trigger_config(&config);

    SCPI_ResultUInt32(context, config.dimension);
    SCPI_ResultDouble(context, config.start);
    SCPI_ResultDouble(context, config.stop);
    SCPI_ResultDouble(context, config.step);
    SCPI_ResultUInt32(context, config.pulse_width_us);
    SCPI_ResultBool(context, config.rising_edge ? TRUE : FALSE);
    SCPI_ResultInt32(context, config.timeout_ms);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_model_turntable_motion(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    model_turntable_motion_config_t config;
    if (SCPI_ParamDouble(context, &config.velocity_units_per_s, TRUE) != TRUE ||
        SCPI_ParamDouble(context, &config.acceleration_units_per_s2, TRUE) != TRUE ||
        !model_turntable_configure_motion(&config)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_model_turntable_motion_q(scpi_t *context)
{
    model_turntable_motion_config_t config;
    model_turntable_get_motion_config(&config);

    SCPI_ResultDouble(context, config.velocity_units_per_s);
    SCPI_ResultDouble(context, config.acceleration_units_per_s2);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_model_turntable_start(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return model_turntable_start() ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_model_turntable_stop(scpi_t *context)
{
    model_turntable_stop();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_model_turntable_state_q(scpi_t *context)
{
    model_turntable_status_t status;
    model_turntable_get_status(&status);

    SCPI_ResultBool(context, status.loaded ? TRUE : FALSE);
    SCPI_ResultBool(context, status.configured ? TRUE : FALSE);
    SCPI_ResultBool(context, status.running ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.phase);
    SCPI_ResultUInt32(context, status.slot_id);
    SCPI_ResultUInt32(context, status.output_index);
    SCPI_ResultUInt32(context, status.dimension);
    SCPI_ResultUInt32(context, status.total_pulses);
    SCPI_ResultUInt32(context, status.emitted_pulses);
    SCPI_ResultUInt32(context, status.accel_pulses);
    SCPI_ResultUInt32(context, status.cruise_pulses);
    SCPI_ResultUInt32(context, status.decel_pulses);
    SCPI_ResultUInt32(context, status.min_interval_us);
    SCPI_ResultUInt32(context, status.max_interval_us);
    SCPI_ResultUInt32(context, status.pulse_width_us);
    SCPI_ResultUInt32(context, status.last_interval_us);
    SCPI_ResultUInt32(context, status.fault_code);
    SCPI_ResultDouble(context, status.current_position);
    return SCPI_RES_OK;
}
