/* Uses the strict integer/result helpers from scpi_trigger_grid_commands.inc. */
#include "trigger_deployment_service.h"

static bool scpi_trigger_deployment_u32(scpi_t *context, uint32_t *out)
{
    uint64_t value;
    if (!scpi_trigger_grid_uint(context, UINT32_MAX, &value)) return false;
    *out = (uint32_t)value;
    return true;
}

static void scpi_trigger_deployment_row(scpi_t *context, const trigger_deployment_row_t *row)
{
    SCPI_ResultUInt64(context, row->board_uid);
    SCPI_ResultUInt32(context, row->tdma_slot);
    SCPI_ResultUInt32(context, row->input_mask);
    SCPI_ResultUInt32(context, row->output_mask);
    SCPI_ResultUInt32(context, row->feedback_mode);
}

scpi_result_t scpi_cmd_trigger_deployment_stage(scpi_t *context)
{
    trigger_deployment_t table = {0};
    uint32_t crc;
    if (!scpi_trigger_deployment_u32(context, &table.generation) ||
        !scpi_trigger_deployment_u32(context, &crc))
        return scpi_trigger_grid_result(context, TRIGGER_DEPLOYMENT_ARGUMENT);
    for (uint32_t role = 0u; role < TRIGGER_DEPLOYMENT_ROLE_COUNT; ++role) {
        trigger_deployment_row_t *row = &table.rows[role];
        if (!scpi_trigger_grid_uint(context, UINT64_MAX, &row->board_uid) ||
            !scpi_trigger_deployment_u32(context, &row->tdma_slot) ||
            !scpi_trigger_deployment_u32(context, &row->input_mask) ||
            !scpi_trigger_deployment_u32(context, &row->output_mask) ||
            !scpi_trigger_deployment_u32(context, &row->feedback_mode))
            return scpi_trigger_grid_result(context, TRIGGER_DEPLOYMENT_ARGUMENT);
    }
    if (!scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context, TRIGGER_DEPLOYMENT_ARGUMENT);
    return scpi_trigger_grid_result(context, trigger_deployment_stage(&table, crc));
}

scpi_result_t scpi_cmd_trigger_deployment_activate(scpi_t *context)
{
    uint32_t generation, crc;
    if (!scpi_trigger_deployment_u32(context, &generation) ||
        !scpi_trigger_deployment_u32(context, &crc) || !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context, TRIGGER_DEPLOYMENT_ARGUMENT);
    return scpi_trigger_grid_result(context, trigger_deployment_activate(generation, crc));
}

scpi_result_t scpi_cmd_trigger_deployment_clear(scpi_t *context)
{
    return scpi_trigger_grid_result(context, scpi_trigger_grid_end(context) ?
        trigger_deployment_clear() : TRIGGER_DEPLOYMENT_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_deployment_q(scpi_t *context)
{
    trigger_deployment_snapshot_t snapshot;
    const uint32_t result = scpi_trigger_grid_end(context) ?
        trigger_deployment_snapshot(&snapshot) : TRIGGER_DEPLOYMENT_ARGUMENT;
    scpi_trigger_grid_result(context, result);
    if (result != TRIGGER_DEPLOYMENT_OK) return SCPI_RES_OK;
    SCPI_ResultUInt32(context, snapshot.active.valid);
    SCPI_ResultUInt32(context, snapshot.last_generation);
    SCPI_ResultUInt32(context, snapshot.staged_valid);
    SCPI_ResultUInt32(context, snapshot.staged_crc);
    SCPI_ResultUInt32(context, snapshot.active.crc);
    SCPI_ResultUInt64(context, snapshot.active.local_uid);
    SCPI_ResultUInt32(context, snapshot.active.local_role);
    SCPI_ResultUInt32(context, snapshot.active.ring_config);
    SCPI_ResultUInt32(context, snapshot.active.schedule_crc);
    SCPI_ResultUInt32(context, snapshot.active.local_slot);
    SCPI_ResultUInt32(context, snapshot.active.table.generation);
    SCPI_ResultUInt32(context, snapshot.staged.generation);
    for (uint32_t role = 0u; role < TRIGGER_DEPLOYMENT_ROLE_COUNT; ++role)
        scpi_trigger_deployment_row(context, &snapshot.active.table.rows[role]);
    for (uint32_t role = 0u; role < TRIGGER_DEPLOYMENT_ROLE_COUNT; ++role)
        scpi_trigger_deployment_row(context, &snapshot.staged.rows[role]);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_deployment_plan_q(scpi_t *context)
{
    uint32_t generation, role, epoch, grid_generation;
    uint64_t ordinal, minimum_lead, maximum_ahead;
    if (!scpi_trigger_deployment_u32(context, &generation) ||
        !scpi_trigger_deployment_u32(context, &role) ||
        !scpi_trigger_deployment_u32(context, &epoch) ||
        !scpi_trigger_deployment_u32(context, &grid_generation) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &ordinal) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &minimum_lead) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &maximum_ahead) ||
        !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context, TRIGGER_DEPLOYMENT_ARGUMENT);
    trigger_deployment_plan_t preview;
    const uint32_t result = trigger_deployment_plan(generation, role, epoch, grid_generation,
        ordinal, minimum_lead, maximum_ahead, &preview);
    scpi_trigger_grid_result(context, result);
    if (result != TRIGGER_DEPLOYMENT_OK) return SCPI_RES_OK;
    SCPI_ResultUInt32(context, preview.deployment.table.generation);
    SCPI_ResultUInt32(context, preview.deployment.crc);
    SCPI_ResultUInt32(context, preview.role);
    scpi_trigger_deployment_row(context, &preview.route);
    SCPI_ResultUInt32(context, preview.clock.configuration.grid.epoch);
    SCPI_ResultUInt32(context, preview.clock.configuration.grid.generation);
    SCPI_ResultUInt64(context, preview.clock.plan.ordinal);
    SCPI_ResultUInt64(context, preview.clock.plan.target_vdc_ns);
    SCPI_ResultUInt64(context, preview.clock.plan.deadline_ticks);
    SCPI_ResultUInt64(context, preview.clock.plan.projected_vdc_ns);
    SCPI_ResultUInt32(context, preview.clock.model.token);
    SCPI_ResultUInt32(context, preview.clock.model.dco.dco_update_seq);
    SCPI_ResultInt32(context, preview.clock.model.dco.period_adjust_ppb);
    SCPI_ResultUInt64(context, preview.clock.now_ticks);
    SCPI_ResultUInt64(context, preview.clock.checked_at_ticks);
    SCPI_ResultUInt64(context, preview.clock.minimum_lead_ticks);
    SCPI_ResultUInt32(context, preview.clock.tick_hz);
    SCPI_ResultUInt32(context, trigger_deployment_role_kind(preview.role));
    return SCPI_RES_OK;
}
