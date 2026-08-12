#include "app.h"

#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "event_bus.h"
#include "ota_crc32.h"
#include "ota_ao.h"
#include "osal.h"
#include "product_config.h"
#include "project_config.h"
#include "resource_arbiter.h"
#include "sync_io_hw_profile.h"
#include "scpi_port.h"
#include "storage_manager.h"
#include "sync_config_ui.h"
#include "sync_trigger.h"
#include "sync_io.h"
#include "trigger_measure.h"
#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
#include "usbtmc_scpi_port.h"
#endif

#define APP_UI_REFRESH_PERIOD_MS 250u
#define APP_UI_KEY_DEBOUNCE_MS 35u
#define APP_LOG_SERVICE_BYTES 256u
#define APP_SYSTEM_MODE_TABLE_VERSION 1u
#define APP_RESOURCE_ARBITER_TABLE_VERSION 1u
#define APP_FAULT_CODE_TABLE_VERSION 1u

static uint32_t s_last_tick_ms;
static uint32_t s_last_ui_refresh_ms;
static uint32_t s_last_ui_key_change_ms;
static bool s_app_ready;
static bool s_ui_dirty;
static bool s_ui_key_sample;
static bool s_ui_key_stable;
static app_loop_engine_status_t s_loop_engine_status;
static app_vdc_sync_status_t s_vdc_sync_status;
static app_dpll_status_t s_dpll_status;
static app_calibration_status_t s_calibration_status;
static app_config_gate_status_t s_config_gate_status;

typedef struct {
    uint32_t main_input_base_pin;
    uint32_t main_input_pin_count;
    uint32_t main_output_base_pin;
    uint32_t main_output_pin_count;
    uint32_t trig_in_pin;
    uint32_t gate_in_pin;
    uint32_t trig_out_pin;
    uint32_t rj45_trig_in_pin;
    uint32_t rj45_trig_out_pin;
    uint32_t arm_in_pin;
    uint32_t ext_clk_in_pin;
    uint32_t sync_clk_out_pin;
    uint32_t aux0_pin;
    uint32_t aux1_pin;
    uint32_t aux2_pin;
    uint32_t aux3_pin;
} app_hw_profile_blob_t;

static const app_system_mode_entry_t s_system_mode_template[4] = {
    {
        .mode_id = RESOURCE_ARBITER_MODE_BOOT,
        .run_allowed = 0u,
        .ota_allowed = 0u,
        .fault_allowed = 1u,
        .name = "BOOT",
    },
    {
        .mode_id = RESOURCE_ARBITER_MODE_RUN,
        .run_allowed = 1u,
        .ota_allowed = 0u,
        .fault_allowed = 1u,
        .name = "RUN",
    },
    {
        .mode_id = RESOURCE_ARBITER_MODE_OTA,
        .run_allowed = 0u,
        .ota_allowed = 1u,
        .fault_allowed = 1u,
        .name = "OTA",
    },
    {
        .mode_id = RESOURCE_ARBITER_MODE_FAULT,
        .run_allowed = 0u,
        .ota_allowed = 0u,
        .fault_allowed = 1u,
        .name = "FAULT",
    },
};

static const app_resource_arbiter_entry_t s_resource_arbiter_template[10] = {
    { .resource_id = 0u, .mask = RESOURCE_ARBITER_RESOURCE_FLASH, .owner_mode = RESOURCE_ARBITER_MODE_OTA, .active = 0u, .name = "FLASH", .owner_name = "-" },
    { .resource_id = 1u, .mask = RESOURCE_ARBITER_RESOURCE_SPI0, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "SPI0", .owner_name = "-" },
    { .resource_id = 2u, .mask = RESOURCE_ARBITER_RESOURCE_USB, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "USB", .owner_name = "-" },
    { .resource_id = 3u, .mask = RESOURCE_ARBITER_RESOURCE_PIO0, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "PIO0", .owner_name = "-" },
    { .resource_id = 4u, .mask = RESOURCE_ARBITER_RESOURCE_PIO1, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "PIO1", .owner_name = "-" },
    { .resource_id = 5u, .mask = RESOURCE_ARBITER_RESOURCE_PIO2, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "PIO2", .owner_name = "-" },
    { .resource_id = 6u, .mask = RESOURCE_ARBITER_RESOURCE_DMA, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "DMA", .owner_name = "-" },
    { .resource_id = 7u, .mask = RESOURCE_ARBITER_RESOURCE_LCD, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "LCD", .owner_name = "-" },
    { .resource_id = 8u, .mask = RESOURCE_ARBITER_RESOURCE_SD, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "SD", .owner_name = "-" },
    { .resource_id = 9u, .mask = RESOURCE_ARBITER_RESOURCE_AUX, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "AUX", .owner_name = "-" },
};

