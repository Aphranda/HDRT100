#include "scpi_config_commands.h"

#include "distributed_refmem.h"
#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_command_catalog.h"
#include "refmem_control_route.h"
#include "refmem_domain_config.h"
#include "sync_io_sequence.h"
#include "trigger_deployment_service.h"
#include "trigger_sequence_service.h"

scpi_result_t scpi_config_trigger_parameter_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 8u);
    SCPI_ResultUInt32(context, 2u);
    SCPI_ResultUInt32(context, 5u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 80u);
    SCPI_ResultUInt32(context, 0x11112222u);
    SCPI_ResultUInt32(context, 0x22223333u);
    SCPI_ResultUInt32(context, 0x33334444u);
    SCPI_ResultUInt32(context, 0x44445555u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_sweep_q(scpi_t *context)
{
    trigger_sequence_angle_t angle;
    trigger_sequence_service_get_angle(&angle);
    uint32_t count = 0u;
    if (angle.sweep_valid && angle.step_deg != 0.0f) {
        const float span = (angle.stop_deg - angle.start_deg) / angle.step_deg;
        if (span >= 0.0f && span < 1000000.0f) {
            count = (uint32_t)span + 1u;
        }
    }
    SCPI_ResultDouble(context, angle.start_deg);
    SCPI_ResultDouble(context, angle.stop_deg);
    SCPI_ResultDouble(context, angle.step_deg);
    SCPI_ResultUInt32(context, count);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_pulse_q(scpi_t *context)
{
    trigger_sequence_angle_t angle;
    trigger_sequence_service_get_angle(&angle);
    SCPI_ResultText(context, angle.pulse_edge == 0u ? "RISING" : "FALLING");
    SCPI_ResultUInt32(context, angle.pulse_width_us);
    SCPI_ResultUInt32(context, angle.pulse_timeout_ms);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, angle.pulse_valid ? "OK" : "UNCONFIGURED");
    SCPI_ResultBool(context, angle.pulse_valid ? TRUE : FALSE);
    SCPI_ResultDouble(context, angle.pulse_hz);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_position_q(scpi_t *context)
{
    SCPI_ResultText(context, "DTC_SWEEP");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 381u);
    SCPI_ResultInt32(context, -10);
    SCPI_ResultInt32(context, -9);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_breakpoint_q(scpi_t *context)
{
    trigger_sequence_angle_t angle;
    trigger_sequence_service_get_angle(&angle);
    SCPI_ResultDouble(context, angle.breakpoint_deg);
    SCPI_ResultBool(context, angle.breakpoint_valid ? TRUE : FALSE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_q(scpi_t *context)
{
    trigger_sequence_store_t *store = trigger_sequence_service_config();
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(store, NULL);
    if (plan == NULL || !plan->used) {
        SCPI_ResultText(context, "");
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultBool(context, FALSE);
        for (uint32_t i = 0u; i < 6u; ++i) {
            SCPI_ResultUInt32(context, 0u);
        }
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, plan->id);
    SCPI_ResultUInt32(context, plan->generation);
    SCPI_ResultUInt32(context, plan->crc);
    SCPI_ResultUInt32(context, plan->count);
    SCPI_ResultBool(context, TRUE);
    for (uint32_t i = 0u; i < 6u; ++i) {
        SCPI_ResultUInt32(context, i < plan->count ? plan->state_ids[i] : 0u);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_map_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_check_q(scpi_t *context)
{
    SCPI_ResultText(context, "PLAN_A");
    SCPI_ResultUInt32(context, 6u);
    SCPI_ResultUInt32(context, 0x02030405u);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_active_q(scpi_t *context)
{
    trigger_sequence_active_t active;
    const trigger_sequence_result_t result = trigger_sequence_get_active(
        trigger_sequence_service_config(), &active);
    if (result != TRIGGER_SEQUENCE_OK) {
        SCPI_ResultText(context, "");
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultBool(context, FALSE);
        SCPI_ResultText(context, "FAIL");
        SCPI_ResultText(context, trigger_sequence_result_name(result));
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, active.id);
    SCPI_ResultUInt32(context, active.crc);
    SCPI_ResultUInt32(context, active.count);
    SCPI_ResultBool(context, active.valid ? TRUE : FALSE);
    SCPI_ResultText(context, active.valid ? "PASS" : "FAIL");
    SCPI_ResultText(context, trigger_sequence_result_name(active.result));
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_switch_write(scpi_t *context)
{
    int32_t numbers[1];
    uint32_t value = 0u;
    SCPI_CommandNumbers(context, numbers, 1u, 1);
    const uint32_t number = (numbers[0] > 0) ? (uint32_t)numbers[0] : 1u;
    if (number < 1u || number > 2u ||
        !SCPI_ParamUInt32(context, &value, TRUE) ||
        (number == 1u && (value < 1u || value > 8u)) ||
        (number == 2u && value > 1u)) {
        scpi_port_push_exec_error(context, "BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }
    /* Outward alias of the unified control command: resolve the switch-type
     * route (configured via TRIGGER_SWITCH_ROUTE, else the deployment roles
     * SWITCH_A/B), then submit the same TRIGGER_SWITCH record. */
    uint32_t route = refmem_domain_config_switch_route(number);
    if (route == 0u) {
        trigger_deployment_snapshot_t snapshot;
        if (trigger_deployment_snapshot_cached(&snapshot) == 0u &&
            snapshot.active.valid) {
            const uint32_t role = (number == 1u)
                ? (uint32_t)TRIGGER_DEPLOYMENT_SLOT_SWITCH_A
                : (uint32_t)TRIGGER_DEPLOYMENT_SLOT_SWITCH_B;
            const uint32_t slot = snapshot.active.table.rows[role].tdma_slot;
            if (slot < 8u) {
                route = 1u << slot;
            }
        }
    }
    if (route == 0u) {
        scpi_port_push_exec_error(context, "BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }
    const uint32_t local_bit = 1u << distributed_refmem_local_slot();
    if ((route & ~local_bit) != 0u) {
        if (!distributed_refmem_ring_transport_ready()) {
            scpi_port_push_exec_error(context, "NOT_ONLINE_SAFE");
            return SCPI_RES_ERR;
        }
        uint32_t control_owner = 0u;
        const bool owner_valid =
            refmem_application_model_control_owner_slot(&control_owner);
        if (refmem_control_entry_decide(distributed_refmem_local_slot(),
                                        control_owner, owner_valid,
                                        route) ==
            REFMEM_CONTROL_ENTRY_REJECT_REMOTE) {
            scpi_port_push_exec_error(context, "NOT_CONTROL_OWNER");
            return SCPI_RES_ERR;
        }
    }
    const uint32_t params[2] = { number, value };
    uint32_t seq = 0u;
    if (!distributed_refmem_control_submit(
            (uint16_t)REFMEM_CMD_ID_TRIGGER_SWITCH, route, route,
            params, 2u, &seq)) {
        scpi_port_push_exec_error(context, "REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "QUEUED");
    SCPI_ResultUInt32(context, seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_switch_q(scpi_t *context)
{
    int32_t numbers[1];
    SCPI_CommandNumbers(context, numbers, 1u, 1);
    const uint32_t number = (numbers[0] > 0) ? (uint32_t)numbers[0] : 1u;
    /* number/valid/applied/value/desired/reserved: value is the physical
     * position (SWITCH1 read back from the SP8T lines, SWITCH2 from the last
     * applied value) and desired is the last commanded target. */
    const uint32_t value = sync_io_sequence_get_switch(number);
    const uint32_t desired = sync_io_sequence_get_switch_desired(number);
    SCPI_ResultInt32(context, numbers[0]);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, (value == desired) ? 1u : 0u);
    SCPI_ResultUInt32(context, value);
    SCPI_ResultUInt32(context, desired);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}
