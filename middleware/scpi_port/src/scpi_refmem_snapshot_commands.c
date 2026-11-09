#include "scpi_system_snapshot_commands.h"
#include "vdc_run_output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "diagnostics_tdma_record.h"
#include "pico/time.h"
#include "board_config.h"
#include "board_identity.h"
#include "refmem_control_route.h"
#include "refmem_cmd_load.h"
#include "refmem_cmd_codec.h"
#include "refmem_cmd_frame.h"
#include "refmem_command_catalog.h"
#include "refmem_function.h"
#include "refmem_node_control.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "distributed_refmem_vdc_bridge.h"
#include "osal.h"
#include "project_build_info.h"
#include "refmem_application_model.h"
#include "refmem_pio_spi_adapter.h"
#include "refmem_quality.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_slot_claim.h"
#include "refmem_sync.h"
#include "refmem_sync_hello.h"
#include "refmem_table_registry.h"
#include "refmem_lease_table.h"
#include "tdma_maintenance_runtime.h"
#include "scpi_port_internal.h"
#include "storage_manager.h"
#include "system_manager.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"
#include "tdma_rx_start_cut.h"
#include "tdma_service_timing.h"
#include "tdma_long_frame.h"
#include "tdma_maintenance.h"
#include "tdma_maintenance_stream.h"
#include "tdma_maintenance_runtime.h"
#include "tdma_process_image_layout.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "vdc_dpll_manager.h"
#include "vdc_timestamp_clock.h"
#include "scpi_system_snapshot_internal.h"
#include "refmem_obs.h"
#include "tdma_ring_runtime.h"
#include "tdma_runtime_owner.h"
#include "sync_io_sequence.h"
#include "app_refmem_domain_config.h"
#include "calibration_manager.h"

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
    SCPI_ResultUInt32(context, status.initialized);
    SCPI_ResultUInt32(context, status.init_stage);
    SCPI_ResultUInt32(context, status.init_error);
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

    refmem_slot_claim_gate_status_t gate;
    (void)refmem_slot_claim_gate_evaluate(&map, &gate);

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
    SCPI_ResultUInt32(context, gate.ready);
    SCPI_ResultUInt32(context, gate.first_bad_slot);
    SCPI_ResultUInt32(context, gate.first_reason);
    SCPI_ResultUInt32(context, gate.required_missing_count);
    SCPI_ResultUInt32(context, gate.mismatch_count);
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
    SCPI_ResultUInt32(context, slot->online_required);
    SCPI_ResultUInt32(context, slot->claim_epoch);
    SCPI_ResultUInt32(context, slot->last_claim_seq);
    SCPI_ResultUInt32(context, slot->claim_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_claim_evidence_q(scpi_t *context)
{
    refmem_slot_claim_map_t map;
    if (!refmem_slot_claim_derive_map(refmem_application_model_get_generic_node_table(),
                                      refmem_application_model_get_board_capability_table(),
                                      refmem_application_model_get_node_load_table(),
                                      refmem_application_model_get_fb_instance_table(),
                                      &map)) {
        return SCPI_RES_ERR;
    }

    uint32_t evidence_id = 0u;
    (void)SCPI_ParamUInt32(context, &evidence_id, FALSE);
    const refmem_slot_claim_evidence_t *evidence =
        refmem_slot_claim_find_evidence(&map, evidence_id);

    SCPI_ResultUInt32(context, map.version);
    SCPI_ResultUInt32(context, map.claim_epoch);
    SCPI_ResultUInt32(context, map.evidence_count);
    SCPI_ResultUInt32(context, evidence_id);
    if (evidence == NULL) {
        SCPI_ResultUInt32(context, UINT32_MAX);
        SCPI_ResultUInt32(context, UINT32_MAX);
        SCPI_ResultUInt32(context, UINT32_MAX);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, UINT32_MAX);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        return SCPI_RES_OK;
    }

    SCPI_ResultUInt32(context, evidence->evidence_id);
    SCPI_ResultUInt32(context, evidence->candidate_id);
    SCPI_ResultUInt32(context, evidence->slot_id);
    SCPI_ResultUInt32(context, evidence->board_id);
    SCPI_ResultUInt32(context, evidence->board_uuid_crc32);
    SCPI_ResultUInt32(context, evidence->preferred_slot_id);
    SCPI_ResultUInt32(context, evidence->claim_state);
    SCPI_ResultUInt32(context, evidence->reason);
    SCPI_ResultUInt32(context, evidence->claim_policy);
    SCPI_ResultUInt32(context, evidence->claim_priority);
    SCPI_ResultUInt32(context, evidence->claim_epoch);
    SCPI_ResultUInt32(context, evidence->evidence_crc32);
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

    const char *load_path = (path != NULL && path_len > 0u) ? path : SCPI_REFMEM_PACKAGE_PATH;
    char path_buffer[96];
    if (path != NULL && path_len > 0u) {
        for (size_t i = 0u; i < path_len; i++) {
            path_buffer[i] = path[i];
        }
        path_buffer[path_len] = '\0';
        load_path = path_buffer;
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_manifest_scan_job(&job_id)) {
        const bool staged =
            distributed_refmem_stage_sd_system_pack(load_path,
                                                    0u,
                                                    STORAGE_MANAGER_MANIFEST_IO_ERROR,
                                                    0u,
                                                    0u,
                                                    1u,
                                                    "",
                                                    0u,
                                                    0u,
                                                    REFMEM_TABLE_PACKAGE_ERR_TOO_SMALL,
                                                    NULL,
                                                    0u,
                                                    NULL,
                                                    0u,
                                                    0u,
                                                    0u);
        refmem_application_model_load_snapshot_t snapshot;
        refmem_application_model_get_load_snapshot(&snapshot);
        SCPI_ResultText(context, staged ? "STAGED" : "REJECTED");
        scpi_refmem_result_load_snapshot(context, &snapshot);
        return SCPI_RES_OK;
    }
    (void)scpi_refmem_wait_storage_job(job_id);

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    if (job.id != job_id ||
        job.state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
        job.state == STORAGE_MANAGER_JOB_STATE_RUNNING) {
        const bool staged =
            distributed_refmem_stage_sd_system_pack(load_path,
                                                    job.path_hash,
                                                    STORAGE_MANAGER_MANIFEST_IO_ERROR,
                                                    0u,
                                                    0u,
                                                    1u,
                                                    "",
                                                    0u,
                                                    0u,
                                                    REFMEM_TABLE_PACKAGE_ERR_TOO_SMALL,
                                                    NULL,
                                                    0u,
                                                    NULL,
                                                    0u,
                                                    0u,
                                                    0u);
        refmem_application_model_load_snapshot_t snapshot;
        refmem_application_model_get_load_snapshot(&snapshot);
        SCPI_ResultText(context, staged ? "STAGED" : "REJECTED");
        scpi_refmem_result_load_snapshot(context, &snapshot);
        return SCPI_RES_OK;
    }

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    size_t package_size = 0u;
    refmem_table_package_validation_t validation = {0};
    validation.error = REFMEM_TABLE_PACKAGE_ERR_TOO_SMALL;
    bool package_read = false;
    bool package_valid = false;
    bool staging_write_leased = false;
    uint8_t *package_buffer = NULL;
    size_t package_capacity = 0u;
    if ((uint32_t)vector.manifest_status == REFMEM_APP_MODEL_SD_MANIFEST_OK &&
        vector.manifest_missing_count == 0u) {
        staging_write_leased = refmem_table_registry_begin_staging_write(
            REFMEM_TABLE_OWNER_REFMEM_AO, &package_buffer, &package_capacity);
        if (staging_write_leased) {
            package_read = scpi_refmem_read_package(load_path,
                                                    package_buffer,
                                                    package_capacity,
                                                    &package_size);
            package_valid = package_read &&
                            refmem_table_registry_validate_package(package_buffer,
                                                                   package_size,
                                                                   &validation);
        }
    }

    const bool staged =
        distributed_refmem_stage_sd_system_pack(load_path,
                                                package_read ? job.path_hash : 0u,
                                                (uint32_t)vector.manifest_status,
                                                vector.manifest_schema,
                                                vector.manifest_required_count,
                                                vector.manifest_missing_count,
                                                vector.manifest_build_id,
                                                validation.package_crc32,
                                                package_valid ? 1u : 0u,
                                                validation.error,
                                                package_valid ? package_buffer : NULL,
                                                package_valid ? package_size : 0u,
                                                validation.table_crc32,
                                                REFMEM_TABLE_REGISTRY_COUNT,
                                                validation.owner_validated_table_mask,
                                                validation.first_bad_table);

    if (staging_write_leased) {
        (void)refmem_table_registry_end_staging_write(
            REFMEM_TABLE_OWNER_REFMEM_AO, staged && package_valid);
    }

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

    if (!distributed_refmem_can_accept_node_load_intent(
            scpi_refmem_realtime_idle() ? 1u : 0u)) {
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
        distributed_refmem_stage_node_load(node_id,
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
        distributed_refmem_stage_board_capability(board_id,
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

scpi_result_t scpi_cmd_refmem_load_activate(scpi_t *context)
{
    const bool activated =
        distributed_refmem_activate_staging(scpi_refmem_realtime_idle() ? 1u : 0u);

    refmem_table_registry_snapshot_t snapshot;
    refmem_table_registry_get_snapshot(&snapshot);
    refmem_table_image_descriptor_t active;
    refmem_table_image_descriptor_t staging;
    refmem_table_image_descriptor_t rollbackable;
    (void)refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ACTIVE, &active);
    (void)refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_STAGING, &staging);
    (void)refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ROLLBACKABLE,
                                                    &rollbackable);

    SCPI_ResultText(context, activated ? "ACTIVE" : "REJECTED");
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.table_count);
    SCPI_ResultUInt32(context, snapshot.active_table_mask);
    SCPI_ResultUInt32(context, snapshot.staging_table_mask);
    SCPI_ResultUInt32(context, snapshot.registry_crc32);
    SCPI_ResultUInt32(context, snapshot.last_error);
    scpi_refmem_result_table_image_descriptor(context, &active);
    scpi_refmem_result_table_image_descriptor(context, &staging);
    scpi_refmem_result_table_image_descriptor(context, &rollbackable);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_load_status_q(scpi_t *context)
{
    refmem_application_model_load_snapshot_t snapshot;
    refmem_application_model_get_load_snapshot(&snapshot);
    scpi_refmem_result_load_snapshot(context, &snapshot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_cmd_submit(scpi_t *context)
{
    const char *hex = NULL;
    size_t hex_len = 0u;
    uint8_t wire[REFMEM_CMD_WIRE_SIZE];
    size_t wire_size = 0u;
    if (SCPI_ParamCharacters(context, &hex, &hex_len, TRUE) != TRUE ||
        !scpi_refmem_sync_hex_decode(hex, hex_len, wire, sizeof(wire),
                                     &wire_size) ||
        wire_size != REFMEM_CMD_WIRE_SIZE) {
        scpi_port_push_exec_error(context, "REFMEM_CMD_SUBMIT_HEX");
        return SCPI_RES_ERR;
    }
    if (!refmem_cmd_load_submit(wire, (uint32_t)wire_size)) {
        scpi_port_push_exec_error(context, "REFMEM_CMD_SUBMIT_BUSY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "QUEUED");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_cmd_submit_type(scpi_t *context)
{
    uint32_t type = 0u;
    const char *hex = NULL;
    size_t hex_len = 0u;
    uint8_t wire[80];
    size_t wire_size = 0u;
    if (!scpi_port_read_u32(context, &type) ||
        SCPI_ParamCharacters(context, &hex, &hex_len, TRUE) != TRUE ||
        !scpi_refmem_sync_hex_decode(hex, hex_len, wire, sizeof(wire),
                                     &wire_size) ||
        wire_size == 0u) {
        scpi_port_push_exec_error(context, "REFMEM_CMD_SUBMIT_TYPE_HEX");
        return SCPI_RES_ERR;
    }
    if (!refmem_cmd_load_submit_type((uint8_t)type, wire,
                                     (uint32_t)wire_size)) {
        scpi_port_push_exec_error(context, "REFMEM_CMD_SUBMIT_TYPE_BUSY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "QUEUED");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_config_begin(scpi_t *context)
{
    if (!distributed_refmem_config_transaction_begin()) {
        scpi_port_push_exec_error(context, "CONFIG_TXN_BUSY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_config_commit(scpi_t *context)
{
    if (!distributed_refmem_config_transaction_commit()) {
        scpi_port_push_exec_error(context, "CONFIG_TXN_COMMIT_FAILED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_config_abort(scpi_t *context)
{
    distributed_refmem_config_transaction_abort();
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_config_status_q(scpi_t *context)
{
    SCPI_ResultUInt32(context,
                      distributed_refmem_config_transaction_active() ? 1u : 0u);
    SCPI_ResultUInt32(context, distributed_refmem_idle_window() ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_idle_window(scpi_t *context)
{
    uint32_t open = 0u;
    if (!scpi_port_read_u32(context, &open)) {
        return SCPI_RES_ERR;
    }
    distributed_refmem_set_idle_window(open != 0u);
    SCPI_ResultUInt32(context, open != 0u ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_idle_window_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, distributed_refmem_idle_window() ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_lease_q(scpi_t *context)
{
    refmem_lease_snapshot_t lease;
    memset(&lease, 0, sizeof(lease));
    if (!refmem_lease_table_get(distributed_refmem_local_slot(), &lease)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, lease.owner_slot);
    SCPI_ResultUInt32(context, lease.epoch);
    SCPI_ResultUInt32(context, lease.state);
    SCPI_ResultUInt32(context, lease.expiry_ms);
    SCPI_ResultUInt32(context, lease.lease_seq);
    SCPI_ResultText(context,
                    lease.state == REFMEM_LEASE_GRANTED
                        ? "GRANTED"
                        : (lease.state == REFMEM_LEASE_EXPIRED ? "EXPIRED"
                                                               : "FREE"));
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_cmd_drop(scpi_t *context)
{
    uint32_t count = 0u;
    if (!scpi_port_read_u32(context, &count)) {
        return SCPI_RES_ERR;
    }
    tdma_maintenance_runtime_drop_chunks(count);
    SCPI_ResultUInt32(context, count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_cmd_status_q(scpi_t *context)
{
    refmem_cmd_load_status_t status;
    if (!refmem_cmd_load_get_status(&status)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, status.load_initialized);
    SCPI_ResultUInt32(context, status.submitted);
    SCPI_ResultUInt32(context, status.served);
    SCPI_ResultUInt32(context, status.rejected);
    SCPI_ResultUInt32(context, status.window_closed);
    SCPI_ResultUInt32(context, status.rx_events);
    SCPI_ResultUInt32(context, status.decoded);
    SCPI_ResultUInt32(context, status.posted);
    SCPI_ResultUInt32(context, status.acked);
    SCPI_ResultUInt32(context, status.tx_ok);
    SCPI_ResultUInt32(context, status.tx_fail);
    SCPI_ResultUInt32(context, status.tx_stage);
    SCPI_ResultUInt32(context, status.rx_seen);
    SCPI_ResultUInt32(context, status.rx_delivered);
    SCPI_ResultUInt32(context, status.rx_drop_count);
    SCPI_ResultUInt32(context, status.last_rx_packet_size);
    SCPI_ResultUInt32(context, status.last_rx_payload_class);
    SCPI_ResultUInt32(context, status.rx_config_control);
    SCPI_ResultUInt32(context, status.rx_process_image);
    SCPI_ResultUInt32(context, status.build_class);
    SCPI_ResultUInt32(context, status.build_size);
    SCPI_ResultUInt32(context, status.build_load);
    SCPI_ResultUInt32(context, status.inject_attempts);
    SCPI_ResultUInt32(context, status.inject_ok);
    SCPI_ResultUInt32(context, status.parser_registered);
    SCPI_ResultUInt32(context, status.parser_unknown);
    SCPI_ResultUInt32(context, status.refmem_parser_ok);
    SCPI_ResultUInt32(context, status.refmem_parser_reject);
    SCPI_ResultUInt32(context, status.t2_static_ok);
    SCPI_ResultUInt32(context, status.t2_static_reject);
    SCPI_ResultUInt32(context, status.t2_static_applied);
    SCPI_ResultUInt32(context, status.t2_static_last_config);
    SCPI_ResultUInt32(context, status.t2_static_last_value);
    SCPI_ResultUInt32(context, status.t2_dynamic_ok);
    SCPI_ResultUInt32(context, status.t2_dynamic_reject);
    SCPI_ResultUInt32(context, status.t2_dynamic_applied);
    SCPI_ResultUInt32(context, status.t2_dynamic_last_target);
    SCPI_ResultUInt32(context, status.t2_dynamic_last_run);
    SCPI_ResultUInt32(context, status.t2_dynamic_last_config);
    SCPI_ResultUInt32(context, status.t2_dynamic_last_value);
    SCPI_ResultUInt32(context, status.frag_ok);
    SCPI_ResultUInt32(context, status.frag_reject);
    SCPI_ResultUInt32(context, status.frag_complete);
    SCPI_ResultUInt32(context, status.frag_submit_fail);
    SCPI_ResultUInt32(context, status.frag_pending);
    SCPI_ResultUInt32(context, status.frag_pending_index);
    SCPI_ResultUInt32(context, status.frag_pending_count);
    SCPI_ResultUInt32(context, status.queue_depth);
    SCPI_ResultUInt32(context, status.queue_enqueued);
    SCPI_ResultUInt32(context, status.queue_dequeued);
    SCPI_ResultUInt32(context, status.queue_full);
    SCPI_ResultUInt32(context, status.rx_chunk_rejects);
    SCPI_ResultUInt32(context, status.inject_launched);
    SCPI_ResultUInt32(context, status.inject_dropped_unlaunched);
    SCPI_ResultUInt32(context, status.inject_new_sequence);
    SCPI_ResultUInt32(context, status.inject_repeated_sequence);
    SCPI_ResultUInt32(context, status.inject_last_sequence);
    SCPI_ResultUInt32(context, status.beacon_frames);
    SCPI_ResultUInt32(context, status.rx_polls);
    SCPI_ResultUInt32(context, status.rx_seen_chunks);
    SCPI_ResultUInt32(context, status.rx_accepted_chunks);
    SCPI_ResultUInt32(context, status.rx_dedup_sequence);
    SCPI_ResultUInt32(context, status.rx_bad_mailbox);
    SCPI_ResultUInt32(context, status.rx_last_sequence);
    SCPI_ResultUInt32(context, status.rx_sequence_cursor);
    SCPI_ResultUInt32(context, status.rx_restarts);
    SCPI_ResultUInt32(context, status.rx_duplicate_chunks);
    SCPI_ResultUInt32(context, status.tx_awaiting_ack);
    SCPI_ResultUInt32(context, status.tx_ack_ok);
    SCPI_ResultUInt32(context, status.tx_retries);
    SCPI_ResultUInt32(context, status.tx_ack_fail);
    SCPI_ResultUInt32(context, status.tx_dropped_chunks);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_node_control_q(scpi_t *context)
{
    uint32_t node_id = distributed_refmem_local_slot();
    (void)SCPI_ParamUInt32(context, &node_id, FALSE);

    refmem_command_snapshot_t snapshot;
    uint32_t seq = 0u;
    uint32_t crc32 = 0u;
    if (!distributed_refmem_node_control_read(node_id, &snapshot, &seq,
                                              &crc32)) {
        /* 同 CTRL:STATus?：空记录应答而不是无响应。node_id 仍照实回填，
         * 其余全零表示"该节点尚无命令事实"。 */
        memset(&snapshot, 0, sizeof(snapshot));
        seq = 0u;
        crc32 = 0u;
    }

    SCPI_ResultUInt32(context, node_id);
    SCPI_ResultUInt32(context, seq);
    SCPI_ResultUInt32(context, crc32);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.command_seq);
    SCPI_ResultUInt32(context, snapshot.source_node);
    SCPI_ResultUInt32(context, snapshot.target_mask);
    SCPI_ResultUInt32(context, snapshot.required_mask);
    SCPI_ResultUInt32(context, snapshot.command_type);
    SCPI_ResultUInt32(context, snapshot.command_class);
    SCPI_ResultUInt32(context, snapshot.ack_flags);
    SCPI_ResultUInt32(context, snapshot.nack_flags);
    SCPI_ResultUInt32(context, snapshot.busy_flags);
    SCPI_ResultUInt32(context, snapshot.timeout_flags);
    SCPI_ResultUInt32(context, snapshot.last_reason);
    SCPI_ResultUInt32(context, snapshot.last_reason_slot);
    SCPI_ResultUInt32(context, snapshot.evidence_index);
    SCPI_ResultUInt32(context, snapshot.last_completed_seq);
    SCPI_ResultUInt32(context, snapshot.run_id);
    SCPI_ResultUInt32(context, snapshot.issue_epoch);
    SCPI_ResultUInt32(context, snapshot.payload_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_cmdframe_q(scpi_t *context)
{
    uint32_t calls = 0u;
    uint32_t apply_fail = 0u;
    uint32_t target_miss = 0u;
    uint32_t request_fail = 0u;
    uint32_t ok = 0u;
    distributed_refmem_cmd_frame_counters(&calls, &apply_fail, &target_miss,
                                          &request_fail, &ok);
    SCPI_ResultUInt32(context, calls);
    SCPI_ResultUInt32(context, apply_fail);
    SCPI_ResultUInt32(context, target_miss);
    SCPI_ResultUInt32(context, request_fail);
    SCPI_ResultUInt32(context, ok);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_cmd_reply_q(scpi_t *context)
{
    refmem_cmd_reply_status_t status;
    if (!distributed_refmem_cmd_reply_status(&status)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, status.rx_count);
    SCPI_ResultUInt32(context, status.applied_count);
    SCPI_ResultUInt32(context, status.stale_count);
    SCPI_ResultUInt32(context, status.bad_target_count);
    SCPI_ResultUInt32(context, status.duplicate_count);
    SCPI_ResultUInt32(context, status.armed_count);
    SCPI_ResultUInt32(context, status.published_count);
    SCPI_ResultUInt32(context, status.publish_remaining);
    SCPI_ResultUInt32(context, status.pending_seq);
    SCPI_ResultUInt32(context, status.aggregate_state);
    SCPI_ResultUInt32(context, status.acked_mask);
    SCPI_ResultUInt32(context, status.nacked_mask);
    SCPI_ResultUInt32(context, status.last_slot);
    SCPI_ResultUInt32(context, status.last_state);
    SCPI_ResultUInt32(context, status.last_reason);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_nodes_q(scpi_t *context)
{
    uint32_t offset = 0u;
    uint32_t count = DISTRIBUTED_REFMEM_NODE_COUNT;
    (void)SCPI_ParamUInt32(context, &offset, FALSE);
    (void)SCPI_ParamUInt32(context, &count, FALSE);
    if (offset >= DISTRIBUTED_REFMEM_NODE_COUNT || count == 0u) {
        return SCPI_RES_ERR;
    }
    if (count > DISTRIBUTED_REFMEM_NODE_COUNT - offset) {
        count = DISTRIBUTED_REFMEM_NODE_COUNT - offset;
    }
    for (uint32_t i = offset; i < offset + count; i++) {
        refmem_node_status_t status;
        if (!distributed_refmem_get_node_status(i, &status)) {
            return SCPI_RES_ERR;
        }
        SCPI_ResultUInt32(context, status.node_id);
        SCPI_ResultUInt32(context, status.state);
        SCPI_ResultUInt32(context, status.heartbeat);
        SCPI_ResultUInt32(context, status.slot_version);
        SCPI_ResultUInt32(context, status.last_update_ms);
        SCPI_ResultUInt32(context, status.age_ms);
        SCPI_ResultUInt32(context, status.stale);
        SCPI_ResultUInt32(context, status.stale_count);
        SCPI_ResultUInt32(context, status.fault_code);
        SCPI_ResultUInt32(context, status.flags);
        SCPI_ResultUInt32(context, status.node_type);
        SCPI_ResultUInt32(context, status.control_state);
        SCPI_ResultUInt32(context, status.ack_flags);
        SCPI_ResultUInt32(context, status.nack_flags);
        SCPI_ResultUInt32(context, status.last_reason);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_command_status_q(scpi_t *context)
{
    (void)context;
    refmem_command_aggregate_t aggregate;
    if (!distributed_refmem_command_aggregate(&aggregate)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, aggregate.node_count);
    SCPI_ResultUInt32(context, aggregate.posted);
    SCPI_ResultUInt32(context, aggregate.taken);
    SCPI_ResultUInt32(context, aggregate.busy);
    SCPI_ResultUInt32(context, aggregate.acked);
    SCPI_ResultUInt32(context, aggregate.nacked);
    SCPI_ResultUInt32(context, aggregate.timed_out);
    SCPI_ResultUInt32(context, aggregate.last_state);
    SCPI_ResultUInt32(context, aggregate.last_reason);
    SCPI_ResultUInt32(context, aggregate.last_reason_slot);
    SCPI_ResultUInt32(context, aggregate.last_completed_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_command_target_q(scpi_t *context)
{
    uint32_t slot = 0u;
    if (!SCPI_ParamUInt32(context, &slot, TRUE)) {
        return SCPI_RES_ERR;
    }
    refmem_command_snapshot_t snapshot;
    if (!distributed_refmem_node_control_read(slot, &snapshot, NULL, NULL)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, slot);
    SCPI_ResultUInt32(context, snapshot.command_seq);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.command_type);
    SCPI_ResultUInt32(context, snapshot.target_mask);
    SCPI_ResultUInt32(context, snapshot.required_mask);
    SCPI_ResultUInt32(context, snapshot.ack_flags);
    SCPI_ResultUInt32(context, snapshot.nack_flags);
    SCPI_ResultUInt32(context, snapshot.last_reason);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ctrl_execute(scpi_t *context)
{
    const char *token = NULL;
    size_t token_len = 0u;
    if (!SCPI_ParamCharacters(context, &token, &token_len, TRUE) ||
        token == NULL || token_len == 0u) {
        return SCPI_RES_ERR;
    }

    const refmem_catalog_entry_t *entry = NULL;
    bool numeric = true;
    uint32_t id = 0u;
    for (size_t i = 0u; i < token_len; i++) {
        if (token[i] < '0' || token[i] > '9') {
            numeric = false;
            break;
        }
        id = id * 10u + (uint32_t)(token[i] - '0');
    }
    if (numeric) {
        entry = refmem_command_catalog_find_by_id((uint16_t)id);
    } else {
        char name[40];
        if (token_len >= sizeof(name)) {
            scpi_port_push_exec_error(context, "UNSUPPORTED_COMMAND");
            return SCPI_RES_ERR;
        }
        memcpy(name, token, token_len);
        name[token_len] = '\0';
        entry = refmem_command_catalog_find_by_name(name);
    }
    if (entry == NULL) {
        scpi_port_push_exec_error(context, "UNSUPPORTED_COMMAND");
        return SCPI_RES_ERR;
    }

    uint32_t target_mask = 0u;
    uint32_t required_mask = 0u;
    if (!scpi_port_read_u32(context, &target_mask) ||
        !scpi_port_read_u32(context, &required_mask) ||
        target_mask == 0u || (required_mask & ~target_mask) != 0u) {
        scpi_port_push_exec_error(context, "BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }

    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS];
    memset(params, 0, sizeof(params));
    for (uint32_t i = 0u; i < entry->param_words; i++) {
        if (!scpi_port_read_u32(context, &params[i])) {
            scpi_port_push_exec_error(context, "BAD_ARGUMENT");
            return SCPI_RES_ERR;
        }
    }
    if (!refmem_command_catalog_validate_params(
            entry, entry->param_words, entry->param_kind)) {
        scpi_port_push_exec_error(context, "BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }

    /* N5.5 entry convergence: only the derived control owner has a usable
     * TDMA maintenance uplink for remote commands.  A non-owner entry must
     * fail closed with an explicit error instead of queueing an intent that
     * can never be delivered. */
    const uint32_t local_bit = 1u << distributed_refmem_local_slot();
    if ((target_mask & ~local_bit) != 0u) {
        /* Remote targets need the TDMA maintenance transport.  Refuse while
         * the ring is down; otherwise the record would stay POSTED with no
         * possible reply and block the single command slot until reboot.
         * Checked before the owner/model lookup so a stopped ring cannot block
         * inside the model path. */
        if (!distributed_refmem_ring_transport_ready()) {
            scpi_port_push_exec_error(context, "NOT_ONLINE_SAFE");
            return SCPI_RES_ERR;
        }
        uint32_t control_owner = 0u;
        const bool owner_valid =
            refmem_application_model_control_owner_slot(&control_owner);
        if (refmem_control_entry_decide(distributed_refmem_local_slot(),
                                        control_owner,
                                        owner_valid,
                                        target_mask) ==
            REFMEM_CONTROL_ENTRY_REJECT_REMOTE) {
            scpi_port_push_exec_error(context, "NOT_CONTROL_OWNER");
            return SCPI_RES_ERR;
        }
    }

    uint32_t command_seq = 0u;
    if (!distributed_refmem_control_submit(entry->id, target_mask,
                                           required_mask, params,
                                           (uint16_t)entry->param_words,
                                           &command_seq)) {
        scpi_port_push_exec_error(context, "REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "QUEUED");
    SCPI_ResultUInt32(context, command_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ctrl_status_q(scpi_t *context)
{
    refmem_command_snapshot_t snapshot;
    uint32_t seq = 0u;
    uint32_t crc32 = 0u;
    if (!distributed_refmem_node_control_read(distributed_refmem_local_slot(),
                                              &snapshot, &seq, &crc32)) {
        /* 空记录（seq==0）或 CRC 暂不匹配：必须给出确定应答。
         * 返回 SCPI_RES_ERR 会让本查询完全没有响应，并把执行错误留在队列里，
         * 使后续查询被错配成 -113 "Undefined header"（四板实测 2026-10-27）。
         * 全零 = "本板尚无命令事实"，这是可判读的状态，不是失败。 */
        memset(&snapshot, 0, sizeof(snapshot));
        seq = 0u;
        crc32 = 0u;
    }
    SCPI_ResultUInt32(context, seq);
    SCPI_ResultUInt32(context, crc32);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.command_seq);
    SCPI_ResultUInt32(context, snapshot.target_mask);
    SCPI_ResultUInt32(context, snapshot.required_mask);
    SCPI_ResultUInt32(context, snapshot.ack_flags);
    SCPI_ResultUInt32(context, snapshot.nack_flags);
    SCPI_ResultUInt32(context, snapshot.busy_flags);
    SCPI_ResultUInt32(context, snapshot.timeout_flags);
    SCPI_ResultUInt32(context, snapshot.last_reason);
    SCPI_ResultUInt32(context, snapshot.last_reason_slot);
    SCPI_ResultUInt32(context, snapshot.last_completed_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ctrl_owner_q(scpi_t *context)
{
    uint32_t owner_slot = 0u;
    const bool owner_valid =
        refmem_application_model_control_owner_slot(&owner_slot);
    const uint32_t local_slot = distributed_refmem_local_slot();
    SCPI_ResultUInt32(context, owner_valid ? owner_slot : 0xFFFFFFFFu);
    SCPI_ResultUInt32(context, owner_valid ? 1u : 0u);
    SCPI_ResultUInt32(context, local_slot);
    SCPI_ResultUInt32(context, (owner_valid && owner_slot == local_slot) ? 1u : 0u);
    (void)context;
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ctrl_catalog_q(scpi_t *context)
{
    uint32_t offset = 0u;
    uint32_t count = refmem_command_catalog_count;
    (void)SCPI_ParamUInt32(context, &offset, FALSE);
    (void)SCPI_ParamUInt32(context, &count, FALSE);
    if (count > 16u) {
        count = 16u;
    }
    for (uint32_t i = offset;
         i < refmem_command_catalog_count && i < offset + count; i++) {
        const refmem_catalog_entry_t *entry = &refmem_command_catalog[i];
        SCPI_ResultUInt32(context, entry->id);
        SCPI_ResultText(context, entry->name);
        SCPI_ResultUInt32(context, entry->command_type);
        SCPI_ResultUInt32(context, entry->owner);
        SCPI_ResultUInt32(context, entry->param_words);
        SCPI_ResultUInt32(context, entry->param_kind);
        SCPI_ResultUInt32(context, entry->realtime);
    }
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

scpi_result_t scpi_cmd_refmem_table_image_q(scpi_t *context)
{
    uint32_t role = REFMEM_TABLE_IMAGE_ACTIVE;
    (void)SCPI_ParamUInt32(context, &role, FALSE);

    refmem_table_image_descriptor_t descriptor;
    if (!refmem_table_registry_get_image_descriptor((refmem_table_image_role_t)role,
                                                    &descriptor)) {
        return SCPI_RES_ERR;
    }

    scpi_refmem_result_table_image_descriptor(context, &descriptor);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_table_view_q(scpi_t *context)
{
    uint32_t role = REFMEM_TABLE_IMAGE_ACTIVE;
    uint32_t table_id = REFMEM_APP_TABLE_APPLICATION_MAP;
    (void)SCPI_ParamUInt32(context, &role, FALSE);
    (void)SCPI_ParamUInt32(context, &table_id, FALSE);

    refmem_table_view_t view;
    if (!refmem_table_registry_access_table((refmem_table_image_role_t)role,
                                            table_id,
                                            &view)) {
        return SCPI_RES_ERR;
    }

    const uint32_t first_u32 =
        view.size >= sizeof(uint32_t) ? scpi_refmem_read_u32_le(view.data) : 0u;
    SCPI_ResultUInt32(context, view.version);
    SCPI_ResultUInt32(context, view.role);
    SCPI_ResultUInt32(context, view.table_id);
    SCPI_ResultUInt32(context, view.table_seq);
    SCPI_ResultUInt32(context, view.package_crc32);
    SCPI_ResultUInt32(context, view.table_crc32);
    SCPI_ResultUInt32(context, view.image_offset);
    SCPI_ResultUInt32(context, view.image_size);
    SCPI_ResultUInt32(context, first_u32);
    (void)refmem_table_registry_release_table(&view);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_quality_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t index = 0u;
    (void)SCPI_ParamUInt32(context, &index, FALSE);

    refmem_pio_spi_adapter_snapshot_t adapter;
    if (!scpi_refmem_get_adapter_snapshot(&adapter)) {
        return SCPI_RES_ERR;
    }

    refmem_realtime_tdma_snapshot_t tdma;
    if (!distributed_refmem_get_realtime_tdma(&tdma)) {
        return SCPI_RES_ERR;
    }

    const refmem_application_model_snapshot_t *model =
        refmem_application_model_get_snapshot();
    refmem_quality_runtime_table_t table;
    if (!refmem_quality_build_runtime_table(
            model != NULL ? model->connection_quality_crc32 : 0u,
            scpi_refmem_sync_context(),
            &adapter,
            &tdma,
            &table)) {
        return SCPI_RES_ERR;
    }

    const refmem_connection_quality_entry_t *entry =
        refmem_quality_get_entry(&table, index);
    if (entry == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, index);
    SCPI_ResultUInt32(context, table.version);
    SCPI_ResultUInt32(context, table.entry_count);
    SCPI_ResultUInt32(context, table.active_table_crc32);
    SCPI_ResultUInt32(context, table.local_slot);
    SCPI_ResultUInt32(context, table.epoch_id);
    SCPI_ResultUInt32(context, table.run_id);
    SCPI_ResultUInt32(context, table.overflow_count);
    SCPI_ResultUInt32(context, entry->quality_id);
    SCPI_ResultUInt32(context, entry->scope);
    SCPI_ResultUInt32(context, entry->source_node);
    SCPI_ResultUInt32(context, entry->target_node);
    SCPI_ResultUInt32(context, entry->seq_expected);
    SCPI_ResultUInt32(context, entry->seq_last);
    SCPI_ResultUInt32(context, entry->crc_error_count);
    SCPI_ResultUInt32(context, entry->stale_count);
    SCPI_ResultUInt32(context, entry->late_count);
    SCPI_ResultUInt32(context, entry->drop_count);
    SCPI_ResultUInt32(context, entry->timeout_count);
    SCPI_ResultUInt32(context, entry->last_error);
    SCPI_ResultUInt32(context, entry->last_error_tick);
    SCPI_ResultUInt32(context, entry->p99);
    SCPI_ResultUInt32(context, entry->p999);
    SCPI_ResultUInt32(context, entry->evidence_index);
    return SCPI_RES_OK;
}

static const refmem_function_entry_t *scpi_function_lookup(
    scpi_t *context)
{
    const char *token = NULL;
    size_t token_len = 0u;
    if (!SCPI_ParamCharacters(context, &token, &token_len, TRUE) ||
        token == NULL || token_len == 0u) {
        return NULL;
    }
    bool numeric = true;
    uint32_t id = 0u;
    for (size_t i = 0u; i < token_len; ++i) {
        if (token[i] < '0' || token[i] > '9') {
            numeric = false;
            break;
        }
        id = id * 10u + (uint32_t)(token[i] - '0');
    }
    if (numeric) {
        if (id > 0xFFFFu) {
            return NULL;
        }
        return refmem_function_find_by_id((uint16_t)id);
    }
    char name[40];
    if (token_len >= sizeof(name)) {
        return NULL;
    }
    memcpy(name, token, token_len);
    name[token_len] = '\0';
    return refmem_function_find_by_name(name);
}

scpi_result_t scpi_cmd_ctrl_function_run(scpi_t *context)
{
    const refmem_function_entry_t *entry = scpi_function_lookup(context);
    if (entry == NULL) {
        scpi_port_push_exec_error(context, "UNSUPPORTED_FUNCTION");
        return SCPI_RES_ERR;
    }
    uint32_t target_mask = 0u;
    uint32_t required_mask = 0u;
    if (!scpi_port_read_u32(context, &target_mask) ||
        !scpi_port_read_u32(context, &required_mask) ||
        target_mask == 0u || (required_mask & ~target_mask) != 0u) {
        scpi_port_push_exec_error(context, "BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }
    uint32_t args[REFMEM_FUNCTION_ARG_MAX];
    memset(args, 0, sizeof(args));
    for (uint32_t i = 0u; i < entry->arg_words; ++i) {
        if (!scpi_port_read_u32(context, &args[i])) {
            scpi_port_push_exec_error(context, "BAD_ARGUMENT");
            return SCPI_RES_ERR;
        }
    }
    uint32_t function_seq = 0u;
    if (!refmem_function_run(entry->id, target_mask, required_mask, args,
                             entry->arg_words, &function_seq)) {
        scpi_port_push_exec_error(context, "BUSY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "QUEUED");
    SCPI_ResultUInt32(context, function_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ctrl_function_status_q(scpi_t *context)
{
    uint32_t function_seq = 0u;
    if (!scpi_port_read_u32(context, &function_seq) || function_seq == 0u) {
        scpi_port_push_exec_error(context, "BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }
    refmem_function_snapshot_t snapshot;
    if (!refmem_function_get_status(function_seq, &snapshot)) {
        scpi_port_push_exec_error(context, "NOT_FOUND");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.function_seq);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.function_id);
    SCPI_ResultUInt32(context, snapshot.step_count);
    SCPI_ResultUInt32(context, snapshot.step_index);
    SCPI_ResultUInt32(context, snapshot.step_command_id);
    SCPI_ResultUInt32(context, snapshot.step_command_seq);
    SCPI_ResultUInt32(context, snapshot.step_state);
    SCPI_ResultUInt32(context, snapshot.step_ack_mask);
    SCPI_ResultUInt32(context, snapshot.step_nack_mask);
    SCPI_ResultUInt32(context, snapshot.last_reason);
    SCPI_ResultUInt32(context, snapshot.started_ms);
    SCPI_ResultUInt32(context, snapshot.step_started_ms);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ctrl_function_catalog_q(scpi_t *context)
{
    uint32_t offset = 0u;
    uint32_t count = refmem_function_catalog_count;
    (void)SCPI_ParamUInt32(context, &offset, FALSE);
    (void)SCPI_ParamUInt32(context, &count, FALSE);
    if (count > 8u) {
        count = 8u;
    }
    for (uint32_t i = offset;
         i < refmem_function_catalog_count && i < offset + count; i++) {
        const refmem_function_entry_t *entry = &refmem_function_catalog[i];
        SCPI_ResultUInt32(context, entry->id);
        SCPI_ResultText(context, entry->name);
        SCPI_ResultUInt32(context, entry->arg_words);
        SCPI_ResultUInt32(context, entry->step_count);
        SCPI_ResultUInt32(context, entry->failure_policy);
        for (uint32_t s = 0u; s < entry->step_count; ++s) {
            SCPI_ResultUInt32(context, entry->steps[s].command_id);
            SCPI_ResultUInt32(context, entry->steps[s].arg_base);
            SCPI_ResultUInt32(context, entry->steps[s].arg_words);
        }
    }
    return SCPI_RES_OK;
}

/* Fill one local slot row from this board's provider.  Returns false when the
 * key has no provider on this board (the caller then publishes a stale row).
 * `epoch/seq` are placeholders until the R1-3 publish plane supplies the real
 * publisher counters; `age_ms` is 0 because the snapshot is taken now. */
static bool refmem_obs_fill_local_row(uint32_t key, refmem_obs_row_t *row)
{
    if (row == NULL) {
        return false;
    }
    switch (key) {
    case 1u:
    case 2u:
        /* switch1/switch2: the configured switch state is local to this board. */
        row->row_words = 2u;
        row->row[0] = sync_io_sequence_get_switch(key);
        row->row[1] = sync_io_sequence_get_switch_desired(key);
        return true;
    case 4u: {
        /* guide_plan: the planner snapshot is local; the row mirrors the
         * BOOT/PLAN progress readback so a single OBS key carries the round
         * verdict without five separate queries. */
        app_cal_guide_snapshot_t guide;
        if (!app_cal_guide_snapshot(&guide)) {
            return false;
        }
        row->row_words = 7u;
        row->row[0] = guide.action_id;
        row->row[1] = guide.generation;
        row->row[2] = guide.progress_status;
        row->row[3] = guide.trials_done;
        row->row[4] = guide.passed_mask;
        row->row[5] = guide.failed_mask;
        row->row[6] = guide.abort_site;
        return true;
    }
    case 5u: {
        /* boot_plan: the boot service state is local to this board. */
        tdma_boot_plan_t plan;
        if (!app_guide_boot_plan_get(&plan)) {
            return false;
        }
        tdma_boot_profile_t profile;
        const bool have_profile = app_guide_boot_profile_get(&profile);
        row->row_words = 5u;
        row->row[0] = (uint32_t)plan.action;
        row->row[1] = app_guide_boot_fail_step();
        row->row[2] = app_guide_boot_done() ? 1u : 0u;
        row->row[3] = app_guide_boot_half_ring() ? 1u : 0u;
        row->row[4] = app_guide_boot_profile_is_default()
                          ? 2u
                          : (have_profile ? 1u : 0u);
        return true;
    }
    case 6u: {
        /* ring_status: the ring runtime snapshot is local to this board. */
        tdma_ring_runtime_snapshot_t ring;
        if (!tdma_runtime_owner_get_ring_snapshot(&ring)) {
            return false;
        }
        row->row_words = 6u;
        row->row[0] = ring.enabled;
        row->row[1] = ring.up_running;
        row->row[2] = ring.down_running;
        row->row[3] = (ring.up_configured != 0u && ring.down_configured != 0u)
                          ? 1u
                          : 0u;
        row->row[4] = ring.node_count;
        row->row[5] = ring.local_slot_id;
        return true;
    }
    case 7u: {
        /* cal_status: the calibration training store is the local writer. */
        calibration_training_store_status_t store;
        calibration_manager_get_training_store_status(&store);
        row->row_words = 3u;
        row->row[0] = store.persisted_valid;
        row->row[1] = store.record_generation;
        row->row[2] = store.reject_reason;
        return true;
    }
    default:
        return false;
    }
}

/* REFMEM-READ-01 observation read. The wire format and the stale/valid rules
 * are owned by refmem_obs; this handler parses the paging arguments, fills one
 * row per slot, and maps contract error codes onto SCPI execution errors. Only
 * the local slot can be valid today: peer rows are filled by the R1-3 publish
 * plane (not implemented), so they answer valid=0/age=0xFFFFFFFF/stale=1
 * instead of borrowing the local provider value. */
scpi_result_t scpi_cmd_refmem_obs_q(scpi_t *context)
{
    uint32_t key = 0u;
    uint32_t offset = 0u;
    uint32_t count = 0u;
    if (!SCPI_ParamUInt32(context, &key, TRUE) ||
        !SCPI_ParamUInt32(context, &offset, TRUE) ||
        !SCPI_ParamUInt32(context, &count, FALSE)) {
        scpi_port_push_exec_error(context, "OBS_ARG");
        return SCPI_RES_ERR;
    }
    if (key < 1u || key > 255u || refmem_obs_schema_words((uint8_t)key) == 0u) {
        scpi_port_push_exec_error(context, "OBS_KEY");
        return SCPI_RES_ERR;
    }
    if (offset >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        scpi_port_push_exec_error(context, "OBS_OFFSET");
        return SCPI_RES_ERR;
    }
    if (count == 0u || count > DISTRIBUTED_REFMEM_NODE_COUNT - offset) {
        count = DISTRIBUTED_REFMEM_NODE_COUNT - offset;
    }
    if (count > REFMEM_OBS_MAX_ROWS) {
        count = REFMEM_OBS_MAX_ROWS;
    }

    const uint32_t local_slot = distributed_refmem_local_slot();
    refmem_obs_result_t result;
    memset(&result, 0, sizeof(result));
    result.key = key;
    result.offset = offset;
    result.count = count;

    for (uint32_t i = 0u; i < count; i++) {
        refmem_obs_row_t *row = &result.rows[i];
        row->slot = offset + i;
        row->row_words = refmem_obs_schema_words((uint8_t)key);
        if (row->row_words > REFMEM_OBS_MAX_ROW_WORDS) {
            row->row_words = REFMEM_OBS_MAX_ROW_WORDS;
        }
        if (offset + i != local_slot ||
            !refmem_obs_fill_local_row(key, row)) {
            /* No publisher for this slot on this board: honest stale row. */
            row->valid = 0u;
            row->epoch = 0u;
            row->seq = 0u;
            row->age_ms = REFMEM_OBS_AGE_INVALID;
            row->stale = 1u;
            continue;
        }
        /* Local snapshot taken now; real seq/epoch arrive with R1-3. */
        row->valid = 1u;
        row->epoch = 1u;
        row->seq = 1u;
        row->age_ms = 0u;
        row->stale = 0u;
    }

    char text[512];
    if (refmem_obs_encode(&result, text, sizeof(text)) != REFMEM_OBS_OK) {
        scpi_port_push_exec_error(context, "OBS_ENCODE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, text);
    return SCPI_RES_OK;
}

