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

#include "trigger_sequence_pilot.h"
/* The T2 runtime snapshot type used by the reservation page below used to
 * arrive through scpi_trigger_t2_runtime_commands.inc. That page handler is a
 * separate translation unit now, so name the dependency here. */
#include "trigger_t2_runtime.h"
#include "tdma_seat_admission.h"
#include "tdma_seat_mount_store.h"
#include "scpi_snapshot_commands_internal.h"
scpi_result_t scpi_cmd_system_tdma_long_frame_status_q(scpi_t *context)
{
    tdma_long_frame_ensure_ready();
    tdma_maintenance_snapshot_t snapshot;
    tdma_maintenance_get_snapshot(&s_tdma_maintenance, &snapshot);
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.tx_sent_count);
    SCPI_ResultUInt32(context, snapshot.tx_blocked_count);
    SCPI_ResultUInt32(context, snapshot.rx_published_count);
    SCPI_ResultUInt32(context, snapshot.rx_reject_count);
    SCPI_ResultUInt32(context, snapshot.window_open_count);
    SCPI_ResultUInt32(context, snapshot.cycle_count);
    SCPI_ResultUInt32(context, snapshot.window_open ? 1u : 0u);
    uint32_t mailbox_calls = 0u;
    uint32_t chunks = 0u;
    uint32_t frames = 0u;
    uint32_t rejects = 0u;
    tdma_maintenance_runtime_get_counters(
        &mailbox_calls, &chunks, &frames, &rejects);
    SCPI_ResultUInt32(context, TDMA_LONG_FRAME_MAX);
    SCPI_ResultUInt32(context, mailbox_calls);
    SCPI_ResultUInt32(context, chunks);
    SCPI_ResultUInt32(context, frames);
    SCPI_ResultUInt32(context, rejects);
    /* RX reassembly health: a restart means a frame was abandoned because a
     * chunk was lost or another frame interleaved; duplicates are harmless.
     * Both are needed to tell CRC corruption from interleaving. */
    tdma_maintenance_runtime_diag_t runtime_diag;
    tdma_maintenance_runtime_get_diag(&runtime_diag);
    SCPI_ResultUInt32(context, runtime_diag.rx_restarts);
    SCPI_ResultUInt32(context, runtime_diag.rx_duplicate_chunks);
    /* P0-W attribution: `rejects` above mixes several failure sources, so the
     * split is published read-only.  A chunk cannot be rejected before it is
     * handed to the wire, hence chunks >= rx_chunk_rejects must always hold. */
    SCPI_ResultUInt32(context, runtime_diag.rx_chunk_rejects);
    SCPI_ResultUInt32(context, runtime_diag.tx_retries);
    SCPI_ResultUInt32(context, runtime_diag.tx_fail);
    SCPI_ResultUInt32(context, runtime_diag.queue_full);
    uint32_t priority_calls = 0u;
    uint32_t yield_slots = 0u;
    uint32_t attempts = 0u;
    uint32_t publish_ok = 0u;
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_pio_spi_ring_adapter_get_maintenance_counters(
        adapter, &priority_calls, &yield_slots, &attempts, &publish_ok);
    SCPI_ResultUInt32(context, priority_calls);
    SCPI_ResultUInt32(context, yield_slots);
    SCPI_ResultUInt32(context, attempts);
    SCPI_ResultUInt32(context, publish_ok);
    return SCPI_RES_OK;
}

bool scpi_refmem_wait_storage_job(uint32_t job_id)
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

bool scpi_refmem_read_package(const char *path,
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
        if (!storage_manager_acquire_file_read_job_result(job_id, &read_info)) {
            return false;
        }
        /* Copy straight into the caller's destination.  The former 512-byte
         * static staging chunk duplicated the storage result buffer and cost
         * main RAM for no isolation benefit (the job already owns its own
         * buffer until release). */
        size_t copied = 0u;
        const bool copied_ok = storage_manager_copy_file_read_job_result(
            job_id, 0u, buffer + *returned_size,
            buffer_size - *returned_size, &copied);
        storage_manager_release_file_read_job_result(job_id);
        if (!copied_ok || copied == 0u || copied > read_length ||
            copied > read_info.returned ||
            *returned_size + copied > buffer_size) {
            return false;
        }
        *returned_size += copied;
        offset += copied;
    }

    return *returned_size == info_job.size;
}

