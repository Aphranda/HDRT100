#include "distributed_refmem.h"
#include "distributed_refmem_internal.h"

#include <string.h>

#include "drv_flash.h"
#include "osal.h"
#include "board_config.h"
#include "board_identity.h"
#include "project_config.h"

#include "distributed_refmem_vdc_bridge.h"
#include "diagnostics.h"
#include "ota_ao.h"
#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_cmd_load.h"
#include "refmem_lease_table.h"
#include "refmem_control_route.h"
#include "refmem_node_control.h"
#include "refmem_param_pool.h"
#include "refmem_cmd_frame.h"
#include "refmem_cmd_reply.h"
#include "tdma_maintenance_runtime.h"
#include "refmem_node_load_sync.h"
#include "refmem_quality.h"
#include "refmem_realtime_tdma.h"
#include "tdma_runtime_owner.h"
#include "refmem_slot_claim.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_sync.h"
#include "refmem_sync_vdc_feedback.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "tdma_process_image_layout.h"
#include "tdma_process_image_map.h"
#include "tdma_profile.h"
#include "trigger_position_mailbox.h"
#include "vdc_dpll_manager.h"

_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE ==
                   TDMA_PROCESS_IMAGE_REFMEM_SIZE + TDMA_PROCESS_IMAGE_VDC_SIZE,
               "A T2 position record spans the freed VDC observation area plus the RefMem region");

/* RAM placement macros live in a header: the split command units need them. */
#include "distributed_refmem_time_critical.h"

#include "distributed_refmem_split_internal.h"
bool distributed_refmem_register_node_load_owner(
    uint32_t instance_id,
    distributed_refmem_node_load_owner_t owner,
    void *context)
{
    if (owner == NULL || instance_id == 0u) {
        return false;
    }

    osal_critical_enter();
    distributed_refmem_node_load_owner_entry_t *empty = NULL;
    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT; i++) {
        if (s_node_load_owners[i].owner != NULL &&
            s_node_load_owners[i].instance_id == instance_id) {
            s_node_load_owners[i].owner = owner;
            s_node_load_owners[i].context = context;
            osal_critical_exit();
            return true;
        }
        if (empty == NULL && s_node_load_owners[i].owner == NULL) {
            empty = &s_node_load_owners[i];
        }
    }

    if (empty != NULL) {
        empty->instance_id = instance_id;
        empty->owner = owner;
        empty->context = context;
        osal_critical_exit();
        return true;
    }

    osal_critical_exit();
    return false;
}

bool distributed_refmem_can_accept_node_load_intent(uint32_t realtime_idle)
{
    if (!s_refmem_core_initialized) {
        return false;
    }

    if (realtime_idle != 0u) {
        return true;
    }
    /* A busy scheduler may be carrying a VDC command or another admitted
     * realtime intent.  Node-load configuration is deferred until the
     * scheduler is idle; it must not gain permission by preempting traffic. */
    return false;
}

