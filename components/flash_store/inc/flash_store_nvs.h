#ifndef FLASH_STORE_NVS_H
#define FLASH_STORE_NVS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_store_record.h"

/* Read-only NVS journal planner.  It never performs Flash IO; callers must
 * submit the returned bytes through FlashTransactionAO. */
typedef enum {
    FLASH_STORE_NVS_OK = 0,
    FLASH_STORE_NVS_BAD_ARGUMENT,
    FLASH_STORE_NVS_EMPTY,
    FLASH_STORE_NVS_NO_SPACE,
    FLASH_STORE_NVS_TRUNCATED,
    FLASH_STORE_NVS_CORRUPT,
    FLASH_STORE_NVS_UNKNOWN_SCHEMA,
    FLASH_STORE_NVS_UNKNOWN_OBJECT,
    FLASH_STORE_NVS_FLAGS,
} flash_store_nvs_result_t;

typedef struct {
    size_t append_offset;
    size_t latest_offset;
    size_t latest_span;
    uint32_t valid_count;
    bool has_latest;
    bool saw_torn_tail;
    bool needs_rotation;
    flash_store_record_header_t latest;
} flash_store_nvs_scan_t;

size_t flash_store_nvs_record_span(size_t record_size,
                                   size_t program_alignment);

flash_store_nvs_result_t flash_store_nvs_scan(
    const uint8_t *sector,
    size_t sector_size,
    uint32_t expected_schema_version,
    uint32_t expected_object_type,
    uint32_t known_flags_mask,
    size_t program_alignment,
    uint8_t *scratch_payload,
    size_t scratch_capacity,
    flash_store_nvs_scan_t *scan);

flash_store_nvs_result_t flash_store_nvs_plan_append(
    uint32_t schema_version,
    uint32_t object_type,
    uint32_t generation,
    uint32_t sequence,
    uint32_t flags,
    const uint8_t *payload,
    uint32_t payload_length,
    size_t append_offset,
    size_t sector_size,
    size_t program_alignment,
    uint8_t *program_buffer,
    size_t program_capacity,
    size_t *program_size);

#endif