static const app_fault_code_entry_t s_fault_code_template[20] = {
    { .fault_id = 0u, .domain_id = 0u, .severity = 0u, .recoverable = 1u, .sticky = 0u, .name = "NONE" },
    { .fault_id = 1u, .domain_id = 0u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "DIAG_FAULT_LATCHED" },
    { .fault_id = 2u, .domain_id = 1u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "TRIG_INVALID_SEQ_CONFIG" },
    { .fault_id = 3u, .domain_id = 1u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "TRIG_RESOURCE_CONFLICT" },
    { .fault_id = 4u, .domain_id = 1u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "TRIG_IO_ARM_FAILED" },
    { .fault_id = 5u, .domain_id = 1u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "TRIG_IO_LOST" },
    { .fault_id = 10u, .domain_id = 1u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "TRIG_INVALID_ENC_TARGET" },
    { .fault_id = 11u, .domain_id = 1u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "TRIG_INVALID_ENC_PINS" },
    { .fault_id = 20u, .domain_id = 1u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "TRIG_INVALID_BISS_CONFIG" },
    { .fault_id = 100u, .domain_id = 1u, .severity = 3u, .recoverable = 0u, .sticky = 1u, .name = "TRIG_FORCED_FAULT" },
    { .fault_id = 200u, .domain_id = 2u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "OTA_BUSY" },
    { .fault_id = 201u, .domain_id = 2u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "OTA_INVALID_STATE" },
    { .fault_id = 202u, .domain_id = 2u, .severity = 2u, .recoverable = 0u, .sticky = 1u, .name = "OTA_IMAGE_TOO_LARGE" },
    { .fault_id = 203u, .domain_id = 2u, .severity = 2u, .recoverable = 0u, .sticky = 1u, .name = "OTA_BAD_HEADER" },
    { .fault_id = 204u, .domain_id = 2u, .severity = 3u, .recoverable = 0u, .sticky = 1u, .name = "OTA_FLASH_ERASE" },
    { .fault_id = 205u, .domain_id = 2u, .severity = 3u, .recoverable = 0u, .sticky = 1u, .name = "OTA_FLASH_PROGRAM" },
    { .fault_id = 300u, .domain_id = 3u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "STORAGE_PATH_DENIED" },
    { .fault_id = 301u, .domain_id = 3u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "STORAGE_MANIFEST_INVALID" },
    { .fault_id = 304u, .domain_id = 3u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "STORAGE_RESOURCE_BUSY" },
    { .fault_id = 305u, .domain_id = 3u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "STORAGE_PATH_ERROR" },
};

static uint32_t app_crc32_text(const char *text)
{
    return ota_crc32_compute((const uint8_t *)text, (uint32_t)strlen(text));
}

static uint32_t app_crc32_blob(const void *data, size_t size)
{
    return ota_crc32_compute((const uint8_t *)data, (uint32_t)size);
}

