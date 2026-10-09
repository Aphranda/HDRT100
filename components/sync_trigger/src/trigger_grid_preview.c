#include "trigger_grid_preview.h"

#include <string.h>
#include "board_config.h"
#include "pico/platform.h"
#include "tdma_runtime_owner.h"
#include "vdc_timestamp_clock.h"
#include "trigger_time_cursor.h"

/* No Core1 work or hardware output owner is introduced. The gate protects
 * different Core0 management callers; TDMA's metadata gate excludes ARM. */
static uint32_t s_grid_busy;
static trigger_grid_preview_snapshot_t s_grid;
static trigger_grid_anchor_snapshot_t s_anchor;

static bool preview_enter(void)
{
    uint32_t idle = 0u;
    return get_core_num() == 0u && __atomic_compare_exchange_n(
        &s_grid_busy, &idle, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void preview_leave(void)
{
    __atomic_store_n(&s_grid_busy, 0u, __ATOMIC_RELEASE);
}

static bool ring_stopped(tdma_ring_clock_snapshot_t *ring)
{
    tdma_ring_clock_snapshot_t before, after;
    tdma_service_ring_runtime_config_t staged;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&before) || before.enabled ||
        before.adapter_started || before.config_seq != before.applied_config_seq ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) || !staged.enabled ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&after) || after.enabled ||
        after.adapter_started || after.config_seq != after.applied_config_seq ||
        before.config_seq != after.config_seq) return false;
    /* STOP/unconfigure clears the runtime's topology fields. Bind the
     * retained owner configuration, never infer a follower's slot as zero.
     * A retained nonempty runtime must still agree with that configuration. */
    if ((before.schedule_crc32 && (before.schedule_crc32 != staged.schedule_crc32 ||
            before.local_slot_id != staged.local_slot_id)) ||
        (after.schedule_crc32 && (after.schedule_crc32 != staged.schedule_crc32 ||
            after.local_slot_id != staged.local_slot_id))) return false;
    *ring = after;
    ring->local_slot_id = staged.local_slot_id;
    ring->schedule_crc32 = staged.schedule_crc32;
    return true;
}

static bool model_available(vdc_dpll_manager_committed_model_t *model)
{
    const uint32_t session = vdc_dpll_manager_feedback_session();
    return session && vdc_dpll_manager_get_committed_model(model) &&
        model->session == session && session == vdc_dpll_manager_feedback_session() &&
        model->token && model->dco.valid && model->dco.dco_update_seq &&
        model->dco.nominal_period_ns && model->dco.period_adjust_ppb > -1000000000 &&
        model->dco.lock_state <= VDC_DOMAIN_LOCK_FAULT;
}

static bool grid_matches_vdc_period(const trigger_time_grid_t *grid,
    const vdc_dpll_manager_committed_model_t *model)
{
    /* Accept the configured operating period instead of pinning both the grid
     * and the DCO to the 1 ms default: a 500/750 us ring profile must be able
     * to bind its grid to the matching DCO period.  Non-zero mutual equality is
     * the real invariant. */
    return grid && model && grid->period_ns != 0u &&
        grid->period_ns == model->dco.nominal_period_ns;
}

static bool binding_matches(const trigger_grid_preview_snapshot_t *bound,
    const tdma_ring_clock_snapshot_t *ring,
    const vdc_dpll_manager_committed_model_t *model)
{
    return bound->session == model->session && bound->ring_config == ring->config_seq &&
        bound->role_generation == model->role_generation &&
        bound->clock_epoch == model->clock_epoch_id && bound->clock_run == model->clock_run_id &&
        bound->local_slot == model->local_slot && model->local_slot == ring->local_slot_id &&
        bound->schedule_crc == model->dco.tdma_schedule_crc32 &&
        model->dco.tdma_schedule_crc32 == ring->schedule_crc32;
}

static bool grid_equal(const trigger_time_grid_t *a, const trigger_time_grid_t *b)
{
    return a->epoch == b->epoch && a->generation == b->generation &&
        a->anchor_vdc_ns == b->anchor_vdc_ns && a->period_ns == b->period_ns &&
        a->subdivisions == b->subdivisions;
}

