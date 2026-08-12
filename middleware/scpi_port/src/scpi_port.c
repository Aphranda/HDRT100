#include "scpi_port.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "diagnostics.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "drv_watchdog.h"
#include "pico/unique_id.h"
#include "pico/error.h"
#include "pico/stdio.h"
#include "project_config.h"
#include "scpi_calibration_commands.h"
#include "scpi_communication_biss_commands.h"
#include "scpi_config_commands.h"
#include "scpi/scpi.h"
#include "scpi_loop_engine_commands.h"
#include "scpi_measure_commands.h"
#include "scpi_ota_commands.h"
#include "scpi_port_internal.h"
#include "scpi_sync_commands.h"
#include "scpi_storage_commands.h"
#include "scpi_system_access_commands.h"
#include "scpi_system_diagnostics_commands.h"
#include "scpi_system_runtime_commands.h"
#include "scpi_system_snapshot_commands.h"
#include "scpi_trigger_commands.h"
#include "scpi_usb_control.h"
#include "storage_manager.h"
#include "sync_trigger.h"
#include "sync_io_hw_profile.h"

#define SCPI_PORT_INPUT_BUFFER_LENGTH 768u
#define SCPI_PORT_ERROR_QUEUE_SIZE    16
#define SCPI_PORT_IDN_VENDOR          PROJECT_VENDOR_NAME
#define SCPI_PORT_IDN_MODEL           PROJECT_MODEL_NAME
#define SCPI_PORT_IDN_SERIAL          NULL
#define SCPI_PORT_POLL_CHARS          32u

static scpi_t s_scpi_context;
static char s_scpi_input_buffer[SCPI_PORT_INPUT_BUFFER_LENGTH];
static scpi_error_t s_scpi_error_queue[SCPI_PORT_ERROR_QUEUE_SIZE];
static char s_scpi_idn_serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2u + 1u];
static char *s_scpi_capture_buffer;
static size_t s_scpi_capture_capacity;
static size_t s_scpi_capture_len;
static bool s_scpi_capture_truncated;
static scpi_port_write_fn_t s_scpi_stream_write;
static scpi_port_flush_fn_t s_scpi_stream_flush;
static void *s_scpi_stream_context;
static volatile uint32_t s_scpi_trigger_debug_stage;
static volatile uint32_t s_scpi_trigger_debug_mode;
static volatile uint32_t s_scpi_trigger_debug_posted;

static bool scpi_port_trigger_is_armed(void);
static void scpi_port_flush_output(void);

static size_t scpi_port_write(scpi_t *context, const char *data, size_t len)
{
    (void)context;
    if (s_scpi_capture_buffer != NULL) {
        const size_t space = s_scpi_capture_capacity > s_scpi_capture_len ?
                             s_scpi_capture_capacity - s_scpi_capture_len :
                             0u;
        const size_t copy_len = len < space ? len : space;
        if (copy_len > 0u) {
            memcpy(&s_scpi_capture_buffer[s_scpi_capture_len], data, copy_len);
            s_scpi_capture_len += copy_len;
        }
        if (copy_len < len) {
            s_scpi_capture_truncated = true;
        }
        return len;
    }

    if (s_scpi_stream_write != NULL) {
        return s_scpi_stream_write(data, len, s_scpi_stream_context);
    }

    for (size_t i = 0u; i < len; i++) {
        putchar_raw(data[i]);
    }
    return len;
}

static scpi_result_t scpi_port_flush(scpi_t *context)
{
    (void)context;
    scpi_port_flush_output();
    return SCPI_RES_OK;
}

static void scpi_port_flush_output(void)
{
    if (s_scpi_stream_flush != NULL) {
        s_scpi_stream_flush(s_scpi_stream_context);
        return;
    }

    stdio_flush();
}