bool distributed_refmem_stage_node_load(uint32_t node_id,
                                        uint32_t instance_id,
                                        uint32_t role_mask,
                                        uint32_t persona_mask,
                                        uint32_t enabled,
                                        uint32_t required,
                                        uint32_t load_order)
{
    if (!s_refmem_core_initialized) {
        return false;
    }

    if (node_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        (void)refmem_application_model_stage_scpi_node_config(node_id,
                                                              instance_id,
                                                              role_mask,
                                                              persona_mask,
                                                              enabled,
                                                              required,
                                                              load_order);
        return false;
    }

    const uint32_t fields[] = {
        node_id,
        instance_id,
        role_mask,
        persona_mask,
        enabled,
        required,
        load_order,
    };
    const uint32_t payload_crc32 = distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    const uint32_t target_mask = (uint32_t)(1u << node_id);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = instance_id,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_INLINE_SMALL,
        .payload_ref = instance_id,
        .payload_size = (uint32_t)sizeof(fields),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }

    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                node_id,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    const bool staged =
        refmem_application_model_stage_scpi_node_config(node_id,
                                                        instance_id,
                                                        role_mask,
                                                        persona_mask,
                                                        enabled,
                                                        required,
                                                        load_order);
    if (staged) {
        (void)distributed_refmem_node_load_auto_enqueue(instance_id);
        (void)distributed_refmem_command_ack(node_id, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(node_id,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_stage_sd_system_pack(const char *path,
                                             uint32_t path_hash,
                                             uint32_t manifest_status,
                                             uint32_t manifest_schema,
                                             uint32_t manifest_required_count,
                                             uint32_t manifest_missing_count,
                                             const char *manifest_build_id,
                                             uint32_t package_crc32,
                                             uint32_t package_valid,
                                             uint32_t package_error,
                                             const uint8_t *package_data,
                                             size_t package_size,
                                             const uint32_t *table_crc32,
                                             uint32_t table_crc32_count,
                                             uint32_t owner_validated_table_mask,
                                             uint32_t first_bad_table)
{
    if (!s_refmem_core_initialized) {
        return false;
    }

    uint32_t fields[10u + REFMEM_TABLE_REGISTRY_COUNT];
    fields[0] = path_hash;
    fields[1] = manifest_status;
    fields[2] = manifest_schema;
    fields[3] = manifest_required_count;
    fields[4] = manifest_missing_count;
    fields[5] = package_crc32;
    fields[6] = package_valid;
    fields[7] = package_error;
    fields[8] = owner_validated_table_mask;
    fields[9] = first_bad_table;
    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        fields[10u + i] =
            (table_crc32 != NULL && i < table_crc32_count) ? table_crc32[i] : 0u;
    }

    const uint32_t payload_crc32 = distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    const uint32_t local_target = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    const uint32_t target_mask = (uint32_t)(1u << local_target);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_STAGING_REF,
        .payload_ref = path_hash,
        .payload_size = (uint32_t)sizeof(fields),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }

    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                local_target,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    const bool staged =
        refmem_application_model_stage_sd_system_pack(path,
                                                      path_hash,
                                                      manifest_status,
                                                      manifest_schema,
                                                      manifest_required_count,
                                                      manifest_missing_count,
                                                      manifest_build_id,
                                                      package_crc32,
                                                      package_valid,
                                                      package_error);
    if (staged && package_valid != 0u) {
        refmem_application_model_load_snapshot_t snapshot;
        refmem_table_package_validation_t validation = {0};
        refmem_application_model_get_load_snapshot(&snapshot);
        validation.valid = package_valid;
        validation.error = package_error;
        validation.package_crc32 = package_crc32;
        validation.table_count = REFMEM_TABLE_REGISTRY_COUNT;
        validation.table_mask = (1u << REFMEM_TABLE_REGISTRY_COUNT) - 1u;
        validation.owner_validated_table_mask = owner_validated_table_mask;
        validation.first_bad_table = first_bad_table;
        for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
            validation.table_crc32[i] =
                (table_crc32 != NULL && i < table_crc32_count) ? table_crc32[i] : 0u;
        }
        (void)refmem_table_registry_stage_package_image(&snapshot,
                                                        package_data,
                                                        package_size,
                                                        &validation);
    }

    if (staged) {
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(local_target,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_activate_staging(uint32_t realtime_idle)
{
    if (!s_refmem_core_initialized) {
        return false;
    }

    refmem_table_image_descriptor_t staging;
    if (!refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_STAGING,
                                                    &staging)) {
        return false;
    }

    const uint32_t fields[] = {
        staging.table_mask,
        staging.package_crc32,
        staging.table_seq,
        realtime_idle,
    };
    const uint32_t payload_crc32 = distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    const uint32_t local_target = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    const uint32_t target_mask = (uint32_t)(1u << local_target);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_TABLE_PACKAGE_ACTIVATE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_STAGING_REF,
        .payload_ref = staging.package_crc32,
        .payload_size = (uint32_t)sizeof(fields),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }

    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                local_target,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    refmem_application_model_load_snapshot_t load;
    refmem_application_model_get_load_snapshot(&load);
    const refmem_table_activation_gate_t gate = {
        .refmem_idle = load.mode == REFMEM_APP_MODEL_MODE_IDLE ? 1u : 0u,
        .realtime_idle = realtime_idle != 0u ? 1u : 0u,
        .flash_safe = distributed_refmem_flash_activation_safe() ? 1u : 0u,
        .crc_ok = staging.package_crc32 != 0u &&
                  staging.state >= REFMEM_TABLE_VALIDATION_CRC_OK ? 1u : 0u,
        .owner_ok = staging.state == REFMEM_TABLE_VALIDATION_OWNER_OK ? 1u : 0u,
        .slot_claim_ok = distributed_refmem_slot_claim_gate_ready() ? 1u : 0u,
        .deployment_gate_ok =
            refmem_application_model_get_snapshot()->valid != 0u &&
                    distributed_refmem_quality_gate_ready()
                ? 1u
                : 0u,
        .command_ack_ok = 1u,
    };

    const bool staging_candidate_ready =
        staging.state == REFMEM_TABLE_VALIDATION_OWNER_OK &&
        staging.table_mask == REFMEM_APP_TABLE_MASK_ALL &&
        staging.package_crc32 != 0u;
    const bool gate_ready_for_preparse =
        gate.refmem_idle != 0u &&
        gate.realtime_idle != 0u &&
        gate.flash_safe != 0u &&
        gate.crc_ok != 0u &&
        gate.owner_ok != 0u &&
        gate.slot_claim_ok != 0u &&
        gate.deployment_gate_ok != 0u &&
        gate.command_ack_ok != 0u;

    tdma_foundation_profile_t prepared_tdma_profile;
    uint32_t prepared_tdma_schedule_crc32 = 0u;
    if (staging_candidate_ready && gate_ready_for_preparse) {
        if (!refmem_application_model_prepare_staging_table_views()) {
            (void)refmem_table_registry_note_activation_result(
                REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID);
            refmem_application_model_discard_prepared_table_views();
            (void)distributed_refmem_command_nack(
                local_target,
                distributed_refmem_activation_nack_reason(
                    REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID),
                REFMEM_VECTOR_REGION_ACK_CMD);
            return false;
        }
        if (!refmem_application_model_get_prepared_tdma_foundation_profile(
                &prepared_tdma_profile) ||
            !distributed_refmem_tdma_profile_activation_ready(
                &prepared_tdma_profile,
                &prepared_tdma_schedule_crc32)) {
            (void)refmem_table_registry_note_activation_result(
                REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE);
            refmem_application_model_discard_prepared_table_views();
            (void)distributed_refmem_command_nack(
                local_target,
                distributed_refmem_activation_nack_reason(
                    REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE),
                REFMEM_VECTOR_REGION_ACK_CMD);
            return false;
        }
    }

    const bool activated = refmem_table_registry_activate_staging(&gate);
    refmem_table_registry_snapshot_t registry;
    refmem_table_registry_get_snapshot(&registry);
    if (activated) {
        const bool model_applied =
            refmem_application_model_commit_prepared_table_views() ||
            refmem_application_model_apply_active_table_views();
        if (!model_applied ||
            !distributed_refmem_apply_tdma_foundation_profile(
                &prepared_tdma_profile,
                prepared_tdma_schedule_crc32)) {
            (void)refmem_table_registry_note_activation_result(
                REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE);
            (void)distributed_refmem_command_nack(
                local_target,
                distributed_refmem_activation_nack_reason(
                    REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE),
                REFMEM_VECTOR_REGION_ACK_CMD);
            return false;
        }
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    refmem_application_model_discard_prepared_table_views();
    (void)distributed_refmem_command_nack(
        local_target,
        distributed_refmem_activation_nack_reason(registry.last_error),
        REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_stage_board_capability(uint32_t board_id,
                                               uint32_t board_uuid_crc32,
                                               uint32_t capability_mask,
                                               uint32_t io_constraint_mask,
                                               uint32_t ip_core_mask,
                                               uint32_t default_persona_mask,
                                               uint32_t hw_profile_crc32,
                                               uint32_t active_default_slot,
                                               uint32_t online_required)
{
    if (!s_refmem_core_initialized) {
        return false;
    }

    const uint32_t fields[] = {
        board_id,
        board_uuid_crc32,
        capability_mask,
        io_constraint_mask,
        ip_core_mask,
        default_persona_mask,
        hw_profile_crc32,
        active_default_slot,
        online_required,
    };
    const uint32_t payload_crc32 = distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    const uint32_t local_target = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    const uint32_t target_mask = (uint32_t)(1u << local_target);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_INLINE_SMALL,
        .payload_ref = board_id,
        .payload_size = (uint32_t)sizeof(fields),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }

    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                local_target,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
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
    if (staged) {
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(local_target,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_stage_model_turntable_load(uint32_t slot_id,
                                                   uint32_t output_index)
{
    if (!s_refmem_core_initialized || slot_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        return false;
    }

    const uint32_t payload_crc32 =
        distributed_refmem_model_payload_crc32(slot_id, output_index);
    const uint32_t target_mask = (uint32_t)(1u << slot_id);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = REFMEM_APP_INSTANCE_TEMPLATE_MODEL_TURNTABLE,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_INLINE_SMALL,
        .payload_ref = output_index,
        .payload_size = 2u * sizeof(uint32_t),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }
    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                slot_id,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    const bool staged =
        refmem_application_model_stage_scpi_node_config(
            slot_id,
            REFMEM_APP_INSTANCE_TEMPLATE_MODEL_TURNTABLE,
            REFMEM_APP_ROLE_MODEL_TURNTABLE | REFMEM_APP_ROLE_TEST_AGENT,
            REFMEM_APP_PERSONA_MODEL_INSTRUMENTS,
            1u,
            0u,
            0u);
    const bool loaded =
        staged &&
        distributed_refmem_execute_node_load_owner(
            REFMEM_APP_INSTANCE_TEMPLATE_MODEL_TURNTABLE,
            slot_id,
            output_index);
    if (loaded) {
        (void)distributed_refmem_node_load_auto_enqueue(
            REFMEM_APP_INSTANCE_TEMPLATE_MODEL_TURNTABLE);
        (void)distributed_refmem_command_ack(slot_id, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(slot_id,
                                          staged
                                              ? REFMEM_COMMAND_REASON_RUN_STATE_DENIED
                                              : REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_build_node_load_sync_frame(uint32_t instance_id,
                                                   uint8_t source_slot,
                                                   uint8_t target_mask,
                                                   uint32_t epoch_id,
                                                   uint32_t run_id,
                                                   uint32_t seq32,
                                                   uint32_t compact_time,
                                                   uint8_t *frame,
                                                   size_t frame_capacity,
                                                   size_t *frame_size)
{
    refmem_node_load_entry_t entry;
    if (!s_refmem_core_initialized ||
        !refmem_application_model_get_staging_node_load_entry(instance_id, &entry)) {
        if (frame_size != NULL) {
            *frame_size = 0u;
        }
        return false;
    }

    refmem_application_model_load_snapshot_t load;
    refmem_application_model_get_load_snapshot(&load);
    return refmem_node_load_sync_build_delta_frame(&entry,
                                                   source_slot,
                                                   target_mask,
                                                   epoch_id,
                                                   run_id,
                                                   seq32,
                                                   load.load_seq,
                                                   compact_time,
                                                   frame,
                                                   frame_capacity,
                                                   frame_size);
}

bool distributed_refmem_sync_command_handler(
    void *handler_context,
    const refmem_sync_command_payload_t *payload,
    uint8_t source_slot,
    uint32_t epoch_id,
    uint32_t run_id)
{
    (void)handler_context;
    refmem_command_request_t request;
    if (payload == NULL ||
        !refmem_command_request_from_sync_payload(
            payload, source_slot, epoch_id, run_id, &request)) {
        return false;
    }
    const uint32_t local_slot = s_refmem_sync_context.local_slot;
    if ((request.target_mask & (1u << local_slot)) == 0u ||
        !refmem_command_try_post(&s_refmem_command_slot, &request, osal_tick_ms())) {
        return false;
    }
    const refmem_command_take_result_t take =
        refmem_command_try_take(&s_refmem_command_slot,
                                local_slot,
                                epoch_id,
                                run_id,
                                request.payload_crc32,
                                0u);
    if (take != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }
    return refmem_command_ack(&s_refmem_command_slot, local_slot, 0u);
}

bool distributed_refmem_apply_node_load_sync_payload(const uint8_t *payload,
                                                     uint16_t payload_size)
{
    if (!s_refmem_core_initialized) {
        return false;
    }

    return distributed_refmem_apply_node_load_sync_payload_internal(payload,
                                                                    payload_size);
}

bool distributed_refmem_configure_node_load_auto_sync(
    uint32_t enabled,
    uint32_t local_slot,
    uint32_t target_mask,
    uint32_t baud_hz,
    uint32_t deadline_us,
    uint32_t uplink_duplex_mode,
    const refmem_spi_physical_pin_config_t *uplink_adapter_pins,
    uint32_t downlink_duplex_mode,
    const refmem_spi_physical_pin_config_t *downlink_adapter_pins)
{
    if (board_identity_get_no() == 5u) {
        return false;
    }
    if (!s_refmem_core_initialized ||
        enabled > 1u ||
        local_slot >= DISTRIBUTED_REFMEM_NODE_COUNT ||
        target_mask > 0xFFu ||
        deadline_us == 0u ||
        (uplink_duplex_mode != DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF &&
         uplink_duplex_mode != DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_FULL) ||
        (downlink_duplex_mode != DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF &&
         downlink_duplex_mode != DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_FULL) ||
        uplink_adapter_pins == NULL ||
        downlink_adapter_pins == NULL) {
        return false;
    }

    s_node_load_auto_sync.enabled = enabled;
    s_node_load_auto_sync.local_slot = (uint8_t)local_slot;
    s_node_load_auto_sync.target_mask = (uint8_t)target_mask;
    s_node_load_auto_sync.baud_hz =
        baud_hz == 0u ? BOARD_REFMEM_SPI_BAUD_HZ : baud_hz;
    s_node_load_auto_sync.deadline_us = deadline_us;
    s_node_load_auto_sync.uplink_duplex_mode = uplink_duplex_mode;
    s_node_load_auto_sync.downlink_duplex_mode = downlink_duplex_mode;
    s_node_load_auto_sync.uplink_adapter_pins = *uplink_adapter_pins;
    s_node_load_auto_sync.downlink_adapter_pins = *downlink_adapter_pins;
    s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_NONE;
    s_node_load_auto_sync.active_instance_id = 0u;
    s_node_load_auto_sync.active_intent_seq = 0u;
    s_node_load_auto_sync.last_processed_completed_seq = 0u;
    s_node_load_auto_sync.last_error = 0u;
    if (!refmem_sync_init(&s_refmem_sync_context,
                          s_node_load_auto_sync.local_slot,
                          s_node_load_auto_sync.epoch_id,
                          s_node_load_auto_sync.run_id)) {
        return false;
    }
    refmem_sync_set_command_handler(&s_refmem_sync_context,
                                    distributed_refmem_sync_command_handler,
                                    NULL);
    /* SCPI and RefMem are separate Core0 tasks. Only the RefMem service may
     * retire/write the dedicated command receiver; configuration leaves it
     * for the next service boundary. */
    return true;
}

void distributed_refmem_get_node_load_auto_sync(
    distributed_refmem_node_load_auto_sync_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->enabled = s_node_load_auto_sync.enabled;
    snapshot->local_slot = s_node_load_auto_sync.local_slot;
    snapshot->target_mask = s_node_load_auto_sync.target_mask;
    snapshot->baud_hz = s_node_load_auto_sync.baud_hz;
    snapshot->deadline_us = s_node_load_auto_sync.deadline_us;
    snapshot->uplink_duplex_mode = s_node_load_auto_sync.uplink_duplex_mode;
    snapshot->uplink_rx_pin = s_node_load_auto_sync.uplink_adapter_pins.rx_pin;
    snapshot->uplink_sck_pin = s_node_load_auto_sync.uplink_adapter_pins.sck_pin;
    snapshot->uplink_tx_pin = s_node_load_auto_sync.uplink_adapter_pins.tx_pin;
    snapshot->downlink_duplex_mode = s_node_load_auto_sync.downlink_duplex_mode;
    snapshot->downlink_rx_pin =
        s_node_load_auto_sync.downlink_adapter_pins.rx_pin;
    snapshot->downlink_sck_pin =
        s_node_load_auto_sync.downlink_adapter_pins.sck_pin;
    snapshot->downlink_tx_pin =
        s_node_load_auto_sync.downlink_adapter_pins.tx_pin;
    snapshot->pending_count = s_node_load_auto_sync.pending_count;
    snapshot->active_intent = s_node_load_auto_sync.active_intent;
    snapshot->active_instance_id = s_node_load_auto_sync.active_instance_id;
    snapshot->next_seq32 = s_node_load_auto_sync.next_seq32;
    snapshot->submitted_tx_count = s_node_load_auto_sync.submitted_tx_count;
    snapshot->submitted_rx_count = s_node_load_auto_sync.submitted_rx_count;
    snapshot->applied_rx_count = s_node_load_auto_sync.applied_rx_count;
    snapshot->failed_apply_count = s_node_load_auto_sync.failed_apply_count;
    snapshot->dropped_pending_count = s_node_load_auto_sync.dropped_pending_count;
    snapshot->last_rx_result = s_node_load_auto_sync.last_rx_result;
    snapshot->last_frame_type = s_node_load_auto_sync.last_frame_type;
    snapshot->last_source_slot = s_node_load_auto_sync.last_source_slot;
    snapshot->last_error = s_node_load_auto_sync.last_error;
}

bool distributed_refmem_refresh_vdc_command_context(void)
{
    vdc_dpll_manager_refmem_snapshot_t snapshot;
    if (!vdc_dpll_manager_get_refmem_snapshot(&snapshot)) {
        return false;
    }
    return distributed_refmem_refresh_vdc_command_context_from_snapshot(
        &snapshot);
}

bool distributed_refmem_refresh_vdc_command_context_from_snapshot(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    const uint8_t local_slot =
        (uint8_t)snapshot->schedule.local_slot_id;
    const uint32_t epoch_id = snapshot->clock_epoch_id;
    const uint32_t run_id = snapshot->clock_run_id;
    const uint32_t schedule_epoch = snapshot->schedule.schedule_epoch;
    if (local_slot >= REFMEM_SYNC_NODE_COUNT ||
        snapshot->control_profile.valid != 1u ||
        snapshot->control_profile.generation == 0u) {
        return false;
    }

    tdma_ring_clock_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) {
        /* A busy snapshot is not a STOP. Preserve retained values and retry
         * next RefMem turn; ordinary mailbox processing still proceeds. */
        return false;
    }
    const uint32_t ring_config_seq =
        ring.enabled != 0u && ring.adapter_started != 0u &&
        ring.config_seq != 0u && ring.config_seq == ring.applied_config_seq &&
        ring.local_slot_id == local_slot &&
        ring.schedule_crc32 == snapshot->schedule.schedule_crc32
            ? ring.config_seq : 0u;

    const bool identity_changed =
        !s_vdc_command_context_initialized ||
        s_vdc_command_context_local_slot != local_slot ||
        s_vdc_command_context_epoch_id != epoch_id ||
        s_vdc_command_context_run_id != run_id ||
        s_vdc_command_context_schedule_epoch != schedule_epoch;
    const bool consumer_changed = s_vdc_command_context.consumer_generation !=
                                  snapshot->control_profile.generation;
    const bool ring_changed = s_vdc_command_context.consumer_ring_config_seq !=
                             ring_config_seq;
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
    if (identity_changed || consumer_changed || ring_changed ||
        s_tdma_flight_sync.vdc_command_rx_admission_epoch == 0u) {
        distributed_refmem_tdma_reset_resident_command_state();
        s_tdma_flight_sync.vdc_command_rx_admission_epoch =
            tdma_service_core0_advance_flight_rx_admission_epoch(
                tdma_runtime_owner_get());
        if (s_tdma_flight_sync.vdc_command_rx_admission_epoch == 0u) {
            return false;
        }
    }
#else
    (void)consumer_changed;
    (void)ring_changed;
#endif
    if (identity_changed &&
        !refmem_sync_vdc_reset(&s_vdc_command_context,
                              local_slot,
                              epoch_id,
                              run_id)) {
        return false;
    }
    if (!refmem_sync_vdc_set_consumer_binding(
            &s_vdc_command_context, snapshot->control_profile.generation,
            ring_config_seq)) {
        return false;
    }
    s_vdc_command_context_local_slot = local_slot;
    s_vdc_command_context_epoch_id = epoch_id;
    s_vdc_command_context_run_id = run_id;
    s_vdc_command_context_schedule_epoch = schedule_epoch;
    s_vdc_command_context_initialized = true;
    return true;
}

void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_reset_vdc_command_batch)(void)
{
    memset(&s_vdc_command_batch, 0, sizeof(s_vdc_command_batch));
}

bool DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_begin_vdc_command_batch)(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot)
{
    if (snapshot == NULL ||
        snapshot->control_profile.valid != 1u ||
        snapshot->control_profile.mode != VDC_DPLL_CONTROL_MODE_MASTER ||
        snapshot->schedule.local_slot_id >= REFMEM_SYNC_NODE_COUNT ||
        snapshot->schedule.ring_binding.node_count == 0u ||
        snapshot->schedule.ring_binding.node_count > REFMEM_SYNC_NODE_COUNT ||
        (snapshot->dpll_update_seq == s_vdc_command_published_update_seq &&
         snapshot->control_profile.generation ==
             s_vdc_command_published_generation &&
         snapshot->schedule.local_slot_id ==
             s_vdc_command_published_source_slot &&
         snapshot->schedule.schedule_crc32 ==
             s_vdc_command_published_schedule_crc32)) {
        return false;
    }

    memset(&s_vdc_command_batch, 0, sizeof(s_vdc_command_batch));
    s_vdc_command_batch.active = 1u;
    s_vdc_command_batch.source_slot = snapshot->schedule.local_slot_id;
    s_vdc_command_batch.node_count =
        snapshot->schedule.ring_binding.node_count;
    s_vdc_command_batch.command_seq =
        distributed_refmem_next_nonzero_sequence(&s_vdc_command_sequence);
    s_vdc_command_batch.control_generation = snapshot->control_profile.generation;
    s_vdc_command_batch.schedule_crc32 = snapshot->schedule.schedule_crc32;
    s_vdc_command_batch.period_adjust_ppb =
        snapshot->dco_period_adjust_ppb;
    s_vdc_command_batch.phase_offset_ns = snapshot->dco_phase_offset_ns;
    s_vdc_command_batch.lock_state = snapshot->dpll_state;
    s_vdc_command_batch.quality = snapshot->quality_health_state;
    s_vdc_command_batch.epoch_id = snapshot->clock_epoch_id;
    s_vdc_command_batch.run_id = snapshot->clock_run_id;
    s_vdc_command_batch.source_update_seq = snapshot->dpll_update_seq;
    return true;
}

bool DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_capture_vdc_command_batch)(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot)
{
    if (snapshot == NULL || snapshot->control_profile.valid != 1u ||
        snapshot->control_profile.mode != VDC_DPLL_CONTROL_MODE_MASTER) {
        if (s_vdc_command_batch.active != 0u) {
            distributed_refmem_reset_vdc_command_batch();
        }
        return false;
    }
    if (s_vdc_command_batch.active == 0u) {
        return distributed_refmem_begin_vdc_command_batch(snapshot);
    }
    if (s_vdc_command_batch.source_slot != snapshot->schedule.local_slot_id ||
        s_vdc_command_batch.node_count !=
            snapshot->schedule.ring_binding.node_count ||
        s_vdc_command_batch.control_generation !=
            snapshot->control_profile.generation ||
        s_vdc_command_batch.schedule_crc32 != snapshot->schedule.schedule_crc32 ||
        s_vdc_command_batch.epoch_id != snapshot->clock_epoch_id ||
        s_vdc_command_batch.run_id != snapshot->clock_run_id) {
        distributed_refmem_reset_vdc_command_batch();
        return false;
    }
    return true;
}

/* Enqueue at most one target at the TDMA owner boundary.  The frozen batch
 * prevents source fields from changing while its per-target frames are
 * admitted, and the first admission selects the common future window. */
bool distributed_refmem_publish_vdc_command_once(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    if (snapshot->control_profile.valid != 1u ||
        snapshot->control_profile.mode != VDC_DPLL_CONTROL_MODE_MASTER) {
        if (s_vdc_command_batch.active != 0u) {
            distributed_refmem_reset_vdc_command_batch();
        }
        return false;
    }

    if (s_vdc_command_batch.active == 0u ||
        s_vdc_command_batch.source_slot != snapshot->schedule.local_slot_id ||
               s_vdc_command_batch.node_count !=
                   snapshot->schedule.ring_binding.node_count ||
               s_vdc_command_batch.control_generation !=
                   snapshot->control_profile.generation ||
               s_vdc_command_batch.schedule_crc32 !=
                   snapshot->schedule.schedule_crc32 ||
               s_vdc_command_batch.epoch_id != snapshot->clock_epoch_id ||
               s_vdc_command_batch.run_id != snapshot->clock_run_id) {
        distributed_refmem_reset_vdc_command_batch();
        return false;
    }

    if (s_vdc_command_batch.window_planned == 0u) {
        vdc_tdma_window_plan_t plan;
        vdc_gate_result_t gate;
        if (!vdc_dpll_manager_plan_published_tdma_window(
                snapshot,
                VDC_DOMAIN_WINDOW_REFMEM_DATA,
                VDC_DPLL_MANAGER_PLAN_NOW_NS,
                &plan,
                &gate) ||
            !plan.valid || plan.window_start_ns == 0u ||
            plan.schedule_crc32 != s_vdc_command_batch.schedule_crc32) {
            (void)gate;
            distributed_refmem_reset_vdc_command_batch();
            return false;
        }
        s_vdc_command_batch.window_epoch = plan.schedule_epoch;
        s_vdc_command_batch.effective_vdc_time_ns = plan.window_start_ns;
        s_vdc_command_batch.window_end_ns = plan.window_end_ns;
        s_vdc_command_batch.guard_start_ns = plan.guard_start_ns;
        s_vdc_command_batch.guard_end_ns = plan.guard_end_ns;
        s_vdc_command_batch.window_planned = 1u;
    }

    while (s_vdc_command_batch.next_target_index <
           s_vdc_command_batch.node_count) {
        const uint32_t target = s_vdc_command_batch.next_target_index;
        if (target == s_vdc_command_batch.source_slot) {
            s_vdc_command_batch.next_target_index++;
            continue;
        }

        uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
        size_t frame_size = 0u;
        const uint32_t frame_seq =
            distributed_refmem_next_nonzero_sequence(
                &s_vdc_command_frame_sequence);
        if (!refmem_sync_vdc_command_frame_build(
                (uint8_t)s_vdc_command_batch.source_slot,
                (uint8_t)target,
                s_vdc_command_batch.epoch_id,
                s_vdc_command_batch.run_id,
                frame_seq,
                osal_tick_ms(),
                s_vdc_command_batch.control_generation,
                s_vdc_command_batch.command_seq,
                s_vdc_command_batch.schedule_crc32,
                s_vdc_command_batch.effective_vdc_time_ns,
                s_vdc_command_batch.period_adjust_ppb,
                s_vdc_command_batch.phase_offset_ns,
                s_vdc_command_batch.lock_state,
                s_vdc_command_batch.quality,
                frame,
                sizeof(frame),
                &frame_size)) {
            return false;
        }

        refmem_realtime_tdma_intent_config_t config = {
            .window_epoch = s_vdc_command_batch.window_epoch,
            .window_index = frame_seq,
            .deadline_us = 1000000u,
            .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
            .baud_hz = BOARD_REFMEM_SPI_BAUD_HZ,
            .pins = {
                .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
                .csn_pin = REFMEM_SPI_PHYSICAL_PIN_UNUSED,
                .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
                .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
            },
            .payload_class = REFMEM_REALTIME_TDMA_PAYLOAD_VDC_COMMAND,
            .vdc_window_plan_valid = 1u,
            .vdc_window_class = VDC_DOMAIN_WINDOW_REFMEM_DATA,
            .vdc_schedule_crc32 = s_vdc_command_batch.schedule_crc32,
            .vdc_window_start_ns = s_vdc_command_batch.effective_vdc_time_ns,
            .vdc_window_end_ns = s_vdc_command_batch.window_end_ns,
            .vdc_guard_start_ns = s_vdc_command_batch.guard_start_ns,
            .vdc_guard_end_ns = s_vdc_command_batch.guard_end_ns,
            .frame = frame,
            .frame_size = frame_size,
        };
        if (!refmem_realtime_tdma_submit_tx(&s_refmem_realtime_tdma,
                                            &config)) {
            return false;
        }
        s_vdc_command_batch.next_target_index++;
        return true;
    }

    s_vdc_command_published_update_seq =
        s_vdc_command_batch.source_update_seq;
    s_vdc_command_published_generation =
        s_vdc_command_batch.control_generation;
    s_vdc_command_published_source_slot = s_vdc_command_batch.source_slot;
    s_vdc_command_published_schedule_crc32 =
        s_vdc_command_batch.schedule_crc32;
    distributed_refmem_reset_vdc_command_batch();
    return false;
}

void distributed_refmem_tdma_publish_service(void)
{
    if (!s_refmem_core_initialized || s_tdma_flight_sync.enabled != 0u ||
        s_vdc_command_batch.active == 0u) {
        return;
    }
    vdc_dpll_manager_refmem_snapshot_t snapshot;
    if (!vdc_dpll_manager_get_refmem_snapshot(&snapshot)) {
        return;
    }
    (void)distributed_refmem_publish_vdc_command_once(&snapshot);
}

bool distributed_refmem_get_vdc_follower_command(
    uint32_t source_slot,
    uint32_t expected_consumer_generation,
    uint32_t expected_ring_config_seq,
    refmem_sync_vdc_command_snapshot_t *snapshot)
{
    if (snapshot == NULL || source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }
    return refmem_sync_vdc_copy_command_for_binding(
        &s_vdc_command_context, (uint8_t)source_slot,
        expected_consumer_generation, expected_ring_config_seq, snapshot);
}

bool distributed_refmem_set_tdma_ring_local_slot(uint32_t local_slot_id)
{
    const tdma_foundation_profile_t *profile =
        refmem_application_model_get_tdma_foundation_profile();
    if (profile == NULL) {
        return false;
    }
    return distributed_refmem_set_tdma_ring_topology(
        local_slot_id, profile->ring.reference_index, profile->ring.node_count);
}

bool distributed_refmem_set_tdma_ring_topology(uint32_t local_slot_id,
                                               uint32_t reference_slot_id,
                                               uint32_t node_count)
{
    /* NO5 is an external phase/DPLL observer.  It must remain outside the
     * NO1..NO4 TDMA ring so a maintenance command cannot claim flight PIO,
     * state-machine, DMA, GPIO, IRQ or DREQ resources on the observer board. */
    if (board_identity_get_no() == 5u) {
        return false;
    }
    if (!s_refmem_core_initialized) {
        return false;
    }
    tdma_service_service_t *tdma_owner = tdma_runtime_owner_get();
    tdma_ring_runtime_snapshot_t ring_snapshot;
    if (tdma_owner == NULL ||
        !tdma_ring_runtime_get_snapshot(&tdma_owner->ring_runtime,
                                        &ring_snapshot) ||
        ring_snapshot.enabled != 0u) {
        return false;
    }
    if (!vdc_dpll_manager_set_tdma_ring_topology(local_slot_id,
                                                 reference_slot_id,
                                                 node_count)) {
        return false;
    }
    if (!refmem_application_model_set_tdma_ring_topology(local_slot_id,
                                                         reference_slot_id,
                                                         node_count)) {
        return false;
    }
    const tdma_foundation_profile_t *profile =
        refmem_application_model_get_tdma_foundation_profile();
    uint32_t schedule_crc32 = 0u;
    if (!distributed_refmem_tdma_profile_activation_ready(profile,
                                                          &schedule_crc32) ||
        !distributed_refmem_apply_tdma_foundation_profile(profile,
                                                          schedule_crc32)) {
        return false;
    }
    return true;
}

bool distributed_refmem_tdma_ring_arm(void)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    if (board_identity_get_no() == 5u) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_CONFIG_REJECTED,
                         __ATOMIC_RELEASE);
        return false;
    }
    if (!s_refmem_core_initialized || owner == NULL) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_OWNER_UNAVAILABLE,
                         __ATOMIC_RELEASE);
        return false;
    }
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_ring_runtime_get_snapshot(&owner->ring_runtime, &ring)) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_SNAPSHOT_UNAVAILABLE,
                         __ATOMIC_RELEASE);
        return false;
    }
    if (ring.enabled != 0u || ring.adapter_started != 0u) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_ACTIVE,
                         __ATOMIC_RELEASE);
        return false;
    }
    if (owner->ring_staged_config.enabled == 0u) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_STAGED_CONFIG_MISSING,
                         __ATOMIC_RELEASE);
        return false;
    }
    const tdma_process_image_map_t map =
        distributed_refmem_default_flight_map(owner->ring_staged_config.node_count);
    const tdma_service_flight_map_result_t map_result =
        tdma_service_configure_flight_map_checked(owner, &map);
    if (map_result != TDMA_SERVICE_FLIGHT_MAP_OK) {
        distributed_refmem_tdma_arm_result_t result =
            DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_REJECTED;
        switch (map_result) {
        case TDMA_SERVICE_FLIGHT_MAP_SNAPSHOT_UNAVAILABLE:
            result = DISTRIBUTED_REFMEM_TDMA_ARM_SNAPSHOT_UNAVAILABLE;
            break;
        case TDMA_SERVICE_FLIGHT_MAP_RUNTIME_ACTIVE:
            result = DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_ACTIVE;
            break;
        case TDMA_SERVICE_FLIGHT_MAP_BUSY:
            result = DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_BUSY;
            break;
        case TDMA_SERVICE_FLIGHT_MAP_ENGINE_ACTIVE:
            result = DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_ENGINE_ACTIVE;
            break;
        default:
            break;
        }
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         result, __ATOMIC_RELEASE);
        return false;
    }
    if (owner->calibration_gate_required != 0u &&
        (owner->calibration_stage.profile_crc32 !=
             owner->ring_staged_config.operating_profile_crc32 ||
         owner->calibration_stage.schedule_crc32 !=
             owner->ring_staged_config.schedule_crc32 ||
         !tdma_ring_runtime_validate_calibration_stage(
             &owner->calibration_stage,
             owner->ring_staged_config.node_count,
             NULL))) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_CALIBRATION_GATE_REJECTED,
                         __ATOMIC_RELEASE);
        return false;
    }
    if (!tdma_service_ring_arm(owner)) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_CONFIG_REJECTED,
                         __ATOMIC_RELEASE);
        return false;
    }
    __atomic_store_n(&s_tdma_ring_arm_last_result,
                     DISTRIBUTED_REFMEM_TDMA_ARM_OK,
                     __ATOMIC_RELEASE);
    return true;
}

