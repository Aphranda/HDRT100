#ifndef POTA_METADATA_H
#define POTA_METADATA_H

#include "pota_types.h"

#define POTA_METADATA_MAGIC   0x504F5441u
#define POTA_METADATA_VERSION 1u

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
    uint32_t slot_b_size;
    uint32_t slot_b_crc32;
    uint32_t last_boot_result;
    uint32_t last_boot_source_slot;
    uint32_t last_boot_size;
    uint32_t last_boot_crc32;
    uint32_t boot_mode;
    uint32_t boot_capabilities;
    uint32_t copy_txn_state;
    uint32_t copy_source_slot;
    uint32_t copy_destination_slot;
    uint32_t copy_size;
    uint32_t copy_crc32;
    uint32_t copy_written;
    uint32_t copy_attempts;
    uint32_t copy_last_error;
    uint32_t metadata_crc32;
} pota_metadata_t;

uint32_t pota_metadata_crc32(const pota_metadata_t *metadata);
bool pota_metadata_is_valid(const pota_metadata_t *metadata);

#endif
