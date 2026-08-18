#ifndef REFMEM_SYNC_H
#define REFMEM_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_sync_frame.h"

#define REFMEM_SYNC_NODE_COUNT 8u

typedef enum {
    REFMEM_SYNC_RX_ACCEPTED = 0u,
    REFMEM_SYNC_RX_BAD_ARGUMENT = 1u,
    REFMEM_SYNC_RX_FRAME_INVALID = 2u,
    REFMEM_SYNC_RX_SOURCE_SLOT_INVALID = 3u,
    REFMEM_SYNC_RX_TARGET_MISMATCH = 4u,
    REFMEM_SYNC_RX_EPOCH_MISMATCH = 5u,
    REFMEM_SYNC_RX_DUPLICATE_SEQ = 6u,
    REFMEM_SYNC_RX_STALE_SEQ = 7u,
} refmem_sync_rx_result_t;

typedef struct {
    uint32_t seen;
    uint32_t hello_seen;
    uint32_t epoch_seen;
    uint32_t frame_count;
    uint32_t duplicate_count;
    uint32_t stale_count;
    uint32_t drop_count;
    uint32_t last_seq32;
    uint32_t expected_seq32;
    uint32_t last_frame_type;
    uint32_t last_compact_time;
    uint32_t last_payload_crc32;
} refmem_sync_peer_state_t;

typedef struct {
    uint32_t frame_rx_count;
    uint32_t accepted_count;
    uint32_t bad_frame_count;
    uint32_t header_error_count;
    uint32_t crc_error_count;
    uint32_t source_error_count;
    uint32_t target_mismatch_count;
    uint32_t epoch_mismatch_count;
    uint32_t duplicate_count;
    uint32_t stale_count;
    uint32_t drop_count;
} refmem_sync_quality_counters_t;

typedef struct {
    uint32_t visible;
    uint32_t source_slot;
    uint32_t slot_id;
    uint32_t payload_kind;
    uint32_t slot_seq;
    uint32_t field_id;
    uint32_t field_offset;
    uint32_t field_width;
    uint32_t dirty_mask;
    uint32_t value_u32;
    uint32_t value_crc32;
    uint32_t last_frame_seq32;
    uint32_t committed_count;
    uint32_t visible_count;
} refmem_sync_mirror_snapshot_t;

typedef struct {
    uint32_t seen;
    uint32_t source_slot;
    uint32_t command_seq;
    uint32_t delta_seq32;
    uint32_t taken_flags;
    uint32_t ack_flags;
    uint32_t nack_flags;
    uint32_t busy_flags;
    uint32_t timeout_flags;
    uint32_t last_reason;
    uint32_t last_reason_slot;
    uint32_t evidence_index;
    uint32_t last_frame_seq32;
    uint32_t received_count;
} refmem_sync_ack_snapshot_t;

typedef struct {
    uint32_t seen;
    uint32_t source_slot;
    uint32_t fence_seq;
    uint32_t fence_scope;
    uint32_t required_mask;
    uint32_t min_table_seq;
    uint32_t required_visible_mask;
    uint32_t missing_mask;
    uint32_t passed;
    uint32_t timed_out;
    uint32_t last_reason;
    uint32_t evidence_index;
    uint32_t last_frame_seq32;
    uint32_t received_count;
} refmem_sync_fence_snapshot_t;

typedef struct {
    uint32_t seen;
    uint32_t source_slot;
    uint32_t quality_id;
    uint32_t scope;
    uint32_t target_slot;
    uint32_t seq_expected;
    uint32_t seq_last;
    uint32_t crc_error_count;
    uint32_t stale_count;
    uint32_t drop_count;
    uint32_t late_count;
    uint32_t timeout_count;
    uint32_t last_error;
    uint32_t p99_us;
    uint32_t p999_us;
    uint32_t evidence_index;
    uint32_t last_frame_seq32;
    uint32_t received_count;
} refmem_sync_remote_quality_snapshot_t;

typedef struct {
    uint8_t local_slot;
    uint32_t active_epoch_id;
    uint32_t active_run_id;
    refmem_sync_peer_state_t peer[REFMEM_SYNC_NODE_COUNT];
    refmem_sync_mirror_snapshot_t mirror[REFMEM_SYNC_NODE_COUNT];
    refmem_sync_ack_snapshot_t ack[REFMEM_SYNC_NODE_COUNT];
    refmem_sync_fence_snapshot_t fence[REFMEM_SYNC_NODE_COUNT];
    refmem_sync_remote_quality_snapshot_t remote_quality[REFMEM_SYNC_NODE_COUNT];
    refmem_sync_quality_counters_t quality;
} refmem_sync_context_t;

typedef struct {
    refmem_sync_rx_result_t result;
    refmem_sync_frame_result_t frame_result;
    refmem_sync_frame_header_t header;
    const uint8_t *payload;
    uint16_t payload_size;
    uint32_t source_slot;
    uint32_t accepted;
} refmem_sync_rx_snapshot_t;

bool refmem_sync_init(refmem_sync_context_t *context,
                      uint8_t local_slot,
                      uint32_t active_epoch_id,
                      uint32_t active_run_id);
bool refmem_sync_set_epoch(refmem_sync_context_t *context,
                           uint32_t active_epoch_id,
                           uint32_t active_run_id);
refmem_sync_rx_result_t refmem_sync_receive_frame(refmem_sync_context_t *context,
                                                  const uint8_t *frame,
                                                  size_t frame_size,
                                                  refmem_sync_rx_snapshot_t *snapshot);
const refmem_sync_peer_state_t *refmem_sync_get_peer(
    const refmem_sync_context_t *context,
    uint8_t source_slot);
const refmem_sync_mirror_snapshot_t *refmem_sync_get_mirror(
    const refmem_sync_context_t *context,
    uint8_t source_slot);
const refmem_sync_ack_snapshot_t *refmem_sync_get_ack(
    const refmem_sync_context_t *context,
    uint8_t source_slot);
const refmem_sync_fence_snapshot_t *refmem_sync_get_fence(
    const refmem_sync_context_t *context,
    uint8_t source_slot);
const refmem_sync_remote_quality_snapshot_t *refmem_sync_get_remote_quality(
    const refmem_sync_context_t *context,
    uint8_t source_slot);
void refmem_sync_get_quality(const refmem_sync_context_t *context,
                             refmem_sync_quality_counters_t *quality);

#endif
