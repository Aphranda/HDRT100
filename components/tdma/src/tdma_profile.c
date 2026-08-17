#include "tdma_profile.h"

#include <string.h>

#define TDMA_PROFILE_CRC_OFFSET 2166136261u
#define TDMA_PROFILE_CRC_PRIME 16777619u
#define TDMA_PROFILE_DEFAULT_SHORT_CAPACITY 292u
#define TDMA_PROFILE_DEFAULT_LONG_CAPACITY 1024u

static uint32_t tdma_profile_hash_u32(uint32_t hash, uint32_t value)
{
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= (value >> shift) & 0xFFu;
        hash *= TDMA_PROFILE_CRC_PRIME;
    }
    return hash;
}

static uint32_t tdma_profile_previous_slot(uint32_t slot, uint32_t node_count)
{
    return slot == 0u ? node_count - 1u : slot - 1u;
}

static uint32_t tdma_profile_next_slot(uint32_t slot, uint32_t node_count)
{
    return (slot + 1u) % node_count;
}

static void tdma_profile_set_result(tdma_profile_result_t *result,
                                    tdma_profile_result_t value)
{
    if (result != NULL) {
        *result = value;
    }
}

static void tdma_profile_set_traffic(tdma_traffic_class_profile_t *traffic,
                                     uint32_t class_id,
                                     uint32_t payload_mask,
                                     uint32_t reserved_bytes_per_cycle,
                                     uint32_t max_frames_per_cycle,
                                     uint32_t queue_depth,
                                     uint32_t deadline_ns,
                                     uint32_t flags,
                                     uint32_t overflow_policy)
{
    traffic->class_id = class_id;
    traffic->payload_mask = payload_mask;
    traffic->reserved_bytes_per_cycle = reserved_bytes_per_cycle;
    traffic->max_frames_per_cycle = max_frames_per_cycle;
    traffic->queue_depth = queue_depth;
    traffic->deadline_ns = deadline_ns;
    traffic->flags = flags;
    traffic->overflow_policy = overflow_policy;
}

uint32_t tdma_ring_profile_crc32(const tdma_ring_profile_t *profile)
{
    uint32_t hash = TDMA_PROFILE_CRC_OFFSET;
    if (profile == NULL) {
        return 0u;
    }

    hash = tdma_profile_hash_u32(hash, profile->version);
    hash = tdma_profile_hash_u32(hash, profile->flags);
    hash = tdma_profile_hash_u32(hash, profile->node_count);
    hash = tdma_profile_hash_u32(hash, profile->local_index);
    hash = tdma_profile_hash_u32(hash, profile->reference_index);
    hash = tdma_profile_hash_u32(hash, profile->up_group_id);
    hash = tdma_profile_hash_u32(hash, profile->down_group_id);
    hash = tdma_profile_hash_u32(hash, profile->upstream_slot_id);
    hash = tdma_profile_hash_u32(hash, profile->downstream_slot_id);
    hash = tdma_profile_hash_u32(hash, profile->feedback_slot_id);
    return hash;
}

bool tdma_ring_profile_default(tdma_ring_profile_t *profile,
                               uint32_t local_slot_id,
                               uint32_t reference_slot_id,
                               uint32_t node_count)
{
    if (profile == NULL || node_count < 2u || node_count > TDMA_RING_NODE_MAX ||
        local_slot_id >= node_count || reference_slot_id >= node_count) {
        return false;
    }

    memset(profile, 0, sizeof(*profile));
    profile->version = TDMA_RING_PROFILE_VERSION;
    profile->flags = TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN;
    profile->node_count = node_count;
    profile->local_index = local_slot_id;
    profile->reference_index = reference_slot_id;
    profile->up_group_id = 1u;
    profile->down_group_id = 2u;
    profile->upstream_slot_id = tdma_profile_previous_slot(local_slot_id, node_count);
    profile->downstream_slot_id = tdma_profile_next_slot(local_slot_id, node_count);
    profile->feedback_slot_id = reference_slot_id;
    profile->profile_crc32 = tdma_ring_profile_crc32(profile);
    return true;
}

