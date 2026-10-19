#ifndef TRIGGER_T2_RUNTIME_H
#define TRIGGER_T2_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_action_runtime.h"
#include "trigger_time_grid.h"
#include "trigger_time_cursor.h"
#include "trigger_turntable_predictor.h"

#define TRIGGER_T2_RUNTIME_POSITION_COUNT 3u
/* Announce tolerance, ms: a re-solved target that moved less than this counts as "no
 * news" and is NOT re-announced, so the single-depth first-class seat is not flooded by
 * prediction wobble. Measured motivation (2026-10-16): with a CONSTANT Core0 prepare
 * cadence, staging attempts per prepare call varied 0.9% -> 47.8% (53x) between runs,
 * and seat stage-refusals equalled the position wait counter 1:1 in five runs (to the
 * count in the last one). The follower executes on the last announced target, so its
 * staleness from suppressing sub-tolerance wobble is bounded by this value; FREEZE and
 * the first announcement are unaffected (< 0 means the old exact-match behaviour). */
#define TRIGGER_T2_ANNOUNCE_TARGET_TOLERANCE_MS 4u
/* Minimum interval between two ANNOUNCEMENTS of the same position, ms. Measured
 * (2026-10-16): the magnitude tolerance alone changed nothing (submits/prep 48.43% with
 * vs 47.83% without), i.e. the re-solved target wobbles by more than the tolerance
 * between Core0 prepare calls, so an interval bound is what actually caps how many
 * records the single-depth first-class seat is offered. The follower executes the last
 * announced target, so its staleness is bounded by this interval + the tolerance; the
 * first announcement and FREEZE are unaffected. */
#define TRIGGER_T2_ANNOUNCE_MIN_INTERVAL_MS 10u
/* Global offer interval, ms: at most one ANNOUNCEMENT (re-announce) across ALL positions per
 * this interval. Motivation (2026-10-16/17): the per-position interval above smooths each
 * position, but three positions in the window plus FREEZE re-sends still burst the shared
 * single-depth seat. Measured with the per-position limit in place: ~271 offers/s vs ~164
 * seat boardings/s (turnaround ~5.4 ms/record), i.e. the seat is over-offered by ~1.65x and
 * a second record inside one turnaround is refused. At 6 ms the offer rate caps at ~167/s,
 * which matches the boarding rate, so the single slot should stop filling in steady state.
 * Only RE-announcements are shaped (a position's first announcement always passes), and the
 * per-position staleness bound (T_min + tolerance) is unchanged; the extra delay a record
 * can see is at most this interval. Predictive freezing tolerates that delay (owner ruling
 * 2026-10-17: "due to predictive freezing the delay impact is small"). */
#define TRIGGER_T2_ANNOUNCE_MIN_GLOBAL_INTERVAL_MS 6u
/* Second-position fallback (T2-PROVISIONAL-001): the next position's target already rides
 * in the same 16 B slice, so a position whose FREEZE never arrived executes on that cached
 * target instead of being lost. Always on -- this is the feature, not an experiment. The
 * margin is the arming budget: inside it the cache may fire, past the target it may not
 * (a late action is never manufactured). */
#define TRIGGER_T2_PROVISIONAL_MARGIN_MS 20u
/* Installable bounds for the three announce gates above. They are no longer only
 * compile-time constants: the right value depends on the measured seat turnaround of the
 * ring that is actually running, so `TRIGger:RESERVation:T2:ANNOUNCE` installs them at
 * STOP and `...:ANNOUNCE?` reads them back without a re-flash (owner ruling 2026-10-17).
 * A gate of 0 would silently disable the shaping, so 0 is refused rather than treated as
 * "off"; the defaults below stay the measured values. */
#define TRIGGER_T2_ANNOUNCE_MAX_TOLERANCE_MS 1000u
#define TRIGGER_T2_ANNOUNCE_MAX_INTERVAL_MS 1000u
enum {
    TRIGGER_T2_RUNTIME_SEGMENT_CURSOR = 0u,
    TRIGGER_T2_RUNTIME_SEGMENT_RECEIVE,
    TRIGGER_T2_RUNTIME_SEGMENT_DEADLINES,
    TRIGGER_T2_RUNTIME_SEGMENT_COUNT,
};
/* How many action rows of the board's own role the runtime keeps: the role's
 * group sequence (SWITCH_A 8 / SWITCH_B 2 / GATEWAY 2). This is the number of
 * rows the static template table can already hold, so the sequence needs no
 * capacity growth (T2-SEQ-001); widening the table itself was disproven. */
