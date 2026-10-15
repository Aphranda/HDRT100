#include "trigger_t2_runtime.h"

#include <string.h>

#include "distributed_refmem.h"
#include "board_config.h"
#include "vdc_dpll_manager.h"
#include "vdc_timer1_coordinate.h"
#include "sync_io_gateway.h"
#include "sync_io_switch.h"
#include "trigger_counter_service.h"
#include "trigger_position_mailbox.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/platform.h"
#include "hardware/structs/systick.h"
#include "hardware/regs/m33.h"
#include "tdma_seat_mount_store.h"
#define T2_HOT(name) __not_in_flash_func(name)
/* Attribute form of T2_HOT, for definitions that tests extract by name. */
#define T2_RAM __not_in_flash("t2")
/* Core1 SysTick, the same clock as the SYNC_TRIGGER phase receipt. */
#define T2_TICK_MASK M33_SYST_RVR_RELOAD_BITS
static inline uint32_t t2_now(void)
{
    return (M33_SYST_RVR_RELOAD_BITS - systick_hw->cvr) & M33_SYST_RVR_RELOAD_BITS;
}
/* Main SRAM has no slack left; this diagnostic lives in scratch Y. */
#define T2_FREEZE_TIMING_SECTION __scratch_y("t2_freeze_timing")
#else
#define T2_HOT(name) name
#define T2_RAM
#define T2_TICK_MASK 0x00FFFFFFu
static inline uint32_t t2_now(void) { return 0u; }
#define T2_FREEZE_TIMING_SECTION
/* The bench diagnostic sink lives in the TDMA owner; host builds drop it. */
static inline void tdma_seat_mount_store_note_reject(uint32_t code) { (void)code; }
#endif
#define T2_BOUNDED __attribute__((noinline, optimize("Os")))

/* STOP validates the full shared tables; RUN retains only immutable fields
 * it actually consumes. The one local template is stored separately. */
static struct {
    trigger_time_grid_t grid;
    trigger_turntable_predictor_config_t predictor;
    uint32_t run_id, source_slot, target_mask, descriptor_window_sync, origin;
    uint32_t deployment_generation;
} s_config;
static trigger_t2_runtime_snapshot_t s_state;

/* Scan STEP for the next run; 0 keeps the rolling window disabled (see the header).
 * Defined after s_state, which the STOP-only guard inspects. */
static int32_t s_scan_step_mdeg;
/* How many times start() actually took the window branch (A.3.6d). */
static uint32_t s_window_branch_entered;
/* Migration equivalence evidence (A.3.11): samples where the window's announced set
 * differs from the legacy announced_mask, compared per scan ordinal. Valid while the
 * scan's positions coincide with the three legacy slots (the current bench scan). */
static uint32_t s_announce_mismatch_count;

/* Migration step 2, slice 1 (TRIGGER_T2_TODO.md A.3.13): the published threshold mask is
 * derived from the rolling window's own announcements instead of the legacy array. The
 * two sets were measured identical at every sample (announce_mismatch == 0), and this
 * block runs after window_advance() and before the store, so the sets are aligned. A run
 * without a configured scan step keeps the legacy source unchanged. */
static uint32_t t2_published_mask(void)
{
    if (!s_scan_step_mdeg) return s_state.predictor.announced_mask;
    uint32_t mask = 0u;
    for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot) {
        const trigger_turntable_window_entry_t *entry = &s_state.predictor.window[slot];
        if (entry->valid && entry->announced &&
            entry->absolute_index < TRIGGER_T2_RUNTIME_POSITION_COUNT)
            mask |= 1u << entry->absolute_index;
    }
    return mask;
}

bool trigger_t2_runtime_scan_step(int32_t step_mdeg)
{
    /* Only the value is validated here: the SCPI configure path already refuses to run
     * while the load is forbidden, and trigger_t2_runtime_configure() resets the state,
     * so consulting s_state.active here would reject a legitimate re-configure (measured
     * on the bench: the T2 acceptance came back with boards_configured=0 and all targets
     * zero because the mount step's configure was refused while a previous run was still
     * marked active). */
    if (step_mdeg <= 0) return false;
    s_scan_step_mdeg = step_mdeg;
    return true;
}
/* Static templates never change in RUN. The first hardware executor has one
 * local template per position and one pending hardware transaction per board. */
static trigger_action_template_t s_local_template;
static uint32_t s_local_template_count, s_deployment_crc;
typedef struct {
    uint64_t action_q, deadline_tick;
    uint32_t session, ring_config_seq, prediction_version, expiry_sync;
    uint32_t model_token;
} t2_deadline_plan_t;
_Static_assert(sizeof(t2_deadline_plan_t) % sizeof(uint32_t) == 0u,
    "deadline handoff uses atomic words");
typedef struct {
    uint32_t words[sizeof(t2_deadline_plan_t) / sizeof(uint32_t)];
    uint32_t guard;
} t2_deadline_mailbox_t;
/* Core0 is the only RUN writer of mailboxes; Core1 owns plans and states. */
static t2_deadline_mailbox_t s_deadline_mailbox[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static t2_deadline_plan_t s_deadline[TRIGGER_T2_RUNTIME_POSITION_COUNT];
/* Step 1 (T2-DEPLOY-003): the wire reuses instance ids 1..3 for every position of
 * a scan, so a sticky per-slot frozen bit stopped the follower after the first
 * few positions. This window owns the instances instead: it recycles a slot once
 * its action finished, and the acknowledgement byte is derived from it, so it
 * always names the instances that are currently awaiting confirmation. */
static trigger_position_window_t s_position_window;
static uint32_t s_deadline_seen[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static uint32_t s_frozen_public;
/* Core1 publishes the threshold before attempting the FREEZE handoff; Core0
 * abandons in-progress predictions for that position. Transport additionally
 * serializes PREVIEW/FREEZE acceptance to close the final check/stage race. */
static uint32_t s_threshold_public;
static uint32_t s_version_exhausted;
static uint32_t s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
static uint32_t s_pending_mask;
static uint32_t s_core0_prepare_calls, s_core0_preview_saved, s_core0_prepare_last_reason;
static volatile uint32_t s_guard;

#define T2_WINDOW_HISTORY_CAPACITY 17u
typedef struct { uint32_t count, coarse; } t2_count_sample_t;
static t2_count_sample_t s_count_history[T2_WINDOW_HISTORY_CAPACITY];
static uint32_t s_count_head, s_count_size, s_count_last_seq;
static uint32_t s_last_calculated_coarse[TRIGGER_T2_RUNTIME_POSITION_COUNT];
_Static_assert(sizeof(trigger_position_descriptor_t) % sizeof(uint32_t) == 0u,
    "preview handoff uses whole atomic words");
static uint32_t s_preview_history[TRIGGER_T2_RUNTIME_POSITION_COUNT][3]
    [sizeof(trigger_position_descriptor_t) / sizeof(uint32_t)];
static volatile uint32_t s_preview_guard;
static volatile uint32_t s_session;

static bool T2_HOT(preview_copy)(uint32_t index, uint32_t age,
                         trigger_position_descriptor_t *out)
{
    if (index >= TRIGGER_T2_RUNTIME_POSITION_COUNT || age >= 3u || !out)
        return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t before = __atomic_load_n(&s_preview_guard, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        for (size_t i = 0u; i < sizeof(*out) / sizeof(uint32_t); ++i) {
            const uint32_t word = __atomic_load_n(&s_preview_history[index][age][i],
                                                 __ATOMIC_RELAXED);
            memcpy((uint8_t *)out + i * sizeof(word), &word, sizeof(word));
        }
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        if (before == __atomic_load_n(&s_preview_guard, __ATOMIC_ACQUIRE))
            return out->prediction_version != 0u;
    }
    return false;
}

/* Follower PREVIEW accept runs on the Core1 receive path. */
static void T2_HOT(preview_save)(uint32_t index,
                         const trigger_position_descriptor_t *descriptor)
{
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0u; i < sizeof(*descriptor) / sizeof(uint32_t); ++i) {
        const uint32_t old = __atomic_load_n(&s_preview_history[index][1][i], __ATOMIC_RELAXED);
        const uint32_t latest = __atomic_load_n(&s_preview_history[index][0][i], __ATOMIC_RELAXED);
        uint32_t word;
        memcpy(&word, (const uint8_t *)descriptor + i * sizeof(word), sizeof(word));
        __atomic_store_n(&s_preview_history[index][2][i], old, __ATOMIC_RELAXED);
        __atomic_store_n(&s_preview_history[index][1][i], latest, __ATOMIC_RELAXED);
        __atomic_store_n(&s_preview_history[index][0][i], word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_RELEASE);
}

static void T2_HOT(publish_begin)(void)
{
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_ACQ_REL);
}

static void T2_HOT(publish_end)(void)
{
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_RELEASE);
}