bool tdma_ring_profile_validate(const tdma_ring_profile_t *profile,
                                tdma_profile_result_t *result)
{
    tdma_profile_set_result(result, TDMA_PROFILE_BAD_ARGUMENT);
    if (profile == NULL) {
        return false;
    }
    if (profile->version != TDMA_RING_PROFILE_VERSION) {
        tdma_profile_set_result(result, TDMA_PROFILE_BAD_VERSION);
        return false;
    }
    if (profile->node_count < 2u || profile->node_count > TDMA_RING_NODE_MAX ||
        profile->local_index >= profile->node_count ||
        profile->reference_index >= profile->node_count ||
        profile->upstream_slot_id !=
            tdma_profile_previous_slot(profile->local_index, profile->node_count) ||
        profile->downstream_slot_id !=
            tdma_profile_next_slot(profile->local_index, profile->node_count) ||
        profile->feedback_slot_id != profile->reference_index ||
        profile->upstream_slot_id == profile->local_index ||
        profile->downstream_slot_id == profile->local_index) {
        tdma_profile_set_result(result, TDMA_PROFILE_BAD_TOPOLOGY);
        return false;
    }
    if ((profile->flags & TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN) == 0u ||
        profile->up_group_id == 0u || profile->down_group_id == 0u ||
        profile->up_group_id == profile->down_group_id) {
        tdma_profile_set_result(result, TDMA_PROFILE_DIRECTION_CONFLICT);
        return false;
    }
    if (profile->profile_crc32 != tdma_ring_profile_crc32(profile)) {
        tdma_profile_set_result(result, TDMA_PROFILE_CRC_MISMATCH);
        return false;
    }

    tdma_profile_set_result(result, TDMA_PROFILE_OK);
    return true;
}

uint32_t tdma_foundation_profile_crc32(const tdma_foundation_profile_t *profile)
{
    uint32_t hash = TDMA_PROFILE_CRC_OFFSET;
    if (profile == NULL) {
        return 0u;
    }

    hash = tdma_profile_hash_u32(hash, profile->version);
    hash = tdma_profile_hash_u32(hash, profile->enabled);
    hash = tdma_profile_hash_u32(hash, profile->owner_instance_id);
    hash = tdma_profile_hash_u32(hash, profile->ring.profile_crc32);
    hash = tdma_profile_hash_u32(hash, profile->resource.adapter_type);
    hash = tdma_profile_hash_u32(hash, profile->resource.pio_block_id);
    hash = tdma_profile_hash_u32(hash, profile->resource.up_state_machine_id);
    hash = tdma_profile_hash_u32(hash, profile->resource.down_state_machine_id);
    hash = tdma_profile_hash_u32(hash, profile->resource.tx_dma_channel_id);
    hash = tdma_profile_hash_u32(hash, profile->resource.rx_dma_channel_id);
    hash = tdma_profile_hash_u32(hash, profile->resource.core1_service_id);
    hash = tdma_profile_hash_u32(hash, profile->resource.io_claim_mask);
    hash = tdma_profile_hash_u32(hash, profile->resource.ip_core_claim_mask);
    hash = tdma_profile_hash_u32(hash, profile->resource.short_frame_capacity);
    hash = tdma_profile_hash_u32(hash, profile->resource.long_frame_capacity);
    hash = tdma_profile_hash_u32(hash, profile->resource.payload_whitelist_mask);
    for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
        const tdma_traffic_class_profile_t *traffic = &profile->resource.traffic[i];
        hash = tdma_profile_hash_u32(hash, traffic->class_id);
        hash = tdma_profile_hash_u32(hash, traffic->payload_mask);
        hash = tdma_profile_hash_u32(hash, traffic->reserved_bytes_per_cycle);
        hash = tdma_profile_hash_u32(hash, traffic->max_frames_per_cycle);
        hash = tdma_profile_hash_u32(hash, traffic->queue_depth);
        hash = tdma_profile_hash_u32(hash, traffic->deadline_ns);
        hash = tdma_profile_hash_u32(hash, traffic->flags);
        hash = tdma_profile_hash_u32(hash, traffic->overflow_policy);
    }
    return hash;
}