#define TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY TRIGGER_ACTION_TEMPLATE_CAPACITY
/* No group in flight for a slot (fits uint8_t group cursors). */
#define TRIGGER_T2_RUNTIME_GROUP_NONE 0xFFu
/* One terminal receipt per executed position. A scan has many more positions than the
 * runtime has slots, and nothing drains this buffer in firmware (take_event is a host/
 * diagnostic reader), so it is sized for a short scan rather than for the slot count: the
 * event DETAIL is bounded, while event_count + event_dropped_count stays the total number
 * of receipts and the window's executed total stays the authoritative per-position count. */
#define TRIGGER_T2_RUNTIME_EVENT_CAPACITY 9u
#define TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC 2u
/* Upper bound for the same window when it is supplied by T2:CONFigure (coarse
 * ordinals). The default above stays for callers that send the 12-field command. */
#define TRIGGER_T2_DESCRIPTOR_WINDOW_MAX 1000u
/* A: floor for the DECLARED lead, in scan pulses (one STEP of the scan). Measured on
 * the bench 2026-10-16 over a full 361-position scan: a 100-pulse lead (~35 ms) still
 * lost 6-8% of the positions to TRIGGER_T2_ACTION_FAIL_LATE, while a 300-pulse lead
 * (~114 ms) and a 500-pulse one lost none. T2:CONFigure refuses a configuration whose
 * threshold cannot reach this many pulses, so a too-thin lead fails at configuration
 * instead of turning into late positions during the run. */
/* Minimum announcement lead the configure step accepts (pulses). FROZEN 2026-10-17 from a
 * measured four-point sweep with the announce throttle in place (361 positions, three
 * boards, 1:1 seat):
 *   300 pulses (~116 ms): all 361/361, zero fail     (old operating point)
 *   200 pulses (~ 77 ms): all 361/361, zero fail     <-- adopted floor
 *   150 pulses (~ 58 ms): GATEWAY lost 1 to LATE     (first failure)
 *   100 pulses (~ 39 ms): refused without this floor
 * The old floor of 300u was correct for the un-throttled path, where a 100-pulse lead
 * lost 6-8% of a scan to LATE. The throttle removed enough contention to make 200 safe,
 * which is a -33% lead. NOTE: pulses are the wrong unit for this floor (300 pulses is a
 * different time at a different scan rate); re-express it in MILLISECONDS when the scan
 * rate stops being a fixed bench property. */
#define TRIGGER_T2_MIN_DECLARE_LEAD_PULSES 200u
/* Records the follower takes from its position lane per service pass. One record per pass
 * caps the consumer at one per coarse slice: measured 2026-10-16 with a long announcement
 * lead, 361 positions produced ~155-176k lane records (~1.1 per slice), so a single-record
 * pass left the single-depth mailbox busy and the bounded hold ring dropped the overflow
 * (hold_drop ~176k-214k) until one board per run starved with a zero window. */
#define TRIGGER_T2_RUNTIME_RECEIVE_DRAIN_MAX 4u
/* Compact wire revision is never truncated or wrapped within a run. */
#define TRIGGER_T2_PREDICTION_VERSION_MAX UINT16_MAX

/* Why a frozen position did not run. The abandon count alone cannot say whether the loss
 * belongs to the lane (late arm), to the local solve (no plan), to the position geometry
 * (sibling conflict) or to the backend (refused/terminal); these are different owners and
 * different fixes. Appended to the T2:ACTion? tail so existing page indices do not move. */
enum {
    TRIGGER_T2_ACTION_FAIL_LATE = 0u,     /* deadline already behind the action cursor */
    TRIGGER_T2_ACTION_FAIL_EXPIRED = 1u,  /* the position's window had closed */
    TRIGGER_T2_ACTION_FAIL_NO_PLAN = 2u,  /* no usable local solve when the FREEZE arrived */
    TRIGGER_T2_ACTION_FAIL_CONFLICT = 3u, /* sibling span/order conflict in the same slot set */
    TRIGGER_T2_ACTION_FAIL_BACKEND = 4u,  /* backend refused, cancelled or uncertain */
    TRIGGER_T2_ACTION_FAIL_COUNT = 5u,
};

