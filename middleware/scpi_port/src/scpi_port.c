#include "scpi_port.h"

#include <stdio.h>
#include <string.h>

#include "diagnostics.h"
#include "drv_watchdog.h"
#include "ota_ao.h"
#include "pico/error.h"
#include "pico/stdio.h"
#include "project_config.h"
#include "scpi/scpi.h"
#include "storage_manager.h"
#include "sync_trigger.h"
#include "trigger_measure.h"

#define SCPI_PORT_INPUT_BUFFER_LENGTH 768u
#define SCPI_PORT_ERROR_QUEUE_SIZE    16
#define SCPI_PORT_IDN_VENDOR          "RP2350_TRIG"
#define SCPI_PORT_IDN_MODEL           "SYNC_TRIGGER"
#define SCPI_PORT_IDN_SERIAL          NULL
#define SCPI_PORT_POLL_CHARS          32u
#define SCPI_PORT_MMEM_PAGE_LIMIT_MAX 16u
#define SCPI_PORT_MMEM_READ_BYTES_MAX 128u
#define SCPI_PORT_STORAGE_JOB_WAIT_LOOPS 200u

static scpi_t s_scpi_context;
static char s_scpi_input_buffer[SCPI_PORT_INPUT_BUFFER_LENGTH];
static scpi_error_t s_scpi_error_queue[SCPI_PORT_ERROR_QUEUE_SIZE];

static bool scpi_port_wait_storage_job(uint32_t job_id);
static bool scpi_port_trigger_is_armed(void);

static size_t scpi_port_write(scpi_t *context, const char *data, size_t len)
{
    (void)context;
    for (size_t i = 0u; i < len; i++) {
        putchar_raw(data[i]);
    }
    return len;
}

static scpi_result_t scpi_port_flush(scpi_t *context)
{
    (void)context;
    stdio_flush();
    return SCPI_RES_OK;
}

static int scpi_port_error(scpi_t *context, int_fast16_t error)
{
    (void)context;
    LOG_WARN("scpi", "error=%d", (int)error);
    return 0;
}

static scpi_result_t scpi_port_control(scpi_t *context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val)
{
    (void)context;
    (void)ctrl;
    (void)val;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_port_reset(scpi_t *context)
{
    (void)context;
    const sync_trigger_event_t event = {
        .type = SYNC_TRIGGER_EVENT_RESET,
    };

    return sync_trigger_post_event(&event) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_core_tst_q(scpi_t *context)
{
    SCPI_ResultInt32(context, 0);
    return SCPI_RES_OK;
}

static bool scpi_port_read_u32(scpi_t *context, uint32_t *value)
{
    return SCPI_ParamUInt32(context, value, TRUE) == TRUE;
}

static scpi_result_t scpi_port_result_ok(scpi_t *context)
{
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

static void scpi_port_get_trigger_summary(sync_trigger_summary_t *summary)
{
    sync_trigger_get_summary(summary);
}

static bool scpi_port_post_trigger_event(sync_trigger_event_type_t type, uint32_t value)
{
    const sync_trigger_event_t event = {
        .type = type,
        .value = value,
    };

    return sync_trigger_post_event(&event);
}

static bool scpi_port_trigger_is_armed(void)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    return vector.state == TRIG_STATE_SEQ_ARMED ||
           vector.state == TRIG_STATE_ENC_ARMED;
}

static scpi_result_t scpi_cmd_firmware_version_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, PROJECT_VERSION_MAJOR);
    SCPI_ResultUInt32(context, PROJECT_VERSION_MINOR);
    SCPI_ResultUInt32(context, PROJECT_VERSION_PATCH);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_firmware_build_q(scpi_t *context)
{
    SCPI_ResultText(context, g_project_build_id);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_bootloader_version_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, PROJECT_BOOTLOADER_VERSION_MAJOR);
    SCPI_ResultUInt32(context, PROJECT_BOOTLOADER_VERSION_MINOR);
    SCPI_ResultUInt32(context, PROJECT_BOOTLOADER_VERSION_PATCH);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_bootloader_capability_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.boot_capabilities);
    return SCPI_RES_OK;
}

#if PROJECT_ENABLE_OTA_FAULT_INJECTION
static scpi_result_t scpi_cmd_boot_reset(scpi_t *context)
{
    scpi_result_t result = scpi_port_result_ok(context);
    drv_watchdog_reboot(50u);
    return result;
}
#endif

