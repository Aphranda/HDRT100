#include "sync_io_gateway.h"
#include "sync_io_switch.h"
#include "trigger_action_template_store.h"
#include "trigger_deployment_service.h"
#include "trigger_grid_preview.h"
#include "trigger_sequence_timing.h"
#include "trigger_t2_runtime.h"
#include "tdma_seat_mount_store.h"

typedef struct {
    const trigger_t2_runtime_config_t *config;
    uint64_t origin_count;
    uint32_t count_epoch, operation;
} scpi_t2_stopped_request_t;

static bool scpi_t2_update_stopped(void *context)
{
    const scpi_t2_stopped_request_t *r=context;
    if (r->operation==0u) {
        const bool ok = trigger_t2_runtime_configure(r->config);
        uint32_t cur = 0u, cur_gen = 0u;
        tdma_seat_mount_store_status(&cur, &cur_gen);
        if (cur == 90u) tdma_seat_mount_store_note_reject(ok ? 100u : 101u);
        return ok;
    }
    if (r->operation==1u) {
        trigger_t2_runtime_snapshot_t state;
        if (!trigger_t2_runtime_snapshot(&state) || !state.configured || state.active) {
            tdma_seat_mount_store_note_reject(151u); return false;
        }
        if (!distributed_refmem_reset_position_stopped()) {
            tdma_seat_mount_store_note_reject(152u); return false;
        }
        const bool started = trigger_t2_runtime_start(r->count_epoch,r->origin_count);
        if (!started) tdma_seat_mount_store_note_reject(153u);
        return started;
    }
    trigger_t2_runtime_stop();
    return true;
}

static bool scpi_t2_stopped(scpi_t2_stopped_request_t *request)
{
    const bool ran = tdma_service_run_stopped_maintenance(tdma_runtime_owner_get(),
        scpi_t2_update_stopped,request);
    uint32_t seen = 0u, seen_gen = 0u;
    tdma_seat_mount_store_status(&seen, &seen_gen);
    if (!ran && seen == 90u) tdma_seat_mount_store_note_reject(102u);
    return ran;
}

