#include "scpi_communication_biss_commands.h"

#include <limits.h>

#include "biss_protocol.h"
#include "distributed_config.h"
#include "scpi_port_internal.h"
#include "sync_trigger.h"

static bool scpi_communication_biss_is_armed(void)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    return vector.state == TRIG_STATE_BISS_ARMED;
}

static bool scpi_communication_biss_role_supported(trig_biss_role_t role)
{
    return role == TRIG_BISS_ROLE_TAP_MONITOR;
}

static const char *scpi_biss_role_to_string(trig_biss_role_t role)
{
    switch (role) {
    case TRIG_BISS_ROLE_TAP_MONITOR:  return "TAP";
    case TRIG_BISS_ROLE_SLAVE_TX:     return "SLAVE_TX";
    case TRIG_BISS_ROLE_MASTER_RX:    return "MASTER_RX";
    case TRIG_BISS_ROLE_BRIDGE_PROXY: return "BRIDGE";
    default:                          return "UNKNOWN";
    }
}
/* ── 协议触发 / BiSS-C 节点命令 ── */

static scpi_result_t scpi_cmd_biss_set_u32(scpi_t *context,
                                           trig_event_type_t type,
                                           uint32_t min_value,
                                           uint32_t max_value)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t value;
    if (!scpi_port_read_u32(context, &value) ||
        value < min_value ||
        value > max_value) {
        return SCPI_RES_ERR;
    }

    if (scpi_communication_biss_is_armed()) {
        scpi_port_push_exec_error(context, "BISS_ARMED");
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = type,
        .payload.value = value,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_biss_configure(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    if (scpi_communication_biss_is_armed()) {
        scpi_port_push_exec_error(context, "BISS_ARMED");
        return SCPI_RES_ERR;
    }

    const trig_event_t event = { .type = TRIG_EVENT_CONFIGURE_BISS };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_biss_role(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ROLE,
                                 (uint32_t)TRIG_BISS_ROLE_TAP_MONITOR,
                                 (uint32_t)TRIG_BISS_ROLE_BRIDGE_PROXY);
}

scpi_result_t scpi_cmd_biss_role_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, scpi_biss_role_to_string(vector.biss_role));
    SCPI_ResultUInt32(context, (uint32_t)vector.biss_role);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_device(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_DEVICE,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_device_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_device_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_clock(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CLOCK,
                                 1u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_clock_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_clock_hz);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_frame_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_FRAME_BITS,
                                 1u,
                                 BISS_PROFILE_MAX_FRAME_BITS);
}

scpi_result_t scpi_cmd_biss_frame_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_frame_bits);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_position_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_POSITION_OFFSET,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS - 1u);
}

scpi_result_t scpi_cmd_biss_position_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_position_offset);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_position_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_POSITION_BITS,
                                 1u,
                                 BISS_PROFILE_MAX_POSITION_BITS);
}

scpi_result_t scpi_cmd_biss_position_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_position_bits);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_position_modulo(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_POSITION_MODULO,
                                 1u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_position_modulo_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_position_modulo);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_sample_edge(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_EDGE,
                                 (uint32_t)BISS_SAMPLE_EDGE_RISING,
                                 (uint32_t)BISS_SAMPLE_EDGE_FALLING);
}

scpi_result_t scpi_cmd_biss_sample_edge_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context,
                    vector.biss_sample_edge == (uint32_t)BISS_SAMPLE_EDGE_RISING ?
                        "RISING" :
                        "FALLING");
    SCPI_ResultUInt32(context, vector.biss_sample_edge);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_sample_delay(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_DELAY,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_sample_delay_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_sample_delay_cycles);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_sample_scan(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_SCAN,
                                 0u,
                                 1u);
}

scpi_result_t scpi_cmd_biss_sample_scan_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultBool(context, vector.biss_sample_scan_enabled ? TRUE : FALSE);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_sample_scan_start(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_SCAN_START,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_sample_scan_start_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_start_cycles);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_sample_scan_end(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_SCAN_END,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_sample_scan_end_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_end_cycles);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_sample_scan_step(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_SCAN_STEP,
                                 1u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_sample_scan_step_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_step_cycles);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_timeout_us(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_TIMEOUT,
                                 1u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_timeout_us_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_timeout_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_anchor_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ANCHOR_OFFSET,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS - 1u);
}

scpi_result_t scpi_cmd_biss_anchor_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_anchor_offset);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_anchor_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ANCHOR_BITS,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS);
}

scpi_result_t scpi_cmd_biss_anchor_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_anchor_bits);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_anchor_mask(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ANCHOR_MASK,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_anchor_mask_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt64(context, vector.biss_anchor_mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_anchor_value(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ANCHOR_VALUE,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_anchor_value_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt64(context, vector.biss_anchor_value);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_error_bit(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ERROR_BIT,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_error_bit_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_error_bit_offset);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_warning_bit(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_WARNING_BIT,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_warning_bit_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_warning_bit_offset);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_status_gate(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_STATUS_GATE,
                                 (uint32_t)BISS_STATUS_GATE_IGNORE,
                                 (uint32_t)BISS_STATUS_GATE_BLOCK_TRIGGER);
}

scpi_result_t scpi_cmd_biss_status_gate_q(scpi_t *context)
{
    static const char *const names[] = {
        "IGNORE",
        "COUNT_ONLY",
        "BLOCK_TRIGGER",
    };
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    const uint32_t policy = vector.biss_status_gate_policy;
    SCPI_ResultText(context, policy <= (uint32_t)BISS_STATUS_GATE_BLOCK_TRIGGER ?
                                 names[policy] :
                                 "UNKNOWN");
    SCPI_ResultUInt32(context, policy);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_OFFSET,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS - 1u);
}

scpi_result_t scpi_cmd_biss_crc_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_offset);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_BITS,
                                 0u,
                                 BISS_PROFILE_MAX_CRC_BITS);
}

