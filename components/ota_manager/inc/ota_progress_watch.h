#ifndef OTA_PROGRESS_WATCH_H
#define OTA_PROGRESS_WATCH_H

#include <stdbool.h>
#include <stdint.h>

/* Host-interruption watchdog for the OTA session (OTA-HAOFV-012).
 *
 * The AO only sees limited state per service call.  A session that stops
 * making progress in a state that depends on the host (RECEIVING/ERASE_SLOT)
 * must fail closed instead of holding the maintenance admission forever: a
 * dead host otherwise leaves the board in RECEIVING and rejects the next
 * BEGIN with INVALID_STATE. */

#define OTA_PROGRESS_WATCH_DEFAULT_TIMEOUT_MS 10000u

typedef struct {
    uint32_t last_ms;
    uint32_t state;
    uint32_t received_size;
    uint32_t programmed_size;
    bool initialized;
} ota_progress_watch_t;

typedef enum {
    OTA_PROGRESS_WATCH_IDLE = 0,
    OTA_PROGRESS_WATCH_PROGRESS,
    OTA_PROGRESS_WATCH_TIMEOUT,
} ota_progress_watch_result_t;

void ota_progress_watch_reset(ota_progress_watch_t *watch, uint32_t now_ms);

/* Advance the watch with the values published in the OTA vector.
 * A change of state/received/programmed counts as progress.  The timeout is
 * wrap-safe and can be disabled with timeout_ms == 0. */
ota_progress_watch_result_t ota_progress_watch_step(
    ota_progress_watch_t *watch, uint32_t now_ms, uint32_t state,
    uint32_t received_size, uint32_t programmed_size, uint32_t timeout_ms);

#endif
