#ifndef POTA_METADATA_H
#define POTA_METADATA_H

#include "pota_types.h"

#include <stddef.h>

#define POTA_METADATA_MAGIC   0x4F544D44u
#define POTA_METADATA_VERSION 3u
#define POTA_METADATA_VERSION_V2 2u

#define POTA_FAULT_INJECT_NONE      0x00000000u
#define POTA_FAULT_INJECT_COPY_FAIL 0x00000001u

#define POTA_BOOT_CAP_COPY_TO_ACTIVE 0x00000001u
#define POTA_BOOT_CAP_DIRECT_AB      0x00000002u

typedef enum {
    POTA_BOOT_RESULT_NONE = 0,
    POTA_BOOT_RESULT_APPLIED,
    POTA_BOOT_RESULT_NO_PENDING,
    POTA_BOOT_RESULT_MAX_ATTEMPTS,
    POTA_BOOT_RESULT_STAGE_VALIDATE_FAILED,
    POTA_BOOT_RESULT_COPY_FAILED,
    POTA_BOOT_RESULT_ACTIVE_VALIDATE_FAILED,
} pota_boot_result_t;

typedef enum {
    POTA_COPY_TXN_NONE = 0,
    POTA_COPY_TXN_STARTED,
    POTA_COPY_TXN_ERASED_ACTIVE,
    POTA_COPY_TXN_PROGRAMMING,
    POTA_COPY_TXN_VERIFYING,
    POTA_COPY_TXN_DONE,
    POTA_COPY_TXN_FAILED,
} pota_copy_txn_state_t;

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
    uint8_t slot_a_sha256[POTA_SHA256_SIZE];
    uint32_t slot_b_size;
    uint32_t slot_b_crc32;
    uint8_t slot_b_sha256[POTA_SHA256_SIZE];
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
} pota_metadata_t;

uint32_t pota_metadata_crc32(const pota_metadata_t *metadata);
uint32_t pota_metadata_ext_crc32(const pota_metadata_t *metadata);
uint32_t pota_metadata_ab_crc32(const pota_metadata_t *metadata);
void pota_metadata_update_crc(pota_metadata_t *metadata);
bool pota_metadata_is_valid(const pota_metadata_t *metadata);
bool pota_metadata_copy_txn_state_is_valid(uint32_t state);
bool pota_metadata_boot_mode_is_valid(uint32_t mode);
bool pota_metadata_slot_or_none_is_valid(uint32_t slot);
void pota_metadata_clear_copy_transaction_fields(pota_metadata_t *metadata);
void pota_metadata_init_extension_defaults(pota_metadata_t *metadata);
void pota_metadata_set_default(pota_metadata_t *metadata);
void pota_metadata_upgrade_if_needed(pota_metadata_t *metadata);
bool pota_metadata_mark_pending(pota_metadata_t *metadata,
                                pota_slot_t slot,
                                uint32_t image_size,
                                uint32_t image_crc32);
bool pota_metadata_can_confirm_active(const pota_metadata_t *metadata);
bool pota_metadata_confirm_active(pota_metadata_t *metadata);
bool pota_metadata_set_boot_mode(pota_metadata_t *metadata, pota_boot_mode_t mode);
bool pota_metadata_set_fault_injection(pota_metadata_t *metadata, uint32_t flags);
bool pota_metadata_begin_copy_transaction(pota_metadata_t *metadata,
                                          pota_slot_t source,
                                          pota_slot_t destination,
                                          uint32_t image_size,
                                          uint32_t image_crc32);
bool pota_metadata_update_copy_transaction(pota_metadata_t *metadata,
                                           uint32_t state,
                                           uint32_t written,
                                           uint32_t last_error);
bool pota_metadata_finish_copy_transaction(pota_metadata_t *metadata);
bool pota_metadata_fail_copy_transaction(pota_metadata_t *metadata, uint32_t last_error);
bool pota_metadata_clear_copy_transaction(pota_metadata_t *metadata);
bool pota_metadata_record_boot_result(pota_metadata_t *metadata,
                                      pota_boot_result_t result,
                                      pota_slot_t source_slot,
                                      bool clear_pending);
bool pota_metadata_apply_copy_to_active_done(pota_metadata_t *metadata,
                                             pota_slot_t staging_slot,
                                             pota_slot_t active_slot);
bool pota_metadata_apply_direct_ab_pending(pota_metadata_t *metadata,
                                           pota_slot_t pending_slot);
bool pota_metadata_rollback_direct_ab(pota_metadata_t *metadata,
                                      pota_boot_result_t reason,
                                      pota_slot_t failed_slot,
                                      pota_slot_t rollback_slot);
bool pota_metadata_increment_boot_attempts(pota_metadata_t *metadata);
const pota_metadata_t *pota_metadata_select_newest(const pota_metadata_t *copies,
                                                   size_t copy_count);

#endif
