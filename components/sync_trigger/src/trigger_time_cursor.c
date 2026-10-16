#include "trigger_time_cursor.h"
#include "trigger_time_projection.h"

#include <string.h>

#include "board_config.h"
#include "vdc_dpll_manager.h"
#include "vdc_timestamp_clock.h"
#include "tdma_runtime_owner.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/platform.h"
#define TRIGGER_CURSOR_HOT(name) __not_in_flash_func(name)
#else
#define TRIGGER_CURSOR_HOT(name) name
#endif

_Static_assert(sizeof(trigger_time_cursor_snapshot_t) % sizeof(uint32_t) == 0u,
    "cursor publication uses whole atomic words");
static uint32_t s_cursor_words[sizeof(trigger_time_cursor_snapshot_t) / sizeof(uint32_t)];
static uint32_t s_integral_slice_ns;
/* Core1 owns this binding during RUN; configure/clear are STOP operations.
 * Keep only identity and the last observed raw tick, not a second DCO copy. */
static struct {
    uint32_t role_generation, clock_epoch, clock_run, local_slot, schedule_crc;
    uint64_t last_raw;
    bool bound, fault;
} s_binding;
static volatile uint32_t s_guard;
/* First sticky fault of the current binding. Core1 writes it once, at the
 * transition into the fault; configure/clear/session reset zero it. */
static trigger_time_cursor_fault_t s_fault;
static volatile uint32_t s_fault_guard;

static void fault_store(const trigger_time_cursor_fault_t *fault)
{
    const uint32_t guard = __atomic_load_n(&s_fault_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_fault_guard, guard + 1u, __ATOMIC_SEQ_CST);
    s_fault = *fault;
    __atomic_store_n(&s_fault_guard, guard + 2u, __ATOMIC_RELEASE);
}

static void fault_reset(void)
{
    const trigger_time_cursor_fault_t none = {0};
    fault_store(&none);
}

static __attribute__((noinline)) void TRIGGER_CURSOR_HOT(publish)(const trigger_time_cursor_snapshot_t *next)
{
    /* Configuration is STOP-owned; Core1 is the only RUN writer. Readers
     * never modify the guard, so publication needs ordered stores, not an
     * exclusive read/modify/write retry loop. */
    const uint32_t guard = __atomic_load_n(&s_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_guard, guard + 1u, __ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(*next) / sizeof(uint32_t); ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)next + i * sizeof(word), sizeof(word));
        __atomic_store_n(&s_cursor_words[i], word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_guard, guard + 2u, __ATOMIC_RELEASE);
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
    s_integral_slice_ns = grid->period_ns % grid->subdivisions == 0u ?
        grid->period_ns / grid->subdivisions : 0u;
    memset(&s_binding, 0, sizeof(s_binding));
    fault_reset();
    publish(&next);
    return true;
}

void trigger_time_cursor_clear(void)
{
    trigger_time_cursor_snapshot_t next = {0};
    s_integral_slice_ns = 0u;
    memset(&s_binding, 0, sizeof(s_binding));
    fault_reset();
    publish(&next);
}

static __attribute__((noinline)) void TRIGGER_CURSOR_HOT(invalidate)(trigger_time_cursor_snapshot_t *next,
    trigger_time_cursor_error_t error, bool sticky)
{
    next->valid = 0u;
    next->last_error = error;
    s_binding.fault = s_binding.fault || sticky;
    publish(next);
}

/* Record which check is about to latch the sticky fault and which of its
 * inputs differed. Called only on that transition, so it stays out of the
 * realtime RAM image. */
