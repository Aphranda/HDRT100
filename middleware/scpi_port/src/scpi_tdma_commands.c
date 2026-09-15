#include "scpi_tdma_commands.h"

#include <stdint.h>

#include "scpi_port_internal.h"
#include "tdma_operating_profile.h"
#include "tdma_runtime_owner.h"

static void scpi_tdma_result_profile(
    scpi_t *context,
    const tdma_operating_profile_t *profile)
{
    SCPI_ResultUInt32(context, profile->level);
    SCPI_ResultUInt32(context, profile->baud_hz);
    SCPI_ResultUInt32(context, profile->cycle_period_ns);
    SCPI_ResultUInt32(context, profile->train_cycles);
    SCPI_ResultUInt32(context, profile->flags);
    SCPI_ResultUInt32(context, profile->profile_crc32);
}

scpi_result_t scpi_cmd_tdma_event_tap(scpi_t *context)
{
    uint32_t enabled, prefix_bits, sample_delay_cycles;
    if (!scpi_port_read_u32(context, &enabled) ||
        !scpi_port_read_u32(context, &prefix_bits) ||
        !scpi_port_read_u32(context, &sample_delay_cycles) ||
        !tdma_runtime_owner_set_event_tap(enabled, prefix_bits, sample_delay_cycles)) {
        scpi_port_push_exec_error(context, "TDMA_EVENT_TAP_STOP_CONFIG_REQUIRED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_tdma_event_tap_q(scpi_t *context)
{
    tdma_pio_spi_event_tap_snapshot_t snapshot;
    if (!tdma_runtime_owner_get_event_tap(&snapshot)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, snapshot.requested.enabled);
    SCPI_ResultUInt32(context, snapshot.requested.prefix_bits);
    SCPI_ResultUInt32(context, snapshot.requested.sample_delay_cycles);
    SCPI_ResultUInt32(context, snapshot.requested.generation);
    SCPI_ResultUInt32(context, snapshot.applied.enabled);
    SCPI_ResultUInt32(context, snapshot.applied.prefix_bits);
    SCPI_ResultUInt32(context, snapshot.applied.sample_delay_cycles);
    SCPI_ResultUInt32(context, snapshot.applied.generation);
    SCPI_ResultUInt32(context, snapshot.applied_valid);
    SCPI_ResultUInt32(context, snapshot.actual_valid);
    SCPI_ResultUInt32(context, snapshot.actual_prefix_bits);
    SCPI_ResultUInt32(context, snapshot.actual_sample_delay_cycles);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_tdma_opmode_catalog_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, TDMA_OPERATING_PROFILE_COUNT);
    for (uint32_t level = 0u; level < TDMA_OPERATING_PROFILE_COUNT; level++) {
        tdma_operating_profile_t profile;
        if (!tdma_operating_profile_get(level, &profile)) {
            return SCPI_RES_ERR;
        }
        scpi_tdma_result_profile(context, &profile);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_tdma_opmode_q(scpi_t *context)
{
    tdma_operating_profile_manager_t snapshot;
    if (!tdma_runtime_owner_get_operating_profile(&snapshot)) {
        return SCPI_RES_ERR;
    }
    scpi_tdma_result_profile(context, &snapshot.active);
    scpi_tdma_result_profile(context, &snapshot.staged);
    SCPI_ResultUInt32(context, snapshot.stage_count);
    SCPI_ResultUInt32(context, snapshot.apply_count);
    SCPI_ResultUInt32(context, snapshot.reject_count);
    SCPI_ResultUInt32(context, snapshot.last_result);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_tdma_opmode_stage(scpi_t *context)
{
    uint32_t level = 0u;
    if (!scpi_port_read_u32(context, &level) ||
        !tdma_runtime_owner_stage_operating_profile(level)) {
        scpi_port_push_exec_error(context, "TDMA_OPMODE_STAGE");
        return SCPI_RES_ERR;
    }
    tdma_operating_profile_manager_t snapshot;
    if (!tdma_runtime_owner_get_operating_profile(&snapshot)) {
        return SCPI_RES_ERR;
    }
    scpi_tdma_result_profile(context, &snapshot.staged);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_tdma_opmode_apply(scpi_t *context)
{
    if (!tdma_runtime_owner_apply_operating_profile()) {
        scpi_port_push_exec_error(context, "TDMA_OPMODE_APPLY_STOP_REQUIRED");
        return SCPI_RES_ERR;
    }
    tdma_operating_profile_manager_t snapshot;
    if (!tdma_runtime_owner_get_operating_profile(&snapshot)) {
        return SCPI_RES_ERR;
    }
    scpi_tdma_result_profile(context, &snapshot.active);
    return SCPI_RES_OK;
}