typedef enum {
    TRIGGER_T2_RUNTIME_OK = 0u,
    TRIGGER_T2_RUNTIME_ARGUMENT = 1u,
    TRIGGER_T2_RUNTIME_NOT_CONFIGURED = 2u,
    TRIGGER_T2_RUNTIME_NOT_ACTIVE = 3u,
    TRIGGER_T2_RUNTIME_COUNTER = 4u,
    TRIGGER_T2_RUNTIME_CURSOR = 5u,
    TRIGGER_T2_RUNTIME_TRANSPORT = 6u,
    TRIGGER_T2_RUNTIME_DESCRIPTOR = 7u,
    TRIGGER_T2_RUNTIME_ACTION = 8u,
    TRIGGER_T2_RUNTIME_BACKWARD = 9u,
} trigger_t2_runtime_result_t;

typedef struct {
    trigger_time_grid_t grid;
    trigger_turntable_predictor_config_t predictor;
    trigger_action_template_table_t templates;
    trigger_deployment_t deployment;
    uint32_t template_crc;
    uint32_t run_id;
    uint32_t local_role;
    uint32_t source_slot;
    uint32_t target_mask;
    /* Sync-sequence allowance after the predicted target, configured at STOP. */
    uint32_t descriptor_window_sync;
    uint32_t origin;
} trigger_t2_runtime_config_t;

