#include "trigger_action_template_store.h"
#include "trigger_deployment_service.h"
#include "trigger_grid_preview.h"
#include "trigger_t2_runtime.h"

static scpi_result_t scpi_trigger_t2_fail(scpi_t *context)
{
    return scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_t2_configure(scpi_t *context)
{
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
    if (trigger_grid_preview_snapshot(&grid) || !grid.configured ||
        trigger_deployment_snapshot(&deployment) || !deployment.active.valid ||
        trigger_action_template_snapshot(&templates) || !templates.installed ||
        !templates.resources_loaded || !templates.binding_current)
        return scpi_trigger_t2_fail(context);

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
    return scpi_trigger_grid_result(context,
        trigger_t2_runtime_configure(&config) ? TRIGGER_T2_RUNTIME_OK :
        TRIGGER_T2_RUNTIME_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_t2_start(scpi_t *context)
{
    uint64_t count_epoch, origin_count;
    if (!scpi_trigger_grid_uint(context, UINT32_MAX, &count_epoch) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &origin_count) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    return scpi_trigger_grid_result(context,
        trigger_t2_runtime_start((uint32_t)count_epoch, origin_count) ?
        TRIGGER_T2_RUNTIME_OK : TRIGGER_T2_RUNTIME_NOT_CONFIGURED);
}

scpi_result_t scpi_cmd_trigger_t2_stop(scpi_t *context)
{
    if (!scpi_trigger_grid_end(context)) return scpi_trigger_t2_fail(context);
    trigger_t2_runtime_stop();
    return scpi_trigger_grid_result(context, TRIGGER_T2_RUNTIME_OK);
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
