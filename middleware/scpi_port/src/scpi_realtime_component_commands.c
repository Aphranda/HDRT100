#include "scpi_realtime_component_commands.h"

#include <string.h>

#include "distributed_config.h"
#include "scpi_port_internal.h"
#include "scpi_trigger_commands.h"
#include "storage_manager.h"
#include "sync_io_hw_profile.h"
#include "sync_trigger.h"

static bool scpi_realtime_component_biss_role_supported(trig_biss_role_t role)
{
    return role == TRIG_BISS_ROLE_TAP_MONITOR;
}

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

/* ── SEQ_STEP 模式命令 ── */

static uint32_t        s_seq_table_buf[TRIG_SEQ_TABLE_MAX];
static uint32_t        s_seq_table_len;
static uint32_t        s_seq_table_width;

static uint32_t scpi_trigger_seq_length_or_default(void)
{
    return s_seq_table_len > 0u ? s_seq_table_len : 1u;
}

static uint32_t scpi_trigger_seq_width_or_default(void)
{
    return s_seq_table_width > 0u ? s_seq_table_width : 1u;
}

static void scpi_trigger_prepare_default_seq(void)
{
    if (s_seq_table_len == 0u) {
        s_seq_table_buf[0] = 0u;
    }
}

static const char *scpi_trig_mode_to_string(trig_mode_t mode)
{
    switch (mode) {
    case TRIG_MODE_IDLE:             return "IDLE";
    case TRIG_MODE_SEQ_STEP:         return "SEQ_STEP";
    case TRIG_MODE_ENC_COUNT:        return "ENC_COUNT";
    case TRIG_MODE_PROTOCOL_TRIGGER: return "PROTOCOL_TRIGGER";
    default:                         return "UNKNOWN";
    }
}

