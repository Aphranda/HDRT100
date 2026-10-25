#include "scpi_action_probe.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "scpi_port_internal.h"
#include "scpi_system_snapshot_internal.h"

#include "distributed_action_probe.h"
scpi_result_t scpi_cmd_action_probe_configure(scpi_t *context)
{
    uint32_t session;
    if (!scpi_vdc_run_u32(context,&session) || !scpi_vdc_run_end(context) ||
        !distributed_action_probe_configure(session)) {
        scpi_port_push_exec_error(context,"ACTION_PROBE_STOP_DEPLOYMENT_SESSION");return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context,session);return SCPI_RES_OK;
}
scpi_result_t scpi_cmd_action_probe_send(scpi_t *context)
{
    uint32_t role,request,value;
    if (!scpi_vdc_run_u32(context,&role) || !scpi_vdc_run_u32(context,&request) ||
        !scpi_vdc_run_u32(context,&value) || !scpi_vdc_run_end(context) ||
        !distributed_action_probe_send(role,request,value)) {
        scpi_port_push_exec_error(context,"ACTION_PROBE_QUEUE_IDENTITY");return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context,request);return SCPI_RES_OK;
}
scpi_result_t scpi_cmd_action_probe_clear(scpi_t *context)
{
    if (!scpi_vdc_run_end(context) || !distributed_action_probe_clear()) {
        scpi_port_push_exec_error(context,"ACTION_PROBE_CLEAR_STOP");return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}
scpi_result_t scpi_cmd_action_probe_q(scpi_t *context)
{
    distributed_action_probe_status_t s;
    if (!scpi_vdc_run_end(context) || !distributed_action_probe_get(&s)) {
        scpi_port_push_exec_error(context,"ACTION_PROBE_SNAPSHOT_BUSY");return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context,2u);
    SCPI_ResultUInt32(context,s.configured);SCPI_ResultUInt32(context,s.session);
    SCPI_ResultUInt32(context,s.deployment_generation);SCPI_ResultUInt32(context,s.deployment_crc);
    SCPI_ResultUInt32(context,s.local_slot);SCPI_ResultUInt32(context,s.role);
    SCPI_ResultUInt32(context,s.sent);SCPI_ResultUInt32(context,s.received);
    SCPI_ResultUInt32(context,s.applied);SCPI_ResultUInt32(context,s.duplicates);SCPI_ResultUInt32(context,s.rejected);
    SCPI_ResultUInt32(context,s.queued);SCPI_ResultUInt32(context,s.completed);
    SCPI_ResultUInt32(context,s.last_request);SCPI_ResultUInt32(context,s.value);
    SCPI_ResultUInt32(context,s.ring_config);SCPI_ResultUInt32(context,s.cancelled);
    SCPI_ResultUInt32(context,s.last_completed_request);SCPI_ResultUInt32(context,s.completed_mask);
    SCPI_ResultUInt32(context,s.cancel_reason);SCPI_ResultUInt32(context,s.elapsed_ms);
    SCPI_ResultUInt32(context,s.completed_elapsed_ms);SCPI_ResultUInt32(context,s.last_result);
    return SCPI_RES_OK;
}