static __attribute__((noinline, optimize("Os"))) void cursor_note_fault(
    const trigger_time_cursor_snapshot_t *next, trigger_time_cursor_fault_site_t site,
    trigger_time_cursor_error_t error, const tdma_ring_clock_snapshot_t *ring,
    const vdc_dpll_manager_committed_model_t *model)
{
    if (s_binding.fault) return;
    uint32_t diff = 0u, flags = (uint32_t)site | ((uint32_t)error << 4u);
    trigger_time_cursor_fault_t fault = {.sample_seq = next->sample_seq};
    if (ring) {
        flags |= (ring->enabled ? 1u << 8u : 0u) |
            (ring->adapter_started ? 1u << 9u : 0u) |
            (ring->data_enabled ? 1u << 10u : 0u) |
            (ring->training_dirty ? 1u << 11u : 0u);
        if (ring->config_seq != ring->applied_config_seq)
            diff |= TRIGGER_TIME_CURSOR_DIFF_RING_PENDING;
        if (ring->config_seq != next->ring_config_seq)
            diff |= TRIGGER_TIME_CURSOR_DIFF_RING_CONFIG;
        if (!ring->schedule_crc32) diff |= TRIGGER_TIME_CURSOR_DIFF_RING_SCHEDULE_ZERO;
        fault.ring_config = ring->config_seq;
    }
    if (model) {
        if (model->role_generation != s_binding.role_generation)
            diff |= TRIGGER_TIME_CURSOR_DIFF_ROLE;
        if (model->clock_epoch_id != s_binding.clock_epoch)
            diff |= TRIGGER_TIME_CURSOR_DIFF_CLOCK_EPOCH;
        if (model->clock_run_id != s_binding.clock_run)
            diff |= TRIGGER_TIME_CURSOR_DIFF_CLOCK_RUN;
        if (model->local_slot != s_binding.local_slot)
            diff |= TRIGGER_TIME_CURSOR_DIFF_SLOT;
        if (model->dco.tdma_schedule_crc32 != s_binding.schedule_crc)
            diff |= TRIGGER_TIME_CURSOR_DIFF_SCHEDULE;
        if (model->dco.nominal_period_ns != next->period_ns)
            diff |= TRIGGER_TIME_CURSOR_DIFF_PERIOD;
        if (ring && model->local_slot != ring->local_slot_id)
            diff |= TRIGGER_TIME_CURSOR_DIFF_RING_SLOT;
        if (ring && model->dco.tdma_schedule_crc32 != ring->schedule_crc32)
            diff |= TRIGGER_TIME_CURSOR_DIFF_RING_SCHEDULE;
        fault.model_token = model->token;
    }
    fault.flags = flags | (diff << 16u);
    fault_store(&fault);
}

/* Keep the projection separate to prevent inlining from expanding SRAM
 * past the existing aligned-buffer boundary. Both realtime paths are hot. */
static __attribute__((noinline, optimize("Os"))) void TRIGGER_CURSOR_HOT(cursor_project)(
    trigger_time_cursor_snapshot_t *next,
    const vdc_dpll_manager_committed_model_t *model,
    uint32_t session, uint32_t ring_config)
{
    uint64_t now_raw, local_ns, output_ns;
    if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &now_raw) ||
        now_raw < model->valid_from_raw) {
        invalidate(next, TRIGGER_TIME_CURSOR_MODEL_TIME, false);
        return;
    }
#if 1000000000u % BOARD_SYS_CLOCK_HZ == 0u
    const uint32_t ns_per_tick = 1000000000u / BOARD_SYS_CLOCK_HZ;
    if (now_raw > UINT64_MAX / ns_per_tick) {
        invalidate(next, TRIGGER_TIME_CURSOR_MODEL_TIME, false);
        return;
    }
    local_ns = now_raw * ns_per_tick;
#else
    if (!vdc_timer1_ticks_to_ns(BOARD_SYS_CLOCK_HZ, now_raw, false, &local_ns)) {
        invalidate(next, TRIGGER_TIME_CURSOR_MODEL_TIME, false);
        return;
    }