/* FREEZE starts freezing; it completes when every target reports the position
 * frozen. Until then the origin resends it, spaced so PREVIEWs still board. */
#define T2_FREEZE_RESEND_SLICES 4u
static uint64_t s_freeze_last_slice;
static uint32_t s_freeze_resend_next;

static T2_BOUNDED T2_RAM bool resend_one_freeze(void)
{
    uint32_t open = s_state.published_mask &
        ~(s_state.freeze_complete_mask | s_state.freeze_abandoned_mask);
    if (!open) {
        /* Nothing waits for completion, so nothing is pending. */
        s_state.freeze_ack_stale_mask = 0u;
        s_state.freeze_ack_incomplete_mask = 0u;
        return true;
    }
    /* The acknowledgement path can only fill a slot from a RECEIVED image, so a
     * mask that carries the position's own source slot can never complete: the
     * origin would resend every position until its window closed. Wait on the
     * peers only; a mask with no peer (single-board bench) is complete by
     * construction. */
    const uint32_t own = 1u << s_config.source_slot;
    const uint32_t peers = (s_config.target_mask & ~own);
    s_state.freeze_ack_own_in_mask = (s_config.target_mask & own) ? 1u : 0u;
    s_state.freeze_complete_mask |= peers
        ? (open & distributed_refmem_position_freeze_acked(s_config.run_id, peers))
        : open;
    if (peers)
        distributed_refmem_position_ack_pending(s_config.run_id, peers,
            &s_state.freeze_ack_stale_mask, &s_state.freeze_ack_incomplete_mask);
    else {
        s_state.freeze_ack_stale_mask = 0u;
        s_state.freeze_ack_incomplete_mask = 0u;
    }
    open &= ~s_state.freeze_complete_mask;
    const uint64_t now = s_state.cursor.pulse_index;
    if (!open || now - s_freeze_last_slice < T2_FREEZE_RESEND_SLICES) return true;
    for (uint32_t step = 0u; step < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++step) {
        const uint32_t index = (s_freeze_resend_next + step) % TRIGGER_T2_RUNTIME_POSITION_COUNT;
        const uint32_t bit = 1u << index;
        if (!(open & bit)) continue;
        const uint32_t expiry = s_state.descriptors[index].expiry_sync;
        if (expiry && now > (uint64_t)expiry + 1u) {
            s_state.freeze_abandoned_mask |= bit;
            if (s_state.freeze_abandoned_count != UINT32_MAX)
                ++s_state.freeze_abandoned_count;
            continue;
        }
        const trigger_position_descriptor_t freeze = {
            .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
            .stage = TRIGGER_POSITION_STAGE_FREEZE,
            .position_id = index + 1u,
            .prediction_version = s_state.freeze_version[index],
        };
        if (!distributed_refmem_stage_position_descriptor(&freeze, s_config.target_mask)) {
            s_state.transport_reject_count++;
            s_state.last_error = TRIGGER_T2_RUNTIME_TRANSPORT;
            return false;
        }
        s_state.freeze_resend_count++;
        s_freeze_last_slice = now;
        s_freeze_resend_next = index + 1u;
        return true;
    }
    return true;
}

/* Max clk_sys cycles of the origin FREEZE path, per run: the pending check,
 * the threshold stage, the resend/ack pass and the whole call. It runs from
 * XIP every coarse slice; this splits the SYNC_TRIGGER RECEIVE segment. */
static uint32_t T2_FREEZE_TIMING_SECTION
    s_freeze_cycles[TRIGGER_T2_RUNTIME_FREEZE_TIMING_COUNT];

static inline __attribute__((always_inline)) void freeze_timing_note(uint32_t index,
    uint32_t start)
{
    const uint32_t cycles = (t2_now() - start) & T2_TICK_MASK;
    if (cycles > s_freeze_cycles[index]) s_freeze_cycles[index] = cycles;
}

/* Diagnostic split of the origin's SYNC_TRIGGER service, one maximum per part of
 * service_origin(). The RECEIVE segment maximum alone says the phase is over its
 * budget but not which part spends it, and the code is small enough that the
 * suspect cannot be identified by reading it (measured 2026-10-13: RECEIVE 22.5k
 * cycles while the whole FREEZE call is only 4.9k). Probe only - no behaviour. */
static uint32_t T2_FREEZE_TIMING_SECTION
    s_origin_probe_max[TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNT];

static inline __attribute__((always_inline)) void origin_probe_end(uint32_t part,
    uint32_t *mark)
{
    const uint32_t now = t2_now();
    const uint32_t cycles = (now - *mark) & T2_TICK_MASK;
    if (cycles > s_origin_probe_max[part]) s_origin_probe_max[part] = cycles;
    *mark = now;
}

static bool stage_one_freeze_timed(void);
static bool stage_pending_freeze(void);

/* The origin calls this every coarse slice. While it ran from XIP, a
 * threshold slice overran SYNC_TRIGGER (26871 > 26800 cycles) and
 * quarantined the load (t2-20261012-100700), so the whole FREEZE path runs
 * from SRAM like service_origin; s_freeze_cycles splits what remains. */
static T2_BOUNDED bool T2_HOT(stage_one_freeze)(void)
{
    const uint32_t start = t2_now();
    const bool ok = stage_one_freeze_timed();
    freeze_timing_note(TRIGGER_T2_RUNTIME_FREEZE_TIMING_TOTAL, start);
    return ok;
}

static T2_BOUNDED bool T2_HOT(stage_one_freeze_timed)(void)
{
    /* The legacy descriptor mailbox remains single-depth until compact-q
     * transport admission is installed. Do not overtake an in-flight PREVIEW
     * and reorder it behind FREEZE. */
    uint32_t mark = t2_now();
    const bool pending = distributed_refmem_position_descriptor_pending();
    freeze_timing_note(TRIGGER_T2_RUNTIME_FREEZE_TIMING_PENDING, mark);
    if (pending) return true;
    if (!s_pending_mask) {
        mark = t2_now();
        const bool ok = resend_one_freeze();
        freeze_timing_note(TRIGGER_T2_RUNTIME_FREEZE_TIMING_RESEND, mark);
        return ok;
    }
    mark = t2_now();
    const bool staged = stage_pending_freeze();
    freeze_timing_note(TRIGGER_T2_RUNTIME_FREEZE_TIMING_STAGE, mark);
    return staged;
}

