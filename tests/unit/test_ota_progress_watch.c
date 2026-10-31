#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ota_progress_watch.h"
#include "ota_vector.h"

int main(void)
{
    ota_progress_watch_t watch;

    /* Not initialized: the watch stays idle and cannot time out. */
    watch.initialized = false;
    assert(ota_progress_watch_step(&watch, 10u, OTA_STATE_RECEIVING, 0u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_IDLE);
    assert(ota_progress_watch_step(NULL, 10u, OTA_STATE_RECEIVING, 0u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_IDLE);

    /* Non-timed state: elapsed time alone never times out. */
    ota_progress_watch_reset(&watch, 100u);
    assert(ota_progress_watch_step(&watch, 50000u, OTA_STATE_IDLE, 0u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_IDLE);

    /* Entering RECEIVING is progress; a stall then times out. */
    assert(ota_progress_watch_step(&watch, 200u, OTA_STATE_RECEIVING, 0u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 1100u, OTA_STATE_RECEIVING, 0u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 1200u, OTA_STATE_RECEIVING, 0u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_TIMEOUT);

    /* A received_size change resets the deadline. */
    assert(ota_progress_watch_step(&watch, 1200u, OTA_STATE_RECEIVING, 64u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 2100u, OTA_STATE_RECEIVING, 64u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 2200u, OTA_STATE_RECEIVING, 64u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_TIMEOUT);

    /* ERASE_SLOT advances through programmed_size. */
    ota_progress_watch_reset(&watch, 0u);
    assert(ota_progress_watch_step(&watch, 10u, OTA_STATE_ERASE_SLOT, 0u, 0u,
                                   1000u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 20u, OTA_STATE_ERASE_SLOT, 0u, 4096u,
                                   1000u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 1020u, OTA_STATE_ERASE_SLOT, 0u,
                                   4096u, 1000u) == OTA_PROGRESS_WATCH_TIMEOUT);

    /* Timeout comparison is wrap-safe across the 32-bit uptime wrap. */
    ota_progress_watch_reset(&watch, 0xFFFFFFF0u);
    assert(ota_progress_watch_step(&watch, 0xFFFFFFF0u, OTA_STATE_RECEIVING, 0u,
                                   0u, 1000u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 0x00000010u, OTA_STATE_RECEIVING, 0u,
                                   0u, 1000u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 0x00000400u, OTA_STATE_RECEIVING, 0u,
                                   0u, 1000u) == OTA_PROGRESS_WATCH_TIMEOUT);

    /* Disabled timeout behaves like a plain idle watch. */
    ota_progress_watch_reset(&watch, 0u);
    assert(ota_progress_watch_step(&watch, 10u, OTA_STATE_RECEIVING, 0u, 0u,
                                   0u) == OTA_PROGRESS_WATCH_PROGRESS);
    assert(ota_progress_watch_step(&watch, 60000u, OTA_STATE_RECEIVING, 0u, 0u,
                                   0u) == OTA_PROGRESS_WATCH_IDLE);

    printf("OTA progress watch host unit tests passed\n");
    return 0;
}