static uint32_t app_crc32_table_entry(const uint32_t *fields,
                                      size_t field_count,
                                      const char *name,
                                      const char *owner)
{
    uint8_t buffer[160];
    size_t used = 0u;

    if (fields != NULL && field_count > 0u) {
        const size_t field_bytes = field_count * sizeof(uint32_t);
        if (field_bytes > sizeof(buffer)) {
            return 0u;
        }
        memcpy(buffer, fields, field_bytes);
        used = field_bytes;
    }

    if (name != NULL) {
        const size_t len = strlen(name);
        if (used + len > sizeof(buffer)) {
            return 0u;
        }
        memcpy(&buffer[used], name, len);
        used += len;
    }

    if (owner != NULL) {
        if (used + 1u > sizeof(buffer)) {
            return 0u;
        }
        buffer[used++] = '|';
        const size_t len = strlen(owner);
        if (used + len > sizeof(buffer)) {
            return 0u;
        }
        memcpy(&buffer[used], owner, len);
        used += len;
    }

    return app_crc32_blob(buffer, used);
}

static uint32_t app_system_mode_table_crc32(void)
{
    uint32_t crc = APP_SYSTEM_MODE_TABLE_VERSION;
    for (uint32_t i = 0u; i < 4u; i++) {
        const app_system_mode_entry_t *entry = &s_system_mode_template[i];
        const uint32_t fields[] = {
            entry->mode_id,
            entry->run_allowed,
            entry->ota_allowed,
            entry->fault_allowed,
        };
        crc ^= app_crc32_table_entry(fields,
                                     sizeof(fields) / sizeof(fields[0]),
                                     entry->name,
                                     NULL);
    }
    return crc;
}

static uint32_t app_resource_arbiter_table_crc32(void)
{
    uint32_t crc = APP_RESOURCE_ARBITER_TABLE_VERSION;
    for (uint32_t i = 0u; i < 10u; i++) {
        const app_resource_arbiter_entry_t *entry =
            &s_resource_arbiter_template[i];
        const uint32_t fields[] = {
            entry->resource_id,
            entry->mask,
            entry->owner_mode,
        };
        crc ^= app_crc32_table_entry(fields,
                                     sizeof(fields) / sizeof(fields[0]),
                                     entry->name,
                                     NULL);
    }
    return crc;
}

static uint32_t app_fault_code_table_crc32(void)
{
    uint32_t crc = APP_FAULT_CODE_TABLE_VERSION;
    for (uint32_t i = 0u; i < 20u; i++) {
        const app_fault_code_entry_t *entry = &s_fault_code_template[i];
        const uint32_t fields[] = {
            entry->fault_id,
            entry->domain_id,
            entry->severity,
            entry->recoverable,
            entry->sticky,
        };
        crc ^= app_crc32_table_entry(fields,
                                     sizeof(fields) / sizeof(fields[0]),
                                     entry->name,
                                     NULL);
    }
    return crc;
}