static scpi_result_t scpi_trigger_t2_fail(scpi_t *context)
{
    return scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_t2_configure(scpi_t *context)
{
    /* Arm the diagnostic channel: the first code written after this baseline
     * is the real cause; later writes (e.g. the generic 102) must not mask it. */
    tdma_seat_mount_store_note_reject(90u);
    uint64_t run_id, source_slot, target_mask, origin;
    uint64_t pre_roll, post_roll, threshold, pulses_per_degree;
    int32_t start_angle, zero_angle, terminal_angle, step_mdeg;
    if (!scpi_trigger_grid_uint(context, UINT32_MAX, &run_id) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &source_slot) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &target_mask) ||
        !scpi_trigger_grid_uint(context, 1u, &origin) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &pre_roll) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &post_roll) ||
        !scpi_trigger_grid_uint(context, 999u, &threshold) ||
        SCPI_ParamInt32(context, &start_angle, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &zero_angle, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &terminal_angle, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &step_mdeg, TRUE) != TRUE ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &pulses_per_degree) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);

    /* Optional 13th field: the promised window in coarse ordinals (a descriptor is
     * valid until target + window). Older callers send 12 fields and keep the
     * built-in grace, so the freeze/window budget becomes a STOP-state install
     * parameter that can be tuned without a re-flash. */
    uint32_t window_field = 0u;
    const bool window_given = SCPI_ParamUInt32(context, &window_field, FALSE) == TRUE;
    if (window_given &&
        (window_field == 0u || window_field > TRIGGER_T2_DESCRIPTOR_WINDOW_MAX))
        return scpi_trigger_t2_fail(context);

    trigger_grid_preview_snapshot_t grid;
    trigger_deployment_snapshot_t deployment;
    trigger_action_template_store_snapshot_t templates;
    trigger_turntable_scan_geometry_t geometry;
    if (trigger_grid_preview_snapshot(&grid) || !grid.configured) {
        tdma_seat_mount_store_note_reject(91u);
        return scpi_trigger_t2_fail(context);
    }
    if (trigger_deployment_snapshot(&deployment) || !deployment.active.valid) {
        tdma_seat_mount_store_note_reject(92u);
        return scpi_trigger_t2_fail(context);
    }
    if (trigger_action_template_snapshot(&templates) || !templates.installed ||
        !templates.resources_loaded || !templates.binding_current) {
        tdma_seat_mount_store_note_reject(93u);
        return scpi_trigger_t2_fail(context);
    }

    /* The scan STEP cannot ride in config: that struct is partially initialised across
 * the tree and a new field breaks those initialisers under -Wextra -Werror. The
 * setter is STOP-only and rejects a non-positive step. */
    if (!trigger_t2_runtime_scan_step(step_mdeg)) return scpi_trigger_t2_fail(context);

    trigger_t2_runtime_config_t config={0};
    config.grid=grid.grid;
    config.predictor=(trigger_turntable_predictor_config_t){
        .pulses_per_degree=(uint32_t)pulses_per_degree,
        .start_angle_mdeg=start_angle,
        .zero_angle_mdeg=zero_angle,
        .terminal_angle_mdeg=terminal_angle,
        .pre_roll_mdeg=(uint32_t)pre_roll,
        .post_roll_mdeg=(uint32_t)post_roll,
        .threshold_per_mille=(uint32_t)threshold};
    /* Fail-closed: a scan whose STEP/rolls/ppd cannot be run must be refused HERE, not
     * discovered after ARM. The derived geometry (positions, whole step_pulses, travel)
     * is read back through T2? once the runtime enters the window at start. */
    if (trigger_turntable_scan_validate(&config.predictor, step_mdeg, 0u, &geometry) !=
        TRIGGER_TURNTABLE_SCAN_OK)
        return scpi_trigger_t2_fail(context);
    /* A: a declaration lead thinner than the measured floor cannot be saved by anything
     * downstream - it fails the configure instead of the run. */
    if (geometry.step_pulses * (1000u - threshold) / 1000u <
        TRIGGER_T2_MIN_DECLARE_LEAD_PULSES)
        return scpi_trigger_t2_fail(context);
    config.templates=templates.table;
    config.deployment=deployment.active.table;
    config.template_crc=templates.crc;
    config.run_id=(uint32_t)run_id;
    config.local_role=deployment.active.local_role;
    config.source_slot=(uint32_t)source_slot;
    config.target_mask=(uint32_t)target_mask;
    config.descriptor_window_sync = window_given ? window_field :
        TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC;
    config.origin=(uint32_t)origin;
    scpi_t2_stopped_request_t request={.config=&config};
    return scpi_trigger_grid_result(context,
        scpi_t2_stopped(&request) ? TRIGGER_T2_RUNTIME_OK :
        TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_t2_start(scpi_t *context)
{
    tdma_seat_mount_store_note_reject(90u);
    uint64_t count_epoch, origin_count;
    if (!scpi_trigger_grid_uint(context, UINT32_MAX, &count_epoch) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &origin_count) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    scpi_t2_stopped_request_t request={.operation=1u,
        .count_epoch=(uint32_t)count_epoch,.origin_count=origin_count};
    return scpi_trigger_grid_result(context,
        scpi_t2_stopped(&request) ?
        TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_NOT_CONFIGURED);
}

scpi_result_t scpi_cmd_trigger_t2_stop(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    scpi_t2_stopped_request_t request={.operation=2u};
    return scpi_trigger_grid_result(context,scpi_t2_stopped(&request) ?
        TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_t2_q(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    trigger_t2_runtime_snapshot_t s;
    if (!trigger_t2_runtime_snapshot(&s)) return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, s.last_error);
    SCPI_ResultUInt32(context,s.configured);
    SCPI_ResultUInt32(context,s.active);
    SCPI_ResultUInt32(context,s.origin);
    SCPI_ResultUInt32(context,s.source_slot);
    SCPI_ResultUInt32(context,s.target_mask);
    SCPI_ResultUInt32(context,s.run_id);
    SCPI_ResultUInt32(context,s.last_sync_sequence);
    SCPI_ResultUInt32(context,s.published_mask);
    SCPI_ResultUInt32(context,s.received_mask);
    SCPI_ResultUInt32(context,s.descriptor_reject_count);
    SCPI_ResultUInt32(context,s.transport_reject_count);
    SCPI_ResultUInt32(context,s.event_count);
    SCPI_ResultUInt32(context,s.event_dropped_count);
    SCPI_ResultUInt32(context,s.frozen_mask);
    SCPI_ResultUInt32(context,s.freeze_mismatch_count);
    SCPI_ResultUInt32(context,s.freeze_fallback_count);
    SCPI_ResultUInt32(context,s.freeze_missing_local_count);
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++i) {
        SCPI_ResultUInt32(context,s.freeze_version[i]);
        SCPI_ResultUInt32(context,s.freeze_local_version[i]);
        SCPI_ResultUInt32(context,s.freeze_local_age[i]);
    }
    SCPI_ResultUInt64(context,s.last_counter_count);
    SCPI_ResultUInt32(context,s.cursor.pulse_index > UINT32_MAX ?
        UINT32_MAX : (uint32_t)s.cursor.pulse_index);
    SCPI_ResultUInt32(context,s.cursor.fine_slice_index);
    /* STOP/RUN diagnostic: count model state explains a missed threshold
     * without turning predictor facts into a wire payload. */
    SCPI_ResultUInt32(context,s.predictor.announced_mask);
    for (uint32_t i=0u;i<TRIGGER_T2_RUNTIME_POSITION_COUNT;++i) {
        SCPI_ResultUInt64(context,s.predictor.target_count[i]);
        SCPI_ResultUInt64(context,s.predictor.threshold_count[i]);
    }
    SCPI_ResultUInt32(context,s.predictor.last_error);
    SCPI_ResultUInt32(context,s.core0_prepare_calls);
    SCPI_ResultUInt32(context,s.core0_preview_saved);
    SCPI_ResultUInt32(context,s.core0_prepare_last_reason);
    for (uint32_t i=0u;i<TRIGGER_T2_RUNTIME_SEGMENT_COUNT;++i)
        SCPI_ResultUInt32(context,s.segment_max_cycles[i]);
    SCPI_ResultUInt32(context,s.freeze_complete_mask);
    SCPI_ResultUInt32(context,s.freeze_abandoned_mask);
    SCPI_ResultUInt32(context,s.freeze_resend_count);
    /* Appended: how often a FREEZE completion was abandoned, which the per-run
     * mask above cannot answer. Positional readers keep their indices. */
    SCPI_ResultUInt32(context,s.freeze_abandoned_count);
    /* Appended: a well-formed position that arrived after its promised window.
     * It is still inside descriptor_reject_count above; this field separates
     * lateness from identity/grid/deployment causes. */
    SCPI_ResultUInt32(context,s.late_arrival_count);
    /* Appended: the magnitude of the most recent late arrival (arrival ordinal
     * and the window it missed), so lateness is readable and not just counted. */
    SCPI_ResultUInt32(context,s.late_last_current_ms);
    SCPI_ResultUInt32(context,s.late_last_expiry_sync);
    /* Appended: the PROMISE itself, per position - the target ordinal the origin
     * asked for (low word: the consumer admits on the low word, exactly as the
     * runtime casts it) and the last ordinal that can still serve it. A lateness
     * counter without the promise it was measured against is not evidence. */
    for (uint32_t i=0u;i<TRIGGER_T2_RUNTIME_POSITION_COUNT;++i) {
        SCPI_ResultUInt32(context,(uint32_t)s.descriptors[i].target_ms_index);
        SCPI_ResultUInt32(context,s.descriptors[i].expiry_sync);
    }
    /* Appended: WHY a FREEZE is still being resent - the targets that do not
     * speak this run id, and the position bits nobody has frozen. */
    SCPI_ResultUInt32(context,s.freeze_ack_stale_mask);
    SCPI_ResultUInt32(context,s.freeze_ack_incomplete_mask);
    /* Appended: 1 when the configured mask lists the source slot itself, whose
     * bits can never be acknowledged and are therefore ignored. */
    SCPI_ResultUInt32(context,s.freeze_ack_own_in_mask);
    /* Appended: origin FREEZE path maxima in clk_sys cycles - pending check,
     * threshold stage, resend/ack pass, whole call. */
    uint32_t freeze_cycles[TRIGGER_T2_RUNTIME_FREEZE_TIMING_COUNT];
    if (!trigger_t2_runtime_freeze_timing(freeze_cycles))
        memset(freeze_cycles, 0, sizeof(freeze_cycles));
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_FREEZE_TIMING_COUNT; ++i)
        SCPI_ResultUInt32(context, freeze_cycles[i]);
    /* Appended: positions whose PREVIEW was never saved, so their FREEZE could not
     * be staged (T2-FREEZE-002 / A-13). The mask names them and the counter says how
     * many positions this run lost that way. Positional readers keep their indices. */
    SCPI_ResultUInt32(context, s.freeze_no_preview_mask);
    SCPI_ResultUInt32(context, s.freeze_no_preview_count);
    /* Appended last: the origin's SYNC_TRIGGER service split in clk_sys cycles -
     * counter read, prediction observe, rolling-window block, FREEZE call. The
     * RECEIVE segment maximum says the phase is over budget but not which part
     * spends it. Positional readers keep their indices. */
    uint32_t origin_probe[TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNT];
    if (!trigger_t2_runtime_origin_probe(origin_probe))
        memset(origin_probe, 0, sizeof(origin_probe));
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_ORIGIN_PROBE_COUNT; ++i)
        SCPI_ResultUInt32(context, origin_probe[i]);
    /* Appended: the SECOND POSITION actually used - a position executed on the next
     * target carried in the same slice because its FREEZE never arrived. This is not
     * freeze_fallback_count (version mismatch on the FREEZE path); it is the only field
     * that answers "how often did the second position rescue a position". */
    SCPI_ResultUInt32(context, s.fallback_promoted_count);
    /* Appended: receive-side timestamp of the record this engine admitted (see
     * trigger_t2_runtime.h). rx_local_tick is split into lo/hi because SCPI has no 64-bit
     * result type here; the pair is the local TIMER1 tick of the admission, the sequence is
     * the observation that produced the local action_cursor, and the two counters say how
     * many admissions were stamped and how many were already late. */
    SCPI_ResultUInt32(context, (uint32_t)(s.rx_local_tick & 0xFFFFFFFFu));
    SCPI_ResultUInt32(context, (uint32_t)(s.rx_local_tick >> 32u));
    SCPI_ResultUInt32(context, s.rx_local_sample_seq);
    SCPI_ResultUInt32(context, s.rx_timestamp_count);
    SCPI_ResultUInt32(context, s.rx_stale_count);
    return SCPI_RES_OK;
}

