#ifndef OTA_METADATA_H
#define OTA_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_partition.h"
#include "pota_platform.h"

#define OTA_METADATA_MAGIC   0x4F544D44u
#define OTA_METADATA_VERSION 3u
#define OTA_METADATA_COPY_COUNT 2u

#define OTA_FAULT_INJECT_NONE      0x00000000u
#define OTA_FAULT_INJECT_COPY_FAIL 0x00000001u

#define OTA_BOOT_CAP_COPY_TO_ACTIVE 0x00000001u
#define OTA_BOOT_CAP_DIRECT_AB      0x00000002u

typedef enum {
    OTA_BOOT_MODE_COPY_TO_ACTIVE = 0,
    OTA_BOOT_MODE_DIRECT_AB,
} ota_boot_mode_t;

typedef enum {
    OTA_BOOT_RESULT_NONE = 0,
    OTA_BOOT_RESULT_APPLIED,
    OTA_BOOT_RESULT_NO_PENDING,
    OTA_BOOT_RESULT_MAX_ATTEMPTS,
    OTA_BOOT_RESULT_STAGE_VALIDATE_FAILED,
    OTA_BOOT_RESULT_COPY_FAILED,
    OTA_BOOT_RESULT_ACTIVE_VALIDATE_FAILED,
} ota_boot_result_t;

typedef enum {
    OTA_COPY_TXN_NONE = 0,
    OTA_COPY_TXN_STARTED,
    OTA_COPY_TXN_ERASED_ACTIVE,
    OTA_COPY_TXN_PROGRAMMING,
    OTA_COPY_TXN_VERIFYING,
    OTA_COPY_TXN_DONE,
    OTA_COPY_TXN_FAILED,
} ota_copy_txn_state_t;

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
    uint32_t copy_txn_state;
    uint32_t copy_source_slot;
    uint32_t copy_destination_slot;
    uint32_t copy_size;
    uint32_t copy_crc32;
    uint32_t copy_written;
    uint32_t copy_attempts;
    uint32_t copy_last_error;
    uint32_t metadata_ext_crc32;
    uint32_t boot_mode;
    uint32_t previous_slot;
    uint32_t boot_generation;
    uint32_t boot_capabilities;
    uint32_t metadata_ab_crc32;
} ota_metadata_t;

typedef struct {
    uint32_t valid_lane_count;
    uint32_t valid_record_count;
    uint32_t newest_lane_generation;
    uint32_t newest_sequence;
    uint32_t newest_security_counter;
    uint32_t newest_lane;
    uint32_t newest_record_page;
} ota_metadata_bcb_health_t;

bool ota_metadata_load(ota_metadata_t *metadata);
bool ota_metadata_store(const ota_metadata_t *metadata);
bool ota_metadata_mark_pending(ota_slot_t slot, uint32_t image_size,
                               uint32_t image_crc32,
                               uint32_t security_counter);
pota_platform_step_result_t ota_metadata_mark_pending_step(
    ota_slot_t slot, uint32_t image_size, uint32_t image_crc32,
    uint32_t security_counter);
bool ota_metadata_confirm_active(void);
bool ota_metadata_set_boot_mode(ota_boot_mode_t mode);
bool ota_metadata_set_fault_injection(uint32_t flags);
bool ota_metadata_begin_copy_transaction(ota_slot_t source,
                                         ota_slot_t destination,
                                         uint32_t image_size,
                                         uint32_t image_crc32);
bool ota_metadata_update_copy_transaction(uint32_t state,
                                          uint32_t written,
                                          uint32_t last_error);
bool ota_metadata_finish_copy_transaction(void);
bool ota_metadata_fail_copy_transaction(uint32_t last_error);
bool ota_metadata_clear_copy_transaction(void);
bool ota_metadata_corrupt_copy(uint32_t copy_index);
bool ota_metadata_repair_copies(void);
bool ota_metadata_get_bcb_health(ota_metadata_bcb_health_t *health);
const char *ota_metadata_boot_result_to_string(uint32_t result);
uint32_t ota_metadata_crc32(const ota_metadata_t *metadata);
uint32_t ota_metadata_ext_crc32(const ota_metadata_t *metadata);
uint32_t ota_metadata_ab_crc32(const ota_metadata_t *metadata);

#endif