static T2_BOUNDED bool T2_HOT(stage_pending_freeze)(void)
{
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT;
         ++index) {
        if ((s_pending_mask & (1u << index)) == 0u) continue;
        trigger_position_descriptor_t latest;
        if (!preview_copy(index, 0u, &latest)) {
            /* T2-FREEZE-002 (评审 A-13): a pending position with no saved PREVIEW
             * used to end this whole pass (return true), so every later pending
             * position was never even examined and the loss left no trace. Record
             * the position once - the mask names it and the counter reports how
             * many positions this run lost that way - then keep examining the
             * remaining pending positions. The pending bit stays set so a preview
             * that still arrives can be staged, and window expiry keeps the
             * existing abandonment evidence. */
            const uint32_t no_preview_bit = 1u << index;
            if ((s_state.freeze_no_preview_mask & no_preview_bit) == 0u) {
                s_state.freeze_no_preview_mask |= no_preview_bit;
                s_state.freeze_no_preview_count++;
            }
            continue;
        }
        const trigger_position_descriptor_t freeze = {
            .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
            .stage = TRIGGER_POSITION_STAGE_FREEZE,
            .position_id = index + 1u,
            .prediction_version = latest.prediction_version,
        };
        if (!distributed_refmem_stage_position_descriptor(&freeze,
                                                         s_config.target_mask)) {
            s_state.transport_reject_count++;
            s_state.last_error = TRIGGER_T2_RUNTIME_TRANSPORT;
            return false;
        }
        s_state.descriptors[index] = latest;
        s_state.freeze_version[index] = latest.prediction_version;
        /* Step 1 (T2-DEPLOY-003): this is the writer that actually freezes on a
         * follower (measured: window entered=3 while frozen=0 when only
         * accept_remote_position() was instrumented), so the window must be fed
         * here as well: instance id = this slot + 1, identity = the solve epoch. */
        if (trigger_position_window_alloc(&s_position_window, index + 1u,
                                          latest.count_epoch,
                                          latest.prediction_version, 0u)) {
            (void)trigger_position_window_mark(&s_position_window, index + 1u,
                                               TRIGGER_POSITION_WINDOW_ANNOUNCED);
            (void)trigger_position_window_mark(&s_position_window, index + 1u,
                                               TRIGGER_POSITION_WINDOW_FROZEN);
        }
        s_state.frozen_mask |= 1u << index;
        s_state.published_mask |= 1u << index;
        s_pending_mask &= ~(1u << index);
        s_freeze_last_slice = s_state.cursor.pulse_index;
        return true;
    }
    return true;
}

static void deadline_publish(uint32_t index, const t2_deadline_plan_t *plan)
{
    t2_deadline_mailbox_t *mail = &s_deadline_mailbox[index];
    (void)__atomic_add_fetch(&mail->guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0u; i < sizeof(*plan) / sizeof(uint32_t); ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)plan + i * sizeof(word), sizeof(word));
        __atomic_store_n(&mail->words[i], word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&mail->guard, 1u, __ATOMIC_RELEASE);
}

/* Follower path runs every coarse slice; from XIP it overran SYNC_TRIGGER. */
static T2_BOUNDED void T2_HOT(deadline_receive)(void)
{
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        if (s_state.frozen_mask & (1u << index)) continue;
        t2_deadline_mailbox_t *mail = &s_deadline_mailbox[index];
        const uint32_t before = __atomic_load_n(&mail->guard, __ATOMIC_ACQUIRE);
        if ((before & 1u) || before == s_deadline_seen[index]) continue;
        t2_deadline_plan_t plan;
        for (size_t i = 0u; i < sizeof(plan) / sizeof(uint32_t); ++i) {
            const uint32_t word = __atomic_load_n(&mail->words[i], __ATOMIC_RELAXED);
            memcpy((uint8_t *)&plan + i * sizeof(word), &word, sizeof(word));
        }
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        if (before != __atomic_load_n(&mail->guard, __ATOMIC_ACQUIRE)) continue;
        s_deadline_seen[index] = before;
        if (plan.prediction_version && plan.session == s_state.cursor.session &&
            plan.ring_config_seq == s_state.cursor.ring_config_seq)
            s_deadline[index] = plan;
    }
}

/* Peak cycles of one service segment; locates a phase overrun without a probe. */
static inline __attribute__((always_inline)) void segment_end(uint32_t segment,
                                                              uint32_t *mark)
{
    const uint32_t now = t2_now();
    const uint32_t cycles = (now - *mark) & T2_TICK_MASK;
    if (cycles > s_state.segment_max_cycles[segment])
        s_state.segment_max_cycles[segment] = cycles;
    *mark = now;
}

/* One SRAM copy of the validator for the follower receive path. */
static T2_BOUNDED bool T2_HOT(descriptor_valid)(const trigger_position_descriptor_t *descriptor)
{
    return trigger_position_descriptor_valid(descriptor, s_config.grid.subdivisions);
}

static T2_RAM void action_fail(uint32_t index)
{
    s_state.action_failed_mask |= 1u << index;
    {
        const uint32_t failed = trigger_position_window_instance_at(&s_position_window, index);
        if (failed != 0u) {
            (void)trigger_position_window_mark(&s_position_window, failed,
                                               TRIGGER_POSITION_WINDOW_ABANDONED);
            (void)trigger_position_window_release(&s_position_window, failed);
        }
    }
    s_state.last_error = TRIGGER_T2_RUNTIME_ACTION;
}

static T2_BOUNDED bool T2_HOT(accept_remote_position)(uint64_t current_ms)
{
    trigger_position_descriptor_t descriptor;
    if (!distributed_refmem_get_position_descriptor(
            s_config.source_slot, &descriptor)) return true;
    if (descriptor.position_id < 1u ||
        descriptor.position_id > TRIGGER_POSITION_INSTANCE_MAX ||
        descriptor.prediction_version > TRIGGER_T2_PREDICTION_VERSION_MAX)
        goto reject;
    uint32_t index;
    uint32_t bit;
    /* The descriptor carries the solve epoch as its absolute identity (the wire's
     * compact target_ms is not part of this legacy descriptor), so the window is
     * keyed by instance here and by count_epoch for the identity check. */
    if (!trigger_position_window_alloc(&s_position_window, descriptor.position_id,
                                       descriptor.count_epoch,
                                       descriptor.prediction_version, 0u))
        goto reject;
    {
        const int32_t slot = trigger_position_window_find(&s_position_window,
                                                          descriptor.position_id);
        /* The window is as wide as the wire acknowledgement; this runtime keeps
         * per-position state for POSITION_COUNT slots only, so a slot beyond that
         * is refused instead of indexing past s_deadline[]/freeze_version[]. */
        if (slot < 0 || (uint32_t)slot >= TRIGGER_T2_RUNTIME_POSITION_COUNT)
            goto reject;
        index = (uint32_t)slot;
        bit = 1u << index;
    }
    if (trigger_position_window_find(&s_position_window, descriptor.position_id) >= 0 &&
        trigger_position_window_ack_mask(&s_position_window) & bit) return true;
    if (descriptor.stage == TRIGGER_POSITION_STAGE_FREEZE) {
        if (!descriptor_valid(&descriptor))
            goto reject;
        /* mark() forbids stage jumps (ENTERED -> FROZEN is a two-step move and was
         * refused every time - bench: mark_rejected 34 per follower with alloc 0),
         * so announce first; re-marking is idempotent and therefore safe here. */
        (void)trigger_position_window_mark(&s_position_window,
                                           descriptor.position_id,
                                           TRIGGER_POSITION_WINDOW_ANNOUNCED);
        (void)trigger_position_window_mark(&s_position_window,
                                           descriptor.position_id,
                                           TRIGGER_POSITION_WINDOW_FROZEN);
        s_state.frozen_mask |= bit;
        __atomic_store_n(&s_frozen_public, s_state.frozen_mask, __ATOMIC_RELEASE);
        /* Freeze complete for this follower: report it on every image. */
        distributed_refmem_note_position_frozen(s_config.run_id, true, s_state.frozen_mask);
        s_state.freeze_version[index] = descriptor.prediction_version;
        const t2_deadline_plan_t *plan = &s_deadline[index];
        s_state.freeze_local_age[index] = 0xFFu;
        s_state.freeze_local_version[index] = plan->prediction_version;
        if (!plan->prediction_version) {
            s_state.freeze_missing_local_count++;
            s_state.freeze_mismatch_count++;
            action_fail(index);
            return false;
        }
        /* FREEZE uses the newest completed local solve, even when RX has a
         * newer PREVIEW which Core0 has not solved yet. Never solve at FREEZE. */
        for (uint32_t age = 0u; age < 3u; ++age) {
            trigger_position_descriptor_t previous;
            if (preview_copy(index, age, &previous) &&
                previous.prediction_version == plan->prediction_version) {
                s_state.freeze_local_age[index] = (uint8_t)age;
                s_state.descriptors[index] = previous;
                break;
            }
        }
        if (plan->prediction_version != descriptor.prediction_version) {
            s_state.freeze_mismatch_count++;
            s_state.freeze_fallback_count++;
        }
        if (current_ms > plan->expiry_sync ||
            plan->session != s_state.cursor.session ||
            plan->ring_config_seq != s_state.cursor.ring_config_seq ||
            plan->action_q <= s_state.cursor.action_cursor) {
            action_fail(index);
            return false;
        }
        for (uint32_t other = 0u; other < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++other) {
            if (other == index || !(s_state.frozen_mask & (1u << other)) ||
                (s_state.action_failed_mask & (1u << other))) continue;
            const uint64_t a = plan->action_q, b = s_deadline[other].action_q;
            const uint64_t distance = a > b ? a - b : b - a;
            if (distance < s_local_template.span_slices ||
                (s_armed_index == other && a < b)) {
                action_fail(index);
                return false;
            }
        }
        return true;
    }
    /* The compact wire carries only the slice; bind it to this run's start-up
     * identity and grid before validation. No local CRC on Core1: a compact
     * record is identified by its version, and a bitwise CRC here overran the
     * SYNC_TRIGGER phase on followers. */
    descriptor.run_id = s_config.run_id;
    descriptor.sync_epoch = s_state.cursor.grid_epoch;
    descriptor.count_epoch = s_state.predictor.count_epoch;
    descriptor.grid_generation = s_config.grid.generation;
    descriptor.deployment_generation = s_config.deployment_generation;
    descriptor.deployment_crc = s_deployment_crc;
    descriptor.valid_from_sync = 0u;
    descriptor.expiry_sync = descriptor.target_ms_index >
        UINT32_MAX - s_config.descriptor_window_sync ? 0u :
        (uint32_t)descriptor.target_ms_index + s_config.descriptor_window_sync;
    descriptor.descriptor_crc32 = 0u;
    /* Transport owns wire CRC. Validate each new version here once, not the
     * same deployment/table CRC on every realtime polling call. */
    if (s_state.received_mask & bit &&
        s_state.descriptors[index].prediction_version == descriptor.prediction_version)
        return true;
    if (!descriptor_valid(&descriptor) ||
        descriptor.run_id != s_config.run_id ||
        descriptor.sync_epoch != s_state.cursor.grid_epoch ||
        descriptor.count_epoch != s_state.predictor.count_epoch ||
        descriptor.grid_generation != s_config.grid.generation ||
        descriptor.deployment_generation != s_config.deployment_generation ||
        descriptor.deployment_crc != s_deployment_crc) {
        goto reject;
    }
    /* Window admission, split so a LATE arrival is attributed instead of being
     * folded into the aggregate descriptor reject (which mixes every rejection
     * cause above too). The accept/reject decision is unchanged: an empty
     * window (expiry_sync == 0, target overflow) is still rejected, it is just
     * not counted as lateness because no window was ever promised.
     * valid_from_sync is forced to 0 by this consumer, so only lateness is
     * reachable here. */
    if (current_ms < descriptor.valid_from_sync ||
        current_ms > descriptor.expiry_sync) {
        if (descriptor.expiry_sync != 0u && current_ms > descriptor.expiry_sync) {
            ++s_state.late_arrival_count;
            /* Magnitude for the page: how late, against which promise. */
            s_state.late_last_current_ms = (uint32_t)current_ms;
            s_state.late_last_expiry_sync = descriptor.expiry_sync;
        }
        goto reject;
    }
    if (s_state.received_mask & bit &&
        s_state.descriptors[index].prediction_version >= descriptor.prediction_version)
        goto reject;
    s_state.descriptors[index] = descriptor;
    preview_save(index, &descriptor);
    s_state.received_mask |= bit;
    return true;
reject:
    s_state.descriptor_reject_count++;
    s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
    return false;
}

