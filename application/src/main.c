#include "app.h"
#include "board.h"
#include "diagnostics.h"
#include "osal.h"
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

#if PROJECT_USE_FREERTOS
static void task_system(void *context)
{
    (void)context;

    if (!app_bringup()) {
        app_blink_fault_forever();
    }

    while (true) {
        board_service();
        app_diag_service();
        osal_task_delay_ms(1u);
    }
}

static void task_io_frontend(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        app_comm_service();
        osal_task_delay_ms(1u);
    }
}

static void task_trigger(void *context)
{
    (void)context;

    while (true) {
        if (!app_is_ready()) {
            osal_task_delay_ms(1u);
            continue;
        }

        app_trigger_service();
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
    const osal_task_config_t io_frontend_task_config = {
        .name = "io_frontend",
        .entry = task_io_frontend,
        .context = NULL,
        .stack_words = 1024u,
        .priority = 3u,
    };
    const osal_task_config_t trigger_task_config = {
        .name = "trigger",
        .entry = task_trigger,
        .context = NULL,
        .stack_words = 1024u,
        .priority = 5u,
    };
    const osal_task_config_t ota_task_config = {
        .name = "ota",
        .entry = task_ota,
        .context = NULL,
        .stack_words = 1536u,
        .priority = 3u,
    };
    const osal_task_config_t ui_task_config = {
        .name = "ui",
        .entry = task_ui,
        .context = NULL,
        .stack_words = 1536u,
        .priority = 2u,
    };

    if (!osal_task_create(&system_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "system task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&io_frontend_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "io_frontend task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&trigger_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "trigger task creation failed");
        app_blink_fault_forever();
    }
    if (!osal_task_create(&ota_task_config, NULL)) {
        diagnostics_mark_fault("rtos", "ota task creation failed");
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

    while (true) {
        board_service();
        app_run_once();
    }
#endif
}