bool app_init(void)
{
    s_last_tick_ms = board_uptime_ms();
    s_last_ui_refresh_ms = s_last_tick_ms;
    s_last_ui_key_change_ms = s_last_tick_ms;
    s_app_ready = false;
    s_ui_dirty = false;
    s_ui_key_sample = false;
    s_ui_key_stable = false;
    s_loop_engine_status.ready = false;
    s_loop_engine_status.service_count = 0u;
    s_loop_engine_status.first_service_ms = 0u;
    s_loop_engine_status.last_service_ms = s_last_tick_ms;
    s_vdc_sync_status.ready = false;
    s_vdc_sync_status.lock_state = 0u;
    s_vdc_sync_status.service_count = 0u;
    s_vdc_sync_status.first_service_ms = 0u;
    s_vdc_sync_status.last_service_ms = s_last_tick_ms;
    s_vdc_sync_status.sync_seq = 0u;
    s_dpll_status.ready = false;
    s_dpll_status.state = 0u;
    s_dpll_status.service_count = 0u;
    s_dpll_status.first_service_ms = 0u;
    s_dpll_status.last_service_ms = s_last_tick_ms;
    s_dpll_status.update_seq = 0u;
    s_calibration_status.ready = false;
    s_calibration_status.state = 0u;
    s_calibration_status.service_count = 0u;
    s_calibration_status.first_service_ms = 0u;
    s_calibration_status.last_service_ms = s_last_tick_ms;
    s_calibration_status.command_seq = 1u;
    s_calibration_status.link_count = 1u;
    s_calibration_status.delay_count = 1u;
    s_calibration_status.active_crc32 = 0x10000003u;
    s_calibration_status.last_error = 0u;
    s_config_gate_status.ready = false;
    s_config_gate_status.gate_state = 0u;
    s_config_gate_status.service_count = 0u;
    s_config_gate_status.first_service_ms = 0u;
    s_config_gate_status.last_service_ms = s_last_tick_ms;
    s_config_gate_status.epoch = s_last_tick_ms;
    s_config_gate_status.run_id = 0u;
    s_config_gate_status.config_version = 1u;
    s_config_gate_status.calibration_version = 1u;
    s_config_gate_status.loop_plan_version = 1u;
    s_config_gate_status.action_map_version = 1u;
    s_config_gate_status.command_seq = 1u;
    s_config_gate_status.target_mask = 0x0Fu;
    s_config_gate_status.ack_flags = 0u;
    s_config_gate_status.nack_flags = 0u;
    s_config_gate_status.busy_flags = 0u;
    s_config_gate_status.timeout_flags = 0u;
    s_config_gate_status.build_crc32 = 0u;
    s_config_gate_status.hw_profile_crc32 = 0u;
    s_config_gate_status.role_map_crc32 = 0u;
    s_config_gate_status.loop_plan_crc32 = 0u;
    s_config_gate_status.action_map_crc32 = 0u;
    s_config_gate_status.calibration_crc32 = 0u;
    s_config_gate_status.config_crc32 = 0u;
    LOG_INFO("app", "application initialized");

    const sync_io_config_t sync_io_config = {
        .capture_sample_hz = 1000000u,
        .sync_clock_hz = 1000000u,
    };

    if (!sync_io_init(&sync_io_config)) {
        diagnostics_mark_fault("sync_io", "sync IO initialization failed");
        return false;
    }

    if (!scpi_port_init()) {
        diagnostics_mark_fault("scpi", "SCPI initialization failed");
        return false;
    }

#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
    if (!product_config_init()) {
        diagnostics_mark_fault("product_config", "product config initialization failed");
        return false;
    }

    if (!usbtmc_scpi_port_init()) {
        diagnostics_mark_fault("usb", "USB SCPI initialization failed");
        return false;
    }
#endif

    if (!resource_arbiter_init()) {
        diagnostics_mark_fault("resource_arbiter", "resource arbiter initialization failed");
        return false;
    }

    if (!event_bus_init()) {
        diagnostics_mark_fault("event_bus", "event bus initialization failed");
        return false;
    }

    if (!distributed_refmem_init()) {
        diagnostics_mark_fault("refmem", "distributed refmem initialization failed");
        return false;
    }

    if (!distributed_config_init()) {
        diagnostics_mark_fault("config", "distributed config initialization failed");
        return false;
    }

    if (!ota_ao_init()) {
        diagnostics_mark_fault("ota", "OTA initialization failed");
        return false;
    }

    if (!storage_manager_init()) {
        diagnostics_mark_fault("storage", "storage manager initialization failed");
        return false;
    }

    if (!sync_trigger_init()) {
        diagnostics_mark_fault("trigger", "sync trigger initialization failed");
        return false;
    }

    if (!sync_config_ui_init()) {
        diagnostics_mark_fault("ui", "sync config UI initialization failed");
        return false;
    }
    s_ui_dirty = true;
    s_app_ready = true;
    s_loop_engine_status.ready = true;
    s_vdc_sync_status.ready = true;
    s_dpll_status.ready = true;
    s_calibration_status.ready = true;
    s_config_gate_status.build_crc32 = app_crc32_text(g_project_build_id);
    const app_hw_profile_blob_t hw_profile = {
        .main_input_base_pin = SYNC_IO_HW_MAIN_INPUT_BASE_PIN,
        .main_input_pin_count = SYNC_IO_HW_MAIN_INPUT_PIN_COUNT,
        .main_output_base_pin = SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN,
        .main_output_pin_count = SYNC_IO_HW_MAIN_OUTPUT_PIN_COUNT,
        .trig_in_pin = SYNC_IO_HW_TRIG_IN_PIN,
        .gate_in_pin = SYNC_IO_HW_GATE_IN_PIN,
        .trig_out_pin = SYNC_IO_HW_TRIG_OUT_PIN,
        .rj45_trig_in_pin = SYNC_IO_HW_RJ45_TRIG_IN_PIN,
        .rj45_trig_out_pin = SYNC_IO_HW_RJ45_TRIG_OUT_PIN,
        .arm_in_pin = SYNC_IO_HW_ARM_IN_PIN,
        .ext_clk_in_pin = SYNC_IO_HW_EXT_CLK_IN_PIN,
        .sync_clk_out_pin = SYNC_IO_HW_SYNC_CLK_OUT_PIN,
        .aux0_pin = SYNC_IO_HW_AUX0_PIN,
        .aux1_pin = SYNC_IO_HW_AUX1_PIN,
        .aux2_pin = SYNC_IO_HW_AUX2_PIN,
        .aux3_pin = SYNC_IO_HW_AUX3_PIN,
    };
    s_config_gate_status.hw_profile_crc32 = app_crc32_blob(&hw_profile, sizeof(hw_profile));
    const distributed_config_snapshot_t *config_snapshot = distributed_config_get_snapshot();
    s_config_gate_status.config_version = config_snapshot->config_version;
    s_config_gate_status.calibration_version = config_snapshot->calibration_version;
    s_config_gate_status.loop_plan_version = config_snapshot->loop_plan_version;
    s_config_gate_status.action_map_version = config_snapshot->action_map_version;
    s_config_gate_status.target_mask = config_snapshot->target_mask;
    s_config_gate_status.role_map_crc32 = config_snapshot->role_map_crc32;
    s_config_gate_status.loop_plan_crc32 = config_snapshot->loop_plan_crc32;
    s_config_gate_status.action_map_crc32 = config_snapshot->action_map_crc32;
    s_config_gate_status.calibration_crc32 = config_snapshot->calibration_crc32;
    s_config_gate_status.config_crc32 = config_snapshot->config_crc32;
    const bool config_valid = distributed_config_validate();
    s_config_gate_status.ack_flags = config_valid ? s_config_gate_status.target_mask : 0u;
    s_config_gate_status.nack_flags = config_valid ? 0u : s_config_gate_status.target_mask;
    s_config_gate_status.run_id = s_config_gate_status.build_crc32 ^
                                  s_config_gate_status.hw_profile_crc32 ^
                                  s_config_gate_status.config_crc32 ^
                                  s_config_gate_status.epoch;
    s_config_gate_status.ready = config_valid;
    s_config_gate_status.gate_state = config_valid ? 1u : 2u;
    if (!config_valid) {
        diagnostics_mark_fault("config", "distributed config consistency check failed");
    }

    return true;
}

