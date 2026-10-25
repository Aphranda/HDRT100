#include "scpi_trigger_template_commands.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "trigger_action_template_store.h"
#include "tdma_seat_mount_store.h"
#include "scpi_trigger_deployment_commands.h"
#include "scpi_trigger_grid_commands.h"

static void scpi_trigger_template_row(scpi_t *context,
    const trigger_action_template_t *r)
{
    SCPI_ResultUInt32(context,r->template_id);
    SCPI_ResultUInt32(context,r->generation);
    SCPI_ResultUInt32(context,r->role);
    SCPI_ResultUInt32(context,r->kind);
    SCPI_ResultUInt32(context,r->argument);
    SCPI_ResultUInt32(context,r->span_slices);
    SCPI_ResultInt64(context,r->delta_q);
}

scpi_result_t scpi_cmd_trigger_template_install(scpi_t *context)
{
    trigger_action_template_table_t t={0}; uint32_t crc;
    if(!scpi_trigger_deployment_u32(context,&t.generation) ||
       !scpi_trigger_deployment_u32(context,&crc) ||
       !scpi_trigger_deployment_u32(context,&t.count) ||
       t.count==0u || t.count>TRIGGER_ACTION_TEMPLATE_CAPACITY)
        return scpi_trigger_grid_result(context,TRIGGER_ACTION_TEMPLATE_STORE_CHANGED);
    for(uint32_t i=0u;i<t.count;++i){
        trigger_action_template_t *r=&t.row[i]; int64_t delta;
        if(!scpi_trigger_deployment_u32(context,&r->template_id) ||
           !scpi_trigger_deployment_u32(context,&r->generation) ||
           !scpi_trigger_deployment_u32(context,&r->role) ||
           !scpi_trigger_deployment_u32(context,&r->kind) ||
           !scpi_trigger_deployment_u32(context,&r->argument) ||
           !scpi_trigger_deployment_u32(context,&r->span_slices) ||
           SCPI_ParamInt64(context,&delta,TRUE)!=TRUE){
            return scpi_trigger_grid_result(context,TRIGGER_ACTION_TEMPLATE_STORE_CHANGED);
        }
        r->delta_q=delta;
    }
    if(!scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context,TRIGGER_ACTION_TEMPLATE_STORE_CHANGED);
    const uint32_t template_result = trigger_action_template_install(&t, crc);
    /* Surface the verdict on the seat mount status channel for bench readout. */
    tdma_seat_mount_store_note_reject(70u + template_result);
    return scpi_trigger_grid_result(context, template_result);
}

scpi_result_t scpi_cmd_trigger_template_clear(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_action_template_clear():TRIGGER_ACTION_TEMPLATE_STORE_CHANGED);
}

scpi_result_t scpi_cmd_trigger_template_activate(scpi_t *context)
{
    const uint32_t activate_result = scpi_trigger_grid_end(context)?
        trigger_action_template_activate_resources():TRIGGER_ACTION_TEMPLATE_STORE_CHANGED;
    tdma_seat_mount_store_note_reject(80u + activate_result);
    return scpi_trigger_grid_result(context, activate_result);
}

scpi_result_t scpi_cmd_trigger_template_release(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_action_template_release_resources():TRIGGER_ACTION_TEMPLATE_STORE_CHANGED);
}

scpi_result_t scpi_cmd_trigger_template_q(scpi_t *context)
{
    trigger_action_template_store_snapshot_t s;
    const uint32_t result=scpi_trigger_grid_end(context)?
        trigger_action_template_snapshot(&s):TRIGGER_ACTION_TEMPLATE_STORE_CHANGED;
    scpi_trigger_grid_result(context,result);
    if(result) return SCPI_RES_OK;
    SCPI_ResultUInt32(context,s.installed);
    SCPI_ResultUInt32(context,s.binding_current);
    SCPI_ResultUInt32(context,s.resources_loaded);
    SCPI_ResultUInt32(context,s.generation);
    SCPI_ResultUInt32(context,s.crc);
    SCPI_ResultUInt32(context,s.last_generation);
    SCPI_ResultUInt32(context,s.table.count);
    for(uint32_t i=0u;i<s.table.count;++i) scpi_trigger_template_row(context,&s.table.row[i]);
    return SCPI_RES_OK;
}
