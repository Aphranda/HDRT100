#include "app.h"
#include "board.h"
#include "diagnostics.h"
#include "drv_flash.h"
#include "osal.h"
#if PROJECT_USE_MULTICORE
#include "pico/multicore.h"
#endif
#include "pico/stdlib.h"

static void app_blink_fault_forever(void)
{
    while (true) {
        board_status_led_toggle();
        osal_delay_ms(100u);
    }
}

static bool app_bringup(void)
{
    stdio_init_all();
    osal_delay_ms(1500u);

    if (!board_init()) {
        app_blink_fault_forever();
    }

    if (!app_init()) {
        diagnostics_mark_fault("app", "application initialization failed");
    }

    return true;
}

#if PROJECT_USE_MULTICORE
static bool s_core1_started;

static void core1_realtime_entry(void)
{
    while (!app_is_ready()) {
        tight_loop_contents();
    }

    while (true) {
        drv_flash_core1_lockout_poll();
        app_realtime_run_once();
        tight_loop_contents();
    }
}

static void app_start_realtime_core(void)
{
    if (s_core1_started) {
        return;
    }

    multicore_launch_core1(core1_realtime_entry);
    s_core1_started = true;
}
#endif

#if PROJECT_USE_FREERTOS
static void task_system(void *context)
{
    (void)context;

    if (!app_bringup()) {
        app_blink_fault_forever();
    }

#if PROJECT_USE_MULTICORE
    app_start_realtime_core();
#endif

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

        app_loop_engine_service();
        osal_task_delay_ms(1u);
    }
}

static void task_vdc_sync(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        app_vdc_sync_service();
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

        app_calibration_service();
        osal_task_delay_ms(1u);
    }
}

static void task_dpll(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        app_dpll_service();
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

#if !PROJECT_USE_MULTICORE
static void task_trigger(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

#if !PROJECT_USE_MULTICORE
        app_trigger_service();
#endif
        osal_task_delay_ms(1u);
    }
}
#endif

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

        app_ui_service();
        osal_task_delay_ms(5u);
    }
}
#endif

int main(void)
{
#if PROJECT_USE_FREERTOS
    if (!osal_kernel_init()) {
        app_blink_fault_forever();
    }

    const osal_task_config_t system_task_config = {
        .name = "system",
        .entry = task_system,
        .context = NULL,
        .stack_words = 2048u,
        .priority = 4u,
    };
    const osal_task_config_t usb_device_task_config = {
        .name = "usb_device",
        .entry = task_usb_device,
        .context = NULL,
        .stack_words = 1536u,
        .priority = 4u,
    };
    const osal_task_config_t scpi_task_config = {
        .name = "scpi",
        .entry = task_scpi,
        .context = NULL,
        .stack_words = 3072u,
        .priority = 3u,
    };
    const osal_task_config_t refmem_task_config = {
        .name = "refmem_sync",
        .entry = task_refmem_sync,
        .context = NULL,
        .stack_words = 2048u,
        .priority = 4u,
    };
    const osal_task_config_t loop_engine_task_config = {
        .name = "loop_engine",
        .entry = task_loop_engine,
        .context = NULL,
        .stack_words = 3072u,
        .priority = 3u,
    };
    const osal_task_config_t vdc_sync_task_config = {
        .name = "vdc_sync",
        .entry = task_vdc_sync,
        .context = NULL,
        .stack_words = 2048u,
        .priority = 4u,
    };
    const osal_task_config_t calibration_task_config = {
        .name = "calibration",
        .entry = task_calibration,
        .context = NULL,
        .stack_words = 2048u,
        .priority = 3u,
    };
    const osal_task_config_t dpll_task_config = {
        .name = "dpll",
        .entry = task_dpll,
        .context = NULL,
        .stack_words = 2048u,
        .priority = 3u,
    };
    const osal_task_config_t config_gate_task_config = {
        .name = "cfg_gate",
        .entry = task_config_gate,
        .context = NULL,
        .stack_words = 2048u,
        .priority = 3u,
    };
#if !PROJECT_USE_MULTICORE
    const osal_task_config_t trigger_task_config = {
        .name = "trigger",
        .entry = task_trigger,
        .context = NULL,
        .stack_words = 1024u,
        .priority = 5u,
    };
#endif
    const osal_task_config_t ota_task_config = {
        .name = "ota",
        .entry = task_ota,
        .context = NULL,
        .stack_words = 1536u,
        .priority = 3u,
    };
    const osal_task_config_t storage_task_config = {
        .name = "storage",
        .entry = task_storage,
        .context = NULL,
        .stack_words = 3072u,
        .priority = 2u,
    };
    const osal_task_config_t ui_task_config = {
        .name = "ui",
        .entry = task_ui,
        .context = NULL,
        .stack_words = 2048u,
        .priority = 2u,
    };

    if (!osal_task_create(&system_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "system task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&usb_device_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "usb_device task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&scpi_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "scpi task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&refmem_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "refmem_sync task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&loop_engine_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "loop_engine task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&vdc_sync_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "vdc_sync task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&calibration_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "calibration task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&dpll_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "dpll task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&config_gate_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "cfg_gate task creation failed");
        app_blink_fault_forever();
    }
#if !PROJECT_USE_MULTICORE
    if (!osal_task_create(&trigger_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "trigger task creation failed");
        app_blink_fault_forever();
    }
#endif
    if (!osal_task_create(&ota_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "ota task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&storage_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "storage task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&ui_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "ui task creation failed");
        app_blink_fault_forever();
    }

    osal_kernel_start();
    app_blink_fault_forever();
#else
    (void)osal_kernel_init();
    (void)app_bringup();

#if PROJECT_USE_MULTICORE
    app_start_realtime_core();
#endif

    while (true) {
        board_service();
#if PROJECT_USE_MULTICORE
        app_management_run_once();
#else
        app_run_once();
#endif
    }
#endif
}