bool app_is_ready(void)
{
    return s_app_ready;
}

void app_comm_service(void)
{
    app_usb_device_service();
    app_scpi_service();
}

void app_usb_device_service(void)
{
#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
    usbtmc_scpi_port_service();
#endif
}

void app_scpi_service(void)
{
#if !PROJECT_ENABLE_USB_RUNTIME_SWITCH
    scpi_port_service();
#endif
}

void app_refmem_service(void)
{
    distributed_refmem_service();
}

void app_loop_engine_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_loop_engine_status.service_count == 0u) {
        s_loop_engine_status.first_service_ms = now_ms;
    }
    s_loop_engine_status.service_count++;
    s_loop_engine_status.last_service_ms = now_ms;
    s_loop_engine_status.ready = s_app_ready;
    osal_critical_exit();
}

void app_loop_engine_get_status(app_loop_engine_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_loop_engine_status;
    status->ready = s_app_ready;
    osal_critical_exit();
}

void app_vdc_sync_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_vdc_sync_status.service_count == 0u) {
        s_vdc_sync_status.first_service_ms = now_ms;
    }
    s_vdc_sync_status.service_count++;
    s_vdc_sync_status.last_service_ms = now_ms;
    s_vdc_sync_status.ready = s_app_ready;
    s_vdc_sync_status.lock_state = 0u;
    s_vdc_sync_status.sync_seq++;
    osal_critical_exit();
}

