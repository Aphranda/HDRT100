#include "app.h"

#include "board.h"
#include "diagnostics.h"
#include "event_bus.h"
#include "ota_ao.h"
#include "osal.h"
#include "product_config.h"
#include "project_config.h"
#include "resource_arbiter.h"
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

static uint32_t s_last_tick_ms;
static uint32_t s_last_ui_refresh_ms;
static uint32_t s_last_ui_key_change_ms;
static bool s_app_ready;
static bool s_ui_dirty;
static bool s_ui_key_sample;
static bool s_ui_key_stable;

bool app_init(void)
{
    s_last_tick_ms = board_uptime_ms();
    s_last_ui_refresh_ms = s_last_tick_ms;
    s_last_ui_key_change_ms = s_last_tick_ms;
    s_app_ready = false;
    s_ui_dirty = false;
    s_ui_key_sample = false;
    s_ui_key_stable = false;
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
