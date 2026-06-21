#include "drv_watchdog.h"

#include "hardware/watchdog.h"

void drv_watchdog_enable(uint32_t timeout_ms)
{
    watchdog_enable(timeout_ms, true);
}

void drv_watchdog_feed(void)
{
    watchdog_update();
}
