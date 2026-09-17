#include "scpi_sequence_node_commands.h"

#include "scpi_system_snapshot_commands.h"
#include "trigger_sequence_service.h"
#include "scpi_port_internal.h"
#include "scpi_config_commands.h"
#include "refmem_application_model.h"
#include "distributed_refmem.h"
#include "sync_trigger.h"
#include "trigger_sequence_link.h"
#include "tdma_runtime_owner.h"

static bool sequence_node_config_allowed(scpi_t *context)
{
    if (trigger_sequence_service_is_active()) {
        scpi_port_push_exec_error(context, "SEQUENCE_RUNTIME_FROZEN");
        return false;
    }
    return true;
}

scpi_result_t scpi_sequence_node_load(scpi_t *context)
{
    if (!sequence_node_config_allowed(context)) return SCPI_RES_ERR;
    return scpi_cmd_refmem_load_node(context);
}

scpi_result_t scpi_sequence_node_activate(scpi_t *context)
{
    if (!sequence_node_config_allowed(context)) return SCPI_RES_ERR;
    return scpi_cmd_refmem_load_activate(context);
}

scpi_result_t scpi_sequence_node_load_q(scpi_t *context)
{
    return scpi_cmd_refmem_load_status_q(context);
}

scpi_result_t scpi_sequence_node_role(scpi_t *context)
{
    static const scpi_choice_def_t roles[] = {
        {"DUT", REFMEM_APP_ROLE_LINK_SWITCHER},
        {"DUT_LINK_CONTROL", REFMEM_APP_ROLE_LINK_SWITCHER},
        {"VNA", REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER},
        {"VNA_GATEWAY", REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER},
        {"COUNTER", REFMEM_APP_ROLE_PULSE_DISTRIBUTOR},
        {"PULSE_COUNTER", REFMEM_APP_ROLE_PULSE_DISTRIBUTOR},
        SCPI_CHOICE_LIST_END
    };
    uint32_t node_id, instance_id;
    int32_t role;
    if (!scpi_sequence_param_u32(context, &node_id) ||
        !scpi_sequence_param_u32(context, &instance_id) ||
        !SCPI_ParamChoice(context, roles, &role, TRUE) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    if (!sequence_node_config_allowed(context)) return SCPI_RES_ERR;
    /* Do not retain the aligned trigger vector across RMTP serialization:
     * USB runtime mode executes this on the bounded USB task stack. */
    if (!sync_trigger_sequence_can_start()) {
        scpi_port_push_exec_error(context, "REFMEM_RT_NOT_IDLE");
        return SCPI_RES_ERR;
    }
    if (!distributed_refmem_stage_sequence_role(node_id, instance_id, (uint32_t)role, 1u)) {
        scpi_port_push_exec_error(context, "SEQUENCE_ROLE_STAGE_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "STAGED");
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_node_role_q(scpi_t *context)
{
    uint32_t instance_id;
    if (!scpi_sequence_param_u32(context, &instance_id) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    refmem_fb_instance_entry_t active, staging;
    if (!refmem_application_model_get_sequence_instance(instance_id, false, &active) ||
        !refmem_application_model_get_sequence_instance(instance_id, true, &staging) ||
        (active.fb_type != REFMEM_APP_FB_LINK_SWITCHER &&
         active.fb_type != REFMEM_APP_FB_INSTRUMENT_CONTROLLER &&
         active.fb_type != REFMEM_APP_FB_PULSE_COUNTER)) {
        scpi_port_push_exec_error(context, "SEQUENCE_ROLE_INSTANCE_INVALID");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, instance_id);
    SCPI_ResultText(context, active.fb_type == REFMEM_APP_FB_PULSE_COUNTER ? "COUNTER" :
        (active.fb_type == REFMEM_APP_FB_LINK_SWITCHER ? "DUT" : "VNA"));
    SCPI_ResultUInt32(context, active.enable_condition);
    SCPI_ResultUInt32(context, staging.enable_condition);
    SCPI_ResultUInt32(context, active.resource_claim);
    SCPI_ResultUInt32(context, active.io_claim);
    SCPI_ResultUInt32(context, active.ip_core_claim);
    SCPI_ResultUInt32(context, staging.resource_claim);
    SCPI_ResultUInt32(context, staging.io_claim);
    SCPI_ResultUInt32(context, staging.ip_core_claim);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_link_config(scpi_t *context)
{
    /* LOOPBACK is an explicit physical cable-return mode. RJ45 remains a
     * compatibility spelling, not a claim of remote-node routing. */
    static const scpi_choice_def_t modes[] = {
        {"OFF", 0}, {"LOOPBACK", 1}, {"RJ45", 1}, {"POSITION", 2}, SCPI_CHOICE_LIST_END};
    static const scpi_choice_def_t inputs[] = {
        {"MANUAL", 0}, {"IN1", 1}, {"IN2", 2}, {"IN3", 3}, {"IN4", 4},
        SCPI_CHOICE_LIST_END};
    static const scpi_choice_def_t outputs[] = {
        {"OUT1", 1}, {"OUT2", 2}, {"OUT3", 4}, {"OUT4", 8}, SCPI_CHOICE_LIST_END};
    static const scpi_choice_def_t edges[] = {{"RISing", 0}, {"FALLing", 1}, SCPI_CHOICE_LIST_END};
    int32_t mode, input, output, edge;
    trigger_sequence_link_config_t config = {0};
    if (!SCPI_ParamChoice(context, modes, &mode, TRUE)) return SCPI_RES_ERR;
    config.enabled = mode != 0;
    config.counter_enabled = mode == 2;
    if (config.enabled) {
        if (config.counter_enabled &&
            !scpi_sequence_param_u32(context, &config.counter_slot)) return SCPI_RES_ERR;
        if (!scpi_sequence_param_u32(context, &config.dut_slot) ||
            !scpi_sequence_param_u32(context, &config.vna_slot)) return SCPI_RES_ERR;
        if (config.counter_enabled) {
            if (!SCPI_ParamChoice(context, inputs, &input, TRUE) || input == 0 ||
                !scpi_sequence_param_u32(context, &config.counter_threshold) ||
                config.counter_threshold == 0u) {
                scpi_port_push_exec_error(context, "COUNTER_INPUT_OR_THRESHOLD_INVALID");
                return SCPI_RES_ERR;
            }
            config.counter_input = (uint32_t)input;
        }
        if (
            !SCPI_ParamChoice(context, inputs, &input, TRUE) ||
            !SCPI_ParamChoice(context, outputs, &output, TRUE) ||
            !scpi_sequence_param_u32(context, &config.pulse_us) ||
            !scpi_sequence_param_u32(context, &config.timeout_ms) ||
            !SCPI_ParamChoice(context, edges, &edge, TRUE)) return SCPI_RES_ERR;
        config.ready_input = (uint32_t)input;
        config.trigger_output_mask = (uint32_t)output;
        config.falling = edge != 0;
    }
    if (!scpi_sequence_params_end(context) || !sequence_node_config_allowed(context)) return SCPI_RES_ERR;
    if (!trigger_sequence_link_configure(&config)) {
        scpi_port_push_exec_error(context, "SEQUENCE_LINK_CONFIG_REJECTED_STOP_TDMA_FIRST");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_accepted(context);
}

scpi_result_t scpi_sequence_link_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_link_status_t s;
    trigger_sequence_link_get_status(&s);
    SCPI_ResultBool(context, s.config.enabled);
    SCPI_ResultUInt32(context, s.phase);
    SCPI_ResultUInt32(context, s.error);
    SCPI_ResultUInt32(context, s.binding_epoch);
    SCPI_ResultUInt32(context, s.model_epoch);
    SCPI_ResultUInt32(context, s.run_id);
    SCPI_ResultUInt32(context, s.generation);
    SCPI_ResultUInt32(context, s.step);
    SCPI_ResultUInt32(context, s.tx_fragments);
    SCPI_ResultUInt32(context, s.rx_messages);
    SCPI_ResultUInt32(context, s.rejected);
    SCPI_ResultUInt32(context, s.triggers);
    SCPI_ResultUInt32(context, s.ready);
    SCPI_ResultUInt32(context, s.completed);
    SCPI_ResultUInt32(context, s.config.dut_slot);
    SCPI_ResultUInt32(context, s.config.vna_slot);
    SCPI_ResultUInt32(context, s.config.ready_input);
    SCPI_ResultUInt32(context, s.config.trigger_output_mask);
    SCPI_ResultUInt32(context, s.config.pulse_us);
    SCPI_ResultUInt32(context, s.config.timeout_ms);
    SCPI_ResultBool(context, s.config.falling);
    SCPI_ResultUInt32(context, s.repeat_count);
    SCPI_ResultUInt32(context, s.exchange_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_link_transport_q(scpi_t *context)
{
    uint32_t values[6];
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    const tdma_local_return_snapshot_quality_t quality =
        tdma_pio_spi_ring_adapter_get_local_return_snapshot(
            tdma_runtime_owner_get_ring_adapter(), values);
    for (uint32_t i = 0; i < 6u; ++i) SCPI_ResultUInt32(context, values[i]);
    SCPI_ResultUInt32(context, (uint32_t)quality);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_counter_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_link_status_t s;
    trigger_sequence_link_get_status(&s);
    SCPI_ResultBool(context, s.config.counter_enabled);
    SCPI_ResultUInt32(context, s.config.counter_slot);
    SCPI_ResultUInt32(context, s.config.counter_input);
    SCPI_ResultUInt32(context, s.config.counter_threshold);
    SCPI_ResultUInt32(context, s.counter_events);
    SCPI_ResultUInt32(context, s.counter_consumed);
    SCPI_ResultUInt32(context, s.counter_partial);
    SCPI_ResultUInt32(context, s.counter_fault_events);
    SCPI_ResultUInt32(context, s.history_total);
    SCPI_ResultUInt32(context, s.history_retained);
    SCPI_ResultUInt32(context, s.phase);
    SCPI_ResultUInt32(context, s.error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_counter_history_q(scpi_t *context)
{
    uint32_t ordinal;
    trigger_sequence_link_history_t record;
    if (!scpi_sequence_param_u32(context, &ordinal) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    if (!trigger_sequence_link_get_history(ordinal, &record)) {
        scpi_port_push_exec_error(context, "COUNTER_HISTORY_NOT_RETAINED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, record.ordinal);
    SCPI_ResultUInt32(context, record.run_id);
    SCPI_ResultUInt32(context, record.generation);
    SCPI_ResultUInt32(context, record.position);
    SCPI_ResultUInt32(context, record.sequence_index);
    SCPI_ResultUInt32(context, record.threshold_pulses);
    SCPI_ResultUInt32(context, record.observed_pulses);
    SCPI_ResultUInt32(context, record.outcome_flags);
    return SCPI_RES_OK;
}
