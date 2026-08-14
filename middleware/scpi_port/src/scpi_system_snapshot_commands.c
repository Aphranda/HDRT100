#include "scpi_system_snapshot_commands.h"

#include <stdbool.h>
#include <stdint.h>

#include "distributed_config.h"
#include "distributed_refmem.h"
#include "osal.h"
#include "project_build_info.h"
#include "refmem_application_model.h"
#include "refmem_slot_claim.h"
#include "refmem_table_registry.h"
#include "scpi_port_internal.h"
#include "storage_manager.h"
#include "system_manager.h"
#include "sync_trigger.h"

#define SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS 200u
#define SCPI_REFMEM_PACKAGE_PATH "/refmem/app_model.rmtp"
#define SCPI_REFMEM_PACKAGE_READ_CHUNK 128u
#define SCPI_REFMEM_LOAD_MAX_BYTES 4096u

static bool scpi_refmem_wait_storage_job(uint32_t job_id);
static bool scpi_refmem_read_package(const char *path,
                                     uint8_t *buffer,
                                     size_t buffer_size,
                                     size_t *returned_size);
static bool scpi_refmem_model_mode_idle(void);
static bool scpi_refmem_realtime_idle(void);
static void scpi_refmem_result_load_snapshot(scpi_t *context,
                                             const refmem_application_model_load_snapshot_t *snapshot);
static void scpi_refmem_result_board_load_snapshot(
    scpi_t *context,
    const refmem_board_capability_load_snapshot_t *snapshot);

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

