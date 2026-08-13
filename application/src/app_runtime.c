#include "app.h"
#include "app_runtime.h"
#include "app_tasks.h"
#include "board.h"
#include "diagnostics.h"
#include "drv_flash.h"
#include "osal.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#if !PROJECT_USE_FREERTOS || !PROJECT_USE_MULTICORE
#error "feature/rtos-multicore-haofv requires PROJECT_USE_FREERTOS=ON and PROJECT_USE_MULTICORE=ON"
#endif

void app_runtime_fault_forever(void)
{
    while (true) {
        board_status_led_toggle();
        osal_delay_ms(100u);
    }
}

bool app_runtime_bringup(void)
{
    stdio_init_all();
    osal_delay_ms(1500u);

    if (!board_init()) {
        app_runtime_fault_forever();
    }

    if (!app_init()) {
        diagnostics_mark_fault("app", "application initialization failed");
    }

    return true;
}

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

void app_runtime_start_realtime_core(void)
{
    if (s_core1_started) {
        return;
    }

    multicore_launch_core1(core1_realtime_entry);
    s_core1_started = true;
}

bool app_runtime_init(void)
{
    if (!osal_kernel_init()) {
        return false;
    }

    return app_tasks_create_all();
}

void app_runtime_run(void)
{
    osal_kernel_start();
    app_runtime_fault_forever();
}
