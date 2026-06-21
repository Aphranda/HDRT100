#ifndef DRV_WATCHDOG_H
#define DRV_WATCHDOG_H

#include <stdint.h>

void drv_watchdog_enable(uint32_t timeout_ms);
void drv_watchdog_feed(void);
void drv_watchdog_reboot(uint32_t delay_ms);

#endif
