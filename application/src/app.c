#include "app.h"

#include "calibration_manager.h"
#include "diagnostics.h"
#include "drv_watchdog.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "event_bus.h"
#include "loop_engine.h"
#include "model_turntable.h"
#include "ota_ao.h"
#include "product_config.h"
#include "resource_arbiter.h"
#include "scpi_port.h"
#include "storage_manager.h"
#include "system_manager.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"
#include "sync_io.h"
#include "trigger_measure.h"
#include "ui_manager.h"
#include "vdc_dpll_manager.h"
#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
#include "usbtmc_scpi_port.h"
#endif

static bool s_app_ready;
static bool s_app_control_plane_ready;

bool app_init(void)
{
    s_app_ready = false;
    s_app_control_plane_ready = false;
    diagnostics_housekeeping_init();
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
    s_app_control_plane_ready = true;
#else
    s_app_control_plane_ready = true;
#endif

    if (!resource_arbiter_init()) {
        diagnostics_mark_fault("resource_arbiter", "resource arbiter initialization failed");
        return false;
    }

    if (!event_bus_init()) {
        diagnostics_mark_fault("event_bus", "event bus initialization failed");
        return false;
    }

    if (!loop_engine_init()) {
        diagnostics_mark_fault("loop_engine", "loop engine initialization failed");
        return false;
    }

    if (!model_turntable_init()) {
        diagnostics_mark_fault("model_turntable", "model turntable initialization failed");
        return false;
    }

    if (!calibration_manager_init()) {
        diagnostics_mark_fault("calibration", "calibration manager initialization failed");
        return false;
    }

    if (!tdma_runtime_owner_init()) {
        diagnostics_mark_fault("tdma", "TDMA runtime owner initialization failed");
        return false;
    }

    if (!vdc_dpll_manager_init()) {
        diagnostics_mark_fault("vdc_dpll", "VDC/DPLL manager initialization failed");
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

    if (!system_manager_init()) {
        diagnostics_mark_fault("config", "distributed config consistency check failed");
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

    if (!ui_manager_init()) {
        diagnostics_mark_fault("ui", "sync config UI initialization failed");
        return false;
    }
    s_app_ready = true;
    loop_engine_set_ready(true);
    calibration_manager_set_ready(true);
    vdc_dpll_manager_set_vdc_ready(true);
    vdc_dpll_manager_set_dpll_ready(true);

    return true;
}

bool app_is_ready(void)
{
    return s_app_ready;
}

bool app_is_control_plane_ready(void)
{
    return s_app_control_plane_ready;
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

void app_config_gate_service(void)
{
    system_manager_service();
}

void app_ota_service(void)
{
    ota_ao_service(500u);
}

void app_diag_service(void)
{
    diagnostics_housekeeping_service();
}

void app_storage_service(void)
{
    storage_manager_service(250u);
}

void app_realtime_run_once(void)
{
    drv_watchdog_mark_progress(1u, 0x0101u);
    diagnostics_record_core1_loop();
    diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CORE1);
    vdc_dpll_manager_set_vdc_ready(true);
    drv_watchdog_mark_progress(1u, 0x0111u);
    vdc_sync_ao_service();
    drv_watchdog_mark_progress(1u, 0x0102u);
    vdc_dpll_manager_set_dpll_ready(true);
    drv_watchdog_mark_progress(1u, 0x0112u);
    sync_dpll_fb_service();
    drv_watchdog_mark_progress(1u, 0x0103u);
    tdma_component_core1_service();
    drv_watchdog_mark_progress(1u, 0x0104u);
    sync_io_capture_latch_service_core1();
    drv_watchdog_mark_progress(1u, 0x0105u);
    distributed_refmem_realtime_run_once();
    drv_watchdog_mark_progress(1u, 0x0106u);
    model_turntable_service();
    drv_watchdog_mark_progress(1u, 0x0107u);
    sync_trigger_service();
    drv_watchdog_mark_progress(1u, 0x0108u);
    trigger_measure_service();   /* 同步自检: 门控测量非阻塞服务 */
    drv_watchdog_mark_progress(1u, 0x0109u);
}
