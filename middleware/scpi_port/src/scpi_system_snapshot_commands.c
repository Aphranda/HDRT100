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
#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
#include "refmem_bootstrap_transport.h"
#include "refmem_bootstrap_relay.h"
#endif
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
#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
#include "refmem_bootstrap_transport.h"
#include "refmem_bootstrap_relay.h"
#endif
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

static tdma_long_frame_fifo_t s_tdma_long_frame_tx;
tdma_long_frame_fifo_t s_tdma_long_frame_rx;
tdma_maintenance_service_t s_tdma_maintenance;
static bool s_tdma_long_frame_ready;
static tdma_maintenance_stream_tx_t s_tdma_maintenance_stream;
static bool s_tdma_maintenance_stream_active;
static uint32_t s_tdma_maintenance_mailbox_seq;
static volatile uint32_t s_tdma_maintenance_mailbox_calls;
static volatile uint32_t s_tdma_maintenance_chunks;

bool tdma_ring_maintenance_mailbox(void *context,
                                          uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE])
{
    (void)context;
    if (!s_tdma_maintenance_stream_active) {
        return false;
    }
    s_tdma_maintenance_mailbox_calls++;
    uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE];
    if (!tdma_maintenance_stream_next_chunk(&s_tdma_maintenance_stream, chunk)) {
        s_tdma_maintenance_stream_active = false;
        return false;
    }
    s_tdma_maintenance_chunks++;
    memset(mailbox, 0, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    mailbox[0] = (uint8_t)TDMA_FLIGHT_MAILBOX_MAGIC;
    mailbox[1] = (uint8_t)(TDMA_FLIGHT_MAILBOX_MAGIC >> 8u);
    mailbox[2] = TDMA_FLIGHT_MAILBOX_VERSION;
    mailbox[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
    mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] =
        (uint8_t)(board_identity_get_no() - 1u);
    mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 0x0Fu;
    mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET] =
        (uint8_t)s_tdma_maintenance_mailbox_seq;
    mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET + 1u] =
        (uint8_t)(s_tdma_maintenance_mailbox_seq >> 8u);
    s_tdma_maintenance_mailbox_seq++;
    memcpy(mailbox + TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE,
           chunk, sizeof(chunk));
    const uint16_t crc =
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET);
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
    return true;
}

bool tdma_ring_maintenance_send(void *context,
                                       const uint8_t *frame,
                                       size_t frame_size,
                                       uint32_t generation,
                                       uint32_t sequence,
                                       uint32_t segment_mask)
{
    (void)context;
    (void)frame;
    (void)frame_size;
    (void)generation;
    (void)sequence;
    (void)segment_mask;
    /* The origin TX scheduler owns admission; the old physical bypass is gone. */
    return false;
}

bool tdma_ring_maintenance_poll(void *context,
                                       uint8_t *frame,
                                       size_t frame_capacity,
                                       size_t *frame_size,
                                       uint32_t *generation,
                                       uint32_t *sequence,
                                       uint32_t *segment_mask)
{
    (void)context;
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    uint32_t transport_sequence = 0u;
    if (adapter == NULL ||
        !tdma_pio_spi_ring_adapter_read_maintenance(
            adapter, frame, frame_capacity, frame_size, &transport_sequence)) {
        return false;
    }
    *generation = 1u;
    *sequence = transport_sequence;
    *segment_mask = 1u;
    return true;
}

bool tdma_ring_maintenance_service(void *context, uint64_t now_ns)
{
    (void)now_ns;
    tdma_maintenance_on_cycle((tdma_maintenance_service_t *)context);
    return true;
}

