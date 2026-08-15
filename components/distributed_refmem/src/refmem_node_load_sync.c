#include "refmem_node_load_sync.h"

#include <string.h>

static void refmem_node_load_sync_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16u) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint32_t refmem_node_load_sync_get_u32(const uint8_t *src)
{
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8u) |
           ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

static bool refmem_node_load_sync_entry_valid(const refmem_node_load_entry_t *entry)
{
    return entry != NULL &&
           entry->node_id < REFMEM_APP_MODEL_NODE_COUNT &&
           entry->instance_id < REFMEM_APP_MODEL_INSTANCE_COUNT &&
           entry->fail_policy <= REFMEM_APP_FAIL_REPORT_ONLY;
}

static void refmem_node_load_sync_write_entry(uint8_t *dst,
                                              const refmem_node_load_entry_t *entry)
{
    refmem_node_load_sync_put_u32(&dst[0u], entry->load_id);
    refmem_node_load_sync_put_u32(&dst[4u], entry->application_id);
    refmem_node_load_sync_put_u32(&dst[8u], entry->profile_id);
    refmem_node_load_sync_put_u32(&dst[12u], entry->node_id);
    refmem_node_load_sync_put_u32(&dst[16u], entry->instance_id);
    refmem_node_load_sync_put_u32(&dst[20u], entry->role_mask);
    refmem_node_load_sync_put_u32(&dst[24u], entry->persona_mask);
    refmem_node_load_sync_put_u32(&dst[28u], entry->enabled);
    refmem_node_load_sync_put_u32(&dst[32u], entry->required);
    refmem_node_load_sync_put_u32(&dst[36u], entry->fail_policy);
    refmem_node_load_sync_put_u32(&dst[40u], entry->load_order);
}

static void refmem_node_load_sync_read_entry(const uint8_t *src,
                                             refmem_node_load_entry_t *entry)
{
    entry->load_id = refmem_node_load_sync_get_u32(&src[0u]);
    entry->application_id = refmem_node_load_sync_get_u32(&src[4u]);
    entry->profile_id = refmem_node_load_sync_get_u32(&src[8u]);
    entry->node_id = refmem_node_load_sync_get_u32(&src[12u]);
    entry->instance_id = refmem_node_load_sync_get_u32(&src[16u]);
    entry->role_mask = refmem_node_load_sync_get_u32(&src[20u]);
    entry->persona_mask = refmem_node_load_sync_get_u32(&src[24u]);
    entry->enabled = refmem_node_load_sync_get_u32(&src[28u]);
    entry->required = refmem_node_load_sync_get_u32(&src[32u]);
    entry->fail_policy = refmem_node_load_sync_get_u32(&src[36u]);
    entry->load_order = refmem_node_load_sync_get_u32(&src[40u]);
}

bool refmem_node_load_sync_build_delta_payload(
    const refmem_node_load_entry_t *entry,
    uint32_t slot_seq,
    uint16_t delta_id,
    uint8_t *payload,
    size_t payload_capacity,
    uint16_t *payload_size)
{
    const size_t entry_size = REFMEM_NODE_LOAD_SYNC_ENTRY_U32_COUNT * sizeof(uint32_t);
    const size_t total_size = sizeof(refmem_sync_delta_header_t) + entry_size;
    if (payload_size != NULL) {
        *payload_size = 0u;
    }
    if (!refmem_node_load_sync_entry_valid(entry) ||
        payload == NULL ||
        payload_size == NULL ||
        payload_capacity < total_size ||
        total_size > UINT16_MAX) {
        return false;
    }

    refmem_sync_delta_header_t delta;
    memset(&delta, 0, sizeof(delta));
    delta.delta_id = delta_id;
    delta.slot_id = (uint8_t)entry->node_id;
    delta.payload_kind = REFMEM_NODE_LOAD_SYNC_PAYLOAD_KIND_ENTRY;
    delta.slot_seq = slot_seq;
    delta.field_id = REFMEM_NODE_LOAD_SYNC_FIELD_ENTRY;
    delta.field_offset = 0u;
    delta.field_width = sizeof(uint32_t);
    delta.dirty_mask = REFMEM_NODE_LOAD_SYNC_DIRTY_ENTRY_ALL;

    memcpy(payload, &delta, sizeof(delta));
    refmem_node_load_sync_write_entry(&payload[sizeof(delta)], entry);
    *payload_size = (uint16_t)total_size;
    return true;
}

bool refmem_node_load_sync_decode_delta_payload(
    const uint8_t *payload,
    uint16_t payload_size,
    refmem_sync_delta_header_t *delta,
    refmem_node_load_entry_t *entry)
{
    const uint16_t entry_size =
        (uint16_t)(REFMEM_NODE_LOAD_SYNC_ENTRY_U32_COUNT * sizeof(uint32_t));
    const uint16_t expected_size =
        (uint16_t)(sizeof(refmem_sync_delta_header_t) + entry_size);
    refmem_sync_delta_header_t decoded_delta;
    refmem_node_load_entry_t decoded_entry;

    if (payload == NULL || entry == NULL || payload_size != expected_size) {
        return false;
    }

    memcpy(&decoded_delta, payload, sizeof(decoded_delta));
    if (decoded_delta.payload_kind != REFMEM_NODE_LOAD_SYNC_PAYLOAD_KIND_ENTRY ||
        decoded_delta.field_id != REFMEM_NODE_LOAD_SYNC_FIELD_ENTRY ||
        decoded_delta.field_width != sizeof(uint32_t) ||
        decoded_delta.dirty_mask != REFMEM_NODE_LOAD_SYNC_DIRTY_ENTRY_ALL) {
        return false;
    }

    refmem_node_load_sync_read_entry(&payload[sizeof(decoded_delta)],
                                     &decoded_entry);
    if (!refmem_node_load_sync_entry_valid(&decoded_entry) ||
        decoded_delta.slot_id != (uint8_t)decoded_entry.node_id) {
        return false;
    }

    if (delta != NULL) {
        *delta = decoded_delta;
    }
    *entry = decoded_entry;
    return true;
}

bool refmem_node_load_sync_build_delta_frame(
    const refmem_node_load_entry_t *entry,
    uint8_t source_slot,
    uint8_t target_mask,
    uint32_t epoch_id,
    uint32_t run_id,
    uint32_t seq32,
    uint32_t slot_seq,
    uint32_t compact_time,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size)
{
    uint8_t payload[sizeof(refmem_sync_delta_header_t) +
                    (REFMEM_NODE_LOAD_SYNC_ENTRY_U32_COUNT * sizeof(uint32_t))];
    uint16_t payload_size = 0u;
    refmem_sync_frame_header_t header;

    if (frame_size != NULL) {
        *frame_size = 0u;
    }
    if (frame == NULL || frame_size == NULL ||
        !refmem_node_load_sync_build_delta_payload(entry,
                                                   slot_seq,
                                                   (uint16_t)(seq32 & 0xFFFFu),
                                                   payload,
                                                   sizeof(payload),
                                                   &payload_size) ||
        !refmem_sync_frame_header_init(&header,
                                       REFMEM_SYNC_FRAME_DELTA,
                                       REFMEM_SYNC_FRAME_FLAG_ACK_REQUEST,
                                       source_slot,
                                       target_mask,
                                       epoch_id,
                                       run_id,
                                       seq32,
                                       0u,
                                       compact_time,
                                       payload,
                                       payload_size)) {
        return false;
    }

    return refmem_sync_frame_encode(&header,
                                    payload,
                                    payload_size,
                                    frame,
                                    frame_capacity,
                                    frame_size);
}