/* Announce gates as install parameters instead of a re-flash: the tolerance, the
 * per-position interval and the global offer interval decide how many records the
 * single-depth first-class seat is offered, and the right value depends on the seat
 * turnaround of the ring that is running. A dedicated command (not another field of
 * T2:CONFigure) so no positional caller of the 12-field configure is disturbed, and a
 * dedicated query so no reader's positional assumption on T2? is invalidated. */
scpi_result_t scpi_cmd_trigger_t2_announce(scpi_t *context)
{
    uint64_t tolerance, min_interval, global_interval;
    if (!scpi_trigger_grid_uint(context, UINT32_MAX, &tolerance) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &min_interval) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &global_interval) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    return scpi_trigger_grid_result(context,
        trigger_t2_runtime_announce((uint32_t)tolerance, (uint32_t)min_interval,
            (uint32_t)global_interval) ? TRIGGER_T2_RUNTIME_OK :
        TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_t2_announce_q(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    uint32_t tolerance = 0u, min_interval = 0u, global_interval = 0u;
    const bool installed = trigger_t2_runtime_announce_get(&tolerance, &min_interval,
                                                           &global_interval);
    scpi_trigger_grid_result(context,
        installed ? TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_ARGUMENT);
    SCPI_ResultUInt32(context, tolerance);
    SCPI_ResultUInt32(context, min_interval);
    SCPI_ResultUInt32(context, global_interval);
    return SCPI_RES_OK;
}

/* T_drain: the fixed period at which the position transport takes the next pending value
 * out of the latest-value table. It is the knob that matches transmit traffic to the
 * measured seat turnaround, and it is a run-time install parameter so a sweep costs no
 * re-flash. 0 = drain as fast as the seat frees. */
/* ---- multi-sync sequence timing (TRIGger:SEQuence:*) --------------------------------
 * One action = switch A + switch B on the same preset edge, wait max(A,B), lead C, TRIG pulse
 * D, analyzer response E; READY IS the next action's preset edge (no extra wait window).
 * Every value is an install parameter: changing it only moves the sequence's edges. */

static bool scpi_sequence_u32(scpi_t *context, uint32_t max, uint32_t *out)
{
    uint64_t value;
    if (!scpi_trigger_grid_uint(context, max, &value)) return false;
    *out = (uint32_t)value;
    return true;
}

scpi_result_t scpi_cmd_trigger_sequence_timing(scpi_t *context)
{
    trigger_sequence_timing_t t;
    if (!trigger_sequence_timing_get(&t)) return scpi_trigger_t2_fail(context);
    if (!scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_US, &t.settle_a_us) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_US, &t.settle_b_us) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_US, &t.lead_us) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_US, &t.trig_us) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_US, &t.vna_us) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_US, &t.move_us) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    return scpi_trigger_grid_result(context,
        trigger_sequence_timing_set(&t) == TRIGGER_SEQUENCE_TIMING_OK ?
        TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_sequence_timing_q(scpi_t *context)
{
    trigger_sequence_timing_t t;
    if (!trigger_sequence_timing_get(&t) || !scpi_trigger_grid_end(context))
        return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, t.settle_a_us);
    SCPI_ResultUInt32(context, t.settle_b_us);
    SCPI_ResultUInt32(context, t.lead_us);
    SCPI_ResultUInt32(context, t.trig_us);
    SCPI_ResultUInt32(context, t.vna_us);
    SCPI_ResultUInt32(context, t.move_us);
    return SCPI_RES_OK;
}

