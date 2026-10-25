#include "trigger_action_service.h"

scpi_result_t scpi_cmd_trigger_action_install(scpi_t *context)
{
    trigger_action_table_t t={0};
    uint32_t crc;
    if (!scpi_trigger_deployment_u32(context,&t.generation) ||
        !scpi_trigger_deployment_u32(context,&crc) ||
        !scpi_trigger_deployment_u32(context,&t.dep_generation) ||
        !scpi_trigger_deployment_u32(context,&t.dep_crc) ||
        !scpi_trigger_deployment_u32(context,&t.grid.epoch) ||
        !scpi_trigger_deployment_u32(context,&t.grid.generation) ||
        !scpi_trigger_grid_uint(context,UINT64_MAX,&t.grid.anchor_vdc_ns) ||
        !scpi_trigger_deployment_u32(context,&t.grid.period_ns) ||
        !scpi_trigger_deployment_u32(context,&t.grid.subdivisions) ||
        !scpi_trigger_deployment_u32(context,&t.count) ||
        t.count>TRIGGER_ACTION_TABLE_CAPACITY)
        return scpi_trigger_grid_result(context,TRIGGER_ACTION_TABLE_ARGUMENT);
    for (uint32_t i=0;i<t.count;++i) {
        trigger_action_row_t *r=&t.rows[i];
        if (!scpi_trigger_deployment_u32(context,&r->action_id) ||
            !scpi_trigger_deployment_u32(context,&r->role) ||
            !scpi_trigger_deployment_u32(context,&r->kind) ||
            !scpi_trigger_deployment_u32(context,&r->argument) ||
            !scpi_trigger_grid_uint(context,UINT64_MAX,&r->ordinal) ||
            !scpi_trigger_deployment_u32(context,&r->span_slices))
            return scpi_trigger_grid_result(context,TRIGGER_ACTION_TABLE_ARGUMENT);
    }
    if (!scpi_trigger_grid_end(context)) {
        tdma_seat_mount_store_note_reject(20u);
        return scpi_trigger_grid_result(context, TRIGGER_ACTION_TABLE_ARGUMENT);
    }
    /* Surface the install verdict on the seat mount status channel so a bench
     * readout can name the failing check without a new page. */
    const uint32_t install_result = trigger_action_install(&t, crc);
    tdma_seat_mount_store_note_reject(20u + install_result);
    return scpi_trigger_grid_result(context, install_result);
}
scpi_result_t scpi_cmd_trigger_action_clear(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_action_clear():TRIGGER_ACTION_TABLE_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_action_q(scpi_t *context)
{
    trigger_action_snapshot_t s;
    uint32_t result=scpi_trigger_grid_end(context)?trigger_action_snapshot(&s):TRIGGER_ACTION_TABLE_ARGUMENT;
    scpi_trigger_grid_result(context,result);
    if (result) return SCPI_RES_OK;
    SCPI_ResultUInt32(context,s.installed);
    SCPI_ResultUInt32(context,s.state);
    SCPI_ResultUInt32(context,s.binding_current);
    SCPI_ResultUInt32(context,s.last_generation);
    SCPI_ResultUInt32(context,s.table.generation);
    SCPI_ResultUInt32(context,s.crc);
    SCPI_ResultUInt32(context,s.table.dep_generation);
    SCPI_ResultUInt32(context,s.table.dep_crc);
    SCPI_ResultUInt64(context,s.binding.local_uid);
    SCPI_ResultUInt32(context,s.binding.local_role);
    SCPI_ResultUInt32(context,s.binding.local_slot);
    SCPI_ResultUInt32(context,s.binding.ring_config);
    SCPI_ResultUInt32(context,s.binding.schedule_crc);
    SCPI_ResultUInt32(context,s.table.grid.epoch);
    SCPI_ResultUInt32(context,s.table.grid.generation);
    SCPI_ResultUInt64(context,s.table.grid.anchor_vdc_ns);
    SCPI_ResultUInt32(context,s.table.grid.period_ns);
    SCPI_ResultUInt32(context,s.table.grid.subdivisions);
    SCPI_ResultUInt32(context,s.table.count);
    SCPI_ResultUInt32(context,s.local_row_mask);
    SCPI_ResultUInt32(context,s.execution_authorized);
    SCPI_ResultUInt32(context,s.armed);
    SCPI_ResultUInt32(context,s.deadline_valid);
    SCPI_ResultUInt32(context,s.anchor_authenticated);
    return SCPI_RES_OK;
}
scpi_result_t scpi_cmd_trigger_action_row_q(scpi_t *context)
{
    uint32_t index;
    if (!scpi_trigger_deployment_u32(context,&index) || !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context,TRIGGER_ACTION_TABLE_ARGUMENT);
    trigger_action_row_t r;
    uint64_t target;
    uint32_t result=trigger_action_row(index,&r,&target);
    scpi_trigger_grid_result(context,result);
    if (result) return SCPI_RES_OK;
    SCPI_ResultUInt32(context,index);
    SCPI_ResultUInt32(context,r.action_id);
    SCPI_ResultUInt32(context,r.role);
    SCPI_ResultUInt32(context,r.kind);
    SCPI_ResultUInt32(context,r.argument);
    SCPI_ResultUInt64(context,r.ordinal);
    SCPI_ResultUInt32(context,r.span_slices);
    SCPI_ResultUInt64(context,target);
    return SCPI_RES_OK;
}
