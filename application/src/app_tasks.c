#include "app_tasks.h"

#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "app_runtime.h"
#include "board.h"
#include "calibration_manager.h"
#include "diagnostics.h"
#include "drv_watchdog.h"
#include "loop_engine.h"
#include "led_manager.h"
#include "osal.h"
#include "project_config.h"
#include "ui_manager.h"

#define APP_PROGRESS(task, phase) (((uint32_t)(task) << 8u) | (uint32_t)(phase))

static void task_system(void *context)
{
    (void)context;

    if (!app_runtime_bringup()) {
        app_runtime_fault_forever();
    }

    app_runtime_start_realtime_core();
    diagnostics_watchdog_configure((1u << DIAGNOSTICS_WATCHDOG_TASK_COUNT) - 1u);
    diagnostics_watchdog_enable(PROJECT_WATCHDOG_TIMEOUT_MS);

    while (true) {
        drv_watchdog_mark_progress(0u, APP_PROGRESS(1u, 1u));
        diagnostics_record_core0_loop();
        board_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(1u, 2u));
        led_manager_service(app_is_ready());
        drv_watchdog_mark_progress(0u, APP_PROGRESS(1u, 3u));
        app_diag_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(1u, 4u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_SYSTEM);
        osal_task_delay_ms(1u);
    }
}

/*
 * WatchdogSupervisorAO is deliberately separate from the system/diagnostics
 * maintenance task.  It only evaluates the health vector and the controlled
 * core1 lockout lease; it must not perform Flash IO, SCPI work, logging, or
 * other potentially blocking operations.  The hardware watchdog remains the
 * final independent reset mechanism.
 */
static void task_watchdog_supervisor(void *context)
{
    (void)context;

    while (true) {
        drv_watchdog_mark_progress(0u, APP_PROGRESS(11u, 1u));
        diagnostics_watchdog_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(11u, 2u));
        osal_task_delay_ms(1u);
    }
}

static void task_usb_device(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_control_plane_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(2u, 1u));
        app_usb_device_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(2u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_USB_DEVICE);
        osal_task_delay_ms(1u);
    }
}

static void task_scpi(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_control_plane_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(3u, 1u));
        app_scpi_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(3u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_SCPI);
        osal_task_delay_ms(1u);
    }
}

static void task_refmem_sync(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(4u, 1u));
        app_refmem_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(4u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_REFMEM_SYNC);
        osal_task_delay_ms(1u);
    }
}

static void task_loop_engine(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(5u, 1u));
        loop_engine_set_ready(true);
        loop_engine_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(5u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_LOOP_ENGINE);
        osal_task_delay_ms(1u);
    }
}

static void task_calibration(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(6u, 1u));
        calibration_manager_set_ready(true);
        calibration_manager_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(6u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CALIBRATION);
        osal_task_delay_ms(1u);
    }
}

static void task_config_gate(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(7u, 1u));
        app_config_gate_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(7u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_CONFIG_GATE);
        osal_task_delay_ms(1u);
    }
}

static void task_ota(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(8u, 1u));
        app_ota_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(8u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_OTA);
        osal_task_delay_ms(1u);
    }
}

static void task_storage(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(9u, 1u));
        app_storage_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(9u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_STORAGE);
        osal_task_delay_ms(1u);
    }
}

static void task_ui(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        drv_watchdog_mark_progress(0u, APP_PROGRESS(10u, 1u));
        ui_manager_service();
        drv_watchdog_mark_progress(0u, APP_PROGRESS(10u, 2u));
        diagnostics_watchdog_task_heartbeat(DIAGNOSTICS_WATCHDOG_TASK_UI);
        osal_task_delay_ms(1u);
    }
}

static bool app_tasks_create_one(const osal_task_config_t *config)
{
    if (osal_task_create(config, NULL)) {
        return true;
    }

    diagnostics_mark_fault("rtos", "task creation failed");
    return false;
}

bool app_tasks_create_all(void)
{
    static const osal_task_config_t task_table[] = {
        {.name = "watchdog_supervisor", .entry = task_watchdog_supervisor, .context = NULL, .stack_words = 1024u, .priority = 6u},
        {.name = "system", .entry = task_system, .context = NULL, .stack_words = 2048u, .priority = 4u},
        {.name = "usb_device", .entry = task_usb_device, .context = NULL, .stack_words = 1536u, .priority = 4u},
        {.name = "scpi", .entry = task_scpi, .context = NULL, .stack_words = 3072u, .priority = 3u},
        {.name = "refmem_sync", .entry = task_refmem_sync, .context = NULL, .stack_words = 2048u, .priority = 4u},
        {.name = "loop_engine", .entry = task_loop_engine, .context = NULL, .stack_words = 3072u, .priority = 3u},
        {.name = "calibration", .entry = task_calibration, .context = NULL, .stack_words = 2048u, .priority = 3u},
        {.name = "cfg_gate", .entry = task_config_gate, .context = NULL, .stack_words = 2048u, .priority = 3u},
        {.name = "ota", .entry = task_ota, .context = NULL, .stack_words = 1536u, .priority = 3u},
        {.name = "storage", .entry = task_storage, .context = NULL, .stack_words = 3072u, .priority = 3u},
        {.name = "ui", .entry = task_ui, .context = NULL, .stack_words = 2048u, .priority = 1u},
    };

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(task_table) / sizeof(task_table[0])); i++) {
        if (!app_tasks_create_one(&task_table[i])) {
            return false;
        }
    }

    return true;
}