scpi_result_t scpi_cmd_biss_crc_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_bits);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_cover_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_COVER_OFFSET,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS - 1u);
}

scpi_result_t scpi_cmd_biss_crc_cover_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_cover_offset);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_cover_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_COVER_BITS,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS);
}

scpi_result_t scpi_cmd_biss_crc_cover_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_cover_bits);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_polynomial(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_POLYNOMIAL,
                                 0u,
                                 0xFFFFu);
}

scpi_result_t scpi_cmd_biss_crc_polynomial_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_polynomial);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_init(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_INIT,
                                 0u,
                                 0xFFFFu);
}

scpi_result_t scpi_cmd_biss_crc_init_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_init);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_xor(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_XOR,
                                 0u,
                                 0xFFFFu);
}

scpi_result_t scpi_cmd_biss_crc_xor_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_xor);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_invert(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_INVERT,
                                 0u,
                                 1u);
}

scpi_result_t scpi_cmd_biss_crc_invert_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultBool(context, vector.biss_crc_invert ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_crc_gate(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_GATE,
                                 (uint32_t)BISS_CRC_GATE_LATE_COUNT,
                                 (uint32_t)BISS_CRC_GATE_BLOCK_TRIGGER);
}

scpi_result_t scpi_cmd_biss_crc_gate_q(scpi_t *context)
{
    static const char *const names[] = {
        "LATE_COUNT",
        "BLOCK_TRIGGER",
    };
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    const uint32_t policy = vector.biss_crc_gate_policy;
    SCPI_ResultText(context, policy <= (uint32_t)BISS_CRC_GATE_BLOCK_TRIGGER ?
                                 names[policy] :
                                 "UNKNOWN");
    SCPI_ResultUInt32(context, policy);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_latency_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_LATENCY_OFFSET,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_latency_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_latency_offset_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_target(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_TARGET,
                                 0u,
                                 UINT32_MAX);
}

scpi_result_t scpi_cmd_biss_target_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_target);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_pins_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_clk_in_pin);
    SCPI_ResultUInt32(context, vector.biss_data_in_pin);
    SCPI_ResultUInt32(context, vector.biss_clk_out_pin);
    SCPI_ResultUInt32(context, vector.biss_data_out_pin);
    SCPI_ResultUInt32(context, vector.biss_pulse_in_pin);
    SCPI_ResultUInt32(context, vector.biss_pulse_out_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_biss_pulse_in(scpi_t *context)
{
    uint32_t delta = 1u;
    (void)scpi_port_read_u32(context, &delta);
    if (delta == 0u) {
        delta = 1u;
    }

    const trig_event_t event = {
        .type = TRIG_EVENT_BISS_PULSE_IN,
        .payload.value = delta,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_biss_frame_rx(scpi_t *context)
{
    uint32_t position;
    if (!scpi_port_read_u32(context, &position)) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = TRIG_EVENT_BISS_FRAME_RX,
        .payload.value = position,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_biss_crc_error(scpi_t *context)
{
    const trig_event_t event = { .type = TRIG_EVENT_BISS_CRC_ERROR };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_biss_timeout_inject(scpi_t *context)
{
    const trig_event_t event = { .type = TRIG_EVENT_BISS_TIMEOUT };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_biss_status_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, scpi_biss_role_to_string(vector.biss_role));
    SCPI_ResultUInt32(context, (uint32_t)vector.biss_role);
    SCPI_ResultText(context,
                    scpi_communication_biss_role_supported(vector.biss_role) ?
                        "OK" :
                        "NOT_IMPLEMENTED");
    SCPI_ResultUInt32(context, (uint32_t)vector.state);
    SCPI_ResultUInt32(context, vector.biss_device_id);
    SCPI_ResultUInt32(context, vector.biss_clock_hz);
    SCPI_ResultUInt32(context, vector.biss_frame_bits);
    SCPI_ResultUInt32(context, vector.biss_position_offset);
    SCPI_ResultUInt32(context, vector.biss_position_bits);
    SCPI_ResultUInt32(context, vector.biss_position_modulo);
    SCPI_ResultUInt32(context, vector.biss_target);
    SCPI_ResultUInt32(context, vector.biss_last_position);
    SCPI_ResultUInt32(context, vector.biss_last_seq);
    SCPI_ResultUInt32(context, vector.biss_frame_error_count);
    SCPI_ResultUInt32(context, vector.biss_status_block_count);
    SCPI_ResultUInt32(context, vector.biss_crc_error_count);
    SCPI_ResultUInt32(context, vector.biss_fifo_overflow_count);
    SCPI_ResultUInt32(context, vector.biss_timeout_count);
    SCPI_ResultUInt32(context, vector.biss_trigger_count);
    SCPI_ResultUInt32(context, vector.biss_pulse_in_count);
    SCPI_ResultUInt32(context, vector.biss_tx_frame_count);
    SCPI_ResultUInt32(context, vector.biss_rx_frame_count);
    SCPI_ResultUInt32(context, vector.biss_pulse_out_count);
    SCPI_ResultUInt32(context, vector.biss_active_sample_edge);
    SCPI_ResultUInt32(context, vector.biss_active_sample_delay_cycles);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_index);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_wrap_count);
    return SCPI_RES_OK;
}

