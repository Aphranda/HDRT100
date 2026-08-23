#include "flash_store_nvs.h"

#include <string.h>

static bool erased(const uint8_t *p, size_t n)
{
    for (size_t i = 0u; i < n; ++i) {
        if (p[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

size_t flash_store_nvs_record_span(size_t record_size, size_t alignment)
{
    if (record_size == 0u || alignment == 0u) {
        return 0u;
    }
    const size_t rem = record_size % alignment;
    return rem == 0u ? record_size : record_size + alignment - rem;
}

flash_store_nvs_result_t flash_store_nvs_scan(
    const uint8_t *sector, size_t sector_size, uint32_t expected_schema_version,
    uint32_t expected_object_type, uint32_t known_flags_mask,
    size_t program_alignment, uint8_t *scratch_payload,
    size_t scratch_capacity, flash_store_nvs_scan_t *scan)
{
    if (sector == NULL || scan == NULL || sector_size < FLASH_STORE_RECORD_HEADER_SIZE ||
        expected_schema_version == 0u || expected_object_type == 0u ||
        program_alignment == 0u) {
        return FLASH_STORE_NVS_BAD_ARGUMENT;
    }
    memset(scan, 0, sizeof(*scan));
    size_t offset = 0u;
    while (offset < sector_size) {
        const size_t remaining = sector_size - offset;
        if (remaining < FLASH_STORE_RECORD_HEADER_SIZE ||
            erased(&sector[offset], remaining < 4u ? remaining : 4u)) {
            scan->append_offset = offset;
            if (remaining < FLASH_STORE_RECORD_HEADER_SIZE && remaining != 0u) {
                scan->saw_torn_tail = !erased(&sector[offset], remaining);
            }
            break;
        }

        const uint32_t payload_length = le32(&sector[offset + 24u]);
        const size_t encoded_size = FLASH_STORE_RECORD_HEADER_SIZE +
                                    (size_t)payload_length;
        const size_t span = flash_store_nvs_record_span(encoded_size,
                                                         program_alignment);
        if (span == 0u || span > remaining) {
            scan->saw_torn_tail = true;
            scan->append_offset = offset;
            break;
        }
        flash_store_record_header_t header;
        const flash_store_record_result_t decoded = flash_store_record_decode(
            &sector[offset], encoded_size, expected_schema_version,
            expected_object_type, known_flags_mask, scratch_payload,
            scratch_capacity, &header);
        if (decoded == FLASH_STORE_RECORD_SCHEMA_MISMATCH) {
            scan->needs_rotation = true;
            return FLASH_STORE_NVS_UNKNOWN_SCHEMA;
        }
        if (decoded == FLASH_STORE_RECORD_OBJECT_MISMATCH) {
            scan->needs_rotation = true;
            return FLASH_STORE_NVS_UNKNOWN_OBJECT;
        }
        if (decoded == FLASH_STORE_RECORD_FLAGS) {
            scan->needs_rotation = true;
            return FLASH_STORE_NVS_FLAGS;
        }
        if (decoded != FLASH_STORE_RECORD_OK) {
            scan->saw_torn_tail = true;
            scan->append_offset = offset;
            break;
        }
        if (!scan->has_latest ||
            flash_store_record_is_newer(&header, &scan->latest)) {
            scan->has_latest = true;
            scan->latest = header;
            scan->latest_offset = offset;
            scan->latest_span = span;
        }
        scan->valid_count++;
        offset += span;
        scan->append_offset = offset;
    }
    if (scan->valid_count == 0u) {
        return scan->saw_torn_tail ? FLASH_STORE_NVS_CORRUPT
                                   : FLASH_STORE_NVS_EMPTY;
    }
    return FLASH_STORE_NVS_OK;
}

flash_store_nvs_result_t flash_store_nvs_plan_append(
    uint32_t schema_version, uint32_t object_type, uint32_t generation,
    uint32_t sequence, uint32_t flags, const uint8_t *payload,
    uint32_t payload_length, size_t append_offset, size_t sector_size,
    size_t program_alignment, uint8_t *program_buffer, size_t program_capacity,
    size_t *program_size)
{
    if (program_buffer == NULL || program_size == NULL ||
        append_offset > sector_size || program_alignment == 0u) {
        return FLASH_STORE_NVS_BAD_ARGUMENT;
    }
    const size_t record_size = FLASH_STORE_RECORD_HEADER_SIZE + payload_length;
    const size_t span = flash_store_nvs_record_span(record_size, program_alignment);
    if (span == 0u || span > sector_size - append_offset ||
        span > program_capacity) {
        return FLASH_STORE_NVS_NO_SPACE;
    }
    memset(program_buffer, 0xFF, span);
    size_t encoded = 0u;
    const flash_store_record_result_t result = flash_store_record_encode(
        schema_version, object_type, generation, sequence, flags, payload,
        payload_length, program_buffer, span, &encoded);
    if (result != FLASH_STORE_RECORD_OK) {
        return result == FLASH_STORE_RECORD_RANGE ? FLASH_STORE_NVS_NO_SPACE
                                                   : FLASH_STORE_NVS_BAD_ARGUMENT;
    }
    *program_size = span;
    return FLASH_STORE_NVS_OK;
}