scpi_result_t scpi_cmd_trigger_seq_length(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) ||
        value == 0u || value > TRIG_SEQ_TABLE_MAX) {
        return SCPI_RES_ERR;
    }
    s_seq_table_len = value;
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_trigger_seq_length_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.seq_length);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_seq_width(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) ||
        value == 0u || value > TRIG_SEQ_WIDTH_MAX) {
        return SCPI_RES_ERR;
    }
    s_seq_table_width = value;
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_trigger_seq_width_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.seq_output_width);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_seq_index_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.seq_index);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_seq_data(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    const char *data = NULL;
    size_t length = 0u;
    if (SCPI_ParamArbitraryBlock(context, &data, &length, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    if (length == 0u || length > sizeof(s_seq_table_buf)) {
        return SCPI_RES_ERR;
    }

    const size_t word_count = length / sizeof(uint32_t);
    if (word_count == 0u || (length % sizeof(uint32_t)) != 0u) {
        return SCPI_RES_ERR;
    }

    memcpy(s_seq_table_buf, data, length);
    s_seq_table_len = (uint32_t)word_count;

    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_trigger_seq_data_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);

    /* 逐字输出序列表 */
    for (uint32_t i = 0u; i < vector.seq_length; i++) {
        SCPI_ResultUInt32(context, vector.seq_table[i]);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_arm(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    scpi_port_set_trigger_debug_stage(200u);
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    scpi_port_set_trigger_debug_stage(201u);
    if (vector.state == TRIG_STATE_BISS_CONFIGURED &&
        !scpi_realtime_component_biss_role_supported(vector.biss_role)) {
        scpi_port_set_trigger_debug_stage(202u);
        scpi_port_push_exec_error(context, "BISS_ROLE_NOT_IMPLEMENTED");
        return SCPI_RES_ERR;
    }

    if (vector.state == TRIG_STATE_IDLE && scpi_trigger_product_mode() == 1u) {
        scpi_trigger_prepare_default_seq();
        const trig_event_t config_event = {
            .type = TRIG_EVENT_CONFIGURE_SEQ,
            .payload.seq_config = {
                .seq_table = s_seq_table_buf,
                .seq_length = scpi_trigger_seq_length_or_default(),
                .seq_width = scpi_trigger_seq_width_or_default(),
            },
        };
        if (!sync_trigger_post(&config_event)) {
            scpi_port_set_trigger_debug_posted(0u);
            return SCPI_RES_ERR;
        }
    }

    const trig_event_t event = { .type = TRIG_EVENT_ARM };
    storage_manager_trace_event(2u, 10u, 1u, 0u, 0u);
    uint32_t job_id = 0u;
    (void)storage_manager_post_snapshot_job("arm", &job_id);
    scpi_port_set_trigger_debug_stage(210u);
    const bool posted = sync_trigger_post(&event);
    scpi_port_set_trigger_debug_posted(posted ? 1u : 0u);
    scpi_port_set_trigger_debug_stage(211u);
    return posted ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_trigger_disarm(scpi_t *context)
{
    const trig_event_t event = { .type = TRIG_EVENT_DISARM };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_trigger_fault(scpi_t *context)
{
    const trig_event_t event = {
        .type = TRIG_EVENT_FAULT,
        .payload.value = 100u,
    };
    storage_manager_trace_event(2u, 100u, 3u, event.payload.value, 0u);
    if (!sync_trigger_post(&event)) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    (void)storage_manager_post_fault_evidence_job(&job_id);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_trigger_source(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t pin;
    if (!scpi_port_read_u32(context, &pin)) {
        return SCPI_RES_ERR;
    }
    /* 有效源: GPIO16-19, GPIO26-29 */
    if ((pin < 16u || pin > 19u) && (pin < 26u || pin > 29u)) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = TRIG_EVENT_SET_SOURCE_PIN,
        .payload.value = pin,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_trigger_source_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.trigger_source_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_edge(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t edge;
    if (!scpi_port_read_u32(context, &edge) || edge > 1u) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = TRIG_EVENT_SET_EDGE,
        .payload.value = edge,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_trigger_edge_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, vector.edge == TRIG_EDGE_RISING ? "RISING" : "FALLING");
    SCPI_ResultUInt32(context, (uint32_t)vector.edge);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_gate(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    scpi_bool_t enable;
    if (SCPI_ParamBool(context, &enable, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = TRIG_EVENT_SET_GATE,
        .payload.value = enable ? 1u : 0u,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_trigger_gate_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultBool(context, vector.gate_enabled ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_safe(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t safe;
    if (!scpi_port_read_u32(context, &safe) || safe > 1u) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = TRIG_EVENT_SET_SAFE_STATE,
        .payload.value = safe,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_trigger_safe_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, vector.safe_state == TRIG_SAFE_ZERO ? "ZERO" : "ONE");
    SCPI_ResultUInt32(context, (uint32_t)vector.safe_state);
    return SCPI_RES_OK;
}

/* ── ENC_COUNT 命令 ── */

scpi_result_t scpi_cmd_enc_target(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t target;
    if (!scpi_port_read_u32(context, &target) || target == 0u) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = TRIG_EVENT_SET_ENC_TARGET,
        .payload.value = target,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_enc_target_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_target);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_enc_count_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_enc_a_pin(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t pin;
    if (!scpi_port_read_u32(context, &pin)) {
        return SCPI_RES_ERR;
    }

    if (pin != SYNC_IO_HW_ENC_A_PIN) {
        scpi_port_push_exec_error(context, "HW_ENC_PIN_FIXED");
        return SCPI_RES_ERR;
    }

    const trig_event_t ev = {
        .type = TRIG_EVENT_SET_ENC_PINS,
        .payload.value = SYNC_IO_HW_ENC_A_PIN |
                         (SYNC_IO_HW_ENC_B_PIN << 8) |
                         (SYNC_IO_HW_ENC_Z_PIN << 16),
    };
    return sync_trigger_post(&ev) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_enc_a_pin_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_a_pin);
    SCPI_ResultUInt32(context, vector.enc_b_pin);
    SCPI_ResultUInt32(context, vector.enc_z_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_enc_rev_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_rev_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_status_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);

    SCPI_ResultText(context, scpi_trig_mode_to_string(vector.active_mode));
    SCPI_ResultUInt32(context, (uint32_t)vector.state);
    SCPI_ResultUInt32(context, vector.trigger_source_pin);
    SCPI_ResultUInt32(context, vector.seq_index);
    SCPI_ResultUInt32(context, vector.enc_target);
    SCPI_ResultUInt32(context, vector.enc_count);
    SCPI_ResultUInt32(context, vector.trigger_count);
    SCPI_ResultUInt32(context, vector.rollover_count);
    SCPI_ResultUInt32(context, vector.error_code);
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