static void complete_action(uint32_t index)
{
    const uint32_t bit = 1u << index;
    s_state.emitted_position_mask |= bit;
    s_state.action_completed_mask |= bit;
    {
        const uint32_t finished = trigger_position_window_instance_at(&s_position_window, index);
        if (finished != 0u) {
            (void)trigger_position_window_mark(&s_position_window, finished,
                                               TRIGGER_POSITION_WINDOW_EXECUTED);
            (void)trigger_position_window_release(&s_position_window, finished);
        }
    }
    if (s_state.event_count >= TRIGGER_T2_RUNTIME_EVENT_CAPACITY) {
        s_state.event_dropped_count++;
        return;
    }
    const uint32_t event_index = s_state.event_count++;
    s_state.events[event_index] = (trigger_action_runtime_event_t){
        .position_id = index + 1u, .template_id = s_local_template.template_id,
        .role = s_local_template.role, .kind = s_local_template.kind,
        .argument = s_local_template.argument, .action_q = s_deadline[index].action_q};
    s_state.event_dispatched[event_index] = 1u;
}

static void T2_HOT(note_backend)(uint32_t index, uint32_t state, uint64_t before, uint64_t after)
{
    if (index >= TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    s_state.action_backend_state[index] = state;
    s_state.action_origin_width_ticks[index] = !after || after < before ||
        after - before > UINT32_MAX ? UINT32_MAX : (uint32_t)(after - before);
}

static T2_RAM bool cancel_backend(void)
{
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_deadline_status_t status;
        (void)sync_io_switch_realtime_cancel();
        if (!sync_io_switch_realtime_status(&status)) return true;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        return status.state == SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
    }
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_GATEWAY) {
        sync_io_gateway_deadline_status_t status;
        (void)sync_io_gateway_realtime_cancel();
        if (!sync_io_gateway_realtime_status(&status)) return true;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        return status.state == SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
    }
    return false;
}

static void stop_backend_core0(void)
{
    if (s_armed_index >= TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    bool stopped = false;
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_snapshot_t status;
        stopped = sync_io_switch_snapshot(&status) && status.loaded &&
            sync_io_switch_stop(status.generation);
    } else if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_GATEWAY) {
        sync_io_gateway_snapshot_t status;
        stopped = sync_io_gateway_snapshot(&status) && status.loaded &&
            sync_io_gateway_stop(status.generation);
    }
    /* STOP is never a completion. Backend retains CANCELLED/UNCERTAIN detail;
     * inability to stop is additionally visible in the T2 diagnostic. */
    if (!stopped) s_state.action_uncertain_mask |= 1u << s_armed_index;
}

