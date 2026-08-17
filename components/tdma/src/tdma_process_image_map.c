#include "tdma_process_image_map.h"

#include <string.h>

#define TDMA_PROCESS_IMAGE_HASH_OFFSET 2166136261u
#define TDMA_PROCESS_IMAGE_HASH_PRIME 16777619u

static void tdma_process_image_set_result(
    tdma_process_image_map_result_t *result,
    tdma_process_image_map_result_t value)
{
    if (result != NULL) {
        *result = value;
    }
}

static uint32_t tdma_process_image_hash_u32(uint32_t hash, uint32_t value)
{
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= (value >> shift) & 0xFFu;
        hash *= TDMA_PROCESS_IMAGE_HASH_PRIME;
    }
    return hash;
}

static bool tdma_process_image_segment_empty(
    const tdma_process_image_segment_t *segment)
{
    static const tdma_process_image_segment_t empty;
    return segment != NULL &&
           memcmp(segment, &empty, sizeof(empty)) == 0;
}

static bool tdma_process_image_segment_valid(
    const tdma_process_image_map_t *map,
    const tdma_process_image_segment_t *segment)
{
    if (map == NULL || segment == NULL || segment->used == 0u) {
        return false;
    }
    return segment->used == 1u &&
           segment->segment_id < TDMA_PROCESS_IMAGE_SEGMENT_COUNT &&
           segment->owner_slot_id < TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT &&
           segment->payload_class != 0u &&
           segment->payload_class <= UINT8_MAX &&
           segment->byte_length != 0u &&
           segment->byte_offset < map->payload_size &&
           segment->byte_length <= map->payload_size - segment->byte_offset &&
           (segment->flags & ~TDMA_PROCESS_SEGMENT_FLAG_MASK) == 0u &&
           !((segment->flags & TDMA_PROCESS_SEGMENT_FLAG_COALESCE_LATEST) != 0u &&
             (segment->flags & TDMA_PROCESS_SEGMENT_FLAG_COMMAND_QUEUE) != 0u) &&
           segment->reserved == 0u;
}

static bool tdma_process_image_segments_overlap(
    const tdma_process_image_segment_t *left,
    const tdma_process_image_segment_t *right)
{
    const uint32_t left_end = left->byte_offset + left->byte_length;
    const uint32_t right_end = right->byte_offset + right->byte_length;
    return left->byte_offset < right_end && right->byte_offset < left_end;
}

uint32_t tdma_process_image_map_crc32(const tdma_process_image_map_t *map)
{
    if (map == NULL) {
        return 0u;
    }
    uint32_t hash = TDMA_PROCESS_IMAGE_HASH_OFFSET;
    hash = tdma_process_image_hash_u32(hash, map->version);
    hash = tdma_process_image_hash_u32(hash, map->payload_size);
    hash = tdma_process_image_hash_u32(hash, map->segment_count);
    hash = tdma_process_image_hash_u32(hash, map->flags);
    for (uint32_t i = 0u; i < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; i++) {
        const tdma_process_image_segment_t *segment = &map->segment[i];
        hash = tdma_process_image_hash_u32(hash, segment->used);
        hash = tdma_process_image_hash_u32(hash, segment->segment_id);
        hash = tdma_process_image_hash_u32(hash, segment->owner_slot_id);
        hash = tdma_process_image_hash_u32(hash, segment->payload_class);
        hash = tdma_process_image_hash_u32(hash, segment->byte_offset);
        hash = tdma_process_image_hash_u32(hash, segment->byte_length);
        hash = tdma_process_image_hash_u32(hash, segment->flags);
        hash = tdma_process_image_hash_u32(hash, segment->reserved);
    }
    return hash == 0u ? 1u : hash;
}

