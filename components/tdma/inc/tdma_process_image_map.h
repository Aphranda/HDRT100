#ifndef TDMA_PROCESS_IMAGE_MAP_H
#define TDMA_PROCESS_IMAGE_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_transport_frame.h"

#define TDMA_PROCESS_IMAGE_MAP_VERSION 1u
#define TDMA_PROCESS_IMAGE_SEGMENT_COUNT 16u

#define TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE 0x00000001u
#define TDMA_PROCESS_SEGMENT_FLAG_ACK_REQUIRED 0x00000002u
#define TDMA_PROCESS_SEGMENT_FLAG_COALESCE_LATEST 0x00000004u
#define TDMA_PROCESS_SEGMENT_FLAG_COMMAND_QUEUE 0x00000008u
#define TDMA_PROCESS_SEGMENT_FLAG_MASK 0x0000000Fu

typedef enum {
    TDMA_PROCESS_IMAGE_MAP_OK = 0u,
    TDMA_PROCESS_IMAGE_MAP_BAD_ARGUMENT = 1u,
    TDMA_PROCESS_IMAGE_MAP_BAD_VERSION = 2u,
    TDMA_PROCESS_IMAGE_MAP_BAD_CAPACITY = 3u,
    TDMA_PROCESS_IMAGE_MAP_BAD_SEGMENT = 4u,
    TDMA_PROCESS_IMAGE_MAP_OVERLAP = 5u,
    TDMA_PROCESS_IMAGE_MAP_DUPLICATE_ID = 6u,
    TDMA_PROCESS_IMAGE_MAP_CRC_MISMATCH = 7u,
    TDMA_PROCESS_IMAGE_MAP_NOT_FOUND = 8u,
    TDMA_PROCESS_IMAGE_MAP_NOT_OWNER = 9u,
} tdma_process_image_map_result_t;

typedef struct {
    uint32_t used;
    uint32_t segment_id;
    uint32_t owner_slot_id;
    uint32_t payload_class;
    uint32_t byte_offset;
    uint32_t byte_length;
    uint32_t flags;
    uint32_t reserved;
} tdma_process_image_segment_t;

typedef struct {
    uint32_t version;
    uint32_t payload_size;
    uint32_t segment_count;
    uint32_t flags;
    tdma_process_image_segment_t segment[TDMA_PROCESS_IMAGE_SEGMENT_COUNT];
    uint32_t map_crc32;
} tdma_process_image_map_t;

uint32_t tdma_process_image_map_crc32(const tdma_process_image_map_t *map);
bool tdma_process_image_map_validate(
    const tdma_process_image_map_t *map,
    tdma_process_image_map_result_t *result);
bool tdma_process_image_map_find(
    const tdma_process_image_map_t *map,
    uint32_t segment_id,
    tdma_process_image_segment_t *segment,
    tdma_process_image_map_result_t *result);
bool tdma_process_image_map_can_publish(
    const tdma_process_image_map_t *map,
    uint32_t local_slot_mask,
    uint32_t segment_id,
    tdma_process_image_segment_t *segment,
    tdma_process_image_map_result_t *result);

#endif
