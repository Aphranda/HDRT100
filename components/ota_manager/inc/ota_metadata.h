#ifndef OTA_METADATA_H
#define OTA_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_partition.h"

#define OTA_METADATA_MAGIC   0x4F544D44u
#define OTA_METADATA_VERSION 3u
#define OTA_METADATA_COPY_COUNT 2u

#define OTA_FAULT_INJECT_NONE      0x00000000u
#define OTA_FAULT_INJECT_COPY_FAIL 0x00000001u

typedef enum {
    OTA_BOOT_RESULT_NONE = 0,
    OTA_BOOT_RESULT_APPLIED,
    OTA_BOOT_RESULT_NO_PENDING,
    OTA_BOOT_RESULT_MAX_ATTEMPTS,
    OTA_BOOT_RESULT_STAGE_VALIDATE_FAILED,
    OTA_BOOT_RESULT_COPY_FAILED,
    OTA_BOOT_RESULT_ACTIVE_VALIDATE_FAILED,
} ota_boot_result_t;

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
    uint32_t last_boot_result;
    uint32_t last_boot_source_slot;
    uint32_t last_boot_size;
    uint32_t last_boot_crc32;
    uint32_t metadata_crc32;
    uint32_t fault_injection_flags;
} ota_metadata_t;

bool ota_metadata_load(ota_metadata_t *metadata);
bool ota_metadata_store(const ota_metadata_t *metadata);
bool ota_metadata_mark_pending(ota_slot_t slot, uint32_t image_size, uint32_t image_crc32);
bool ota_metadata_confirm_active(void);
bool ota_metadata_set_fault_injection(uint32_t flags);
bool ota_metadata_corrupt_copy(uint32_t copy_index);
bool ota_metadata_repair_copies(void);
const char *ota_metadata_boot_result_to_string(uint32_t result);
uint32_t ota_metadata_crc32(const ota_metadata_t *metadata);

#endif
