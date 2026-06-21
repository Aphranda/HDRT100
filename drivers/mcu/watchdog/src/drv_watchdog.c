#include "drv_watchdog.h"

#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/platform.h"

void drv_watchdog_enable(uint32_t timeout_ms)
{
    watchdog_enable(timeout_ms, true);
}

void drv_watchdog_feed(void)
{
    watchdog_update();
}

void drv_watchdog_reboot(uint32_t delay_ms)
{
    save_and_disable_interrupts();
    watchdog_reboot(0u, 0u, delay_ms);

    while (true) {
        tight_loop_contents();
    }
}