/* Armed-deadline polling runs every slice; from XIP it overran followers. */
static T2_BOUNDED void T2_HOT(service_deadlines)(bool cursor_valid)
{
    if (!s_local_template_count) return;
    bool complete = false, terminal = false, uncertain = false;
    uint32_t sequence = 0u;
    uint64_t deadline = 0u;
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_realtime_service_core1();
        sync_io_switch_deadline_status_t status;
        if (!sync_io_switch_realtime_status(&status)) return;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        sequence = status.sequence; deadline = status.deadline_tick;
        complete = status.state == SYNC_IO_SWITCH_DEADLINE_COMPLETE;
        uncertain = status.state == SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
        terminal = uncertain || status.state == SYNC_IO_SWITCH_DEADLINE_CANCELLED;
    } else {
        sync_io_gateway_realtime_service_core1();
        sync_io_gateway_deadline_status_t status;
        if (!sync_io_gateway_realtime_status(&status)) return;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        sequence = status.sequence; deadline = status.deadline_tick;
        complete = status.state == SYNC_IO_GATEWAY_DEADLINE_COMPLETE &&
            status.completed_sequence == status.sequence;
        uncertain = status.state == SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
        terminal = uncertain || status.state == SYNC_IO_GATEWAY_DEADLINE_CANCELLED;
    }
    if (s_armed_index < TRIGGER_T2_RUNTIME_POSITION_COUNT) {
        const uint32_t index = s_armed_index, bit = 1u << index;
        if (sequence != s_state.action_sequence[index] ||
            deadline != s_deadline[index].deadline_tick) {
            cancel_backend();
            s_state.action_uncertain_mask |= bit;
            action_fail(index);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        } else if (complete) {
            complete_action(index);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        } else if (terminal || !cursor_valid ||
                   s_deadline[index].session != s_state.cursor.session ||
                   s_deadline[index].ring_config_seq != s_state.cursor.ring_config_seq) {
            if (!terminal) uncertain = cancel_backend();
            if (uncertain) s_state.action_uncertain_mask |= bit;
            action_fail(index);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        }
    }
    if (!cursor_valid) {
        s_state.action_failed_mask |= s_state.frozen_mask & ~s_state.action_completed_mask;
        return;
    }
    if (s_armed_index < TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    uint32_t selected = TRIGGER_T2_RUNTIME_POSITION_COUNT;
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        const uint32_t bit = 1u << index;
        if (!(s_state.frozen_mask & bit) ||
            ((s_state.action_failed_mask | s_state.action_completed_mask) & bit)) continue;
        const t2_deadline_plan_t *plan = &s_deadline[index];
        if (!plan->prediction_version || plan->session != s_state.cursor.session ||
            plan->ring_config_seq != s_state.cursor.ring_config_seq ||
            s_state.cursor.action_cursor / s_config.grid.subdivisions > plan->expiry_sync ||
            plan->action_q <= s_state.cursor.action_cursor) {
            action_fail(index);
            continue;
        }
        if (selected == TRIGGER_T2_RUNTIME_POSITION_COUNT ||
            plan->action_q < s_deadline[selected].action_q) selected = index;
    }
    if (selected == TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    const t2_deadline_plan_t *plan = &s_deadline[selected];
    const bool armed = s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH ?
        sync_io_switch_realtime_schedule_at(s_local_template.argument, plan->deadline_tick) :
        sync_io_gateway_realtime_schedule_at(s_local_template.argument, plan->deadline_tick);
    if (!armed) {
        /* A rejected absolute deadline is terminal. No fire-now fallback.
         * Origin validation can fail after the edge: preserve UNCERTAIN. */
        if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
            sync_io_switch_deadline_status_t status;
            if (sync_io_switch_realtime_status(&status)) {
                note_backend(selected, status.state, status.origin_before_tick, status.origin_after_tick);
                if (status.state == SYNC_IO_SWITCH_DEADLINE_UNCERTAIN)
                    s_state.action_uncertain_mask |= 1u << selected;
            }
        } else {
            sync_io_gateway_deadline_status_t status;
            if (sync_io_gateway_realtime_status(&status)) {
                note_backend(selected, status.state, status.origin_before_tick, status.origin_after_tick);
                if (status.state == SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN)
                    s_state.action_uncertain_mask |= 1u << selected;
            }
        }
        action_fail(selected);
        return;
    }
    s_armed_index = selected;
    s_state.action_armed_mask |= 1u << selected;
    s_state.action_deadline_tick[selected] = plan->deadline_tick;
    /* Read the owner's accepted sequence; a later mismatch cannot complete us. */
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_deadline_status_t status;
        if (sync_io_switch_realtime_status(&status)) {
            s_state.action_sequence[selected] = status.sequence;
            note_backend(selected, status.state, status.origin_before_tick, status.origin_after_tick);
        }
    } else {
        sync_io_gateway_deadline_status_t status;
        if (sync_io_gateway_realtime_status(&status)) {
            s_state.action_sequence[selected] = status.sequence;
            note_backend(selected, status.state, status.origin_before_tick, status.origin_after_tick);
        }
    }
}

void trigger_t2_runtime_position_window_totals(uint32_t *entered, uint32_t *announced,
                                               uint32_t *frozen, uint32_t *executed,
                                               uint32_t *abandoned)
{
    if (entered) *entered = s_position_window.entered;
    if (announced) *announced = s_position_window.announced;
    if (frozen) *frozen = s_position_window.frozen;
    if (executed) *executed = s_position_window.executed;
    if (abandoned) *abandoned = s_position_window.abandoned;
}

void trigger_t2_runtime_position_window_rejections(uint32_t *mark_rejected,
                                                   uint32_t *alloc_rejected)
{
    if (mark_rejected) *mark_rejected = s_position_window.mark_rejected;
    if (alloc_rejected) *alloc_rejected = s_position_window.alloc_rejected;
}

bool trigger_t2_runtime_configure(const trigger_t2_runtime_config_t *config)
{
    /* Split so a bench readout names the rejected condition (temporary). */
    if (!config) { tdma_seat_mount_store_note_reject(140u); return false; }
    if (!trigger_time_grid_valid(&config->grid)) {
        tdma_seat_mount_store_note_reject(141u); return false;
    }
    if (!trigger_turntable_predictor_config_valid(&config->predictor)) {
        tdma_seat_mount_store_note_reject(142u); return false;
    }
    if (trigger_deployment_validate(&config->deployment, UINT32_MAX) !=
        TRIGGER_DEPLOYMENT_OK) {
        tdma_seat_mount_store_note_reject(143u); return false;
    }
    if (!config->run_id) { tdma_seat_mount_store_note_reject(144u); return false; }
    if (config->source_slot >= TRIGGER_POSITION_DESCRIPTOR_NODE_COUNT) {
        tdma_seat_mount_store_note_reject(145u); return false;
    }
    if (!config->target_mask) { tdma_seat_mount_store_note_reject(146u); return false; }
    /* An origin that lists its own source slot in the freeze target mask would
     * wait for a report it can never receive (the ack path only fills slots from
     * received images): the run is accepted, the own bit is ignored, and the
     * misconfiguration is recorded instead of turning into a resend storm. */
    if (config->origin && (config->target_mask & (1u << config->source_slot)))
        tdma_seat_mount_store_note_reject(149u);
    if (!config->descriptor_window_sync) {
        tdma_seat_mount_store_note_reject(147u); return false;
    }
    if (trigger_action_template_table_validate(
            &config->templates, &config->deployment, config->template_crc) !=
        TRIGGER_ACTION_TEMPLATE_TABLE_OK) {
        tdma_seat_mount_store_note_reject(148u); return false;
    }
    if (s_state.active) return false;
    uint32_t local_count = 0u;
    trigger_action_template_t local = {0};
    if (config->local_role >= TRIGGER_DEPLOYMENT_ROLE_COUNT) return false;
    for (uint32_t i = 0u; i < config->templates.count; ++i)
        if (config->templates.row[i].role == config->local_role) {
            local = config->templates.row[i];
            if (++local_count > TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY) return false;
        }
    publish_begin();
    s_config.grid = config->grid;
    s_config.predictor = config->predictor;
    s_config.run_id = config->run_id;
    s_config.source_slot = config->source_slot;
    s_config.target_mask = config->target_mask;
    s_config.descriptor_window_sync = config->descriptor_window_sync;
    s_config.origin = config->origin;
    s_config.deployment_generation = config->deployment.generation;
    memset(&s_state, 0, sizeof(s_state));
    s_local_template = local;
    s_local_template_count = local_count;
    s_deployment_crc = trigger_deployment_crc(&config->deployment);
    trigger_position_window_reset(&s_position_window);
    s_state.configured = 1u;
    s_state.origin = config->origin ? 1u : 0u;
    s_state.source_slot = config->source_slot;
    s_state.target_mask = config->target_mask;
    s_state.run_id = config->run_id;
    s_state.last_error = TRIGGER_T2_RUNTIME_OK;
    publish_end();
    return true;
}

