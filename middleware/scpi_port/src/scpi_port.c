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
#include "scpi_communication_uart_commands.h"
#include "scpi_config_commands.h"
#include "scpi/scpi.h"
#include "scpi_loop_engine_commands.h"
#include "scpi_measure_commands.h"
#include "scpi_model_commands.h"
#include "scpi_ota_commands.h"
#include "scpi_port_internal.h"
#include "scpi_realtime_component_commands.h"
#include "scpi_sync_commands.h"
#include "scpi_storage_commands.h"
#include "scpi_system_access_commands.h"
#include "scpi_system_diagnostics_commands.h"
#include "scpi_system_runtime_commands.h"
#include "scpi_system_snapshot_commands.h"
#include "scpi_tdma_commands.h"
#include "scpi_trigger_commands.h"
#include "scpi_usb_control.h"
#include "sync_trigger.h"

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

void scpi_port_get_trigger_summary(sync_trigger_summary_t *summary)
{
    sync_trigger_get_summary(summary);
}

bool scpi_port_post_sync_trigger_event(sync_trigger_event_type_t type, uint32_t value)
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

void scpi_port_set_trigger_debug_stage(uint32_t stage)
{
    s_scpi_trigger_debug_stage = stage;
}

void scpi_port_set_trigger_debug_mode(uint32_t mode)
{
    s_scpi_trigger_debug_mode = mode;
}

void scpi_port_set_trigger_debug_posted(uint32_t posted)
{
    s_scpi_trigger_debug_posted = posted;
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
    SCPI_TDMA_COMMANDS,
    SCPI_SYSTEM_ACCESS_COMMANDS,
    SCPI_LOOP_ENGINE_COMMANDS,
#if PROJECT_ENABLE_OTA_FAULT_INJECTION
    {.pattern = "SYSTem:BOOT:RESet", .callback = scpi_cmd_boot_reset},
#endif
    SCPI_CONFIG_COMMANDS,
    SCPI_SYSTEM_DIAGNOSTICS_READ_COMMANDS,
    SCPI_CALIBRATION_COMMANDS,
    SCPI_SYNC_COMMANDS,
    SCPI_TRIGGER_COMMANDS,
    SCPI_REALTIME_COMPONENT_COMMANDS,
    SCPI_COMMUNICATION_BISS_COMMANDS,
    SCPI_COMMUNICATION_UART_COMMANDS,
    SCPI_OTA_COMMANDS,
#if PROJECT_ENABLE_USB_RUNTIME_SWITCH
    SCPI_USB_CONTROL_COMMANDS,
#endif
    SCPI_STORAGE_COMMANDS,
    SCPI_MODEL_COMMANDS,
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