distributed_refmem_tdma_arm_result_t
distributed_refmem_tdma_ring_arm_last_result(void)
{
    return (distributed_refmem_tdma_arm_result_t)__atomic_load_n(
        &s_tdma_ring_arm_last_result, __ATOMIC_ACQUIRE);
}

bool distributed_refmem_tdma_ring_train(uint32_t cycles)
{
    return board_identity_get_no() != 5u &&
           s_refmem_core_initialized && tdma_runtime_owner_train_clock(cycles);
}

bool distributed_refmem_tdma_ring_start(void)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    return board_identity_get_no() != 5u &&
           s_refmem_core_initialized && owner != NULL && tdma_service_ring_start(owner);
}

bool distributed_refmem_tdma_ring_stop(void)
{
    action_stop_requested();
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    return s_refmem_core_initialized && owner != NULL && tdma_service_ring_stop(owner);
}

bool distributed_refmem_set_tdma_ring_log_enabled(bool enabled)
{
    if (!s_refmem_core_initialized) {
        return false;
    }

    s_tdma_ring_log_enabled = enabled;
    if (!enabled) {
        s_tdma_ring_log_last_ms = 0u;
    }
    return true;
}

bool distributed_refmem_get_tdma_ring_log_enabled(void)
{
    return s_tdma_ring_log_enabled;
}

