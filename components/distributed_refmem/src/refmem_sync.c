#include "refmem_sync.h"

#include <string.h>

static bool refmem_sync_target_matches(uint8_t target_mask, uint8_t local_slot)
{
    if (local_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }
    return (target_mask & (uint8_t)(1u << local_slot)) != 0u;
}

static bool refmem_sync_requires_epoch_match(uint8_t frame_type)
{
    return frame_type != (uint8_t)REFMEM_SYNC_FRAME_HELLO;
}

static uint32_t refmem_sync_read_le_u32(const uint8_t *data, uint16_t size)
{
    uint32_t value = 0u;
    const uint16_t width = size > 4u ? 4u : size;
    if (data == NULL) {
        return 0u;
    }
    for (uint16_t i = 0u; i < width; i++) {
        value |= (uint32_t)data[i] << (8u * i);
    }
    return value;
}

static bool refmem_sync_commit_delta(refmem_sync_context_t *context,
                                     const refmem_sync_frame_header_t *header,
                                     const uint8_t *payload,
                                     uint16_t payload_size)
{
    if (context == NULL || header == NULL || payload == NULL ||
        header->source_slot >= REFMEM_SYNC_NODE_COUNT ||
        payload_size < sizeof(refmem_sync_delta_header_t)) {
        return false;
    }

    refmem_sync_delta_header_t delta;
    (void)memcpy(&delta, payload, sizeof(delta));
    if (delta.field_width > 4u ||
        payload_size < (uint16_t)(sizeof(delta) + delta.field_width)) {
        return false;
    }

    refmem_sync_mirror_snapshot_t *mirror = &context->mirror[header->source_slot];
    mirror->visible = 1u;
    mirror->source_slot = header->source_slot;
    mirror->slot_id = delta.slot_id;
    mirror->payload_kind = delta.payload_kind;
    mirror->slot_seq = delta.slot_seq;
    mirror->field_id = delta.field_id;
    mirror->field_offset = delta.field_offset;
    mirror->field_width = delta.field_width;
    mirror->dirty_mask = delta.dirty_mask;
    mirror->value_u32 = refmem_sync_read_le_u32(&payload[sizeof(delta)], delta.field_width);
    mirror->value_crc32 = header->payload_crc32;
    mirror->last_frame_seq32 = header->seq32;
    mirror->committed_count++;
    mirror->visible_count++;
    return true;
}

static void refmem_sync_fill_snapshot(refmem_sync_rx_snapshot_t *snapshot,
                                      refmem_sync_rx_result_t result,
                                      refmem_sync_frame_result_t frame_result,
                                      const refmem_sync_frame_header_t *header,
                                      const uint8_t *payload,
                                      uint16_t payload_size,
                                      bool accepted)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->result = result;
    snapshot->frame_result = frame_result;
    if (header != NULL) {
        snapshot->header = *header;
        snapshot->source_slot = header->source_slot;
    }
    snapshot->payload = payload;
    snapshot->payload_size = payload_size;
    snapshot->accepted = accepted ? 1u : 0u;
}

bool refmem_sync_init(refmem_sync_context_t *context,
                      uint8_t local_slot,
                      uint32_t active_epoch_id,
                      uint32_t active_run_id)
{
    if (context == NULL || local_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }

    memset(context, 0, sizeof(*context));
    context->local_slot = local_slot;
    context->active_epoch_id = active_epoch_id;
    context->active_run_id = active_run_id;
    return true;
}

bool refmem_sync_set_epoch(refmem_sync_context_t *context,
                           uint32_t active_epoch_id,
                           uint32_t active_run_id)
{
    if (context == NULL || context->local_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }

    context->active_epoch_id = active_epoch_id;
    context->active_run_id = active_run_id;
    return true;
}

