#include "scpi_port.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "diagnostics.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "drv_watchdog.h"
#include "biss_protocol.h"
#include "ota_ao.h"
#include "osal.h"
#include "pico/unique_id.h"
#include "pico/error.h"
#include "pico/stdio.h"
#include "project_config.h"
#include "resource_arbiter.h"
#include "scpi_calibration_commands.h"
#include "scpi_config_commands.h"
#include "scpi/scpi.h"
#include "scpi_loop_engine_commands.h"
#include "scpi_ota_commands.h"
#include "scpi_port_internal.h"
#include "scpi_product_commands.h"
#include "scpi_report_commands.h"
#include "scpi_sync_commands.h"
#include "scpi_storage_commands.h"
#include "scpi_system_snapshot_commands.h"
#include "scpi_trigger_commands.h"
#include "scpi_usb_control.h"
#include "storage_manager.h"
#include "sync_trigger.h"
#include "sync_io_hw_profile.h"
#include "trigger_measure.h"

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

static scpi_result_t scpi_cmd_core_tst_q(scpi_t *context)
{
    SCPI_ResultInt32(context, 0);
    return SCPI_RES_OK;
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

static const char *scpi_port_owner_or_dash(const char *owner)
{
    return owner != NULL ? owner : "-";
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

static bool scpi_port_biss_is_armed(void)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    return vector.state == TRIG_STATE_BISS_ARMED;
}

static bool scpi_port_biss_role_supported(trig_biss_role_t role)
{
    return role == TRIG_BISS_ROLE_TAP_MONITOR;
}

static void scpi_port_push_exec_error(scpi_t *context, const char *info)
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

static const char *scpi_diag_level_to_string(diag_level_t level)
{
    switch (level) {
    case DIAG_LEVEL_DEBUG: return "DEBUG";
    case DIAG_LEVEL_INFO:  return "INFO";
    case DIAG_LEVEL_WARN:  return "WARN";
    case DIAG_LEVEL_ERROR: return "ERROR";
    default:               return "UNKNOWN";
    }
}

static scpi_result_t scpi_cmd_log_level(scpi_t *context)
{
    uint32_t level;
    if (!scpi_port_read_u32(context, &level) ||
        level >= (uint32_t)DIAG_LEVEL_COUNT ||
        !diagnostics_set_min_level((diag_level_t)level)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_cmd_log_level_q(scpi_t *context)
{
    const diag_level_t level = diagnostics_get_min_level();
    SCPI_ResultText(context, scpi_diag_level_to_string(level));
    SCPI_ResultUInt32(context, (uint32_t)level);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_log_status_q(scpi_t *context)
{
    diagnostics_status_t status;
    diagnostics_get_status(&status);

    SCPI_ResultText(context, scpi_diag_level_to_string(status.min_level));
    SCPI_ResultUInt32(context, (uint32_t)status.min_level);
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        SCPI_ResultUInt32(context, status.emitted_count[i]);
    }
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        SCPI_ResultUInt32(context, status.dropped_count[i]);
    }
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        SCPI_ResultUInt32(context, status.truncated_count[i]);
    }
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        SCPI_ResultUInt32(context, status.emit_failed_count[i]);
    }
    SCPI_ResultUInt32(context, status.queue_dropped_count);
    SCPI_ResultUInt32(context, status.queue_bytes);
    SCPI_ResultUInt32(context, status.queue_high_watermark);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_core_status_q(scpi_t *context)
{
    diagnostics_core_status_t status;
    diagnostics_get_core_status(&status);

    SCPI_ResultBool(context, status.core1_enabled ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.core0_loop_count);
    SCPI_ResultUInt32(context, status.core1_loop_count);
    SCPI_ResultUInt32(context, status.core0_last_ms);
    SCPI_ResultUInt32(context, status.core1_last_ms);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_rtos_status_q(scpi_t *context)
{
    uint32_t heap_free = 0u;
    uint32_t heap_min_free = 0u;
    osal_task_stats_t task_stats[16];
    const uint32_t task_count =
        osal_task_get_stats(task_stats,
                            (uint32_t)(sizeof(task_stats) / sizeof(task_stats[0])));

    osal_heap_get_status(&heap_free, &heap_min_free);
    SCPI_ResultUInt32(context, heap_free);
    SCPI_ResultUInt32(context, heap_min_free);
    SCPI_ResultUInt32(context, task_count);

    for (uint32_t i = 0u; i < task_count; i++) {
        const osal_task_stats_t *task = &task_stats[i];
        const uint32_t used_words = task->stack_words > task->stack_free_words ?
                                    task->stack_words - task->stack_free_words :
                                    0u;

        SCPI_ResultText(context, task->name != NULL ? task->name : "-");
        SCPI_ResultUInt32(context, task->stack_words);
        SCPI_ResultUInt32(context, task->stack_free_words);
        SCPI_ResultUInt32(context, used_words);
        SCPI_ResultUInt32(context, task->priority);
    }

    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_debug_q(scpi_t *context)
{
    uint32_t sync_stage = 0u;
    uint32_t sync_event = 0u;
    uint32_t sync_state = 0u;
    uint32_t sync_error = 0u;
    sync_trigger_get_debug(&sync_stage, &sync_event, &sync_state, &sync_error);

    SCPI_ResultUInt32(context, s_scpi_trigger_debug_stage);
    SCPI_ResultUInt32(context, s_scpi_trigger_debug_mode);
    SCPI_ResultUInt32(context, s_scpi_trigger_debug_posted);
    SCPI_ResultUInt32(context, sync_stage);
    SCPI_ResultUInt32(context, sync_event);
    SCPI_ResultUInt32(context, sync_state);
    SCPI_ResultUInt32(context, sync_error);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_resource_status_q(scpi_t *context)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);

    SCPI_ResultUInt32(context, snapshot.active_resources);
    SCPI_ResultUInt32(context, snapshot.last_conflict_resources);
    SCPI_ResultText(context, scpi_port_owner_or_dash(snapshot.last_conflict_owner));
    SCPI_ResultText(context, scpi_port_owner_or_dash(snapshot.last_conflict_holder));
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

static const char *scpi_product_trigger_mode_to_string(uint32_t mode)
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

static const char *scpi_product_trigger_state_to_string(trig_state_t state)
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
    SCPI_ResultText(context, scpi_product_trigger_mode_to_string(s_product_trigger_mode));
    SCPI_ResultUInt32(context, s_product_trigger_mode);
    SCPI_ResultText(context, scpi_product_trigger_state_to_string(vector.state));
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

    if (scpi_port_biss_is_armed()) {
        scpi_port_push_exec_error(context, "BISS_ARMED");
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = type,
        .payload.value = value,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_biss_role(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ROLE,
                                 (uint32_t)TRIG_BISS_ROLE_TAP_MONITOR,
                                 (uint32_t)TRIG_BISS_ROLE_BRIDGE_PROXY);
}

static scpi_result_t scpi_cmd_biss_role_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, scpi_biss_role_to_string(vector.biss_role));
    SCPI_ResultUInt32(context, (uint32_t)vector.biss_role);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_device(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_DEVICE,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_device_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_device_id);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_clock(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CLOCK,
                                 1u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_clock_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_clock_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_frame_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_FRAME_BITS,
                                 1u,
                                 BISS_PROFILE_MAX_FRAME_BITS);
}

static scpi_result_t scpi_cmd_biss_frame_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_frame_bits);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_position_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_POSITION_OFFSET,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS - 1u);
}

