#include "trigger_fb.h"

#include <string.h>

#include "board_config.h"
#include "biss_node_io.h"
#include "biss_protocol.h"
#include "resource_arbiter.h"
#include "sync_io.h"
#include "sync_io_mode_biss_tap.h"
#include "sync_io_hw_profile.h"
#include "sync_io_mode_enc_count.h"
#include "sync_io_mode_seq_step.h"
#include "trigger_resource_map.h"

/* ── 内部 ── */

#define TRIG_CFG_SEQ_TABLE   (1u << 0)
#define TRIG_CFG_SEQ_LENGTH  (1u << 1)
#define TRIG_CFG_SEQ_WIDTH   (1u << 2)

#define FB_OWNER_SEQ_STEP  "sync.seq_step"
#define FB_OWNER_ENC_COUNT "sync.enc_count"
#define FB_OWNER_BISS_TAP  "sync.biss_tap"
#define FB_SYNC_CLOCK_RESOURCES \
    (RESOURCE_ARBITER_RESOURCE_PIO2 | RESOURCE_ARBITER_RESOURCE_AUX)

typedef enum {
    FB_OK       = 0,
    FB_IGNORED  = 1,
    FB_ERROR    = 2,
} fb_result_t;

typedef fb_result_t (*fb_handler_t)(trigger_vector_t *vector,
                                    const trig_event_t *event);

static bool fb_valid_enc_pin_group(const trigger_vector_t *vector)
{
    if (vector == NULL) {
        return false;
    }

    return sync_io_hw_enc_pins_valid(vector->enc_a_pin,
                                     vector->enc_b_pin,
                                     vector->enc_z_pin);
}

static uint32_t fb_seq_resources(void)
{
    return trigger_resource_map_for_mode(SYNC_IO_MODE_ID_SEQ_STEP);
}

static uint32_t fb_enc_resources(void)
{
    return trigger_resource_map_for_mode(SYNC_IO_MODE_ID_ENC_COUNT);
}

static uint32_t fb_biss_resources(void)
{
    return trigger_resource_map_for_mode(SYNC_IO_MODE_ID_BISS_TAP);
}

static bool fb_biss_tap_is_running(void)
{
    const sync_io_mode_ops_t *ops =
        sync_io_mode_get_ops(SYNC_IO_MODE_ID_BISS_TAP);
    return ops != NULL && ops->is_running != NULL && ops->is_running();
}

static trig_error_code_t fb_sync_clock_error_code(void)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    if ((snapshot.last_conflict_resources & FB_SYNC_CLOCK_RESOURCES) != 0u) {
        return TRIG_ERROR_RESOURCE_CONFLICT;
    }
    return TRIG_ERROR_IO_ARM_FAILED;
}