bool scpi_refmem_model_mode_idle(void)
{
    refmem_application_model_load_snapshot_t snapshot;
    refmem_application_model_get_load_snapshot(&snapshot);
    return snapshot.mode == REFMEM_APP_MODEL_MODE_IDLE;
}

bool scpi_refmem_realtime_idle(void)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    return vector.state == TRIG_STATE_IDLE;
}

void scpi_refmem_result_load_snapshot(scpi_t *context,
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

void scpi_refmem_result_board_load_snapshot(
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

void scpi_refmem_result_table_image_descriptor(
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

uint32_t scpi_refmem_read_u32_le(const uint8_t *data)
{
    if (data == NULL) {
        return 0u;
    }
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

bool scpi_refmem_sync_ensure_initialized(void)
{
    if (s_refmem_sync.initialized != 0u) {
        return true;
    }

    const uint8_t local_slot = 0u;
    if (!refmem_sync_init(scpi_refmem_sync_context(),
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

void scpi_refmem_sync_apply_node_load_delta(const refmem_sync_rx_snapshot_t *rx)
{
    if (rx == NULL ||
        rx->accepted == 0u ||
        rx->header.frame_type != (uint8_t)REFMEM_SYNC_FRAME_DELTA) {
        return;
    }
    (void)distributed_refmem_apply_node_load_sync_payload(rx->payload,
                                                          rx->payload_size);
}

uint32_t scpi_refmem_sync_build_id_crc32(void)
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

bool scpi_refmem_sync_build_hello_frame(uint8_t source_slot,
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

bool scpi_refmem_sync_build_epoch_frame(uint8_t source_slot,
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

bool scpi_refmem_sync_build_delta_frame(uint8_t source_slot,
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

bool scpi_refmem_sync_build_ack_frame(uint8_t source_slot,
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

bool scpi_refmem_sync_build_fence_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint32_t fence_seq,
                                               uint32_t fence_scope,
                                               uint32_t required_mask,
                                               uint32_t min_table_seq,
                                               uint32_t deadline_us,
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
    payload.deadline_us = deadline_us;

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

bool scpi_refmem_sync_build_quality_frame(uint8_t source_slot,
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
        refmem_sync_get_peer(scpi_refmem_sync_context(), (uint8_t)target_slot);
    if (peer != NULL) {
        payload.seq_expected = peer->expected_seq32;
        payload.seq_last = peer->last_seq32;
    }

    refmem_sync_quality_counters_t counters;
    refmem_sync_get_quality(scpi_refmem_sync_context(), &counters);
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
    payload.p99_us = 0u;
    payload.p999_us = 0u;
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

void scpi_refmem_sync_result_rx_snapshot(scpi_t *context,
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

void scpi_refmem_sync_hex_encode(const uint8_t *data,
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

int scpi_refmem_sync_hex_nibble(char ch)
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

bool scpi_refmem_sync_hex_decode(const char *hex,
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

/* Compact read-only views of the core1-owned RefMem vectors.  The complete
 * payload remains available to in-process consumers through the seqlock
 * getter; SCPI exposes only the fields needed by NO5/maintenance monitoring so
 * the command cannot become a realtime data path. */
scpi_result_t scpi_cmd_refmem_vdc_vector_q(scpi_t *context)
{
    distributed_refmem_vdc_vector_snapshot_t snapshot;
    if (!distributed_refmem_get_vdc_vector_snapshot(&snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, snapshot.flags);
    SCPI_ResultUInt32(context, snapshot.stable_sequence);
    SCPI_ResultUInt32(context, snapshot.publish_sequence);
    SCPI_ResultUInt32(context, snapshot.source_update_seq);
    SCPI_ResultUInt32(context, snapshot.source_service_count);
    SCPI_ResultUInt32(context, snapshot.schedule_epoch);
    SCPI_ResultUInt32(context, snapshot.local_node_id);
    SCPI_ResultUInt32(context, snapshot.reference_node_id);
    SCPI_ResultUInt32(context, snapshot.node_count);
    SCPI_ResultUInt32(context, snapshot.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.servo_profile_crc32);
    SCPI_ResultUInt32(context, snapshot.path_delay_table_crc32);
    SCPI_ResultUInt32(context, snapshot.path_delay_generation);
    SCPI_ResultUInt32(context, snapshot.dpll_state);
    SCPI_ResultInt32(context, snapshot.dpll_last_phase_error_ns);
    SCPI_ResultInt32(context, snapshot.dpll_last_frequency_error_ppb);
    SCPI_ResultInt32(context, snapshot.clock_phase_offset_ns);
    SCPI_ResultInt32(context, snapshot.clock_period_adjust_ppb);
    SCPI_ResultUInt32(context, snapshot.quality_health_state);
    SCPI_ResultUInt32(context, snapshot.quality_lock_quality_tier);
    SCPI_ResultUInt32(context, snapshot.quality_last_timestamp_source);
    SCPI_ResultUInt32(context, snapshot.quality_last_timestamp_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.quality_last_timestamp_flags);
    SCPI_ResultUInt32(context, snapshot.gate_passed);
    SCPI_ResultUInt32(context, snapshot.gate_reject_code);
    SCPI_ResultUInt32(context, snapshot.payload_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_dpll_vector_q(scpi_t *context)
{
    distributed_refmem_dpll_vector_snapshot_t snapshot;
    if (!distributed_refmem_get_dpll_vector_snapshot(&snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, snapshot.flags);
    SCPI_ResultUInt32(context, snapshot.stable_sequence);
    SCPI_ResultUInt32(context, snapshot.publish_sequence);
    SCPI_ResultUInt32(context, snapshot.source_update_seq);
    SCPI_ResultUInt32(context, snapshot.source_service_count);
    SCPI_ResultUInt32(context, snapshot.ready);
    SCPI_ResultUInt32(context, snapshot.schedule_epoch);
    SCPI_ResultUInt32(context, snapshot.local_node_id);
    SCPI_ResultUInt32(context, snapshot.reference_node_id);
    SCPI_ResultUInt32(context, snapshot.node_count);
    SCPI_ResultUInt32(context, snapshot.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.servo_profile_crc32);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.dpll_update_seq);
    SCPI_ResultInt32(context, snapshot.last_phase_error_ns);
    SCPI_ResultInt32(context, snapshot.last_frequency_error_ppb);
    SCPI_ResultInt32(context, snapshot.last_offset_ns);
    SCPI_ResultUInt32(context, snapshot.dco_valid);
    SCPI_ResultUInt32(context, snapshot.dco_update_seq);
    SCPI_ResultUInt32(context, snapshot.dco_source_model_seq);
    SCPI_ResultUInt32(context, snapshot.dco_lock_state);
    SCPI_ResultInt32(context, snapshot.dco_phase_offset_ns);
    SCPI_ResultInt32(context, snapshot.dco_period_adjust_ppb);
    SCPI_ResultUInt32(context, snapshot.quality_health_state);
    SCPI_ResultUInt32(context, snapshot.quality_lock_quality_tier);
    SCPI_ResultUInt32(context, snapshot.quality_last_timestamp_source);
    SCPI_ResultUInt32(context, snapshot.quality_last_timestamp_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.quality_last_timestamp_flags);
    SCPI_ResultUInt32(context, snapshot.gate_passed);
    SCPI_ResultUInt32(context, snapshot.gate_reject_code);
    SCPI_ResultUInt32(context, snapshot.path_delay_table_crc32);
    SCPI_ResultUInt32(context, snapshot.path_delay_generation);
    SCPI_ResultUInt32(context, snapshot.payload_crc32);
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
    SCPI_ResultUInt32(context, snapshot.timeout_us);
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
