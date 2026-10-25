#include "scpi_trigger_action_execute.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "scpi_trigger_deployment_commands.h"
#include "scpi_trigger_grid_commands.h"
#include "trigger_action_service.h"

/* Finite diagnostic execution; raw observation is never an edge latch. */
static void scpi_action_model(scpi_t *context,const vdc_dpll_manager_committed_model_t *m)
{
    SCPI_ResultUInt32(context,m->token);
    SCPI_ResultUInt32(context,m->session);
    SCPI_ResultUInt32(context,m->role_generation);
    SCPI_ResultUInt32(context,m->clock_epoch_id);
    SCPI_ResultUInt32(context,m->clock_run_id);
    SCPI_ResultUInt32(context,m->local_slot);
    SCPI_ResultUInt64(context,m->valid_from_raw);
    SCPI_ResultUInt32(context,m->dco.dco_update_seq);
    SCPI_ResultUInt32(context,m->dco.tdma_schedule_crc32);
    SCPI_ResultUInt64(context,m->dco.base_local_tick64);
    SCPI_ResultUInt64(context,m->dco.base_vdc_time64_ns);
    SCPI_ResultInt32(context,m->dco.phase_offset_ns);
    SCPI_ResultInt32(context,m->dco.period_adjust_ppb);
    SCPI_ResultUInt32(context,m->dco.lock_state);
}
scpi_result_t scpi_cmd_trigger_action_prepare(scpi_t *context)
{
    uint32_t row;
    if (!scpi_trigger_deployment_u32(context,&row) || !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context,TRIGGER_ACTION_TABLE_ARGUMENT);
    return scpi_trigger_grid_result(context,trigger_action_execution_prepare(row));
}
scpi_result_t scpi_cmd_trigger_action_arm(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_action_execution_arm():TRIGGER_ACTION_TABLE_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_action_stop(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_action_execution_stop():TRIGGER_ACTION_TABLE_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_action_release(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_action_execution_release():TRIGGER_ACTION_TABLE_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_action_now_q(scpi_t *context)
{
    trigger_action_now_t s;
    uint32_t result=scpi_trigger_grid_end(context)?trigger_action_execution_now(&s):TRIGGER_ACTION_TABLE_ARGUMENT;
    scpi_trigger_grid_result(context,result);
    if (result) return SCPI_RES_OK;
    SCPI_ResultUInt32(context,s.tick_hz);
    SCPI_ResultUInt32(context,s.diagnostic);
    SCPI_ResultUInt32(context,s.anchor_authenticated);
    SCPI_ResultUInt64(context,s.raw_before_tick);
    SCPI_ResultUInt64(context,s.raw_after_tick);
    SCPI_ResultUInt64(context,s.vdc_before_ns);
    SCPI_ResultUInt64(context,s.vdc_after_ns);
    scpi_action_model(context,&s.model);
    return SCPI_RES_OK;
}
scpi_result_t scpi_cmd_trigger_action_execute_q(scpi_t *context)
{
    trigger_action_execution_snapshot_t s;
    uint32_t result=scpi_trigger_grid_end(context)?trigger_action_execution_snapshot(&s):TRIGGER_ACTION_TABLE_ARGUMENT;
    scpi_trigger_grid_result(context,result);
    if (result) return SCPI_RES_OK;
    SCPI_ResultUInt32(context,s.loaded);
    SCPI_ResultUInt32(context,s.row_index);
    SCPI_ResultUInt32(context,s.binding_current);
    SCPI_ResultUInt32(context,s.model_current);
    SCPI_ResultUInt32(context,s.diagnostic);
    SCPI_ResultUInt32(context,s.anchor_authenticated);
    SCPI_ResultUInt32(context,s.actual_event_valid);
    SCPI_ResultUInt32(context,s.installation.table.generation);
    SCPI_ResultUInt32(context,s.installation.crc);
    SCPI_ResultUInt64(context,s.installation.binding.local_uid);
    SCPI_ResultUInt32(context,s.installation.binding.local_role);
    SCPI_ResultUInt32(context,s.installation.binding.local_slot);
    SCPI_ResultUInt64(context,s.plan.ordinal);
    SCPI_ResultUInt64(context,s.plan.target_vdc_ns);
    SCPI_ResultUInt64(context,s.plan.deadline_ticks);
    SCPI_ResultUInt64(context,s.plan.projected_vdc_ns);
    SCPI_ResultUInt64(context,s.prepared_at_tick);
    SCPI_ResultUInt64(context,s.expires_at_tick);
    SCPI_ResultUInt64(context,s.arm_checked_tick);
    scpi_action_model(context,&s.model);
    SCPI_ResultUInt32(context,s.io.schema);
    SCPI_ResultUInt32(context,s.io.generation);
    SCPI_ResultUInt32(context,s.io.state);
    SCPI_ResultUInt32(context,s.io.loaded);
    SCPI_ResultUInt32(context,s.io.config.kind);
    SCPI_ResultUInt32(context,s.io.config.output_mask);
    SCPI_ResultUInt32(context,s.io.config.code);
    SCPI_ResultUInt32(context,s.io.config.input_port);
    SCPI_ResultUInt32(context,s.io.config.pulse_ns);
    SCPI_ResultUInt32(context,s.io.tick_hz);
    SCPI_ResultUInt32(context,s.io.group_count);
    SCPI_ResultUInt32(context,s.io.sm_claimed_mask);
    SCPI_ResultUInt32(context,s.io.sm_enabled_mask);
    SCPI_ResultUInt32(context,s.io.instruction_words);
    SCPI_ResultUInt32(context,s.io.receipts);
    SCPI_ResultUInt32(context,s.io.expected_receipts);
    SCPI_ResultUInt32(context,s.io.actual_output_mask);
    SCPI_ResultUInt32(context,s.io.ready_level);
    SCPI_ResultUInt32(context,s.io.ready_qualified);
    SCPI_ResultUInt32(context,s.io.fault);
    SCPI_ResultUInt64(context,s.io.deadline_tick);
    SCPI_ResultUInt64(context,s.io.falling_tick);
    SCPI_ResultUInt64(context,s.io.enable_before_tick);
    SCPI_ResultUInt64(context,s.io.enable_after_tick);
    SCPI_ResultUInt64(context,s.io.completed_observed_tick);
    return SCPI_RES_OK;
}
