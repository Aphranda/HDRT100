#ifndef OTA_METADATA_H
#define OTA_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_partition.h"

#define OTA_METADATA_MAGIC   0x4F544D44u
#define OTA_METADATA_VERSION 1u
#define OTA_METADATA_COPY_COUNT 2u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t confirmed_slot;
    uint32_t boot_attempts;
    uint32_t rollback_count;
    uint32_t slot_a_size;
    uint32_t slot_a_crc32;
    uint8_t slot_a_sha256[32];
    uint32_t slot_b_size;
    uint32_t slot_b_crc32;
    uint8_t slot_b_sha256[32];
    uint32_t metadata_crc32;
} ota_metadata_t;

bool ota_metadata_load(ota_metadata_t *metadata);
bool ota_metadata_mark_pending(ota_slot_t slot, uint32_t image_size, uint32_t image_crc32);
uint32_t ota_metadata_crc32(const ota_metadata_t *metadata);

#endif
