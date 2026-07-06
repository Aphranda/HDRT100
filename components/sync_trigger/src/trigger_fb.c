#include "trigger_fb.h"

#include <string.h>

#include "board_config.h"
#include "resource_arbiter.h"
#include "sync_io.h"

/* ── 内部 ── */

#define TRIG_CFG_SEQ_TABLE   (1u << 0)
#define TRIG_CFG_SEQ_LENGTH  (1u << 1)
#define TRIG_CFG_SEQ_WIDTH   (1u << 2)

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

    if (vector->enc_a_pin != 16u && vector->enc_a_pin != 26u) {
        return false;
    }

    return vector->enc_b_pin == (vector->enc_a_pin + 1u) &&
           vector->enc_z_pin == (vector->enc_a_pin + 3u);
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
    case TRIG_EVENT_SET_MARKER_WIDTH:
        vector->marker_width_us = event->payload.value;
        break;
    case TRIG_EVENT_FIRE_MARKER:
        (void)sync_io_fire_marker_us(vector->marker_width_us);
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
        vector->sync_clock_hz = event->payload.value;
        if (vector->sync_clock_enabled) {
            (void)sync_io_start_clock(vector->sync_clock_hz);
        }
        break;
    case TRIG_EVENT_SET_CLOCK_STATE:
        vector->sync_clock_enabled = (event->payload.value != 0u);
        if (vector->sync_clock_enabled) {
            (void)sync_io_start_clock(vector->sync_clock_hz);
        } else {
            sync_io_stop_clock();
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
        vector->enc_a_pin = event->payload.value & 0xFFu;
        vector->enc_b_pin = (event->payload.value >> 8) & 0xFFu;
        vector->enc_z_pin = (event->payload.value >> 16) & 0xFFu;
        break;
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
    case TRIG_EVENT_RESET:
        sync_io_stop_clock();
        sync_io_stop_capture();
        if (sync_io_seq_step_is_running()) {
            sync_io_seq_step_disarm();
            resource_arbiter_release(
                RESOURCE_ARBITER_RESOURCE_PIO1 |
                RESOURCE_ARBITER_RESOURCE_DMA);
        }
        break;
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
        vector->error_code = 1u;
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
    vector->error_code = 0u;
    return FB_OK;
}

/* ── SEQ_CONFIGURED → ARM ── */

static fb_result_t fb_seq_configured_arm(trigger_vector_t *vector,
                                          const trig_event_t *event)
{
    (void)event;

    if (!resource_arbiter_acquire(
            RESOURCE_ARBITER_RESOURCE_PIO1 |
            RESOURCE_ARBITER_RESOURCE_DMA)) {
        vector->error_code = 2u;
        return FB_ERROR;
    }

    if (!sync_io_seq_step_arm(vector->seq_table,
                               vector->seq_length,
                               vector->seq_output_width,
                               vector->trigger_source_pin,
                               (sync_io_edge_t)vector->edge,
                               vector->gate_enabled)) {
        resource_arbiter_release(
            RESOURCE_ARBITER_RESOURCE_PIO1 |
            RESOURCE_ARBITER_RESOURCE_DMA);
        vector->error_code = 3u;
        return FB_ERROR;
    }

    vector->seq_index = 0u;
    vector->rollover_count = 0u;
    vector->state = TRIG_STATE_SEQ_ARMED;
    vector->error_code = 0u;
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
        vector->error_code = 4u;
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
    resource_arbiter_release(
        RESOURCE_ARBITER_RESOURCE_PIO1 |
        RESOURCE_ARBITER_RESOURCE_DMA);

    vector->state = TRIG_STATE_IDLE;
    vector->error_code = 0u;
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
    if (sync_io_seq_step_is_running()) {
        sync_io_seq_step_disarm();
        resource_arbiter_release(
            RESOURCE_ARBITER_RESOURCE_PIO1 |
            RESOURCE_ARBITER_RESOURCE_DMA);
    }
    if (sync_io_enc_count_is_running()) {
        sync_io_enc_count_disarm();
        resource_arbiter_release(RESOURCE_ARBITER_RESOURCE_PIO1);
    }

    vector->state = TRIG_STATE_FAULT;
    vector->error_code = event->payload.value != 0u ? event->payload.value : 100u;
    vector->fault_timestamp_ms = 0u;
    return FB_ERROR;
}

/* ── ENC_COUNT 配置 ── */

static fb_result_t fb_idle_configure_enc(trigger_vector_t *vector,
                                          const trig_event_t *event)
{
    (void)event;

    if (vector->enc_target == 0u) {
        vector->error_code = 10u;
        return FB_ERROR;
    }
    if (!fb_valid_enc_pin_group(vector)) {
        vector->error_code = 11u;   /* invalid encoder pins */
        return FB_ERROR;
    }

    vector->active_mode = TRIG_MODE_ENC_COUNT;
    vector->enc_count = 0u;
    vector->state = TRIG_STATE_ENC_CONFIGURED;
    vector->supported_modes |= (1u << TRIG_MODE_ENC_COUNT);
    vector->error_code = 0u;
    return FB_OK;
}

