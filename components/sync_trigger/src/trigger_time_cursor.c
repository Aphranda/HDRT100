#include "trigger_time_cursor.h"

#include <string.h>

#include "board_config.h"
#include "vdc_dpll_manager.h"

static trigger_time_cursor_snapshot_t s_cursor;
static vdc_dpll_manager_committed_model_t s_previous_model;
static volatile uint32_t s_guard;

static void publish(const trigger_time_cursor_snapshot_t *next)
{
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_ACQ_REL);
    s_cursor = *next;
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_RELEASE);
}

bool trigger_time_cursor_configure(const trigger_time_grid_t *grid)
{
    if (!trigger_time_grid_valid(grid)) return false;
    trigger_time_cursor_snapshot_t next = {0};
    next.configured = 1u;
    next.grid_epoch = grid->epoch;
    next.grid_generation = grid->generation;
    next.anchor_vdc_ns = grid->anchor_vdc_ns;
    next.period_ns = grid->period_ns;
    next.subdivisions = grid->subdivisions;
    next.last_error = TRIGGER_TIME_CURSOR_NO_SYNC;
    memset(&s_previous_model, 0, sizeof(s_previous_model));
    publish(&next);
    return true;
}

void trigger_time_cursor_clear(void)
{
    trigger_time_cursor_snapshot_t next = {0};
    memset(&s_previous_model, 0, sizeof(s_previous_model));
    publish(&next);
}

static bool cursor_model_matches_event(
    const vdc_dpll_manager_committed_model_t *model,
    const vdc_dpll_manager_sync_event_t *event, uint32_t session)
{
    return model->token && model->session == session && model->dco.valid &&
        model->clock_epoch_id == event->clock_epoch_id &&
        model->clock_run_id == event->clock_run_id &&
        model->local_slot == event->local_slot &&
        model->dco.tdma_schedule_crc32 == event->schedule_crc32;
}

void trigger_time_cursor_service(void)
{
    trigger_time_cursor_snapshot_t next;
    vdc_dpll_manager_vdc_status_t vdc_status;
    vdc_dpll_manager_sync_event_t event;
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
    vdc_dpll_manager_get_vdc_status(&vdc_status);
    if (!vdc_status.sync_seq || !vdc_dpll_manager_get_sync_event(&event) ||
        !event.valid) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_NO_SYNC;
        publish(&next);
        return;
    }
    /* sync_seq identifies a valid ring observation; it is deliberately
     * separate from the DPLL lock state. T2 actions require the common clock
     * to be phase-locked before a raw VDC sequence can become an executable
     * time cursor. Keep the VDC publisher independent so diagnostics retain
     * the acquisition sequence, while fail-closing T2 until LOCKED. */
    if (!vdc_status.ready ||
        vdc_status.lock_state != VDC_DOMAIN_LOCK_LOCKED) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_NO_LOCK;
        publish(&next);
        return;
    }
    if (next.session != session) {
        memset(&s_previous_model, 0, sizeof(s_previous_model));
        next.session = session;
        next.sample_seq = 0u;
        next.pulse_index = 0u;
        next.action_cursor = 0u;
        next.valid = 0u;
        next.carrier_sequence = 0u;
        next.identity_crc32 = 0u;
        next.ring_config_seq = 0u;
        next.model_token = 0u;
        next.dco_update_seq = 0u;
        next.event_local_ns = 0u;
        next.event_vdc_ns = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_OK;
    }
    if (next.last_error == TRIGGER_TIME_CURSOR_SAMPLE_GAP) {
        next.valid = 0u;
        publish(&next);
        return;
    }
    if (event.sync_seq != vdc_status.sync_seq || !event.ring_config_seq ||
        !event.carrier_sequence || !event.identity_crc32 ||
        !event.local_rx_timestamp_ns ||
        (event.timestamp_flags & TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) == 0u ||
        !cursor_model_matches_event(&model, &event, session)) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_IDENTITY;
        publish(&next);
        return;
    }
    if (next.sample_seq == vdc_status.sync_seq) {
        if (next.valid && (next.carrier_sequence != event.carrier_sequence ||
                           next.identity_crc32 != event.identity_crc32 ||
                           next.ring_config_seq != event.ring_config_seq)) {
            next.valid = 0u;
            next.last_error = TRIGGER_TIME_CURSOR_IDENTITY;
            publish(&next);
            return;
        }
        s_previous_model = model;
        publish(&next);
        return;
    }
    /* Local sync_seq checks continuity only. The hardware event projected
     * through this board's model determines the shared grid ordinal. */
    delta = next.sample_seq == 0u ? 1u : vdc_status.sync_seq - next.sample_seq;
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
    uint64_t event_raw;
    if (!vdc_timer1_ns_to_ticks(BOARD_SYS_CLOCK_HZ,
            event.local_rx_timestamp_ns, true, &event_raw)) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_MODEL_TIME;
        publish(&next);
        return;
    }
    const vdc_dpll_manager_committed_model_t previous = s_previous_model;
    s_previous_model = model;
    const vdc_dpll_manager_committed_model_t *event_model = &model;
    if (event_raw < model.valid_from_raw) {
        if (!cursor_model_matches_event(&previous, &event, session) ||
            event_raw < previous.valid_from_raw) {
            next.valid = 0u;
            next.last_error = TRIGGER_TIME_CURSOR_MODEL_TIME;
            publish(&next);
            return;
        }
        event_model = &previous;
    }
    uint64_t event_vdc_ns, sync_index;
    const trigger_time_grid_t grid = {
        .anchor_vdc_ns = next.anchor_vdc_ns,
        .epoch = next.grid_epoch,
        .generation = next.grid_generation,
        .period_ns = next.period_ns,
        .subdivisions = next.subdivisions,
    };
    if (!vdc_domain_dco_local_to_output_ns(&event_model->dco,
            event.local_rx_timestamp_ns, &event_vdc_ns) ||
        !trigger_time_grid_sync_index(&grid, event_vdc_ns, &sync_index)) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_GRID_PHASE;
        publish(&next);
        return;
    }
    if (sync_index == UINT64_MAX ||
        sync_index > UINT64_MAX / next.subdivisions) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_OVERFLOW;
        publish(&next);
        return;
    }
    if (next.sample_seq != 0u &&
        (next.pulse_index == 0u || sync_index != next.pulse_index)) {
        next.valid = 0u;
        next.last_error = TRIGGER_TIME_CURSOR_SAMPLE_GAP;
        publish(&next);
        return;
    }
    next.sample_seq = vdc_status.sync_seq;
    next.last_event_id = event.carrier_sequence;
    next.carrier_sequence = event.carrier_sequence;
    next.identity_crc32 = event.identity_crc32;
    next.ring_config_seq = event.ring_config_seq;
    next.model_token = event_model->token;
    next.dco_update_seq = event_model->dco.dco_update_seq;
    next.event_local_ns = event.local_rx_timestamp_ns;
    next.event_vdc_ns = event_vdc_ns;
    next.fine_slice_index = 0u;
    next.action_cursor = sync_index * next.subdivisions;
    next.pulse_index = sync_index + 1u;
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