void distributed_refmem_get_core_vector(distributed_refmem_core_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    osal_critical_enter();
    const refmem_vector_header_region_t *header = distributed_refmem_header();
    snapshot->version = header->layout_version;
    snapshot->table_seq = header->table_seq;
    snapshot->core_count = header->core_count;
    snapshot->core0_vtor_owner = header->core0_vtor_owner;
    snapshot->core1_vtor_owner = header->core1_vtor_owner;
    snapshot->core0_irq_owner_mask = header->core0_irq_owner_mask;
    snapshot->core1_irq_owner_mask = header->core1_irq_owner_mask;
    snapshot->entry_table_owner = header->entry_table_owner;
    snapshot->flags = header->flags;
    snapshot->guard.table_seq = header->table_seq;
    snapshot->guard.owner = header->table_owner;
    snapshot->guard.crc32 = header->header_crc32;
    snapshot->guard.stale = header->header_stale;
    snapshot->guard.flags = header->flags;
    osal_critical_exit();
}

void distributed_refmem_get_runtime_protection(distributed_refmem_runtime_protection_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    osal_critical_enter();
    distributed_refmem_publish_runtime_locked();
    const refmem_vector_header_region_t *header = distributed_refmem_header();
    snapshot->version = header->layout_version;
    snapshot->table_seq = header->table_seq;
    snapshot->ram_resident_required = header->ram_resident_required;
    snapshot->flash_lockout_supported = header->flash_lockout_supported;
    snapshot->flash_lockout_online = header->flash_lockout_online;
    snapshot->flash_lockout_requested = header->flash_lockout_requested;
    snapshot->flash_lockout_acknowledged = header->flash_lockout_acknowledged;
    snapshot->park_state = header->core1_park_state;
    snapshot->last_result = header->flash_lockout_last_result;
    snapshot->last_elapsed_us = header->flash_lockout_last_elapsed_us;
    snapshot->request_seq = header->flash_lockout_request_seq;
    snapshot->ack_seq = header->flash_lockout_ack_seq;
    snapshot->release_seq = header->flash_lockout_release_seq;
    snapshot->timeout_count = header->flash_lockout_timeout_count;
    snapshot->release_timeout_count = header->flash_lockout_release_timeout_count;
    snapshot->entry_table_owner = header->entry_table_owner;
    snapshot->flags = header->runtime_protection_flags;
    snapshot->guard.table_seq = header->table_seq;
    snapshot->guard.owner = header->table_owner;
    snapshot->guard.crc32 = header->header_crc32;
    snapshot->guard.stale = header->header_stale;
    snapshot->guard.flags = header->flags;
    osal_critical_exit();
}

