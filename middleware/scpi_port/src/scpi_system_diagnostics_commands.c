#include "scpi_system_diagnostics_commands.h"

#include <stdint.h>

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
