#include "scpi_system_snapshot_commands.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
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
#include "scpi_port_internal.h"
#include "storage_manager.h"
#include "system_manager.h"
#include "sync_trigger.h"

#define SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS 10000u
#define SCPI_REFMEM_PACKAGE_PATH "/refmem/app_model.rmtp"
#define SCPI_REFMEM_PACKAGE_READ_CHUNK 512u
#define SCPI_REFMEM_LOAD_MAX_BYTES 8192u
#define SCPI_REFMEM_SYNC_FRAME_MAX (REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX)
#define SCPI_REFMEM_SYNC_HEX_MAX ((SCPI_REFMEM_SYNC_FRAME_MAX * 2u) + 1u)
#define SCPI_REFMEM_SYNC_DEFAULT_EPOCH 1u
#define SCPI_REFMEM_SYNC_DEFAULT_RUN 1u
#define SCPI_REFMEM_SYNC_DEFAULT_MAX_PAYLOAD REFMEM_SYNC_FRAME_PAYLOAD_MAX
#define SCPI_REFMEM_SYNC_DEFAULT_MTU SCPI_REFMEM_SYNC_FRAME_MAX
#define SCPI_REFMEM_SYNC_DEFAULT_LATENCY_US 50u
#define SCPI_REFMEM_SYNC_SPI_RAW_MAX 256u
#define SCPI_COMMAND_ACK_SCHEMA_VERSION 1u

typedef struct {
    uint32_t reason_id;
    uint32_t severity;
    uint32_t retryable;
    uint32_t blocking;
    uint32_t detail_code;
    const char *name;
} scpi_command_reason_entry_t;

typedef struct {
    refmem_sync_context_t context;
    refmem_pio_spi_adapter_t adapter;
    refmem_spi_physical_adapter_t spi_adapter;
    uint32_t tx_seq32;
    refmem_sync_rx_snapshot_t last_rx;
    uint32_t initialized;
} scpi_refmem_sync_state_t;

static scpi_refmem_sync_state_t s_refmem_sync;
static uint8_t s_refmem_package_buffer[SCPI_REFMEM_LOAD_MAX_BYTES];
static uint8_t s_refmem_package_chunk[SCPI_REFMEM_PACKAGE_READ_CHUNK];

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
static void scpi_refmem_result_table_image_descriptor(
    scpi_t *context,
    const refmem_table_image_descriptor_t *descriptor);
static uint32_t scpi_refmem_read_u32_le(const uint8_t *data);
static bool scpi_refmem_sync_ensure_initialized(void);
static void scpi_refmem_sync_apply_node_load_delta(const refmem_sync_rx_snapshot_t *rx);
static uint32_t scpi_refmem_sync_build_id_crc32(void);
static bool scpi_refmem_sync_build_hello_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size);
static bool scpi_refmem_sync_build_epoch_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size);
static bool scpi_refmem_sync_build_delta_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint8_t slot_id,
                                               uint32_t slot_seq,
                                               uint16_t field_id,
                                               uint32_t value,
                                               uint32_t dirty_mask,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size);
static bool scpi_refmem_sync_build_ack_frame(uint8_t source_slot,
                                             uint8_t target_mask,
                                             uint32_t seq32,
                                             const refmem_sync_rx_snapshot_t *rx,
                                             uint8_t *frame,
                                             size_t frame_capacity,
                                             size_t *frame_size);
static bool scpi_refmem_sync_build_fence_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint32_t fence_seq,
                                               uint32_t fence_scope,
                                               uint32_t required_mask,
                                               uint32_t min_table_seq,
                                               uint32_t deadline_1e3ns,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size);
static bool scpi_refmem_sync_build_quality_frame(uint8_t source_slot,
                                                 uint8_t target_mask,
                                                 uint32_t seq32,
                                                 uint32_t quality_id,
                                                 uint32_t scope,
                                                 uint32_t target_slot,
                                                 uint8_t *frame,
                                                 size_t frame_capacity,
                                                 size_t *frame_size);
static void scpi_refmem_sync_result_rx_snapshot(scpi_t *context,
                                                const refmem_sync_rx_snapshot_t *snapshot);
static void scpi_refmem_sync_hex_encode(const uint8_t *data,
                                        size_t data_size,
                                        char *hex,
                                        size_t hex_size);
static bool scpi_refmem_sync_hex_decode(const char *hex,
                                        size_t hex_len,
                                        uint8_t *output,
                                        size_t output_size,
                                        size_t *decoded_size);
static const scpi_command_reason_entry_t *
scpi_command_find_reason(uint32_t reason_id);

static const scpi_command_reason_entry_t s_command_reason_table[] = {
    {REFMEM_COMMAND_REASON_NONE, 0u, 0u, 0u, 0u, "NONE"},
    {REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH, 2u, 0u, 1u, 1u, "CONFIG_CRC_MISMATCH"},
    {REFMEM_COMMAND_REASON_HW_PROFILE_MISMATCH, 2u, 0u, 1u, 2u, "HW_PROFILE_MISMATCH"},
    {REFMEM_COMMAND_REASON_NODE_STALE, 2u, 1u, 1u, 3u, "NODE_STALE"},
    {REFMEM_COMMAND_REASON_NODE_FAULT, 3u, 0u, 1u, 4u, "NODE_FAULT"},
    {REFMEM_COMMAND_REASON_FLASH_LOCKOUT_UNREADY, 3u, 1u, 1u, 5u, "FLASH_LOCKOUT_UNREADY"},
    {REFMEM_COMMAND_REASON_RESOURCE_BUSY, 2u, 1u, 1u, 6u, "RESOURCE_BUSY"},
    {REFMEM_COMMAND_REASON_RUN_STATE_DENIED, 2u, 0u, 1u, 7u, "RUN_STATE_DENIED"},
    {REFMEM_COMMAND_REASON_PAYLOAD_CRC_MISMATCH, 2u, 0u, 1u, 8u, "PAYLOAD_CRC_MISMATCH"},
    {REFMEM_COMMAND_REASON_EPOCH_MISMATCH, 2u, 1u, 1u, 9u, "EPOCH_MISMATCH"},
    {REFMEM_COMMAND_REASON_DUP_SEQ_CRC_MISMATCH, 2u, 0u, 1u, 10u, "DUP_SEQ_CRC_MISMATCH"},
    {REFMEM_COMMAND_REASON_TIMEOUT, 3u, 1u, 1u, 11u, "TIMEOUT"},
    {REFMEM_COMMAND_REASON_PERMISSION_DENIED, 2u, 0u, 1u, 12u, "PERMISSION_DENIED"},
    {REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED, 2u, 0u, 1u, 13u, "CONFIG_VALIDATION_FAILED"},
};