void distributed_refmem_get_status(distributed_refmem_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_status;
    osal_critical_exit();
}

bool distributed_refmem_get_node(uint32_t node_id, distributed_refmem_node_snapshot_t *snapshot)
{
    if (snapshot == NULL || node_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        return false;
    }

    osal_critical_enter();

    const refmem_vector_node_region_t *node = distributed_refmem_node_region(node_id);
    snapshot->node_id = node->node_id;
    snapshot->state = node->state;
    snapshot->heartbeat = node->heartbeat;
    snapshot->slot_version = node->slot_version;
    snapshot->last_update_ms = node->last_update_ms;
    snapshot->stale_count = node->stale_count;
    snapshot->fault_code = node->fault_code;
    snapshot->flags = node->flags;
    snapshot->node_type = node->node_type;

    osal_critical_exit();
    return true;
}

bool distributed_refmem_command_aggregate(refmem_command_aggregate_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->node_count = DISTRIBUTED_REFMEM_NODE_COUNT;
    uint32_t newest_seq = 0u;
    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_COUNT; ++i) {
        refmem_command_snapshot_t snapshot;
        if (!distributed_refmem_node_control_read(i, &snapshot, NULL, NULL)) {
            continue;
        }
        switch ((refmem_command_state_t)snapshot.state) {
        case REFMEM_COMMAND_STATE_POSTED: out->posted++; break;
        case REFMEM_COMMAND_STATE_TAKEN: out->taken++; break;
        case REFMEM_COMMAND_STATE_BUSY: out->busy++; break;
        case REFMEM_COMMAND_STATE_ACKED: out->acked++; break;
        case REFMEM_COMMAND_STATE_NACKED: out->nacked++; break;
        case REFMEM_COMMAND_STATE_TIMED_OUT: out->timed_out++; break;
        default: break;
        }
        const bool terminal =
            snapshot.state == (uint32_t)REFMEM_COMMAND_STATE_ACKED ||
            snapshot.state == (uint32_t)REFMEM_COMMAND_STATE_NACKED ||
            snapshot.state == (uint32_t)REFMEM_COMMAND_STATE_TIMED_OUT;
        if (terminal && snapshot.command_seq >= newest_seq) {
            newest_seq = snapshot.command_seq;
            out->last_state = snapshot.state;
            out->last_reason = snapshot.last_reason;
            out->last_reason_slot = i;
            out->last_completed_seq =
                snapshot.last_completed_seq != 0u ? snapshot.last_completed_seq
                                                  : snapshot.command_seq;
        }
    }
    return true;
}