void scpi_port_flush_now(void)
{
    scpi_port_flush_output();
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

bool scpi_port_read_u32(scpi_t *context, uint32_t *value)
{
    return SCPI_ParamUInt32(context, value, TRUE) == TRUE;
}

scpi_result_t scpi_port_result_ok(scpi_t *context)
{
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_port_result_accepted(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
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
           vector.state == TRIG_STATE_ENC_ARMED ||
           vector.state == TRIG_STATE_BISS_ARMED;
}

static bool scpi_port_biss_role_supported(trig_biss_role_t role)
{
    return role == TRIG_BISS_ROLE_TAP_MONITOR;
}

void scpi_port_push_exec_error(scpi_t *context, const char *info)
{
    SCPI_ErrorPushEx(context,
                     SCPI_ERROR_EXECUTION_ERROR,
                     (char *)info,
                     info != NULL ? strlen(info) : 0u);
}

bool scpi_port_reject_if_run_forbidden(scpi_t *context, uint32_t class_id)
{
    if (!scpi_port_trigger_is_armed()) {
        return false;
    }

    if (distributed_config_scpi_run_class_allowed(class_id, false)) {
        return false;
    }

    char error_text[32];
    snprintf(error_text,
             sizeof(error_text),
             "RUN_STATE_DENIED:%lu",
             (unsigned long)distributed_config_scpi_run_class_forbid_code(class_id));
    scpi_port_push_exec_error(context, error_text);
    return true;
}

void scpi_port_get_trigger_debug_snapshot(uint32_t *stage,
                                          uint32_t *mode,
                                          uint32_t *posted)
{
    if (stage != NULL) {
        *stage = s_scpi_trigger_debug_stage;
    }
    if (mode != NULL) {
        *mode = s_scpi_trigger_debug_mode;
    }
    if (posted != NULL) {
        *posted = s_scpi_trigger_debug_posted;
    }
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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_FIRE_TRIGGER, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_pulse_width(scpi_t *context)
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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_FIRE_PULSE, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_marker_width(scpi_t *context)
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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_FIRE_MARKER, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_rj45_trigger_width(scpi_t *context)
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

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_SET_RJ45_TRIGGER_WIDTH,
                                        value) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_rj45_trigger_width_q(scpi_t *context)
{
    sync_trigger_summary_t summary;
    scpi_port_get_trigger_summary(&summary);
    SCPI_ResultUInt32(context, summary.rj45_trigger_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_rj45_trigger_fire(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_post_trigger_event(SYNC_TRIGGER_EVENT_FIRE_RJ45_TRIGGER, 0u) ?
               SCPI_RES_OK :
               SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_rj45_trigger_pins_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, SYNC_IO_HW_RJ45_TRIG_IN_PIN);
    SCPI_ResultUInt32(context, SYNC_IO_HW_RJ45_TRIG_OUT_PIN);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_sample_rate(scpi_t *context)
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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
static uint32_t        s_product_trigger_mode;

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

static const char *scpi_trigger_control_mode_to_string(uint32_t mode)
{
    switch (mode) {
    case 0u: return "IDLE";
    case 1u: return "TRIG";
    case 2u: return "CAL";
    case 3u: return "SYNC";
    case 4u: return "SIM";
    default: return "UNKNOWN";
    }
}

static const char *scpi_trigger_control_state_to_string(trig_state_t state)
{
    switch (state) {
    case TRIG_STATE_IDLE:           return "IDLE";
    case TRIG_STATE_SEQ_CONFIGURED:
    case TRIG_STATE_ENC_CONFIGURED:
    case TRIG_STATE_BISS_CONFIGURED:return "ARMED";
    case TRIG_STATE_SEQ_ARMED:
    case TRIG_STATE_ENC_ARMED:
    case TRIG_STATE_BISS_ARMED:     return "RUN";
    case TRIG_STATE_FAULT:          return "FAULT";
    default:                        return "UNKNOWN";
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

static scpi_result_t scpi_cmd_trigger_mode(scpi_t *context)
{
    uint32_t mode = 0u;
    if (!scpi_port_read_u32(context, &mode) || mode > 4u) {
        return SCPI_RES_ERR;
    }
    s_product_trigger_mode = mode;
    s_scpi_trigger_debug_mode = mode;
    s_scpi_trigger_debug_stage = 100u + mode;

    if (mode == 0u) {
        const trig_event_t event = { .type = TRIG_EVENT_RESET };
        s_scpi_trigger_debug_posted = sync_trigger_post(&event) ? 1u : 0u;
    } else {
        s_scpi_trigger_debug_posted = 0u;
    }

    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_mode_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, scpi_trigger_control_mode_to_string(s_product_trigger_mode));
    SCPI_ResultUInt32(context, s_product_trigger_mode);
    SCPI_ResultText(context, scpi_trigger_control_state_to_string(vector.state));
    SCPI_ResultText(context, "ALLOW");
    SCPI_ResultText(context, vector.error_code == TRIG_ERROR_NONE ? "NONE" : "ERROR");
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_seq_length(scpi_t *context)
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

static scpi_result_t scpi_cmd_trigger_seq_length_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.seq_length);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_seq_width(scpi_t *context)
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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    s_scpi_trigger_debug_stage = 200u;
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    s_scpi_trigger_debug_stage = 201u;
    if (vector.state == TRIG_STATE_BISS_CONFIGURED &&
        !scpi_port_biss_role_supported(vector.biss_role)) {
        s_scpi_trigger_debug_stage = 202u;
        scpi_port_push_exec_error(context, "BISS_ROLE_NOT_IMPLEMENTED");
        return SCPI_RES_ERR;
    }

    if (vector.state == TRIG_STATE_IDLE && s_product_trigger_mode == 1u) {
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
            s_scpi_trigger_debug_posted = 0u;
            return SCPI_RES_ERR;
        }
    }

    const trig_event_t event = { .type = TRIG_EVENT_ARM };
    storage_manager_trace_event(2u, 10u, 1u, 0u, 0u);
    uint32_t job_id = 0u;
    (void)storage_manager_post_snapshot_job("arm", &job_id);
    s_scpi_trigger_debug_stage = 210u;
    const bool posted = sync_trigger_post(&event);
    s_scpi_trigger_debug_posted = posted ? 1u : 0u;
    s_scpi_trigger_debug_stage = 211u;
    return posted ? scpi_port_result_ok(context) : SCPI_RES_ERR;
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

static scpi_result_t scpi_cmd_trigger_source_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.trigger_source_pin);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_edge(scpi_t *context)
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

static scpi_result_t scpi_cmd_trigger_gate_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultBool(context, vector.gate_enabled ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_safe(scpi_t *context)
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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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
    SCPI_SYSTEM_RUNTIME_COMMANDS,
    {.pattern = "*WAI", .callback = SCPI_CoreWai},
    {.pattern = "SYSTem:ERRor[:NEXT]?", .callback = SCPI_SystemErrorNextQ},
    {.pattern = "SYSTem:ERRor:COUNt?", .callback = SCPI_SystemErrorCountQ},
    {.pattern = "SYSTem:VERSion?", .callback = SCPI_SystemVersionQ},
    SCPI_SYSTEM_DIAGNOSTICS_COMMANDS,
    SCPI_SYSTEM_SNAPSHOT_COMMANDS,
    SCPI_SYSTEM_ACCESS_COMMANDS,
    SCPI_LOOP_ENGINE_COMMANDS,
#if PROJECT_ENABLE_OTA_FAULT_INJECTION
    {.pattern = "SYSTem:BOOT:RESet", .callback = scpi_cmd_boot_reset},
#endif
    SCPI_CONFIG_COMMANDS,
    SCPI_SYSTEM_DIAGNOSTICS_READ_COMMANDS,
    SCPI_CALIBRATION_COMMANDS,
    SCPI_SYNC_COMMANDS,
    {.pattern = "TRIGger:WIDTh", .callback = scpi_cmd_trigger_width},
    {.pattern = "TRIGger:WIDTh?", .callback = scpi_cmd_trigger_width_q},
    {.pattern = "TRIGger:IMMediate", .callback = scpi_cmd_trigger_fire},
    {.pattern = "PULSe:WIDTh", .callback = scpi_cmd_pulse_width},
    {.pattern = "PULSe:WIDTh?", .callback = scpi_cmd_pulse_width_q},
    {.pattern = "PULSe:IMMediate", .callback = scpi_cmd_pulse_fire},
    {.pattern = "MARKer:WIDTh", .callback = scpi_cmd_marker_width},
    {.pattern = "MARKer:WIDTh?", .callback = scpi_cmd_marker_width_q},
    {.pattern = "MARKer:IMMediate", .callback = scpi_cmd_marker_fire},
    {.pattern = "RJ45:TRIGger:WIDTh", .callback = scpi_cmd_rj45_trigger_width},
    {.pattern = "RJ45:TRIGger:WIDTh?", .callback = scpi_cmd_rj45_trigger_width_q},
    {.pattern = "RJ45:TRIGger:IMMediate", .callback = scpi_cmd_rj45_trigger_fire},
    {.pattern = "RJ45:TRIGger:PINs?", .callback = scpi_cmd_rj45_trigger_pins_q},
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
    SCPI_TRIGGER_COMMANDS,
    {.pattern = "TRIGger:SEQ:LENGth", .callback = scpi_cmd_trigger_seq_length},
    {.pattern = "TRIGger:SEQ:LENGth?", .callback = scpi_cmd_trigger_seq_length_q},
    {.pattern = "TRIGger:SEQ:WIDTh", .callback = scpi_cmd_trigger_seq_width},
    {.pattern = "TRIGger:SEQ:WIDTh?", .callback = scpi_cmd_trigger_seq_width_q},
    {.pattern = "TRIGger:SEQ:INDex?", .callback = scpi_cmd_trigger_seq_index_q},
    {.pattern = "TRIGger:SEQ:DATA", .callback = scpi_cmd_trigger_seq_data},
    {.pattern = "TRIGger:SEQ:DATA?", .callback = scpi_cmd_trigger_seq_data_q},
    {.pattern = "TRIGger:ARM", .callback = scpi_cmd_trigger_arm},
    {.pattern = "TRIGger:DISarm", .callback = scpi_cmd_trigger_disarm},
    {.pattern = "TRIGger:DISAble", .callback = scpi_cmd_trigger_disarm},
    {.pattern = "TRIGger:FAULT", .callback = scpi_cmd_trigger_fault},
    {.pattern = "TRIGger:ENC:TARGet", .callback = scpi_cmd_enc_target},
    {.pattern = "TRIGger:ENC:TARGet?", .callback = scpi_cmd_enc_target_q},
    {.pattern = "TRIGger:ENC:COUNt?", .callback = scpi_cmd_enc_count_q},
    {.pattern = "TRIGger:ENC:APIN", .callback = scpi_cmd_enc_a_pin},
    {.pattern = "TRIGger:ENC:APIN?", .callback = scpi_cmd_enc_a_pin_q},
    {.pattern = "TRIGger:ENC:REVolution?", .callback = scpi_cmd_enc_rev_q},
    SCPI_COMMUNICATION_BISS_COMMANDS,
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
    SCPI_OTA_COMMANDS,
#if PROJECT_ENABLE_USB_RUNTIME_SWITCH
    SCPI_USB_CONTROL_COMMANDS,
#endif
    SCPI_STORAGE_COMMANDS,
    SCPI_MEASURE_COMMANDS,
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
    pico_get_unique_board_id_string(s_scpi_idn_serial, sizeof(s_scpi_idn_serial));
    SCPI_Init(&s_scpi_context,
              s_scpi_commands,
              &s_scpi_interface,
              scpi_units_def,
              SCPI_PORT_IDN_VENDOR,
              SCPI_PORT_IDN_MODEL,
              s_scpi_idn_serial,
              PROJECT_VERSION_STRING,
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

void scpi_port_feed(const char *data, size_t len)
{
    if (data == NULL || len == 0u) {
        return;
    }

    SCPI_Input(&s_scpi_context, data, (int)len);
}

void scpi_port_set_stream(scpi_port_write_fn_t write_fn, scpi_port_flush_fn_t flush_fn, void *context)
{
    s_scpi_stream_write = write_fn;
    s_scpi_stream_flush = flush_fn;
    s_scpi_stream_context = context;
}

bool scpi_port_execute(const char *data,
                       size_t len,
                       char *response,
                       size_t response_capacity,
                       size_t *response_len)
{
    if (data == NULL || response_len == NULL ||
        (response == NULL && response_capacity > 0u) ||
        len > (size_t)INT_MAX) {
        return false;
    }

    s_scpi_capture_buffer = response;
    s_scpi_capture_capacity = response_capacity;
    s_scpi_capture_len = 0u;
    s_scpi_capture_truncated = false;

    SCPI_Input(&s_scpi_context, data, (int)len);

    *response_len = s_scpi_capture_len;
    const bool ok = !s_scpi_capture_truncated;

    s_scpi_capture_buffer = NULL;
    s_scpi_capture_capacity = 0u;
    s_scpi_capture_len = 0u;
    s_scpi_capture_truncated = false;

    return ok;
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
    config->rj45_trigger_width_us = summary.rj45_trigger_width_us;
    config->marker_width_us = summary.marker_width_us;
    config->capture_sample_hz = summary.capture_sample_hz;
    config->sync_clock_hz = summary.sync_clock_hz;
    config->sync_clock_enabled = summary.sync_clock_enabled;
}
