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
/* Announce gates, installed at STOP instead of being frozen at build time: the right value
 * depends on the measured single-depth seat turnaround of the running ring, and sweeping it
 * used to cost a re-flash. Defaults are the measured values (4/10/6 ms); a 0 is refused so
 * shaping cannot be switched off by an empty write. Read on every announce evaluation. */
static struct {
    uint32_t tolerance_ms, min_interval_ms, global_interval_ms;
} s_announce = {
    TRIGGER_T2_ANNOUNCE_TARGET_TOLERANCE_MS,
    TRIGGER_T2_ANNOUNCE_MIN_INTERVAL_MS,
    TRIGGER_T2_ANNOUNCE_MIN_GLOBAL_INTERVAL_MS,
};
/* How many times start() actually took the window branch (A.3.6d). */
static uint32_t s_window_branch_entered;
/* Migration equivalence evidence (A.3.11), retired: the window now drives the FREEZE, so
 * there is no legacy announced set left to compare against. Kept (always 0) because the
 * diagnostics page still reports it and page indexes do not move. */
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
            entry->absolute_index != UINT32_MAX)
            mask |= 1u << (entry->absolute_index % TRIGGER_T2_RUNTIME_POSITION_COUNT);
    }
    return mask;
}

bool trigger_t2_runtime_announce(uint32_t tolerance_ms, uint32_t min_interval_ms,
                                 uint32_t global_interval_ms)
{
    /* 0 now means "this gate does not suppress": the producer writes the newest value on
     * every pass and the traffic is decided by T_drain alone, which is the shape the
     * architecture asks for (buffer updates immediately, transmit on a fixed period).
     * Values above the bounds are still refused, and a refused write leaves the
     * previously installed gates untouched. */
    if (tolerance_ms > TRIGGER_T2_ANNOUNCE_MAX_TOLERANCE_MS) return false;
    if (min_interval_ms > TRIGGER_T2_ANNOUNCE_MAX_INTERVAL_MS) return false;
    if (global_interval_ms > TRIGGER_T2_ANNOUNCE_MAX_INTERVAL_MS) return false;
    s_announce.tolerance_ms = tolerance_ms;
    s_announce.min_interval_ms = min_interval_ms;
    s_announce.global_interval_ms = global_interval_ms;
    return true;
}

