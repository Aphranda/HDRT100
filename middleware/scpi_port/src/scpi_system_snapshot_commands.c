#include "scpi_system_snapshot_commands.h"

#include <stdbool.h>
#include <stdint.h>

#include "app.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "project_build_info.h"

scpi_result_t scpi_cmd_refmem_status_q(scpi_t *context)
{
    distributed_refmem_status_t status;
    distributed_refmem_get_status(&status);

    SCPI_ResultUInt32(context, status.table_size);
    SCPI_ResultUInt32(context, status.layout_version);
    SCPI_ResultUInt32(context, status.table_seq);
    SCPI_ResultUInt32(context, status.local_node_id);
    SCPI_ResultUInt32(context, status.node_count);
    SCPI_ResultUInt32(context, status.local_heartbeat);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_node_q(scpi_t *context)
{
    distributed_refmem_status_t status;
    distributed_refmem_node_snapshot_t node;
    distributed_refmem_get_status(&status);
    uint32_t node_id = status.local_node_id;

    (void)SCPI_ParamUInt32(context, &node_id, FALSE);
    if (!distributed_refmem_get_node(node_id, &node)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, node.node_id);
    SCPI_ResultUInt32(context, node.state);
    SCPI_ResultUInt32(context, node.heartbeat);
    SCPI_ResultUInt32(context, node.slot_version);
    SCPI_ResultUInt32(context, node.last_update_ms);
    SCPI_ResultUInt32(context, node.stale_count);
    SCPI_ResultUInt32(context, node.fault_code);
    SCPI_ResultUInt32(context, node.flags);
    SCPI_ResultUInt32(context, node.node_type);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_core_vector_q(scpi_t *context)
{
    distributed_refmem_core_vector_snapshot_t snapshot;
    distributed_refmem_get_core_vector(&snapshot);

    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.table_seq);
    SCPI_ResultUInt32(context, snapshot.core_count);
    SCPI_ResultUInt32(context, snapshot.core0_vtor_owner);
    SCPI_ResultUInt32(context, snapshot.core1_vtor_owner);
    SCPI_ResultUInt32(context, snapshot.core0_irq_owner_mask);
    SCPI_ResultUInt32(context, snapshot.core1_irq_owner_mask);
    SCPI_ResultUInt32(context, snapshot.entry_table_owner);
    SCPI_ResultUInt32(context, snapshot.flags);
    SCPI_ResultUInt32(context, snapshot.guard.owner);
    SCPI_ResultUInt32(context, snapshot.guard.crc32);
    SCPI_ResultUInt32(context, snapshot.guard.stale);
    SCPI_ResultUInt32(context, snapshot.guard.flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_runtime_protection_q(scpi_t *context)
{
    distributed_refmem_runtime_protection_snapshot_t snapshot;
    distributed_refmem_get_runtime_protection(&snapshot);

    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.table_seq);
    SCPI_ResultUInt32(context, snapshot.ram_resident_required);
    SCPI_ResultUInt32(context, snapshot.flash_lockout_supported);
    SCPI_ResultUInt32(context, snapshot.flash_lockout_online);
    SCPI_ResultUInt32(context, snapshot.flash_lockout_requested);
    SCPI_ResultUInt32(context, snapshot.flash_lockout_acknowledged);
    SCPI_ResultUInt32(context, snapshot.park_state);
    SCPI_ResultUInt32(context, snapshot.entry_table_owner);
    SCPI_ResultUInt32(context, snapshot.flags);
    SCPI_ResultUInt32(context, snapshot.guard.owner);
    SCPI_ResultUInt32(context, snapshot.guard.crc32);
    SCPI_ResultUInt32(context, snapshot.guard.stale);
    SCPI_ResultUInt32(context, snapshot.guard.flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_config_gate_status_q(scpi_t *context)
{
    app_config_gate_status_t status;
    app_config_gate_get_status(&status);

    SCPI_ResultText(context, g_project_build_id);
    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.gate_state);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.first_service_ms);
    SCPI_ResultUInt32(context, status.last_service_ms);
    SCPI_ResultUInt32(context, status.epoch);
    SCPI_ResultUInt32(context, status.run_id);
    SCPI_ResultUInt32(context, status.config_version);
    SCPI_ResultUInt32(context, status.calibration_version);
    SCPI_ResultUInt32(context, status.loop_plan_version);
    SCPI_ResultUInt32(context, status.action_map_version);
    SCPI_ResultUInt32(context, status.command_seq);
    SCPI_ResultUInt32(context, status.target_mask);
    SCPI_ResultUInt32(context, status.ack_flags);
    SCPI_ResultUInt32(context, status.nack_flags);
    SCPI_ResultUInt32(context, status.busy_flags);
    SCPI_ResultUInt32(context, status.timeout_flags);
    SCPI_ResultUInt32(context, status.build_crc32);
    SCPI_ResultUInt32(context, status.hw_profile_crc32);
    SCPI_ResultUInt32(context, status.role_map_crc32);
    SCPI_ResultUInt32(context, status.loop_plan_crc32);
    SCPI_ResultUInt32(context, status.action_map_crc32);
    SCPI_ResultUInt32(context, status.calibration_crc32);
    SCPI_ResultUInt32(context, status.config_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_config_ack_q(scpi_t *context)
{
    app_config_ack_status_t status;
    app_config_gate_get_ack_status(&status);

    SCPI_ResultUInt32(context, status.version);
    SCPI_ResultUInt32(context, status.command_seq);
    SCPI_ResultUInt32(context, status.target_mask);
    SCPI_ResultUInt32(context, status.ack_flags);
    SCPI_ResultUInt32(context, status.nack_flags);
    SCPI_ResultUInt32(context, status.busy_flags);
    SCPI_ResultUInt32(context, status.timeout_flags);
    SCPI_ResultUInt32(context, status.last_nack_reason);
    SCPI_ResultUInt32(context, status.last_nack_node);
    SCPI_ResultUInt32(context, status.reason_count);
    SCPI_ResultUInt32(context, status.reason_table_crc32);
    SCPI_ResultUInt32(context, status.config_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_config_nack_reason_q(scpi_t *context)
{
    uint32_t reason_id = 0u;
    (void)SCPI_ParamUInt32(context, &reason_id, FALSE);

    const distributed_config_nack_reason_entry_t *reason;
    if (!distributed_config_get_nack_reason(reason_id, &reason)) {
        return SCPI_RES_ERR;
    }

    const distributed_config_nack_reason_table_t *table =
        distributed_config_get_nack_reason_table();
    SCPI_ResultUInt32(context, table->version);
    SCPI_ResultUInt32(context, table->reason_count);
    SCPI_ResultUInt32(context, reason->reason_id);
    SCPI_ResultUInt32(context, reason->severity);
    SCPI_ResultUInt32(context, reason->retryable);
    SCPI_ResultUInt32(context, reason->blocking);
    SCPI_ResultUInt32(context, reason->detail_code);
    SCPI_ResultText(context, reason->name);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_scpi_run_allow_q(scpi_t *context)
{
    uint32_t index = 0u;
    (void)SCPI_ParamUInt32(context, &index, FALSE);

    const distributed_config_scpi_run_policy_entry_t *entry;
    if (!distributed_config_get_scpi_run_policy(index, &entry)) {
        return SCPI_RES_ERR;
    }

    const distributed_config_scpi_run_policy_table_t *table =
        distributed_config_get_scpi_run_policy_table();
    SCPI_ResultUInt32(context, table->version);
    SCPI_ResultUInt32(context, table->entry_count);
    SCPI_ResultUInt32(context, table->enforced);
    SCPI_ResultUInt32(context, table->policy_crc32);
    SCPI_ResultUInt32(context, entry->index);
    SCPI_ResultUInt32(context, entry->class_id);
    SCPI_ResultUInt32(context, entry->run_allowed);
    SCPI_ResultUInt32(context, entry->query_allowed);
    SCPI_ResultUInt32(context, entry->write_allowed);
    SCPI_ResultUInt32(context, entry->forbidden_error_code);
    SCPI_ResultText(context, entry->pattern);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_config_role_q(scpi_t *context)
{
    const distributed_config_role_map_t *role_map = distributed_config_get_role_map();
    uint32_t node_id = 0u;
    (void)SCPI_ParamUInt32(context, &node_id, FALSE);
    if (node_id >= role_map->node_count) {
        return SCPI_RES_ERR;
    }

    const distributed_config_role_entry_t *node = &role_map->node[node_id];
    SCPI_ResultUInt32(context, role_map->version);
    SCPI_ResultUInt32(context, role_map->node_count);
    SCPI_ResultUInt32(context, role_map->target_mask);
    SCPI_ResultUInt32(context, role_map->input_base_pin);
    SCPI_ResultUInt32(context, role_map->output_base_pin);
    SCPI_ResultUInt32(context, role_map->aux_base_pin);
    SCPI_ResultUInt32(context, node->node_id);
    SCPI_ResultUInt32(context, node->role);
    SCPI_ResultUInt32(context, node->persona);
    SCPI_ResultUInt32(context, node->feature_mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_config_loop_q(scpi_t *context)
{
    const distributed_config_loop_plan_t *loop_plan = distributed_config_get_loop_plan();
    uint32_t layer_id = 0u;
    (void)SCPI_ParamUInt32(context, &layer_id, FALSE);
    if (layer_id >= loop_plan->layer_count) {
        return SCPI_RES_ERR;
    }

    const distributed_config_layer_entry_t *layer = &loop_plan->layer[layer_id];
    SCPI_ResultUInt32(context, loop_plan->version);
    SCPI_ResultUInt32(context, loop_plan->node_loop_count);
    SCPI_ResultUInt32(context, loop_plan->array_loop_count);
    SCPI_ResultUInt32(context, loop_plan->layer_count);
    SCPI_ResultUInt32(context, loop_plan->default_wait_rule);
    SCPI_ResultUInt32(context, layer->layer_id);
    SCPI_ResultUInt32(context, layer->node_id);
    SCPI_ResultUInt32(context, layer->action_id);
    SCPI_ResultUInt32(context, layer->wait_rule);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_config_action_q(scpi_t *context)
{
    const distributed_config_action_map_t *action_map = distributed_config_get_action_map();
    uint32_t action_id = 0u;
    (void)SCPI_ParamUInt32(context, &action_id, FALSE);
    if (action_id >= action_map->action_count) {
        return SCPI_RES_ERR;
    }

    const distributed_config_action_entry_t *action = &action_map->action[action_id];
    SCPI_ResultUInt32(context, action_map->version);
    SCPI_ResultUInt32(context, action_map->action_count);
    SCPI_ResultUInt32(context, action->action_id);
    SCPI_ResultUInt32(context, action->node_id);
    SCPI_ResultUInt32(context, action->sma_out_pin);
    SCPI_ResultUInt32(context, action->sma_in_pin);
    SCPI_ResultUInt32(context, action->edge);
    SCPI_ResultUInt32(context, action->delay_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_config_calibration_q(scpi_t *context)
{
    const distributed_config_calibration_t *calibration = distributed_config_get_calibration();
    uint32_t node_id = 0u;
    (void)SCPI_ParamUInt32(context, &node_id, FALSE);
    if (node_id >= calibration->node_count) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, calibration->version);
    SCPI_ResultUInt32(context, calibration->node_count);
    SCPI_ResultUInt32(context, node_id);
    SCPI_ResultUInt32(context, calibration->delta_ns[node_id]);
    SCPI_ResultUInt32(context, calibration->sma_hop_ns);
    SCPI_ResultUInt32(context, calibration->rj45_hop_ns);
    SCPI_ResultUInt32(context, calibration->device_delay_ns);
    SCPI_ResultUInt32(context, calibration->tempco_ppb);
    SCPI_ResultUInt32(context, calibration->valid_window_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_mode_table_q(scpi_t *context)
{
    app_system_mode_table_t table;
    app_system_mode_get_table(&table);

    uint32_t mode_id = 0u;
    (void)SCPI_ParamUInt32(context, &mode_id, FALSE);
    if (mode_id >= table.mode_count) {
        return SCPI_RES_ERR;
    }

    const app_system_mode_entry_t *mode = &table.mode[mode_id];
    SCPI_ResultUInt32(context, table.version);
    SCPI_ResultUInt32(context, table.mode_count);
    SCPI_ResultUInt32(context, table.current_mode);
    SCPI_ResultUInt32(context, table.table_crc32);
    SCPI_ResultUInt32(context, mode->mode_id);
    SCPI_ResultUInt32(context, mode->run_allowed);
    SCPI_ResultUInt32(context, mode->ota_allowed);
    SCPI_ResultUInt32(context, mode->fault_allowed);
    SCPI_ResultText(context, mode->name);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_resource_arbiter_table_q(scpi_t *context)
{
    app_resource_arbiter_table_t table;
    app_resource_arbiter_get_table(&table);

    uint32_t resource_id = 0u;
    (void)SCPI_ParamUInt32(context, &resource_id, FALSE);
    if (resource_id >= table.resource_count) {
        return SCPI_RES_ERR;
    }

    const app_resource_arbiter_entry_t *resource = &table.resource[resource_id];
    SCPI_ResultUInt32(context, table.version);
    SCPI_ResultUInt32(context, table.resource_count);
    SCPI_ResultUInt32(context, table.current_mode);
    SCPI_ResultUInt32(context, table.active_resources);
    SCPI_ResultUInt32(context, table.last_conflict_resources);
    SCPI_ResultUInt32(context, table.table_crc32);
    SCPI_ResultUInt32(context, resource->resource_id);
    SCPI_ResultUInt32(context, resource->mask);
    SCPI_ResultUInt32(context, resource->owner_mode);
    SCPI_ResultUInt32(context, resource->active);
    SCPI_ResultText(context, resource->name);
    SCPI_ResultText(context, resource->owner_name);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_fault_code_table_q(scpi_t *context)
{
    app_fault_code_table_t table;
    app_fault_code_get_table(&table);

    uint32_t fault_id = 0u;
    (void)SCPI_ParamUInt32(context, &fault_id, FALSE);
    uint32_t index = 0u;
    bool found = false;
    for (; index < table.fault_count; index++) {
        if (table.fault[index].fault_id == fault_id) {
            found = true;
            break;
        }
    }
    if (!found) {
        return SCPI_RES_ERR;
    }

    const app_fault_code_entry_t *fault = &table.fault[index];
    SCPI_ResultUInt32(context, table.version);
    SCPI_ResultUInt32(context, table.fault_count);
    SCPI_ResultUInt32(context, table.latched);
    SCPI_ResultUInt32(context, table.table_crc32);
    SCPI_ResultUInt32(context, fault->fault_id);
    SCPI_ResultUInt32(context, fault->domain_id);
    SCPI_ResultUInt32(context, fault->severity);
    SCPI_ResultUInt32(context, fault->recoverable);
    SCPI_ResultUInt32(context, fault->sticky);
    SCPI_ResultText(context, fault->name);
    return SCPI_RES_OK;
}
