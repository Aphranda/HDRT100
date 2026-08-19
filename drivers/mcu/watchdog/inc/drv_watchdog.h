#ifndef DRV_WATCHDOG_H
#define DRV_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool watchdog_caused_reboot;
    bool watchdog_enable_caused_reboot;
    uint32_t reason;
    uint32_t scratch[4];
} drv_watchdog_reset_snapshot_t;

void drv_watchdog_enable(uint32_t timeout_ms);
void drv_watchdog_feed(void);
void drv_watchdog_reboot(uint32_t delay_ms);
void drv_watchdog_get_reset_snapshot(drv_watchdog_reset_snapshot_t *snapshot);
void drv_watchdog_write_evidence(uint32_t magic,
                                 uint32_t expected_mask,
                                 uint32_t seen_mask,
                                 uint32_t stale_mask,
                                 uint32_t core0_loop_count,
                                 uint32_t core1_loop_count);

#endif