void trigger_t2_runtime_scan_diagnostics(uint32_t *step_mdeg, uint32_t *branch_entered,
                                         uint32_t *scan_result, uint32_t *window_step_mdeg,
                                         uint32_t *positions, uint32_t *step_pulses,
                                         uint32_t *window_entered, uint32_t *window_announced,
                                         uint32_t *window_overwrote, uint32_t *announce_mismatch,
                                         int32_t *motion_start_mdeg,
                                         int32_t *motion_stop_mdeg,
                                         uint32_t *motion_pulses,
                                         uint32_t *pre_roll_pulses,
                                         uint32_t *post_roll_pulses)
{
    if (step_mdeg) *step_mdeg = (uint32_t)s_scan_step_mdeg;
    if (branch_entered) *branch_entered = s_window_branch_entered;
    if (scan_result) *scan_result = s_state.predictor.scan_result;
    if (window_step_mdeg) *window_step_mdeg = (uint32_t)s_state.predictor.window_step_mdeg;
    if (positions) *positions = s_state.predictor.scan_geometry.positions;
    if (step_pulses) *step_pulses = (uint32_t)s_state.predictor.scan_geometry.step_pulses;
    /* The window's own health: every entered position announced exactly once, and no
     * unannounced entry replaced (TRIGGER_T2_TODO.md A.3/A.3.7). */
    if (window_entered) *window_entered = s_state.predictor.window_entered_count;
    if (window_announced) *window_announced = s_state.predictor.window_announced_count;
    if (window_overwrote) *window_overwrote = s_state.predictor.window_overwrote_unannounced_count;
    if (announce_mismatch) *announce_mismatch = s_announce_mismatch_count;
    /* Motion geometry of the configured scan (A.3.46): travel is
     * [start_angle - pre_roll, terminal_angle + post_roll], which a caller verifies per
     * angle/step/speed setting ("predict position 0, so start 5 degrees early and stop 5
     * degrees late"). The start may be negative, hence the signed parameters. */
    if (motion_start_mdeg) *motion_start_mdeg = s_state.predictor.scan_geometry.motion_start_mdeg;
    if (motion_stop_mdeg) *motion_stop_mdeg = s_state.predictor.scan_geometry.motion_stop_mdeg;
    if (motion_pulses) *motion_pulses = (uint32_t)s_state.predictor.scan_geometry.motion_pulses;
    if (pre_roll_pulses) *pre_roll_pulses =
        (uint32_t)s_state.predictor.scan_geometry.pre_roll_pulses;
    if (post_roll_pulses) *post_roll_pulses =
        (uint32_t)s_state.predictor.scan_geometry.post_roll_pulses;
}

bool trigger_t2_runtime_start(uint32_t count_epoch, uint64_t origin_count)
{
    if (!s_state.configured || s_state.active || !count_epoch) return false;
    /* START runs inside STOP maintenance, with the RX producer parked. The
     * transport keeps one PREVIEW and one latched FREEZE per position; consume
     * a previous run's untagged records before establishing local mirrors. */
    trigger_position_descriptor_t discarded;
    for (uint32_t i = 0u; i <= TRIGGER_POSITION_FREEZE_LATCH_IDS &&
         distributed_refmem_get_position_descriptor(s_config.source_slot, &discarded); ++i) {}
    publish_begin();
    memset(&s_state, 0, sizeof(s_state));
    s_state.configured = 1u;
    s_state.origin = s_config.origin ? 1u : 0u;
    s_state.source_slot = s_config.source_slot;
    s_state.target_mask = s_config.target_mask;
    s_state.run_id = s_config.run_id;
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++i)
        s_state.action_origin_width_ticks[i] = UINT32_MAX;
    /* NOTE: do NOT configure the window here. trigger_turntable_predictor_configure()
     * below assigns the whole snapshot (.configured/.last_error), which wipes a step set
     * before it - measured on the bench as "geometry 0pos/0pul with no step visible" while
     * the window looked enabled. window_begin_scan() configures the step itself, after
     * that reset. */
    const bool started =
        trigger_time_cursor_configure(&s_config.grid) &&
        trigger_turntable_predictor_configure(
            &s_state.predictor, &s_config.predictor) &&
        trigger_turntable_predictor_begin(
            &s_state.predictor, &s_config.predictor, count_epoch,
            origin_count, s_scan_step_mdeg);
    if (!started) {
        publish_end();
        return false;
    }
    /* The rolling window runs BESIDE the legacy prediction path while the consumers are
     * migrated (docs/trigger/t2/TRIGGER_T2_TODO.md A.3): begin() above still fills
     * target_count[]/announced_mask, so this changes no output yet - it makes the
     * window's own entries, its health counters and the derived scan geometry readable
     * through T2?, which is the evidence the migration is judged on. A scan the window
     * refuses leaves the window disabled and the run proceeds on the legacy path. */
    if (s_scan_step_mdeg) {
        s_window_branch_entered++;
        (void)trigger_turntable_window_begin_scan(&s_state.predictor,
            &s_config.predictor, s_scan_step_mdeg, 0u, origin_count);
    }
    s_state.active = 1u;
    s_pending_mask = 0u;
    memset(s_freeze_cycles, 0, sizeof(s_freeze_cycles));
    memset(s_origin_probe_max, 0, sizeof(s_origin_probe_max));
    memset(s_deadline, 0, sizeof(s_deadline));
    memset(s_deadline_mailbox, 0, sizeof(s_deadline_mailbox));
    memset(s_deadline_seen, 0, sizeof(s_deadline_seen));
    __atomic_store_n(&s_frozen_public, 0u, __ATOMIC_RELEASE);
    distributed_refmem_note_position_frozen(s_config.run_id, !s_config.origin, 0u);
    s_freeze_last_slice = 0u;
    s_freeze_resend_next = 0u;
    __atomic_store_n(&s_threshold_public, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_version_exhausted, 0u, __ATOMIC_RELEASE);
    s_core0_prepare_calls = s_core0_preview_saved = s_core0_prepare_last_reason = 0u;
    s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
    s_count_head = s_count_size = s_count_last_seq = 0u;
    memset(s_count_history, 0, sizeof(s_count_history));
    memset(s_last_calculated_coarse, 0, sizeof(s_last_calculated_coarse));
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_ACQ_REL);
    memset(s_preview_history, 0, sizeof(s_preview_history));
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_RELEASE);
    (void)__atomic_add_fetch(&s_session, 1u, __ATOMIC_RELEASE);
    s_state.last_error = TRIGGER_T2_RUNTIME_OK;
    publish_end();
    return true;
}

void trigger_t2_runtime_stop(void)
{
    (void)__atomic_add_fetch(&s_session, 1u, __ATOMIC_ACQ_REL);
    publish_begin();
    s_state.active = 0u;
    s_pending_mask = 0u;
    /* STOP owner is serialized after the realtime plane has parked. Drop all
     * unsubmitted intentions and cancel the preloaded backend countdown. */
    stop_backend_core0();
    s_state.action_failed_mask |= s_state.frozen_mask & ~s_state.action_completed_mask;
    s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
    distributed_refmem_request_position_cancel();
    distributed_refmem_note_position_frozen(0u, false, 0u);
    trigger_time_cursor_clear();
    publish_end();
}

/* Called up to three times per slice; GCC outlines it, so pin it to SRAM. */
static T2_RAM bool deadlines_need_service(void)
{
    return s_local_template_count &&
        (s_armed_index < TRIGGER_T2_RUNTIME_POSITION_COUNT ||
         (s_state.frozen_mask & ~(s_state.action_failed_mask | s_state.action_completed_mask)) ||
         /* Keep polling a prior gateway transaction for a late READY even
          * after its independent output receipt has completed. */
         (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_GATEWAY && s_state.action_armed_mask));
}

/* NO1 origin path runs every coarse slice; from XIP it overran SYNC_TRIGGER. */
static T2_BOUNDED bool T2_HOT(service_origin)(void)
{
    uint32_t mark = t2_now();
    uint32_t count;
    if (trigger_counter_read_core1(&count) != COUNTER_OK) {
        s_state.last_error = TRIGGER_T2_RUNTIME_COUNTER;
        return false;
    }
    s_state.last_counter_count = count;
    origin_probe_end(TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNTER, &mark);
    uint32_t mask = 0u;
    if (trigger_turntable_predictor_observe(&s_state.predictor,
            &s_config.predictor, count, &mask) != TRIGGER_TURNTABLE_OK) {
        s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
        return false;
    }
    s_pending_mask |= mask;
    origin_probe_end(TRIGGER_T2_RUNTIME_ORIGIN_PROBE_OBSERVE, &mark);
    /* Feed the rolling window in parallel (migration step 1, TRIGGER_T2_TODO.md A.3.0 /
     * A.3.8): the window announces on its own thresholds and rolls, while the legacy mask
     * above still drives every consumer - so this changes no output yet and makes the
     * window's health facts (announced == entered, overwrote == 0) meaningful and
     * readable through TRIGger:REServation:T2:SCAN?. */
    if (s_scan_step_mdeg) {
        uint32_t window_mask = 0u;
        (void)trigger_turntable_window_advance(&s_state.predictor,
                                               &s_config.predictor, count, &window_mask);
        /* Behaviour-neutral equivalence counter: would a consumer that read the window
         * announce exactly what the legacy mask announced? Compared per scan ordinal,
         * which is only the legacy bit index while the scan's positions coincide with the
         * three legacy slots (true for the current bench scan). */
        for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot) {
            const trigger_turntable_window_entry_t *entry = &s_state.predictor.window[slot];
            if (!entry->valid) continue;
            const uint32_t ordinal = entry->absolute_index;
            const bool legacy_announced =
                ordinal < TRIGGER_T2_RUNTIME_POSITION_COUNT &&
                (s_state.predictor.announced_mask & (1u << ordinal)) != 0u;
            if (legacy_announced != entry->announced) s_announce_mismatch_count++;
        }
    }
    __atomic_store_n(&s_threshold_public, t2_published_mask(),
                     __ATOMIC_RELEASE);
    origin_probe_end(TRIGGER_T2_RUNTIME_ORIGIN_PROBE_WINDOW, &mark);
    const bool frozen = stage_one_freeze();
    origin_probe_end(TRIGGER_T2_RUNTIME_ORIGIN_PROBE_FREEZE, &mark);
    return frozen;
}

