#include "app_tasks.h"

#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "app_runtime.h"
#include "board.h"
#include "calibration_manager.h"
#include "diagnostics.h"
#include "loop_engine.h"
#include "osal.h"
#include "ui_manager.h"

static void task_system(void *context)
{
    (void)context;

    if (!app_runtime_bringup()) {
        app_runtime_fault_forever();
    }

    app_runtime_start_realtime_core();

    while (true) {
        board_service();
        app_diag_service();
        osal_task_delay_ms(1u);
    }
}

static void task_usb_device(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        app_usb_device_service();
        osal_task_delay_ms(1u);
    }
}

static void task_scpi(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        app_scpi_service();
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

        app_refmem_service();
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

        loop_engine_set_ready(true);
        loop_engine_service();
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

        calibration_manager_set_ready(true);
        calibration_manager_service();
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

        app_config_gate_service();
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

        app_ota_service();
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

        app_storage_service();
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

        ui_manager_service();
        osal_task_delay_ms(5u);
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
