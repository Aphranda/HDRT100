#include "drv_watchdog.h"

#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/platform.h"

#define DRV_WATCHDOG_EVIDENCE_MAGIC 0x57445445u

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

void drv_watchdog_get_reset_snapshot(drv_watchdog_reset_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    snapshot->watchdog_caused_reboot = watchdog_caused_reboot();
    snapshot->watchdog_enable_caused_reboot = watchdog_enable_caused_reboot();
    snapshot->reason = watchdog_hw->reason;
    for (uint32_t i = 0u; i < 4u; i++) {
        snapshot->scratch[i] = watchdog_hw->scratch[i];
    }
}

void drv_watchdog_write_evidence(uint32_t magic,
                                 uint32_t expected_mask,
                                 uint32_t seen_mask,
                                 uint32_t stale_mask,
                                 uint32_t core0_loop_count,
                                 uint32_t core1_loop_count)
{
    /* Scratch 4 is reserved by the Pico SDK for reboot classification. */
    watchdog_hw->scratch[0] = magic != 0u ? magic : DRV_WATCHDOG_EVIDENCE_MAGIC;
    watchdog_hw->scratch[1] = expected_mask;
    watchdog_hw->scratch[2] = (seen_mask & 0xFFFFu) | ((stale_mask & 0xFFFFu) << 16u);
    watchdog_hw->scratch[3] = (core0_loop_count & 0xFFFFu) |
                              ((core1_loop_count & 0xFFFFu) << 16u);
}