static const scpi_command_reason_entry_t *
scpi_command_find_reason(uint32_t reason_id)
{
    for (uint32_t i = 0u;
         i < (uint32_t)(sizeof(s_command_reason_table) / sizeof(s_command_reason_table[0]));
         i++) {
        if (s_command_reason_table[i].reason_id == reason_id) {
            return &s_command_reason_table[i];
        }
    }
    return NULL;
}

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
    if ((uint32_t)vector.manifest_status == REFMEM_APP_MODEL_SD_MANIFEST_OK &&
        vector.manifest_missing_count == 0u) {
        package_read = scpi_refmem_read_package(load_path,
                                                s_refmem_package_buffer,
                                                sizeof(s_refmem_package_buffer),
                                                &package_size);
        package_valid = package_read &&
                        refmem_table_registry_validate_package(s_refmem_package_buffer,
                                                               package_size,
                                                               &validation);
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
                                                package_valid ? s_refmem_package_buffer : NULL,
                                                package_valid ? package_size : 0u,
                                                validation.table_crc32,
                                                REFMEM_TABLE_REGISTRY_COUNT,
                                                validation.owner_validated_table_mask,
                                                validation.first_bad_table);

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
    if (!refmem_pio_spi_adapter_get_snapshot(&s_refmem_sync.adapter, &adapter)) {
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
            &s_refmem_sync.context,
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

scpi_result_t scpi_cmd_refmem_sync_init(scpi_t *context)
{
    uint32_t local_slot = 0u;
    uint32_t epoch_id = SCPI_REFMEM_SYNC_DEFAULT_EPOCH;
    uint32_t run_id = SCPI_REFMEM_SYNC_DEFAULT_RUN;
    if (!scpi_port_read_u32(context, &local_slot)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &epoch_id, FALSE);
    (void)SCPI_ParamUInt32(context, &run_id, FALSE);
    if (local_slot >= REFMEM_SYNC_NODE_COUNT ||
        !refmem_sync_init(&s_refmem_sync.context, (uint8_t)local_slot, epoch_id, run_id) ||
        !refmem_pio_spi_adapter_init(&s_refmem_sync.adapter,
                                     SCPI_REFMEM_SYNC_DEFAULT_MAX_PAYLOAD,
                                     SCPI_REFMEM_SYNC_DEFAULT_MTU,
                                     SCPI_REFMEM_SYNC_DEFAULT_LATENCY_US)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_INIT");
        return SCPI_RES_ERR;
    }

    memset(&s_refmem_sync.last_rx, 0, sizeof(s_refmem_sync.last_rx));
    s_refmem_sync.tx_seq32 = 1u;
    s_refmem_sync.initialized = 1u;

    refmem_transport_caps_t caps;
    (void)refmem_pio_spi_adapter_get_caps(&s_refmem_sync.adapter, &caps);
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, s_refmem_sync.context.local_slot);
    SCPI_ResultUInt32(context, s_refmem_sync.context.active_epoch_id);
    SCPI_ResultUInt32(context, s_refmem_sync.context.active_run_id);
    SCPI_ResultUInt32(context, caps.adapter_id);
    SCPI_ResultUInt32(context, caps.max_payload_size);
    SCPI_ResultUInt32(context, caps.preferred_mtu);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_hello_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = s_refmem_sync.context.local_slot;
    uint32_t target_mask = 0xFFu;
    uint32_t seq32 = 0u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &seq32, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT || target_mask > 0xFFu) {
        return SCPI_RES_ERR;
    }
    if (seq32 == 0u) {
        seq32 = s_refmem_sync.tx_seq32++;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!scpi_refmem_sync_build_hello_frame((uint8_t)source_slot,
                                            (uint8_t)target_mask,
                                            seq32,
                                            frame,
                                            sizeof(frame),
                                            &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_HELLO");
        return SCPI_RES_ERR;
    }

    refmem_sync_frame_header_t header;
    (void)refmem_sync_frame_decode_header(frame, frame_size, &header);
    char hex[SCPI_REFMEM_SYNC_HEX_MAX];
    scpi_refmem_sync_hex_encode(frame, frame_size, hex, sizeof(hex));
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, frame_size);
    SCPI_ResultUInt32(context, header.source_slot);
    SCPI_ResultUInt32(context, header.target_mask);
    SCPI_ResultUInt32(context, header.epoch_id);
    SCPI_ResultUInt32(context, header.run_id);
    SCPI_ResultUInt32(context, header.seq32);
    SCPI_ResultUInt32(context, header.payload_crc32);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_epoch_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = s_refmem_sync.context.local_slot;
    uint32_t target_mask = 0xFFu;
    uint32_t seq32 = 0u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &seq32, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT || target_mask > 0xFFu) {
        return SCPI_RES_ERR;
    }
    if (seq32 == 0u) {
        seq32 = s_refmem_sync.tx_seq32++;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!scpi_refmem_sync_build_epoch_frame((uint8_t)source_slot,
                                            (uint8_t)target_mask,
                                            seq32,
                                            frame,
                                            sizeof(frame),
                                            &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_EPOCH");
        return SCPI_RES_ERR;
    }

    refmem_sync_frame_header_t header;
    (void)refmem_sync_frame_decode_header(frame, frame_size, &header);
    char hex[SCPI_REFMEM_SYNC_HEX_MAX];
    scpi_refmem_sync_hex_encode(frame, frame_size, hex, sizeof(hex));
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, frame_size);
    SCPI_ResultUInt32(context, header.source_slot);
    SCPI_ResultUInt32(context, header.target_mask);
    SCPI_ResultUInt32(context, header.epoch_id);
    SCPI_ResultUInt32(context, header.run_id);
    SCPI_ResultUInt32(context, header.seq32);
    SCPI_ResultUInt32(context, header.payload_crc32);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_delta_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = s_refmem_sync.context.local_slot;
    uint32_t target_mask = 0xFFu;
    uint32_t seq32 = 0u;
    uint32_t slot_id = source_slot;
    uint32_t slot_seq = 1u;
    uint32_t field_id = 0u;
    uint32_t value = 0u;
    uint32_t dirty_mask = 1u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &seq32, FALSE);
    (void)SCPI_ParamUInt32(context, &slot_id, FALSE);
    (void)SCPI_ParamUInt32(context, &slot_seq, FALSE);
    (void)SCPI_ParamUInt32(context, &field_id, FALSE);
    (void)SCPI_ParamUInt32(context, &value, FALSE);
    (void)SCPI_ParamUInt32(context, &dirty_mask, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT ||
        slot_id >= REFMEM_SYNC_NODE_COUNT ||
        target_mask > 0xFFu ||
        field_id > 0xFFFFu) {
        return SCPI_RES_ERR;
    }
    if (seq32 == 0u) {
        seq32 = s_refmem_sync.tx_seq32++;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!scpi_refmem_sync_build_delta_frame((uint8_t)source_slot,
                                            (uint8_t)target_mask,
                                            seq32,
                                            (uint8_t)slot_id,
                                            slot_seq,
                                            (uint16_t)field_id,
                                            value,
                                            dirty_mask,
                                            frame,
                                            sizeof(frame),
                                            &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_DELTA");
        return SCPI_RES_ERR;
    }

    refmem_sync_frame_header_t header;
    (void)refmem_sync_frame_decode_header(frame, frame_size, &header);
    char hex[SCPI_REFMEM_SYNC_HEX_MAX];
    scpi_refmem_sync_hex_encode(frame, frame_size, hex, sizeof(hex));
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, frame_size);
    SCPI_ResultUInt32(context, header.source_slot);
    SCPI_ResultUInt32(context, header.target_mask);
    SCPI_ResultUInt32(context, header.epoch_id);
    SCPI_ResultUInt32(context, header.run_id);
    SCPI_ResultUInt32(context, header.seq32);
    SCPI_ResultUInt32(context, header.payload_crc32);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_ack_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }
    if (s_refmem_sync.last_rx.header.header_size != REFMEM_SYNC_FRAME_HEADER_SIZE ||
        s_refmem_sync.last_rx.header.source_slot >= REFMEM_SYNC_NODE_COUNT) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_ACK_NO_RX");
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = s_refmem_sync.context.local_slot;
    uint32_t target_mask = (uint32_t)(1u << s_refmem_sync.last_rx.header.source_slot);
    uint32_t seq32 = 0u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &seq32, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT || target_mask > 0xFFu) {
        return SCPI_RES_ERR;
    }
    if (seq32 == 0u) {
        seq32 = s_refmem_sync.tx_seq32++;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!scpi_refmem_sync_build_ack_frame((uint8_t)source_slot,
                                          (uint8_t)target_mask,
                                          seq32,
                                          &s_refmem_sync.last_rx,
                                          frame,
                                          sizeof(frame),
                                          &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_ACK");
        return SCPI_RES_ERR;
    }

    refmem_sync_frame_header_t header;
    (void)refmem_sync_frame_decode_header(frame, frame_size, &header);
    char hex[SCPI_REFMEM_SYNC_HEX_MAX];
    scpi_refmem_sync_hex_encode(frame, frame_size, hex, sizeof(hex));
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, frame_size);
    SCPI_ResultUInt32(context, header.source_slot);
    SCPI_ResultUInt32(context, header.target_mask);
    SCPI_ResultUInt32(context, header.epoch_id);
    SCPI_ResultUInt32(context, header.run_id);
    SCPI_ResultUInt32(context, header.seq32);
    SCPI_ResultUInt32(context, header.payload_crc32);
    SCPI_ResultUInt32(context, header.ack_seq32);
    SCPI_ResultUInt32(context, s_refmem_sync.last_rx.accepted);
    SCPI_ResultUInt32(context, s_refmem_sync.last_rx.result);
    SCPI_ResultUInt32(context, s_refmem_sync.last_rx.frame_result);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_fence_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = s_refmem_sync.context.local_slot;
    uint32_t target_mask = 0xFFu;
    uint32_t seq32 = 0u;
    uint32_t fence_seq = 1u;
    uint32_t fence_scope = 1u;
    uint32_t required_mask = target_mask;
    uint32_t min_table_seq = 0u;
    uint32_t deadline_1e3ns = 1000u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &seq32, FALSE);
    (void)SCPI_ParamUInt32(context, &fence_seq, FALSE);
    (void)SCPI_ParamUInt32(context, &fence_scope, FALSE);
    (void)SCPI_ParamUInt32(context, &required_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &min_table_seq, FALSE);
    (void)SCPI_ParamUInt32(context, &deadline_1e3ns, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT ||
        target_mask > 0xFFu ||
        required_mask > 0xFFu) {
        return SCPI_RES_ERR;
    }
    if (seq32 == 0u) {
        seq32 = s_refmem_sync.tx_seq32++;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!scpi_refmem_sync_build_fence_frame((uint8_t)source_slot,
                                            (uint8_t)target_mask,
                                            seq32,
                                            fence_seq,
                                            fence_scope,
                                            required_mask,
                                            min_table_seq,
                                            deadline_1e3ns,
                                            frame,
                                            sizeof(frame),
                                            &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_FENCE");
        return SCPI_RES_ERR;
    }

    refmem_sync_frame_header_t header;
    (void)refmem_sync_frame_decode_header(frame, frame_size, &header);
    char hex[SCPI_REFMEM_SYNC_HEX_MAX];
    scpi_refmem_sync_hex_encode(frame, frame_size, hex, sizeof(hex));
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, frame_size);
    SCPI_ResultUInt32(context, header.source_slot);
    SCPI_ResultUInt32(context, header.target_mask);
    SCPI_ResultUInt32(context, header.epoch_id);
    SCPI_ResultUInt32(context, header.run_id);
    SCPI_ResultUInt32(context, header.seq32);
    SCPI_ResultUInt32(context, header.payload_crc32);
    SCPI_ResultUInt32(context, fence_seq);
    SCPI_ResultUInt32(context, fence_scope);
    SCPI_ResultUInt32(context, required_mask);
    SCPI_ResultUInt32(context, min_table_seq);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_quality_frame_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = s_refmem_sync.context.local_slot;
    uint32_t target_mask = 0xFFu;
    uint32_t seq32 = 0u;
    uint32_t quality_id = 1u;
    uint32_t scope = 1u;
    uint32_t target_slot = source_slot;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &seq32, FALSE);
    (void)SCPI_ParamUInt32(context, &quality_id, FALSE);
    (void)SCPI_ParamUInt32(context, &scope, FALSE);
    (void)SCPI_ParamUInt32(context, &target_slot, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT ||
        target_slot >= REFMEM_SYNC_NODE_COUNT ||
        target_mask > 0xFFu) {
        return SCPI_RES_ERR;
    }
    if (seq32 == 0u) {
        seq32 = s_refmem_sync.tx_seq32++;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!scpi_refmem_sync_build_quality_frame((uint8_t)source_slot,
                                              (uint8_t)target_mask,
                                              seq32,
                                              quality_id,
                                              scope,
                                              target_slot,
                                              frame,
                                              sizeof(frame),
                                              &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_QUALITY_FRAME");
        return SCPI_RES_ERR;
    }

    refmem_sync_frame_header_t header;
    (void)refmem_sync_frame_decode_header(frame, frame_size, &header);
    char hex[SCPI_REFMEM_SYNC_HEX_MAX];
    scpi_refmem_sync_hex_encode(frame, frame_size, hex, sizeof(hex));
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, frame_size);
    SCPI_ResultUInt32(context, header.source_slot);
    SCPI_ResultUInt32(context, header.target_mask);
    SCPI_ResultUInt32(context, header.epoch_id);
    SCPI_ResultUInt32(context, header.run_id);
    SCPI_ResultUInt32(context, header.seq32);
    SCPI_ResultUInt32(context, header.payload_crc32);
    SCPI_ResultUInt32(context, quality_id);
    SCPI_ResultUInt32(context, scope);
    SCPI_ResultUInt32(context, target_slot);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_rx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    const char *hex = NULL;
    size_t hex_len = 0u;
    if (SCPI_ParamCharacters(context, &hex, &hex_len, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!scpi_refmem_sync_hex_decode(hex,
                                     hex_len,
                                     frame,
                                     sizeof(frame),
                                     &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_RX_HEX");
        return SCPI_RES_ERR;
    }

    const uint32_t timestamp = osal_tick_ms();
    if (refmem_pio_spi_adapter_inject_rx_frame(&s_refmem_sync.adapter,
                                               frame,
                                               frame_size,
                                               timestamp)) {
        uint8_t staged_frame[SCPI_REFMEM_SYNC_FRAME_MAX];
        size_t staged_size = 0u;
        if (!refmem_pio_spi_adapter_poll(&s_refmem_sync.adapter,
                                         staged_frame,
                                         sizeof(staged_frame),
                                         &staged_size)) {
            scpi_port_push_exec_error(context, "REFMEM_SYNC_RX_POLL");
            return SCPI_RES_ERR;
        }

        (void)refmem_sync_receive_frame(&s_refmem_sync.context,
                                        staged_frame,
                                        staged_size,
                                        &s_refmem_sync.last_rx);
    } else {
        (void)refmem_sync_receive_frame(&s_refmem_sync.context,
                                        frame,
                                        frame_size,
                                        &s_refmem_sync.last_rx);
    }
    scpi_refmem_sync_apply_node_load_delta(&s_refmem_sync.last_rx);
    SCPI_ResultText(context, s_refmem_sync.last_rx.accepted != 0u ? "ACCEPTED" : "REJECTED");
    scpi_refmem_sync_result_rx_snapshot(context, &s_refmem_sync.last_rx);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_mirror_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = 0u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return SCPI_RES_ERR;
    }

    const refmem_sync_mirror_snapshot_t *mirror =
        refmem_sync_get_mirror(&s_refmem_sync.context, (uint8_t)source_slot);
    if (mirror == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, source_slot);
    SCPI_ResultUInt32(context, mirror->visible);
    SCPI_ResultUInt32(context, mirror->source_slot);
    SCPI_ResultUInt32(context, mirror->slot_id);
    SCPI_ResultUInt32(context, mirror->payload_kind);
    SCPI_ResultUInt32(context, mirror->slot_seq);
    SCPI_ResultUInt32(context, mirror->field_id);
    SCPI_ResultUInt32(context, mirror->field_offset);
    SCPI_ResultUInt32(context, mirror->field_width);
    SCPI_ResultUInt32(context, mirror->dirty_mask);
    SCPI_ResultUInt32(context, mirror->value_u32);
    SCPI_ResultUInt32(context, mirror->value_crc32);
    SCPI_ResultUInt32(context, mirror->last_frame_seq32);
    SCPI_ResultUInt32(context, mirror->committed_count);
    SCPI_ResultUInt32(context, mirror->visible_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_ack_status_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = 0u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return SCPI_RES_ERR;
    }

    const refmem_sync_ack_snapshot_t *ack =
        refmem_sync_get_ack(&s_refmem_sync.context, (uint8_t)source_slot);
    if (ack == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, source_slot);
    SCPI_ResultUInt32(context, ack->seen);
    SCPI_ResultUInt32(context, ack->source_slot);
    SCPI_ResultUInt32(context, ack->command_seq);
    SCPI_ResultUInt32(context, ack->delta_seq32);
    SCPI_ResultUInt32(context, ack->taken_flags);
    SCPI_ResultUInt32(context, ack->ack_flags);
    SCPI_ResultUInt32(context, ack->nack_flags);
    SCPI_ResultUInt32(context, ack->busy_flags);
    SCPI_ResultUInt32(context, ack->timeout_flags);
    SCPI_ResultUInt32(context, ack->last_reason);
    SCPI_ResultUInt32(context, ack->last_reason_slot);
    SCPI_ResultUInt32(context, ack->evidence_index);
    SCPI_ResultUInt32(context, ack->last_frame_seq32);
    SCPI_ResultUInt32(context, ack->received_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_fence_status_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = 0u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return SCPI_RES_ERR;
    }

    const refmem_sync_fence_snapshot_t *fence =
        refmem_sync_get_fence(&s_refmem_sync.context, (uint8_t)source_slot);
    if (fence == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, source_slot);
    SCPI_ResultUInt32(context, fence->seen);
    SCPI_ResultUInt32(context, fence->source_slot);
    SCPI_ResultUInt32(context, fence->fence_seq);
    SCPI_ResultUInt32(context, fence->fence_scope);
    SCPI_ResultUInt32(context, fence->required_mask);
    SCPI_ResultUInt32(context, fence->min_table_seq);
    SCPI_ResultUInt32(context, fence->required_visible_mask);
    SCPI_ResultUInt32(context, fence->missing_mask);
    SCPI_ResultUInt32(context, fence->passed);
    SCPI_ResultUInt32(context, fence->timed_out);
    SCPI_ResultUInt32(context, fence->last_reason);
    SCPI_ResultUInt32(context, fence->evidence_index);
    SCPI_ResultUInt32(context, fence->last_frame_seq32);
    SCPI_ResultUInt32(context, fence->received_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_quality_status_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = 0u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return SCPI_RES_ERR;
    }

    const refmem_sync_remote_quality_snapshot_t *quality =
        refmem_sync_get_remote_quality(&s_refmem_sync.context, (uint8_t)source_slot);
    if (quality == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, source_slot);
    SCPI_ResultUInt32(context, quality->seen);
    SCPI_ResultUInt32(context, quality->source_slot);
    SCPI_ResultUInt32(context, quality->quality_id);
    SCPI_ResultUInt32(context, quality->scope);
    SCPI_ResultUInt32(context, quality->target_slot);
    SCPI_ResultUInt32(context, quality->seq_expected);
    SCPI_ResultUInt32(context, quality->seq_last);
    SCPI_ResultUInt32(context, quality->crc_error_count);
    SCPI_ResultUInt32(context, quality->stale_count);
    SCPI_ResultUInt32(context, quality->drop_count);
    SCPI_ResultUInt32(context, quality->late_count);
    SCPI_ResultUInt32(context, quality->timeout_count);
    SCPI_ResultUInt32(context, quality->last_error);
    SCPI_ResultUInt32(context, quality->p99_1e3ns);
    SCPI_ResultUInt32(context, quality->p999_1e3ns);
    SCPI_ResultUInt32(context, quality->evidence_index);
    SCPI_ResultUInt32(context, quality->last_frame_seq32);
    SCPI_ResultUInt32(context, quality->received_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_peer_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t source_slot = 0u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    if (source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return SCPI_RES_ERR;
    }

    const refmem_sync_peer_state_t *peer =
        refmem_sync_get_peer(&s_refmem_sync.context, (uint8_t)source_slot);
    if (peer == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, source_slot);
    SCPI_ResultUInt32(context, peer->seen);
    SCPI_ResultUInt32(context, peer->hello_seen);
    SCPI_ResultUInt32(context, peer->epoch_seen);
    SCPI_ResultUInt32(context, peer->frame_count);
    SCPI_ResultUInt32(context, peer->duplicate_count);
    SCPI_ResultUInt32(context, peer->stale_count);
    SCPI_ResultUInt32(context, peer->drop_count);
    SCPI_ResultUInt32(context, peer->last_seq32);
    SCPI_ResultUInt32(context, peer->expected_seq32);
    SCPI_ResultUInt32(context, peer->last_frame_type);
    SCPI_ResultUInt32(context, peer->last_compact_time);
    SCPI_ResultUInt32(context, peer->last_payload_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_quality_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    refmem_sync_quality_counters_t quality;
    refmem_sync_get_quality(&s_refmem_sync.context, &quality);
    SCPI_ResultUInt32(context, s_refmem_sync.context.local_slot);
    SCPI_ResultUInt32(context, s_refmem_sync.context.active_epoch_id);
    SCPI_ResultUInt32(context, s_refmem_sync.context.active_run_id);
    SCPI_ResultUInt32(context, quality.frame_rx_count);
    SCPI_ResultUInt32(context, quality.accepted_count);
    SCPI_ResultUInt32(context, quality.bad_frame_count);
    SCPI_ResultUInt32(context, quality.header_error_count);
    SCPI_ResultUInt32(context, quality.crc_error_count);
    SCPI_ResultUInt32(context, quality.source_error_count);
    SCPI_ResultUInt32(context, quality.target_mismatch_count);
    SCPI_ResultUInt32(context, quality.epoch_mismatch_count);
    SCPI_ResultUInt32(context, quality.duplicate_count);
    SCPI_ResultUInt32(context, quality.stale_count);
    SCPI_ResultUInt32(context, quality.drop_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_arm(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t role = REFMEM_SPI_PHYSICAL_ROLE_MASTER;
    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = BOARD_REFMEM_SPI_CSN_PIN,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    if (!scpi_port_read_u32(context, &role)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.csn_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.tx_pin, FALSE);

    if (!refmem_spi_physical_adapter_arm(&s_refmem_sync.spi_adapter,
                                         (refmem_spi_physical_role_t)role,
                                         baud_hz,
                                         &pins)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_SPI_ARM");
        return SCPI_RES_ERR;
    }

    refmem_spi_physical_snapshot_t snapshot;
    (void)refmem_spi_physical_adapter_get_snapshot(&s_refmem_sync.spi_adapter, &snapshot);
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, snapshot.role);
    SCPI_ResultUInt32(context, snapshot.baud_hz);
    SCPI_ResultUInt32(context, snapshot.rx_pin);
    SCPI_ResultUInt32(context, snapshot.csn_pin);
    SCPI_ResultUInt32(context, snapshot.sck_pin);
    SCPI_ResultUInt32(context, snapshot.tx_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_disarm(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    refmem_spi_physical_adapter_disarm(&s_refmem_sync.spi_adapter);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_refmem_sync_spi_status_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    refmem_spi_physical_snapshot_t snapshot;
    if (!refmem_spi_physical_adapter_get_snapshot(&s_refmem_sync.spi_adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, snapshot.armed);
    SCPI_ResultUInt32(context, snapshot.role);
    SCPI_ResultUInt32(context, snapshot.baud_hz);
    SCPI_ResultUInt32(context, snapshot.tx_count);
    SCPI_ResultUInt32(context, snapshot.rx_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    SCPI_ResultUInt32(context, snapshot.bad_packet_count);
    SCPI_ResultUInt32(context, snapshot.drop_count);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.last_tx_size);
    SCPI_ResultUInt32(context, snapshot.last_rx_size);
    SCPI_ResultUInt32(context, snapshot.rx_pin);
    SCPI_ResultUInt32(context, snapshot.csn_pin);
    SCPI_ResultUInt32(context, snapshot.sck_pin);
    SCPI_ResultUInt32(context, snapshot.tx_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_line_release(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    refmem_spi_physical_adapter_disarm(&s_refmem_sync.spi_adapter);
    refmem_spi_physical_line_release();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_refmem_sync_spi_line_drive(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t line_index = 0u;
    uint32_t level = 0u;
    if (!scpi_port_read_u32(context, &line_index)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &level, FALSE);
    if (line_index > 3u || level > 1u) {
        return SCPI_RES_ERR;
    }

    refmem_spi_physical_adapter_disarm(&s_refmem_sync.spi_adapter);
    if (!refmem_spi_physical_line_drive(line_index, level != 0u)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_SPI_LINE_DRIVE");
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, line_index);
    SCPI_ResultUInt32(context, level);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_line_status_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    const uint32_t level_mask = refmem_spi_physical_line_sample();
    SCPI_ResultUInt32(context, level_mask);
    SCPI_ResultUInt32(context, BOARD_REFMEM_SPI_RX_PIN);
    SCPI_ResultUInt32(context, BOARD_REFMEM_SPI_CSN_PIN);
    SCPI_ResultUInt32(context, BOARD_REFMEM_SPI_SCK_PIN);
    SCPI_ResultUInt32(context, BOARD_REFMEM_SPI_TX_PIN);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_raw_tx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t byte_count = 0u;
    uint32_t seed = 0xA5u;
    if (!scpi_port_read_u32(context, &byte_count)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &seed, FALSE);
    if (byte_count == 0u || byte_count > SCPI_REFMEM_SYNC_SPI_RAW_MAX || seed > 0xFFu) {
        return SCPI_RES_ERR;
    }

    uint32_t checksum = 0u;
    for (uint32_t i = 0u; i < byte_count; i++) {
        checksum += (uint8_t)(seed + i);
    }

    if (!refmem_spi_physical_adapter_transmit_raw(&s_refmem_sync.spi_adapter,
                                                  (uint8_t)seed,
                                                  byte_count)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_SPI_RAW_TX");
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, byte_count);
    SCPI_ResultUInt32(context, checksum);
    SCPI_ResultUInt32(context, (uint8_t)seed);
    SCPI_ResultUInt32(context, (uint8_t)(seed + byte_count - 1u));
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_raw_rx_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t expected_size = 0u;
    uint32_t seed = 0xA5u;
    uint32_t timeout_1e6ns = 1000u;
    if (!scpi_port_read_u32(context, &expected_size)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &seed, FALSE);
    (void)SCPI_ParamUInt32(context, &timeout_1e6ns, FALSE);
    if (expected_size == 0u ||
        expected_size > SCPI_REFMEM_SYNC_SPI_RAW_MAX ||
        seed > 0xFFu) {
        return SCPI_RES_ERR;
    }

    uint8_t buffer[SCPI_REFMEM_SYNC_SPI_RAW_MAX];
    size_t received_size = 0u;
    if (!refmem_spi_physical_adapter_receive_raw(&s_refmem_sync.spi_adapter,
                                                 buffer,
                                                 expected_size,
                                                 &received_size,
                                                 timeout_1e6ns)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_SPI_RAW_RX");
        return SCPI_RES_ERR;
    }

    uint32_t checksum = 0u;
    uint32_t mismatch_count = 0u;
    for (uint32_t i = 0u; i < received_size; i++) {
        checksum += buffer[i];
        if (buffer[i] != (uint8_t)(seed + i)) {
            mismatch_count++;
        }
    }

    SCPI_ResultText(context, "RAW");
    SCPI_ResultUInt32(context, (uint32_t)received_size);
    SCPI_ResultUInt32(context, checksum);
    SCPI_ResultUInt32(context, buffer[0]);
    SCPI_ResultUInt32(context, buffer[received_size - 1u]);
    SCPI_ResultUInt32(context, mismatch_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_adapter_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    refmem_pio_spi_adapter_snapshot_t snapshot;
    if (!refmem_pio_spi_adapter_get_snapshot(&s_refmem_sync.adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.adapter_id);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.capability_mask);
    SCPI_ResultUInt32(context, snapshot.max_payload_size);
    SCPI_ResultUInt32(context, snapshot.preferred_mtu);
    SCPI_ResultUInt32(context, snapshot.latency_class_us);
    SCPI_ResultUInt32(context, snapshot.tx_count);
    SCPI_ResultUInt32(context, snapshot.rx_count);
    SCPI_ResultUInt32(context, snapshot.tx_reject_count);
    SCPI_ResultUInt32(context, snapshot.rx_empty_count);
    SCPI_ResultUInt32(context, snapshot.bad_frame_count);
    SCPI_ResultUInt32(context, snapshot.drop_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.last_tx_size);
    SCPI_ResultUInt32(context, snapshot.last_rx_size);
    SCPI_ResultUInt32(context, snapshot.last_rx_timestamp);
    SCPI_ResultUInt32(context, snapshot.rx_pending);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_auto(scpi_t *context)
{
    uint32_t enabled = 0u;
    uint32_t local_slot = 0u;
    uint32_t target_mask = 0xFFu;
    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    uint32_t deadline_1e3ns = 1000000u;
    uint32_t uplink_duplex_mode = DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF;
    uint32_t downlink_duplex_mode = DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF;
    refmem_spi_physical_pin_config_t uplink_adapter_pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = REFMEM_SPI_PHYSICAL_PIN_UNUSED,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    refmem_spi_physical_pin_config_t downlink_adapter_pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = REFMEM_SPI_PHYSICAL_PIN_UNUSED,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };

    if (!scpi_port_read_u32(context, &enabled)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &local_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &deadline_1e3ns, FALSE);
    (void)SCPI_ParamUInt32(context, &uplink_duplex_mode, FALSE);
    (void)SCPI_ParamUInt32(context, &uplink_adapter_pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &uplink_adapter_pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &uplink_adapter_pins.tx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &downlink_duplex_mode, FALSE);
    (void)SCPI_ParamUInt32(context, &downlink_adapter_pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &downlink_adapter_pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &downlink_adapter_pins.tx_pin, FALSE);

    if (!distributed_refmem_configure_node_load_auto_sync(enabled,
                                                          local_slot,
                                                          target_mask,
                                                          baud_hz,
                                                          deadline_1e3ns,
                                                          uplink_duplex_mode,
                                                          &uplink_adapter_pins,
                                                          downlink_duplex_mode,
                                                          &downlink_adapter_pins)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_AUTO");
        return SCPI_RES_ERR;
    }

    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_refmem_sync_auto_q(scpi_t *context)
{
    distributed_refmem_node_load_auto_sync_snapshot_t snapshot;
    distributed_refmem_get_node_load_auto_sync(&snapshot);

    SCPI_ResultUInt32(context, snapshot.enabled);
    SCPI_ResultUInt32(context, snapshot.local_slot);
    SCPI_ResultUInt32(context, snapshot.target_mask);
    SCPI_ResultUInt32(context, snapshot.baud_hz);
    SCPI_ResultUInt32(context, snapshot.deadline_1e3ns);
    SCPI_ResultUInt32(context, snapshot.uplink_duplex_mode);
    SCPI_ResultUInt32(context, snapshot.uplink_rx_pin);
    SCPI_ResultUInt32(context, snapshot.uplink_sck_pin);
    SCPI_ResultUInt32(context, snapshot.uplink_tx_pin);
    SCPI_ResultUInt32(context, snapshot.downlink_duplex_mode);
    SCPI_ResultUInt32(context, snapshot.downlink_rx_pin);
    SCPI_ResultUInt32(context, snapshot.downlink_sck_pin);
    SCPI_ResultUInt32(context, snapshot.downlink_tx_pin);
    SCPI_ResultUInt32(context, snapshot.pending_count);
    SCPI_ResultUInt32(context, snapshot.active_intent);
    SCPI_ResultUInt32(context, snapshot.active_instance_id);
    SCPI_ResultUInt32(context, snapshot.next_seq32);
    SCPI_ResultUInt32(context, snapshot.submitted_tx_count);
    SCPI_ResultUInt32(context, snapshot.submitted_rx_count);
    SCPI_ResultUInt32(context, snapshot.applied_rx_count);
    SCPI_ResultUInt32(context, snapshot.failed_apply_count);
    SCPI_ResultUInt32(context, snapshot.dropped_pending_count);
    SCPI_ResultUInt32(context, snapshot.last_rx_result);
    SCPI_ResultUInt32(context, snapshot.last_frame_type);
    SCPI_ResultUInt32(context, snapshot.last_source_slot);
    SCPI_ResultUInt32(context, snapshot.last_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_status_q(scpi_t *context)
{
    refmem_realtime_tdma_snapshot_t snapshot;
    if (!distributed_refmem_get_realtime_tdma(&snapshot)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.owner_core);
    SCPI_ResultUInt32(context, snapshot.armed);
    SCPI_ResultUInt32(context, snapshot.service_count);
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, snapshot.completed_seq);
    SCPI_ResultUInt32(context, snapshot.dropped_seq);
    SCPI_ResultUInt32(context, snapshot.window_epoch);
    SCPI_ResultUInt32(context, snapshot.window_index);
    SCPI_ResultUInt32(context, snapshot.intent_type);
    SCPI_ResultUInt32(context, snapshot.role);
    SCPI_ResultUInt32(context, snapshot.baud_hz);
    SCPI_ResultUInt32(context, snapshot.rx_pin);
    SCPI_ResultUInt32(context, snapshot.csn_pin);
    SCPI_ResultUInt32(context, snapshot.sck_pin);
    SCPI_ResultUInt32(context, snapshot.tx_pin);
    SCPI_ResultUInt32(context, snapshot.deadline_1e3ns);
    SCPI_ResultUInt32(context, snapshot.frame_size);
    SCPI_ResultUInt32(context, snapshot.ready_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    SCPI_ResultUInt32(context, snapshot.overrun_count);
    SCPI_ResultUInt32(context, snapshot.reject_count);
    SCPI_ResultUInt32(context, snapshot.last_result);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.timestamp_source);
    SCPI_ResultUInt32(context, snapshot.timestamp_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.timestamp_flags);
    SCPI_ResultUInt32(context, snapshot.submit_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.submit_time_ns_hi);
    SCPI_ResultUInt32(context, snapshot.core1_arm_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.core1_arm_time_ns_hi);
    SCPI_ResultUInt32(context, snapshot.core1_start_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.core1_start_time_ns_hi);
    SCPI_ResultUInt32(context, snapshot.core1_done_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.core1_done_time_ns_hi);
    SCPI_ResultUInt32(context, snapshot.core1_elapsed_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_node_tx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t instance_id = 0u;
    uint32_t target_mask = 0xFFu;
    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    uint32_t deadline_1e3ns = 1000u;
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = BOARD_REFMEM_SPI_CSN_PIN,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    if (!scpi_port_read_u32(context, &instance_id)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &deadline_1e3ns, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.csn_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.tx_pin, FALSE);
    if (target_mask > 0xFFu) {
        return SCPI_RES_ERR;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    const uint32_t seq32 = s_refmem_sync.tx_seq32++;
    if (!distributed_refmem_build_node_load_sync_frame(instance_id,
                                                       s_refmem_sync.context.local_slot,
                                                       (uint8_t)target_mask,
                                                       s_refmem_sync.context.active_epoch_id,
                                                       s_refmem_sync.context.active_run_id,
                                                       seq32,
                                                       osal_tick_ms(),
                                                       frame,
                                                       sizeof(frame),
                                                       &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_NODE_FRAME");
        return SCPI_RES_ERR;
    }

    const refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_refmem_sync.context.active_epoch_id,
        .window_index = seq32,
        .deadline_1e3ns = deadline_1e3ns,
        .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
        .baud_hz = baud_hz,
        .pins = pins,
        .frame = frame,
        .frame_size = frame_size,
    };
    if (!distributed_refmem_submit_realtime_tdma_tx(&config)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_NODE_TX_BUSY");
        return SCPI_RES_ERR;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)distributed_refmem_get_realtime_tdma(&snapshot);
    SCPI_ResultText(context, "ACCEPTED");
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, instance_id);
    SCPI_ResultUInt32(context, (uint32_t)frame_size);
    SCPI_ResultUInt32(context, seq32);
    SCPI_ResultUInt32(context, target_mask);
    SCPI_ResultUInt32(context, baud_hz);
    SCPI_ResultUInt32(context, deadline_1e3ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_tx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    const char *hex = NULL;
    size_t hex_len = 0u;
    if (SCPI_ParamCharacters(context, &hex, &hex_len, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    uint32_t deadline_1e3ns = 1000u;
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = BOARD_REFMEM_SPI_CSN_PIN,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &deadline_1e3ns, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.csn_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.tx_pin, FALSE);
    if (!scpi_refmem_sync_hex_decode(hex,
                                     hex_len,
                                     frame,
                                     sizeof(frame),
                                     &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_TX_HEX");
        return SCPI_RES_ERR;
    }

    const refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_refmem_sync.context.active_epoch_id,
        .window_index = s_refmem_sync.tx_seq32,
        .deadline_1e3ns = deadline_1e3ns,
        .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
        .baud_hz = baud_hz,
        .pins = pins,
        .frame = frame,
        .frame_size = frame_size,
    };
    if (!distributed_refmem_submit_realtime_tdma_tx(&config)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_TX_BUSY");
        return SCPI_RES_ERR;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)distributed_refmem_get_realtime_tdma(&snapshot);
    SCPI_ResultText(context, "ACCEPTED");
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, (uint32_t)frame_size);
    SCPI_ResultUInt32(context, baud_hz);
    SCPI_ResultUInt32(context, deadline_1e3ns);
    SCPI_ResultUInt32(context, pins.rx_pin);
    SCPI_ResultUInt32(context, pins.csn_pin);
    SCPI_ResultUInt32(context, pins.sck_pin);
    SCPI_ResultUInt32(context, pins.tx_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_rx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    uint32_t deadline_1e3ns = 1000000u;
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = BOARD_REFMEM_SPI_CSN_PIN,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    (void)SCPI_ParamUInt32(context, &deadline_1e3ns, FALSE);
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.csn_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.tx_pin, FALSE);

    const refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_refmem_sync.context.active_epoch_id,
        .window_index = s_refmem_sync.tx_seq32,
        .deadline_1e3ns = deadline_1e3ns,
        .role = REFMEM_SPI_PHYSICAL_ROLE_SLAVE,
        .baud_hz = baud_hz,
        .pins = pins,
        .frame = NULL,
        .frame_size = 0u,
    };
    if (!distributed_refmem_submit_realtime_tdma_rx(&config)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_RX_BUSY");
        return SCPI_RES_ERR;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)distributed_refmem_get_realtime_tdma(&snapshot);
    SCPI_ResultText(context, "ACCEPTED");
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, deadline_1e3ns);
    SCPI_ResultUInt32(context, baud_hz);
    SCPI_ResultUInt32(context, pins.rx_pin);
    SCPI_ResultUInt32(context, pins.csn_pin);
    SCPI_ResultUInt32(context, pins.sck_pin);
    SCPI_ResultUInt32(context, pins.tx_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_frame_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!distributed_refmem_get_realtime_tdma_frame(frame, sizeof(frame), &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_NO_FRAME");
        return SCPI_RES_ERR;
    }

    const uint32_t timestamp = osal_tick_ms();
    if (refmem_pio_spi_adapter_inject_rx_frame(&s_refmem_sync.adapter,
                                               frame,
                                               frame_size,
                                               timestamp)) {
        uint8_t staged_frame[SCPI_REFMEM_SYNC_FRAME_MAX];
        size_t staged_size = 0u;
        if (!refmem_pio_spi_adapter_poll(&s_refmem_sync.adapter,
                                         staged_frame,
                                         sizeof(staged_frame),
                                         &staged_size)) {
            scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_FRAME_POLL");
            return SCPI_RES_ERR;
        }
        (void)refmem_sync_receive_frame(&s_refmem_sync.context,
                                        staged_frame,
                                        staged_size,
                                        &s_refmem_sync.last_rx);
    } else {
        (void)refmem_sync_receive_frame(&s_refmem_sync.context,
                                        frame,
                                        frame_size,
                                        &s_refmem_sync.last_rx);
    }
    scpi_refmem_sync_apply_node_load_delta(&s_refmem_sync.last_rx);

    char hex[SCPI_REFMEM_SYNC_HEX_MAX];
    scpi_refmem_sync_hex_encode(frame, frame_size, hex, sizeof(hex));
    SCPI_ResultText(context, s_refmem_sync.last_rx.accepted != 0u ? "ACCEPTED" : "REJECTED");
    scpi_refmem_sync_result_rx_snapshot(context, &s_refmem_sync.last_rx);
    SCPI_ResultUInt32(context, (uint32_t)frame_size);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_vdc_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t local_slot = s_refmem_sync.context.local_slot;
    uint32_t reference_slot = 0u;
    (void)SCPI_ParamUInt32(context, &local_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &reference_slot, FALSE);

    vdc_tdma_schedule_profile_t schedule;
    vdc_domain_default_schedule(&schedule, local_slot, reference_slot);

    vdc_tdma_frame_envelope_t envelope;
    refmem_vdc_bridge_status_t status;
    if (!distributed_refmem_build_realtime_tdma_vdc_envelope(&schedule,
                                                             &envelope,
                                                             &status)) {
        SCPI_ResultText(context, "REJECTED");
        SCPI_ResultUInt32(context, status.result);
        SCPI_ResultUInt32(context, status.frame_type);
        SCPI_ResultUInt32(context, status.source_slot);
        SCPI_ResultUInt32(context, status.payload_class);
        SCPI_ResultUInt32(context, status.frame_crc32);
        SCPI_ResultUInt32(context, status.payload_crc32);
        SCPI_ResultUInt32(context, status.gate.passed);
        SCPI_ResultUInt32(context, status.gate.reject_code);
        SCPI_ResultUInt32(context, status.gate.reject_slot);
        SCPI_ResultUInt32(context, status.gate.reject_evidence);
        return SCPI_RES_OK;
    }

    SCPI_ResultText(context, "ACCEPTED");
    SCPI_ResultUInt32(context, status.result);
    SCPI_ResultUInt32(context, status.frame_type);
    SCPI_ResultUInt32(context, status.source_slot);
    SCPI_ResultUInt32(context, status.payload_class);
    SCPI_ResultUInt32(context, status.frame_crc32);
    SCPI_ResultUInt32(context, status.payload_crc32);
    SCPI_ResultUInt32(context, status.gate.passed);
    SCPI_ResultUInt32(context, status.gate.reject_code);
    SCPI_ResultUInt32(context, status.gate.reject_slot);
    SCPI_ResultUInt32(context, status.gate.reject_evidence);
    SCPI_ResultUInt32(context, envelope.frame_version);
    SCPI_ResultUInt32(context, envelope.frame_seq);
    SCPI_ResultUInt32(context, envelope.schedule_epoch);
    SCPI_ResultUInt32(context, envelope.slot_index);
    SCPI_ResultUInt32(context, envelope.source_slot_id);
    SCPI_ResultUInt32(context, envelope.reference_slot_id);
    SCPI_ResultUInt32(context, envelope.window_class);
    SCPI_ResultUInt32(context, envelope.payload_class);
    SCPI_ResultUInt32(context, (uint32_t)(envelope.window_start_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(envelope.window_start_ns >> 32u));
    SCPI_ResultUInt32(context, envelope.schedule_crc32);
    SCPI_ResultUInt32(context, envelope.frame_crc32);
    SCPI_ResultUInt32(context, envelope.payload_crc32);
    SCPI_ResultUInt32(context, envelope.timestamp.timestamp_source);
    SCPI_ResultUInt32(context, envelope.timestamp.timestamp_resolution_ns);
    SCPI_ResultUInt32(context, envelope.timestamp.timestamp_flags);
    SCPI_ResultUInt32(context, envelope.timestamp.late_ns);
    SCPI_ResultUInt32(context, envelope.timestamp.jitter_ns);
    SCPI_ResultUInt32(context, envelope.timestamp.delay_ns);
    SCPI_ResultUInt32(context,
                      (uint32_t)(envelope.timestamp.observed_time_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(envelope.timestamp.observed_time_ns >> 32u));
    SCPI_ResultUInt32(context,
                      (uint32_t)(envelope.timestamp.done_time_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(envelope.timestamp.done_time_ns >> 32u));
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_abort(scpi_t *context)
{
    distributed_refmem_abort_realtime_tdma();
    return scpi_port_result_ok(context);
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
    uint32_t info_job_id = 0u;
    if (!storage_manager_post_file_info_job(path, &info_job_id) ||
        !scpi_refmem_wait_storage_job(info_job_id)) {
        return false;
    }

    storage_manager_job_result_t info_job;
    storage_manager_get_job_result(&info_job);
    if (info_job.id != info_job_id ||
        info_job.state != STORAGE_MANAGER_JOB_STATE_DONE ||
        info_job.is_dir ||
        info_job.size == 0u ||
        info_job.size > buffer_size) {
        return false;
    }

    uint32_t offset = 0u;
    while (offset < info_job.size) {
        const uint32_t remaining = info_job.size - offset;
        const uint32_t read_length = remaining > SCPI_REFMEM_PACKAGE_READ_CHUNK ?
                                         SCPI_REFMEM_PACKAGE_READ_CHUNK :
                                         remaining;
        uint32_t job_id = 0u;
        if (!storage_manager_post_file_read_job(path,
                                                offset,
                                                read_length,
                                                &job_id)) {
            return false;
        }
        if (!scpi_refmem_wait_storage_job(job_id)) {
            return false;
        }

        storage_manager_file_read_t read_info;
        if (!storage_manager_get_file_read_job_result(job_id,
                                                      &read_info,
                                                      s_refmem_package_chunk,
                                                      sizeof(s_refmem_package_chunk))) {
            return false;
        }
        if (read_info.returned == 0u ||
            read_info.returned > sizeof(s_refmem_package_chunk) ||
            read_info.returned > read_length ||
            *returned_size + read_info.returned > buffer_size) {
            return false;
        }

        for (uint32_t i = 0u; i < read_info.returned; i++) {
            buffer[*returned_size + i] = s_refmem_package_chunk[i];
        }
        *returned_size += read_info.returned;
        offset += read_info.returned;
    }

    return *returned_size == info_job.size;
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

static void scpi_refmem_result_table_image_descriptor(
    scpi_t *context,
    const refmem_table_image_descriptor_t *descriptor)
{
    SCPI_ResultUInt32(context, descriptor->version);
    SCPI_ResultUInt32(context, descriptor->role);
    SCPI_ResultUInt32(context, descriptor->state);
    SCPI_ResultUInt32(context, descriptor->table_mask);
    SCPI_ResultUInt32(context, descriptor->package_crc32);
    SCPI_ResultUInt32(context, descriptor->table_seq);
    SCPI_ResultUInt32(context, descriptor->path_hash);
    SCPI_ResultUInt32(context, descriptor->last_result);
    SCPI_ResultUInt32(context, descriptor->evidence_index);
}

static uint32_t scpi_refmem_read_u32_le(const uint8_t *data)
{
    if (data == NULL) {
        return 0u;
    }
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool scpi_refmem_sync_ensure_initialized(void)
{
    if (s_refmem_sync.initialized != 0u) {
        return true;
    }

    const uint8_t local_slot = 0u;
    if (!refmem_sync_init(&s_refmem_sync.context,
                          local_slot,
                          SCPI_REFMEM_SYNC_DEFAULT_EPOCH,
                          SCPI_REFMEM_SYNC_DEFAULT_RUN) ||
        !refmem_pio_spi_adapter_init(&s_refmem_sync.adapter,
                                     SCPI_REFMEM_SYNC_DEFAULT_MAX_PAYLOAD,
                                     SCPI_REFMEM_SYNC_DEFAULT_MTU,
                                     SCPI_REFMEM_SYNC_DEFAULT_LATENCY_US)) {
        return false;
    }
    s_refmem_sync.tx_seq32 = 1u;
    s_refmem_sync.initialized = 1u;
    return true;
}

static void scpi_refmem_sync_apply_node_load_delta(const refmem_sync_rx_snapshot_t *rx)
{
    if (rx == NULL ||
        rx->accepted == 0u ||
        rx->header.frame_type != (uint8_t)REFMEM_SYNC_FRAME_DELTA) {
        return;
    }
    (void)distributed_refmem_apply_node_load_sync_payload(rx->payload,
                                                          rx->payload_size);
}

static uint32_t scpi_refmem_sync_build_id_crc32(void)
{
    uint32_t hash = 2166136261u;
    const char *text = g_project_build_id;
    while (text != NULL && *text != '\0') {
        hash ^= (uint8_t)*text;
        hash *= 16777619u;
        text++;
    }
    return hash;
}

static bool scpi_refmem_sync_build_hello_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size)
{
    refmem_transport_caps_t caps;
    if (!refmem_pio_spi_adapter_get_caps(&s_refmem_sync.adapter, &caps)) {
        return false;
    }

    const refmem_board_capability_table_t *board_table =
        refmem_application_model_get_board_capability_table();
    if (board_table == NULL || board_table->board_count == 0u) {
        return false;
    }
    uint32_t board_id = source_slot;
    if (board_id >= board_table->board_count) {
        board_id = 0u;
    }
    const refmem_application_model_snapshot_t *model =
        refmem_application_model_get_snapshot();
    const refmem_application_map_t *application =
        refmem_application_model_get_application_map();

    refmem_sync_hello_config_t config;
    memset(&config, 0, sizeof(config));
    config.build_id_crc32 = scpi_refmem_sync_build_id_crc32();
    config.layout_version = application != NULL ? application->layout_version : 0u;
    config.application_crc32 = model != NULL ? model->package_crc32 : 0u;
    config.config_crc32 = model != NULL ? model->application_map_crc32 : 0u;
    config.source_slot = source_slot;
    config.target_mask = target_mask;
    config.epoch_id = s_refmem_sync.context.active_epoch_id;
    config.run_id = s_refmem_sync.context.active_run_id;
    config.seq32 = seq32;
    config.compact_time = osal_tick_ms();

    refmem_sync_hello_payload_t payload;
    if (!refmem_sync_hello_payload_from_board(&config,
                                              &board_table->board[board_id],
                                              &caps,
                                              &payload)) {
        return false;
    }
    return refmem_sync_hello_encode_frame(&config,
                                          &payload,
                                          frame,
                                          frame_capacity,
                                          frame_size);
}

static bool scpi_refmem_sync_build_epoch_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size)
{
    const refmem_application_model_snapshot_t *model =
        refmem_application_model_get_snapshot();
    refmem_table_registry_snapshot_t registry;
    refmem_table_registry_get_snapshot(&registry);
    distributed_refmem_status_t status;
    distributed_refmem_get_status(&status);

    refmem_sync_epoch_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.table_seq = status.table_seq;
    payload.layout_crc32 = registry.registry_crc32;
    payload.application_crc32 = model != NULL ? model->package_crc32 : 0u;
    payload.config_crc32 = model != NULL ? model->application_map_crc32 : 0u;
    payload.calibration_crc32 = model != NULL ? model->deployment_gate_crc32 : 0u;
    payload.sync_profile_crc32 = model != NULL ? model->connection_quality_crc32 : 0u;
    payload.quality_epoch = s_refmem_sync.context.active_epoch_id;

    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(&header,
                                       REFMEM_SYNC_FRAME_EPOCH,
                                       0u,
                                       source_slot,
                                       target_mask,
                                       s_refmem_sync.context.active_epoch_id,
                                       s_refmem_sync.context.active_run_id,
                                       seq32,
                                       0u,
                                       osal_tick_ms(),
                                       &payload,
                                       sizeof(payload))) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    &payload,
                                    sizeof(payload),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static bool scpi_refmem_sync_build_delta_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint8_t slot_id,
                                               uint32_t slot_seq,
                                               uint16_t field_id,
                                               uint32_t value,
                                               uint32_t dirty_mask,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size)
{
    uint8_t payload[sizeof(refmem_sync_delta_header_t) + sizeof(uint32_t)];
    refmem_sync_delta_header_t delta;
    memset(&delta, 0, sizeof(delta));
    delta.delta_id = (uint16_t)(seq32 & 0xFFFFu);
    delta.slot_id = slot_id;
    delta.payload_kind = REFMEM_APP_DATA_U32;
    delta.slot_seq = slot_seq;
    delta.field_id = field_id;
    delta.field_offset = 0u;
    delta.field_width = sizeof(uint32_t);
    delta.dirty_mask = dirty_mask;
    memcpy(payload, &delta, sizeof(delta));
    payload[sizeof(delta) + 0u] = (uint8_t)(value & 0xFFu);
    payload[sizeof(delta) + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
    payload[sizeof(delta) + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
    payload[sizeof(delta) + 3u] = (uint8_t)((value >> 24u) & 0xFFu);

    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(&header,
                                       REFMEM_SYNC_FRAME_DELTA,
                                       REFMEM_SYNC_FRAME_FLAG_ACK_REQUEST,
                                       source_slot,
                                       target_mask,
                                       s_refmem_sync.context.active_epoch_id,
                                       s_refmem_sync.context.active_run_id,
                                       seq32,
                                       0u,
                                       osal_tick_ms(),
                                       payload,
                                       sizeof(payload))) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    payload,
                                    sizeof(payload),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static bool scpi_refmem_sync_build_ack_frame(uint8_t source_slot,
                                             uint8_t target_mask,
                                             uint32_t seq32,
                                             const refmem_sync_rx_snapshot_t *rx,
                                             uint8_t *frame,
                                             size_t frame_capacity,
                                             size_t *frame_size)
{
    if (rx == NULL ||
        rx->header.header_size != REFMEM_SYNC_FRAME_HEADER_SIZE ||
        rx->header.source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }

    const uint32_t local_bit = (uint32_t)(1u << source_slot);
    refmem_sync_ack_nack_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.command_seq = rx->header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_COMMAND ?
        rx->header.seq32 : 0u;
    payload.delta_seq32 = rx->header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_DELTA ?
        rx->header.seq32 : 0u;
    payload.taken_flags = local_bit;
    if (rx->accepted != 0u) {
        payload.ack_flags = local_bit;
    } else {
        payload.nack_flags = local_bit;
    }
    payload.last_reason = rx->accepted != 0u ? 0u : (uint32_t)rx->result;
    if (rx->accepted == 0u &&
        rx->result == REFMEM_SYNC_RX_FRAME_INVALID &&
        rx->frame_result != REFMEM_SYNC_FRAME_OK) {
        payload.last_reason = (uint32_t)rx->frame_result;
    }
    payload.last_reason_slot = source_slot;
    payload.evidence_index = 0u;

    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(&header,
                                       REFMEM_SYNC_FRAME_ACK_NACK,
                                       0u,
                                       source_slot,
                                       target_mask,
                                       s_refmem_sync.context.active_epoch_id,
                                       s_refmem_sync.context.active_run_id,
                                       seq32,
                                       rx->header.seq32,
                                       osal_tick_ms(),
                                       &payload,
                                       sizeof(payload))) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    &payload,
                                    sizeof(payload),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static bool scpi_refmem_sync_build_fence_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint32_t fence_seq,
                                               uint32_t fence_scope,
                                               uint32_t required_mask,
                                               uint32_t min_table_seq,
                                               uint32_t deadline_1e3ns,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size)
{
    const refmem_application_model_snapshot_t *model =
        refmem_application_model_get_snapshot();
    refmem_table_registry_snapshot_t registry;
    refmem_table_registry_get_snapshot(&registry);

    refmem_sync_fence_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.fence_seq = fence_seq;
    payload.fence_scope = fence_scope;
    payload.required_mask = required_mask;
    payload.min_table_seq = min_table_seq;
    payload.layout_crc32 = registry.registry_crc32;
    payload.application_crc32 = model != NULL ? model->package_crc32 : 0u;
    payload.config_crc32 = model != NULL ? model->application_map_crc32 : 0u;
    payload.calibration_crc32 = model != NULL ? model->deployment_gate_crc32 : 0u;
    payload.sync_profile_crc32 = model != NULL ? model->connection_quality_crc32 : 0u;
    payload.deadline_1e3ns = deadline_1e3ns;

    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(&header,
                                       REFMEM_SYNC_FRAME_FENCE,
                                       0u,
                                       source_slot,
                                       target_mask,
                                       s_refmem_sync.context.active_epoch_id,
                                       s_refmem_sync.context.active_run_id,
                                       seq32,
                                       0u,
                                       osal_tick_ms(),
                                       &payload,
                                       sizeof(payload))) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    &payload,
                                    sizeof(payload),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static bool scpi_refmem_sync_build_quality_frame(uint8_t source_slot,
                                                 uint8_t target_mask,
                                                 uint32_t seq32,
                                                 uint32_t quality_id,
                                                 uint32_t scope,
                                                 uint32_t target_slot,
                                                 uint8_t *frame,
                                                 size_t frame_capacity,
                                                 size_t *frame_size)
{
    refmem_sync_quality_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.quality_id = quality_id;
    payload.scope = scope;
    payload.source_slot = source_slot;
    payload.target_slot = target_slot;

    const refmem_sync_peer_state_t *peer =
        refmem_sync_get_peer(&s_refmem_sync.context, (uint8_t)target_slot);
    if (peer != NULL) {
        payload.seq_expected = peer->expected_seq32;
        payload.seq_last = peer->last_seq32;
    }

    refmem_sync_quality_counters_t counters;
    refmem_sync_get_quality(&s_refmem_sync.context, &counters);
    payload.crc_error_count = counters.crc_error_count;
    payload.stale_count = counters.stale_count;
    payload.drop_count = counters.drop_count;
    payload.late_count = 0u;
    payload.timeout_count = 0u;
    if (counters.crc_error_count != 0u) {
        payload.last_error = REFMEM_SYNC_FRAME_BAD_PAYLOAD_CRC;
    } else if (counters.stale_count != 0u) {
        payload.last_error = REFMEM_SYNC_RX_STALE_SEQ;
    } else if (counters.duplicate_count != 0u) {
        payload.last_error = REFMEM_SYNC_RX_DUPLICATE_SEQ;
    } else if (counters.target_mismatch_count != 0u) {
        payload.last_error = REFMEM_SYNC_RX_TARGET_MISMATCH;
    }
    payload.p99_1e3ns = 0u;
    payload.p999_1e3ns = 0u;
    payload.evidence_index = 0u;

    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(&header,
                                       REFMEM_SYNC_FRAME_QUALITY,
                                       0u,
                                       source_slot,
                                       target_mask,
                                       s_refmem_sync.context.active_epoch_id,
                                       s_refmem_sync.context.active_run_id,
                                       seq32,
                                       0u,
                                       osal_tick_ms(),
                                       &payload,
                                       sizeof(payload))) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    &payload,
                                    sizeof(payload),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static void scpi_refmem_sync_result_rx_snapshot(scpi_t *context,
                                                const refmem_sync_rx_snapshot_t *snapshot)
{
    SCPI_ResultUInt32(context, snapshot->accepted);
    SCPI_ResultUInt32(context, snapshot->result);
    SCPI_ResultUInt32(context, snapshot->frame_result);
    SCPI_ResultUInt32(context, snapshot->header.frame_type);
    SCPI_ResultUInt32(context, snapshot->header.source_slot);
    SCPI_ResultUInt32(context, snapshot->header.target_mask);
    SCPI_ResultUInt32(context, snapshot->header.epoch_id);
    SCPI_ResultUInt32(context, snapshot->header.run_id);
    SCPI_ResultUInt32(context, snapshot->header.seq32);
    SCPI_ResultUInt32(context, snapshot->header.payload_size);
    SCPI_ResultUInt32(context, snapshot->header.payload_crc32);
}

static void scpi_refmem_sync_hex_encode(const uint8_t *data,
                                        size_t data_size,
                                        char *hex,
                                        size_t hex_size)
{
    static const char digits[] = "0123456789ABCDEF";
    if (hex == NULL || hex_size == 0u) {
        return;
    }
    if (data == NULL || hex_size < (data_size * 2u) + 1u) {
        hex[0] = '\0';
        return;
    }
    for (size_t i = 0u; i < data_size; i++) {
        hex[i * 2u] = digits[(data[i] >> 4u) & 0x0Fu];
        hex[(i * 2u) + 1u] = digits[data[i] & 0x0Fu];
    }
    hex[data_size * 2u] = '\0';
}

static int scpi_refmem_sync_hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

static bool scpi_refmem_sync_hex_decode(const char *hex,
                                        size_t hex_len,
                                        uint8_t *output,
                                        size_t output_size,
                                        size_t *decoded_size)
{
    if (decoded_size != NULL) {
        *decoded_size = 0u;
    }
    if (hex == NULL || output == NULL || decoded_size == NULL ||
        (hex_len % 2u) != 0u ||
        (hex_len / 2u) > output_size) {
        return false;
    }
    for (size_t i = 0u; i < hex_len; i += 2u) {
        const int high = scpi_refmem_sync_hex_nibble(hex[i]);
        const int low = scpi_refmem_sync_hex_nibble(hex[i + 1u]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[i / 2u] = (uint8_t)(((uint8_t)high << 4u) | (uint8_t)low);
    }
    *decoded_size = hex_len / 2u;
    return true;
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
    SCPI_ResultUInt32(context, snapshot.last_result);
    SCPI_ResultUInt32(context, snapshot.last_elapsed_us);
    SCPI_ResultUInt32(context, snapshot.request_seq);
    SCPI_ResultUInt32(context, snapshot.ack_seq);
    SCPI_ResultUInt32(context, snapshot.release_seq);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    SCPI_ResultUInt32(context, snapshot.release_timeout_count);
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

scpi_result_t scpi_cmd_command_ack_q(scpi_t *context)
{
    refmem_command_snapshot_t snapshot;
    if (!distributed_refmem_get_command_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, SCPI_COMMAND_ACK_SCHEMA_VERSION);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.command_seq);
    SCPI_ResultUInt32(context, snapshot.source_node);
    SCPI_ResultUInt32(context, snapshot.source_instance);
    SCPI_ResultUInt32(context, snapshot.target_mask);
    SCPI_ResultUInt32(context, snapshot.required_mask);
    SCPI_ResultUInt32(context, snapshot.command_type);
    SCPI_ResultUInt32(context, snapshot.command_class);
    SCPI_ResultUInt32(context, snapshot.payload_kind);
    SCPI_ResultUInt32(context, snapshot.payload_ref);
    SCPI_ResultUInt32(context, snapshot.payload_size);
    SCPI_ResultUInt32(context, snapshot.payload_crc32);
    SCPI_ResultUInt32(context, snapshot.issue_epoch);
    SCPI_ResultUInt32(context, snapshot.run_id);
    SCPI_ResultUInt32(context, snapshot.issue_tick32);
    SCPI_ResultUInt32(context, snapshot.timeout_1e3ns);
    SCPI_ResultUInt32(context, snapshot.taken_flags);
    SCPI_ResultUInt32(context, snapshot.ack_flags);
    SCPI_ResultUInt32(context, snapshot.nack_flags);
    SCPI_ResultUInt32(context, snapshot.busy_flags);
    SCPI_ResultUInt32(context, snapshot.timeout_flags);
    SCPI_ResultUInt32(context, snapshot.last_reason);
    SCPI_ResultUInt32(context, snapshot.last_reason_slot);
    SCPI_ResultUInt32(context, snapshot.reason_table_crc32);
    SCPI_ResultUInt32(context, snapshot.evidence_index);
    SCPI_ResultUInt32(context, snapshot.clear_seq);
    SCPI_ResultUInt32(context, snapshot.last_completed_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_command_nack_reason_q(scpi_t *context)
{
    refmem_command_snapshot_t snapshot;
    if (!distributed_refmem_get_command_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }

    uint32_t reason_id = snapshot.last_reason;
    (void)SCPI_ParamUInt32(context, &reason_id, FALSE);

    const scpi_command_reason_entry_t *reason =
        scpi_command_find_reason(reason_id);
    if (reason == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, SCPI_COMMAND_ACK_SCHEMA_VERSION);
    SCPI_ResultUInt32(context,
                      (uint32_t)(sizeof(s_command_reason_table) /
                                 sizeof(s_command_reason_table[0])));
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