void app_vdc_sync_get_status(app_vdc_sync_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_vdc_sync_status;
    status->ready = s_app_ready;
    osal_critical_exit();
}

void app_dpll_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_dpll_status.service_count == 0u) {
        s_dpll_status.first_service_ms = now_ms;
    }
    s_dpll_status.service_count++;
    s_dpll_status.last_service_ms = now_ms;
    s_dpll_status.ready = s_app_ready;
    s_dpll_status.state = 0u;
    s_dpll_status.update_seq++;
    osal_critical_exit();
}

void app_dpll_get_status(app_dpll_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_dpll_status;
    status->ready = s_app_ready;
    osal_critical_exit();
}

void app_calibration_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_calibration_status.service_count == 0u) {
        s_calibration_status.first_service_ms = now_ms;
    }
    s_calibration_status.service_count++;
    s_calibration_status.last_service_ms = now_ms;
    s_calibration_status.ready = s_app_ready;
    s_calibration_status.state = 0u;
    osal_critical_exit();
}

void app_calibration_get_status(app_calibration_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_calibration_status;
    status->ready = s_app_ready;
    osal_critical_exit();
}

void app_config_gate_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_config_gate_status.service_count == 0u) {
        s_config_gate_status.first_service_ms = now_ms;
    }
    s_config_gate_status.service_count++;
    s_config_gate_status.last_service_ms = now_ms;
    const bool gate_ready = s_app_ready &&
                            s_config_gate_status.nack_flags == 0u &&
                            s_config_gate_status.config_crc32 != 0u;
    s_config_gate_status.ready = gate_ready;
    s_config_gate_status.gate_state = gate_ready ? 1u : 2u;
    osal_critical_exit();
}

void app_config_gate_get_status(app_config_gate_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_config_gate_status;
    osal_critical_exit();
}

void app_config_gate_get_ack_status(app_config_ack_status_t *status)
{
    if (status == NULL) {
        return;
    }

    const distributed_config_snapshot_t *config_snapshot = distributed_config_get_snapshot();
    const distributed_config_nack_reason_table_t *reason_table =
        distributed_config_get_nack_reason_table();

    osal_critical_enter();
    status->version = config_snapshot->config_version;
    status->command_seq = s_config_gate_status.command_seq;
    status->target_mask = s_config_gate_status.target_mask;
    status->ack_flags = s_config_gate_status.ack_flags;
    status->nack_flags = s_config_gate_status.nack_flags;
    status->busy_flags = s_config_gate_status.busy_flags;
    status->timeout_flags = s_config_gate_status.timeout_flags;
    status->last_nack_reason = s_config_gate_status.nack_flags == 0u ?
                               (uint32_t)DISTRIBUTED_CONFIG_NACK_NONE :
                               (uint32_t)DISTRIBUTED_CONFIG_NACK_CONFIG_CRC_MISMATCH;
    status->last_nack_node = s_config_gate_status.nack_flags == 0u ?
                             UINT32_MAX :
                             DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    status->reason_count = reason_table->reason_count;
    status->reason_table_crc32 = config_snapshot->nack_reason_crc32;
    status->config_crc32 = s_config_gate_status.config_crc32;
    osal_critical_exit();
}

void app_system_mode_get_table(app_system_mode_table_t *table)
{
    if (table == NULL) {
        return;
    }

    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);

    table->version = APP_SYSTEM_MODE_TABLE_VERSION;
    table->mode_count = 4u;
    table->current_mode = (uint32_t)snapshot.mode;
    table->table_crc32 = app_system_mode_table_crc32();
    memcpy(table->mode, s_system_mode_template, sizeof(s_system_mode_template));
}