#endif
    if (!trigger_time_projection_fast(&model->dco, local_ns, &output_ns)) {
        invalidate(next, TRIGGER_TIME_CURSOR_MODEL_TIME, false);
        return;
    }
    if (session != vdc_dpll_manager_feedback_session()) {
        invalidate(next, TRIGGER_TIME_CURSOR_SESSION, false);
        return;
    }
    /* A NEW committed model re-bases the projection: raw time, the projected VDC time
     * and the coarse pulse index are only continuous inside one generation, and the
     * followers' typed follow path re-issues the model (fresh token) during a run.
     * Comparing across that re-base is meaningless, and treating it as a regression is
     * fatal - measured on the bench 2026-10-16, the three followers latched site BACKWARD
     * (err 10) and then site OVERFLOW (err 5) at t+8s/40s/120s of a 361-position scan,
     * exactly where each one's position window stopped growing, while the projection's own
     * coarse index had simply stepped back with the new generation (a uint64 subtraction
     * against the previous generation's pulse_index underflows to ~2^64). So adopt the new
     * generation as the baseline - the projection below overwrites event_vdc_ns, last_raw
     * and pulse_index - and keep both checks for regressions WITHIN one generation, where
     * they are real faults. */
    const bool rebased = s_binding.bound && model->token != next->model_token;
    if (s_binding.bound && !rebased &&
        (now_raw < s_binding.last_raw ||
         output_ns < next->event_vdc_ns)) {
        cursor_note_fault(next, TRIGGER_TIME_CURSOR_FAULT_BACKWARD,
            TRIGGER_TIME_CURSOR_BACKWARD, NULL, model);
        invalidate(next, TRIGGER_TIME_CURSOR_BACKWARD, true);
        return;
    }
    if (output_ns < next->anchor_vdc_ns) {
        invalidate(next, TRIGGER_TIME_CURSOR_GRID_PHASE, false);
        return;
    }
    const uint64_t elapsed = output_ns - next->anchor_vdc_ns;
    const uint64_t coarse = elapsed / next->period_ns;
    const uint32_t remainder = (uint32_t)(elapsed % next->period_ns);
    const uint32_t fine = s_integral_slice_ns ? remainder / s_integral_slice_ns :
        (uint32_t)((uint64_t)remainder * next->subdivisions / next->period_ns);
    /* Valid geometry has 2 <= subdivisions <= period_ns. Hence the complete
     * q is floor(elapsed * subdivisions / period_ns) <= elapsed, and coarse
     * is at most UINT64_MAX/2. Neither q nor coarse+1 can overflow. Across a generation
     * boundary the gap is not defined (see `rebased` above): coarse can legitimately be
     * BELOW the previous generation's pulse_index, and subtracting would underflow into
     * the OVERFLOW latch. One step, no missed-cycle accounting, on that poll only. */
    const uint64_t delta = (s_binding.bound && !rebased) ?
        coarse - (next->pulse_index - 1u) : 1u;
    if (delta > UINT32_MAX - next->sample_seq) {
        cursor_note_fault(next, TRIGGER_TIME_CURSOR_FAULT_OVERFLOW,
            TRIGGER_TIME_CURSOR_OVERFLOW, NULL, model);
        invalidate(next, TRIGGER_TIME_CURSOR_OVERFLOW, true);
        return;
    }
    /* Polling can span more than one millisecond. Attribute the latest
     * position directly; never synthesize received events or replay cells. */
    next->sample_seq += (uint32_t)delta;
    if (s_binding.bound && delta > 1u) {
        const uint64_t missed = delta - 1u;
        next->missed_sync_count = missed > UINT32_MAX - next->missed_sync_count ?
            UINT32_MAX : next->missed_sync_count + (uint32_t)missed;
    }
    next->ring_config_seq = ring_config;
    next->model_token = model->token;
    next->dco_update_seq = model->dco.dco_update_seq;
    next->event_local_ns = local_ns;
    next->event_vdc_ns = output_ns;
    next->fine_slice_index = fine;
    next->action_cursor = coarse * next->subdivisions + fine;
    next->pulse_index = coarse + 1u;
    next->valid = 1u;
    next->last_error = TRIGGER_TIME_CURSOR_OK;
    s_binding.role_generation = model->role_generation;
    s_binding.clock_epoch = model->clock_epoch_id;
    s_binding.clock_run = model->clock_run_id;
    s_binding.local_slot = model->local_slot;
    s_binding.schedule_crc = model->dco.tdma_schedule_crc32;
    s_binding.last_raw = now_raw;
    s_binding.bound = true;
    publish(next);
}

/* Session/configuration transitions are infrequent and need not duplicate
 * their clear path in the realtime RAM footprint. */
static __attribute__((noinline, optimize("Os"))) void cursor_reset_session(
    trigger_time_cursor_snapshot_t *next, uint32_t session)
{
    memset(&s_binding, 0, sizeof(s_binding));
    next->session = session;
    next->sample_seq = 0u;
    next->pulse_index = 0u;
    next->action_cursor = 0u;
    next->fine_slice_index = 0u;
    next->valid = 0u;
    next->last_event_id = 0u;
    next->carrier_sequence = 0u;
    next->identity_crc32 = 0u;
    next->ring_config_seq = 0u;
    next->model_token = 0u;
    next->dco_update_seq = 0u;
    next->event_local_ns = 0u;
    next->event_vdc_ns = 0u;
    next->missed_sync_count = 0u;
    next->last_error = TRIGGER_TIME_CURSOR_OK;
    fault_reset();
}

/* The committed-model owner changes its nonzero token for every DCO byte,
 * session, role generation, clock epoch/run and slot change. It never wraps.
 * Only applied_command_seq may change under one token; this cursor does not
 * consume it. Cache this qualification only via a successfully projected
 * next.model_token, never by recording an unaccepted candidate here. */
static __attribute__((noinline, optimize("Os"))) trigger_time_cursor_error_t
TRIGGER_CURSOR_HOT(cursor_qualify_model)(const trigger_time_cursor_snapshot_t *next,
    const vdc_dpll_manager_committed_model_t *model)
{
    if (!model->dco.valid || !model->dco.dco_update_seq ||
        !model->dco.nominal_period_ns ||
        model->dco.period_adjust_ppb <= -1000000000 ||
        model->dco.lock_state > VDC_DOMAIN_LOCK_FAULT)
        return TRIGGER_TIME_CURSOR_NO_SYNC;
    if (model->dco.nominal_period_ns != next->period_ns ||
        (s_binding.bound &&
         (s_binding.role_generation != model->role_generation ||
          s_binding.clock_epoch != model->clock_epoch_id ||
          s_binding.clock_run != model->clock_run_id ||
          s_binding.local_slot != model->local_slot ||
          s_binding.schedule_crc != model->dco.tdma_schedule_crc32)))
        return TRIGGER_TIME_CURSOR_IDENTITY;
    return TRIGGER_TIME_CURSOR_OK;
}

