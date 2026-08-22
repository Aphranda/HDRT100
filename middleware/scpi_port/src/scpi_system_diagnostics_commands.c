#include "scpi_system_diagnostics_commands.h"

#include <stdint.h>

#include "flash_transaction.h"
#include "flash_map.h"
#include "resource_arbiter.h"
#include "scpi_port_internal.h"
#include "sync_trigger.h"

static const char *scpi_owner_or_dash(const char *owner)
{
    return owner != NULL ? owner : "-";
}

scpi_result_t scpi_system_diagnostics_run_last_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultText(context, "COMPLETE");
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_system_diagnostics_run_summary_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultText(context, "PLAN_A");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "COMPLETE");
    SCPI_ResultInt32(context, -10);
    SCPI_ResultInt32(context, 370);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 381u);
    SCPI_ResultUInt32(context, 381u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 6u);
    SCPI_ResultUInt32(context, 2286u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "LOCKED");
    SCPI_ResultUInt32(context, 0x01020304u);
    SCPI_ResultUInt32(context, 0x02030405u);
    SCPI_ResultUInt32(context, 0x03040506u);
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_system_diagnostics_page_block_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "EMPTY");
    return SCPI_RES_OK;
}

scpi_result_t scpi_system_diagnostics_count_zero_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_system_diagnostics_statistics_q(scpi_t *context)
{
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_debug_q(scpi_t *context)
{
    uint32_t port_stage = 0u;
    uint32_t port_mode = 0u;
    uint32_t port_posted = 0u;
    uint32_t sync_stage = 0u;
    uint32_t sync_event = 0u;
    uint32_t sync_state = 0u;
    uint32_t sync_error = 0u;

    scpi_port_get_trigger_debug_snapshot(&port_stage, &port_mode, &port_posted);
    sync_trigger_get_debug(&sync_stage, &sync_event, &sync_state, &sync_error);

    SCPI_ResultUInt32(context, port_stage);
    SCPI_ResultUInt32(context, port_mode);
    SCPI_ResultUInt32(context, port_posted);
    SCPI_ResultUInt32(context, sync_stage);
    SCPI_ResultUInt32(context, sync_event);
    SCPI_ResultUInt32(context, sync_state);
    SCPI_ResultUInt32(context, sync_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_resource_status_q(scpi_t *context)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);

    SCPI_ResultUInt32(context, snapshot.active_resources);
    SCPI_ResultUInt32(context, snapshot.last_conflict_resources);
    SCPI_ResultText(context, scpi_owner_or_dash(snapshot.last_conflict_owner));
    SCPI_ResultText(context, scpi_owner_or_dash(snapshot.last_conflict_holder));
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_resource_training_q(scpi_t *context)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);

    SCPI_ResultBool(context, snapshot.calibration_training_active ? TRUE : FALSE);
    SCPI_ResultBool(context, snapshot.tdma_clock_training_active ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_flash_map_q(scpi_t *context)
{
    uint32_t partition_id = 0u;
    if (!SCPI_ParamUInt32(context, &partition_id, TRUE)) {
        return SCPI_RES_ERR;
    }
    const flash_map_partition_t *partition =
        flash_map_find_by_id(partition_id);
    if (partition == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, FLASH_MAP_VERSION);
    SCPI_ResultText(context, FLASH_MAP_DEPLOYMENT_STATE);
    SCPI_ResultUInt32(context, FLASH_MAP_PARTITION_COUNT);
    SCPI_ResultUInt32(context, partition->id);
    SCPI_ResultUInt32(context, partition->offset);
    SCPI_ResultUInt32(context, partition->size);
    SCPI_ResultUInt32(context, partition->alignment);
    SCPI_ResultUInt32(context, partition->boot_permissions);
    SCPI_ResultUInt32(context, partition->app_permissions);
    SCPI_ResultUInt32(context, partition->factory_permissions);
    SCPI_ResultBool(context, partition->executable ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_flash_access_q(scpi_t *context)
{
    uint32_t partition_id = 0u;
    uint32_t context_id = 0u;
    uint32_t operation = 0u;
    uint32_t active_partition_id = 0u;
    uint32_t scratch_lease = 0u;
    uint32_t relative_offset = 0u;
    uint32_t length = 0u;
    if (!SCPI_ParamUInt32(context, &partition_id, TRUE) ||
        !SCPI_ParamUInt32(context, &context_id, TRUE) ||
        !SCPI_ParamUInt32(context, &operation, TRUE) ||
        !SCPI_ParamUInt32(context, &active_partition_id, TRUE) ||
        !SCPI_ParamUInt32(context, &scratch_lease, TRUE) ||
        !SCPI_ParamUInt32(context, &relative_offset, TRUE) ||
        !SCPI_ParamUInt32(context, &length, TRUE) || scratch_lease > 1u) {
        return SCPI_RES_ERR;
    }

    uint32_t absolute_offset = 0u;
    const bool is_allowed = flash_map_operation_allowed(
        &(const flash_map_access_t){
            .context = (flash_map_context_t)context_id,
            .active_app_partition_id = active_partition_id,
            .scratch_lease = scratch_lease != 0u,
        },
        partition_id,
        (flash_map_operation_t)operation,
        relative_offset,
        length,
        &absolute_offset);

    SCPI_ResultBool(context, is_allowed ? TRUE : FALSE);
    SCPI_ResultUInt32(context, absolute_offset);
    SCPI_ResultUInt32(context, partition_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_flash_transaction_q(scpi_t *context)
{
    flash_transaction_vector_t vector;
    if (!flash_transaction_ao_get_vector(&vector)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, vector.state);
    SCPI_ResultUInt32(context, vector.job_id);
    SCPI_ResultUInt32(context, vector.requester);
    SCPI_ResultUInt32(context, vector.partition_id);
    SCPI_ResultUInt32(context, vector.operation);
    SCPI_ResultUInt32(context, vector.requested_bytes);
    SCPI_ResultUInt32(context, vector.processed_bytes);
    SCPI_ResultUInt32(context, vector.verified_bytes);
    SCPI_ResultUInt32(context, vector.map_version);
    SCPI_ResultUInt32(context, vector.provider_generation);
    SCPI_ResultUInt32(context, vector.store_generation);
    SCPI_ResultUInt32(context, vector.transaction_generation);
    SCPI_ResultUInt32(context, vector.completion_level);
    SCPI_ResultUInt32(context, vector.last_result);
    SCPI_ResultUInt32(context, vector.last_error);
    SCPI_ResultUInt32(context, vector.retry_count);
    SCPI_ResultUInt32(context, vector.abort_pending);
    SCPI_ResultUInt32(context, vector.lockout_request_seq);
    SCPI_ResultUInt32(context, vector.lockout_ack_seq);
    SCPI_ResultUInt32(context, vector.lockout_timeout_count);
    SCPI_ResultUInt32(context, vector.erase_count_delta);
    SCPI_ResultUInt32(context, vector.program_count_delta);
    SCPI_ResultUInt32(context, vector.verify_failure_count);
    SCPI_ResultUInt32(context, vector.temperature_flags);
    SCPI_ResultUInt32(context, vector.policy_gate_reason);
    SCPI_ResultUInt32(context, vector.started_timestamp_ms);
    SCPI_ResultUInt32(context, vector.completed_timestamp_ms);
    return SCPI_RES_OK;
}