/* Channel count, polarization count and grid slice: the shape of the action table. */
scpi_result_t scpi_cmd_trigger_sequence_counts(scpi_t *context)
{
    trigger_sequence_timing_t t;
    if (!trigger_sequence_timing_get(&t)) return scpi_trigger_t2_fail(context);
    if (!scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_CHANNELS, &t.channels) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_POLARIZATIONS,
            &t.polarizations) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_GRID_NS, &t.grid_ns) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    return scpi_trigger_grid_result(context,
        trigger_sequence_timing_set(&t) == TRIGGER_SEQUENCE_TIMING_OK ?
        TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_sequence_counts_q(scpi_t *context)
{
    trigger_sequence_timing_t t;
    if (!trigger_sequence_timing_get(&t) || !scpi_trigger_grid_end(context))
        return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, t.channels);
    SCPI_ResultUInt32(context, t.polarizations);
    SCPI_ResultUInt32(context, t.grid_ns);
    return SCPI_RES_OK;
}

/* Derived periods: what the installed timing actually means. Read-only. */
scpi_result_t scpi_cmd_trigger_sequence_period_q(scpi_t *context)
{
    trigger_sequence_timing_t t;
    if (!trigger_sequence_timing_get(&t) || !scpi_trigger_grid_end(context))
        return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, trigger_sequence_timing_actions(&t));
    SCPI_ResultUInt32(context, trigger_sequence_timing_action_us(&t));
    SCPI_ResultUInt32(context, trigger_sequence_timing_position_us(&t));
    return SCPI_RES_OK;
}

