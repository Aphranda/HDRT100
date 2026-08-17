#include "app.h"
#include "app_runtime.h"
#include "app_tasks.h"
#include "board.h"
#include "diagnostics.h"
#include "drv_flash.h"
#include "osal.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"

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

    /* HAOFV realtime core discipline: core1 runs on a fixed 1 ms (TDMA cycle)
     * tick. Every round executes at a deterministic phase of the cycle so the
     * TDMA/VDC window schedule (vdc_domain_plan_tdma_window) stays
     * predictable; between ticks core1 waits instead of free-running. The
     * flash lockout poll still runs on every tick (>= 1 kHz ACK response,
     * well inside the core0 wait budget). */
    const uint64_t tick_us = 1000ull;
    uint64_t next_tick_us = to_us_since_boot(get_absolute_time()) + tick_us;
    while (true) {
        sleep_until(from_us_since_boot(next_tick_us));
        next_tick_us += tick_us;
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

    drv_flash_lockout_status_t lockout_status;
    drv_flash_get_lockout_status(&lockout_status);
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
