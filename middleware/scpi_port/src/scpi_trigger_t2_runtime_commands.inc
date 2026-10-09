#include "trigger_action_template_store.h"
#include "trigger_deployment_service.h"
#include "trigger_grid_preview.h"
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
    uint64_t pre_roll, post_roll, threshold;
    int32_t start_angle, zero_angle, terminal_angle;
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
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);

    trigger_grid_preview_snapshot_t grid;
    trigger_deployment_snapshot_t deployment;
    trigger_action_template_store_snapshot_t templates;
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

    trigger_t2_runtime_config_t config={0};
    config.grid=grid.grid;
    config.predictor=(trigger_turntable_predictor_config_t){
        .pulses_per_degree=1000u,
        .start_angle_mdeg=start_angle,
        .zero_angle_mdeg=zero_angle,
        .terminal_angle_mdeg=terminal_angle,
        .pre_roll_mdeg=(uint32_t)pre_roll,
        .post_roll_mdeg=(uint32_t)post_roll,
        .threshold_per_mille=(uint32_t)threshold};
    config.templates=templates.table;
    config.deployment=deployment.active.table;
    config.template_crc=templates.crc;
    config.run_id=(uint32_t)run_id;
    config.local_role=deployment.active.local_role;
    config.source_slot=(uint32_t)source_slot;
    config.target_mask=(uint32_t)target_mask;
    config.descriptor_window_sync=TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC;
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
    return SCPI_RES_OK;
}