/* ── ENC_CONFIGURED → ARM ── */

static fb_result_t fb_enc_configured_arm(trigger_vector_t *vector,
                                          const trig_event_t *event)
{
    (void)event;

    if (!resource_arbiter_acquire(
            RESOURCE_ARBITER_RESOURCE_PIO1)) {
        vector->error_code = 2u;
        return FB_ERROR;
    }

    if (!sync_io_enc_count_arm(vector->enc_target,
                                vector->enc_a_pin,
                                BOARD_SYNC_TRIG_OUT_PIN)) {
        resource_arbiter_release(RESOURCE_ARBITER_RESOURCE_PIO1);
        vector->error_code = 3u;
        return FB_ERROR;
    }

    vector->enc_count = 0u;
    vector->state = TRIG_STATE_ENC_ARMED;
    vector->error_code = 0u;
    return FB_OK;
}

/* ── ENC_ARMED → 周期服务 ── */

static fb_result_t fb_enc_armed_service(trigger_vector_t *vector,
                                         const trig_event_t *event)
{
    (void)event;

    if (!sync_io_enc_count_is_running()) {
        vector->state = TRIG_STATE_FAULT;
        vector->error_code = 4u;
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

    return FB_IGNORED;
}

/* ── ENC_ARMED → DISARM ── */

static fb_result_t fb_enc_armed_disarm(trigger_vector_t *vector,
                                        const trig_event_t *event)
{
    (void)event;

    sync_io_enc_count_disarm();
    resource_arbiter_release(RESOURCE_ARBITER_RESOURCE_PIO1);

    vector->state = TRIG_STATE_IDLE;
    vector->error_code = 0u;
    return FB_OK;
}

/* ── FAULT → 清除 ── */

static fb_result_t fb_fault_clear(trigger_vector_t *vector,
                                   const trig_event_t *event)
{
    (void)event;

    if (sync_io_seq_step_is_running()) {
        sync_io_seq_step_disarm();
        resource_arbiter_release(
            RESOURCE_ARBITER_RESOURCE_PIO1 |
            RESOURCE_ARBITER_RESOURCE_DMA);
    }

    vector->state = TRIG_STATE_IDLE;
    vector->error_code = 0u;
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
    { TRIG_STATE_IDLE, TRIG_EVENT_RESET,             fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_TRIGGER_WIDTH, fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_FIRE_TRIGGER,      fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_PULSE_WIDTH,   fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_FIRE_PULSE,        fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_SET_MARKER_WIDTH,  fb_instant_cmd },
    { TRIG_STATE_IDLE, TRIG_EVENT_FIRE_MARKER,       fb_instant_cmd },
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
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_RESET,            fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_TRIGGER_WIDTH, fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_FIRE_TRIGGER,     fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_PULSE_WIDTH,  fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_FIRE_PULSE,       fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_SET_MARKER_WIDTH, fb_instant_cmd },
    { TRIG_STATE_SEQ_CONFIGURED, TRIG_EVENT_FIRE_MARKER,      fb_instant_cmd },
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
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_FIRE_MARKER,      fb_seq_armed_reject },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_RESET,            fb_seq_armed_disarm },
    { TRIG_STATE_SEQ_ARMED, TRIG_EVENT_FAULT,            fb_force_fault },

    /* FAULT */
    { TRIG_STATE_FAULT, TRIG_EVENT_CLEAR_FAULT, fb_fault_clear },
    { TRIG_STATE_FAULT, TRIG_EVENT_DISARM,      fb_fault_clear },
    { TRIG_STATE_FAULT, TRIG_EVENT_RESET,       fb_fault_clear },

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
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_RESET,           fb_instant_cmd },
    { TRIG_STATE_ENC_CONFIGURED, TRIG_EVENT_FAULT,           fb_force_fault },

    /* ENC_ARMED */
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_DISARM,           fb_enc_armed_disarm },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_DMA_ROLLOVER,     fb_enc_armed_service },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_RUNTIME_SAMPLE,   fb_runtime_sample },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_ENC_Z_PULSE,      fb_instant_cmd },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_PCNT_CLEAR,       fb_instant_cmd },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_SET_ENC_TARGET,   fb_instant_cmd },
    { TRIG_STATE_ENC_ARMED, TRIG_EVENT_RESET,            fb_enc_armed_disarm },
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
    vector->enc_a_pin = 16u;
    vector->enc_b_pin = 17u;
    vector->enc_z_pin = 19u;
    vector->enc_decode = TRIG_PCNT_DECODE_QUAD_1X;
    vector->enc_dir = TRIG_PCNT_DIR_CW;
    vector->enc_z_enabled = true;
    vector->enc_gate_enabled = false;
    vector->enc_filter_ns = 0u;
    vector->enc_preset = 0u;
    vector->enc_cmp_pulse_ns = 67u;   /* ~10 PIO cycles */
    vector->trigger_width_us = 10u;
    vector->pulse_width_us = 10u;
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