static fb_result_t fb_start_sync_clock(trigger_vector_t *vector)
{
    if (vector == NULL) {
        return FB_ERROR;
    }
    if (vector->sync_clock_hz == 0u) {
        sync_io_status_t status;
        sync_io_get_status(&status);
        vector->sync_clock_enabled = status.sync_clock_running;
        vector->sync_clock_hz = status.sync_clock_hz;
        vector->error_code = TRIG_ERROR_IO_ARM_FAILED;
        return FB_ERROR;
    }

    if (!sync_io_start_clock(vector->sync_clock_hz)) {
        sync_io_status_t status;
        sync_io_get_status(&status);
        vector->sync_clock_enabled = status.sync_clock_running;
        vector->sync_clock_hz = status.sync_clock_hz;
        vector->error_code = fb_sync_clock_error_code();
        return FB_ERROR;
    }

    vector->sync_clock_enabled = true;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

static void fb_release_running_io(trigger_vector_t *vector)
{
    (void)vector;

    if (sync_io_seq_step_is_running()) {
        sync_io_seq_step_disarm();
    }
    resource_arbiter_release_owned(fb_seq_resources(), FB_OWNER_SEQ_STEP);

    if (sync_io_enc_count_is_running()) {
        sync_io_enc_count_disarm();
    }
    resource_arbiter_release_owned(fb_enc_resources(), FB_OWNER_ENC_COUNT);

    if (biss_node_io_is_running() || fb_biss_tap_is_running()) {
        biss_node_io_disarm();
    }
    resource_arbiter_release_owned(fb_biss_resources(), FB_OWNER_BISS_TAP);
}

static fb_result_t fb_reset_all(trigger_vector_t *vector,
                                const trig_event_t *event)
{
    (void)event;

    sync_io_stop_clock();
    sync_io_stop_capture();
    fb_release_running_io(vector);

    vector->state = TRIG_STATE_IDLE;
    vector->active_mode = TRIG_MODE_IDLE;
    vector->error_code = TRIG_ERROR_NONE;
    vector->sync_clock_enabled = false;
    return FB_OK;
}

static bool fb_valid_biss_config(const trigger_vector_t *vector)
{
    if (vector == NULL) {
        return false;
    }

    if (vector->protocol != TRIG_PROTOCOL_BISS_C ||
        vector->biss_role != TRIG_BISS_ROLE_TAP_MONITOR ||
        vector->biss_clock_hz == 0u) {
        return false;
    }

    biss_profile_t profile;
    if (!biss_node_io_make_profile(vector, &profile)) {
        return false;
    }
    if (biss_profile_validate(&profile) != BISS_PROFILE_OK) {
        return false;
    }

    if (vector->biss_sample_scan_enabled != 0u) {
        if (vector->biss_sample_scan_start_cycles >
                vector->biss_sample_scan_end_cycles ||
            vector->biss_sample_scan_step_cycles == 0u ||
            vector->biss_sample_scan_end_cycles >= profile.pio_cycles_per_bit) {
            return false;
        }
    }

    return true;
}

/* ── 即时脉冲处理器（IDLE / SEQ_CONFIGURED / FAULT 状态共用）── */

static fb_result_t fb_instant_cmd(trigger_vector_t *vector,
                                   const trig_event_t *event)
{
    switch (event->type) {
    case TRIG_EVENT_SET_TRIGGER_WIDTH:
        vector->trigger_width_us = event->payload.value;
        break;
    case TRIG_EVENT_FIRE_TRIGGER:
        (void)sync_io_fire_pulse_us(vector->trigger_width_us);
        break;
    case TRIG_EVENT_SET_PULSE_WIDTH:
        vector->pulse_width_us = event->payload.value;
        break;
    case TRIG_EVENT_FIRE_PULSE:
        (void)sync_io_fire_pulse_out_us(vector->pulse_width_us);
        break;
    case TRIG_EVENT_SET_RJ45_TRIGGER_WIDTH:
        vector->rj45_trigger_width_us = event->payload.value;
        vector->marker_width_us = event->payload.value;
        break;
    case TRIG_EVENT_FIRE_RJ45_TRIGGER:
        (void)sync_io_fire_rj45_trigger_us(vector->rj45_trigger_width_us);
        break;
    case TRIG_EVENT_SET_SAMPLE_RATE:
        vector->capture_sample_hz = event->payload.value;
        (void)sync_io_start_capture(vector->capture_sample_hz);
        break;
    case TRIG_EVENT_SET_SAMPLE_STATE:
        if (event->payload.value != 0u) {
            (void)sync_io_start_capture(vector->capture_sample_hz);
        } else {
            sync_io_stop_capture();
        }
        break;
    case TRIG_EVENT_SET_CLOCK_FREQ:
    {
        const uint32_t previous_hz = vector->sync_clock_hz;
        vector->sync_clock_hz = event->payload.value;
        if (vector->sync_clock_enabled) {
            if (fb_start_sync_clock(vector) != FB_OK) {
                if (vector->sync_clock_enabled) {
                    vector->sync_clock_hz = previous_hz;
                }
                return FB_ERROR;
            }
        }
        break;
    }
    case TRIG_EVENT_SET_CLOCK_STATE:
        if (event->payload.value != 0u) {
            if (fb_start_sync_clock(vector) != FB_OK) {
                return FB_ERROR;
            }
        } else {
            sync_io_stop_clock();
            vector->sync_clock_enabled = false;
            vector->error_code = TRIG_ERROR_NONE;
        }
        break;
    /* ── 触发源 / 边沿 / 门控 / 安全态 ── */
    case TRIG_EVENT_SET_SOURCE_PIN:
        vector->trigger_source_pin = event->payload.value;
        break;
    case TRIG_EVENT_SET_EDGE:
        vector->edge = (trig_edge_t)event->payload.value;
        break;
    case TRIG_EVENT_SET_GATE:
        vector->gate_enabled = (event->payload.value != 0u);
        break;
    case TRIG_EVENT_SET_SAFE_STATE:
        vector->safe_state = (trig_safe_state_t)event->payload.value;
        break;
    /* ── ENC_COUNT ── */
    case TRIG_EVENT_SET_ENC_TARGET:
        vector->enc_target = event->payload.value;
        break;
    case TRIG_EVENT_SET_ENC_PINS:
    {
        const uint32_t next_a_pin = event->payload.value & 0xFFu;
        const uint32_t next_b_pin = (event->payload.value >> 8) & 0xFFu;
        const uint32_t next_z_pin = (event->payload.value >> 16) & 0xFFu;
        if (!sync_io_hw_enc_pins_valid(next_a_pin, next_b_pin, next_z_pin)) {
            vector->error_code = TRIG_ERROR_INVALID_ENC_PINS;
            return FB_ERROR;
        }
        vector->enc_a_pin = next_a_pin;
        vector->enc_b_pin = next_b_pin;
        vector->enc_z_pin = next_z_pin;
        break;
    }
    case TRIG_EVENT_ENC_Z_PULSE:
        vector->enc_rev_count++;
        break;
    /* ── PCNT ── */
    case TRIG_EVENT_SET_PCNT_DECODE:
        vector->enc_decode = (trig_pcnt_decode_t)event->payload.value;
        break;
    case TRIG_EVENT_SET_PCNT_DIR:
        vector->enc_dir = (trig_pcnt_dir_t)event->payload.value;
        break;
    case TRIG_EVENT_SET_PCNT_FILTER:
        vector->enc_filter_ns = event->payload.value;
        break;
    case TRIG_EVENT_SET_PCNT_GATE:
        vector->enc_gate_enabled = (event->payload.value != 0u);
        break;
    case TRIG_EVENT_SET_PCNT_CMP:
        vector->enc_cmp_pulse_ns = event->payload.value;
        break;
    case TRIG_EVENT_SET_PCNT_PRESET:
        vector->enc_preset = event->payload.value;
        break;
    case TRIG_EVENT_PCNT_CLEAR:
        vector->enc_total += vector->enc_count;   /* 先累计本次清零前的值 */
        vector->enc_count = 0u;                    /* 再清零 */
        break;
    /* ── 协议触发 / BiSS-C 节点 ── */
    case TRIG_EVENT_SET_BISS_ROLE:
        vector->biss_role = (trig_biss_role_t)event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_DEVICE:
        vector->biss_device_id = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CLOCK:
        vector->biss_clock_hz = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_FRAME_BITS:
        vector->biss_frame_bits = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_POSITION_OFFSET:
        vector->biss_position_offset = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_POSITION_BITS:
        vector->biss_position_bits = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_POSITION_MODULO:
        vector->biss_position_modulo = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_SAMPLE_EDGE:
        vector->biss_sample_edge = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_SAMPLE_DELAY:
        vector->biss_sample_delay_cycles = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_SAMPLE_SCAN:
        vector->biss_sample_scan_enabled = event->payload.value != 0u ? 1u : 0u;
        break;
    case TRIG_EVENT_SET_BISS_SAMPLE_SCAN_START:
        vector->biss_sample_scan_start_cycles = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_SAMPLE_SCAN_END:
        vector->biss_sample_scan_end_cycles = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_SAMPLE_SCAN_STEP:
        vector->biss_sample_scan_step_cycles = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_TIMEOUT:
        vector->biss_timeout_us = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_ANCHOR_OFFSET:
        vector->biss_anchor_offset = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_ANCHOR_BITS:
        vector->biss_anchor_bits = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_ANCHOR_MASK:
        vector->biss_anchor_mask = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_ANCHOR_VALUE:
        vector->biss_anchor_value = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_ERROR_BIT:
        vector->biss_error_bit_offset = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_WARNING_BIT:
        vector->biss_warning_bit_offset = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_STATUS_GATE:
        vector->biss_status_gate_policy = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_OFFSET:
        vector->biss_crc_offset = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_BITS:
        vector->biss_crc_bits = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_COVER_OFFSET:
        vector->biss_crc_cover_offset = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_COVER_BITS:
        vector->biss_crc_cover_bits = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_POLYNOMIAL:
        vector->biss_crc_polynomial = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_INIT:
        vector->biss_crc_init = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_XOR:
        vector->biss_crc_xor = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_INVERT:
        vector->biss_crc_invert = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_CRC_GATE:
        vector->biss_crc_gate_policy = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_LATENCY_OFFSET:
        vector->biss_latency_offset_ns = event->payload.value;
        break;
    case TRIG_EVENT_SET_BISS_TARGET:
        vector->biss_target = event->payload.value;
        break;
    case TRIG_EVENT_BISS_CRC_ERROR:
        vector->biss_crc_error_count++;
        break;
    case TRIG_EVENT_BISS_TIMEOUT:
        vector->biss_timeout_count++;
        break;
    case TRIG_EVENT_RESET:
        return fb_reset_all(vector, event);
    default:
        return FB_IGNORED;
    }
    return FB_OK;
}

/* ── IDLE → SEQ_STEP 配置 ── */

static fb_result_t fb_idle_configure_seq(trigger_vector_t *vector,
                                          const trig_event_t *event)
{
    if (event->payload.seq_config.seq_table == NULL ||
        event->payload.seq_config.seq_length == 0u ||
        event->payload.seq_config.seq_length > TRIG_SEQ_TABLE_MAX ||
        event->payload.seq_config.seq_width == 0u ||
        event->payload.seq_config.seq_width > TRIG_SEQ_WIDTH_MAX) {
        vector->error_code = TRIG_ERROR_INVALID_SEQ_CONFIG;
        return FB_ERROR;
    }

    vector->seq_length = event->payload.seq_config.seq_length;
    vector->seq_output_width = event->payload.seq_config.seq_width;
    memcpy(vector->seq_table,
           event->payload.seq_config.seq_table,
           vector->seq_length * sizeof(uint32_t));
    vector->active_mode = TRIG_MODE_SEQ_STEP;
    vector->seq_index = 0u;
    vector->state = TRIG_STATE_SEQ_CONFIGURED;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

/* ── SEQ_CONFIGURED → ARM ── */

static fb_result_t fb_seq_configured_arm(trigger_vector_t *vector,
                                          const trig_event_t *event)
{
    (void)event;

    const uint32_t resources = fb_seq_resources();
    if (resources == 0u ||
        !resource_arbiter_acquire_owned(resources, FB_OWNER_SEQ_STEP)) {
        vector->error_code = TRIG_ERROR_RESOURCE_CONFLICT;
        return FB_ERROR;
    }

    const sync_io_seq_step_mode_config_t config = {
        .seq_table = vector->seq_table,
        .seq_length = vector->seq_length,
        .seq_width = vector->seq_output_width,
        .trigger_pin = vector->trigger_source_pin,
        .edge = (sync_io_edge_t)vector->edge,
        .gate_enabled = vector->gate_enabled,
    };
    const sync_io_mode_ops_t *ops =
        sync_io_mode_get_ops(SYNC_IO_MODE_ID_SEQ_STEP);

    if (ops == NULL || ops->arm == NULL || !ops->arm(&config)) {
        resource_arbiter_release_owned(resources, FB_OWNER_SEQ_STEP);
        vector->error_code = TRIG_ERROR_IO_ARM_FAILED;
        return FB_ERROR;
    }

    vector->seq_index = 0u;
    vector->rollover_count = 0u;
    vector->state = TRIG_STATE_SEQ_ARMED;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

/* ── SEQ_CONFIGURED → 重新配置 ── */

static fb_result_t fb_seq_configured_reconfigure(trigger_vector_t *vector,
                                                  const trig_event_t *event)
{
    if (event->type != TRIG_EVENT_CONFIGURE_SEQ) {
        return FB_IGNORED;
    }
    vector->active_mode = TRIG_MODE_IDLE;
    vector->state = TRIG_STATE_IDLE;
    return fb_idle_configure_seq(vector, event);
}

/* ── SEQ_ARMED → 周期服务 ── */

static fb_result_t fb_seq_armed_service(trigger_vector_t *vector,
                                         const trig_event_t *event)
{
    (void)event;

    if (!sync_io_seq_step_is_running()) {
        vector->state = TRIG_STATE_FAULT;
        vector->error_code = TRIG_ERROR_IO_LOST;
        vector->fault_timestamp_ms = 0u;  /* TODO: osal_tick_ms() */
        return FB_ERROR;
    }

    vector->seq_index = sync_io_seq_step_get_index();
    vector->rollover_count = sync_io_seq_step_get_rollover_count();

    /* 当前 SEQ_STEP 表项与触发边沿 1:1, seq_idx 即已执行步数. */
    vector->trigger_count =
        (uint32_t)(vector->rollover_count * vector->seq_length + vector->seq_index);
    vector->output_count = vector->trigger_count;

    return FB_OK;
}

/* ── SEQ_ARMED → DISARM ── */

static fb_result_t fb_seq_armed_disarm(trigger_vector_t *vector,
                                        const trig_event_t *event)
{
    (void)event;

    sync_io_seq_step_disarm();
    resource_arbiter_release_owned(fb_seq_resources(), FB_OWNER_SEQ_STEP);

    vector->state = TRIG_STATE_IDLE;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

/* ── 武装态拒绝写入 ── */

static fb_result_t fb_seq_armed_reject(trigger_vector_t *vector,
                                        const trig_event_t *event)
{
    (void)vector;
    (void)event;
    return FB_IGNORED;
}

static fb_result_t fb_force_fault(trigger_vector_t *vector,
                                  const trig_event_t *event)
{
    fb_release_running_io(vector);

    vector->state = TRIG_STATE_FAULT;
    vector->error_code = event->payload.value != 0u ? event->payload.value : TRIG_ERROR_FORCED_FAULT;
    vector->fault_timestamp_ms = 0u;
    return FB_ERROR;
}

/* ── 协议触发 / BiSS-C 节点配置 ── */

static fb_result_t fb_idle_configure_biss(trigger_vector_t *vector,
                                           const trig_event_t *event)
{
    (void)event;

    if (!fb_valid_biss_config(vector)) {
        vector->error_code = TRIG_ERROR_INVALID_BISS_CONFIG;
        return FB_ERROR;
    }

    vector->active_mode = TRIG_MODE_PROTOCOL_TRIGGER;
    vector->state = TRIG_STATE_BISS_CONFIGURED;
    vector->supported_modes |= (1u << TRIG_MODE_PROTOCOL_TRIGGER);
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

static fb_result_t fb_biss_configured_arm(trigger_vector_t *vector,
                                           const trig_event_t *event)
{
    (void)event;

    if (!fb_valid_biss_config(vector)) {
        vector->error_code = TRIG_ERROR_INVALID_BISS_CONFIG;
        return FB_ERROR;
    }

    const uint32_t resources = fb_biss_resources();
    if (resources == 0u ||
        !resource_arbiter_acquire_owned(resources, FB_OWNER_BISS_TAP)) {
        vector->error_code = TRIG_ERROR_RESOURCE_CONFLICT;
        return FB_ERROR;
    }

    vector->biss_active_sample_edge = vector->biss_sample_edge;
    vector->biss_active_sample_delay_cycles = vector->biss_sample_delay_cycles;
    vector->biss_sample_scan_index = 0u;
    vector->biss_sample_scan_wrap_count = 0u;

    if (!biss_node_io_arm(vector)) {
        resource_arbiter_release_owned(resources, FB_OWNER_BISS_TAP);
        vector->error_code = TRIG_ERROR_IO_ARM_FAILED;
        return FB_ERROR;
    }

    vector->biss_pulse_in_count = 0u;
    vector->biss_pulse_out_count = 0u;
    vector->biss_tx_frame_count = 0u;
    vector->biss_rx_frame_count = 0u;
    vector->biss_frame_error_count = 0u;
    vector->biss_crc_error_count = 0u;
    vector->biss_status_block_count = 0u;
    vector->biss_fifo_overflow_count = 0u;
    vector->biss_timeout_count = 0u;
    vector->biss_trigger_count = 0u;
    vector->trigger_count = 0u;
    vector->output_count = 0u;
    vector->state = TRIG_STATE_BISS_ARMED;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

static fb_result_t fb_biss_armed_disarm(trigger_vector_t *vector,
                                         const trig_event_t *event)
{
    (void)event;

    biss_node_io_disarm();
    resource_arbiter_release_owned(fb_biss_resources(), FB_OWNER_BISS_TAP);
    vector->state = TRIG_STATE_IDLE;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

static fb_result_t fb_biss_armed_pulse_in(trigger_vector_t *vector,
                                           const trig_event_t *event)
{
    const uint32_t pulse_delta = event->payload.value != 0u ? event->payload.value : 1u;

    vector->biss_pulse_in_count += pulse_delta;
    vector->biss_last_seq++;
    vector->biss_last_position = vector->biss_pulse_in_count;
    vector->biss_tx_frame_count++;
    vector->trigger_count = vector->biss_pulse_in_count;
    return FB_OK;
}

static fb_result_t fb_biss_armed_frame_rx(trigger_vector_t *vector,
                                           const trig_event_t *event)
{
    return biss_node_io_process_position(vector, event->payload.value) ?
               FB_OK :
               FB_ERROR;
}

static bool fb_biss_apply_sample_scan_step(void)
{
    sync_io_biss_tap_config_t config;
    if (!biss_node_io_get_tap_config(&config)) {
        return false;
    }

    const sync_io_mode_ops_t *ops =
        sync_io_mode_get_ops(SYNC_IO_MODE_ID_BISS_TAP);
    if (ops == NULL || ops->arm == NULL || !ops->arm(&config)) {
        return false;
    }

    biss_node_io_sample_scan_rearm_succeeded();
    return true;
}

/* ── ENC_COUNT 配置 ── */

static fb_result_t fb_idle_configure_enc(trigger_vector_t *vector,
                                          const trig_event_t *event)
{
    (void)event;

    if (vector->enc_target == 0u) {
        vector->error_code = TRIG_ERROR_INVALID_ENC_TARGET;
        return FB_ERROR;
    }
    if (!fb_valid_enc_pin_group(vector)) {
        vector->error_code = TRIG_ERROR_INVALID_ENC_PINS;
        return FB_ERROR;
    }

    vector->active_mode = TRIG_MODE_ENC_COUNT;
    vector->enc_count = 0u;
    vector->state = TRIG_STATE_ENC_CONFIGURED;
    vector->supported_modes |= (1u << TRIG_MODE_ENC_COUNT);
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

/* ── ENC_CONFIGURED → ARM ── */

static fb_result_t fb_enc_configured_arm(trigger_vector_t *vector,
                                          const trig_event_t *event)
{
    (void)event;

    const uint32_t resources = fb_enc_resources();
    if (resources == 0u ||
        !resource_arbiter_acquire_owned(resources, FB_OWNER_ENC_COUNT)) {
        vector->error_code = TRIG_ERROR_RESOURCE_CONFLICT;
        return FB_ERROR;
    }

    const sync_io_enc_count_mode_config_t config = {
        .target = vector->enc_target,
        .in_pin_base = vector->enc_a_pin,
        .output_pin = SYNC_IO_HW_TRIG_OUT_PIN,
    };
    const sync_io_mode_ops_t *ops =
        sync_io_mode_get_ops(SYNC_IO_MODE_ID_ENC_COUNT);

    if (ops == NULL || ops->arm == NULL || !ops->arm(&config)) {
        resource_arbiter_release_owned(resources, FB_OWNER_ENC_COUNT);
        vector->error_code = TRIG_ERROR_IO_ARM_FAILED;
        return FB_ERROR;
    }

    vector->enc_count = 0u;
    vector->state = TRIG_STATE_ENC_ARMED;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

/* ── ENC_ARMED → 周期服务 ── */

static fb_result_t fb_enc_armed_service(trigger_vector_t *vector,
                                         const trig_event_t *event)
{
    (void)event;

    if (!sync_io_enc_count_is_running()) {
        vector->state = TRIG_STATE_FAULT;
        vector->error_code = TRIG_ERROR_IO_LOST;
        return FB_ERROR;
    }

    vector->enc_count = sync_io_enc_count_get_count();
    vector->enc_total = vector->enc_count + vector->enc_rev_count * vector->enc_target;
    return FB_OK;
}

static fb_result_t fb_runtime_sample(trigger_vector_t *vector,
                                      const trig_event_t *event)
{
    if (vector->state == TRIG_STATE_SEQ_ARMED) {
        return fb_seq_armed_service(vector, event);
    }

    if (vector->state == TRIG_STATE_ENC_ARMED) {
        return fb_enc_armed_service(vector, event);
    }

    if (vector->state == TRIG_STATE_BISS_ARMED) {
        (void)event;
        const biss_node_io_poll_result_t poll_result =
            biss_node_io_poll_runtime(vector);
        if (poll_result == BISS_NODE_IO_POLL_SCAN_STEP) {
            if (!fb_biss_apply_sample_scan_step()) {
                vector->state = TRIG_STATE_FAULT;
                vector->error_code = TRIG_ERROR_IO_ARM_FAILED;
                return FB_ERROR;
            }
            return FB_OK;
        }
        if (poll_result == BISS_NODE_IO_POLL_REARM_FAILED) {
            vector->state = TRIG_STATE_FAULT;
            vector->error_code = TRIG_ERROR_IO_ARM_FAILED;
            return FB_ERROR;
        }
        if (poll_result != BISS_NODE_IO_POLL_OK) {
            vector->state = TRIG_STATE_FAULT;
            vector->error_code = TRIG_ERROR_IO_LOST;
            return FB_ERROR;
        }
        return FB_OK;
    }

    return FB_IGNORED;
}

/* ── ENC_ARMED → DISARM ── */

static fb_result_t fb_enc_armed_disarm(trigger_vector_t *vector,
                                        const trig_event_t *event)
{
    (void)event;

    sync_io_enc_count_disarm();
    resource_arbiter_release_owned(fb_enc_resources(), FB_OWNER_ENC_COUNT);

    vector->state = TRIG_STATE_IDLE;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

/* ── FAULT → 清除 ── */

static fb_result_t fb_fault_clear(trigger_vector_t *vector,
                                   const trig_event_t *event)
{
    (void)event;

    fb_release_running_io(vector);

    vector->state = TRIG_STATE_IDLE;
    vector->active_mode = TRIG_MODE_IDLE;
    vector->error_code = TRIG_ERROR_NONE;
    return FB_OK;
}

/* ── ECC 表 ── */

typedef struct {
    trig_state_t       state;
    trig_event_type_t  event;
    fb_handler_t       handler;
} ecc_entry_t;

static const ecc_entry_t s_ecc_table[] = {
    /* IDLE */
    { TRIG_STATE_IDLE, TRIG_EVENT_CONFIGURE_SEQ,    fb_idle_configure_seq },
    { TRIG_STATE_IDLE, TRIG_EVENT_CONFIGURE_ENC,    fb_idle_configure_enc },
    { TRIG_STATE_IDLE, TRIG_EVENT_CONFIGURE_BISS,   fb_idle_configure_biss },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_SOURCE_PIN,   fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_EDGE,         fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_GATE,         fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_SAFE_STATE,   fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_ENC_TARGET,   fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_ENC_PINS,     fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_PCNT_DECODE,  fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_PCNT_DIR,     fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_PCNT_FILTER,  fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_PCNT_GATE,    fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_PCNT_CMP,     fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_PCNT_PRESET,  fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_PCNT_CLEAR,       fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_ROLE,    fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_DEVICE,  fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CLOCK,   fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_FRAME_BITS, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_POSITION_OFFSET, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_POSITION_BITS, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_POSITION_MODULO, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_SAMPLE_EDGE, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_SAMPLE_DELAY, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_SAMPLE_SCAN, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_START, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_END, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_STEP, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_TIMEOUT, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_ANCHOR_OFFSET, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_ANCHOR_BITS, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_ANCHOR_MASK, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_ANCHOR_VALUE, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_ERROR_BIT, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_WARNING_BIT, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_STATUS_GATE, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_OFFSET, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_BITS, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_COVER_OFFSET, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_COVER_BITS, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_POLYNOMIAL, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_INIT, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_XOR, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_INVERT, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_CRC_GATE, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_LATENCY_OFFSET, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_BISS_TARGET,  fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_RESET,             fb_reset_all },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_TRIGGER_WIDTH, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_FIRE_TRIGGER,      fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_PULSE_WIDTH,   fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_FIRE_PULSE,        fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_RJ45_TRIGGER_WIDTH, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_FIRE_RJ45_TRIGGER,      fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_SAMPLE_RATE,   fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_SAMPLE_STATE,  fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_CLOCK_FREQ,    fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_CLOCK_STATE,   fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_FAULT,             fb_force_fault },

    /* SEQ_CONFIGURED */
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_ARM,              fb_seq_configured_arm },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_DISARM,           fb_seq_armed_disarm },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_CONFIGURE_SEQ,    fb_seq_configured_reconfigure },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_SOURCE_PIN,   fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_EDGE,         fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_GATE,         fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_SAFE_STATE,   fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_RESET,            fb_reset_all },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_TRIGGER_WIDTH, fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_FIRE_TRIGGER,     fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_PULSE_WIDTH,  fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_FIRE_PULSE,       fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_RJ45_TRIGGER_WIDTH, fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_FIRE_RJ45_TRIGGER,      fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_SAMPLE_RATE,  fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_SAMPLE_STATE, fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_CLOCK_FREQ,   fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_CLOCK_STATE,  fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_FAULT,            fb_force_fault },

    /* SEQ_ARMED */
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_DISARM,           fb_seq_armed_disarm },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_DMA_ROLLOVER,     fb_seq_armed_service },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_RUNTIME_SAMPLE,   fb_runtime_sample },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_CONFIGURE_SEQ,    fb_seq_armed_reject },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_SET_TRIGGER_WIDTH, fb_seq_armed_reject },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_FIRE_TRIGGER,     fb_seq_armed_reject },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_FIRE_PULSE,       fb_seq_armed_reject },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_FIRE_RJ45_TRIGGER, fb_seq_armed_reject },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_RESET,            fb_reset_all },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_FAULT,            fb_force_fault },

    /* FAULT */
    { TRIG_STATE_FAULT, TRIG_EVENT_CLEAR_FAULT, fb_fault_clear },
    { TRIG_STATE_FAULT, TRIG_EVENT_DISARM,      fb_fault_clear },
    { TRIG_STATE_FAULT, TRIG_EVENT_RESET,       fb_reset_all },

    /* BISS_CONFIGURED */
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_ARM,                     fb_biss_configured_arm },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_DISARM,                  fb_biss_armed_disarm },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_CONFIGURE_BISS,          fb_idle_configure_biss },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_ROLE,           fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_DEVICE,         fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CLOCK,          fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_FRAME_BITS,     fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_POSITION_OFFSET, fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_POSITION_BITS,  fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_POSITION_MODULO, fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_SAMPLE_EDGE,     fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_SAMPLE_DELAY,    fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_SAMPLE_SCAN,      fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_START, fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_END,  fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_STEP, fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_TIMEOUT,         fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_ANCHOR_OFFSET,   fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_ANCHOR_BITS,     fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_ANCHOR_MASK,     fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_ANCHOR_VALUE,    fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_ERROR_BIT,       fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_WARNING_BIT,     fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_STATUS_GATE,     fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_OFFSET,      fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_BITS,        fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_COVER_OFFSET, fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_COVER_BITS,  fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_POLYNOMIAL,  fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_INIT,        fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_XOR,         fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_INVERT,      fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_CRC_GATE,        fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_LATENCY_OFFSET,  fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_SET_BISS_TARGET,         fb_instant_cmd },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_RESET,                   fb_reset_all },
    { TRIG_STATE_BISS_CONFIGURED, TRIG_EVENT_FAULT,                   fb_force_fault },

    /* BISS_ARMED */
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_DISARM,         fb_biss_armed_disarm },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_RUNTIME_SAMPLE, fb_runtime_sample },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_BISS_PULSE_IN,  fb_biss_armed_pulse_in },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_BISS_FRAME_RX,  fb_biss_armed_frame_rx },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_BISS_CRC_ERROR, fb_instant_cmd },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_BISS_TIMEOUT,   fb_instant_cmd },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_ROLE,  fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_DEVICE, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CLOCK, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_FRAME_BITS, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_POSITION_OFFSET, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_POSITION_BITS, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_POSITION_MODULO, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_SAMPLE_EDGE, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_SAMPLE_DELAY, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_SAMPLE_SCAN, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_START, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_END, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_SAMPLE_SCAN_STEP, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_TIMEOUT, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_ANCHOR_OFFSET, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_ANCHOR_BITS, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_ANCHOR_MASK, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_ANCHOR_VALUE, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_ERROR_BIT, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_WARNING_BIT, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_STATUS_GATE, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_OFFSET, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_BITS, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_COVER_OFFSET, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_COVER_BITS, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_POLYNOMIAL, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_INIT, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_XOR, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_INVERT, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_CRC_GATE, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_LATENCY_OFFSET, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_SET_BISS_TARGET, fb_seq_armed_reject },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_RESET,          fb_reset_all },
    { TRIG_STATE_BISS_ARMED, TRIG_EVENT_FAULT,          fb_force_fault },

    /* ENC_CONFIGURED */
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_ARM,              fb_enc_configured_arm },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_DISARM,           fb_enc_armed_disarm },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_CONFIGURE_ENC,    fb_idle_configure_enc },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_SET_ENC_TARGET,   fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_SET_ENC_PINS,     fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_SET_PCNT_DECODE,  fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_SET_PCNT_DIR,     fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_SET_PCNT_FILTER,  fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_SET_PCNT_GATE,    fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_SET_PCNT_CMP,     fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_SET_PCNT_PRESET,  fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_RESET,           fb_reset_all },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_FAULT,           fb_force_fault },

    /* ENC_ARMED */
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_DISARM,           fb_enc_armed_disarm },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_DMA_ROLLOVER,     fb_enc_armed_service },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_RUNTIME_SAMPLE,   fb_runtime_sample },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_ENC_Z_PULSE,      fb_instant_cmd },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_PCNT_CLEAR,       fb_instant_cmd },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_SET_ENC_TARGET,   fb_instant_cmd },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_RESET,            fb_reset_all },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_FAULT,            fb_force_fault },
};
#define TRIG_ECC_TABLE_COUNT \
    (sizeof(s_ecc_table) / sizeof(s_ecc_table[0]))