bool tdma_foundation_profile_default(tdma_foundation_profile_t *profile,
                                     uint32_t owner_instance_id,
                                     uint32_t local_slot_id,
                                     uint32_t reference_slot_id,
                                     uint32_t adapter_type)
{
    if (profile == NULL || adapter_type == TDMA_ADAPTER_NONE ||
        adapter_type > TDMA_ADAPTER_RS485) {
        return false;
    }

    memset(profile, 0, sizeof(*profile));
    profile->version = TDMA_FOUNDATION_PROFILE_VERSION;
    profile->enabled = 1u;
    profile->owner_instance_id = owner_instance_id;
    if (!tdma_ring_profile_default(&profile->ring,
                                   local_slot_id,
                                   reference_slot_id,
                                   TDMA_RING_NODE_MAX)) {
        return false;
    }
    profile->resource.adapter_type = adapter_type;
    if (adapter_type == TDMA_ADAPTER_PIO_SPI ||
        adapter_type == TDMA_ADAPTER_BISS_C) {
        profile->resource.pio_block_id = 0u;
        profile->resource.up_state_machine_id = 0u;
        profile->resource.down_state_machine_id = 1u;
    } else {
        profile->resource.pio_block_id = TDMA_RESOURCE_ID_UNUSED;
        profile->resource.up_state_machine_id = TDMA_RESOURCE_ID_UNUSED;
        profile->resource.down_state_machine_id = TDMA_RESOURCE_ID_UNUSED;
    }
    profile->resource.tx_dma_channel_id = 0u;
    profile->resource.rx_dma_channel_id = 1u;
    profile->resource.core1_service_id = 1u;
    profile->resource.short_frame_capacity = TDMA_PROFILE_DEFAULT_SHORT_CAPACITY;
    profile->resource.long_frame_capacity = TDMA_PROFILE_DEFAULT_LONG_CAPACITY;
    profile->resource.payload_whitelist_mask = TDMA_PAYLOAD_FOUNDATION_DEFAULT_MASK;
    tdma_profile_set_traffic(
        &profile->resource.traffic[TDMA_TRAFFIC_VDC_REALTIME],
        TDMA_TRAFFIC_VDC_REALTIME,
        TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE) |
            TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_IDLE_BEACON),
        128u, 2u, 4u, 10000u,
        TDMA_TRAFFIC_FLAG_TIME_AWARE_GATE | TDMA_TRAFFIC_FLAG_STRICT_RESERVED,
        TDMA_OVERFLOW_FAULT);
    tdma_profile_set_traffic(
        &profile->resource.traffic[TDMA_TRAFFIC_REFMEM_REALTIME],
        TDMA_TRAFFIC_REFMEM_REALTIME,
        TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_REFMEM_DELTA) |
            TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_REFMEM_ACK_FENCE),
        584u, 2u, 8u, 1000000u,
        TDMA_TRAFFIC_FLAG_TIME_AWARE_GATE | TDMA_TRAFFIC_FLAG_STRICT_RESERVED |
            TDMA_TRAFFIC_FLAG_RELIABLE,
        TDMA_OVERFLOW_BACKPRESSURE);
    tdma_profile_set_traffic(
        &profile->resource.traffic[TDMA_TRAFFIC_CONFIG_CONTROL],
        TDMA_TRAFFIC_CONFIG_CONTROL,
        TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_CONFIG_CONTROL),
        128u, 1u, 4u, 100000000u,
        TDMA_TRAFFIC_FLAG_RELIABLE | TDMA_TRAFFIC_FLAG_SHAPED |
            TDMA_TRAFFIC_FLAG_PREEMPTIBLE,
        TDMA_OVERFLOW_BACKPRESSURE);
    tdma_profile_set_traffic(
        &profile->resource.traffic[TDMA_TRAFFIC_OTA_BULK],
        TDMA_TRAFFIC_OTA_BULK,
        TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_OTA_BULK),
        0u, 1u, 4u, 0u,
        TDMA_TRAFFIC_FLAG_RELIABLE | TDMA_TRAFFIC_FLAG_SHAPED |
            TDMA_TRAFFIC_FLAG_PREEMPTIBLE,
        TDMA_OVERFLOW_BACKPRESSURE);
    tdma_profile_set_traffic(
        &profile->resource.traffic[TDMA_TRAFFIC_LOG_BEST_EFFORT],
        TDMA_TRAFFIC_LOG_BEST_EFFORT,
        TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_LOG_STREAM),
        0u, 1u, 8u, 0u,
        TDMA_TRAFFIC_FLAG_SHAPED | TDMA_TRAFFIC_FLAG_PREEMPTIBLE,
        TDMA_OVERFLOW_DROP_OLDEST);
    profile->profile_crc32 = tdma_foundation_profile_crc32(profile);
    return true;
}