static scpi_result_t scpi_cmd_biss_position_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_position_offset);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_position_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_POSITION_BITS,
                                 1u,
                                 BISS_PROFILE_MAX_POSITION_BITS);
}

static scpi_result_t scpi_cmd_biss_position_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_position_bits);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_position_modulo(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_POSITION_MODULO,
                                 1u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_position_modulo_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_position_modulo);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_sample_edge(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_EDGE,
                                 (uint32_t)BISS_SAMPLE_EDGE_RISING,
                                 (uint32_t)BISS_SAMPLE_EDGE_FALLING);
}

static scpi_result_t scpi_cmd_biss_sample_edge_q(scpi_t *context)
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

static scpi_result_t scpi_cmd_biss_sample_delay(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_DELAY,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_sample_delay_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_sample_delay_cycles);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_sample_scan(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_SCAN,
                                 0u,
                                 1u);
}

static scpi_result_t scpi_cmd_biss_sample_scan_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultBool(context, vector.biss_sample_scan_enabled ? TRUE : FALSE);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_enabled);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_sample_scan_start(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_SCAN_START,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_sample_scan_start_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_start_cycles);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_sample_scan_end(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_SCAN_END,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_sample_scan_end_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_end_cycles);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_sample_scan_step(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_SAMPLE_SCAN_STEP,
                                 1u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_sample_scan_step_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_sample_scan_step_cycles);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_timeout_us(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_TIMEOUT,
                                 1u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_timeout_us_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_timeout_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_anchor_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ANCHOR_OFFSET,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS - 1u);
}

static scpi_result_t scpi_cmd_biss_anchor_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_anchor_offset);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_anchor_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ANCHOR_BITS,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS);
}