/* One action's code pair (A channel, B polarization), and how many times the table repeats. */
scpi_result_t scpi_cmd_trigger_sequence_code(scpi_t *context)
{
    uint32_t action, channel, polarization;
    if (!scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_ACTIONS - 1u, &action) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_CHANNELS, &channel) ||
        !scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_POLARIZATIONS, &polarization) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    return scpi_trigger_grid_result(context,
        trigger_sequence_code_set(action, channel, polarization) == TRIGGER_SEQUENCE_CODE_OK ?
        TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_sequence_code_q(scpi_t *context)
{
    uint32_t action, channel = 0u, polarization = 0u;
    if (!scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_ACTIONS - 1u, &action) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    if (!trigger_sequence_code_get(action, &channel, &polarization))
        return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, channel);
    SCPI_ResultUInt32(context, polarization);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_sequence_repeat(scpi_t *context)
{
    uint32_t repeat;
    if (!scpi_sequence_u32(context, TRIGGER_SEQUENCE_TIMING_MAX_REPEAT, &repeat) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    return scpi_trigger_grid_result(context,
        trigger_sequence_code_repeat_set(repeat) == TRIGGER_SEQUENCE_CODE_OK ?
        TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_sequence_repeat_q(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, trigger_sequence_code_repeat());
    SCPI_ResultUInt32(context, trigger_sequence_timing_position_actions_total());
    return SCPI_RES_OK;
}

/* 1 (default) = bench-accurate: an unanswered READY blocks the next action. 0 = diagnostic mode
 * for a rig without an analyzer: the pending READY is retired and COUNTED as a collision, so the
 * sequence and the whole position chain can be exercised. */
scpi_result_t scpi_cmd_trigger_sequence_ready(scpi_t *context)
{
    trigger_sequence_timing_t t;
    if (!trigger_sequence_timing_get(&t)) return scpi_trigger_t2_fail(context);
    if (!scpi_sequence_u32(context, 1u, &t.ready_required) || !scpi_trigger_grid_end(context))
        return scpi_trigger_t2_fail(context);
    if (trigger_sequence_timing_set(&t) != TRIGGER_SEQUENCE_TIMING_OK)
        return scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_ARGUMENT);
    sync_io_gateway_realtime_set_ready_required(t.ready_required != 0u);
    return scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
}

scpi_result_t scpi_cmd_trigger_sequence_ready_q(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, sync_io_gateway_realtime_ready_required() ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_t2_drain(scpi_t *context)
{
    uint64_t drain_ms;
    if (!scpi_trigger_grid_uint(context, UINT32_MAX, &drain_ms) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    return scpi_trigger_grid_result(context,
        distributed_refmem_position_drain_ms((uint32_t)drain_ms) ?
        TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_t2_drain_q(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    uint32_t drain_ms = 0u;
    distributed_refmem_position_drain_get(&drain_ms);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, drain_ms);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_t2_cursor_q(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    trigger_time_cursor_snapshot_t cursor;
    if (!trigger_time_cursor_snapshot(&cursor)) return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, cursor.last_error);
    SCPI_ResultUInt32(context, cursor.configured);
    SCPI_ResultUInt32(context, cursor.valid);
    SCPI_ResultUInt32(context, cursor.grid_epoch);
    SCPI_ResultUInt32(context, cursor.grid_generation);
    SCPI_ResultUInt32(context, cursor.session);
    SCPI_ResultUInt32(context, cursor.sample_seq);
    SCPI_ResultUInt32(context, cursor.carrier_sequence);
    SCPI_ResultUInt32(context, cursor.identity_crc32);
    SCPI_ResultUInt32(context, cursor.ring_config_seq);
    SCPI_ResultUInt32(context, cursor.model_token);
    SCPI_ResultUInt32(context, cursor.dco_update_seq);
    SCPI_ResultUInt64(context, cursor.event_local_ns);
    SCPI_ResultUInt64(context, cursor.event_vdc_ns);
    SCPI_ResultUInt64(context, cursor.action_cursor);
    SCPI_ResultUInt64(context, cursor.pulse_index);
    SCPI_ResultUInt32(context, cursor.missed_sync_count);
    /* Appended: the first sticky binding fault - site (0 = none), error,
     * ring enabled/adapter/data/training bits, differs mask, sample_seq, live ring
     * config, live model token. Positional readers above keep their indices. */
    trigger_time_cursor_fault_t fault;
    if (!trigger_time_cursor_fault_snapshot(&fault)) memset(&fault, 0, sizeof(fault));
    SCPI_ResultUInt32(context, fault.flags & 0xfu);
    SCPI_ResultUInt32(context, (fault.flags >> 4u) & 0xfu);
    SCPI_ResultUInt32(context, (fault.flags >> 8u) & 0xfu);
    SCPI_ResultUInt32(context, fault.flags >> 16u);
    SCPI_ResultUInt32(context, fault.sample_seq);
    SCPI_ResultUInt32(context, fault.ring_config);
    SCPI_ResultUInt32(context, fault.model_token);
    return SCPI_RES_OK;
}

/* One-shot diagnostics for the parallel rolling window (TRIGGER_T2_TODO.md A.3.6d):
 * the configured scan step, how often start() took the window branch, the window's own
 * validation result and step, and the derived geometry. A dedicated query - not another
 * field appended to T2? - so no reader's positional assumption can be invalidated. */
scpi_result_t scpi_cmd_trigger_t2_scan_q(scpi_t *context)
{
    uint32_t step_mdeg = 0u, branch_entered = 0u, scan_result = 0u;
    uint32_t window_step_mdeg = 0u, positions = 0u, step_pulses = 0u;
    uint32_t window_entered = 0u, window_announced = 0u, window_overwrote = 0u;
    uint32_t announce_mismatch = 0u;
    int32_t motion_start_mdeg = 0, motion_stop_mdeg = 0;
    uint32_t motion_pulses = 0u, pre_roll_pulses = 0u, post_roll_pulses = 0u;
    trigger_t2_runtime_scan_diagnostics(&step_mdeg, &branch_entered, &scan_result,
                                        &window_step_mdeg, &positions, &step_pulses,
                                        &window_entered, &window_announced, &window_overwrote,
                                        &announce_mismatch, &motion_start_mdeg,
                                        &motion_stop_mdeg, &motion_pulses,
                                        &pre_roll_pulses, &post_roll_pulses);
    SCPI_ResultUInt32(context, step_mdeg);
    SCPI_ResultUInt32(context, branch_entered);
    SCPI_ResultUInt32(context, scan_result);
    SCPI_ResultUInt32(context, window_step_mdeg);
    SCPI_ResultUInt32(context, positions);
    SCPI_ResultUInt32(context, step_pulses);
    SCPI_ResultUInt32(context, window_entered);
    SCPI_ResultUInt32(context, window_announced);
    SCPI_ResultUInt32(context, window_overwrote);
    SCPI_ResultUInt32(context, announce_mismatch);
    /* Motion geometry of the configured scan (A.3.46): travel
     * [start - pre_roll, terminal + post_roll] in mdeg, plus its pulse budget. */
    SCPI_ResultInt32(context, motion_start_mdeg);
    SCPI_ResultInt32(context, motion_stop_mdeg);
    SCPI_ResultUInt32(context, motion_pulses);
    SCPI_ResultUInt32(context, pre_roll_pulses);
    SCPI_ResultUInt32(context, post_roll_pulses);
    return SCPI_RES_OK;
}

/* Narrow diagnostic: why multi-group positions failed, and how far their group sequence got.
 * Kept short on purpose - T2:ACTion? is truncated by the tool's read window long before the
 * fail reasons, so this page is the only practical way to see them on the bench. */
/* The staged window, on its own page: plan group count, fed cursor, queue depth, switch state,
 * last completed sequence. Self-contained so its field order cannot drift with other appends. */
/* Self-describing diagnostics: schema word first, then a FIXED field list, so a reading can never be
 * mis-aligned by fields appended elsewhere. */
#define T2_ACTION_DIAG_SCHEMA 0xDA01u
/* Gateway-only diagnostics: its own page, so the field order cannot be shifted by other appends. */
#define T2_GATEWAY_DIAG_SCHEMA 0xDA02u
scpi_result_t scpi_cmd_trigger_t2_gateway_diag_q(scpi_t *context)
{
    uint32_t fields[15] = {0u};
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    sync_io_gateway_realtime_diag(fields);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, T2_GATEWAY_DIAG_SCHEMA);
    for (uint32_t i = 0u; i < 15u; ++i) SCPI_ResultUInt32(context, fields[i]);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_t2_action_diag_q(scpi_t *context)
{
    trigger_t2_runtime_window_stats_t st;
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    if (!trigger_t2_runtime_window_stats(&st)) return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, T2_ACTION_DIAG_SCHEMA);
    SCPI_ResultUInt32(context, st.action_advance);
    SCPI_ResultUInt32(context, st.action_finish);
    SCPI_ResultUInt32(context, st.finish_group);
    SCPI_ResultUInt32(context, st.finish_total);
    SCPI_ResultUInt32(context, st.window_seq);
    SCPI_ResultUInt32(context, st.window_expected);
    SCPI_ResultUInt32(context, st.window_last);
    SCPI_ResultUInt32(context, st.window_complete);
    SCPI_ResultUInt32(context, st.freeze_no_plan_zero);
    SCPI_ResultUInt32(context, st.freeze_no_plan_floor);
    SCPI_ResultUInt32(context, st.window_seq);
    SCPI_ResultUInt32(context, st.window_complete);
    SCPI_ResultUInt32(context, st.window_boundary);
    SCPI_ResultUInt32(context, st.backend_identity);
    SCPI_ResultUInt32(context, st.backend_terminal);
    SCPI_ResultUInt32(context, st.backend_arm);
    SCPI_ResultUInt32(context, st.plan_window);
    /* Gateway staged-table refusals: NO_LEASE, BAD_COUNT, BAD_OFFSET, BAD_DELTA, BUSY, READY,
     * BAD_PULSE, SCHEDULE. */
    {uint32_t reject[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
     sync_io_gateway_realtime_stage_reject(reject);
     for (uint32_t i = 0u; i < 8u; ++i) SCPI_ResultUInt32(context, reject[i]);}
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_t2_action_window_q(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    uint32_t count = 0u, fed = 0u, queued = 0u, state = 0u, last = 0u;
    sync_io_switch_realtime_sequence_state(&count, &fed, &queued, &state, &last);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, count);
    SCPI_ResultUInt32(context, fed);
    SCPI_ResultUInt32(context, queued);
    SCPI_ResultUInt32(context, state);
    SCPI_ResultUInt32(context, last);
    return SCPI_RES_OK;
}


scpi_result_t scpi_cmd_trigger_t2_action_why_q(scpi_t *context)
{
    trigger_t2_runtime_snapshot_t s;
    if (!trigger_t2_runtime_snapshot(&s) || !scpi_trigger_grid_end(context))
        return scpi_trigger_t2_fail(context);
    scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
    SCPI_ResultUInt32(context, s.action_groups_executed);
    SCPI_ResultUInt32(context, s.action_group_count);
    SCPI_ResultUInt32(context, trigger_t2_runtime_group_pack(0u));
    SCPI_ResultUInt32(context, trigger_t2_runtime_group_pack(1u));
    SCPI_ResultUInt32(context, trigger_t2_runtime_group_pack(2u));
    for (uint32_t reason = 0u; reason < TRIGGER_T2_ACTION_FAIL_COUNT; ++reason)
        SCPI_ResultUInt32(context, s.action_fail_reason[reason]);
    /* Why the switch refused a whole-table arm: NO_READY, BUSY, CLOCK, TICK, PLAN, ORIGIN. */
    {uint32_t reject[7]; sync_io_switch_realtime_stage_reject(reject);
     for (uint32_t i=0u;i<7u;++i) SCPI_ResultUInt32(context, reject[i]);}
    /* Staged window state: plan count, fed cursor, queue depth, switch state, last sequence. */
    {uint32_t count = 0u, fed = 0u, queued = 0u, state = 0u, last = 0u;
     sync_io_switch_realtime_sequence_state(&count, &fed, &queued, &state, &last);
     SCPI_ResultUInt32(context, count);
     SCPI_ResultUInt32(context, fed);
     SCPI_ResultUInt32(context, queued);
     SCPI_ResultUInt32(context, state);
     SCPI_ResultUInt32(context, last);}
    /* Where positions lose their plan, plus Core0's own last reason code. */
    {trigger_t2_runtime_solve_stats_t st;
     if (trigger_t2_runtime_solve_stats(&st)) {
         SCPI_ResultUInt32(context, st.prepare_calls);
         SCPI_ResultUInt32(context, st.prepare_last_reason);
         SCPI_ResultUInt32(context, st.no_descriptor);
         SCPI_ResultUInt32(context, st.expand_fail);
         SCPI_ResultUInt32(context, st.version_zero);
         SCPI_ResultUInt32(context, st.published);
     }}
    /* Last arm's measured span (cycles) and its bound: tells WHY the window check failed. */
    {uint32_t span=0u, bound=0u, late=0u;
     sync_io_switch_realtime_origin_span(&span,&bound,&late);
     SCPI_ResultUInt32(context, span); SCPI_ResultUInt32(context, bound);
     SCPI_ResultUInt32(context, late);}
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_t2_action_q(scpi_t *context)
{
    trigger_t2_runtime_snapshot_t s;
    if (!scpi_trigger_grid_end(context) || !trigger_t2_runtime_snapshot(&s))
        return scpi_trigger_t2_fail(context);
    SCPI_ResultUInt32(context,1u);
    SCPI_ResultUInt32(context,s.action_armed_mask);
    SCPI_ResultUInt32(context,s.action_completed_mask);
    SCPI_ResultUInt32(context,s.action_failed_mask);
    SCPI_ResultUInt32(context,s.action_uncertain_mask);
    for (uint32_t i=0u;i<TRIGGER_T2_RUNTIME_POSITION_COUNT;++i) {
        SCPI_ResultUInt32(context,s.action_sequence[i]);
        SCPI_ResultUInt64(context,s.action_deadline_tick[i]);
        SCPI_ResultUInt32(context,s.action_backend_state[i]);
        SCPI_ResultUInt32(context,s.action_origin_width_ticks[i]);
    }
    SCPI_ResultUInt32(context,s.event_count);
    for (uint32_t i=0u;i<s.event_count;++i) {
        SCPI_ResultUInt32(context,s.events[i].position_id);
        SCPI_ResultUInt64(context,s.events[i].action_q);
    }
    /* Appended: the rolling prediction window's own health facts. A scan must
     * announce every entered position exactly once (window_announced_count ==
     * window_entered_count) and must never replace an unannounced entry
     * (window_overwrote_unannounced_count == 0); late and rate-stale counts say how
     * often a position was entered already due, or announced without a usable rate.
     * Read-only; positional readers keep their indices. */
    SCPI_ResultUInt32(context,s.predictor.window_entered_count);
    SCPI_ResultUInt32(context,s.predictor.window_announced_count);
    SCPI_ResultUInt32(context,s.predictor.window_overwrote_unannounced_count);
    SCPI_ResultUInt32(context,s.predictor.window_late_entry_count);
    SCPI_ResultUInt32(context,s.predictor.window_recomputed_count);
    SCPI_ResultUInt32(context,s.predictor.window_rate_stale_count);
    SCPI_ResultUInt32(context,s.predictor.window_step_mdeg);
    SCPI_ResultUInt32(context,s.predictor.scan_geometry.positions);
    SCPI_ResultUInt32(context,s.predictor.scan_geometry.step_pulses);
    /* Appended last: cumulative position-window totals (Step 1). The 3-bit
     * frozen/action masks saturate, so these counters are the only readable
     * evidence of how many positions this board actually consumed. */
    {
        uint32_t w_entered=0u,w_announced=0u,w_frozen=0u,w_executed=0u,w_abandoned=0u;
        trigger_t2_runtime_position_window_totals(&w_entered,&w_announced,&w_frozen,
                                                  &w_executed,&w_abandoned);
        uint32_t w_mark_rejected=0u,w_alloc_rejected=0u;
        trigger_t2_runtime_position_window_rejections(&w_mark_rejected,&w_alloc_rejected);
        SCPI_ResultUInt32(context,w_entered);
        SCPI_ResultUInt32(context,w_announced);
        SCPI_ResultUInt32(context,w_frozen);
        SCPI_ResultUInt32(context,w_executed);
        SCPI_ResultUInt32(context,w_abandoned);
        SCPI_ResultUInt32(context,w_mark_rejected);
        SCPI_ResultUInt32(context,w_alloc_rejected);
    }
    /* Appended after the window totals: WHY each action_fail() happened. "361 published,
     * 331 executed" is only attributable with this split - a late arm belongs to the lane,
     * a missing plan to the local solve, a conflict to the position geometry and a refused
     * deadline to the backend. */
    for (uint32_t reason = 0u; reason < TRIGGER_T2_ACTION_FAIL_COUNT; ++reason)
        SCPI_ResultUInt32(context, s.action_fail_reason[reason]);
    /* Appended after the fail reasons: the per-position GROUP SEQUENCE progress of
     * T2-SEQ-001. Appending keeps every earlier index where it was. Without these a
     * sequence that arms but never advances is indistinguishable from one that never
     * armed: groups_executed counts completed groups, group_count the mounted role's
     * groups, and the remaining words pack, per position slot, the next group to
     * prepare (4 bits), the group in flight (4 bits, 0xF = none) and the bitmask of
     * completed groups (8 bits). */
    SCPI_ResultUInt32(context, s.action_groups_executed);
    SCPI_ResultUInt32(context, s.action_group_count);
    SCPI_ResultUInt32(context, trigger_t2_runtime_group_pack(0u));
    SCPI_ResultUInt32(context, trigger_t2_runtime_group_pack(1u));
    SCPI_ResultUInt32(context, trigger_t2_runtime_group_pack(2u));
    /* Appended after the group words: the FREEZE lead evidence of this board. The
     * design promise is "freeze early, act later", so these say how much budget the
     * rest of the chain actually had: the smallest and the newest lead left when a
     * position froze (coarse ordinals), how often a FREEZE was admitted at or after
     * its own target, and the window's resend/abandon pressure. */
    SCPI_ResultUInt32(context, s.freeze_lead_min_slices);
    SCPI_ResultUInt32(context, s.freeze_lead_last_slices);
    SCPI_ResultUInt32(context, s.freeze_lead_expired_count);
    SCPI_ResultUInt32(context, s.freeze_resend_count);
    SCPI_ResultUInt32(context, s.freeze_abandoned_count);
    /* A: the DECLARED lead this board actually gave (target ordinal minus announce
     * ordinal, in coarse ordinals = ms), minimum and last over the run. */
    /* v1 segmentation, all packed min|last<<16: declared lead at announce (origin),
     * lead at FREEZE staging (origin), lead at arm (follower). */
    SCPI_ResultUInt32(context, s.declare_lead_pack);
    SCPI_ResultUInt32(context, s.stage_lead_pack);
    SCPI_ResultUInt32(context, s.arm_lead_pack);
    /* Stage 5: origin-side lead when the frozen ack became visible (min | last<<16).
     * ack round trip = stage_lead_pack - ack_lead_pack; this is the only readout of the
     * path that does not use the fast channel, so the seat reservation for it can be
     * judged on time instead of only on the first-class cost. */
    SCPI_ResultUInt32(context, s.ack_lead_pack);
    return SCPI_RES_OK;
}
