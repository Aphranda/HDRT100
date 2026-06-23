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
#include "sync_io.h"

#define SCPI_PORT_INPUT_BUFFER_LENGTH 768u
#define SCPI_PORT_ERROR_QUEUE_SIZE    16
#define SCPI_PORT_IDN_VENDOR          "RP2350_TRIG"
#define SCPI_PORT_IDN_MODEL           "SYNC_TRIGGER"
#define SCPI_PORT_IDN_SERIAL          NULL
#define SCPI_PORT_POLL_CHARS          32u

static scpi_t s_scpi_context;
static char s_scpi_input_buffer[SCPI_PORT_INPUT_BUFFER_LENGTH];
static scpi_error_t s_scpi_error_queue[SCPI_PORT_ERROR_QUEUE_SIZE];

static scpi_port_config_t s_scpi_config = {
    .trigger_width_us = 10u,
    .pulse_width_us = 10u,
    .marker_width_us = 10u,
    .capture_sample_hz = 1000000u,
    .sync_clock_hz = 1000000u,
    .sync_clock_enabled = false,
};

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
    s_scpi_config.trigger_width_us = 10u;
    s_scpi_config.pulse_width_us = 10u;
    s_scpi_config.marker_width_us = 10u;
    s_scpi_config.capture_sample_hz = 1000000u;
    s_scpi_config.sync_clock_hz = 1000000u;
    s_scpi_config.sync_clock_enabled = false;

    sync_io_stop_clock();
    sync_io_stop_capture();
    return SCPI_RES_OK;
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

    s_scpi_config.trigger_width_us = value;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_width_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.trigger_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_fire(scpi_t *context)
{
    (void)context;
    return sync_io_fire_pulse_us(s_scpi_config.trigger_width_us) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_pulse_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.pulse_width_us = value;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pulse_width_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.pulse_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pulse_fire(scpi_t *context)
{
    (void)context;
    return sync_io_fire_pulse_out_us(s_scpi_config.pulse_width_us) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_marker_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.marker_width_us = value;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_marker_width_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.marker_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_marker_fire(scpi_t *context)
{
    (void)context;
    return sync_io_fire_marker_us(s_scpi_config.marker_width_us) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_sample_rate(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.capture_sample_hz = value;
    return sync_io_start_capture(value) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_sample_rate_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.capture_sample_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_sample_state(scpi_t *context)
{
    scpi_bool_t state;
    if (SCPI_ParamBool(context, &state, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    if (state) {
        return sync_io_start_capture(s_scpi_config.capture_sample_hz) ? SCPI_RES_OK : SCPI_RES_ERR;
    }

    sync_io_stop_capture();
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_sample_state_q(scpi_t *context)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    SCPI_ResultBool(context, status.capture_running ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_freq(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.sync_clock_hz = value;
    if (s_scpi_config.sync_clock_enabled) {
        return sync_io_start_clock(value) ? SCPI_RES_OK : SCPI_RES_ERR;
    }

    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_freq_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.sync_clock_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_state(scpi_t *context)
{
    scpi_bool_t state;
    if (SCPI_ParamBool(context, &state, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    if (state) {
        if (!sync_io_start_clock(s_scpi_config.sync_clock_hz)) {
            return SCPI_RES_ERR;
        }
        s_scpi_config.sync_clock_enabled = true;
        return SCPI_RES_OK;
    }

    sync_io_stop_clock();
    s_scpi_config.sync_clock_enabled = false;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_state_q(scpi_t *context)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    SCPI_ResultBool(context, status.sync_clock_running ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_status_q(scpi_t *context)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    SCPI_ResultBool(context, status.initialized ? TRUE : FALSE);
    SCPI_ResultBool(context, status.capture_running ? TRUE : FALSE);
    SCPI_ResultBool(context, status.sync_clock_running ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.capture_sample_hz);
    SCPI_ResultUInt32(context, status.sync_clock_hz);
    SCPI_ResultUInt32(context, status.dropped_capture_words);
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
#if PROJECT_ENABLE_OTA_FAULT_INJECTION
    {.pattern = "SYSTem:OTA:MODE", .callback = scpi_cmd_ota_mode},
    {.pattern = "SYSTem:OTA:INJect:COPY", .callback = scpi_cmd_ota_inject_copy},
    {.pattern = "SYSTem:OTA:INJect:CLEar", .callback = scpi_cmd_ota_inject_clear},
    {.pattern = "SYSTem:OTA:INJect:COPY?", .callback = scpi_cmd_ota_inject_copy_q},
    {.pattern = "SYSTem:OTA:INJect:MCORrupt", .callback = scpi_cmd_ota_inject_metadata_corrupt},
    {.pattern = "SYSTem:OTA:INJect:MREPair", .callback = scpi_cmd_ota_inject_metadata_repair},
#endif
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

    *config = s_scpi_config;
}