static scpi_result_t scpi_cmd_biss_anchor_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_anchor_bits);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_anchor_mask(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ANCHOR_MASK,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_anchor_mask_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt64(context, vector.biss_anchor_mask);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_anchor_value(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ANCHOR_VALUE,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_anchor_value_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt64(context, vector.biss_anchor_value);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_error_bit(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_ERROR_BIT,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_error_bit_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_error_bit_offset);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_warning_bit(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_WARNING_BIT,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_warning_bit_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_warning_bit_offset);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_status_gate(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_STATUS_GATE,
                                 (uint32_t)BISS_STATUS_GATE_IGNORE,
                                 (uint32_t)BISS_STATUS_GATE_BLOCK_TRIGGER);
}

static scpi_result_t scpi_cmd_biss_status_gate_q(scpi_t *context)
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

static scpi_result_t scpi_cmd_biss_crc_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_OFFSET,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS - 1u);
}

static scpi_result_t scpi_cmd_biss_crc_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_offset);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_crc_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_BITS,
                                 0u,
                                 BISS_PROFILE_MAX_CRC_BITS);
}

static scpi_result_t scpi_cmd_biss_crc_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_bits);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_crc_cover_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_COVER_OFFSET,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS - 1u);
}

static scpi_result_t scpi_cmd_biss_crc_cover_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_cover_offset);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_crc_cover_bits(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_COVER_BITS,
                                 0u,
                                 BISS_PROFILE_MAX_FRAME_BITS);
}

static scpi_result_t scpi_cmd_biss_crc_cover_bits_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_cover_bits);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_crc_polynomial(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_POLYNOMIAL,
                                 0u,
                                 0xFFFFu);
}

static scpi_result_t scpi_cmd_biss_crc_polynomial_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_polynomial);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_crc_init(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_INIT,
                                 0u,
                                 0xFFFFu);
}

static scpi_result_t scpi_cmd_biss_crc_init_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_init);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_crc_xor(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_XOR,
                                 0u,
                                 0xFFFFu);
}

static scpi_result_t scpi_cmd_biss_crc_xor_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_crc_xor);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_crc_invert(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_INVERT,
                                 0u,
                                 1u);
}

static scpi_result_t scpi_cmd_biss_crc_invert_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultBool(context, vector.biss_crc_invert ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_crc_gate(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_CRC_GATE,
                                 (uint32_t)BISS_CRC_GATE_LATE_COUNT,
                                 (uint32_t)BISS_CRC_GATE_BLOCK_TRIGGER);
}

static scpi_result_t scpi_cmd_biss_crc_gate_q(scpi_t *context)
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

static scpi_result_t scpi_cmd_biss_latency_offset(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_LATENCY_OFFSET,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_latency_offset_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_latency_offset_ns);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_target(scpi_t *context)
{
    return scpi_cmd_biss_set_u32(context,
                                 TRIG_EVENT_SET_BISS_TARGET,
                                 0u,
                                 UINT32_MAX);
}

static scpi_result_t scpi_cmd_biss_target_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.biss_target);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_biss_pins_q(scpi_t *context)
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

