#include "scpi_realtime_io_commands.h"

#include "distributed_config.h"
#include "scpi_port_internal.h"
#include "sync_io.h"
#include "sync_io_hw_profile.h"
#include "sync_trigger.h"
#include "project_config.h"

static uint32_t s_debug_output_mask;

scpi_result_t scpi_cmd_trigger_width(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_SET_TRIGGER_WIDTH, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_trigger_width_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.trigger_width_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_fire(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_FIRE_TRIGGER, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_pulse_width(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_SET_PULSE_WIDTH, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_pulse_width_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.pulse_width_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_pulse_fire(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_FIRE_PULSE, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_marker_width(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_SET_MARKER_WIDTH, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_marker_width_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.marker_width_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_marker_fire(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_FIRE_MARKER, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_rj45_trigger_width(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_SET_RJ45_TRIGGER_WIDTH,
                                             value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_rj45_trigger_width_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.rj45_trigger_width_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_rj45_trigger_fire(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_FIRE_RJ45_TRIGGER, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_rj45_trigger_pins_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, SYNC_IO_HW_RJ45_TRIG_IN_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_RJ45_TRIG_OUT_PIN);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_io_profile_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, SYNC_IO_HW_MAIN_INPUT_BASE_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_MAIN_INPUT_PIN_COUNT);
    SCPI_ResultUInt32(context, SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_MAIN_OUTPUT_PIN_COUNT);
    SCPI_ResultUInt32(context, SYNC_IO_HW_TRIG_IN_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_RJ45_TRIG_IN_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_TRIG_OUT_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_RJ45_TRIG_OUT_PIN);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_input_level_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, SYNC_IO_HW_MAIN_INPUT_BASE_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_MAIN_INPUT_PIN_COUNT);
    SCPI_ResultUInt32(context, sync_io_debug_read_input_mask());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_output_mask(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value)) {
        return SCPI_RES_ERR;
    }

    const uint32_t valid_mask = (1u << SYNC_IO_HW_MAIN_OUTPUT_PIN_COUNT) - 1u;
    if ((value & ~valid_mask) != 0u || !sync_io_debug_set_output_mask(value)) {
        return SCPI_RES_ERR;
    }

    s_debug_output_mask = value;
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_output_mask_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_MAIN_OUTPUT_PIN_COUNT);
    SCPI_ResultUInt32(context, s_debug_output_mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_output_release(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    sync_io_debug_release_output_mask();
    s_debug_output_mask = 0u;
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_model_profile_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, BOARD_DEBUG_MODEL_GPIO_BASE_PIN);
    SCPI_ResultUInt32(context, BOARD_DEBUG_MODEL_GPIO_PIN_COUNT);
    SCPI_ResultUInt32(context, BOARD_DEBUG_MODEL_UART_CONFLICT_MASK);
    SCPI_ResultBool(context, PROJECT_ENABLE_UART_STDIO ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_model_input_level_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, BOARD_DEBUG_MODEL_GPIO_BASE_PIN);
    SCPI_ResultUInt32(context, BOARD_DEBUG_MODEL_GPIO_PIN_COUNT);
    SCPI_ResultUInt32(context, sync_io_debug_model_read_input_mask());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_model_output_mask(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t enable_mask;
    uint32_t value_mask;
    if (!scpi_port_read_u32(context, &enable_mask) ||
        !scpi_port_read_u32(context, &value_mask)) {
        return SCPI_RES_ERR;
    }

    if (PROJECT_ENABLE_UART_STDIO &&
        ((enable_mask & BOARD_DEBUG_MODEL_UART_CONFLICT_MASK) != 0u)) {
        return SCPI_RES_ERR;
    }

    return sync_io_debug_model_set_output_mask(enable_mask, value_mask) ?
               scpi_port_result_ok(context) :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_model_output_mask_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, BOARD_DEBUG_MODEL_GPIO_BASE_PIN);
    SCPI_ResultUInt32(context, BOARD_DEBUG_MODEL_GPIO_PIN_COUNT);
    SCPI_ResultUInt32(context, sync_io_debug_model_get_output_enable_mask());
    SCPI_ResultUInt32(context, sync_io_debug_model_get_output_value_mask());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_model_output_release(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    sync_io_debug_model_release();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_sample_rate(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_SET_SAMPLE_RATE, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_sample_rate_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.capture_sample_hz);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sample_state(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    scpi_bool_t state;
    if (SCPI_ParamBool(context, &state, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_SET_SAMPLE_STATE,
                                             state ? 1u : 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_sample_state_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultBool(context, summary.capture_running ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sample_latch_q(scpi_t *context)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    SCPI_ResultBool(context, status.initialized ? TRUE : FALSE);
    SCPI_ResultBool(context, status.capture_running ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.capture_sample_hz);
    SCPI_ResultUInt32(context, status.dropped_capture_words);
    SCPI_ResultUInt32(context, status.latched_capture_words);
    SCPI_ResultUInt32(context, status.dropped_latched_capture_words);
    SCPI_ResultUInt32(context, status.capture_latch_source);
    SCPI_ResultUInt32(context, status.capture_latch_resolution_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_clock_freq(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_SET_CLOCK_FREQ, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_clock_freq_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.sync_clock_hz);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_clock_state(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    scpi_bool_t state;
    if (SCPI_ParamBool(context, &state, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_sync_trigger_event(SYNC_TRIGGER_EVENT_SET_CLOCK_STATE,
                                             state ? 1u : 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_clock_state_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultBool(context, summary.sync_clock_running ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_status_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultBool(context, summary.io_initialized ? TRUE : FALSE);
    SCPI_ResultBool(context, summary.capture_running ? TRUE : FALSE);
    SCPI_ResultBool(context, summary.sync_clock_running ? TRUE : FALSE);
    SCPI_ResultUInt32(context, summary.capture_sample_hz);
    SCPI_ResultUInt32(context, summary.sync_clock_hz);
    SCPI_ResultUInt32(context, summary.dropped_capture_words);
    return SCPI_RES_OK;
}