bool tdma_process_image_map_validate(
    const tdma_process_image_map_t *map,
    tdma_process_image_map_result_t *result)
{
    tdma_process_image_set_result(result,
                                  TDMA_PROCESS_IMAGE_MAP_BAD_ARGUMENT);
    if (map == NULL) {
        return false;
    }
    if (map->version != TDMA_PROCESS_IMAGE_MAP_VERSION) {
        tdma_process_image_set_result(result,
                                      TDMA_PROCESS_IMAGE_MAP_BAD_VERSION);
        return false;
    }
    if (map->payload_size == 0u ||
        map->payload_size > TDMA_TRANSPORT_SHORT_PAYLOAD_MAX ||
        map->segment_count == 0u ||
        map->segment_count > TDMA_PROCESS_IMAGE_SEGMENT_COUNT ||
        map->flags != 0u) {
        tdma_process_image_set_result(result,
                                      TDMA_PROCESS_IMAGE_MAP_BAD_CAPACITY);
        return false;
    }

    uint32_t used_count = 0u;
    for (uint32_t i = 0u; i < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; i++) {
        const tdma_process_image_segment_t *left = &map->segment[i];
        if (left->used == 0u) {
            if (!tdma_process_image_segment_empty(left)) {
                tdma_process_image_set_result(
                    result,
                    TDMA_PROCESS_IMAGE_MAP_BAD_SEGMENT);
                return false;
            }
            continue;
        }
        if (!tdma_process_image_segment_valid(map, left)) {
            tdma_process_image_set_result(result,
                                          TDMA_PROCESS_IMAGE_MAP_BAD_SEGMENT);
            return false;
        }
        used_count++;
        for (uint32_t j = i + 1u;
             j < TDMA_PROCESS_IMAGE_SEGMENT_COUNT;
             j++) {
            const tdma_process_image_segment_t *right = &map->segment[j];
            if (right->used == 0u) {
                continue;
            }
            if (left->segment_id == right->segment_id) {
                tdma_process_image_set_result(
                    result,
                    TDMA_PROCESS_IMAGE_MAP_DUPLICATE_ID);
                return false;
            }
            if (tdma_process_image_segments_overlap(left, right)) {
                tdma_process_image_set_result(result,
                                              TDMA_PROCESS_IMAGE_MAP_OVERLAP);
                return false;
            }
        }
    }
    if (used_count != map->segment_count) {
        tdma_process_image_set_result(result,
                                      TDMA_PROCESS_IMAGE_MAP_BAD_SEGMENT);
        return false;
    }
    if (map->map_crc32 == 0u ||
        map->map_crc32 != tdma_process_image_map_crc32(map)) {
        tdma_process_image_set_result(result,
                                      TDMA_PROCESS_IMAGE_MAP_CRC_MISMATCH);
        return false;
    }

    tdma_process_image_set_result(result, TDMA_PROCESS_IMAGE_MAP_OK);
    return true;
}

bool tdma_process_image_map_find(
    const tdma_process_image_map_t *map,
    uint32_t segment_id,
    tdma_process_image_segment_t *segment,
    tdma_process_image_map_result_t *result)
{
    if (segment != NULL) {
        memset(segment, 0, sizeof(*segment));
    }
    if (map == NULL || segment == NULL ||
        !tdma_process_image_map_validate(map, result)) {
        return false;
    }
    for (uint32_t i = 0u; i < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; i++) {
        if (map->segment[i].used != 0u &&
            map->segment[i].segment_id == segment_id) {
            *segment = map->segment[i];
            tdma_process_image_set_result(result,
                                          TDMA_PROCESS_IMAGE_MAP_OK);
            return true;
        }
    }
    tdma_process_image_set_result(result,
                                  TDMA_PROCESS_IMAGE_MAP_NOT_FOUND);
    return false;
}

bool tdma_process_image_map_can_publish(
    const tdma_process_image_map_t *map,
    uint32_t local_slot_mask,
    uint32_t segment_id,
    tdma_process_image_segment_t *segment,
    tdma_process_image_map_result_t *result)
{
    if (!tdma_process_image_map_find(map, segment_id, segment, result)) {
        return false;
    }
    const uint32_t owner_bit = 1u << segment->owner_slot_id;
    if ((local_slot_mask & owner_bit) == 0u ||
        (segment->flags & TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE) == 0u) {
        memset(segment, 0, sizeof(*segment));
        tdma_process_image_set_result(result,
                                      TDMA_PROCESS_IMAGE_MAP_NOT_OWNER);
        return false;
    }
    tdma_process_image_set_result(result, TDMA_PROCESS_IMAGE_MAP_OK);
    return true;
}