static scpi_result_t scpi_cmd_biss_pulse_in(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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

static scpi_result_t scpi_cmd_biss_frame_rx(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

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

static scpi_result_t scpi_cmd_biss_crc_error(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = { .type = TRIG_EVENT_BISS_CRC_ERROR };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_biss_timeout_inject(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = { .type = TRIG_EVENT_BISS_TIMEOUT };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_biss_status_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, scpi_biss_role_to_string(vector.biss_role));
    SCPI_ResultUInt32(context, (uint32_t)vector.biss_role);
    SCPI_ResultText(context,
                    scpi_port_biss_role_supported(vector.biss_role) ?
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
    {.pattern = "SYSTem:LOG:LEVel", .callback = scpi_cmd_log_level},
    {.pattern = "SYSTem:LOG:LEVel?", .callback = scpi_cmd_log_level_q},
    {.pattern = "SYSTem:LOG:STATus?", .callback = scpi_cmd_log_status_q},
    SCPI_REPORT_SYSTEM_COMMANDS,
    {.pattern = "SYSTem:CORE?", .callback = scpi_cmd_core_status_q},
    {.pattern = "SYSTem:RTOS:STATus?", .callback = scpi_cmd_rtos_status_q},
    SCPI_SYSTEM_SNAPSHOT_COMMANDS,
    SCPI_PRODUCT_SYSTEM_PERMISSION_COMMANDS,
    SCPI_LOOP_ENGINE_COMMANDS,
    {.pattern = "SYSTem:TRIGger:DBG?", .callback = scpi_cmd_trigger_debug_q},
    {.pattern = "SYSTem:RESource?", .callback = scpi_cmd_resource_status_q},
#if PROJECT_ENABLE_OTA_FAULT_INJECTION
    {.pattern = "SYSTem:BOOT:RESet", .callback = scpi_cmd_boot_reset},
#endif
    SCPI_CONFIG_COMMANDS,
    SCPI_REPORT_READ_COMMANDS,
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
    {.pattern = "TRIGger:BISS:ROLE", .callback = scpi_cmd_biss_role},
    {.pattern = "TRIGger:BISS:ROLE?", .callback = scpi_cmd_biss_role_q},
    {.pattern = "TRIGger:BISS:DEVice", .callback = scpi_cmd_biss_device},
    {.pattern = "TRIGger:BISS:DEVice?", .callback = scpi_cmd_biss_device_q},
    {.pattern = "TRIGger:BISS:CLOCk", .callback = scpi_cmd_biss_clock},
    {.pattern = "TRIGger:BISS:CLOCk?", .callback = scpi_cmd_biss_clock_q},
    {.pattern = "TRIGger:BISS:FBITs", .callback = scpi_cmd_biss_frame_bits},
    {.pattern = "TRIGger:BISS:FBITs?", .callback = scpi_cmd_biss_frame_bits_q},
    {.pattern = "TRIGger:BISS:POFFset", .callback = scpi_cmd_biss_position_offset},
    {.pattern = "TRIGger:BISS:POFFset?", .callback = scpi_cmd_biss_position_offset_q},
    {.pattern = "TRIGger:BISS:PBITs", .callback = scpi_cmd_biss_position_bits},
    {.pattern = "TRIGger:BISS:PBITs?", .callback = scpi_cmd_biss_position_bits_q},
    {.pattern = "TRIGger:BISS:PMODulo", .callback = scpi_cmd_biss_position_modulo},
    {.pattern = "TRIGger:BISS:PMODulo?", .callback = scpi_cmd_biss_position_modulo_q},
    {.pattern = "TRIGger:BISS:SAMPle:EDGE", .callback = scpi_cmd_biss_sample_edge},
    {.pattern = "TRIGger:BISS:SAMPle:EDGE?", .callback = scpi_cmd_biss_sample_edge_q},
    {.pattern = "TRIGger:BISS:SAMPle:DELay", .callback = scpi_cmd_biss_sample_delay},
    {.pattern = "TRIGger:BISS:SAMPle:DELay?", .callback = scpi_cmd_biss_sample_delay_q},
    {.pattern = "TRIGger:BISS:SAMPle:SCAN", .callback = scpi_cmd_biss_sample_scan},
    {.pattern = "TRIGger:BISS:SAMPle:SCAN?", .callback = scpi_cmd_biss_sample_scan_q},
    {.pattern = "TRIGger:BISS:SAMPle:SCAN:STARt", .callback = scpi_cmd_biss_sample_scan_start},
    {.pattern = "TRIGger:BISS:SAMPle:SCAN:STARt?", .callback = scpi_cmd_biss_sample_scan_start_q},
    {.pattern = "TRIGger:BISS:SAMPle:SCAN:END", .callback = scpi_cmd_biss_sample_scan_end},
    {.pattern = "TRIGger:BISS:SAMPle:SCAN:END?", .callback = scpi_cmd_biss_sample_scan_end_q},
    {.pattern = "TRIGger:BISS:SAMPle:SCAN:STEP", .callback = scpi_cmd_biss_sample_scan_step},
    {.pattern = "TRIGger:BISS:SAMPle:SCAN:STEP?", .callback = scpi_cmd_biss_sample_scan_step_q},
    {.pattern = "TRIGger:BISS:TIMEout", .callback = scpi_cmd_biss_timeout_us},
    {.pattern = "TRIGger:BISS:TIMEout?", .callback = scpi_cmd_biss_timeout_us_q},
    {.pattern = "TRIGger:BISS:ANCHor:OFFSet", .callback = scpi_cmd_biss_anchor_offset},
    {.pattern = "TRIGger:BISS:ANCHor:OFFSet?", .callback = scpi_cmd_biss_anchor_offset_q},
    {.pattern = "TRIGger:BISS:ANCHor:BITS", .callback = scpi_cmd_biss_anchor_bits},
    {.pattern = "TRIGger:BISS:ANCHor:BITS?", .callback = scpi_cmd_biss_anchor_bits_q},
    {.pattern = "TRIGger:BISS:ANCHor:MASK", .callback = scpi_cmd_biss_anchor_mask},
    {.pattern = "TRIGger:BISS:ANCHor:MASK?", .callback = scpi_cmd_biss_anchor_mask_q},
    {.pattern = "TRIGger:BISS:ANCHor:VALue", .callback = scpi_cmd_biss_anchor_value},
    {.pattern = "TRIGger:BISS:ANCHor:VALue?", .callback = scpi_cmd_biss_anchor_value_q},
    {.pattern = "TRIGger:BISS:ERRor:BIT", .callback = scpi_cmd_biss_error_bit},
    {.pattern = "TRIGger:BISS:ERRor:BIT?", .callback = scpi_cmd_biss_error_bit_q},
    {.pattern = "TRIGger:BISS:WARNing:BIT", .callback = scpi_cmd_biss_warning_bit},
    {.pattern = "TRIGger:BISS:WARNing:BIT?", .callback = scpi_cmd_biss_warning_bit_q},
    {.pattern = "TRIGger:BISS:STATus:GATE", .callback = scpi_cmd_biss_status_gate},
    {.pattern = "TRIGger:BISS:STATus:GATE?", .callback = scpi_cmd_biss_status_gate_q},
    {.pattern = "TRIGger:BISS:CRC:OFFSet", .callback = scpi_cmd_biss_crc_offset},
    {.pattern = "TRIGger:BISS:CRC:OFFSet?", .callback = scpi_cmd_biss_crc_offset_q},
    {.pattern = "TRIGger:BISS:CRC:BITS", .callback = scpi_cmd_biss_crc_bits},
    {.pattern = "TRIGger:BISS:CRC:BITS?", .callback = scpi_cmd_biss_crc_bits_q},
    {.pattern = "TRIGger:BISS:CRC:COVer:OFFSet", .callback = scpi_cmd_biss_crc_cover_offset},
    {.pattern = "TRIGger:BISS:CRC:COVer:OFFSet?", .callback = scpi_cmd_biss_crc_cover_offset_q},
    {.pattern = "TRIGger:BISS:CRC:COVer:BITS", .callback = scpi_cmd_biss_crc_cover_bits},
    {.pattern = "TRIGger:BISS:CRC:COVer:BITS?", .callback = scpi_cmd_biss_crc_cover_bits_q},
    {.pattern = "TRIGger:BISS:CRC:POLYnomial", .callback = scpi_cmd_biss_crc_polynomial},
    {.pattern = "TRIGger:BISS:CRC:POLYnomial?", .callback = scpi_cmd_biss_crc_polynomial_q},
    {.pattern = "TRIGger:BISS:CRC:INIT", .callback = scpi_cmd_biss_crc_init},
    {.pattern = "TRIGger:BISS:CRC:INIT?", .callback = scpi_cmd_biss_crc_init_q},
    {.pattern = "TRIGger:BISS:CRC:XOR", .callback = scpi_cmd_biss_crc_xor},
    {.pattern = "TRIGger:BISS:CRC:XOR?", .callback = scpi_cmd_biss_crc_xor_q},
    {.pattern = "TRIGger:BISS:CRC:INVert", .callback = scpi_cmd_biss_crc_invert},
    {.pattern = "TRIGger:BISS:CRC:INVert?", .callback = scpi_cmd_biss_crc_invert_q},
    {.pattern = "TRIGger:BISS:CRC:GATE", .callback = scpi_cmd_biss_crc_gate},
    {.pattern = "TRIGger:BISS:CRC:GATE?", .callback = scpi_cmd_biss_crc_gate_q},
    {.pattern = "TRIGger:BISS:LATency:OFFSet", .callback = scpi_cmd_biss_latency_offset},
    {.pattern = "TRIGger:BISS:LATency:OFFSet?", .callback = scpi_cmd_biss_latency_offset_q},
    {.pattern = "TRIGger:BISS:TARGet", .callback = scpi_cmd_biss_target},
    {.pattern = "TRIGger:BISS:TARGet?", .callback = scpi_cmd_biss_target_q},
    {.pattern = "TRIGger:BISS:PINs?", .callback = scpi_cmd_biss_pins_q},
    {.pattern = "TRIGger:BISS:PULSe", .callback = scpi_cmd_biss_pulse_in},
    {.pattern = "TRIGger:BISS:FRAMe", .callback = scpi_cmd_biss_frame_rx},
    {.pattern = "TRIGger:BISS:CRC:ERRor", .callback = scpi_cmd_biss_crc_error},
    {.pattern = "TRIGger:BISS:TIMEout:INJect", .callback = scpi_cmd_biss_timeout_inject},
    {.pattern = "STATus:BISS?", .callback = scpi_cmd_biss_status_q},
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
