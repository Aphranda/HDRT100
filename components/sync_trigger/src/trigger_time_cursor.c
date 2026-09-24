#include "trigger_time_cursor.h"

#include <string.h>

#include "vdc_dpll_manager.h"

static trigger_time_cursor_snapshot_t s_cursor;
static volatile uint32_t s_guard;

static void publish(const trigger_time_cursor_snapshot_t *next)
{
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_ACQ_REL);
    s_cursor = *next;
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_RELEASE);
}

bool trigger_time_cursor_configure(uint32_t grid_epoch, uint32_t grid_generation,
    uint32_t subdivisions)
{
    if (!grid_epoch || !grid_generation || subdivisions < 2u) return false;
    trigger_time_cursor_snapshot_t next = {0};
    next.configured = 1u;
    next.grid_epoch = grid_epoch;
    next.grid_generation = grid_generation;
    next.subdivisions = subdivisions;
    next.last_error = TRIGGER_TIME_CURSOR_NO_SYNC;
    publish(&next);
    return true;
}

void trigger_time_cursor_clear(void)
{
    trigger_time_cursor_snapshot_t next = {0};
    publish(&next);
}

void trigger_time_cursor_service(void)
{
    trigger_time_cursor_snapshot_t next;
    vdc_dpll_manager_sync_io_observer_status_t observer;
    vdc_dpll_manager_committed_model_t model;
    const uint32_t session = vdc_dpll_manager_feedback_session();
    uint32_t delta;

    if (!__atomic_load_n(&s_cursor.configured, __ATOMIC_ACQUIRE)) return;
    if (!trigger_time_cursor_snapshot(&next)) return;
    if (!session || !vdc_dpll_manager_get_committed_model(&model) ||
        model.session != session || !model.token || !model.dco.valid ||
        !model.dco.dco_update_seq) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_NO_SYNC;
        publish(&next);
        return;
    }
    vdc_dpll_manager_get_sync_io_observer_status(&observer);
    if (!observer.enabled || !observer.last_sample_seq) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_NO_SYNC;
        publish(&next);
        return;
    }
    if (next.session != session) {
        next.session = session;
        next.sample_seq = 0u;
        next.pulse_index = 0u;
        next.action_cursor = 0u;
        next.valid = 0u;
    }
    if (next.sample_seq == observer.last_sample_seq) {
        next.model_token = model.token;
        next.dco_update_seq = model.dco.dco_update_seq;
        publish(&next);
        return;
    }
    /* The first observation after configuration/session start establishes the
     * origin; its absolute sample sequence is not a detected gap. */
    delta = next.sample_seq == 0u ? 1u : observer.last_sample_seq - next.sample_seq;
    next.sample_seq = observer.last_sample_seq;
    next.last_event_id = observer.last_event_id;
    next.model_token = model.token;
    next.dco_update_seq = model.dco.dco_update_seq;
    if (delta != 1u) {
        if (delta > 1u) {
            const uint32_t missed = delta - 1u;
            next.missed_sync_count = UINT32_MAX - next.missed_sync_count < missed
                ? UINT32_MAX : next.missed_sync_count + missed;
        }
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_SAMPLE_GAP;
        publish(&next);
        return;
    }
    if (next.pulse_index == UINT64_MAX ||
        next.pulse_index > UINT64_MAX / next.subdivisions) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_OVERFLOW;
        publish(&next);
        return;
    }
    next.fine_slice_index = 0u;
    next.action_cursor = next.pulse_index * next.subdivisions;
    next.pulse_index++;
    next.valid = 1u;
    next.last_error = TRIGGER_TIME_CURSOR_OK;
    publish(&next);
}

bool trigger_time_cursor_snapshot(trigger_time_cursor_snapshot_t *out)
{
    uint32_t before, after;
    if (!out) return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        before = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        *out = s_cursor;
        after = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before == after) return true;
    }
    return false;
}