void tdma_long_frame_ensure_ready(void)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    if (s_tdma_long_frame_ready) {
        if (adapter != NULL) {
            tdma_pio_spi_ring_adapter_set_maintenance_service(
                adapter, tdma_ring_maintenance_service, &s_tdma_maintenance);
            tdma_pio_spi_ring_adapter_set_maintenance_mailbox(
                adapter, tdma_ring_maintenance_mailbox, NULL);
        }
        return;
    }
    tdma_long_frame_fifo_init(&s_tdma_long_frame_tx);
    tdma_long_frame_fifo_init(&s_tdma_long_frame_rx);
    static const tdma_maintenance_transport_ops_t ops = {
        .send = tdma_ring_maintenance_send,
        .poll = tdma_ring_maintenance_poll,
    };
    static const tdma_maintenance_config_t config = {
        .period_cycles = 64u,
        .window_cycles = 8u,
        .max_window_bytes = TDMA_LONG_FRAME_MAX,
    };
    if (tdma_maintenance_init(&s_tdma_maintenance,
                              &s_tdma_long_frame_tx,
                              &s_tdma_long_frame_rx,
                              &config, &ops, NULL)) {
        s_tdma_long_frame_ready = true;
        if (adapter != NULL) {
            tdma_pio_spi_ring_adapter_set_maintenance_service(
                adapter,
                tdma_ring_maintenance_service,
                &s_tdma_maintenance);
            tdma_pio_spi_ring_adapter_set_maintenance_mailbox(
                adapter,
                tdma_ring_maintenance_mailbox,
                NULL);
        }
    }
}

#define SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS 10000u
#define SCPI_TDMA_FIFO_RESET_WAIT_LOOPS 100u
#define SCPI_REFMEM_PACKAGE_PATH "/refmem/app_model.rmtp"
#define SCPI_REFMEM_PACKAGE_READ_CHUNK 512u
#define SCPI_REFMEM_SYNC_FRAME_MAX (REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX)
#define SCPI_REFMEM_SYNC_HEX_MAX ((SCPI_REFMEM_SYNC_FRAME_MAX * 2u) + 1u)
#define SCPI_REFMEM_SYNC_DEFAULT_EPOCH 1u
#define SCPI_REFMEM_SYNC_DEFAULT_RUN 1u
#define SCPI_REFMEM_SYNC_DEFAULT_MAX_PAYLOAD REFMEM_SYNC_FRAME_PAYLOAD_MAX
#define SCPI_REFMEM_SYNC_DEFAULT_MTU SCPI_REFMEM_SYNC_FRAME_MAX
#define SCPI_REFMEM_SYNC_DEFAULT_LATENCY_US 50u

scpi_refmem_sync_state_t s_refmem_sync;

bool scpi_refmem_get_adapter_snapshot(refmem_pio_spi_adapter_snapshot_t *snapshot)
{
    return refmem_pio_spi_adapter_get_snapshot(&s_refmem_sync.adapter, snapshot);
}

refmem_sync_context_t *scpi_refmem_sync_context(void)
{
    return &s_refmem_sync.context;
}

bool scpi_refmem_wait_storage_job(uint32_t job_id);
bool scpi_refmem_read_package(const char *path,
                                     uint8_t *buffer,
                                     size_t buffer_size,
                                     size_t *returned_size);
bool scpi_refmem_model_mode_idle(void);
bool scpi_refmem_realtime_idle(void);
void scpi_refmem_result_load_snapshot(scpi_t *context,
                                             const refmem_application_model_load_snapshot_t *snapshot);
void scpi_refmem_result_board_load_snapshot(
    scpi_t *context,
    const refmem_board_capability_load_snapshot_t *snapshot);
void scpi_refmem_result_table_image_descriptor(
    scpi_t *context,
    const refmem_table_image_descriptor_t *descriptor);
uint32_t scpi_refmem_read_u32_le(const uint8_t *data);
bool scpi_refmem_sync_ensure_initialized(void);
void scpi_refmem_sync_apply_node_load_delta(const refmem_sync_rx_snapshot_t *rx);
uint32_t scpi_refmem_sync_build_id_crc32(void);
bool scpi_refmem_sync_build_hello_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size);
bool scpi_refmem_sync_build_epoch_frame(uint8_t source_slot,
                                               uint8_t target_mask,
                                               uint32_t seq32,
                                               uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size);
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
                                               size_t *frame_size);
bool scpi_refmem_sync_build_ack_frame(uint8_t source_slot,
                                             uint8_t target_mask,
                                             uint32_t seq32,
                                             const refmem_sync_rx_snapshot_t *rx,
                                             uint8_t *frame,
                                             size_t frame_capacity,
                                             size_t *frame_size);
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
                                               size_t *frame_size);
bool scpi_refmem_sync_build_quality_frame(uint8_t source_slot,
                                                 uint8_t target_mask,
                                                 uint32_t seq32,
                                                 uint32_t quality_id,
                                                 uint32_t scope,
                                                 uint32_t target_slot,
                                                 uint8_t *frame,
                                                 size_t frame_capacity,
                                                 size_t *frame_size);