__attribute__((optimize("Os"))) void TRIGGER_CURSOR_HOT(trigger_time_cursor_service)(void)
{
    trigger_time_cursor_snapshot_t next;
    vdc_dpll_manager_committed_model_t model;
    tdma_ring_clock_snapshot_t ring;
    const uint32_t session = vdc_dpll_manager_feedback_session();

    if (!__atomic_load_n(&s_cursor_words[0], __ATOMIC_ACQUIRE)) return;
    if (!trigger_time_cursor_snapshot(&next)) return;
    if (!session) {
        invalidate(&next, TRIGGER_TIME_CURSOR_NO_SYNC, false);
        return;
    }
    if (next.session != session) cursor_reset_session(&next, session);
    if (s_binding.fault) {
        next.valid = 0u;
        publish(&next);
        return;
    }
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) {
        invalidate(&next, TRIGGER_TIME_CURSOR_NO_SYNC, false);
        return;
    }
    /* A ring still in TRAIN's persona is not ready to bind: right after
     * RING:START the next service stops the adapter for one cycle to restore
     * DATA/CS (tdma_ring_runtime.c), which would otherwise latch the sticky
     * fault on an identity bound one poll earlier. Unbound, this stays a
     * plain retry. TRAIN is serviced only with data disabled, which this check
     * already treats as a ring change once bound. */
    if (!ring.enabled || !ring.adapter_started || !ring.data_enabled ||
        ring.training_dirty ||
        !ring.config_seq || ring.config_seq != ring.applied_config_seq ||
        !ring.schedule_crc32 ||
        (s_binding.bound && next.ring_config_seq != ring.config_seq)) {
        if (s_binding.bound)
            cursor_note_fault(&next, TRIGGER_TIME_CURSOR_FAULT_RING,
                TRIGGER_TIME_CURSOR_IDENTITY, &ring, NULL);
        invalidate(&next, TRIGGER_TIME_CURSOR_IDENTITY, s_binding.bound);
        return;
    }
    /* Reuse the same local committed DCO and TIMER1 coordinates as VDC's
     * periodic output planner. FOLLOWER applies typed clock updates without
     * acquiring the legacy local PI LOCKED state. Neither that state nor RX
     * observation cadence defines this board's locally generated grid. */
    if (!vdc_dpll_manager_get_committed_model(&model) ||
        model.session != session || !model.token) {
        invalidate(&next, TRIGGER_TIME_CURSOR_NO_SYNC, false);
        return;
    }
    if (model.token != next.model_token) {
        const trigger_time_cursor_error_t error = cursor_qualify_model(&next, &model);
        if (error != TRIGGER_TIME_CURSOR_OK) {
            if (error == TRIGGER_TIME_CURSOR_IDENTITY && s_binding.bound)
                cursor_note_fault(&next, TRIGGER_TIME_CURSOR_FAULT_MODEL,
                    error, &ring, &model);
            invalidate(&next, error,
                error == TRIGGER_TIME_CURSOR_IDENTITY && s_binding.bound);
            return;
        }
    }
    /* Ring fields are independently live; a stable model token does not
     * authorize a changed ring, route or schedule. */
    if (model.local_slot != ring.local_slot_id ||
        model.dco.tdma_schedule_crc32 != ring.schedule_crc32) {
        if (s_binding.bound)
            cursor_note_fault(&next, TRIGGER_TIME_CURSOR_FAULT_RING_MODEL,
                TRIGGER_TIME_CURSOR_IDENTITY, &ring, &model);
        invalidate(&next, TRIGGER_TIME_CURSOR_IDENTITY, s_binding.bound);
        return;
    }
    cursor_project(&next, &model, session, ring.config_seq);
}

bool TRIGGER_CURSOR_HOT(trigger_time_cursor_snapshot)(trigger_time_cursor_snapshot_t *out)
{
    uint32_t before, after;
    if (!out) return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        before = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        for (size_t i = 0u; i < sizeof(*out) / sizeof(uint32_t); ++i) {
            const uint32_t word = __atomic_load_n(&s_cursor_words[i], __ATOMIC_RELAXED);
            memcpy((uint8_t *)out + i * sizeof(word), &word, sizeof(word));
        }
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        after = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before == after) return true;
    }
    return false;
}

bool trigger_time_cursor_fault_snapshot(trigger_time_cursor_fault_t *out)
{
    if (!out) return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t before = __atomic_load_n(&s_fault_guard, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        *out = s_fault;
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        if (before == __atomic_load_n(&s_fault_guard, __ATOMIC_ACQUIRE)) return true;
    }
    return false;
}