refmem_sync_rx_result_t refmem_sync_receive_frame(refmem_sync_context_t *context,
                                                  const uint8_t *frame,
                                                  size_t frame_size,
                                                  refmem_sync_rx_snapshot_t *snapshot)
{
    refmem_sync_frame_header_t header;
    const uint8_t *payload = NULL;
    uint16_t payload_size = 0u;

    if (context == NULL || frame == NULL) {
        refmem_sync_fill_snapshot(snapshot,
                                  REFMEM_SYNC_RX_BAD_ARGUMENT,
                                  REFMEM_SYNC_FRAME_BAD_ARGUMENT,
                                  NULL,
                                  NULL,
                                  0u,
                                  false);
        return REFMEM_SYNC_RX_BAD_ARGUMENT;
    }

    context->quality.frame_rx_count++;

    const refmem_sync_frame_result_t frame_result =
        refmem_sync_frame_validate(frame, frame_size, &header, &payload, &payload_size);
    if (frame_result != REFMEM_SYNC_FRAME_OK) {
        context->quality.bad_frame_count++;
        if (frame_result == REFMEM_SYNC_FRAME_BAD_PAYLOAD_CRC) {
            context->quality.crc_error_count++;
        } else {
            context->quality.header_error_count++;
        }
        refmem_sync_fill_snapshot(snapshot,
                                  REFMEM_SYNC_RX_FRAME_INVALID,
                                  frame_result,
                                  NULL,
                                  NULL,
                                  0u,
                                  false);
        return REFMEM_SYNC_RX_FRAME_INVALID;
    }

    if (header.source_slot >= REFMEM_SYNC_NODE_COUNT) {
        context->quality.source_error_count++;
        refmem_sync_fill_snapshot(snapshot,
                                  REFMEM_SYNC_RX_SOURCE_SLOT_INVALID,
                                  frame_result,
                                  &header,
                                  payload,
                                  payload_size,
                                  false);
        return REFMEM_SYNC_RX_SOURCE_SLOT_INVALID;
    }

    if (!refmem_sync_target_matches(header.target_mask, context->local_slot)) {
        context->quality.target_mismatch_count++;
        refmem_sync_fill_snapshot(snapshot,
                                  REFMEM_SYNC_RX_TARGET_MISMATCH,
                                  frame_result,
                                  &header,
                                  payload,
                                  payload_size,
                                  false);
        return REFMEM_SYNC_RX_TARGET_MISMATCH;
    }

    if (refmem_sync_requires_epoch_match(header.frame_type) &&
        (header.epoch_id != context->active_epoch_id ||
         header.run_id != context->active_run_id)) {
        context->quality.epoch_mismatch_count++;
        refmem_sync_fill_snapshot(snapshot,
                                  REFMEM_SYNC_RX_EPOCH_MISMATCH,
                                  frame_result,
                                  &header,
                                  payload,
                                  payload_size,
                                  false);
        return REFMEM_SYNC_RX_EPOCH_MISMATCH;
    }

    refmem_sync_peer_state_t *peer = &context->peer[header.source_slot];
    if (peer->seen != 0u) {
        if (header.seq32 == peer->last_seq32) {
            peer->duplicate_count++;
            context->quality.duplicate_count++;
            refmem_sync_fill_snapshot(snapshot,
                                      REFMEM_SYNC_RX_DUPLICATE_SEQ,
                                      frame_result,
                                      &header,
                                      payload,
                                      payload_size,
                                      false);
            return REFMEM_SYNC_RX_DUPLICATE_SEQ;
        }
        if ((int32_t)(header.seq32 - peer->last_seq32) < 0) {
            peer->stale_count++;
            context->quality.stale_count++;
            refmem_sync_fill_snapshot(snapshot,
                                      REFMEM_SYNC_RX_STALE_SEQ,
                                      frame_result,
                                      &header,
                                      payload,
                                      payload_size,
                                      false);
            return REFMEM_SYNC_RX_STALE_SEQ;
        }
        if (header.seq32 > peer->expected_seq32) {
            const uint32_t dropped = header.seq32 - peer->expected_seq32;
            peer->drop_count += dropped;
            context->quality.drop_count += dropped;
        }
    }

    peer->seen = 1u;
    if (header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_HELLO) {
        peer->hello_seen = 1u;
    } else if (header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_EPOCH) {
        peer->epoch_seen = 1u;
    }
    peer->frame_count++;
    peer->last_seq32 = header.seq32;
    peer->expected_seq32 = header.seq32 + 1u;
    peer->last_frame_type = header.frame_type;
    peer->last_compact_time = header.compact_time;
    peer->last_payload_crc32 = header.payload_crc32;

    if (header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_DELTA) {
        (void)refmem_sync_commit_delta(context, &header, payload, payload_size);
    }

    context->quality.accepted_count++;
    refmem_sync_fill_snapshot(snapshot,
                              REFMEM_SYNC_RX_ACCEPTED,
                              frame_result,
                              &header,
                              payload,
                              payload_size,
                              true);
    return REFMEM_SYNC_RX_ACCEPTED;
}

const refmem_sync_peer_state_t *refmem_sync_get_peer(
    const refmem_sync_context_t *context,
    uint8_t source_slot)
{
    if (context == NULL || source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return NULL;
    }
    return &context->peer[source_slot];
}

const refmem_sync_mirror_snapshot_t *refmem_sync_get_mirror(
    const refmem_sync_context_t *context,
    uint8_t source_slot)
{
    if (context == NULL || source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return NULL;
    }
    return &context->mirror[source_slot];
}

void refmem_sync_get_quality(const refmem_sync_context_t *context,
                             refmem_sync_quality_counters_t *quality)
{
    if (quality == NULL) {
        return;
    }
    memset(quality, 0, sizeof(*quality));
    if (context != NULL) {
        *quality = context->quality;
    }
}