static bool observer_anchor_valid(uint32_t event_id, uint64_t anchor_vdc_ns,
    const vdc_dpll_manager_committed_model_t *model,
    trigger_grid_anchor_snapshot_t *out)
{
    vdc_dpll_manager_sync_io_observer_status_t observer;
    vdc_dpll_manager_get_sync_io_observer_status(&observer);
    const uint32_t required = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    if (!model || !model->token || observer.enabled == 0u ||
        /* sample_period_ns is the sampler's resolution, not the output period. */
        model->dco.nominal_period_ns != VDC_DOMAIN_DEFAULT_PERIOD_NS ||
        observer.last_event_id != event_id || observer.last_sample_seq == 0u ||
        observer.last_timestamp_source != VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK ||
        observer.last_timestamp_resolution_ns == 0u ||
        observer.last_timestamp_resolution_ns > 100u ||
        (observer.last_timestamp_flags & required) != required ||
        (observer.last_timestamp_flags & VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u ||
        ((((uint64_t)observer.expected_window_start_hi << 32u) |
          observer.expected_window_start_lo) != anchor_vdc_ns) ||
        observer.schedule_crc32 != model->dco.tdma_schedule_crc32) return false;
    if (out) *out = (trigger_grid_anchor_snapshot_t){
        .valid=1u, .event_id=event_id, .sample_seq=observer.last_sample_seq,
        .model_token=model->token, .session=model->session,
        .anchor_vdc_ns=anchor_vdc_ns, .schedule_crc=observer.schedule_crc32,
        .timestamp_source=observer.last_timestamp_source,
        .resolution_ns=observer.last_timestamp_resolution_ns,
        .timestamp_flags=observer.last_timestamp_flags};
    return true;
}

typedef struct {
    const trigger_time_grid_t *grid;
    uint32_t result;
} preview_configuration_t;

static bool configure_stopped(void *context)
{
    preview_configuration_t *request = context;
    tdma_ring_clock_snapshot_t ring;
    vdc_dpll_manager_committed_model_t model = {0};
    if (!ring_stopped(&ring)) return false;
    const bool has_model = model_available(&model) &&
        model.dco.lock_state == VDC_DOMAIN_LOCK_LOCKED;
    if (request->grid->period_ns != VDC_DOMAIN_DEFAULT_PERIOD_NS ||
        (has_model && (!grid_matches_vdc_period(request->grid, &model) ||
                       model.local_slot != ring.local_slot_id ||
                       model.dco.tdma_schedule_crc32 != ring.schedule_crc32))) {
        request->result = TRIGGER_GRID_PREVIEW_MODEL;
        return false;
    }
    if (request->grid->generation <= s_grid.last_generation) {
        const bool same = s_grid.configured && grid_equal(request->grid, &s_grid.grid) &&
            (has_model ? binding_matches(&s_grid, &ring, &model) :
                s_grid.session == 0u && s_grid.ring_config == ring.config_seq &&
                s_grid.local_slot == ring.local_slot_id &&
                s_grid.schedule_crc == ring.schedule_crc32);
        request->result = same ? TRIGGER_TIME_GRID_OK : TRIGGER_GRID_PREVIEW_GENERATION;
        return same;
    }
    const trigger_grid_preview_snapshot_t candidate = {
        .grid = *request->grid, .configured = 1u,
        .last_generation = request->grid->generation,
        .session = has_model ? model.session : 0u, .ring_config = ring.config_seq,
        .role_generation = has_model ? model.role_generation : 0u,
        .clock_epoch = has_model ? model.clock_epoch_id : 0u,
        .clock_run = has_model ? model.clock_run_id : 0u,
        .local_slot = ring.local_slot_id,
        .schedule_crc = ring.schedule_crc32,
    };
    vdc_dpll_manager_committed_model_t after = {0};
    tdma_ring_clock_snapshot_t ring_after;
    if (!ring_stopped(&ring_after) || ring_after.config_seq != ring.config_seq ||
        ring_after.local_slot_id != ring.local_slot_id ||
        ring_after.schedule_crc32 != ring.schedule_crc32 ||
        (has_model && (!model_available(&after) ||
                       memcmp(&model, &after, sizeof(model))))) {
        request->result = TRIGGER_GRID_PREVIEW_CHANGED;
        return false;
    }
    memset(&s_anchor, 0, sizeof(s_anchor));
    s_grid = candidate;
    if (!trigger_time_cursor_configure(request->grid)) {
        request->result = TRIGGER_GRID_PREVIEW_CHANGED;
        return false;
    }
    request->result = TRIGGER_TIME_GRID_OK;
    return true;
}

uint32_t trigger_grid_preview_configure(const trigger_time_grid_t *grid)
{
    if (!trigger_time_grid_valid(grid)) return TRIGGER_TIME_GRID_ARGUMENT;
    if (!preview_enter()) return TRIGGER_GRID_PREVIEW_BUSY;
    preview_configuration_t request = {grid, TRIGGER_GRID_PREVIEW_NOT_STOPPED};
    (void)tdma_service_update_stopped_metadata(tdma_runtime_owner_get(), configure_stopped, &request);
    preview_leave();
    return request.result;
}

static bool clear_stopped(void *unused)
{
    (void)unused;
    const uint32_t last_generation = s_grid.last_generation;
    memset(&s_grid, 0, sizeof(s_grid));
    memset(&s_anchor, 0, sizeof(s_anchor));
    trigger_time_cursor_clear();
    s_grid.last_generation = last_generation;
    return true;
}

uint32_t trigger_grid_preview_bind_anchor(uint32_t event_id,
    uint64_t anchor_vdc_ns)
{
    if (!event_id || !preview_enter()) return event_id ? TRIGGER_GRID_PREVIEW_BUSY
                                                       : TRIGGER_TIME_GRID_ARGUMENT;
    uint32_t result = TRIGGER_GRID_PREVIEW_UNCONFIGURED;
    tdma_ring_clock_snapshot_t ring;
    vdc_dpll_manager_committed_model_t model;
    trigger_grid_anchor_snapshot_t candidate;
    if (!s_grid.configured || !ring_stopped(&ring) || !model_available(&model) ||
        !grid_matches_vdc_period(&s_grid.grid, &model)) goto done;
    result = TRIGGER_GRID_PREVIEW_ANCHOR;
    if (anchor_vdc_ns != s_grid.grid.anchor_vdc_ns) goto done;
    if (!observer_anchor_valid(event_id, anchor_vdc_ns, &model, &candidate)) goto done;
    if (candidate.schedule_crc != ring.schedule_crc32 ||
        model.local_slot != ring.local_slot_id ||
        !binding_matches(&s_grid, &ring, &model)) goto done;
    candidate.generation = s_grid.grid.generation;
    s_anchor = candidate;
    result = TRIGGER_TIME_GRID_OK;
done:
    preview_leave();
    return result;
}

uint32_t trigger_grid_preview_anchor_snapshot(
    trigger_grid_anchor_snapshot_t *out)
{
    if (!out) return TRIGGER_TIME_GRID_ARGUMENT;
    if (!preview_enter()) return TRIGGER_GRID_PREVIEW_BUSY;
    *out = s_anchor;
    preview_leave();
    return TRIGGER_TIME_GRID_OK;
}

uint32_t trigger_grid_preview_plan_anchored(uint32_t epoch, uint32_t generation,
    uint64_t ordinal, uint64_t minimum_lead_ns, uint64_t maximum_ahead_ns,
    trigger_grid_preview_plan_t *out)
{
    if (!out || !preview_enter()) return out ? TRIGGER_GRID_PREVIEW_BUSY
                                              : TRIGGER_TIME_GRID_ARGUMENT;
    const trigger_grid_anchor_snapshot_t anchor = s_anchor;
    const trigger_time_grid_t grid = s_grid.grid;
    vdc_dpll_manager_committed_model_t model;
    vdc_dpll_manager_sync_io_observer_status_t observer;
    const bool bound = s_grid.configured && anchor.valid &&
        anchor.generation == s_grid.grid.generation &&
        anchor.anchor_vdc_ns == grid.anchor_vdc_ns &&
        model_available(&model);
    vdc_dpll_manager_get_sync_io_observer_status(&observer);
    if (!bound || observer.last_event_id != anchor.event_id ||
        observer.last_sample_seq != anchor.sample_seq ||
        model.token != anchor.model_token || model.session != anchor.session) {
        preview_leave();
        return TRIGGER_GRID_PREVIEW_ANCHOR;
    }
    preview_leave();
    /* Existing exact-q planner owns the full STOP/model/lifetime validation.
     * The anchor identity is checked before and after its call below. */
    trigger_grid_preview_plan_t candidate;
    const uint32_t result = trigger_grid_preview_plan(
        epoch, generation, ordinal, minimum_lead_ns, maximum_ahead_ns, &candidate);
    if (result != TRIGGER_TIME_GRID_OK) return result;
    if (!preview_enter()) return TRIGGER_GRID_PREVIEW_BUSY;
    vdc_dpll_manager_get_sync_io_observer_status(&observer);
    vdc_dpll_manager_committed_model_t after;
    const bool still_bound = observer.last_event_id == anchor.event_id &&
        observer.last_sample_seq == anchor.sample_seq &&
        observer.enabled != 0u &&
        observer.last_timestamp_source == anchor.timestamp_source &&
        observer.last_timestamp_resolution_ns == anchor.resolution_ns &&
        observer.last_timestamp_flags == anchor.timestamp_flags &&
        observer.schedule_crc32 == anchor.schedule_crc &&
        ((((uint64_t)observer.expected_window_start_hi << 32u) |
          observer.expected_window_start_lo) == anchor.anchor_vdc_ns) &&
        vdc_dpll_manager_get_committed_model(&after) &&
        after.token == anchor.model_token && after.session == anchor.session;
    preview_leave();
    if (!still_bound) return TRIGGER_GRID_PREVIEW_ANCHOR;
    *out = candidate;
    return TRIGGER_TIME_GRID_OK;
}

uint32_t trigger_grid_preview_clear(void)
{
    if (!preview_enter()) return TRIGGER_GRID_PREVIEW_BUSY;
    const bool cleared = tdma_service_update_stopped_metadata(
        tdma_runtime_owner_get(), clear_stopped, NULL);
    preview_leave();
    return cleared ? TRIGGER_TIME_GRID_OK : TRIGGER_GRID_PREVIEW_NOT_STOPPED;
}

uint32_t trigger_grid_preview_snapshot(trigger_grid_preview_snapshot_t *out)
{
    if (!out) return TRIGGER_TIME_GRID_ARGUMENT;
    if (!preview_enter()) return TRIGGER_GRID_PREVIEW_BUSY;
    tdma_ring_clock_snapshot_t ring;
    const bool stopped = ring_stopped(&ring);
    if (stopped) *out = s_grid;
    preview_leave();
    return stopped ? TRIGGER_TIME_GRID_OK : TRIGGER_GRID_PREVIEW_NOT_STOPPED;
}

uint32_t trigger_grid_preview_plan(uint32_t epoch, uint32_t generation,
    uint64_t ordinal, uint64_t minimum_lead_ns, uint64_t maximum_ahead_ns,
    trigger_grid_preview_plan_t *out)
{
    if (!out || !minimum_lead_ns || minimum_lead_ns > maximum_ahead_ns)
        return TRIGGER_TIME_GRID_ARGUMENT;
    if (!preview_enter()) return TRIGGER_GRID_PREVIEW_BUSY;
    uint32_t result = TRIGGER_GRID_PREVIEW_UNCONFIGURED;
    trigger_grid_preview_plan_t candidate = {0};
    tdma_ring_clock_snapshot_t ring, ring_after;
    vdc_dpll_manager_committed_model_t after;
    if (!s_grid.configured) goto done;
    result = TRIGGER_GRID_PREVIEW_GENERATION;
    if (epoch != s_grid.grid.epoch || generation != s_grid.grid.generation) goto done;
    result = TRIGGER_GRID_PREVIEW_NOT_STOPPED;
    if (!ring_stopped(&ring)) goto done;
    result = TRIGGER_GRID_PREVIEW_MODEL;
    if (!model_available(&candidate.model)) goto done;
    result = TRIGGER_GRID_PREVIEW_CHANGED;
    if (!binding_matches(&s_grid, &ring, &candidate.model)) goto done;
    result = TRIGGER_GRID_PREVIEW_CLOCK;
    candidate.tick_hz = BOARD_SYS_CLOCK_HZ;
    if (!vdc_timestamp_clock_try_read_ticks64(candidate.tick_hz, &candidate.now_ticks)) goto done;
    if (!vdc_timer1_ns_to_ticks(candidate.tick_hz, minimum_lead_ns, true,
            &candidate.minimum_lead_ticks) ||
        !vdc_timer1_ns_to_ticks(candidate.tick_hz, maximum_ahead_ns, false,
            &candidate.maximum_ahead_ticks)) goto done;
    result = TRIGGER_GRID_PREVIEW_MODEL;
    if (candidate.now_ticks < candidate.model.valid_from_raw) goto done;
    uint64_t now_local_ns;
    if (!vdc_timer1_ticks_to_ns(candidate.tick_hz, candidate.now_ticks, false, &now_local_ns) ||
        now_local_ns < candidate.model.dco.base_local_tick64) goto done;
    candidate.configuration = s_grid;
    result = trigger_time_grid_prepare(&s_grid.grid, ordinal, &candidate.model.dco,
        candidate.tick_hz, candidate.now_ticks, candidate.minimum_lead_ticks,
        candidate.maximum_ahead_ticks, &candidate.plan);
    if (result != TRIGGER_TIME_GRID_OK) goto done;
    result = TRIGGER_GRID_PREVIEW_CLOCK;
    if (!vdc_timestamp_clock_try_read_ticks64(candidate.tick_hz, &candidate.checked_at_ticks) ||
        candidate.checked_at_ticks < candidate.now_ticks) goto done;
    result = TRIGGER_GRID_PREVIEW_CHANGED;
    if (!ring_stopped(&ring_after) || !model_available(&after) ||
        !binding_matches(&s_grid, &ring_after, &after) ||
        memcmp(&candidate.model, &after, sizeof(after))) goto done;
    result = TRIGGER_TIME_GRID_LATE;
    if (candidate.plan.deadline_ticks < candidate.checked_at_ticks ||
        candidate.plan.deadline_ticks - candidate.checked_at_ticks < candidate.minimum_lead_ticks)
        goto done;
    *out = candidate;
    result = TRIGGER_TIME_GRID_OK;
done:
    preview_leave();
    return result;
}