void app_resource_arbiter_get_table(app_resource_arbiter_table_t *table)
{
    if (table == NULL) {
        return;
    }

    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);

    table->version = APP_RESOURCE_ARBITER_TABLE_VERSION;
    table->resource_count = 10u;
    table->current_mode = (uint32_t)snapshot.mode;
    table->active_resources = snapshot.active_resources;
    table->last_conflict_resources = snapshot.last_conflict_resources;
    table->table_crc32 = app_resource_arbiter_table_crc32();
    memcpy(table->resource, s_resource_arbiter_template, sizeof(s_resource_arbiter_template));

    for (uint32_t i = 0u; i < table->resource_count; i++) {
        app_resource_arbiter_entry_t *entry = &table->resource[i];
        entry->active = (snapshot.active_resources & entry->mask) != 0u ? 1u : 0u;
        entry->owner_name = snapshot.resource_owners[entry->resource_id] != NULL ?
                            snapshot.resource_owners[entry->resource_id] :
                            "-";
    }
}

void app_fault_code_get_table(app_fault_code_table_t *table)
{
    if (table == NULL) {
        return;
    }

    table->version = APP_FAULT_CODE_TABLE_VERSION;
    table->fault_count = 20u;
    table->latched = diagnostics_has_fault() ? 1u : 0u;
    table->table_crc32 = app_fault_code_table_crc32();
    memcpy(table->fault, s_fault_code_template, sizeof(s_fault_code_template));
}

void app_trigger_service(void)
{
    sync_trigger_service();
    trigger_measure_service();   /* 同步自检: 门控测量非阻塞服务 */
}

void app_ota_service(void)
{
    ota_ao_service(500u);
}

void app_ui_service(void)
{
    const uint32_t now_ms = board_uptime_ms();
    const bool key_sample = board_key2_is_pressed();

    if (key_sample != s_ui_key_sample) {
        s_ui_key_sample = key_sample;
        s_last_ui_key_change_ms = now_ms;
    }

    if ((uint32_t)(now_ms - s_last_ui_key_change_ms) >= APP_UI_KEY_DEBOUNCE_MS &&
        s_ui_key_stable != s_ui_key_sample) {
        s_ui_key_stable = s_ui_key_sample;
        if (s_ui_key_stable) {
            sync_config_ui_key_next();
            s_ui_dirty = true;
        }
    }

    if ((uint32_t)(now_ms - s_last_ui_refresh_ms) >= APP_UI_REFRESH_PERIOD_MS) {
        s_ui_dirty = true;
    }
    if (sync_config_ui_needs_render()) {
        s_ui_dirty = true;
    }

    if (!s_ui_dirty) {
        return;
    }

    if (sync_config_ui_render()) {
        s_ui_dirty = sync_config_ui_needs_render();
        s_last_ui_refresh_ms = now_ms;
    }
}

void app_diag_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    diagnostics_service(APP_LOG_SERVICE_BYTES);

    if ((uint32_t)(now_ms - s_last_tick_ms) >= PROJECT_LOOP_PERIOD_MS) {
        s_last_tick_ms = now_ms;
        diagnostics_heartbeat(PROJECT_HEALTH_LOG_PERIOD_MS);
    }
}

void app_storage_service(void)
{
    storage_manager_service(250u);
}

void app_run_once(void)
{
    diagnostics_record_core0_loop();
    app_comm_service();
    app_loop_engine_service();
    app_calibration_service();
    app_vdc_sync_service();
    app_dpll_service();
    app_config_gate_service();
    app_trigger_service();
    app_ota_service();
    app_storage_service();
    app_ui_service();
    app_diag_service();
    osal_delay_ms(1u);
}

void app_management_run_once(void)
{
    diagnostics_record_core0_loop();
    app_comm_service();
    app_loop_engine_service();
    app_calibration_service();
    app_vdc_sync_service();
    app_dpll_service();
    app_config_gate_service();
    app_ota_service();
    app_storage_service();
    app_ui_service();
    app_diag_service();
    osal_delay_ms(1u);
}

void app_realtime_run_once(void)
{
    diagnostics_record_core1_loop();
#if PROJECT_USE_MULTICORE
    sync_trigger_service();
#else
    app_trigger_service();
#endif
}
