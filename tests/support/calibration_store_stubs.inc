#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "calibration_training_store.h"
#include "drv_flash.h"
#include "flash_store_nvs.h"
#include "flash_transaction.h"

const uint8_t *drv_flash_xip_ptr(uint32_t offset)
{
    (void)offset;
    return NULL;
}

bool drv_flash_is_erased(uint32_t offset, size_t length)
{
    (void)offset;
    (void)length;
    return false;
}

flash_store_nvs_result_t flash_store_nvs_scan(
    const uint8_t *sector, size_t sector_size,
    uint32_t expected_schema_version, uint32_t expected_object_type,
    uint32_t known_flags_mask, size_t program_alignment,
    uint8_t *scratch_payload, size_t scratch_capacity,
    flash_store_nvs_scan_t *scan)
{
    (void)sector;
    (void)sector_size;
    (void)expected_schema_version;
    (void)expected_object_type;
    (void)known_flags_mask;
    (void)program_alignment;
    (void)scratch_payload;
    (void)scratch_capacity;
    (void)scan;
    return FLASH_STORE_NVS_EMPTY;
}

bool flash_store_record_is_newer(
    const flash_store_record_header_t *candidate,
    const flash_store_record_header_t *current)
{
    (void)candidate;
    (void)current;
    return false;
}

flash_store_record_result_t flash_store_record_decode(
    const uint8_t *record, size_t record_size,
    uint32_t expected_schema_version, uint32_t expected_object_type,
    uint32_t known_flags_mask, uint8_t *payload, size_t payload_capacity,
    flash_store_record_header_t *header)
{
    (void)record;
    (void)record_size;
    (void)expected_schema_version;
    (void)expected_object_type;
    (void)known_flags_mask;
    (void)payload;
    (void)payload_capacity;
    (void)header;
    return FLASH_STORE_RECORD_BAD_ARGUMENT;
}

size_t flash_store_nvs_record_span(size_t record_size, size_t alignment)
{
    return alignment == 0u ? 0u :
        (record_size + alignment - 1u) / alignment * alignment;
}

flash_store_nvs_result_t flash_store_nvs_plan_append(
    uint32_t schema_version, uint32_t object_type, uint32_t generation,
    uint32_t sequence, uint32_t flags, const uint8_t *payload,
    uint32_t payload_length, size_t append_offset, size_t sector_size,
    size_t program_alignment, uint8_t *program_buffer,
    size_t program_capacity, size_t *program_size)
{
    (void)schema_version;
    (void)object_type;
    (void)generation;
    (void)sequence;
    (void)flags;
    (void)payload;
    (void)payload_length;
    (void)append_offset;
    (void)sector_size;
    (void)program_alignment;
    (void)program_buffer;
    (void)program_capacity;
    (void)program_size;
    return FLASH_STORE_NVS_BAD_ARGUMENT;
}

const flash_transaction_completion_lease_t *
flash_transaction_ao_get_completion_lease(void)
{
    return NULL;
}

bool flash_transaction_ao_execute(
    const flash_transaction_request_t *request,
    flash_transaction_completion_t *completion)
{
    (void)request;
    (void)completion;
    return false;
}