T2_BOUNDED trigger_t2_runtime_result_t T2_HOT(trigger_t2_runtime_service)(void)
{
    if (!s_state.configured) return TRIGGER_T2_RUNTIME_NOT_CONFIGURED;
    if (!s_state.active) return TRIGGER_T2_RUNTIME_NOT_ACTIVE;
    publish_begin();
    uint32_t mark = t2_now(), segment = TRIGGER_T2_RUNTIME_SEGMENT_CURSOR;
#define T2_RETURN(value) do { segment_end(segment, &mark); publish_end(); return (value); } while (0)
    trigger_time_cursor_service();
    /* Core1 is the sole RUN writer and publication is guarded: read directly
     * into its snapshot instead of copying the complete cursor twice. */
    trigger_time_cursor_snapshot_t *const cursor = &s_state.cursor;
    if (!trigger_time_cursor_snapshot(cursor)) {
        s_state.cursor.valid = 0u;
        if (deadlines_need_service()) service_deadlines(false);
        s_state.last_error = TRIGGER_T2_RUNTIME_CURSOR;
        T2_RETURN(s_state.last_error);
    }
    if (!cursor->valid || !cursor->pulse_index) {
        cursor->valid = 0u;
        if (deadlines_need_service()) service_deadlines(false);
        s_state.last_error = TRIGGER_T2_RUNTIME_CURSOR;
        T2_RETURN(s_state.last_error);
    }
    segment_end(segment, &mark);
    segment = TRIGGER_T2_RUNTIME_SEGMENT_RECEIVE;
    s_state.last_error = TRIGGER_T2_RUNTIME_OK;
    const bool new_coarse = cursor->sample_seq != s_state.last_sync_sequence;
    s_state.last_sync_sequence = cursor->sample_seq;

    if (s_state.origin && new_coarse) {
        if (!service_origin()) T2_RETURN(s_state.last_error);
    } else if (!s_state.origin) {
        deadline_receive();
        /* The validated cursor already supplies coarse ordinal + 1. */
        (void)accept_remote_position(cursor->pulse_index - 1u);
    }
    segment_end(segment, &mark);
    segment = TRIGGER_T2_RUNTIME_SEGMENT_DEADLINES;
    if (deadlines_need_service()) service_deadlines(true);
    const trigger_t2_runtime_result_t completed=s_state.last_error;
    if (completed != TRIGGER_T2_RUNTIME_OK) T2_RETURN(completed);
    T2_RETURN(TRIGGER_T2_RUNTIME_OK);
#undef T2_RETURN
}

bool trigger_t2_runtime_freeze_timing(
    uint32_t out[TRIGGER_T2_RUNTIME_FREEZE_TIMING_COUNT])
{
    if (!out) return false;
    /* Diagnostic maxima: single Core1 writer, word-sized, no tearing. */
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_FREEZE_TIMING_COUNT; ++i)
        out[i] = __atomic_load_n(&s_freeze_cycles[i], __ATOMIC_RELAXED);
    return true;
}

bool trigger_t2_runtime_origin_probe(
    uint32_t out[TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNT])
{
    if (!out) return false;
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNT; ++i)
        out[i] = __atomic_load_n(&s_origin_probe_max[i], __ATOMIC_RELAXED);
    return true;
}

bool trigger_t2_runtime_take_event(trigger_action_runtime_event_t *out)
{
    if (!out || !s_state.event_count) return false;
    publish_begin();
    *out = s_state.events[0];
    if (s_state.event_count > 1u) {
        memmove(&s_state.events[0], &s_state.events[1],
                (s_state.event_count - 1u) * sizeof(s_state.events[0]));
        memmove(&s_state.event_dispatched[0], &s_state.event_dispatched[1],
                s_state.event_count - 1u);
    }
    s_state.event_dispatched[s_state.event_count-1u]=0u;
    s_state.event_count--;
    publish_end();
    return true;
}

bool trigger_t2_runtime_snapshot(trigger_t2_runtime_snapshot_t *out)
{
    if (!out) return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t before = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        *out = s_state;
        const uint32_t after = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before == after) {
            out->core0_prepare_calls=s_core0_prepare_calls;
            out->core0_preview_saved=s_core0_preview_saved;
            out->core0_prepare_last_reason=s_core0_prepare_last_reason;
            out->prediction_version_exhausted_mask =
                __atomic_load_n(&s_version_exhausted, __ATOMIC_ACQUIRE);
            return true;
        }
    }
    return false;
}

bool trigger_t2_runtime_preview_history(uint32_t position_id, uint32_t age,
                                        trigger_position_descriptor_t *out)
{
    return position_id >= 1u &&
        preview_copy(position_id - 1u, age, out);
}

/* K-2 predicates, split out so a host harness can drive them directly
 * (same extraction technique as tests/python/test_trigger_t2_freeze_abandon.py). */
static bool t2_deadline_position_accepted(uint32_t accepted_mask, uint32_t index)
{
    return (accepted_mask & (1u << index)) != 0u;
}

static bool t2_deadline_plan_current(uint32_t previous_version, uint32_t previous_token,
                                     uint32_t descriptor_version, uint32_t model_token)
{
    return previous_version == descriptor_version && previous_token == model_token;
}

static void prepare_deadlines_core0(uint32_t session,
    const trigger_t2_runtime_snapshot_t *snapshot)
{
    if (!s_local_template_count) return;
    vdc_dpll_manager_committed_model_t model;
    if (!vdc_dpll_manager_get_committed_model(&model) ||
        model.session != snapshot->cursor.session || !model.token || !model.dco.valid ||
        model.dco.nominal_period_ns != s_config.grid.period_ns) return;
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        /* K-2: FREEZE freezes position prediction only, not the DCO. Skip a position only
         * once its deadline has been accepted by the hardware owner (armed/completed); a
         * frozen-but-not-yet-loaded position must still be re-solved by the latest DCO.
         * Core0 reads the snapshot, never Core1's runtime state. */
        if (t2_deadline_position_accepted(
                snapshot->action_armed_mask | snapshot->action_completed_mask, index))
            continue;
        trigger_position_descriptor_t descriptor;
        if (!preview_copy(index, 0u, &descriptor)) continue;
        /* Mailbox has one RUN writer: Core0 can inspect its own old words. */
        t2_deadline_plan_t previous;
        memcpy(&previous, s_deadline_mailbox[index].words, sizeof(previous));
        if (t2_deadline_plan_current(previous.prediction_version, previous.model_token,
                                     descriptor.prediction_version, model.token)) continue;
        trigger_action_template_instance_t instance;
        if (!trigger_action_template_expand(&s_local_template, &descriptor,
                s_config.grid.subdivisions, &instance)) continue;
        const uint64_t q = instance.position.action_q;
        const uint64_t coarse = q / s_config.grid.subdivisions;
        const uint32_t fine = (uint32_t)(q % s_config.grid.subdivisions);
        if (q <= snapshot->cursor.action_cursor ||
            coarse > (UINT64_MAX - s_config.grid.anchor_vdc_ns) / s_config.grid.period_ns)
            continue;
        uint64_t output_ns = s_config.grid.anchor_vdc_ns + coarse * s_config.grid.period_ns;
        /* Round toward the first representable ns/tick at or after the slice. */
        const uint64_t fraction = (uint64_t)fine * s_config.grid.period_ns;
        const uint64_t fine_ns = fraction / s_config.grid.subdivisions +
            (fraction % s_config.grid.subdivisions != 0u);
        if (output_ns > UINT64_MAX - fine_ns) continue;
        output_ns += fine_ns;
        uint64_t local_ns, raw_tick;
        if (!vdc_domain_dco_output_to_local_ns(&model.dco, output_ns, &local_ns) ||
            !vdc_timer1_ns_to_ticks(BOARD_SYS_CLOCK_HZ, local_ns, true, &raw_tick) ||
            raw_tick < model.valid_from_raw) continue;
        const t2_deadline_plan_t plan = {
            .action_q = q, .deadline_tick = raw_tick,
            .session = model.session, .ring_config_seq = snapshot->cursor.ring_config_seq,
            .prediction_version = descriptor.prediction_version,
            .expiry_sync = descriptor.expiry_sync, .model_token = model.token};
        if (session != __atomic_load_n(&s_session, __ATOMIC_ACQUIRE)) return;
        deadline_publish(index, &plan);
    }
}

