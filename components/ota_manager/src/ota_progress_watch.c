#include "ota_progress_watch.h"

#include <stddef.h>

#include "ota_vector.h"

static bool ota_progress_watch_is_timed_state(uint32_t state)
{
    /* Only states that wait on the host make a no-progress timeout meaningful.
     * ERASE_SLOT advances through programmed_size; VERIFYING/MARK_PENDING are
     * bounded board-side work and keep their own error paths. */
    return state == (uint32_t)OTA_STATE_RECEIVING ||
           state == (uint32_t)OTA_STATE_ERASE_SLOT;
}

void ota_progress_watch_reset(ota_progress_watch_t *watch, uint32_t now_ms)
{
    if (watch == NULL) {
        return;
    }
    watch->last_ms = now_ms;
    watch->state = (uint32_t)OTA_STATE_IDLE;
    watch->received_size = 0u;
    watch->programmed_size = 0u;
    watch->initialized = true;
}

ota_progress_watch_result_t ota_progress_watch_step(
    ota_progress_watch_t *watch, uint32_t now_ms, uint32_t state,
    uint32_t received_size, uint32_t programmed_size, uint32_t timeout_ms)
{
    if (watch == NULL || !watch->initialized) {
        return OTA_PROGRESS_WATCH_IDLE;
    }

    const bool changed = state != watch->state ||
                         received_size != watch->received_size ||
                         programmed_size != watch->programmed_size;
    if (changed) {
        watch->state = state;
        watch->received_size = received_size;
        watch->programmed_size = programmed_size;
        watch->last_ms = now_ms;
        return OTA_PROGRESS_WATCH_PROGRESS;
    }

    if (!ota_progress_watch_is_timed_state(state) || timeout_ms == 0u) {
        watch->last_ms = now_ms;
        return OTA_PROGRESS_WATCH_IDLE;
    }

    /* Unsigned subtraction keeps the comparison correct across the 32-bit
     * uptime wrap; no second timer or re-basing is involved. */
    if ((uint32_t)(now_ms - watch->last_ms) >= timeout_ms) {
        return OTA_PROGRESS_WATCH_TIMEOUT;
    }
    return OTA_PROGRESS_WATCH_PROGRESS;
}