typedef struct {
    uint32_t configured, active, origin;
    uint32_t source_slot, target_mask, run_id;
    uint32_t last_sync_sequence, published_mask, received_mask, emitted_position_mask;
    uint32_t frozen_mask, freeze_mismatch_count;
    uint32_t freeze_version[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint32_t freeze_local_version[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint8_t freeze_local_age[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint32_t freeze_fallback_count, freeze_missing_local_count;
    uint32_t descriptor_reject_count, transport_reject_count;
    /* A well-formed first-class position that arrived AFTER its promised window:
     * still a descriptor reject (that aggregate is unchanged), now attributed on
     * its own instead of sharing a counter with identity/grid/deployment causes.
     * An empty window (target overflow) is not lateness and is not counted. */
    uint32_t late_arrival_count;
    /* Magnitude of the most recent late arrival: the consumer's coarse ordinal
     * when the record arrived and the window it had already missed, so
     * (late_last_current_ms - late_last_expiry_sync) is how late it was. Only
     * meaningful while late_arrival_count is non-zero. */
    uint32_t late_last_current_ms, late_last_expiry_sync;
    /* WHY a FREEZE is still outstanding, per target slot: who is not speaking
     * this run id, and which position bits no reporting board has frozen. A
     * large freeze_resend_count is otherwise indistinguishable between a
     * forward delivery loss and a report that never matches this run. */
    uint32_t freeze_ack_stale_mask, freeze_ack_incomplete_mask;
    /* Set when the configured freeze target mask lists the position source
     * slot itself: those bits can never be acknowledged (the ack path only
     * fills slots from received images) and are ignored, so this flag is how
     * a resend storm caused by that misconfiguration is recognised. */
    uint32_t freeze_ack_own_in_mask;
    uint32_t event_count, event_dropped_count;
    /* ARMED is admission only. COMPLETE/emitted require the matching PIO receipt. */
    uint32_t action_armed_mask, action_completed_mask, action_failed_mask;
    uint32_t action_uncertain_mask;
    /* Attribution of every action_fail() call: see TRIGGER_T2_ACTION_FAIL_*. */
    uint32_t action_fail_reason[TRIGGER_T2_ACTION_FAIL_COUNT];
    uint32_t action_sequence[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    /* Per-slot group sequence progress (T2-SEQ-001): the group count of the
     * mounted role, the next group Core0 prepares, the group currently in
     * flight, and which groups already completed. A position counts as executed
     * only after its LAST group completes; intermediate completions advance the
     * cursor instead of marking the position. Kept in bytes: the sequence had to
     * fit inside the RAM the single-template runtime already used. */
    uint8_t action_group_count;
    uint8_t action_group_next[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint8_t action_group_in_flight[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint8_t action_group_done[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint32_t action_groups_executed;
    /* Last observed backend state and origin bracket width; unknown = UINT32_MAX. */
    uint32_t action_backend_state[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint32_t action_origin_width_ticks[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint64_t action_deadline_tick[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint32_t core0_prepare_calls, core0_preview_saved, core0_prepare_last_reason;
    uint32_t prediction_version_exhausted_mask;
    /* Core1 service peak cycles: cursor, receive (origin or follower), deadlines. */
    uint32_t segment_max_cycles[TRIGGER_T2_RUNTIME_SEGMENT_COUNT];
    /* Origin FREEZE: complete once every target reports it frozen; resent
     * until then, abandoned after the position window expires. */
    uint32_t freeze_complete_mask, freeze_abandoned_mask, freeze_resend_count;
    /* How much of the FREEZE lead was left when this board actually froze a
     * position, in coarse ordinals: min and last over the run, plus how often a
     * FREEZE was admitted at or after its own target. The design promise is "freeze
     * early, act later"; this is the budget the rest of the chain (lane, solve, arm)
     * has to fit into, so a late count can be read against it instead of guessed. */
    uint32_t freeze_lead_min_slices, freeze_lead_last_slices, freeze_lead_expired_count;
    /* The DECLARED lead the origin gave a position, packed min | last<<16 (target
     * ordinal minus the ordinal it announced at, coarse ordinals = ms). The per-mille
     * threshold only becomes a budget once it is measured on the running scan. */
    uint32_t declare_lead_pack;
    /* v1 freeze-chain segmentation: the remaining lead at the two moments this board
     * controls, packed as min | last<<16 (leads are far below 65536 ms). Origin:
     * stage_lead_pack is taken when the FREEZE is staged. Follower: arm_lead_pack when
     * the position is armed. Subtracting (declare-stage), (stage-follower freeze) and
     * (freeze-arm) separates the chain without changing the wire format. */
    uint32_t stage_lead_pack, arm_lead_pack;
    /* Stage 5 (origin only): the lead still left to the position's target at the moment
     * the follower's frozen state became VISIBLE here, i.e. when the refmem image
     * carrying the ack arrived. That image rides the path that does NOT use the fast
     * channel (the second-class seat), so
     *     ack round trip = stage_lead - ack_lead
     * is the only measurement of the second class in this chain; without it, seat time
     * reserved for that path can only be seen as a COST (first-class lead) and never as
     * a benefit. Packed as min | last<<16, pulses on the 1 ms grid, 0 = not observed. */
    uint32_t ack_lead_pack;
    /* Saturating count of positions whose FREEZE completion was abandoned after
     * the position window closed. The mask is per position and is cleared by the
     * next run, so it cannot answer "how often did this happen"; this counter
     * can. A first-class position that never completed its freeze is exactly the
     * loss the seat's own verdict must not report as healthy. */
    uint32_t freeze_abandoned_count;
    /* T2-FREEZE-002 (评审 A-13): positions whose PREVIEW was never saved, so their
     * FREEZE could not be staged. The mask names them; the counter is how many
     * positions this run lost that way (each position counted once). Staging keeps
     * examining the remaining positions instead of ending the pass on the first
     * one - the loss used to be silent and stopped every later position. */
    uint32_t freeze_no_preview_mask, freeze_no_preview_count;
    /* T2-PROVISIONAL-001 second position, consumed: how often a position was executed on
     * the NEXT target carried in the same slice because its FREEZE never arrived. It is
     * NOT freeze_fallback_count (that one counts a FREEZE whose version did not match the
     * local solve), and it is the only counter that answers "was the second position
     * actually used". */
    uint32_t fallback_promoted_count;
    /* Receive-side timestamp of the position record this engine is working on.
     * SEMANTICS: `rx_local_tick` is the local TIMER1 tick sampled at the instant this
     * board ADMITTED a new position record (the moment Core0 consumed a descriptor whose
     * version differs from the last published plan), and `rx_observation_seq` is the
     * cursor observation that projected it into the shared VDC coordinate. It is not the
     * transport lane's arrival tick (that one lives in distributed_refmem): T2 must record
     * when the record became USABLE locally, because every deadline is solved against that
     * same local time base.
     * PLACEMENT: one stamp per engine (a record carries up to two positions received at the
     * same instant), so a per-position array would only duplicate it. The local time of the
     * admission is projected from the local `action_cursor` through the same DCO/TIMER1
     * conversion every deadline uses - no extra clock source, no fabricated tick. A record
     * admitted after its own deadline had already passed (stale) is counted in
     * `rx_stale_count`, the observable form of "don't put an old receive timestamp into a
     * newer model". */
    uint64_t rx_local_tick;
    uint32_t rx_local_sample_seq, rx_timestamp_count, rx_stale_count;
    /* Monotonic source of the field above (also readable without a snapshot). */
    uint8_t event_dispatched[TRIGGER_T2_RUNTIME_EVENT_CAPACITY];
    uint64_t last_counter_count;
    trigger_time_cursor_snapshot_t cursor;
    trigger_turntable_predictor_snapshot_t predictor;
    trigger_action_runtime_event_t events[TRIGGER_T2_RUNTIME_EVENT_CAPACITY];
    trigger_position_descriptor_t descriptors[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    trigger_t2_runtime_result_t last_error;
} trigger_t2_runtime_snapshot_t;

/* Core0 STOP/configuration owner.  The runtime service itself is bounded and
 * called from the realtime trigger phase after RefMem RX has been serviced. */
/* Scan STEP in millidegrees, set for the NEXT run (STOP time only). Kept out of
 * trigger_t2_runtime_config_t on purpose: that struct is partially initialised all over
 * the tree and a new field breaks those initialisers under -Wextra -Werror. Until a
 * caller sets a step the rolling window stays disabled and the run is unchanged. */
bool trigger_t2_runtime_scan_step(int32_t step_mdeg);
/* Install the three announce gates (tolerance, per-position interval, global interval, all
 * in ms) at STOP. Returns false - leaving the previous values installed - for a 0 gate or
 * a value above its bound, so shaping can never be disabled by an empty write. The gates are
 * read on every announce evaluation, so an install takes effect from the next evaluation;
 * FREEZE and a position's FIRST announcement bypass them by design. */
bool trigger_t2_runtime_announce(uint32_t tolerance_ms, uint32_t min_interval_ms,
                                 uint32_t global_interval_ms);
/* Read the installed gates back. Returns false if no gate has been installed (which cannot
 * happen in practice - the defaults are installed at start-up), so a reader never gets a
 * silent zero. Any pointer may be NULL. */
bool trigger_t2_runtime_announce_get(uint32_t *tolerance_ms, uint32_t *min_interval_ms,
                                     uint32_t *global_interval_ms);
/* How many positions executed on the cached NEXT target because their FREEZE never
 * arrived (T2-PROVISIONAL-001). Distinct from freeze_fallback_count, which counts a
 * FREEZE whose version did not match the local solve. */
uint32_t trigger_t2_runtime_fallback_promoted_count(void);
/* One-shot diagnostics for the parallel-window investigation (A.3.6d): the configured
 * step, how often start() took the window branch, the window's own validation result and
 * step, and the derived geometry. Any pointer may be NULL. */
void trigger_t2_runtime_scan_diagnostics(uint32_t *step_mdeg, uint32_t *branch_entered,
                                         uint32_t *scan_result, uint32_t *window_step_mdeg,
                                         uint32_t *positions, uint32_t *step_pulses,
                                         uint32_t *window_entered, uint32_t *window_announced,
                                         uint32_t *window_overwrote, uint32_t *announce_mismatch,
                                         int32_t *motion_start_mdeg,
                                         int32_t *motion_stop_mdeg,
                                         uint32_t *motion_pulses,
                                         uint32_t *pre_roll_pulses,
                                         uint32_t *post_roll_pulses);
bool trigger_t2_runtime_configure(const trigger_t2_runtime_config_t *config);
bool trigger_t2_runtime_start(uint32_t count_epoch, uint64_t origin_count);
void trigger_t2_runtime_stop(void);
trigger_t2_runtime_result_t trigger_t2_runtime_service(void);
/* Per-run maxima (clk_sys cycles) of the origin FREEZE path. Diagnostic. */
enum {
    TRIGGER_T2_RUNTIME_FREEZE_TIMING_PENDING = 0u,
    TRIGGER_T2_RUNTIME_FREEZE_TIMING_STAGE = 1u,
    TRIGGER_T2_RUNTIME_FREEZE_TIMING_RESEND = 2u,
    TRIGGER_T2_RUNTIME_FREEZE_TIMING_TOTAL = 3u,
    TRIGGER_T2_RUNTIME_FREEZE_TIMING_COUNT = 4u,
};
bool trigger_t2_runtime_freeze_timing(
    uint32_t out[TRIGGER_T2_RUNTIME_FREEZE_TIMING_COUNT]);
/* Diagnostic maxima (clk_sys cycles) of the parts of service_origin(), which the
 * origin runs inside the SYNC_TRIGGER RECEIVE segment: the counter read, the
 * prediction observe, the rolling-window block and the FREEZE call. */
enum {
    TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNTER = 0u,
    TRIGGER_T2_RUNTIME_ORIGIN_PROBE_OBSERVE = 1u,
    TRIGGER_T2_RUNTIME_ORIGIN_PROBE_WINDOW = 2u,
    TRIGGER_T2_RUNTIME_ORIGIN_PROBE_FREEZE = 3u,
    TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNT = 4u,
};
bool trigger_t2_runtime_origin_probe(
    uint32_t out[TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNT]);
/* Core0 prediction and follower deadline preparation, outside the VDC phase. */
/* Counters for the "why is there no plan" question (T2-PROGRESS-20261018-043). */
typedef struct {
    uint32_t prepare_calls, prepare_last_reason;
    uint32_t no_descriptor, expand_fail, version_zero, published;
} trigger_t2_runtime_solve_stats_t;
bool trigger_t2_runtime_solve_stats(trigger_t2_runtime_solve_stats_t *out);
typedef struct {
    uint32_t advance, jump, reuse, no_receipt;
    /* Solver-entry skips: group unreachable, still in flight, position already accepted. */
    uint32_t skip_unreachable, skip_inflight, skip_accepted;
    /* Plan supply: publishes that reached the mailbox vs the ways a position bailed out. */
    uint32_t published, no_descriptor, version_zero, expand_fail;
    /* Plan timing versus the freeze of that slot. */
    uint32_t publish_before_freeze, publish_after_freeze;
    /* deadline_receive() exits: guard busy, no version, wrong session, older group, accepted. */
    uint32_t recv_busy, recv_no_version, recv_session, recv_older, recv_ok;
    /* Slot recycling: promoted from the stash, recycled by a newer preview, stash sets. */
    uint32_t recycle_promote, recycle_newer, stash_set;
    uint32_t freeze_hist[8];
    /* NO_PLAN split: version was 0, or at/below the slot floor. */
    uint32_t freeze_no_plan_zero, freeze_no_plan_floor;
    /* complete_action(): how many advances and finishes happened, and the last finish' group/total. */
    uint32_t action_advance, action_finish, finish_group, finish_total;
    /* Last window-branch evaluation: sequence, expected, window end, COMPLETE flag. */
    uint32_t window_seq, window_expected, window_last, window_complete;
    /* Window boundaries crossed (more groups than one window carries). */
    uint32_t window_boundary;
    /* BACKEND failure sources: identity mismatch, terminal/cursor state. */
    uint32_t backend_identity, backend_terminal, backend_arm;
    /* Groups handed to the executor by the last published plan. */
    uint32_t plan_window;
    /* complete_action() early returns: group out of range / no armed group. */
    uint32_t done_skip_range, done_skip_none;
    /* Last identity mismatch inputs (see trigger_t2_runtime.c). */
    uint32_t id_seq_last, id_expected_last, id_count;
    /* Why START refused: 0 guard, 1 grid cursor, 2 predictor configure, 3 predictor begin. */
    uint32_t start_reject[4];
    uint64_t id_deadline_last, id_recorded_last;
    /* Last three advance decisions: completed (bit31 = seen but not advanced), expected, window_last. */
    uint32_t done_hist[3][3];
} trigger_t2_runtime_window_stats_t;
bool trigger_t2_runtime_window_stats(trigger_t2_runtime_window_stats_t *out);

/* frozen, completed, failed, plan version, receive seen, group cursor. */
bool trigger_t2_runtime_slot_probe(uint32_t index, uint32_t out[6]);
void trigger_t2_runtime_prepare_core0(void);
bool trigger_t2_runtime_preview_history(uint32_t position_id, uint32_t age,
                                        trigger_position_descriptor_t *out);
bool trigger_t2_runtime_take_event(trigger_action_runtime_event_t *out);
/* Cumulative per-board position-window totals (Step 1, T2-DEPLOY-003). The
 * frozen/action masks are 3-bit and saturate, so they can never answer "how many
 * positions did this board consume"; these counters can. */
void trigger_t2_runtime_position_window_totals(uint32_t *entered, uint32_t *announced,
                                               uint32_t *frozen, uint32_t *executed,
                                               uint32_t *abandoned);
/* Refused window transitions: 0 on both means the marking call sites never ran. */
void trigger_t2_runtime_position_window_rejections(uint32_t *mark_rejected,
                                                   uint32_t *alloc_rejected);
bool trigger_t2_runtime_snapshot(trigger_t2_runtime_snapshot_t *out);
/* Packed group-sequence progress of one position slot (T2-SEQ-001):
 * bits 0-3 next group to prepare, bits 4-7 group in flight (0xF = none),
 * bits 8-15 bitmask of completed groups. One word per slot keeps the diagnostic
 * readable without growing the page by three words per slot. */
uint32_t trigger_t2_runtime_group_pack(uint32_t index);

#endif