bool tdma_foundation_profile_validate(const tdma_foundation_profile_t *profile,
                                      tdma_profile_result_t *result)
{
    tdma_profile_set_result(result, TDMA_PROFILE_BAD_ARGUMENT);
    if (profile == NULL || profile->enabled == 0u) {
        return false;
    }
    if (profile->version != TDMA_FOUNDATION_PROFILE_VERSION) {
        tdma_profile_set_result(result, TDMA_PROFILE_BAD_VERSION);
        return false;
    }
    if (!tdma_ring_profile_validate(&profile->ring, result)) {
        return false;
    }
    if (profile->resource.adapter_type == TDMA_ADAPTER_NONE ||
        profile->resource.adapter_type > TDMA_ADAPTER_RS485) {
        tdma_profile_set_result(result, TDMA_PROFILE_ADAPTER_MISSING);
        return false;
    }
    if (((profile->resource.adapter_type == TDMA_ADAPTER_PIO_SPI ||
          profile->resource.adapter_type == TDMA_ADAPTER_BISS_C) &&
         (profile->resource.pio_block_id == TDMA_RESOURCE_ID_UNUSED ||
          profile->resource.up_state_machine_id == TDMA_RESOURCE_ID_UNUSED ||
          profile->resource.down_state_machine_id == TDMA_RESOURCE_ID_UNUSED ||
          profile->resource.up_state_machine_id ==
              profile->resource.down_state_machine_id)) ||
        profile->resource.tx_dma_channel_id ==
            profile->resource.rx_dma_channel_id) {
        tdma_profile_set_result(result, TDMA_PROFILE_RESOURCE_CONFLICT);
        return false;
    }
    if (profile->resource.short_frame_capacity == 0u ||
        profile->resource.long_frame_capacity <
            profile->resource.short_frame_capacity) {
        tdma_profile_set_result(result, TDMA_PROFILE_CAPACITY_INVALID);
        return false;
    }
    if ((profile->resource.payload_whitelist_mask &
         TDMA_PAYLOAD_FOUNDATION_REQUIRED_MASK) !=
        TDMA_PAYLOAD_FOUNDATION_REQUIRED_MASK) {
        tdma_profile_set_result(result, TDMA_PROFILE_PAYLOAD_MISSING);
        return false;
    }
    uint32_t classified_payload_mask = 0u;
    for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
        const tdma_traffic_class_profile_t *traffic = &profile->resource.traffic[i];
        if (traffic->class_id != i || traffic->payload_mask == 0u ||
            traffic->max_frames_per_cycle == 0u || traffic->queue_depth == 0u ||
            traffic->overflow_policy > TDMA_OVERFLOW_DROP_NEWEST ||
            (classified_payload_mask & traffic->payload_mask) != 0u ||
            (uint64_t)traffic->reserved_bytes_per_cycle >
                (uint64_t)profile->resource.long_frame_capacity *
                    (uint64_t)traffic->max_frames_per_cycle) {
            tdma_profile_set_result(result, TDMA_PROFILE_CAPACITY_INVALID);
            return false;
        }
        classified_payload_mask |= traffic->payload_mask;
    }
    if (classified_payload_mask != profile->resource.payload_whitelist_mask ||
        (profile->resource.traffic[TDMA_TRAFFIC_VDC_REALTIME].flags &
         (TDMA_TRAFFIC_FLAG_TIME_AWARE_GATE | TDMA_TRAFFIC_FLAG_STRICT_RESERVED)) !=
            (TDMA_TRAFFIC_FLAG_TIME_AWARE_GATE | TDMA_TRAFFIC_FLAG_STRICT_RESERVED) ||
        (profile->resource.traffic[TDMA_TRAFFIC_REFMEM_REALTIME].flags &
         (TDMA_TRAFFIC_FLAG_TIME_AWARE_GATE | TDMA_TRAFFIC_FLAG_STRICT_RESERVED)) !=
            (TDMA_TRAFFIC_FLAG_TIME_AWARE_GATE | TDMA_TRAFFIC_FLAG_STRICT_RESERVED)) {
        tdma_profile_set_result(result, TDMA_PROFILE_PAYLOAD_MISSING);
        return false;
    }
    if (profile->profile_crc32 != tdma_foundation_profile_crc32(profile)) {
        tdma_profile_set_result(result, TDMA_PROFILE_CRC_MISMATCH);
        return false;
    }

    tdma_profile_set_result(result, TDMA_PROFILE_OK);
    return true;
}