scpi_result_t scpi_cmd_refmem_board_q(scpi_t *context)
{
    const refmem_board_capability_table_t *table =
        refmem_application_model_get_board_capability_table();
    const refmem_application_model_snapshot_t *snapshot =
        refmem_application_model_get_snapshot();
    uint32_t board_id = 0u;
    (void)SCPI_ParamUInt32(context, &board_id, FALSE);
    if (table == NULL || board_id >= table->board_count) {
        return SCPI_RES_ERR;
    }

    const refmem_board_capability_entry_t *board = &table->board[board_id];
    SCPI_ResultUInt32(context, table->version);
    SCPI_ResultUInt32(context, table->board_count);
    SCPI_ResultUInt32(context, snapshot->board_capability_crc32);
    SCPI_ResultUInt32(context, board->board_id);
    SCPI_ResultUInt32(context, board->board_uuid_crc32);
    SCPI_ResultUInt32(context, board->capability_mask);
    SCPI_ResultUInt32(context, board->io_constraint_mask);
    SCPI_ResultUInt32(context, board->ip_core_mask);
    SCPI_ResultUInt32(context, board->default_persona_mask);
    SCPI_ResultUInt32(context, board->hw_profile_crc32);
    SCPI_ResultUInt32(context, board->active_default_slot);
    SCPI_ResultUInt32(context, board->online_required);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_claim_q(scpi_t *context)
{
    refmem_slot_claim_map_t map;
    if (!refmem_slot_claim_derive_map(refmem_application_model_get_generic_node_table(),
                                      refmem_application_model_get_board_capability_table(),
                                      refmem_application_model_get_node_load_table(),
                                      refmem_application_model_get_fb_instance_table(),
                                      &map)) {
        return SCPI_RES_ERR;
    }

    uint32_t slot_id = 0u;
    (void)SCPI_ParamUInt32(context, &slot_id, FALSE);
    const refmem_slot_claim_assignment_t *slot =
        refmem_slot_claim_find_assignment(&map, slot_id);
    if (slot == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, map.version);
    SCPI_ResultUInt32(context, map.claim_epoch);
    SCPI_ResultUInt32(context, map.slot_count);
    SCPI_ResultUInt32(context, map.candidate_count);
    SCPI_ResultUInt32(context, map.assigned_count);
    SCPI_ResultUInt32(context, map.conflict_count);
    SCPI_ResultUInt32(context, map.overflow_count);
    SCPI_ResultUInt32(context, map.disabled_count);
    SCPI_ResultUInt32(context, map.map_crc32);
    SCPI_ResultUInt32(context, slot->slot_id);
    SCPI_ResultUInt32(context, slot->board_id);
    SCPI_ResultUInt32(context, slot->board_uuid_crc32);
    SCPI_ResultUInt32(context, slot->capability_mask);
    SCPI_ResultUInt32(context, slot->io_constraint_mask);
    SCPI_ResultUInt32(context, slot->ip_core_mask);
    SCPI_ResultUInt32(context, slot->loaded_instance_mask);
    SCPI_ResultUInt32(context, slot->claim_count);
    SCPI_ResultUInt32(context, slot->claim_state);
    SCPI_ResultUInt32(context, slot->reason);
    SCPI_ResultUInt32(context, slot->claim_policy);
    SCPI_ResultUInt32(context, slot->claim_priority);
    SCPI_ResultUInt32(context, slot->claim_epoch);
    SCPI_ResultUInt32(context, slot->last_claim_seq);
    SCPI_ResultUInt32(context, slot->claim_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_load_sd(scpi_t *context)
{
    if (!scpi_refmem_model_mode_idle()) {
        scpi_port_push_exec_error(context, "REFMEM_MODE_NOT_IDLE");
        return SCPI_RES_ERR;
    }

    if (!scpi_refmem_realtime_idle()) {
        scpi_port_push_exec_error(context, "REFMEM_RT_NOT_IDLE");
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    (void)SCPI_ParamCharacters(context, &path, &path_len, FALSE);
    if (path != NULL && path_len >= 96u) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_manifest_scan_job(&job_id)) {
        scpi_port_push_exec_error(context, "REFMEM_SD_JOB_BUSY");
        return SCPI_RES_ERR;
    }
    (void)scpi_refmem_wait_storage_job(job_id);

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    if (job.id != job_id ||
        job.state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
        job.state == STORAGE_MANAGER_JOB_STATE_RUNNING) {
        scpi_port_push_exec_error(context, "REFMEM_SD_JOB_INCOMPLETE");
        return SCPI_RES_ERR;
    }

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    const char *load_path = (path != NULL && path_len > 0u) ? path : SCPI_REFMEM_PACKAGE_PATH;
    char path_buffer[96];
    if (path != NULL && path_len > 0u) {
        for (size_t i = 0u; i < path_len; i++) {
            path_buffer[i] = path[i];
        }
        path_buffer[path_len] = '\0';
        load_path = path_buffer;
    }

    uint8_t package_buffer[SCPI_REFMEM_LOAD_MAX_BYTES];
    size_t package_size = 0u;
    refmem_table_package_validation_t validation = {0};
    validation.error = REFMEM_TABLE_PACKAGE_ERR_TOO_SMALL;
    bool package_read = false;
    bool package_valid = false;
    if ((uint32_t)vector.manifest_status == REFMEM_APP_MODEL_SD_MANIFEST_OK &&
        vector.manifest_missing_count == 0u) {
        package_read = scpi_refmem_read_package(load_path,
                                                package_buffer,
                                                sizeof(package_buffer),
                                                &package_size);
        package_valid = package_read &&
                        refmem_table_registry_validate_package(package_buffer,
                                                               package_size,
                                                               &validation);
    }

    const bool staged =
        refmem_application_model_stage_sd_system_pack(load_path,
                                                      package_read ? job.path_hash : 0u,
                                                      (uint32_t)vector.manifest_status,
                                                      vector.manifest_schema,
                                                      vector.manifest_required_count,
                                                      vector.manifest_missing_count,
                                                      vector.manifest_build_id,
                                                      validation.package_crc32,
                                                      package_valid ? 1u : 0u,
                                                      validation.error);

    refmem_application_model_load_snapshot_t snapshot;
    refmem_application_model_get_load_snapshot(&snapshot);
    SCPI_ResultText(context, staged ? "STAGED" : "REJECTED");
    scpi_refmem_result_load_snapshot(context, &snapshot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_load_node(scpi_t *context)
{
    if (!scpi_refmem_model_mode_idle()) {
        scpi_port_push_exec_error(context, "REFMEM_MODE_NOT_IDLE");
        return SCPI_RES_ERR;
    }

    if (!scpi_refmem_realtime_idle()) {
        scpi_port_push_exec_error(context, "REFMEM_RT_NOT_IDLE");
        return SCPI_RES_ERR;
    }

    uint32_t node_id = 0u;
    uint32_t instance_id = 0u;
    uint32_t role_mask = 0u;
    uint32_t persona_mask = 0u;
    uint32_t enabled = 1u;
    uint32_t required = 0u;
    uint32_t load_order = 0u;
    if (!scpi_port_read_u32(context, &node_id) ||
        !scpi_port_read_u32(context, &instance_id) ||
        !scpi_port_read_u32(context, &role_mask) ||
        !scpi_port_read_u32(context, &persona_mask)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &enabled, FALSE);
    (void)SCPI_ParamUInt32(context, &required, FALSE);
    (void)SCPI_ParamUInt32(context, &load_order, FALSE);

    const bool staged =
        refmem_application_model_stage_scpi_node_config(node_id,
                                                        instance_id,
                                                        role_mask,
                                                        persona_mask,
                                                        enabled,
                                                        required,
                                                        load_order);

    refmem_application_model_load_snapshot_t snapshot;
    refmem_application_model_get_load_snapshot(&snapshot);
    SCPI_ResultText(context, staged ? "STAGED" : "REJECTED");
    scpi_refmem_result_load_snapshot(context, &snapshot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_load_board(scpi_t *context)
{
    if (!scpi_refmem_model_mode_idle()) {
        scpi_port_push_exec_error(context, "REFMEM_MODE_NOT_IDLE");
        return SCPI_RES_ERR;
    }

    if (!scpi_refmem_realtime_idle()) {
        scpi_port_push_exec_error(context, "REFMEM_RT_NOT_IDLE");
        return SCPI_RES_ERR;
    }

    uint32_t board_id = 0u;
    uint32_t board_uuid_crc32 = 0u;
    uint32_t capability_mask = 0u;
    uint32_t io_constraint_mask = 0u;
    uint32_t ip_core_mask = 0u;
    uint32_t default_persona_mask = 0u;
    uint32_t hw_profile_crc32 = 0u;
    uint32_t active_default_slot = 0u;
    uint32_t online_required = 0u;
    if (!scpi_port_read_u32(context, &board_id) ||
        !scpi_port_read_u32(context, &board_uuid_crc32) ||
        !scpi_port_read_u32(context, &capability_mask) ||
        !scpi_port_read_u32(context, &io_constraint_mask) ||
        !scpi_port_read_u32(context, &ip_core_mask) ||
        !scpi_port_read_u32(context, &default_persona_mask) ||
        !scpi_port_read_u32(context, &hw_profile_crc32) ||
        !scpi_port_read_u32(context, &active_default_slot) ||
        !scpi_port_read_u32(context, &online_required)) {
        return SCPI_RES_ERR;
    }

    const bool staged =
        refmem_application_model_stage_scpi_board_capability(board_id,
                                                             board_uuid_crc32,
                                                             capability_mask,
                                                             io_constraint_mask,
                                                             ip_core_mask,
                                                             default_persona_mask,
                                                             hw_profile_crc32,
                                                             active_default_slot,
                                                             online_required);

    refmem_board_capability_load_snapshot_t snapshot;
    refmem_application_model_get_board_load_snapshot(&snapshot);
    SCPI_ResultText(context, staged ? "STAGED" : "REJECTED");
    scpi_refmem_result_board_load_snapshot(context, &snapshot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_load_status_q(scpi_t *context)
{
    refmem_application_model_load_snapshot_t snapshot;
    refmem_application_model_get_load_snapshot(&snapshot);
    scpi_refmem_result_load_snapshot(context, &snapshot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_load_board_status_q(scpi_t *context)
{
    refmem_board_capability_load_snapshot_t snapshot;
    refmem_application_model_get_board_load_snapshot(&snapshot);
    scpi_refmem_result_board_load_snapshot(context, &snapshot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_table_q(scpi_t *context)
{
    uint32_t table_id = REFMEM_APP_TABLE_APPLICATION_MAP;
    (void)SCPI_ParamUInt32(context, &table_id, FALSE);

    refmem_table_registry_entry_t entry;
    if (!refmem_table_registry_get_entry(table_id, &entry)) {
        return SCPI_RES_ERR;
    }

    refmem_table_registry_snapshot_t snapshot;
    refmem_table_registry_get_snapshot(&snapshot);

    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.table_count);
    SCPI_ResultUInt32(context, snapshot.active_table_mask);
    SCPI_ResultUInt32(context, snapshot.staging_table_mask);
    SCPI_ResultUInt32(context, snapshot.registry_crc32);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, entry.table_id);
    SCPI_ResultUInt32(context, entry.owner);
    SCPI_ResultUInt32(context, entry.layout_version);
    SCPI_ResultUInt32(context, entry.image_offset);
    SCPI_ResultUInt32(context, entry.image_size);
    SCPI_ResultUInt32(context, entry.active_crc32);
    SCPI_ResultUInt32(context, entry.staging_crc32);
    SCPI_ResultUInt32(context, entry.validation_state);
    SCPI_ResultUInt32(context, entry.validator_id);
    SCPI_ResultUInt32(context, entry.last_result);
    SCPI_ResultUInt32(context, entry.evidence_index);
    SCPI_ResultUInt32(context, entry.flags);
    return SCPI_RES_OK;
}

static bool scpi_refmem_wait_storage_job(uint32_t job_id)
{
    for (uint32_t i = 0u; i < SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS; i++) {
#if PROJECT_USE_FREERTOS
        osal_task_delay_ms(1u);
#else
        storage_manager_service(250u);
#endif
        storage_manager_job_result_t job;
        storage_manager_get_job_result(&job);
        if (job.id != job_id) {
            return false;
        }
        if (job.state == STORAGE_MANAGER_JOB_STATE_DONE) {
            return true;
        }
        if (job.state == STORAGE_MANAGER_JOB_STATE_FAILED) {
            return false;
        }
    }
    return false;
}

static bool scpi_refmem_read_package(const char *path,
                                     uint8_t *buffer,
                                     size_t buffer_size,
                                     size_t *returned_size)
{
    if (path == NULL || buffer == NULL || returned_size == NULL || buffer_size == 0u) {
        return false;
    }

    *returned_size = 0u;
    uint32_t offset = 0u;
    while (*returned_size < buffer_size) {
        uint32_t job_id = 0u;
        if (!storage_manager_post_file_read_job(path,
                                                offset,
                                                SCPI_REFMEM_PACKAGE_READ_CHUNK,
                                                &job_id)) {
            return false;
        }
        if (!scpi_refmem_wait_storage_job(job_id)) {
            return false;
        }

        storage_manager_file_read_t read_info;
        uint8_t chunk[SCPI_REFMEM_PACKAGE_READ_CHUNK];
        if (!storage_manager_get_file_read_job_result(job_id,
                                                      &read_info,
                                                      chunk,
                                                      sizeof(chunk))) {
            return false;
        }
        if (read_info.returned == 0u ||
            read_info.returned > sizeof(chunk) ||
            *returned_size + read_info.returned > buffer_size) {
            return false;
        }

        for (uint32_t i = 0u; i < read_info.returned; i++) {
            buffer[*returned_size + i] = chunk[i];
        }
        *returned_size += read_info.returned;
        if (read_info.eof) {
            return true;
        }
        offset += read_info.returned;
    }

    return false;
}

static bool scpi_refmem_model_mode_idle(void)
{
    refmem_application_model_load_snapshot_t snapshot;
    refmem_application_model_get_load_snapshot(&snapshot);
    return snapshot.mode == REFMEM_APP_MODEL_MODE_IDLE;
}

static bool scpi_refmem_realtime_idle(void)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    return vector.state == TRIG_STATE_IDLE;
}

static void scpi_refmem_result_load_snapshot(scpi_t *context,
                                             const refmem_application_model_load_snapshot_t *snapshot)
{
    SCPI_ResultUInt32(context, snapshot->version);
    SCPI_ResultUInt32(context, snapshot->load_seq);
    SCPI_ResultUInt32(context, snapshot->source);
    SCPI_ResultUInt32(context, snapshot->mode);
    SCPI_ResultUInt32(context, snapshot->staging_state);
    SCPI_ResultUInt32(context, snapshot->manifest_status);
    SCPI_ResultUInt32(context, snapshot->manifest_schema);
    SCPI_ResultUInt32(context, snapshot->manifest_required_count);
    SCPI_ResultUInt32(context, snapshot->manifest_missing_count);
    SCPI_ResultUInt32(context, snapshot->path_hash);
    SCPI_ResultUInt32(context, snapshot->active_package_crc32);
    SCPI_ResultUInt32(context, snapshot->staging_package_crc32);
    SCPI_ResultUInt32(context, snapshot->staging_lint_error_count);
    SCPI_ResultUInt32(context, snapshot->staging_first_lint_error);
    SCPI_ResultUInt32(context, snapshot->staging_node_id);
    SCPI_ResultUInt32(context, snapshot->staging_instance_id);
    SCPI_ResultUInt32(context, snapshot->staging_role_mask);
    SCPI_ResultUInt32(context, snapshot->staging_persona_mask);
    SCPI_ResultUInt32(context, snapshot->staging_enabled);
    SCPI_ResultUInt32(context, snapshot->staging_required);
    SCPI_ResultUInt32(context, snapshot->staging_load_order);
    SCPI_ResultUInt32(context, snapshot->last_error);
    SCPI_ResultText(context, snapshot->manifest_build_id);
    SCPI_ResultText(context, snapshot->path);
}

static void scpi_refmem_result_board_load_snapshot(
    scpi_t *context,
    const refmem_board_capability_load_snapshot_t *snapshot)
{
    SCPI_ResultUInt32(context, snapshot->version);
    SCPI_ResultUInt32(context, snapshot->load_seq);
    SCPI_ResultUInt32(context, snapshot->mode);
    SCPI_ResultUInt32(context, snapshot->staging_state);
    SCPI_ResultUInt32(context, snapshot->active_crc32);
    SCPI_ResultUInt32(context, snapshot->staging_crc32);
    SCPI_ResultUInt32(context, snapshot->staging_lint_error_count);
    SCPI_ResultUInt32(context, snapshot->staging_first_lint_error);
    SCPI_ResultUInt32(context, snapshot->staging_board_id);
    SCPI_ResultUInt32(context, snapshot->staging_board_uuid_crc32);
    SCPI_ResultUInt32(context, snapshot->staging_capability_mask);
    SCPI_ResultUInt32(context, snapshot->staging_io_constraint_mask);
    SCPI_ResultUInt32(context, snapshot->staging_ip_core_mask);
    SCPI_ResultUInt32(context, snapshot->staging_default_persona_mask);
    SCPI_ResultUInt32(context, snapshot->staging_hw_profile_crc32);
    SCPI_ResultUInt32(context, snapshot->staging_active_default_slot);
    SCPI_ResultUInt32(context, snapshot->staging_online_required);
    SCPI_ResultUInt32(context, snapshot->last_error);
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
    system_manager_config_gate_status_t status;
    system_manager_get_config_gate_status(&status);

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
    system_manager_config_ack_status_t status;
    system_manager_get_config_ack_status(&status);

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
    system_manager_mode_table_t table;
    system_manager_get_mode_table(&table);

    uint32_t mode_id = 0u;
    (void)SCPI_ParamUInt32(context, &mode_id, FALSE);
    if (mode_id >= table.mode_count) {
        return SCPI_RES_ERR;
    }

    const system_manager_mode_entry_t *mode = &table.mode[mode_id];
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
    system_manager_resource_table_t table;
    system_manager_get_resource_table(&table);

    uint32_t resource_id = 0u;
    (void)SCPI_ParamUInt32(context, &resource_id, FALSE);
    if (resource_id >= table.resource_count) {
        return SCPI_RES_ERR;
    }

    const system_manager_resource_entry_t *resource = &table.resource[resource_id];
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
    system_manager_fault_table_t table;
    system_manager_get_fault_table(&table);

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

    const system_manager_fault_entry_t *fault = &table.fault[index];
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