bool trigger_t2_runtime_announce_get(uint32_t *tolerance_ms, uint32_t *min_interval_ms,
                                     uint32_t *global_interval_ms)
{
    if (tolerance_ms) *tolerance_ms = s_announce.tolerance_ms;
    if (min_interval_ms) *min_interval_ms = s_announce.min_interval_ms;
    if (global_interval_ms) *global_interval_ms = s_announce.global_interval_ms;
    /* An all-zero install is legal (no suppression), so the readback never fails. */
    return true;
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
/* Static templates never change in RUN. The board keeps every row of its own
 * role (T2-SEQ-001): row g is group g of that role's per-position sequence, so
 * the sequence is expanded group by group instead of being flattened into one
 * action. All rows of a role share its kind (the table validator ties SWITCH to
 * SWITCH_A/SWITCH_B and GATEWAY to GATEWAY), so the backend dispatch stays a
 * single role-level value. */
/* One group of the role's sequence, compacted to 8 B: the full
 * trigger_action_template_t row is 32 B and eight of them did not fit the RAM the
 * single-template runtime used (linker: region RAM overflowed by 376 bytes).
 * template_id is kept for the receipt, argument is the channel code or pulse
 * width and delta_q the static fine-slice offset; span_slices is not needed per
 * row because the position's occupied span is the widest row (s_local_span_max). */
typedef struct {
    uint16_t template_id, argument;
    int32_t delta_q;
} t2_local_group_t;
static t2_local_group_t s_local_group[TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY];
static uint32_t s_local_template_count, s_deployment_crc;
static uint32_t s_local_kind, s_local_role, s_local_span_max;
/* Group cursors: next group Core0 prepares, and the group Core1 has in flight
 * (TRIGGER_T2_RUNTIME_GROUP_NONE when none). One group per slot is in flight at
 * a time; the two-group sliding window is the SYNC_IO side of T2-EXEC-002. */
static uint8_t s_group_next[TRIGGER_T2_RUNTIME_POSITION_COUNT];
/* End of the window already handed to the executor (absolute group index, 0 = none). While a
 * window is open it is the authority on how many groups this position has. */
static uint8_t s_group_handed[TRIGGER_T2_RUNTIME_POSITION_COUNT];
/* Last backend sequence the window may report; the backend advances it per group. */
static uint32_t s_action_sequence_last[TRIGGER_T2_RUNTIME_POSITION_COUNT];
/* In-window accounting: a completion is only real when the backend's sequence advances by exactly
 * one and the backend reports a receipt. Counted so the bench shows which rule is violated. */
static uint32_t s_window_advance_count, s_window_jump_count, s_window_reuse_count,
    s_window_no_receipt_count;
static uint8_t s_group_armed[TRIGGER_T2_RUNTIME_POSITION_COUNT];
/* How many groups of one position are handed to the executor in a single call. */
#define T2_PLAN_GROUP_MAX 4u
typedef struct {
    uint64_t action_q, deadline_tick;
    /* Absolute local ticks of the following groups of this same position, solved by Core0 in
     * the same pass and handed to ONE stage_sequence() call. group_count <= 1 keeps the
     * single-group path. */
    uint64_t group_tick[T2_PLAN_GROUP_MAX];
    uint32_t group_argument[T2_PLAN_GROUP_MAX];
    uint32_t group_count;
    /* Tick of the NEXT group of this position, solved by Core0 together with this
     * one. Core1 promotes it the moment the current group completes and arms it in
     * the same pass: measured on the bench 2026-10-16, planning a group only after
     * the previous one completed made the arm too late even at 60 ms spacing.
     * Zero means "no further group".
     * NOTE (2026-10-16, architecture): the per-group sequence belongs to the ACTION
     * side, not to distribution - T2 hands one time slice per position and the
     * executor walks the groups by the installed convention. This field is the
     * interim T2-side window until the executor owns the sequence; see
     * TRIGGER_T2_ARCHITECTURE.md「动作解耦」and SYNC-SCHED-001. */
    uint64_t next_tick;
    uint32_t session, ring_config_seq, prediction_version, expiry_sync;
    uint32_t model_token;
    /* Which group of the role's sequence this plan executes. The next group of
     * the same position shares descriptor version and model token, so without
     * this the Core0 currency check would treat it as already published. */
    uint32_t group;
} t2_deadline_plan_t;
/* Tick deltas of the whole role sequence, relative to the plan's base_tick. */
#define T2_SCHEDULE_WORDS TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY
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
/* Positions the second-position cache rescued (T2-PROVISIONAL-001): each increment is one
 * position that would otherwise have been lost because its FREEZE never arrived. Read
 * through the accessor below, so it is always used and can be published later. */
static uint32_t s_position_fallback_promotions;

/* Receive-side timestamp of the record this engine last ADMITTED (see the header):
 * the local tick of the admission, projected from the local action_cursor through the same
 * DCO/TIMER1 conversion every deadline uses, and the observation sequence that produced
 * that cursor. Recorded once per record, not per position. */
static uint64_t s_rx_local_tick;
static uint32_t s_rx_local_sample_seq, s_rx_timestamp_count, s_rx_stale_count;

uint32_t trigger_t2_runtime_fallback_promoted_count(void)
{
    return s_position_fallback_promotions;
}
/* Core1 publishes the threshold before attempting the FREEZE handoff; Core0
 * abandons in-progress predictions for that position. Transport additionally
 * serializes PREVIEW/FREEZE acceptance to close the final check/stage race. */
static uint32_t s_threshold_public;
static uint32_t s_version_exhausted;
static uint32_t s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
static uint32_t s_pending_mask;
/* A scan has far more positions than the runtime keeps state for: scan ordinal k takes slot
 * k % POSITION_COUNT and goes on the wire as position_id = slot + 1, so the wire and every
 * per-slot array stay as they are. A slot is reused only after its previous occupant is
 * finished, and prediction versions keep rising per slot (never reset), which is what tells
 * a stale PREVIEW/FREEZE of a finished occupant from the next one.
 *  - s_slot_target:   origin, target count of the ordinal that awaits its FREEZE; the saved
 *                     PREVIEW must carry it, otherwise it belongs to an earlier occupant.
 *  - s_slot_floor:    follower, highest version of a finished occupant; anything at or
 *                     below it is a leftover and is ignored.
 *  - s_prepare_ordinal / s_prepare_next: Core0, last ordinal prepared per slot and the
 *                     round-robin start so the two window entries share the one stage a call. */
static uint64_t s_slot_target[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static uint32_t s_slot_floor[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static uint32_t s_prepare_ordinal[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static uint32_t s_prepare_next;
/* Origin: scan ordinal + 1 last announced per slot. Core0 checks it (not the window, which
 * already rolled the announced entry out) before staging a PREVIEW for that position. */
static volatile uint32_t s_announced_ordinal[TRIGGER_T2_RUNTIME_POSITION_COUNT];
/* Follower: target_ms of the occupant a slot last handed on (0 = none), so a FREEZE can be
 * told apart from the PREVIEW now held: it belongs to whichever target it is closer to. */
static uint64_t s_slot_prev_target[TRIGGER_T2_RUNTIME_POSITION_COUNT];
/* Follower (R3): the slot's NEXT occupant, announced while the current one is still in
 * flight. The origin stages a PREVIEW only while a position is pending and never re-stages
 * it after its FREEZE, so a PREVIEW dropped here is lost for good: the successor's FREEZE
 * resends are refused by the finished-not-handed-on branch until a PREVIEW recycles the
 * slot, and no such PREVIEW follows. One stashed successor per slot, s_stash_valid is a
 * bit mask over slots. */
static trigger_position_descriptor_t s_stash[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static uint32_t s_stash_valid;
_Static_assert(TRIGGER_T2_RUNTIME_POSITION_COUNT == 3u,
    "trigger_position_descriptor_valid() bounds a FREEZE position_id by a literal 3");
static uint32_t s_core0_prepare_calls, s_core0_preview_saved, s_core0_prepare_last_reason;
/* Where a position can lose its plan before FREEZE sees it: counted so the bench answers
 * "expand failed" / "descriptor had no version" instead of us guessing (rounds 53-57). */
static uint32_t s_solve_no_descriptor, s_solve_expand_fail, s_solve_version_zero, s_solve_publish;
/* The three ways the solver entry skips a position; counted to see which one starves the plan
 * supply (N=3 executes two positions, N=2 executes all 361). */
static uint32_t s_skip_group_unreachable, s_skip_inflight, s_skip_accepted;
/* Plan timing against the freeze: a plan that arrives after the position froze is useless, because
 * the FREEZE path reads the slot and rules NO_PLAN when the local version is still 0. */
static uint32_t s_publish_before_freeze, s_publish_after_freeze;
/* deadline_receive() exits: guard busy, no version, wrong session, older group. */
static uint32_t s_recv_busy, s_recv_no_version, s_recv_session, s_recv_older, s_recv_ok;
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

/* Remaining lead (ms, coarse ordinals) packed for one segmentation point: the low
 * half keeps the run's minimum, the high half the last value. */
static void note_lead_pack(uint32_t *pack, uint32_t lead)
{
    const uint32_t minimum = *pack & 0xFFFFu;
    const uint32_t shown = minimum && minimum < lead ? minimum : lead;
    *pack = shown | (lead << 16u);
}

/* FREEZE starts freezing; it completes when every target reports the position
 * frozen. Until then the origin resends it, spaced so PREVIEWs still board. */
#define T2_FREEZE_RESEND_SLICES 4u
static uint64_t s_freeze_last_slice;
static uint32_t s_freeze_resend_next;
/* Per-position cursor at the moment its FREEZE was staged. Pairing the two stages is what
 * turns the ack readout into a TIME: at ack visibility, now - s_freeze_stage_at[i] is that
 * position's own round trip (the arrival of the refmem image carrying the ack, i.e. the
 * only measurable number for the path that does not use the fast channel). Cleared when
 * consumed and at run start, so a stale value can never fake a measurement.
 * Cost: TRIGGER_T2_RUNTIME_POSITION_COUNT * 4 B (3 * 4 = 12 B), paid for by the waveform
 * buffer reduction 200 -> 100 records. */
static uint32_t s_freeze_stage_at[TRIGGER_T2_RUNTIME_POSITION_COUNT];

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
    const uint32_t acked_now = peers
        ? (open & distributed_refmem_position_freeze_acked(s_config.run_id, peers))
        : open;
    if (acked_now) {
        /* Stage 5 (the only look at the path that does NOT use the fast channel): the
         * moment the follower's frozen state became visible HERE, i.e. the refmem image
         * carrying the position ack has arrived. PAIRED with the cursor recorded when
         * this position's FREEZE was staged, so the packed value is that position's own
         * round trip in pulses (= ms on the 1 ms grid):
         *     ack round trip = now - s_freeze_stage_at[i]
         * (packed min | last<<16). Without the pairing the pack only held a lead, and
         * subtracting two per-run extremes mixed different positions - which is exactly
         * why the first version could report an ack "earlier" than the follower froze. */
        const uint64_t now_ack = s_state.cursor.pulse_index;
        for (uint32_t step = 0u; step < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++step) {
            const uint32_t bit = 1u << step;
            if (!(acked_now & bit)) continue;
            const uint32_t staged = s_freeze_stage_at[step];
            if (staged == 0u) continue;
            const uint32_t rtt = now_ack > (uint64_t)staged
                ? (uint32_t)(now_ack - staged) : 0u;
            note_lead_pack(&s_state.ack_lead_pack, rtt);
            s_freeze_stage_at[step] = 0u; /* one measurement per position */
        }
    }
    s_state.freeze_complete_mask |= acked_now;
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
            .target_ms_index = s_state.descriptors[index].target_ms_index,
        };
        if (!distributed_refmem_stage_position_descriptor(&freeze, s_config.target_mask)) {
            s_state.transport_reject_count++;
            s_state.last_error = TRIGGER_T2_RUNTIME_TRANSPORT;
            return false;
        }
        s_state.freeze_resend_count++;
        s_freeze_last_slice = now;
        s_freeze_resend_next = index + 1u;
        /* Pairing for stage 5: remember WHEN this position's FREEZE went out, so the ack
         * becoming visible later yields this position's own round trip
         * (now_ack - s_freeze_stage_at[index]) instead of a difference of two per-run
         * extremes taken from different positions. */
        s_freeze_stage_at[index] = (uint32_t)now;
        /* Segmentation v1 (origin side): how much lead was left when this FREEZE was
         * actually staged. Subtracting it from declare_lead_ms isolates the
         * announce -> stage segment. */
        {
            const uint32_t target =
                (uint32_t)s_state.descriptors[index].target_ms_index;
            note_lead_pack(&s_state.stage_lead_pack,
                           target > now ? (uint32_t)(target - now) : 0u);
        }
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
        if (!preview_copy(index, 0u, &latest) ||
            (s_scan_step_mdeg && latest.target_count != s_slot_target[index])) {
            /* A scan reuses slots: a saved PREVIEW whose target is not the one awaiting
             * its FREEZE is an earlier occupant's and counts as no PREVIEW at all.
             * T2-FREEZE-002 (评审 A-13): a pending position with no saved PREVIEW
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
            /* The absolute identity of the frozen position rides on the FREEZE frame. */
            .target_ms_index = latest.target_ms_index,
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

/* Origin: scan ordinal k announces its threshold. It takes slot k % POSITION_COUNT, whose
 * previous occupant (k - POSITION_COUNT) crossed its threshold a whole window ago; if that
 * one is still unconfirmed it is abandoned here (counted, never silent) and the slot's
 * bookkeeping starts over for the new position. */
static T2_BOUNDED void T2_HOT(origin_announce)(uint32_t ordinal, uint64_t target_count)
{
    const uint32_t index = ordinal % TRIGGER_T2_RUNTIME_POSITION_COUNT;
    const uint32_t bit = 1u << index;
    if ((s_state.published_mask | s_pending_mask) & bit &
        ~(s_state.freeze_complete_mask | s_state.freeze_abandoned_mask)) {
        if (s_state.freeze_abandoned_count != UINT32_MAX)
            ++s_state.freeze_abandoned_count;
    }
    s_state.published_mask &= ~bit;
    s_state.freeze_complete_mask &= ~bit;
    s_state.freeze_abandoned_mask &= ~bit;
    s_state.freeze_no_preview_mask &= ~bit;
    s_state.frozen_mask &= ~bit;
    s_state.freeze_version[index] = 0u;
    s_state.descriptors[index] = (trigger_position_descriptor_t){0};
    if (trigger_position_window_find(&s_position_window, index + 1u) >= 0) {
        const trigger_position_window_state_t last =
            s_position_window.record[trigger_position_window_find(
                &s_position_window, index + 1u)].state == TRIGGER_POSITION_WINDOW_FROZEN
            ? TRIGGER_POSITION_WINDOW_EXECUTED : TRIGGER_POSITION_WINDOW_ABANDONED;
        (void)trigger_position_window_mark(&s_position_window, index + 1u, last);
        (void)trigger_position_window_release(&s_position_window, index + 1u);
    }
    s_slot_target[index] = target_count;
    /* A: the declared lead is the budget the whole chain has to fit into; the
     * threshold per mille only turns into a time once it is measured here. The
     * TARGET ordinal is the one carried by the position's own descriptor (the same
     * quantity the follower reports as freeze_lead_*), NOT s_prepare_ordinal - that
     * one names when the preview is due and read back as 1 ms. */
    {
        const uint32_t target_ordinal =
            (uint32_t)s_state.descriptors[index].target_ms_index;
        note_lead_pack(&s_state.declare_lead_pack,
                       target_ordinal > ordinal ? target_ordinal - ordinal : 0u);
    }
    __atomic_store_n(&s_announced_ordinal[index], ordinal + 1u, __ATOMIC_RELEASE);
    s_pending_mask |= bit;
}

static void deadline_publish(uint32_t index, const t2_deadline_plan_t *plan)
{
    if (__atomic_load_n(&s_frozen_public, __ATOMIC_ACQUIRE) & (1u << index))
        ++s_publish_after_freeze;
    else
        ++s_publish_before_freeze;
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
static T2_RAM void deadline_receive_slot(uint32_t index)
{
    const bool frozen = (s_state.frozen_mask & (1u << index)) != 0u;
    t2_deadline_mailbox_t *mail = &s_deadline_mailbox[index];
    const uint32_t before = __atomic_load_n(&mail->guard, __ATOMIC_ACQUIRE);
    if ((before & 1u) || before == s_deadline_seen[index]) return;
    t2_deadline_plan_t plan;
    for (size_t i = 0u; i < sizeof(plan) / sizeof(uint32_t); ++i) {
        const uint32_t word = __atomic_load_n(&mail->words[i], __ATOMIC_RELAXED);
        memcpy((uint8_t *)&plan + i * sizeof(word), &word, sizeof(word));
    }
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (before != __atomic_load_n(&mail->guard, __ATOMIC_ACQUIRE)) { ++s_recv_busy; return; }
    s_deadline_seen[index] = before;
    if (!plan.prediction_version) { ++s_recv_no_version; return; }
    if (plan.session != s_state.cursor.session ||
        plan.ring_config_seq != s_state.cursor.ring_config_seq) { ++s_recv_session; return; }
    /* Groups are ordered WITHIN a position: a smaller group number is only "older" when it
     * belongs to the position already in the slot. The next occupant's group 0 carries a new
     * prediction version and must be accepted. */
    const bool same_position = plan.prediction_version == s_deadline[index].prediction_version;
    if (frozen && same_position && plan.group < s_deadline[index].group) {
        ++s_recv_older;
        return;
    }
    ++s_recv_ok;
    if (frozen && same_position && plan.group == s_deadline[index].group) {
        /* This group is already armed or promoted: its own slice and tick are in the hardware
         * owner's hands and must not move, but the tick of the group after it is exactly what
         * the promotion needs. */
        s_deadline[index].next_tick = plan.next_tick;
        return;
    }
    s_deadline[index] = plan;
}

static T2_BOUNDED void T2_HOT(deadline_receive)(void)
{
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index)
        deadline_receive_slot(index);
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

static T2_RAM void action_fail(uint32_t index, uint32_t reason)
{
    s_state.action_failed_mask |= 1u << index;
    /* A failed position owns no group any more: its sequence is over, so Core0
     * must not prepare a later group for it. */
    s_group_armed[index] = TRIGGER_T2_RUNTIME_GROUP_NONE;
    s_state.action_group_in_flight[index] = TRIGGER_T2_RUNTIME_GROUP_NONE;
    if (reason < TRIGGER_T2_ACTION_FAIL_COUNT &&
        s_state.action_fail_reason[reason] != UINT32_MAX)
        ++s_state.action_fail_reason[reason];
    if (trigger_position_window_find(&s_position_window, index + 1u) >= 0) {
        (void)trigger_position_window_mark(&s_position_window, index + 1u,
                                           TRIGGER_POSITION_WINDOW_ABANDONED);
        (void)trigger_position_window_release(&s_position_window, index + 1u);
    }
    s_state.last_error = TRIGGER_T2_RUNTIME_ACTION;
}

/* Highest prediction version this slot's current occupant has used. */
static T2_BOUNDED uint32_t T2_HOT(slot_version_high)(uint32_t index)
{
    uint32_t high = s_state.freeze_version[index];
    if (s_state.freeze_local_version[index] > high) high = s_state.freeze_local_version[index];
    if (s_state.descriptors[index].prediction_version > high)
        high = s_state.descriptors[index].prediction_version;
    return high;
}

/* Follower: hand a finished slot (action completed or failed) to the next scan position.
 * The occupant's evidence stays in the cumulative window totals; the per-slot bits, plan and
 * versions are cleared so the new occupant is judged on its own, and the highest version the
 * old one used becomes the floor below which any late PREVIEW/FREEZE is a leftover. */
static T2_BOUNDED void T2_HOT(recycle_slot)(uint32_t index)
{
    const uint32_t keep = ~(1u << index);
    const uint32_t high = slot_version_high(index);
    if (high > s_slot_floor[index]) s_slot_floor[index] = high;
    if (s_state.descriptors[index].target_ms_index)
        s_slot_prev_target[index] = s_state.descriptors[index].target_ms_index;
    s_state.frozen_mask &= keep;
    s_state.received_mask &= keep;
    s_state.emitted_position_mask &= keep;
    s_state.action_armed_mask &= keep;
    s_state.action_completed_mask &= keep;
    s_state.action_failed_mask &= keep;
    s_state.action_uncertain_mask &= keep;
    s_state.freeze_version[index] = 0u;
    s_state.freeze_local_version[index] = 0u;
    s_state.freeze_local_age[index] = 0u;
    s_state.action_sequence[index] = 0u;
    s_state.action_deadline_tick[index] = 0u;
    s_state.action_backend_state[index] = 0u;
    s_state.action_origin_width_ticks[index] = UINT32_MAX;
    s_state.descriptors[index] = (trigger_position_descriptor_t){0};
    memset(&s_deadline[index], 0, sizeof(s_deadline[index]));
    __atomic_store_n(&s_frozen_public, s_state.frozen_mask, __ATOMIC_RELEASE);
    distributed_refmem_note_position_frozen(s_config.run_id, true, s_state.frozen_mask);
}

/* The acknowledgement names the instances CURRENTLY awaiting confirmation: a position whose
 * action already completed is no longer one. The origin reuses a slot for the next scan
 * position and clears its own confirmation then, so a finished position still reported as
 * frozen would confirm the NEXT position before it was ever frozen (false acknowledgement). */
static T2_BOUNDED void T2_HOT(report_frozen)(void)
{
    distributed_refmem_note_position_frozen(s_config.run_id, true,
        s_state.frozen_mask & ~s_state.action_completed_mask);
}

/* A refused arrival (wire identity, FREEZE validation, PREVIEW admission) is one aggregate
 * fact: "how many records this run refused". Kept in one place so every rejection site
 * reports the same way. */
static void T2_RAM note_descriptor_reject(void)
{
    s_state.descriptor_reject_count++;
    s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
}

/* A position this board never froze - the successor replaced it, or a new PREVIEW replaced
 * an unfrozen one - is a lost position. The mask cannot answer "how often", so the
 * saturating counter is the evidence (R6). */
static void T2_RAM note_position_lost(void)
{
    if (s_state.freeze_abandoned_count != UINT32_MAX)
        ++s_state.freeze_abandoned_count;
}

/* Bind, validate and admit a PREVIEW into its slot. The compact wire carries only the
 * slice, so this run's start-up identity and grid are bound here; there is no local CRC on
 * Core1 (a bitwise CRC overran the SYNC_TRIGGER phase on followers). Shared by the
 * transport path and by the R3 promotion of a stashed successor, so both admit a descriptor
 * under exactly the same identity, window and slot-floor rules. */
static T2_BOUNDED bool T2_HOT(admit_preview)(
    trigger_position_descriptor_t descriptor, uint32_t index, uint64_t current_ms)
{
    const uint32_t bit = 1u << index;
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
    /* Transport owns wire CRC. Validate each new version here once, not the same
     * deployment/table CRC on every realtime polling call. */
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
    /* Window admission, split so a LATE arrival is attributed instead of being folded into
     * the aggregate descriptor reject (which mixes every rejection cause above too). The
     * accept/reject decision is unchanged: an empty window (expiry_sync == 0, target
     * overflow) is still rejected, it is just not counted as lateness because no window was
     * ever promised. valid_from_sync is forced to 0 by this consumer, so only lateness is
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
    /* R6: an occupant that was received but never frozen, replaced by a DIFFERENT position,
     * is a lost position - counted here instead of disappearing silently. A re-solve of the
     * same position (equal target) is not a replacement. */
    if ((s_state.received_mask & bit) && !(s_state.frozen_mask & bit) &&
        s_state.descriptors[index].target_ms_index != descriptor.target_ms_index)
        note_position_lost();
    s_state.descriptors[index] = descriptor;
    preview_save(index, &descriptor);
    s_state.received_mask |= bit;
    (void)trigger_position_window_alloc(&s_position_window, descriptor.position_id,
        (uint32_t)descriptor.target_ms_index, (uint16_t)descriptor.prediction_version, 0u);
    return true;
reject:
    note_descriptor_reject();
    return false;
}

/* R3: keep the slot's next occupant while the current one is still in flight. Only a
 * PREVIEW newer than everything the occupant used AND naming a different position is a
 * successor: a same-target re-solve of the occupant itself is not, and must not be promoted
 * later as a second action for the same position. Replacing a successor that was never
 * frozen counts the loss (R6). */
static void T2_RAM stash_successor(uint32_t index,
                                   const trigger_position_descriptor_t *descriptor)
{
    const uint32_t bit = 1u << index;
    if (descriptor->stage != TRIGGER_POSITION_STAGE_PREVIEW ||
        descriptor->prediction_version <= slot_version_high(index) ||
        descriptor->target_ms_index == s_state.descriptors[index].target_ms_index)
        return;
    if ((s_stash_valid & bit) &&
        s_stash[index].target_ms_index != descriptor->target_ms_index)
        note_position_lost();
    s_stash[index] = *descriptor;
    s_stash_valid |= bit;
}

/* R3: hand a finished slot to its stashed successor BEFORE the transport is read, so the
 * FREEZE that follows (the origin keeps resending it until it is acknowledged) finds the
 * successor's PREVIEW and freezes it, instead of being refused as a leftover of the
 * finished occupant. A successor whose window closed in the meantime is a lost position. */
static T2_BOUNDED void T2_HOT(promote_stashed)(uint64_t current_ms)
{
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        const uint32_t bit = 1u << index;
        if (!(s_stash_valid & bit)) continue;
        if (!(s_state.frozen_mask & bit) ||
            !((s_state.action_failed_mask | s_state.action_completed_mask) & bit)) continue;
        const trigger_position_descriptor_t successor = s_stash[index];
        s_stash_valid &= ~bit;
        recycle_slot(index);
        if (!admit_preview(successor, index, current_ms)) note_position_lost();
    }
}

static T2_BOUNDED bool T2_HOT(accept_remote_position)(uint64_t current_ms)
{
    /* R3: a slot whose occupant already finished is handed to its stashed successor first,
     * so the FREEZE resend arriving in this same pass freezes the successor instead of being
     * refused as the finished occupant's leftover. */
    promote_stashed(current_ms);
    trigger_position_descriptor_t descriptor;
    if (!distributed_refmem_get_position_descriptor(
            s_config.source_slot, &descriptor)) return true;
    if (descriptor.position_id < 1u ||
        descriptor.position_id > TRIGGER_T2_RUNTIME_POSITION_COUNT ||
        descriptor.prediction_version > TRIGGER_T2_PREDICTION_VERSION_MAX) {
        note_descriptor_reject();
        return false;
    }
    /* position_id is the slot (+1), not the scan position: the scan's position is the
     * PREVIEW's target_ms_index, which is what the window records as its identity. */
    const uint32_t index = descriptor.position_id - 1u;
    const uint32_t bit = 1u << index;
    if (s_state.frozen_mask & bit) {
        if (!((s_state.action_failed_mask | s_state.action_completed_mask) & bit)) {
            /* Frozen and still in flight: the occupant keeps its version and everything
             * addressed to this slot is its duplicate - except a NEWER PREVIEW naming a
             * different position, which is the slot's next occupant. It must not be dropped
             * (see s_stash). */
            stash_successor(index, &descriptor);
            return true;
        }
        /* Finished but not yet handed on: only a PREVIEW newer than anything the finished
         * occupant used starts the next position; the rest is a leftover resend. */
        if (descriptor.stage == TRIGGER_POSITION_STAGE_FREEZE ||
            descriptor.prediction_version <= slot_version_high(index))
            return true;
        recycle_slot(index);
    } else if (s_slot_floor[index] && descriptor.prediction_version <= s_slot_floor[index]) {
        return true;
    }
    if (descriptor.stage == TRIGGER_POSITION_STAGE_FREEZE) {
        if (!descriptor_valid(&descriptor)) {
            note_descriptor_reject();
            return false;
        }
        /* The FREEZE names its position by target_ms_index. A leftover FREEZE of the occupant
         * this slot handed on carries ITS target, so it is told apart by closeness: nearer
         * to the previous occupant's target than to the PREVIEW now held means it is not for
         * the current occupant. Drift between PREVIEW versions of one position is small
         * against the distance between two positions, so no tolerance has to be configured
         * (a fixed 2-sync tolerance rejected legitimate FREEZEs on the bench). */
        if ((s_state.received_mask & bit) && descriptor.target_ms_index != 0u &&
            s_slot_prev_target[index] != 0u) {
            const uint64_t have = s_state.descriptors[index].target_ms_index;
            const uint64_t want = descriptor.target_ms_index;
            const uint64_t prev = s_slot_prev_target[index];
            const uint64_t to_have = have > want ? have - want : want - have;
            const uint64_t to_prev = prev > want ? prev - want : want - prev;
            if (to_prev < to_have) {
                ++s_state.freeze_mismatch_count;
                return true;
            }
        }
        /* The window record normally exists already (the PREVIEW allocated it under the
         * scan position's target_ms); alloc() with that same identity changes nothing,
         * and a FREEZE with no PREVIEW at all still gets a record to count on.
         * mark() forbids stage jumps (ENTERED -> FROZEN is a two-step move and was
         * refused every time - bench: mark_rejected 34 per follower with alloc 0),
         * so announce first; re-marking is idempotent and therefore safe here. */
        (void)trigger_position_window_alloc(&s_position_window, descriptor.position_id,
            (uint32_t)s_state.descriptors[index].target_ms_index,
            (uint16_t)s_state.descriptors[index].prediction_version, 0u);
        (void)trigger_position_window_mark(&s_position_window,
                                           descriptor.position_id,
                                           TRIGGER_POSITION_WINDOW_ANNOUNCED);
        (void)trigger_position_window_mark(&s_position_window,
                                           descriptor.position_id,
                                           TRIGGER_POSITION_WINDOW_FROZEN);
        s_state.frozen_mask |= bit;
        __atomic_store_n(&s_frozen_public, s_state.frozen_mask, __ATOMIC_RELEASE);
        /* How much of the freeze lead was left at this instant. The design promise is
         * "freeze early, act later", so this is the whole budget the rest of the chain
         * (lane transport, local solve, arm) has to fit into; a late/expired count has
         * to be read against it. Units: coarse ordinals (ms on a 1 ms grid). */
        {
            const uint64_t current = s_state.cursor.pulse_index;
            const uint64_t target = descriptor.target_ms_index;
            if (target > current) {
                const uint32_t lead = (uint32_t)(target - current);
                if (!s_state.freeze_lead_min_slices || lead < s_state.freeze_lead_min_slices)
                    s_state.freeze_lead_min_slices = lead;
                s_state.freeze_lead_last_slices = lead;
            } else {
                ++s_state.freeze_lead_expired_count;
            }
        }
        /* Freeze complete for this follower: report it on every image. */
        report_frozen();
        s_state.freeze_version[index] = descriptor.prediction_version;
        /* Pick up a plan that was written between the receive pass and this freeze: arming the
         * previous occupant clears this slot's version, so without this refresh a plan that is
         * already in the mailbox is invisible and the position is failed as NO_PLAN (measured
         * with a three-group window: 109 publishes before the freeze, all received, 109 NO_PLAN). */
        deadline_receive_slot(index);
        const t2_deadline_plan_t *plan = &s_deadline[index];
        s_state.freeze_local_age[index] = 0xFFu;
        s_state.freeze_local_version[index] = plan->prediction_version;
        /* The plan must belong to THIS position: the descriptor's version is the identity, and
         * anything at or below the slot's floor is the finished occupant's leftover. Version 0
         * means no plan at all. Measured with a three-group window: plans were published before
         * the freeze and received (recv_ok=109) yet invisible here, because arming the previous
         * position cleared the slot and Core0 will not republish a plan its mailbox still holds. */
        if (!plan->prediction_version ||
            plan->prediction_version <= s_slot_floor[index]) {
            s_state.freeze_missing_local_count++;
            s_state.freeze_mismatch_count++;
            action_fail(index, TRIGGER_T2_ACTION_FAIL_NO_PLAN);
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
            action_fail(index, plan->action_q <= s_state.cursor.action_cursor ?
                TRIGGER_T2_ACTION_FAIL_LATE : TRIGGER_T2_ACTION_FAIL_EXPIRED);
            return false;
        }
        for (uint32_t other = 0u; other < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++other) {
            if (other == index || !(s_state.frozen_mask & (1u << other)) ||
                (s_state.action_failed_mask & (1u << other))) continue;
            const uint64_t a = plan->action_q, b = s_deadline[other].action_q;
            const uint64_t distance = a > b ? a - b : b - a;
            if (distance < s_local_span_max ||
                (s_armed_index == other && a < b)) {
                action_fail(index, TRIGGER_T2_ACTION_FAIL_CONFLICT);
                return false;
            }
        }
        return true;
    }
    /* Compact PREVIEW: the same binding, validation, window admission and slot save the
     * R3 promotion of a stashed successor uses, so a successor is admitted under exactly
     * the rules of a transport-delivered PREVIEW. */
    return admit_preview(descriptor, index, current_ms);
}

static void complete_action(uint32_t index)
{
    const uint32_t bit = 1u << index;
    /* T2-SEQ-001: one position runs the whole group sequence of its role. A
     * completed group that is not the last one only advances the cursor; the
     * position is marked EXECUTED, released from the window and reported as a
     * receipt only by its LAST group, so a partial sequence can never look like
     * a finished position in the window totals. */
    const uint32_t group = s_group_armed[index];
    /* An open window is the authority on how many groups this position has: the role template
     * count can be smaller than the plan's window (measured: template count 1 with a 2-group
     * window finished the position on the first receipt). */
    const uint32_t groups_total = s_group_handed[index]
        ? s_group_handed[index] : s_local_template_count;
    s_group_armed[index] = TRIGGER_T2_RUNTIME_GROUP_NONE;
    s_state.action_group_in_flight[index] = TRIGGER_T2_RUNTIME_GROUP_NONE;
    if (group < groups_total) {
        s_state.action_group_done[index] |= 1u << group;
        s_state.action_groups_executed++;
        if (group + 1u < groups_total) {
            s_group_next[index] = (uint8_t)(group + 1u);
            s_state.action_group_next[index] = s_group_next[index];
            if (s_group_next[index] < s_group_handed[index]) {
                /* Already in hardware: this receipt advances the cursor, nothing is armed. */
                s_group_armed[index] = s_group_next[index];
                s_state.action_group_in_flight[index] = s_group_next[index];
                s_deadline[index].group = s_group_next[index];
                return;
            }
            /* T2-SEQ-001 in-flight window: promote the next group IN THIS PASS from the
             * tick Core0 solved together with the one that just ran, so the arm does not
             * wait for another Core0 round trip (measured 2026-10-16: that round trip
             * exceeded 25 ms on loaded followers, which no real group spacing affords).
             * The static delta_q gives the next slice without any solve. */
            if (s_deadline[index].next_tick &&
                s_local_group[group + 1u].delta_q >= s_local_group[group].delta_q) {
                const uint64_t step =
                    (uint64_t)((int64_t)s_local_group[group + 1u].delta_q -
                               (int64_t)s_local_group[group].delta_q);
                s_deadline[index].action_q += step;
                s_deadline[index].deadline_tick = s_deadline[index].next_tick;
                s_deadline[index].next_tick = 0u;   /* the group after it comes from Core0 */
                s_deadline[index].group = group + 1u;
            }
            return;
        }
    }
    s_group_handed[index] = 0u;
    s_action_sequence_last[index] = 0u;
    /* Put the group cursor back to the first group: the next position on this slot must be
     * solved from group 0 again. Leaving it at the last group made the Core0 guard at the
     * solver entry skip the slot for ever (group 1 is only derivable from a group-0 plan, so
     * the solve bailed out and the FREEZE saw prediction_version 0 -> NO_PLAN). Bench
     * measurement that pinned this: two-group role executed 6 of 189 positions, NO_PLAN 186,
     * invariant to the position period (1 ms / 4 ms / 100 ms all identical), while single-group
     * roles executed 189 of 189. */
    /* Both sides of the handshake: Core0 publishes from `action_group_next`, Core1 selects on
     * `s_group_next`; leaving either at the last group makes a published group-0 plan unusable
     * (measured: NO_PLAN 186 -> 0 after the Core0 side, while executed stayed at 6). */
    s_state.action_group_next[index] = 0u;
    s_group_next[index] = 0u;
    s_group_armed[index] = TRIGGER_T2_RUNTIME_GROUP_NONE;
    s_state.action_group_in_flight[index] = TRIGGER_T2_RUNTIME_GROUP_NONE;
    /* A finished window must not leave its last group behind: the arm path clears
     * prediction_version, and the FREEZE path reads version 0 as NO_PLAN. Measured with a
     * two-group role: freeze_missing_local=166 against 6 executed positions, while
     * single-group roles (which only ever leave group 0 behind) ran 169/169. */
    s_deadline[index].group = 0u;
    s_deadline[index].group_count = 0u;
    s_deadline[index].prediction_version = 0u;
    s_deadline[index].next_tick = 0u;
    s_state.emitted_position_mask |= bit;
    s_state.action_completed_mask |= bit;
    report_frozen();
    if (trigger_position_window_find(&s_position_window, index + 1u) >= 0) {
        (void)trigger_position_window_mark(&s_position_window, index + 1u,
                                           TRIGGER_POSITION_WINDOW_EXECUTED);
        (void)trigger_position_window_release(&s_position_window, index + 1u);
    }
    if (s_state.event_count >= TRIGGER_T2_RUNTIME_EVENT_CAPACITY) {
        s_state.event_dropped_count++;
        return;
    }
    const t2_local_group_t *last = &s_local_group[
        s_local_template_count ? s_local_template_count - 1u : 0u];
    const uint32_t event_index = s_state.event_count++;
    s_state.events[event_index] = (trigger_action_runtime_event_t){
        .position_id = index + 1u, .template_id = last->template_id,
        .role = s_local_role, .kind = s_local_kind,
        .argument = last->argument, .action_q = s_deadline[index].action_q};
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
    if (s_local_kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_deadline_status_t status;
        (void)sync_io_switch_realtime_cancel();
        if (!sync_io_switch_realtime_status(&status)) return true;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        return status.state == SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
    }
    if (s_local_kind == TRIGGER_ACTION_TEMPLATE_GATEWAY) {
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
    if (s_local_kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_snapshot_t status;
        stopped = sync_io_switch_snapshot(&status) && status.loaded &&
            sync_io_switch_stop(status.generation);
    } else if (s_local_kind == TRIGGER_ACTION_TEMPLATE_GATEWAY) {
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
    bool s_switch_receipt_seen = false;
    if (s_local_kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_realtime_service_core1();
        sync_io_switch_deadline_status_t status;
        if (!sync_io_switch_realtime_status(&status)) return;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        sequence = status.sequence; deadline = status.deadline_tick;
        s_switch_receipt_seen = status.receipt_mask != 0u;
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
        const uint32_t expected = s_state.action_sequence[index];
        const uint32_t window_last = s_action_sequence_last[index];
        if (window_last && sequence == expected && expected > 0u) ++s_window_reuse_count;
        if (sequence > expected && sequence <= window_last) {
            /* Inside the handed window. The switch loads the NEXT queue entry as soon as the
             * previous group completes, so a sequence advance means "a group finished and the
             * next one is armed" ONLY together with a receipt: without one the backend has just
             * advertised the armed group and nothing finished yet (measured: 3 advances, 0 jumps,
             * 3 without receipt, 159 repeats of the same sequence). Counting those as completions
             * made executed run ahead of the hardware and left the window PENDING for ever. */
            ++s_window_advance_count;
            if (sequence != expected + 1u) ++s_window_jump_count;
            s_deadline[index].deadline_tick = deadline;
            if (!s_switch_receipt_seen) {
                ++s_window_no_receipt_count;
                return;
            }
            complete_action(index);
            s_state.action_sequence[index] = sequence;
            if (s_group_next[index] < s_group_handed[index])
                s_state.action_group_in_flight[index] = s_group_next[index];
            return;
        }
        if (sequence != expected ||
            deadline != s_deadline[index].deadline_tick) {
            cancel_backend();
            s_state.action_uncertain_mask |= bit;
            action_fail(index, TRIGGER_T2_ACTION_FAIL_BACKEND);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        } else if (complete) {
            complete_action(index);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        } else if (terminal ||
                   /* An armed absolute deadline belongs to the backend: a transiently
                    * invalid cursor says nothing about it, and the plan-identity checks
                    * below need a valid cursor to compare against (comparing against the
                    * stale snapshot cancelled live actions - measured 2026-10-16 as bursts
                    * of abandoned positions on all three followers at the same tick). Only
                    * a terminal backend state or a REAL identity change cancels here. */
                   (cursor_valid &&
                    (s_deadline[index].session != s_state.cursor.session ||
                     s_deadline[index].ring_config_seq != s_state.cursor.ring_config_seq))) {
            if (!terminal) uncertain = cancel_backend();
            if (uncertain) s_state.action_uncertain_mask |= bit;
            action_fail(index, TRIGGER_T2_ACTION_FAIL_BACKEND);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        }
    }
    if (!cursor_valid) {
        /* Defer: an invalid cursor is a statement about the time base, not about these
         * positions. The armed one is serviced by the backend above; the rest keep their
         * frozen state until the cursor returns, at which point the selection loop below
         * fails any that is provably late (TRIGGER_T2_ACTION_FAIL_LATE) or expired. Failing
         * them here - the previous behaviour - turned every cursor blip into a burst of
         * abandoned positions (measured 2026-10-16: 23-30 per follower over a 361-position
         * scan, in bursts that hit all three boards on the same tick). */
        return;
    }
    if (s_armed_index < TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    uint32_t selected = TRIGGER_T2_RUNTIME_POSITION_COUNT;
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        const uint32_t bit = 1u << index;
        if (!(s_state.frozen_mask & bit) ||
            ((s_state.action_failed_mask | s_state.action_completed_mask) & bit)) continue;
        const t2_deadline_plan_t *plan = &s_deadline[index];
        /* T2-SEQ-001: a plan that belongs to a group which already finished is
         * not late - the next group simply has not been prepared yet, and its
         * action lies in the future. Without this the selection pass right after
         * a group completion judged the completed group's plan LATE and failed
         * the position. */
        if (plan->group != s_group_next[index]) continue;
        if (!plan->prediction_version || plan->session != s_state.cursor.session ||
            plan->ring_config_seq != s_state.cursor.ring_config_seq ||
            s_state.cursor.action_cursor / s_config.grid.subdivisions > plan->expiry_sync ||
            plan->action_q <= s_state.cursor.action_cursor) {
            action_fail(index, !plan->prediction_version ? TRIGGER_T2_ACTION_FAIL_NO_PLAN :
                (plan->action_q <= s_state.cursor.action_cursor ?
                 TRIGGER_T2_ACTION_FAIL_LATE : TRIGGER_T2_ACTION_FAIL_EXPIRED));
            continue;
        }
        if (selected == TRIGGER_T2_RUNTIME_POSITION_COUNT ||
            plan->action_q < s_deadline[selected].action_q) selected = index;
    }
    if (selected == TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    const t2_deadline_plan_t *plan = &s_deadline[selected];
    if (plan->group != s_group_next[selected]) {
        /* The plan still belongs to the group that already finished, or to a
         * group the cursor has not reached: never arm a stale group. Core0
         * republishes the plan for the current group. */
        /* The version is no longer cleared here: the FREEZE compares it with the descriptor's own
     * version, so a leftover can never be accepted, and an early plan for the next position
     * stays visible instead of being destroyed by this arm (that destruction was worth 109
     * NO_PLAN positions with a three-group window). */
        action_fail(selected, TRIGGER_T2_ACTION_FAIL_NO_PLAN);
        return;
    }
    bool armed;
    uint32_t handed = 0u;
    if (plan->group_count > 1u) {
        /* One call per window, whichever backend this role owns: the spacing then comes from
         * the table Core0 solved, not from re-arming after each completion. */
        uint64_t delta_ticks[T2_PLAN_GROUP_MAX];
        const uint64_t base = plan->group_tick[0];
        for (uint32_t k = 0u; k < plan->group_count; ++k)
            delta_ticks[k] = (plan->group_tick[k] > base) ? plan->group_tick[k] - base : 0u;
        armed = s_local_kind == TRIGGER_ACTION_TEMPLATE_SWITCH ?
            sync_io_switch_realtime_stage_sequence(base, plan->group_argument,
                delta_ticks, plan->group_count) :
            sync_io_gateway_realtime_stage_sequence(base, plan->group_argument,
                delta_ticks, plan->group_count);
        if (armed) handed = plan->group_count;
    } else {
        armed = s_local_kind == TRIGGER_ACTION_TEMPLATE_SWITCH ?
            sync_io_switch_realtime_schedule_at(
            s_local_group[s_group_next[selected]].argument, plan->deadline_tick) :
            sync_io_gateway_realtime_schedule_at(
            s_local_group[s_group_next[selected]].argument, plan->deadline_tick);
    }
    if (!armed) {
        /* A rejected absolute deadline is terminal. No fire-now fallback.
         * Origin validation can fail after the edge: preserve UNCERTAIN. */
        if (s_local_kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
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
        action_fail(selected, TRIGGER_T2_ACTION_FAIL_BACKEND);
        return;
    }
    s_armed_index = selected;
    /* Segmentation v1 (follower side): lead left at the moment of arming; subtracting
     * it from freeze_lead_* isolates the admit -> arm segment. */
    {
        const uint32_t target =
            (uint32_t)s_state.descriptors[selected].target_ms_index;
        const uint64_t now_ms = s_state.cursor.pulse_index;
        note_lead_pack(&s_state.arm_lead_pack,
                       target > now_ms ? (uint32_t)(target - now_ms) : 0u);
    }
    s_group_armed[selected] = (uint8_t)plan->group;
    s_state.action_group_in_flight[selected] = plan->group;
    s_group_handed[selected] = (uint8_t)(plan->group + (handed ? handed : 1u));
    s_state.action_armed_mask |= 1u << selected;
    s_state.action_deadline_tick[selected] = plan->deadline_tick;
    /* Read the owner's accepted sequence; a later mismatch cannot complete us. */
    if (s_local_kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_deadline_status_t status;
        if (sync_io_switch_realtime_status(&status)) {
            s_state.action_sequence[selected] = status.sequence;
            s_action_sequence_last[selected] = status.sequence + (handed ? handed - 1u : 0u);
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
    /* T2-SEQ-001: keep EVERY row of this board's role, in table order, as the
     * group sequence of one position (SWITCH_A 8 / SWITCH_B 2 / GATEWAY 2). The
     * table capacity is unchanged; all rows of a role must share its kind because
     * one backend serves the role. */
    uint32_t local_count = 0u, local_kind = 0u, span_max = 0u;
    if (config->local_role >= TRIGGER_DEPLOYMENT_ROLE_COUNT) return false;
    for (uint32_t i = 0u; i < config->templates.count; ++i) {
        const trigger_action_template_t *row = &config->templates.row[i];
        if (row->role != config->local_role) continue;
        if (local_count >= TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY) return false;
        if (!local_count) local_kind = row->kind;
        else if (row->kind != local_kind) return false;
        /* The compact record is only lossless inside these ranges; a row outside
         * them is refused rather than truncated (a truncated channel code or
         * offset would command the wrong thing). Rows must also be in
         * non-decreasing delta_q order: a later group is derived from the group
         * before it, so a row that moves backwards in time is not a sequence. */
        if (!row->template_id || row->template_id > UINT16_MAX ||
            row->argument > UINT16_MAX ||
            row->delta_q < INT32_MIN || row->delta_q > INT32_MAX ||
            (local_count && row->delta_q < s_local_group[local_count - 1u].delta_q)) {
            tdma_seat_mount_store_note_reject(150u); return false;
        }
        if (row->span_slices > span_max) span_max = row->span_slices;
        s_local_group[local_count].template_id = (uint16_t)row->template_id;
        s_local_group[local_count].argument = (uint16_t)row->argument;
        s_local_group[local_count].delta_q = (int32_t)row->delta_q;
        ++local_count;
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
    /* The pairing array lives outside s_state, so a run start/reset must clear it too:
     * a stale staging cursor would otherwise fabricate an ack round trip. */
    memset(s_freeze_stage_at, 0, sizeof(s_freeze_stage_at));
    s_local_template_count = local_count;
    s_local_kind = local_kind;
    s_local_role = config->local_role;
    s_local_span_max = span_max ? span_max : 1u;
    s_state.action_group_count = local_count;
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++i) {
        s_group_next[i] = 0u;
    s_group_handed[i] = 0u;
    s_action_sequence_last[i] = 0u;
        s_group_armed[i] = TRIGGER_T2_RUNTIME_GROUP_NONE;
        s_state.action_group_next[i] = 0u;
        s_state.action_group_in_flight[i] = TRIGGER_T2_RUNTIME_GROUP_NONE;
    }
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
    /* The pairing array lives outside s_state, so a run start/reset must clear it too:
     * a stale staging cursor would otherwise fabricate an ack round trip. */
    memset(s_freeze_stage_at, 0, sizeof(s_freeze_stage_at));
    s_state.configured = 1u;
    s_state.origin = s_config.origin ? 1u : 0u;
    s_state.source_slot = s_config.source_slot;
    s_state.target_mask = s_config.target_mask;
    s_state.run_id = s_config.run_id;
    /* The group sequence is part of this run's position accounting, so START must
     * re-establish it after the reset above (the mounted rows themselves live
     * outside s_state and survive). */
    s_state.action_group_count = s_local_template_count;
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++i) {
        s_group_next[i] = 0u;
    s_group_handed[i] = 0u;
    s_action_sequence_last[i] = 0u;
        s_group_armed[i] = TRIGGER_T2_RUNTIME_GROUP_NONE;
        s_state.action_group_next[i] = 0u;
        s_state.action_group_in_flight[i] = TRIGGER_T2_RUNTIME_GROUP_NONE;
    }
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
    memset(s_slot_target, 0, sizeof(s_slot_target));
    memset(s_slot_floor, 0, sizeof(s_slot_floor));
    memset(s_slot_prev_target, 0, sizeof(s_slot_prev_target));
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++i)
        __atomic_store_n(&s_announced_ordinal[i], 0u, __ATOMIC_RELEASE);
    memset(s_prepare_ordinal, 0, sizeof(s_prepare_ordinal));
    s_prepare_next = 0u;
    trigger_position_window_reset(&s_position_window);
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
         (s_local_kind == TRIGGER_ACTION_TEMPLATE_GATEWAY && s_state.action_armed_mask));
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
    /* Without a scan step the three legacy positions drive the FREEZE; with one, the
     * rolling window does (the legacy mask only knows START/ZERO/TERMINAL). */
    if (!s_scan_step_mdeg) s_pending_mask |= mask;
    origin_probe_end(TRIGGER_T2_RUNTIME_ORIGIN_PROBE_OBSERVE, &mark);
    /* The rolling window announces each position's threshold exactly once and rolls in
     * the same call, so the announced entry has already left the window when advance()
     * returns: read who occupies each window slot BEFORE advancing, then map the returned
     * slot bits to scan ordinals (migration step 2: the window now drives the FREEZE). */
    if (s_scan_step_mdeg) {
        uint32_t ordinal[TRIGGER_TURNTABLE_WINDOW_MAX];
        uint64_t target[TRIGGER_TURNTABLE_WINDOW_MAX];
        for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot) {
            const trigger_turntable_window_entry_t *entry = &s_state.predictor.window[slot];
            ordinal[slot] = entry->valid ? entry->absolute_index : UINT32_MAX;
            target[slot] = entry->target_count;
        }
        uint32_t window_mask = 0u;
        (void)trigger_turntable_window_advance(&s_state.predictor,
                                               &s_config.predictor, count, &window_mask);
        for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot)
            if ((window_mask & (1u << slot)) && ordinal[slot] != UINT32_MAX)
                origin_announce(ordinal[slot], target[slot]);
    }
    __atomic_store_n(&s_threshold_public, t2_published_mask(),
                     __ATOMIC_RELEASE);
    origin_probe_end(TRIGGER_T2_RUNTIME_ORIGIN_PROBE_WINDOW, &mark);
    const bool frozen = stage_one_freeze();
    origin_probe_end(TRIGGER_T2_RUNTIME_ORIGIN_PROBE_FREEZE, &mark);
    return frozen;
}

/* The follower's position lane cannot wait for a cursor: its single-depth mailbox plus the
 * bounded hold ring are the only storage and this service is the only drain. A cursor blip
 * (non-sticky, and frequent enough to matter: measured 2026-10-16, one board drained at half
 * the rate of its peers - 66k lane accepts against 134k - and its window stayed empty) must
 * therefore not stop the drain. Admission and arming still need a valid cursor and wait for
 * the next pass; only the transport is taken here. Bounded batch, per-record work unchanged:
 * see TRIGGER_T2_RUNTIME_RECEIVE_DRAIN_MAX. */
/* Second-position fallback (T2-PROVISIONAL-001). The record already carries the NEXT
 * position's target (same 16 B slice), so when a position's FREEZE never arrives the cached
 * target is used instead of losing the position entirely. This is the feature, not an
 * experiment: it always runs. The guards below mean it can only fire where the old code
 * would have waited forever, so a healthy run behaves exactly as before:
 *   - the position is not frozen yet (a real FREEZE always wins);
 *   - it has a staged descriptor (something to execute);
 *   - the cached target is still in the future (never fire late) and inside the margin. */
static T2_BOUNDED void T2_HOT(position_fallback)(uint64_t current_ms)
{
    const trigger_position_next_cache_t *cache = trigger_position_next_cache();

    if (cache->position_id < 1u ||
        cache->position_id > TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    const uint32_t index = cache->position_id - 1u, bit = 1u << index;
    if ((s_state.frozen_mask & bit) != 0u) return;                   /* FREEZE already owns it */
    if (s_state.descriptors[index].prediction_version == 0u) return; /* nothing to execute */
    if (cache->target_ms_index <= (uint32_t)current_ms) return;      /* past: never fire late */
    if ((uint32_t)current_ms + TRIGGER_T2_PROVISIONAL_MARGIN_MS <
        cache->target_ms_index) return;                              /* too early: wait */
    s_state.descriptors[index].target_ms_index = cache->target_ms_index;
    s_state.frozen_mask |= bit;
    s_position_fallback_promotions++;
    report_frozen();
}

static T2_BOUNDED void T2_HOT(drain_lane)(uint64_t current_ms)
{
    deadline_receive();
    for (uint32_t taken = 0u; taken < TRIGGER_T2_RUNTIME_RECEIVE_DRAIN_MAX; ++taken)
        (void)accept_remote_position(current_ms);
    position_fallback(current_ms);
}

/* Same drain with no valid cursor: the snapshot keeps its fields when it is invalidated, so
 * the last known coarse ordinal is still usable for admission timestamps. Gated on a time
 * base this run has ALREADY established (a real coarse sample was taken): a board that never
 * bound has no consistent session/grid to admit records into, and admitting them there would
 * only turn into NO_PLAN failures later (pinned by the zero_pulse_index case). */
static T2_BOUNDED void T2_HOT(drain_lane_without_cursor)(void)
{
    if (!s_state.last_sync_sequence) return;
    drain_lane(s_state.cursor.pulse_index ? s_state.cursor.pulse_index - 1u : 0u);
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
        drain_lane_without_cursor();
        if (deadlines_need_service()) service_deadlines(false);
        s_state.last_error = TRIGGER_T2_RUNTIME_CURSOR;
        T2_RETURN(s_state.last_error);
    }
    if (!cursor->valid || !cursor->pulse_index) {
        cursor->valid = 0u;
        drain_lane_without_cursor();
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
        /* The validated cursor already supplies coarse ordinal + 1; the lane is drained in a
         * bounded batch so a long announcement lead's record burst cannot outrun the
         * consumer (see TRIGGER_T2_RUNTIME_RECEIVE_DRAIN_MAX). */
        drain_lane(cursor->pulse_index - 1u);
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
            /* Second-position fallback, read from its own monotonic counter: it is a
             * different fact from freeze_fallback_count (version mismatch on the FREEZE
             * path), and only this one says "the cached next target was executed". */
            out->fallback_promoted_count = s_position_fallback_promotions;
            /* Receive-side timestamp: when this engine ADMITTED the record it is working
             * on, plus its age in microseconds at query time (see the header comment). */
            out->rx_local_tick = s_rx_local_tick;
            out->rx_local_sample_seq = s_rx_local_sample_seq;
            out->rx_timestamp_count = s_rx_timestamp_count;
            out->rx_stale_count = s_rx_stale_count;
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

uint32_t trigger_t2_runtime_group_pack(uint32_t index)
{
    if (index >= TRIGGER_T2_RUNTIME_POSITION_COUNT) return 0u;
    trigger_t2_runtime_snapshot_t snapshot;
    if (!trigger_t2_runtime_snapshot(&snapshot)) return 0u;
    return ((uint32_t)snapshot.action_group_next[index] & 0xFu) |
        (((uint32_t)snapshot.action_group_in_flight[index] & 0xFu) << 4u) |
        ((uint32_t)snapshot.action_group_done[index] << 8u);
}

/* K-2 predicates, split out so a host harness can drive them directly
 * (same extraction technique as tests/python/test_trigger_t2_freeze_abandon.py). */
static bool t2_deadline_position_accepted(uint32_t accepted_mask, uint32_t index)
{
    return (accepted_mask & (1u << index)) != 0u;
}

static bool t2_deadline_plan_current(uint32_t previous_version, uint32_t previous_token,
                                     uint32_t previous_group, uint32_t descriptor_version,
                                     uint32_t model_token, uint32_t group)
{
    return previous_version == descriptor_version && previous_token == model_token &&
        previous_group == group;
}

/* Absolute Timer1 tick for a fine-slice ordinal, through the committed DCO model.
 * Shared by a group's own solve and by the solve of the group that follows it. */
static bool slice_to_tick(uint64_t q, const vdc_dpll_manager_committed_model_t *model,
                          uint64_t *raw_tick)
{
    const uint64_t coarse = q / s_config.grid.subdivisions;
    const uint32_t fine = (uint32_t)(q % s_config.grid.subdivisions);
    if (coarse > (UINT64_MAX - s_config.grid.anchor_vdc_ns) / s_config.grid.period_ns)
        return false;
    uint64_t output_ns = s_config.grid.anchor_vdc_ns + coarse * s_config.grid.period_ns;
    /* Round toward the first representable ns/tick at or after the slice. */
    const uint64_t fraction = (uint64_t)fine * s_config.grid.period_ns;
    const uint64_t fine_ns = fraction / s_config.grid.subdivisions +
        (fraction % s_config.grid.subdivisions != 0u);
    if (output_ns > UINT64_MAX - fine_ns) return false;
    output_ns += fine_ns;
    uint64_t local_ns;
    return vdc_domain_dco_output_to_local_ns(&model->dco, output_ns, &local_ns) &&
        vdc_timer1_ns_to_ticks(BOARD_SYS_CLOCK_HZ, local_ns, true, raw_tick) &&
        *raw_tick >= model->valid_from_raw;
}

/* Widen a compact group record back into the full row the shared expansion helper
 * takes; nothing about the solve depends on the stored form. */
static trigger_action_template_t group_row(uint32_t group)
{
    const t2_local_group_t *row = &s_local_group[group];
    return (trigger_action_template_t){
        .template_id = row->template_id, .generation = 1u,
        .role = s_local_role, .kind = s_local_kind,
        .argument = row->argument, .span_slices = s_local_span_max,
        .delta_q = row->delta_q};
}

static void note_rx_timestamp(const trigger_t2_runtime_snapshot_t *snapshot,
    const vdc_dpll_manager_committed_model_t *model, uint64_t deadline_tick)
{
    uint64_t admission_tick;
    if (!slice_to_tick(snapshot->cursor.action_cursor, model, &admission_tick)) return;
    s_rx_local_tick = admission_tick;
    s_rx_local_sample_seq = snapshot->cursor.sample_seq;
    ++s_rx_timestamp_count;
    /* The record was already late when this board admitted it. */
    if (deadline_tick < admission_tick) ++s_rx_stale_count;
}

static void prepare_deadlines_core0(uint32_t session,
    const trigger_t2_runtime_snapshot_t *snapshot)
{    if (!s_local_template_count) return;
    vdc_dpll_manager_committed_model_t model;
    if (!vdc_dpll_manager_get_committed_model(&model) ||
        model.session != snapshot->cursor.session || !model.token || !model.dco.valid ||
        model.dco.nominal_period_ns != s_config.grid.period_ns) return;
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        /* K-2: FREEZE freezes position prediction only, not the DCO. Skip a position only
         * once its action sequence FINISHED (completed or failed): a frozen-but-not-yet
         * loaded position must still be re-solved by the latest DCO, and a position that
         * merely armed an earlier group still needs its next group prepared.
         * T2-SEQ-001: prepare the group the Core1 cursor is waiting for; the plan carries
         * the group so a later group of the same position (same prediction version and
         * model token) is published instead of being mistaken for an old plan.
         * Core0 reads the snapshot, never Core1's runtime state. */
        const uint32_t group = snapshot->action_group_next[index];
        /* An armed group is already in the hardware owner's hands and is never
         * re-solved (K-2: accepted actions are not rewritten); only when its
         * completion advances the cursor does the next group get prepared. */
        if (group >= s_local_template_count) { ++s_skip_group_unreachable; continue; }
        if (snapshot->action_group_in_flight[index] == group) { ++s_skip_inflight; continue; }
        if (t2_deadline_position_accepted(
                snapshot->action_completed_mask | snapshot->action_failed_mask, index)) {
            ++s_skip_accepted;
            continue;
        }
        /* Where a group's absolute slice comes from. Group 0 solves from the newest
         * PREVIEW. A LATER group continues the SAME position, so it is derived from
         * the plan of the group before it (Core0's own last publish for this slot)
         * and never re-solved from the descriptor mirror: that mirror is refreshed
         * only when the FREEZE still finds its preview in the ring, so a missing
         * match leaves the PREVIOUS occupant's descriptor behind and the derived
         * slice lands in the past. Measured on the bench 2026-10-16 as "group 0
         * completes, group 1 never prepared, nothing failed" (per slot done=0b1,
         * next=1, in_flight=none, every fail counter 0 - the top page cannot show
         * this, which is why the group words were appended to T2:ACTion?). */
        uint64_t q;
        uint32_t plan_version, plan_expiry;
        if (group == 0u) {
            trigger_position_descriptor_t descriptor;
            if (!preview_copy(index, 0u, &descriptor)) { ++s_solve_no_descriptor; continue; }
            /* Mailbox has one RUN writer: Core0 can inspect its own old words. */
            t2_deadline_plan_t previous;
            memcpy(&previous, s_deadline_mailbox[index].words, sizeof(previous));
            if (t2_deadline_plan_current(previous.prediction_version, previous.model_token,
                                         previous.group, descriptor.prediction_version,
                                         model.token, group)) continue;
            const trigger_action_template_t row = group_row(group);
            trigger_action_template_instance_t instance;
            if (!trigger_action_template_expand(&row, &descriptor,
                    s_config.grid.subdivisions, &instance)) {
                ++s_solve_expand_fail;
                continue;
            }
            q = instance.position.action_q;
            plan_version = descriptor.prediction_version;
            plan_expiry = descriptor.expiry_sync;
            if (!plan_version) ++s_solve_version_zero;
        } else {
            t2_deadline_plan_t previous;
            memcpy(&previous, s_deadline_mailbox[index].words, sizeof(previous));
            if (previous.group + 1u != group || !previous.prediction_version ||
                s_local_group[group].delta_q < s_local_group[group - 1u].delta_q)
                continue;
            const uint64_t step = (uint64_t)((int64_t)s_local_group[group].delta_q -
                (int64_t)s_local_group[group - 1u].delta_q);
            if (previous.action_q > UINT64_MAX - step) continue;
            q = previous.action_q + step;
            plan_version = previous.prediction_version;
            plan_expiry = previous.expiry_sync;
        }
        if (q <= snapshot->cursor.action_cursor) continue;
        uint64_t raw_tick;
        if (!slice_to_tick(q, &model, &raw_tick)) continue;
        /* Solve the NEXT group's tick in the same pass, so Core1 can arm it without
         * waiting for another Core0 solve (see next_tick above). Zero when this is
         * the last group or the offsets are not usable. */
        uint64_t next_tick = 0u;
        if (group + 1u < s_local_template_count &&
            s_local_group[group + 1u].delta_q >= s_local_group[group].delta_q) {
            const uint64_t step = (uint64_t)((int64_t)s_local_group[group + 1u].delta_q -
                (int64_t)s_local_group[group].delta_q);
            if (q <= UINT64_MAX - step) (void)slice_to_tick(q + step, &model, &next_tick);
        }
        t2_deadline_plan_t plan = {
            .action_q = q, .deadline_tick = raw_tick, .next_tick = next_tick,
            .session = model.session, .ring_config_seq = snapshot->cursor.ring_config_seq,
            .prediction_version = plan_version,
            .expiry_sync = plan_expiry, .model_token = model.token,
            .group = group, .group_count = 0u};
        plan.group_tick[plan.group_count] = raw_tick;
        plan.group_argument[plan.group_count++] = s_local_group[group].argument;
        for (uint32_t k = group + 1u; k < s_local_template_count &&
             plan.group_count < T2_PLAN_GROUP_MAX; ++k) {
            if (s_local_group[k].delta_q < s_local_group[k - 1u].delta_q) break;
            const uint64_t step = (uint64_t)((int64_t)s_local_group[k].delta_q -
                (int64_t)s_local_group[k - 1u].delta_q);
            const uint64_t from_q = q + (uint64_t)((int64_t)s_local_group[k - 1u].delta_q -
                (int64_t)s_local_group[group].delta_q);
            if (from_q > UINT64_MAX - step) break;
            uint64_t tick = 0u;
            if (!slice_to_tick(from_q + step, &model, &tick)) break;
            if (tick <= plan.group_tick[plan.group_count - 1u]) break;
            plan.group_tick[plan.group_count] = tick;
            plan.group_argument[plan.group_count++] = s_local_group[k].argument;
        }
        if (session != __atomic_load_n(&s_session, __ATOMIC_ACQUIRE)) return;
        /* Receive-side stamp: the moment THIS board admitted the record that produced this
         * plan, in the same local time base the deadline was solved in. */
        if (group == 0u) note_rx_timestamp(snapshot, &model, raw_tick);
        ++s_solve_publish;
        deadline_publish(index, &plan);
    }
}

bool trigger_t2_runtime_window_stats(trigger_t2_runtime_window_stats_t *out)
{
    if (!out) return false;
    out->advance = s_window_advance_count;
    out->jump = s_window_jump_count;
    out->reuse = s_window_reuse_count;
    out->no_receipt = s_window_no_receipt_count;
    out->skip_unreachable = s_skip_group_unreachable;
    out->skip_inflight = s_skip_inflight;
    out->skip_accepted = s_skip_accepted;
    out->published = s_solve_publish;
    out->no_descriptor = s_solve_no_descriptor;
    out->version_zero = s_solve_version_zero;
    out->expand_fail = s_solve_expand_fail;
    out->publish_before_freeze = s_publish_before_freeze;
    out->publish_after_freeze = s_publish_after_freeze;
    out->recv_busy = s_recv_busy;
    out->recv_no_version = s_recv_no_version;
    out->recv_session = s_recv_session;
    out->recv_older = s_recv_older;
    out->recv_ok = s_recv_ok;
    return true;
}

bool trigger_t2_runtime_solve_stats(trigger_t2_runtime_solve_stats_t *out)
{
    if (!out) return false;
    out->prepare_calls = s_core0_prepare_calls;
    out->prepare_last_reason = s_core0_prepare_last_reason;
    out->no_descriptor = s_solve_no_descriptor;
    out->expand_fail = s_solve_expand_fail;
    out->version_zero = s_solve_version_zero;
    out->published = s_solve_publish;
    return true;
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
    /* NO pending gate here any more. The position TX is a latest-value table that merges
     * an unwritten value into the newest one, so "a record is still waiting" is not a
     * reason to stop producing - that gate belonged to the single-depth mailbox and it is
     * exactly what made the producer slave to the transmit cadence (measured 2026-10-17:
     * with T_drain=50 ms the publish rate still tracked the taker and the merge counter
     * stayed 0, i.e. the producer never had anything to merge). The transmit side decides
     * WHEN a value goes out; the producer decides only WHAT the newest value is. */

    /* With a scan step the candidates are the window's one or two entries (any scan
     * ordinal, mapped to its slot); without one they are the three legacy positions.
     * Starting from a rotating entry keeps a position that changes every slice from
     * starving the other one, since one PREVIEW is staged per call. */
    const uint32_t entries = s_scan_step_mdeg ? TRIGGER_TURNTABLE_WINDOW_MAX
                                              : TRIGGER_T2_RUNTIME_POSITION_COUNT;
    for (uint32_t step = 0u; step < entries; ++step) {
        const uint32_t entry_index = (s_prepare_next + step) % entries;
        uint32_t index = entry_index;
        uint64_t window_threshold, window_target;
        if (s_scan_step_mdeg) {
            const trigger_turntable_window_entry_t *entry =
                &snapshot.predictor.window[entry_index];
            if (!entry->valid) continue;
            index = entry->absolute_index % TRIGGER_T2_RUNTIME_POSITION_COUNT;
            if (__atomic_load_n(&s_announced_ordinal[index], __ATOMIC_ACQUIRE) >=
                entry->absolute_index + 1u) continue;
            window_threshold = entry->threshold_count;
            window_target = entry->target_count;
            /* A new scan position on this slot starts its own preparation cadence. */
            if (s_prepare_ordinal[index] != entry->absolute_index + 1u) {
                s_prepare_ordinal[index] = entry->absolute_index + 1u;
                s_last_calculated_coarse[index] = 0u;
            }
        } else {
            window_threshold = snapshot.predictor.threshold_count[index];
            window_target = snapshot.predictor.target_count[index];
        }
        const uint32_t bit = 1u << index;
        if (__atomic_load_n(&s_threshold_public, __ATOMIC_ACQUIRE) & bit ||
            (!s_scan_step_mdeg && snapshot.predictor.announced_mask & bit) ||
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
        /* NEXT-position cache (T2-PROVISIONAL-001): if the following position already has
         * a live descriptor, ship its target inside this record (same slice, bytes 10..14).
         * The follower then holds the next target without a second boarding, which is what
         * lets a missing FREEZE fall back to the cached target instead of skipping the
         * position. Zero stays "unknown" when the neighbour has no descriptor yet. */
        {
            const uint32_t neighbour = (index + 1u) % TRIGGER_T2_RUNTIME_POSITION_COUNT;
            if (s_state.descriptors[neighbour].prediction_version != 0u) {
                next.next_position_id = s_state.descriptors[neighbour].position_id;
                next.next_target_ms_index = s_state.descriptors[neighbour].target_ms_index;
            }
        }
        /* Announce suppression: EXACT match was the old rule; measured 2026-10-16, with a
         * CONSTANT Core0 prepare cadence (~1,600/s), the staging-attempt rate per prepare
         * call varied 0.9% -> 47.8% (53x) between runs, and every attempt the single-depth
         * first-class seat cannot take is refused (refused == position wait_count, to the
         * count, in five runs). Two independent bounds now apply; the first announcement
         * and FREEZE stay untouched (FREEZE is staged on the freeze path, not here):
         *   (a) magnitude -- a re-solved target that moved less than the tolerance is noise;
         *   (b) interval  -- at most one announcement per T_min, so the records offered per
         *       position window are bounded. The follower executes the last announced
         *       target, so its staleness stays below T_min + tolerance.
         * Measured (2026-10-16): the magnitude bound ALONE changed nothing (submits/prep
         * 48.43% vs 47.83% without it), i.e. the predictor wobbles by more than the 4 ms
         * tolerance between prepares -- which is why the interval bound, not a bigger
         * tolerance, is what actually caps the offered rate. */
        static uint32_t s_announce_ms[TRIGGER_T2_RUNTIME_POSITION_COUNT];
        /* Global offer shaping: one re-announcement per T_global ACROSS positions. The
         * per-position interval below cannot see the burst formed by three positions in
         * the window plus FREEZE re-sends, which is what keeps the shared single-depth
         * seat full (measured ~271 offers/s vs ~164 boardings/s). Only re-announcements
         * are shaped -- a position's first announcement always passes -- so no position
         * can be starved of its initial announce. */
        static uint32_t s_last_offer_ms;
        const int64_t announce_delta_q =
            ((int64_t)next.target_ms_index - (int64_t)previous.target_ms_index) *
                (int64_t)s_config.grid.subdivisions +
            ((int64_t)next.target_fine_slice_index -
             (int64_t)previous.target_fine_slice_index);
        const int64_t announce_tolerance_q =
            (int64_t)s_announce.tolerance_ms *
            (int64_t)s_config.grid.subdivisions;
        const bool announce_too_soon = s_announce_ms[index] != 0u &&
            (uint32_t)coarse - s_announce_ms[index] <
                (uint32_t)s_announce.min_interval_ms;
        const bool offer_too_soon = s_last_offer_ms != 0u &&
            (uint32_t)coarse - s_last_offer_ms <
                (uint32_t)s_announce.global_interval_ms;
        if (previous.prediction_version &&
            (announce_too_soon || offer_too_soon ||
             (announce_delta_q <= announce_tolerance_q &&
              -announce_delta_q <= announce_tolerance_q)))
            continue;
        s_announce_ms[index] = (uint32_t)coarse;
        s_last_offer_ms = (uint32_t)coarse;
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
        if (s_scan_step_mdeg &&
            __atomic_load_n(&s_announced_ordinal[index], __ATOMIC_ACQUIRE) >= s_prepare_ordinal[index])
            continue;
        if (distributed_refmem_stage_position_descriptor(
                &next, s_config.target_mask)) {
            preview_save(index, &next); ++s_core0_preview_saved; s_core0_prepare_last_reason=8u;
            s_prepare_next = (entry_index + 1u) % entries;
        } else s_core0_prepare_last_reason=9u;
        return;
    }
}