/* ── 公共接口 ── */

bool trigger_fb_init(trigger_vector_t *vector)
{
    if (vector == NULL) {
        return false;
    }

    memset(vector, 0, sizeof(*vector));
    vector->initialized = true;
    vector->state = TRIG_STATE_IDLE;
    vector->active_mode = TRIG_MODE_IDLE;
    vector->supported_modes = (1u << TRIG_MODE_SEQ_STEP);
    vector->trigger_source_pin = 16u;    /* GPIO16 = TRIG_IN */
    vector->edge = TRIG_EDGE_RISING;
    vector->gate_enabled = false;
    vector->safe_state = TRIG_SAFE_ZERO;
    vector->enc_a_pin = SYNC_IO_HW_ENC_A_PIN;
    vector->enc_b_pin = SYNC_IO_HW_ENC_B_PIN;
    vector->enc_z_pin = SYNC_IO_HW_ENC_Z_PIN;
    vector->enc_decode = TRIG_PCNT_DECODE_QUAD_1X;
    vector->enc_dir = TRIG_PCNT_DIR_CW;
    vector->enc_z_enabled = true;
    vector->enc_gate_enabled = false;
    vector->enc_filter_ns = 0u;
    vector->enc_preset = 0u;
    vector->enc_cmp_pulse_ns = 67u;   /* ~10 PIO cycles */
    vector->protocol = TRIG_PROTOCOL_BISS_C;
    vector->biss_role = TRIG_BISS_ROLE_TAP_MONITOR;
    vector->biss_device_id = 0u;
    vector->biss_phase = 0u;
    vector->biss_clock_hz = 1000000u;
    vector->biss_frame_bits = 48u;
    vector->biss_position_offset = 4u;
    vector->biss_position_bits = 32u;
    vector->biss_position_modulo = UINT32_MAX;
    vector->biss_anchor_offset = 0u;
    vector->biss_anchor_bits = 2u;
    vector->biss_anchor_mask = 0x3u;
    vector->biss_anchor_value = 0x2u;
    vector->biss_sample_edge = BISS_SAMPLE_EDGE_FALLING;
    vector->biss_sample_delay_cycles = 25u;
    vector->biss_sample_scan_enabled = 0u;
    vector->biss_sample_scan_start_cycles = 0u;
    vector->biss_sample_scan_end_cycles = 31u;
    vector->biss_sample_scan_step_cycles = 1u;
    vector->biss_active_sample_edge = vector->biss_sample_edge;
    vector->biss_active_sample_delay_cycles = vector->biss_sample_delay_cycles;
    vector->biss_timeout_us = 20u;
    vector->biss_error_bit_offset = 36u;
    vector->biss_warning_bit_offset = 37u;
    vector->biss_status_gate_policy = BISS_STATUS_GATE_BLOCK_TRIGGER;
    vector->biss_crc_offset = 38u;
    vector->biss_crc_bits = 6u;
    vector->biss_crc_cover_offset = 4u;
    vector->biss_crc_cover_bits = 34u;
    vector->biss_crc_polynomial = 0x03u;
    vector->biss_crc_init = 0u;
    vector->biss_crc_xor = 0u;
    vector->biss_crc_invert = 1u;
    vector->biss_crc_gate_policy = BISS_CRC_GATE_LATE_COUNT;
    vector->biss_target = 0u;
    vector->biss_clk_in_pin = SYNC_IO_HW_BISS_CLK_IN_PIN;
    vector->biss_data_in_pin = SYNC_IO_HW_BISS_DATA_IN_PIN;
    vector->biss_clk_out_pin = SYNC_IO_HW_BISS_CLK_OUT_PIN;
    vector->biss_data_out_pin = SYNC_IO_HW_BISS_DATA_OUT_PIN;
    vector->biss_pulse_in_pin = SYNC_IO_HW_RJ45_TRIG_IN_PIN;
    vector->biss_pulse_out_pin = SYNC_IO_HW_RJ45_TRIG_OUT_PIN;
    vector->trigger_width_us = 10u;
    vector->pulse_width_us = 10u;
    vector->rj45_trigger_width_us = 10u;
    vector->marker_width_us = 10u;
    vector->capture_sample_hz = 1000000u;
    vector->sync_clock_hz = 1000000u;

    return true;
}

void trigger_fb_execute(trigger_vector_t *vector, const trig_event_t *event)
{
    if (vector == NULL || event == NULL) {
        return;
    }

    for (uint32_t i = 0u; i < TRIG_ECC_TABLE_COUNT; i++) {
        if (s_ecc_table[i].state == vector->state &&
            s_ecc_table[i].event == event->type) {
            s_ecc_table[i].handler(vector, event);
            return;
        }
    }
}
