#ifndef TRIGGER_T2_RUNTIME_H
#define TRIGGER_T2_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_action_runtime.h"
#include "trigger_time_grid.h"
#include "trigger_time_cursor.h"
#include "trigger_turntable_predictor.h"

#define TRIGGER_T2_RUNTIME_POSITION_COUNT 3u
enum {
    TRIGGER_T2_RUNTIME_SEGMENT_CURSOR = 0u,
    TRIGGER_T2_RUNTIME_SEGMENT_RECEIVE,
    TRIGGER_T2_RUNTIME_SEGMENT_DEADLINES,
    TRIGGER_T2_RUNTIME_SEGMENT_COUNT,
};
#define TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY 1u
/* One terminal receipt per executed position. A scan has many more positions than the
 * runtime has slots, and nothing drains this buffer in firmware (take_event is a host/
 * diagnostic reader), so it is sized for a short scan rather than for the slot count: the
 * event DETAIL is bounded, while event_count + event_dropped_count stays the total number
 * of receipts and the window's executed total stays the authoritative per-position count. */
#define TRIGGER_T2_RUNTIME_EVENT_CAPACITY 16u
#define TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC 2u
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

#endif