void scpi_refmem_sync_result_rx_snapshot(scpi_t *context,
                                                const refmem_sync_rx_snapshot_t *snapshot);
void scpi_refmem_sync_hex_encode(const uint8_t *data,
                                        size_t data_size,
                                        char *hex,
                                        size_t hex_size);
bool scpi_refmem_sync_hex_decode(const char *hex,
                                        size_t hex_len,
                                        uint8_t *output,
                                        size_t output_size,
                                        size_t *decoded_size);
const scpi_command_reason_entry_t *
scpi_command_find_reason(uint32_t reason_id);

const scpi_command_reason_entry_t s_command_reason_table[] = {
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
    {REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE, 2u, 0u, 1u, 14u, "NOT_ONLINE_SAFE"},
};

const scpi_command_reason_entry_t *
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
        !refmem_sync_init(scpi_refmem_sync_context(), (uint8_t)local_slot, epoch_id, run_id) ||
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
    uint32_t deadline_us = 1000u;
    (void)SCPI_ParamUInt32(context, &source_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &seq32, FALSE);
    (void)SCPI_ParamUInt32(context, &fence_seq, FALSE);
    (void)SCPI_ParamUInt32(context, &fence_scope, FALSE);
    (void)SCPI_ParamUInt32(context, &required_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &min_table_seq, FALSE);
    (void)SCPI_ParamUInt32(context, &deadline_us, FALSE);
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
                                            deadline_us,
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

        (void)refmem_sync_receive_frame(scpi_refmem_sync_context(),
                                        staged_frame,
                                        staged_size,
                                        &s_refmem_sync.last_rx);
    } else {
        (void)refmem_sync_receive_frame(scpi_refmem_sync_context(),
                                        frame,
                                        frame_size,
                                        &s_refmem_sync.last_rx);
    }
    scpi_refmem_sync_apply_node_load_delta(&s_refmem_sync.last_rx);
    SCPI_ResultText(context, s_refmem_sync.last_rx.accepted != 0u ? "ACCEPTED" : "REJECTED");
    scpi_refmem_sync_result_rx_snapshot(context, &s_refmem_sync.last_rx);
    return SCPI_RES_OK;
}

#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
scpi_result_t scpi_cmd_refmem_sync_frame_tx(scpi_t *context)
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
                                     &frame_size) ||
        !refmem_spi_physical_adapter_transmit(
            refmem_bootstrap_transport_adapter(), frame, frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_FRAME_TX");
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, (uint32_t)frame_size);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_frame_rx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t timeout_ms = 1000u;
    (void)SCPI_ParamUInt32(context, &timeout_ms, FALSE);
    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!refmem_spi_physical_adapter_receive(
            refmem_bootstrap_transport_adapter(), frame, sizeof(frame),
            &frame_size, timeout_ms)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_FRAME_RX");
        return SCPI_RES_ERR;
    }

    (void)refmem_sync_receive_frame(scpi_refmem_sync_context(),
                                    frame,
                                    frame_size,
                                    &s_refmem_sync.last_rx);
    SCPI_ResultText(context, s_refmem_sync.last_rx.accepted != 0u ? "ACCEPTED" : "REJECTED");
    scpi_refmem_sync_result_rx_snapshot(context, &s_refmem_sync.last_rx);
    return SCPI_RES_OK;
}
#else
scpi_result_t scpi_cmd_refmem_sync_frame_tx(scpi_t *context)
{
    (void)context;
    return SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_refmem_sync_frame_rx(scpi_t *context)
{
    (void)context;
    return SCPI_RES_ERR;
}
#endif

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
        refmem_sync_get_mirror(scpi_refmem_sync_context(), (uint8_t)source_slot);
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
        refmem_sync_get_ack(scpi_refmem_sync_context(), (uint8_t)source_slot);
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
        refmem_sync_get_fence(scpi_refmem_sync_context(), (uint8_t)source_slot);
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
        refmem_sync_get_remote_quality(scpi_refmem_sync_context(), (uint8_t)source_slot);
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
    SCPI_ResultUInt32(context, quality->p99_us);
    SCPI_ResultUInt32(context, quality->p999_us);
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
        refmem_sync_get_peer(scpi_refmem_sync_context(), (uint8_t)source_slot);
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
    refmem_sync_get_quality(scpi_refmem_sync_context(), &quality);
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