void trigger_t2_runtime_prepare_core0(void)
{
    if (s_core0_prepare_calls != UINT32_MAX) ++s_core0_prepare_calls;
    const uint32_t session = __atomic_load_n(&s_session, __ATOMIC_ACQUIRE);
    trigger_t2_runtime_snapshot_t snapshot;
    if (!trigger_t2_runtime_snapshot(&snapshot) || !snapshot.active ||
        !snapshot.cursor.valid) { s_core0_prepare_last_reason=1u; return; }
    if (!snapshot.origin) {
        prepare_deadlines_core0(session, &snapshot);
        return;
    }
    if (snapshot.last_sync_sequence == s_count_last_seq ||
        !snapshot.last_sync_sequence) { s_core0_prepare_last_reason=2u; return; }

    const uint64_t coarse =
        snapshot.cursor.action_cursor / s_config.grid.subdivisions;
    if (coarse > UINT32_MAX || snapshot.last_counter_count > UINT32_MAX) { s_core0_prepare_last_reason=3u; return; }
    s_count_last_seq = snapshot.last_sync_sequence;
    s_count_history[s_count_head] = (t2_count_sample_t){
        .count = (uint32_t)snapshot.last_counter_count, .coarse = (uint32_t)coarse};
    s_count_head = (s_count_head + 1u) % T2_WINDOW_HISTORY_CAPACITY;
    if (s_count_size < T2_WINDOW_HISTORY_CAPACITY) s_count_size++;
    if (distributed_refmem_position_descriptor_pending()) { s_core0_prepare_last_reason=4u; return; }

    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT;
         ++index) {
        const uint32_t bit = 1u << index;
        /* Only the threshold/target VALUES come from the rolling window here; the skip
         * condition keeps the legacy announced_mask on purpose (A.3.16b): window_advance()
         * runs EARLIER in the same origin service cycle, so entry->announced is already set
         * by the time this loop reads the snapshot - using it as a skip condition made
         * every preparation skip and starved the whole path (preview_saved == 0). */
        const trigger_turntable_window_entry_t *entry =
            s_scan_step_mdeg
                ? trigger_turntable_window_find(&snapshot.predictor, index) : NULL;
        const uint64_t window_threshold = entry ? entry->threshold_count
                                                : snapshot.predictor.threshold_count[index];
        const uint64_t window_target = entry ? entry->target_count
                                             : snapshot.predictor.target_count[index];
        if (__atomic_load_n(&s_threshold_public, __ATOMIC_ACQUIRE) & bit ||
            snapshot.predictor.announced_mask & bit ||
            snapshot.last_counter_count >= window_threshold ||
            snapshot.last_counter_count >= window_target)
            continue;

        const uint64_t to_threshold = window_threshold - snapshot.last_counter_count;
        const uint64_t span = window_target - snapshot.predictor.origin_count;
        const uint32_t window = to_threshold <= span / 20u ? 4u :
                                to_threshold <= span / 5u ? 8u : 16u;
        const uint32_t interval = window == 4u ? 1u :
                                  window == 8u ? 2u : 8u;
        if (s_last_calculated_coarse[index] &&
            coarse - s_last_calculated_coarse[index] < interval) continue;
        s_last_calculated_coarse[index] = (uint32_t)coarse;

        t2_count_sample_t old = {0};
        bool found = false;
        for (uint32_t age = 1u; age < s_count_size; ++age) {
            const uint32_t slot = (s_count_head + T2_WINDOW_HISTORY_CAPACITY -
                                   1u - age) % T2_WINDOW_HISTORY_CAPACITY;
            old = s_count_history[slot];
            if (old.coarse < coarse && coarse - old.coarse >= window) {
                found = true;
                break;
            }
        }
        if (!found || old.count >= snapshot.last_counter_count ||
            snapshot.last_counter_count - old.count > UINT32_MAX ||
            coarse - old.coarse > UINT32_MAX) { s_core0_prepare_last_reason=5u; continue; }

        trigger_position_descriptor_t previous = {0};
        (void)preview_copy(index, 0u, &previous);
        trigger_position_descriptor_t next = {
            .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
            .run_id = s_config.run_id,
            .sync_epoch = snapshot.cursor.grid_epoch,
            .position_id = index + 1u,
            .count_epoch = snapshot.predictor.count_epoch,
            /* Same source as the judgement above (migration item 3, A.3.19/A.3.33): the
             * window entry's target when this position is in the window, the legacy array
             * otherwise. The rest of this literal is runtime-specific (observed_count /
             * window_pulses / window_syncs, and a prediction_version that increments per
             * attempt) - which is why the descriptor is built here and NOT with
             * trigger_turntable_predictor_build_descriptor_window(). */
            .target_count = window_target,
            .prediction_version = previous.prediction_version + 1u,
            .grid_generation = s_config.grid.generation,
            .deployment_generation = s_config.deployment_generation,
            .deployment_crc = s_deployment_crc,
            .valid_from_sync = (uint32_t)coarse,
            .expiry_sync = UINT32_MAX,
            .stage = TRIGGER_POSITION_STAGE_PREVIEW,
            .observed_count = snapshot.last_counter_count,
            .window_pulses = (uint32_t)(snapshot.last_counter_count - old.count),
            .window_syncs = (uint32_t)(coarse - old.coarse),
        };
        uint64_t target_q;
        if (!trigger_position_descriptor_solve_q(
                &next, s_config.grid.subdivisions, &target_q)) { s_core0_prepare_last_reason=6u; continue; }
        next.target_ms_index = target_q / s_config.grid.subdivisions;
        next.target_fine_slice_index =
            (uint32_t)(target_q % s_config.grid.subdivisions);
        if (next.target_ms_index < coarse ||
            next.target_ms_index > UINT32_MAX - s_config.descriptor_window_sync) {
            s_core0_prepare_last_reason=7u; continue;
        }
        next.expiry_sync = (uint32_t)next.target_ms_index +
                           s_config.descriptor_window_sync;
        if (previous.prediction_version &&
            previous.target_ms_index == next.target_ms_index &&
            previous.target_fine_slice_index == next.target_fine_slice_index)
            continue;
        if (previous.prediction_version >= TRIGGER_T2_PREDICTION_VERSION_MAX) {
            /* Preserve the last valid local version for FREEZE fallback.
             * A new run is needed before the compact revision can restart. */
            __atomic_fetch_or(&s_version_exhausted, bit, __ATOMIC_RELEASE);
            s_core0_prepare_last_reason = 10u;
            continue;
        }
        next.descriptor_crc32 = trigger_position_descriptor_crc(&next);
        if (session != __atomic_load_n(&s_session, __ATOMIC_ACQUIRE)) return;
        if (__atomic_load_n(&s_threshold_public, __ATOMIC_ACQUIRE) & bit)
            continue;
        if (distributed_refmem_stage_position_descriptor(
                &next, s_config.target_mask)) {
            preview_save(index, &next); ++s_core0_preview_saved; s_core0_prepare_last_reason=8u;
        } else s_core0_prepare_last_reason=9u;
        return;
    }
}
