/* STOP-only arithmetic preview. These commands confer no output authority. */
#include "trigger_grid_preview.h"

static bool scpi_trigger_grid_uint(scpi_t *context, uint64_t maximum, uint64_t *out)
{
    scpi_parameter_t parameter;
    if (!SCPI_Parameter(context, &parameter, TRUE) ||
        parameter.type != SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA || parameter.len <= 0 ||
        context->param_list.lex_state.pos != parameter.ptr + parameter.len) return false;
    /* The local lexer loses trailing numeric whitespace from aggregate length.
     * Reject it before a subsequent token can be silently truncated. Integers
     * never pass through double, including coordinates beyond 2^53. */
    int index = parameter.ptr[0] == '+' ? 1 : 0;
    if (index == parameter.len) return false;
    uint64_t value = 0u;
    for (; index < parameter.len; ++index) {
        const unsigned char digit = (unsigned char)parameter.ptr[index];
        if (digit < '0' || digit > '9' || value > (maximum - (digit - '0')) / 10u)
            return false;
        value = value * 10u + (digit - '0');
    }
    *out = value;
    return true;
}

static bool scpi_trigger_grid_end(scpi_t *context)
{
    scpi_parameter_t extra;
    return !SCPI_Parameter(context, &extra, FALSE) && !SCPI_ParamErrorOccurred(context);
}

static scpi_result_t scpi_trigger_grid_result(scpi_t *context, uint32_t result)
{
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, result);
    return SCPI_RES_OK;
}

static void scpi_trigger_grid_configuration(scpi_t *context,
    const trigger_grid_preview_snapshot_t *snapshot)
{
    SCPI_ResultUInt32(context, snapshot->configured);
    SCPI_ResultUInt32(context, snapshot->last_generation);
    SCPI_ResultUInt32(context, snapshot->grid.epoch);
    SCPI_ResultUInt32(context, snapshot->grid.generation);
    SCPI_ResultUInt64(context, snapshot->grid.anchor_vdc_ns);
    SCPI_ResultUInt32(context, snapshot->grid.period_ns);
    SCPI_ResultUInt32(context, snapshot->grid.subdivisions);
    SCPI_ResultUInt32(context, snapshot->session);
    SCPI_ResultUInt32(context, snapshot->ring_config);
    SCPI_ResultUInt32(context, snapshot->role_generation);
    SCPI_ResultUInt32(context, snapshot->clock_epoch);
    SCPI_ResultUInt32(context, snapshot->clock_run);
    SCPI_ResultUInt32(context, snapshot->local_slot);
    SCPI_ResultUInt32(context, snapshot->schedule_crc);
}

scpi_result_t scpi_cmd_trigger_grid_configure(scpi_t *context)
{
    uint64_t epoch, generation, anchor, period, subdivisions;
    if (!scpi_trigger_grid_uint(context, UINT32_MAX, &epoch) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &generation) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &anchor) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &period) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &subdivisions) ||
        !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context, TRIGGER_TIME_GRID_ARGUMENT);
    const trigger_time_grid_t grid = {.epoch = (uint32_t)epoch,
        .generation = (uint32_t)generation, .anchor_vdc_ns = anchor,
        .period_ns = (uint32_t)period, .subdivisions = (uint32_t)subdivisions};
    return scpi_trigger_grid_result(context, trigger_grid_preview_configure(&grid));
}

scpi_result_t scpi_cmd_trigger_grid_clear(scpi_t *context)
{
    return scpi_trigger_grid_result(context, scpi_trigger_grid_end(context) ?
        trigger_grid_preview_clear() : TRIGGER_TIME_GRID_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_grid_q(scpi_t *context)
{
    trigger_grid_preview_snapshot_t snapshot;
    const uint32_t result = scpi_trigger_grid_end(context) ?
        trigger_grid_preview_snapshot(&snapshot) : TRIGGER_TIME_GRID_ARGUMENT;
    scpi_trigger_grid_result(context, result);
    if (result == TRIGGER_TIME_GRID_OK) scpi_trigger_grid_configuration(context, &snapshot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_grid_plan_q(scpi_t *context)
{
    uint64_t epoch, generation, ordinal, minimum_lead, maximum_ahead;
    if (!scpi_trigger_grid_uint(context, UINT32_MAX, &epoch) ||
        !scpi_trigger_grid_uint(context, UINT32_MAX, &generation) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &ordinal) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &minimum_lead) ||
        !scpi_trigger_grid_uint(context, UINT64_MAX, &maximum_ahead) ||
        !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context, TRIGGER_TIME_GRID_ARGUMENT);
    trigger_grid_preview_plan_t preview;
    const uint32_t result = trigger_grid_preview_plan((uint32_t)epoch, (uint32_t)generation,
        ordinal, minimum_lead, maximum_ahead, &preview);
    scpi_trigger_grid_result(context, result);
    if (result != TRIGGER_TIME_GRID_OK) return SCPI_RES_OK;
    scpi_trigger_grid_configuration(context, &preview.configuration);
    SCPI_ResultUInt32(context, preview.model.token);
    SCPI_ResultUInt32(context, preview.model.dco.dco_update_seq);
    SCPI_ResultUInt32(context, preview.model.applied_command_seq);
    SCPI_ResultInt32(context, preview.model.dco.period_adjust_ppb);
    SCPI_ResultUInt64(context, preview.model.dco.base_local_tick64);
    SCPI_ResultUInt64(context, preview.model.dco.base_vdc_time64_ns);
    SCPI_ResultInt32(context, preview.model.dco.phase_offset_ns);
    SCPI_ResultUInt64(context, preview.model.valid_from_raw);
    SCPI_ResultUInt64(context, preview.plan.ordinal);
    SCPI_ResultUInt64(context, preview.plan.target_vdc_ns);
    SCPI_ResultUInt64(context, preview.plan.model_local_ns);
    SCPI_ResultUInt64(context, preview.plan.deadline_ticks);
    SCPI_ResultUInt64(context, preview.plan.projected_vdc_ns);
    SCPI_ResultUInt64(context, preview.now_ticks);
    SCPI_ResultUInt64(context, preview.checked_at_ticks);
    SCPI_ResultUInt64(context, preview.minimum_lead_ticks);
    SCPI_ResultUInt64(context, preview.maximum_ahead_ticks);
    SCPI_ResultUInt32(context, preview.tick_hz);
    return SCPI_RES_OK;
}