static scpi_result_t scpi_cmd_trigger_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_SET_TRIGGER_WIDTH, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_trigger_width_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.trigger_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_fire(scpi_t *context)
{
    (void)context;
    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_FIRE_TRIGGER, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_pulse_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_SET_PULSE_WIDTH, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_pulse_width_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.pulse_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pulse_fire(scpi_t *context)
{
    (void)context;
    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_FIRE_PULSE, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_marker_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_SET_MARKER_WIDTH, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_marker_width_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.marker_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_marker_fire(scpi_t *context)
{
    (void)context;
    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_FIRE_MARKER, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_sample_rate(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_SET_SAMPLE_RATE, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_sample_rate_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.capture_sample_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_sample_state(scpi_t *context)
{
    scpi_bool_t state;
    if (SCPI_ParamBool(context, &state, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_SET_SAMPLE_STATE,
                                        state ? 1u : 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_sample_state_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultBool(context, summary.capture_running ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_freq(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_SET_CLOCK_FREQ, value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_clock_freq_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.sync_clock_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_state(scpi_t *context)
{
    scpi_bool_t state;
    if (SCPI_ParamBool(context, &state, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_SET_CLOCK_STATE,
                                        state ? 1u : 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_clock_state_q(scpi_t *context)
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

static const char *scpi_trig_mode_to_string(trig_mode_t mode)
{
    switch (mode) {
    case TRIG_MODE_IDLE:     return "IDLE";
    case TRIG_MODE_SEQ_STEP:  return "SEQ_STEP";
    case TRIG_MODE_ENC_COUNT: return "ENC_COUNT";
    default:                  return "UNKNOWN";
    }
}

static scpi_result_t scpi_cmd_trigger_mode(scpi_t *context)
{
    uint32_t mode;
    if (!scpi_port_read_u32(context, &mode) ||
        mode >= (uint32_t)TRIG_MODE_COUNT) {
        return SCPI_RES_ERR;
    }

    if (mode == (uint32_t)TRIG_MODE_SEQ_STEP) {
        s_seq_table_len = (s_seq_table_len > 0u) ? s_seq_table_len : 1u;
        s_seq_table_width = (s_seq_table_width > 0u) ? s_seq_table_width : 4u;

        const trig_event_t event = {
            .type = TRIG_EVENT_CONFIGURE_SEQ,
            .payload.seq_config = {
                .seq_table  = s_seq_table_buf,
                .seq_length = s_seq_table_len,
                .seq_width  = s_seq_table_width,
            },
        };
        return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
    }

    if (mode == (uint32_t)TRIG_MODE_ENC_COUNT) {
        const trig_event_t event = { .type = TRIG_EVENT_CONFIGURE_ENC };
        return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
    }

    /* TRIG_MODE_IDLE */
    return SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_trigger_mode_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, scpi_trig_mode_to_string(vector.active_mode));
    SCPI_ResultUInt32(context, (uint32_t)vector.active_mode);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_seq_length(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) ||
        value == 0u || value > TRIG_SEQ_TABLE_MAX) {
        return SCPI_RES_ERR;
    }
    s_seq_table_len = value;
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_trigger_seq_length_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.seq_length);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_seq_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) ||
        value == 0u || value > TRIG_SEQ_WIDTH_MAX) {
        return SCPI_RES_ERR;
    }
    s_seq_table_width = value;
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_trigger_seq_width_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.seq_output_width);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_seq_index_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.seq_index);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_seq_data(scpi_t *context)
{
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

static scpi_result_t scpi_cmd_trigger_seq_data_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);

    /* 逐字输出序列表 */
    for (uint32_t i = 0u; i < vector.seq_length; i++) {
        SCPI_ResultUInt32(context, vector.seq_table[i]);
    }
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_arm(scpi_t *context)
{
    const trig_event_t event = { .type = TRIG_EVENT_ARM };
    storage_manager_trace_event(2u, 10u, 1u, 0u, 0u);
    uint32_t job_id = 0u;
    if (storage_manager_post_snapshot_job("arm", &job_id)) {
        (void)scpi_port_wait_storage_job(job_id);
    }
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_trigger_disarm(scpi_t *context)
{
    const trig_event_t event = { .type = TRIG_EVENT_DISARM };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_trigger_fault(scpi_t *context)
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

static scpi_result_t scpi_cmd_trigger_source(scpi_t *context)
{
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

static scpi_result_t scpi_cmd_trigger_source_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.trigger_source_pin);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_edge(scpi_t *context)
{
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

static scpi_result_t scpi_cmd_trigger_edge_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, vector.edge == TRIG_EDGE_RISING ? "RISING" : "FALLING");
    SCPI_ResultUInt32(context, (uint32_t)vector.edge);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_gate(scpi_t *context)
{
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

static scpi_result_t scpi_cmd_trigger_gate_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultBool(context, vector.gate_enabled ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_safe(scpi_t *context)
{
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

static scpi_result_t scpi_cmd_trigger_safe_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, vector.safe_state == TRIG_SAFE_ZERO ? "ZERO" : "ONE");
    SCPI_ResultUInt32(context, (uint32_t)vector.safe_state);
    return SCPI_RES_OK;
}

/* ── ENC_COUNT 命令 ── */

static scpi_result_t scpi_cmd_enc_target(scpi_t *context)
{
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

static scpi_result_t scpi_cmd_enc_target_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_target);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_enc_count_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_count);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_enc_a_pin(scpi_t *context)
{
    uint32_t pin;
    if (!scpi_port_read_u32(context, &pin) || (pin != 16u && pin != 26u)) {
        return SCPI_RES_ERR;
    }

    /* enc_count.pio samples a 4-pin group:
     * A=base, B=base+1, offset2 spare, Z=base+3. */
    const trig_event_t ev = {
        .type = TRIG_EVENT_SET_ENC_PINS,
        .payload.value = pin | ((pin + 1u) << 8) | ((pin + 3u) << 16),
    };
    return sync_trigger_post(&ev) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_enc_a_pin_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_a_pin);
    SCPI_ResultUInt32(context, vector.enc_b_pin);
    SCPI_ResultUInt32(context, vector.enc_z_pin);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_enc_rev_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_rev_count);
    return SCPI_RES_OK;
}

/* ── PCNT 脉冲计数器命令 ── */

static void scpi_post_pcnt_event(trig_event_type_t type, uint32_t value)
{
    const trig_event_t event = { .type = type, .payload.value = value };
    sync_trigger_post(&event);
}

static scpi_result_t scpi_cmd_pcnt_decode(scpi_t *context)
{
    uint32_t v;
    if (!scpi_port_read_u32(context, &v) || v > 3u) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_DECODE, v);
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_pcnt_decode_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    const char *s = "UNKNOWN";
    switch (v.enc_decode) {
    case TRIG_PCNT_DECODE_SINGLE:  s = "SINGLE"; break;
    case TRIG_PCNT_DECODE_QUAD_1X: s = "QUAD1X"; break;
    case TRIG_PCNT_DECODE_QUAD_2X: s = "QUAD2X"; break;
    case TRIG_PCNT_DECODE_UP_DOWN: s = "UPDOWN"; break;
    default: break;
    }
    SCPI_ResultText(context, s);
    SCPI_ResultUInt32(context, (uint32_t)v.enc_decode);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pcnt_dir(scpi_t *context)
{
    uint32_t v;
    if (!scpi_port_read_u32(context, &v) || v > 2u) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_DIR, v);
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_pcnt_dir_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    const char *s[] = {"CW","CCW","BOTH"};
    SCPI_ResultText(context, s[v.enc_dir <= 2 ? v.enc_dir : 0]);
    SCPI_ResultUInt32(context, (uint32_t)v.enc_dir);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pcnt_filter(scpi_t *context)
{
    uint32_t v;
    if (!scpi_port_read_u32(context, &v)) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_FILTER, v);
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_pcnt_filter_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_filter_ns);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pcnt_gate(scpi_t *context)
{
    scpi_bool_t en;
    if (SCPI_ParamBool(context, &en, TRUE) != TRUE) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_GATE, en ? 1u : 0u);
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_pcnt_gate_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultBool(context, v.enc_gate_enabled ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pcnt_cmp(scpi_t *context)
{
    uint32_t v;
    if (!scpi_port_read_u32(context, &v)) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_CMP, v);
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_pcnt_cmp_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_cmp_pulse_ns);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pcnt_preset(scpi_t *context)
{
    uint32_t v;
    if (!scpi_port_read_u32(context, &v)) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_PRESET, v);
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_pcnt_preset_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_preset);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pcnt_clear(scpi_t *context)
{
    scpi_post_pcnt_event(TRIG_EVENT_PCNT_CLEAR, 0u);
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_pcnt_total_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_total);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pcnt_freq_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_frequency_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_status_q(scpi_t *context)
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

static scpi_result_t scpi_cmd_status_q(scpi_t *context)
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

static scpi_result_t scpi_cmd_ota_status_q(scpi_t *context)
{
    ota_vector_t vector;
    ota_ao_get_vector(&vector);
    SCPI_ResultText(context, ota_state_to_string((ota_state_t)vector.state));
    SCPI_ResultUInt32(context, vector.target_slot);
    SCPI_ResultText(context, ota_error_to_string(vector.error_code));
    SCPI_ResultUInt32(context, vector.last_result);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_ota_progress_q(scpi_t *context)
{
    ota_vector_t vector;
    ota_ao_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.received_size);
    SCPI_ResultUInt32(context, vector.expected_size);
    SCPI_ResultUInt32(context, vector.progress_permille);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_ota_begin(scpi_t *context)
{
    uint32_t size;
    uint32_t crc32;
    if (!scpi_port_read_u32(context, &size) || !scpi_port_read_u32(context, &crc32)) {
        return SCPI_RES_ERR;
    }

    const ota_event_t event = {
        .type = OTA_EVENT_BEGIN,
        .payload.begin = {
            .size = size,
            .crc32 = crc32,
            .image_version = 0u,
            .flags = 0u,
        },
    };

    return ota_ao_post_event(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_ota_package_begin(scpi_t *context)
{
    uint32_t size;
    uint32_t crc32;
    if (!scpi_port_read_u32(context, &size) || !scpi_port_read_u32(context, &crc32)) {
        return SCPI_RES_ERR;
    }

    const ota_event_t event = {
        .type = OTA_EVENT_BEGIN,
        .payload.begin = {
            .size = size,
            .crc32 = crc32,
            .image_version = 0u,
            .flags = OTA_BEGIN_FLAG_PACKAGE,
        },
    };

    return ota_ao_post_event(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_ota_data(scpi_t *context)
{
    const char *data = NULL;
    size_t length = 0u;
    if (SCPI_ParamArbitraryBlock(context, &data, &length, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    if (length == 0u || length > OTA_EVENT_MAX_DATA_SIZE) {
        return SCPI_RES_ERR;
    }

    const ota_event_t event = {
        .type = OTA_EVENT_DATA_BLOCK,
        .payload.data = {
            .data = (const uint8_t *)data,
            .length = (uint32_t)length,
            .block_index = 0u,
        },
    };

    return ota_ao_post_event(&event) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_ota_simple_event_ack(scpi_t *context, ota_event_type_t type)
{
    const ota_event_t event = {
        .type = type,
    };

    return ota_ao_post_event(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_ota_end(scpi_t *context)
{
    (void)context;
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_END);
}

static scpi_result_t scpi_cmd_ota_abort(scpi_t *context)
{
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_ABORT);
}

static scpi_result_t scpi_cmd_ota_boot(scpi_t *context)
{
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_BOOT);
}

static scpi_result_t scpi_cmd_ota_commit(scpi_t *context)
{
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_COMMIT);
}

static scpi_result_t scpi_cmd_ota_slot_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.active_slot);
    SCPI_ResultUInt32(context, metadata.pending_slot);
    SCPI_ResultUInt32(context, metadata.confirmed_slot);
    SCPI_ResultUInt32(context, metadata.boot_attempts);
    SCPI_ResultUInt32(context, metadata.rollback_count);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_ota_result_q(scpi_t *context)
{
    ota_vector_t vector;
    ota_metadata_t metadata;
    ota_ao_get_vector(&vector);

    SCPI_ResultUInt32(context, vector.last_result);
    SCPI_ResultText(context, ota_error_to_string(vector.error_code));
    if (ota_ao_get_metadata(&metadata)) {
        SCPI_ResultText(context, ota_metadata_boot_result_to_string(metadata.last_boot_result));
        SCPI_ResultUInt32(context, metadata.last_boot_source_slot);
        SCPI_ResultUInt32(context, metadata.last_boot_size);
        SCPI_ResultUInt32(context, metadata.last_boot_crc32);
    } else {
        SCPI_ResultText(context, "METADATA_LOAD_FAILED");
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
    }
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_ota_transaction_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.copy_txn_state);
    SCPI_ResultUInt32(context, metadata.copy_source_slot);
    SCPI_ResultUInt32(context, metadata.copy_destination_slot);
    SCPI_ResultUInt32(context, metadata.copy_size);
    SCPI_ResultUInt32(context, metadata.copy_crc32);
    SCPI_ResultUInt32(context, metadata.copy_written);
    SCPI_ResultUInt32(context, metadata.copy_attempts);
    SCPI_ResultUInt32(context, metadata.copy_last_error);
    return SCPI_RES_OK;
}

static const char *scpi_ota_boot_mode_to_string(uint32_t mode)
{
    switch ((ota_boot_mode_t)mode) {
    case OTA_BOOT_MODE_COPY_TO_ACTIVE:
        return "COPY_TO_ACTIVE";
    case OTA_BOOT_MODE_DIRECT_AB:
        return "DIRECT_AB";
    default:
        return "UNKNOWN";
    }
}

static uint32_t scpi_ota_next_target_slot(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return (uint32_t)OTA_SLOT_NONE;
    }

    if (metadata->boot_mode != (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
        return (uint32_t)OTA_SLOT_B;
    }

    if (metadata->active_slot == (uint32_t)OTA_SLOT_A) {
        return (uint32_t)OTA_SLOT_B;
    }

    if (metadata->active_slot == (uint32_t)OTA_SLOT_B) {
        return (uint32_t)OTA_SLOT_A;
    }

    return (uint32_t)OTA_SLOT_B;
}

static scpi_result_t scpi_cmd_ota_mode_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, scpi_ota_boot_mode_to_string(metadata.boot_mode));
    SCPI_ResultUInt32(context, metadata.boot_mode);
    return SCPI_RES_OK;
}

#if PROJECT_ENABLE_OTA_FAULT_INJECTION
static scpi_result_t scpi_cmd_ota_mode(scpi_t *context)
{
    uint32_t mode;
    if (!scpi_port_read_u32(context, &mode) ||
        mode > (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
        return SCPI_RES_ERR;
    }

    return ota_metadata_set_boot_mode((ota_boot_mode_t)mode) ?
               scpi_port_result_ok(context) :
               SCPI_RES_ERR;
}
#endif

static scpi_result_t scpi_cmd_ota_target_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, scpi_ota_next_target_slot(&metadata));
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_ota_capability_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.boot_capabilities);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_status_q(scpi_t *context)
{
    (void)storage_manager_probe();

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, storage_manager_state_string(vector.state));
    SCPI_ResultBool(context, vector.card_present ? TRUE : FALSE);
    SCPI_ResultBool(context, vector.fs_mounted ? TRUE : FALSE);
    SCPI_ResultText(context, sd_card_status_string(vector.card_status));
    SCPI_ResultUInt32(context, vector.storage_error);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_info_q(scpi_t *context)
{
    (void)storage_manager_probe();

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, storage_manager_state_string(vector.state));
    SCPI_ResultText(context, sd_card_type_string(vector.card_type));
    SCPI_ResultBool(context, vector.high_capacity ? TRUE : FALSE);
    SCPI_ResultUInt32(context, vector.block_count);
    SCPI_ResultUInt32(context, vector.capacity_kib);
    SCPI_ResultBool(context, vector.fatfs_available ? TRUE : FALSE);
    SCPI_ResultBool(context, vector.fs_mounted ? TRUE : FALSE);
    SCPI_ResultUInt32(context, vector.probe_count);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_raw_clear(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    uint32_t sector_count = 0u;
    const char *confirm = NULL;
    size_t confirm_len = 0u;
    if (SCPI_ParamUInt32(context, &sector_count, TRUE) != TRUE ||
        SCPI_ParamCharacters(context, &confirm, &confirm_len, TRUE) != TRUE ||
        confirm == NULL ||
        confirm_len != 5u ||
        strncmp(confirm, "ERASE", 5u) != 0) {
        return SCPI_RES_ERR;
    }

    uint32_t cleared_count = 0u;
    sd_card_status_t raw_status = SD_CARD_STATUS_BAD_RESPONSE;
    const bool ok = storage_manager_raw_clear_prefix(sector_count, &cleared_count, &raw_status);
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, ok ? "OK" : "ERROR");
    SCPI_ResultUInt32(context, sector_count);
    SCPI_ResultUInt32(context, cleared_count);
    SCPI_ResultText(context, sd_card_status_string(raw_status));
    SCPI_ResultUInt32(context, vector.storage_error);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_raw_read_q(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    uint32_t sector = 0u;
    if (SCPI_ParamUInt32(context, &sector, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    uint8_t data[512];
    sd_card_status_t raw_status = SD_CARD_STATUS_BAD_RESPONSE;
    const bool ok = storage_manager_raw_read_sector(sector, data, sizeof(data), &raw_status);
    char hex[129];
    if (ok) {
        for (size_t i = 0u; i < 64u; i++) {
            (void)snprintf(hex + (i * 2u), sizeof(hex) - (i * 2u), "%02X", data[i]);
        }
    } else {
        hex[0] = '\0';
    }

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, ok ? "OK" : "ERROR");
    SCPI_ResultUInt32(context, sector);
    SCPI_ResultText(context, sd_card_status_string(raw_status));
    SCPI_ResultUInt32(context, vector.storage_error);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_mkfs(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    const char *confirm = NULL;
    size_t confirm_len = 0u;
    if (SCPI_ParamCharacters(context, &confirm, &confirm_len, TRUE) != TRUE ||
        confirm == NULL ||
        confirm_len != 5u ||
        strncmp(confirm, "ERASE", 5u) != 0) {
        return SCPI_RES_ERR;
    }

    fatfs_port_status_t format_status = FATFS_PORT_STATUS_FORMAT_FAILED;
    const bool ok = storage_manager_format_volume(&format_status);
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, ok ? "OK" : "ERROR");
    SCPI_ResultText(context, fatfs_port_status_string(format_status));
    SCPI_ResultText(context, storage_manager_state_string(vector.state));
    SCPI_ResultUInt32(context, vector.storage_error);
    SCPI_ResultUInt32(context, vector.block_count);
    SCPI_ResultUInt32(context, vector.capacity_kib);
    SCPI_ResultUInt32(context, fatfs_port_last_mkfs_result());
    SCPI_ResultUInt32(context, fatfs_port_last_mount_result());
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_init(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_system_init_job(&job_id)) {
        return SCPI_RES_ERR;
    }
    (void)scpi_port_wait_storage_job(job_id);

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    if (job.id != job_id ||
        job.state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
        job.state == STORAGE_MANAGER_JOB_STATE_RUNNING) {
        return SCPI_RES_ERR;
    }

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, job.state == STORAGE_MANAGER_JOB_STATE_DONE ? "OK" : "ERROR");
    SCPI_ResultText(context, storage_manager_manifest_status_string(vector.manifest_status));
    SCPI_ResultUInt32(context, vector.manifest_schema);
    SCPI_ResultText(context, vector.manifest_build_id);
    SCPI_ResultUInt32(context, vector.manifest_required_count);
    SCPI_ResultUInt32(context, vector.manifest_missing_count);
    SCPI_ResultUInt32(context, job.error);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_manifest_q(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_manifest_scan_job(&job_id)) {
        return SCPI_RES_ERR;
    }
    (void)scpi_port_wait_storage_job(job_id);

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    if (job.id != job_id ||
        job.state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
        job.state == STORAGE_MANAGER_JOB_STATE_RUNNING) {
        return SCPI_RES_ERR;
    }

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, storage_manager_manifest_status_string(vector.manifest_status));
    SCPI_ResultUInt32(context, vector.manifest_schema);
    SCPI_ResultText(context, vector.manifest_product_id);
    SCPI_ResultText(context, vector.manifest_hardware_id);
    SCPI_ResultText(context, vector.manifest_build_id);
    SCPI_ResultUInt32(context, vector.manifest_required_count);
    SCPI_ResultUInt32(context, vector.manifest_missing_count);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_job_info(scpi_t *context)
{
    const char *path = NULL;
    size_t path_len = 0u;
    if (SCPI_ParamCharacters(context, &path, &path_len, TRUE) != TRUE ||
        path == NULL ||
        path_len == 0u ||
        path_len >= 96u) {
        return SCPI_RES_ERR;
    }

    char path_buffer[96];
    memcpy(path_buffer, path, path_len);
    path_buffer[path_len] = '\0';

    uint32_t job_id = 0u;
    if (!storage_manager_post_file_info_job(path_buffer, &job_id)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, job_id);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_storage_job_q(scpi_t *context)
{
    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    const char *kind = job.is_dir ? "DIR" : "FILE";
    if (job.type == STORAGE_MANAGER_JOB_TYPE_FILE_READ) {
        kind = "READ";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE) {
        kind = "CATALOG";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_SNAPSHOT_WRITE) {
        kind = "SNAPSHOT";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_MANIFEST_SCAN) {
        kind = "MANIFEST";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_FAULT_EVIDENCE) {
        kind = "FAULT_EVIDENCE";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_SYSTEM_INIT) {
        kind = "MANIFEST";
    }

    SCPI_ResultText(context, storage_manager_job_state_string(job.state));
    SCPI_ResultUInt32(context, job.id);
    SCPI_ResultText(context, storage_manager_job_type_string(job.type));
    SCPI_ResultText(context, job.path);
    SCPI_ResultUInt32(context, job.size);
    SCPI_ResultText(context, kind);
    SCPI_ResultUInt32(context, job.path_hash);
    SCPI_ResultUInt32(context, job.error);
    return SCPI_RES_OK;
}

static bool scpi_port_wait_storage_job(uint32_t job_id)
{
    for (uint32_t i = 0u; i < SCPI_PORT_STORAGE_JOB_WAIT_LOOPS; i++) {
        storage_manager_service(250u);
        storage_manager_job_result_t job;
        storage_manager_get_job_result(&job);
        if (job.id != job_id) {
            return false;
        }
        if (job.state == STORAGE_MANAGER_JOB_STATE_DONE) {
            return true;
        }
        if (job.state == STORAGE_MANAGER_JOB_STATE_FAILED) {
            return false;
        }
    }
    return false;
}

static scpi_result_t scpi_cmd_snapshot_write(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    const char *kind = NULL;
    size_t kind_len = 0u;
    (void)SCPI_ParamCharacters(context, &kind, &kind_len, FALSE);

    char kind_buffer[16];
    if (kind != NULL && kind_len > 0u) {
        if (kind_len >= sizeof(kind_buffer)) {
            return SCPI_RES_ERR;
        }
        memcpy(kind_buffer, kind, kind_len);
        kind_buffer[kind_len] = '\0';
    } else {
        (void)snprintf(kind_buffer, sizeof(kind_buffer), "boot");
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_snapshot_job(kind_buffer, &job_id)) {
        return SCPI_RES_ERR;
    }
    if (!scpi_port_wait_storage_job(job_id)) {
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_snapshot_last_q(scpi_t *context)
{
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, vector.last_snapshot_error == 0u ? "OK" : "ERROR");
    SCPI_ResultText(context, vector.last_snapshot_kind);
    SCPI_ResultUInt32(context, vector.last_snapshot_id);
    SCPI_ResultText(context, vector.last_snapshot_path);
    SCPI_ResultUInt32(context, vector.last_snapshot_path_hash);
    SCPI_ResultUInt32(context, vector.last_snapshot_error);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trace_last_q(scpi_t *context)
{
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, vector.last_trace_error == 0u ? "OK" : "ERROR");
    SCPI_ResultText(context, vector.last_trace_kind);
    SCPI_ResultUInt32(context, vector.last_trace_id);
    SCPI_ResultText(context, vector.last_trace_path);
    SCPI_ResultUInt32(context, vector.last_trace_path_hash);
    SCPI_ResultUInt32(context, vector.last_trace_event_count);
    SCPI_ResultUInt32(context, vector.last_trace_error);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_fault_last_q(scpi_t *context)
{
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, vector.last_fault_report_error == 0u ? "OK" : "ERROR");
    SCPI_ResultUInt32(context, vector.last_fault_report_id);
    SCPI_ResultText(context, vector.last_fault_report_path);
    SCPI_ResultUInt32(context, vector.last_fault_report_path_hash);
    SCPI_ResultUInt32(context, vector.last_fault_snapshot_id);
    SCPI_ResultUInt32(context, vector.last_fault_trace_id);
    SCPI_ResultUInt32(context, vector.last_fault_report_error);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_mmem_catalog_q(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    (void)SCPI_ParamCharacters(context, &path, &path_len, FALSE);
    char path_buffer[96];
    if (path != NULL && path_len > 0u) {
        if (path_len >= sizeof(path_buffer)) {
            return SCPI_RES_ERR;
        }
        memcpy(path_buffer, path, path_len);
        path_buffer[path_len] = '\0';
    } else {
        (void)snprintf(path_buffer, sizeof(path_buffer), "/");
    }

    char catalog[384];
    storage_manager_catalog_page_t page;
    uint32_t job_id = 0u;
    bool ok = storage_manager_post_catalog_page_job(path_buffer,
                                                    0u,
                                                    SCPI_PORT_MMEM_PAGE_LIMIT_MAX,
                                                    &job_id);
    if (ok) {
        ok = scpi_port_wait_storage_job(job_id);
    }
    if (ok) {
        ok = storage_manager_get_catalog_page_job_result(job_id,
                                                         &page,
                                                         catalog,
                                                         sizeof(catalog));
    } else {
        memset(&page, 0, sizeof(page));
        catalog[0] = '\0';
    }
    if (ok && !page.complete) {
        const size_t catalog_len = strlen(catalog);
        if (catalog_len > 0u && catalog[catalog_len - 1u] == ';') {
            catalog[catalog_len - 1u] = '\0';
        }
    }
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    if (!ok && vector.state == STORAGE_MANAGER_STATE_PATH_DENIED) {
        (void)snprintf(catalog, sizeof(catalog), "PATH_DENIED");
    }
    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultText(context, catalog);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_mmem_info_q(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    if (SCPI_ParamCharacters(context, &path, &path_len, TRUE) != TRUE ||
        path == NULL ||
        path_len == 0u ||
        path_len >= 96u) {
        return SCPI_RES_ERR;
    }

    char path_buffer[96];
    memcpy(path_buffer, path, path_len);
    path_buffer[path_len] = '\0';

    uint32_t job_id = 0u;
    bool ok = storage_manager_post_file_info_job(path_buffer, &job_id);
    if (ok) {
        ok = scpi_port_wait_storage_job(job_id);
    }

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultText(context, ok ? job.path : path_buffer);
    SCPI_ResultUInt32(context, ok ? job.size : 0u);
    SCPI_ResultText(context, ok ? (job.is_dir ? "DIR" : "FILE") : "UNKNOWN");
    SCPI_ResultUInt32(context, ok ? job.path_hash : 0u);
    SCPI_ResultUInt32(context, vector.storage_error);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_mmem_catalog_page_q(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    uint32_t offset = 0u;
    uint32_t limit = 0u;
    if (SCPI_ParamCharacters(context, &path, &path_len, TRUE) != TRUE ||
        path == NULL ||
        path_len == 0u ||
        path_len >= 96u ||
        SCPI_ParamUInt32(context, &offset, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &limit, TRUE) != TRUE ||
        limit == 0u) {
        return SCPI_RES_ERR;
    }
    if (limit > SCPI_PORT_MMEM_PAGE_LIMIT_MAX) {
        limit = SCPI_PORT_MMEM_PAGE_LIMIT_MAX;
    }

    char path_buffer[96];
    memcpy(path_buffer, path, path_len);
    path_buffer[path_len] = '\0';

    char catalog[384];
    storage_manager_catalog_page_t page;
    uint32_t job_id = 0u;
    bool ok = storage_manager_post_catalog_page_job(path_buffer,
                                                    offset,
                                                    limit,
                                                    &job_id);
    if (ok) {
        ok = scpi_port_wait_storage_job(job_id);
    }
    if (ok) {
        ok = storage_manager_get_catalog_page_job_result(job_id,
                                                         &page,
                                                         catalog,
                                                         sizeof(catalog));
    } else {
        memset(&page, 0, sizeof(page));
        catalog[0] = '\0';
    }
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    if (!ok && vector.state == STORAGE_MANAGER_STATE_PATH_DENIED) {
        (void)snprintf(catalog, sizeof(catalog), "PATH_DENIED");
    }

    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultText(context, ok ? page.path : path_buffer);
    SCPI_ResultUInt32(context, ok ? offset : 0u);
    SCPI_ResultUInt32(context, ok ? page.returned_count : 0u);
    SCPI_ResultUInt32(context, ok ? page.next_offset : 0u);
    SCPI_ResultBool(context, ok && page.complete ? TRUE : FALSE);
    SCPI_ResultBool(context, ok && page.truncated ? TRUE : FALSE);
    SCPI_ResultText(context, catalog);
    return SCPI_RES_OK;
}

static void scpi_port_hex_encode(const uint8_t *data, size_t data_size, char *hex, size_t hex_size)
{
    static const char digits[] = "0123456789ABCDEF";
    if (hex == NULL || hex_size == 0u) {
        return;
    }
    if (data == NULL || hex_size < (data_size * 2u) + 1u) {
        hex[0] = '\0';
        return;
    }
    for (size_t i = 0u; i < data_size; i++) {
        hex[i * 2u] = digits[(data[i] >> 4u) & 0x0Fu];
        hex[(i * 2u) + 1u] = digits[data[i] & 0x0Fu];
    }
    hex[data_size * 2u] = '\0';
}

static scpi_result_t scpi_cmd_mmem_read_q(scpi_t *context)
{
    if (scpi_port_trigger_is_armed()) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    uint32_t offset = 0u;
    uint32_t length = 0u;
    if (SCPI_ParamCharacters(context, &path, &path_len, TRUE) != TRUE ||
        path == NULL ||
        path_len == 0u ||
        path_len >= 96u ||
        SCPI_ParamUInt32(context, &offset, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &length, TRUE) != TRUE ||
        length == 0u) {
        return SCPI_RES_ERR;
    }
    if (length > SCPI_PORT_MMEM_READ_BYTES_MAX) {
        length = SCPI_PORT_MMEM_READ_BYTES_MAX;
    }

    char path_buffer[96];
    memcpy(path_buffer, path, path_len);
    path_buffer[path_len] = '\0';

    uint8_t data[SCPI_PORT_MMEM_READ_BYTES_MAX];
    char hex[(SCPI_PORT_MMEM_READ_BYTES_MAX * 2u) + 1u];
    storage_manager_file_read_t read_info;
    uint32_t job_id = 0u;
    bool ok = storage_manager_post_file_read_job(path_buffer, offset, length, &job_id);
    if (ok) {
        ok = scpi_port_wait_storage_job(job_id);
    }
    if (ok) {
        ok = storage_manager_get_file_read_job_result(job_id,
                                                      &read_info,
                                                      data,
                                                      sizeof(data));
    } else {
        memset(&read_info, 0, sizeof(read_info));
    }
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    const uint32_t returned = ok ? read_info.returned : 0u;
    scpi_port_hex_encode(data, returned, hex, sizeof(hex));

    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultText(context, ok ? read_info.path : path_buffer);
    SCPI_ResultUInt32(context, ok ? read_info.offset : 0u);
    SCPI_ResultUInt32(context, length);
    SCPI_ResultUInt32(context, returned);
    SCPI_ResultBool(context, ok && read_info.eof ? TRUE : FALSE);
    SCPI_ResultUInt32(context, ok ? read_info.path_hash : 0u);
    SCPI_ResultUInt32(context, vector.storage_error);
    SCPI_ResultText(context, ok ? hex : "");
    return SCPI_RES_OK;
}

#if PROJECT_ENABLE_OTA_FAULT_INJECTION
static scpi_result_t scpi_cmd_ota_inject_copy(scpi_t *context)
{
    return ota_metadata_set_fault_injection(OTA_FAULT_INJECT_COPY_FAIL) ?
               scpi_port_result_ok(context) :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_ota_inject_clear(scpi_t *context)
{
    return ota_metadata_set_fault_injection(OTA_FAULT_INJECT_NONE) ?
               scpi_port_result_ok(context) :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_ota_inject_copy_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.fault_injection_flags);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_ota_inject_metadata_corrupt(scpi_t *context)
{
    uint32_t copy_index;
    if (!scpi_port_read_u32(context, &copy_index)) {
        return SCPI_RES_ERR;
    }

    return ota_metadata_corrupt_copy(copy_index) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_ota_inject_metadata_repair(scpi_t *context)
{
    return ota_metadata_repair_copies() ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}
#endif

/* ── 触发测量 (同步自检) ── */

static scpi_result_t scpi_cmd_meas_freq_q(scpi_t *context)
{
    uint32_t gate_ms = 1000u;
    scpi_port_read_u32(context, &gate_ms);
    if (gate_ms < 10u || gate_ms > 60000u) {
        gate_ms = 1000u;
    }
    const uint32_t freq_hz = trigger_measure_quick_freq_hz(gate_ms);
    SCPI_ResultUInt32(context, freq_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_meas_report_q(scpi_t *context)
{
    trigger_measure_report_t report;
    if (!trigger_measure_get_report(&report)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, report.freq_hz);
    SCPI_ResultUInt32(context, report.period_ns);
    SCPI_ResultUInt32(context, report.trigger_count);
    SCPI_ResultUInt32(context, report.elapsed_us);
    SCPI_ResultUInt32(context, report.jitter_est_ppm);
    return SCPI_RES_OK;
}

static const scpi_command_t s_scpi_commands[] = {
    {.pattern = "*CLS", .callback = SCPI_CoreCls},
    {.pattern = "*ESE", .callback = SCPI_CoreEse},
    {.pattern = "*ESE?", .callback = SCPI_CoreEseQ},
    {.pattern = "*ESR?", .callback = SCPI_CoreEsrQ},
    {.pattern = "*IDN?", .callback = SCPI_CoreIdnQ},
    {.pattern = "*OPC", .callback = SCPI_CoreOpc},
    {.pattern = "*OPC?", .callback = SCPI_CoreOpcQ},
    {.pattern = "*RST", .callback = SCPI_CoreRst},
    {.pattern = "*SRE", .callback = SCPI_CoreSre},
    {.pattern = "*SRE?", .callback = SCPI_CoreSreQ},
    {.pattern = "*STB?", .callback = SCPI_CoreStbQ},
    {.pattern = "*TST?", .callback = scpi_cmd_core_tst_q},
    {.pattern = "*WAI", .callback = SCPI_CoreWai},
    {.pattern = "SYSTem:ERRor[:NEXT]?", .callback = SCPI_SystemErrorNextQ},
    {.pattern = "SYSTem:ERRor:COUNt?", .callback = SCPI_SystemErrorCountQ},
    {.pattern = "SYSTem:VERSion?", .callback = SCPI_SystemVersionQ},
    {.pattern = "SYSTem:FW:VERSion?", .callback = scpi_cmd_firmware_version_q},
    {.pattern = "SYSTem:FW:BUILD?", .callback = scpi_cmd_firmware_build_q},
    {.pattern = "SYSTem:BOOT:VERSion?", .callback = scpi_cmd_bootloader_version_q},
    {.pattern = "SYSTem:BOOT:CAPability?", .callback = scpi_cmd_bootloader_capability_q},
#if PROJECT_ENABLE_OTA_FAULT_INJECTION
    {.pattern = "SYSTem:BOOT:RESet", .callback = scpi_cmd_boot_reset},
#endif
    {.pattern = "TRIGger:WIDTh", .callback = scpi_cmd_trigger_width},
    {.pattern = "TRIGger:WIDTh?", .callback = scpi_cmd_trigger_width_q},
    {.pattern = "TRIGger:IMMediate", .callback = scpi_cmd_trigger_fire},
    {.pattern = "PULSe:WIDTh", .callback = scpi_cmd_pulse_width},
    {.pattern = "PULSe:WIDTh?", .callback = scpi_cmd_pulse_width_q},
    {.pattern = "PULSe:IMMediate", .callback = scpi_cmd_pulse_fire},
    {.pattern = "MARKer:WIDTh", .callback = scpi_cmd_marker_width},
    {.pattern = "MARKer:WIDTh?", .callback = scpi_cmd_marker_width_q},
    {.pattern = "MARKer:IMMediate", .callback = scpi_cmd_marker_fire},
    {.pattern = "SAMPle:RATE", .callback = scpi_cmd_sample_rate},
    {.pattern = "SAMPle:RATE?", .callback = scpi_cmd_sample_rate_q},
    {.pattern = "SAMPle:STATe", .callback = scpi_cmd_sample_state},
    {.pattern = "SAMPle:STATe?", .callback = scpi_cmd_sample_state_q},
    {.pattern = "OUTPut:CLOCk:FREQuency", .callback = scpi_cmd_clock_freq},
    {.pattern = "OUTPut:CLOCk:FREQuency?", .callback = scpi_cmd_clock_freq_q},
    {.pattern = "OUTPut:CLOCk:STATe", .callback = scpi_cmd_clock_state},
    {.pattern = "OUTPut:CLOCk:STATe?", .callback = scpi_cmd_clock_state_q},
    {.pattern = "STATus:SYNC?", .callback = scpi_cmd_status_q},
    {.pattern = "TRIGger:SOURce", .callback = scpi_cmd_trigger_source},
    {.pattern = "TRIGger:SOURce?", .callback = scpi_cmd_trigger_source_q},
    {.pattern = "TRIGger:EDGE", .callback = scpi_cmd_trigger_edge},
    {.pattern = "TRIGger:EDGE?", .callback = scpi_cmd_trigger_edge_q},
    {.pattern = "TRIGger:GATE", .callback = scpi_cmd_trigger_gate},
    {.pattern = "TRIGger:GATE?", .callback = scpi_cmd_trigger_gate_q},
    {.pattern = "TRIGger:SAFE", .callback = scpi_cmd_trigger_safe},
    {.pattern = "TRIGger:SAFE?", .callback = scpi_cmd_trigger_safe_q},
    {.pattern = "TRIGger:MODE", .callback = scpi_cmd_trigger_mode},
    {.pattern = "TRIGger:MODE?", .callback = scpi_cmd_trigger_mode_q},
    {.pattern = "TRIGger:SEQ:LENGth", .callback = scpi_cmd_trigger_seq_length},
    {.pattern = "TRIGger:SEQ:LENGth?", .callback = scpi_cmd_trigger_seq_length_q},
    {.pattern = "TRIGger:SEQ:WIDTh", .callback = scpi_cmd_trigger_seq_width},
    {.pattern = "TRIGger:SEQ:WIDTh?", .callback = scpi_cmd_trigger_seq_width_q},
    {.pattern = "TRIGger:SEQ:INDex?", .callback = scpi_cmd_trigger_seq_index_q},
    {.pattern = "TRIGger:SEQ:DATA", .callback = scpi_cmd_trigger_seq_data},
    {.pattern = "TRIGger:SEQ:DATA?", .callback = scpi_cmd_trigger_seq_data_q},
    {.pattern = "TRIGger:ARM", .callback = scpi_cmd_trigger_arm},
    {.pattern = "TRIGger:DISarm", .callback = scpi_cmd_trigger_disarm},
    {.pattern = "TRIGger:FAULT", .callback = scpi_cmd_trigger_fault},
    {.pattern = "TRIGger:ENC:TARGet", .callback = scpi_cmd_enc_target},
    {.pattern = "TRIGger:ENC:TARGet?", .callback = scpi_cmd_enc_target_q},
    {.pattern = "TRIGger:ENC:COUNt?", .callback = scpi_cmd_enc_count_q},
    {.pattern = "TRIGger:ENC:APIN", .callback = scpi_cmd_enc_a_pin},
    {.pattern = "TRIGger:ENC:APIN?", .callback = scpi_cmd_enc_a_pin_q},
    {.pattern = "TRIGger:ENC:REVolution?", .callback = scpi_cmd_enc_rev_q},
    {.pattern = "TRIGger:PCNT:DECode", .callback = scpi_cmd_pcnt_decode},
    {.pattern = "TRIGger:PCNT:DECode?", .callback = scpi_cmd_pcnt_decode_q},
    {.pattern = "TRIGger:PCNT:DIRection", .callback = scpi_cmd_pcnt_dir},
    {.pattern = "TRIGger:PCNT:DIRection?", .callback = scpi_cmd_pcnt_dir_q},
    {.pattern = "TRIGger:PCNT:FILTer", .callback = scpi_cmd_pcnt_filter},
    {.pattern = "TRIGger:PCNT:FILTer?", .callback = scpi_cmd_pcnt_filter_q},
    {.pattern = "TRIGger:PCNT:GATE", .callback = scpi_cmd_pcnt_gate},
    {.pattern = "TRIGger:PCNT:GATE?", .callback = scpi_cmd_pcnt_gate_q},
    {.pattern = "TRIGger:PCNT:CMP", .callback = scpi_cmd_pcnt_cmp},
    {.pattern = "TRIGger:PCNT:CMP?", .callback = scpi_cmd_pcnt_cmp_q},
    {.pattern = "TRIGger:PCNT:PRESet", .callback = scpi_cmd_pcnt_preset},
    {.pattern = "TRIGger:PCNT:PRESet?", .callback = scpi_cmd_pcnt_preset_q},
    {.pattern = "TRIGger:PCNT:CLEar", .callback = scpi_cmd_pcnt_clear},
    {.pattern = "TRIGger:PCNT:TOTal?", .callback = scpi_cmd_pcnt_total_q},
    {.pattern = "TRIGger:PCNT:FREQuency?", .callback = scpi_cmd_pcnt_freq_q},
    {.pattern = "STATus:TRIGger?", .callback = scpi_cmd_trigger_status_q},
    {.pattern = "SYSTem:OTA:STATus?", .callback = scpi_cmd_ota_status_q},
    {.pattern = "SYSTem:OTA:PROGress?", .callback = scpi_cmd_ota_progress_q},
    {.pattern = "SYSTem:OTA:BEGIN", .callback = scpi_cmd_ota_begin},
    {.pattern = "SYSTem:OTA:PBEGIN", .callback = scpi_cmd_ota_package_begin},
    {.pattern = "SYSTem:OTA:DATA", .callback = scpi_cmd_ota_data},
    {.pattern = "SYSTem:OTA:END", .callback = scpi_cmd_ota_end},
    {.pattern = "SYSTem:OTA:ABORt", .callback = scpi_cmd_ota_abort},
    {.pattern = "SYSTem:OTA:BOOT", .callback = scpi_cmd_ota_boot},
    {.pattern = "SYSTem:OTA:COMMit", .callback = scpi_cmd_ota_commit},
    {.pattern = "SYSTem:OTA:SLOT?", .callback = scpi_cmd_ota_slot_q},
    {.pattern = "SYSTem:OTA:RESult?", .callback = scpi_cmd_ota_result_q},
    {.pattern = "SYSTem:OTA:TXN?", .callback = scpi_cmd_ota_transaction_q},
    {.pattern = "SYSTem:OTA:TRANsaction?", .callback = scpi_cmd_ota_transaction_q},
    {.pattern = "SYSTem:OTA:MODE?", .callback = scpi_cmd_ota_mode_q},
    {.pattern = "SYSTem:OTA:TARGet?", .callback = scpi_cmd_ota_target_q},
    {.pattern = "SYSTem:OTA:CAPability?", .callback = scpi_cmd_ota_capability_q},
    {.pattern = "SYSTem:SD:STATus?", .callback = scpi_cmd_storage_status_q},
    {.pattern = "SYSTem:SD:INFO?", .callback = scpi_cmd_storage_info_q},
    {.pattern = "SYSTem:SD:RAW:CLEar", .callback = scpi_cmd_storage_raw_clear},
    {.pattern = "SYSTem:SD:RAW:READ?", .callback = scpi_cmd_storage_raw_read_q},
    {.pattern = "SYSTem:SD:MKFS", .callback = scpi_cmd_storage_mkfs},
    {.pattern = "SYSTem:SD:INITialize", .callback = scpi_cmd_storage_init},
    {.pattern = "SYSTem:SD:MANifest?", .callback = scpi_cmd_storage_manifest_q},
    {.pattern = "SYSTem:STORage:JOB:INFO", .callback = scpi_cmd_storage_job_info},
    {.pattern = "SYSTem:STORage:JOB?", .callback = scpi_cmd_storage_job_q},
    {.pattern = "SYSTem:SNAPshot:WRITe", .callback = scpi_cmd_snapshot_write},
    {.pattern = "SYSTem:SNAPshot:LAST?", .callback = scpi_cmd_snapshot_last_q},
    {.pattern = "SYSTem:TRACe:LAST?", .callback = scpi_cmd_trace_last_q},
    {.pattern = "SYSTem:FAULT:LAST?", .callback = scpi_cmd_fault_last_q},
    {.pattern = "SYSTem:STORage:STATus?", .callback = scpi_cmd_storage_status_q},
    {.pattern = "MMEMory:CATalog:PAGE?", .callback = scpi_cmd_mmem_catalog_page_q},
    {.pattern = "MMEMory:CATalog?", .callback = scpi_cmd_mmem_catalog_q},
    {.pattern = "MMEMory:INFO?", .callback = scpi_cmd_mmem_info_q},
    {.pattern = "MMEMory:READ?", .callback = scpi_cmd_mmem_read_q},
#if PROJECT_ENABLE_OTA_FAULT_INJECTION
    {.pattern = "SYSTem:OTA:MODE", .callback = scpi_cmd_ota_mode},
    {.pattern = "SYSTem:OTA:INJect:COPY", .callback = scpi_cmd_ota_inject_copy},
    {.pattern = "SYSTem:OTA:INJect:CLEar", .callback = scpi_cmd_ota_inject_clear},
    {.pattern = "SYSTem:OTA:INJect:COPY?", .callback = scpi_cmd_ota_inject_copy_q},
    {.pattern = "SYSTem:OTA:INJect:MCORrupt", .callback = scpi_cmd_ota_inject_metadata_corrupt},
    {.pattern = "SYSTem:OTA:INJect:MREPair", .callback = scpi_cmd_ota_inject_metadata_repair},
#endif
    {.pattern = "MEASure:FREQuency?", .callback = scpi_cmd_meas_freq_q},
    {.pattern = "MEASure:REPort?", .callback = scpi_cmd_meas_report_q},
    SCPI_CMD_LIST_END,
};

static scpi_interface_t s_scpi_interface = {
    .error = scpi_port_error,
    .write = scpi_port_write,
    .control = scpi_port_control,
    .flush = scpi_port_flush,
    .reset = scpi_port_reset,
};

bool scpi_port_init(void)
{
    SCPI_Init(&s_scpi_context,
              s_scpi_commands,
              &s_scpi_interface,
              scpi_units_def,
              SCPI_PORT_IDN_VENDOR,
              SCPI_PORT_IDN_MODEL,
              SCPI_PORT_IDN_SERIAL,
              PROJECT_NAME,
              s_scpi_input_buffer,
              sizeof(s_scpi_input_buffer),
              s_scpi_error_queue,
              SCPI_PORT_ERROR_QUEUE_SIZE);

    LOG_INFO("scpi", "SCPI service initialized");
    return true;
}

void scpi_port_service(void)
{
    char buffer[SCPI_PORT_POLL_CHARS];
    size_t count = 0u;

    while (count < sizeof(buffer)) {
        const int ch = getchar_timeout_us(0u);
        if (ch == PICO_ERROR_TIMEOUT) {
            break;
        }
        if (ch < 0) {
            break;
        }
        buffer[count] = (char)ch;
        count++;
    }

    if (count > 0u) {
        SCPI_Input(&s_scpi_context, buffer, (int)count);
    }
}

void scpi_port_get_config(scpi_port_config_t *config)
{
    if (config == NULL) {
        return;
    }

    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);

    config->trigger_width_us = summary.trigger_width_us;
    config->pulse_width_us = summary.pulse_width_us;
    config->marker_width_us = summary.marker_width_us;
    config->capture_sample_hz = summary.capture_sample_hz;
    config->sync_clock_hz = summary.sync_clock_hz;
    config->sync_clock_enabled = summary.sync_clock_enabled;
}
