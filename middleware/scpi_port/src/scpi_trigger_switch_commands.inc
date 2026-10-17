#include "trigger_switch_service.h"

scpi_result_t scpi_cmd_trigger_switch_load(scpi_t *context)
{
    uint32_t generation,crc;
    if (!scpi_trigger_deployment_u32(context,&generation) ||
        !scpi_trigger_deployment_u32(context,&crc) || !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context,SWITCH_ARGUMENT);
    return scpi_trigger_grid_result(context,trigger_switch_load(generation,crc));
}
scpi_result_t scpi_cmd_trigger_switch_stage(scpi_t *context)
{
    uint32_t code;
    if (!scpi_trigger_deployment_u32(context,&code) || !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context,SWITCH_ARGUMENT);
    return scpi_trigger_grid_result(context,trigger_switch_stage(code));
}
scpi_result_t scpi_cmd_trigger_switch_apply(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_switch_apply():SWITCH_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_switch_stop(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_switch_stop():SWITCH_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_switch_unload(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_switch_unload():SWITCH_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_switch_q(scpi_t *context)
{
    trigger_switch_snapshot_t s;
    const uint32_t result=scpi_trigger_grid_end(context)?trigger_switch_snapshot(&s):SWITCH_ARGUMENT;
    scpi_trigger_grid_result(context,result);
    if (result) return SCPI_RES_OK;
    SCPI_ResultUInt32(context,s.loaded);
    SCPI_ResultUInt32(context,s.binding.table.generation);
    SCPI_ResultUInt32(context,s.binding.crc);
    SCPI_ResultUInt64(context,s.binding.local_uid);
    SCPI_ResultUInt32(context,s.binding.local_role);
    SCPI_ResultUInt32(context,s.binding.local_slot);
    SCPI_ResultUInt32(context,s.binding.ring_config);
    SCPI_ResultUInt32(context,s.binding.schedule_crc);
    SCPI_ResultUInt32(context,s.io.schema);
    SCPI_ResultUInt32(context,s.io.generation);
    SCPI_ResultUInt32(context,s.io.state);
    SCPI_ResultUInt32(context,s.io.loaded);
    SCPI_ResultUInt32(context,s.io.output_mask);
    SCPI_ResultUInt32(context,s.io.group_count);
    SCPI_ResultUInt32(context,s.io.desired_code);
    SCPI_ResultUInt32(context,s.io.applied_code);
    SCPI_ResultUInt32(context,s.io.applied_valid);
    SCPI_ResultUInt32(context,s.io.stage_sequence);
    SCPI_ResultUInt32(context,s.io.completed_sequence);
    SCPI_ResultUInt32(context,s.io.cancelled_sequence);
    SCPI_ResultUInt32(context,s.io.sm_claimed_mask);
    SCPI_ResultUInt32(context,s.io.sm_enabled_mask);
    SCPI_ResultUInt32(context,s.io.instruction_words);
    SCPI_ResultUInt32(context,s.io.program_offset);
    SCPI_ResultUInt32(context,s.io.actual_output_mask);
    SCPI_ResultUInt32(context,s.io.receipt_mask);
    SCPI_ResultUInt32(context,s.io.fault);
    /* Appended: why a whole-table arm was refused (all zero = never refused). */
    for (uint32_t i=0;i<6u;++i) SCPI_ResultUInt32(context,s.io.stage_reject[i]);
    for (uint32_t i=0;i<SYNC_IO_SWITCH_MAX_GROUPS;++i) {
        SCPI_ResultUInt32(context,s.io.groups[i].sm);
        SCPI_ResultUInt32(context,s.io.groups[i].output_base_pin);
        SCPI_ResultUInt32(context,s.io.groups[i].output_count);
        SCPI_ResultUInt32(context,s.io.groups[i].code);
        SCPI_ResultUInt32(context,s.io.groups[i].tx_level);
        SCPI_ResultUInt32(context,s.io.groups[i].rx_level);
    }
    return SCPI_RES_OK;
}
