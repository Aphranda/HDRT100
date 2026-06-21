#include "ota_metadata.h"

#include <string.h>

#include "drv_flash.h"
#include "ota_crc32.h"

#define OTA_METADATA_COPY_SIZE    DRV_FLASH_SECTOR_SIZE
#define OTA_METADATA_COPY_A_OFFSET OTA_METADATA_OFFSET
#define OTA_METADATA_COPY_B_OFFSET (OTA_METADATA_OFFSET + OTA_METADATA_COPY_SIZE)

static uint32_t ota_metadata_copy_offset(uint32_t copy_index)
{
    return (copy_index == 0u) ? OTA_METADATA_COPY_A_OFFSET : OTA_METADATA_COPY_B_OFFSET;
}

uint32_t ota_metadata_crc32(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    ota_metadata_t copy = *metadata;
    copy.metadata_crc32 = 0u;
    return ota_crc32_compute((const uint8_t *)&copy, sizeof(copy));
}

static bool ota_metadata_is_valid(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    if (metadata->magic != OTA_METADATA_MAGIC || metadata->version != OTA_METADATA_VERSION) {
        return false;
    }

    return ota_metadata_crc32(metadata) == metadata->metadata_crc32;
}

bool ota_metadata_load(ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    ota_metadata_t copies[OTA_METADATA_COPY_COUNT];
    bool valid[OTA_METADATA_COPY_COUNT] = {false, false};

    for (uint32_t i = 0u; i < OTA_METADATA_COPY_COUNT; i++) {
        if (drv_flash_read(ota_metadata_copy_offset(i), &copies[i], sizeof(copies[i]))) {
            valid[i] = ota_metadata_is_valid(&copies[i]);
        }
    }

    if (valid[0] && valid[1]) {
        *metadata = (copies[0].sequence >= copies[1].sequence) ? copies[0] : copies[1];
        return true;
    }

    if (valid[0]) {
        *metadata = copies[0];
        return true;
    }

    if (valid[1]) {
        *metadata = copies[1];
        return true;
    }

    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = OTA_METADATA_MAGIC;
    metadata->version = OTA_METADATA_VERSION;
    metadata->sequence = 0u;
    metadata->active_slot = (uint32_t)OTA_SLOT_A;
    metadata->confirmed_slot = (uint32_t)OTA_SLOT_A;
    metadata->pending_slot = (uint32_t)OTA_SLOT_NONE;
    metadata->metadata_crc32 = ota_metadata_crc32(metadata);
    return true;
}

static bool ota_metadata_store(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    const uint32_t copy_index = metadata->sequence % OTA_METADATA_COPY_COUNT;
    const uint32_t offset = ota_metadata_copy_offset(copy_index);
    uint8_t page[DRV_FLASH_PAGE_SIZE];

    if (!drv_flash_erase(offset, OTA_METADATA_COPY_SIZE)) {
        return false;
    }

    const uint8_t *src = (const uint8_t *)metadata;
    uint32_t written = 0u;
    while (written < sizeof(*metadata)) {
        memset(page, 0xFF, sizeof(page));
        const uint32_t chunk = ((sizeof(*metadata) - written) > sizeof(page)) ?
                                   sizeof(page) :
                                   (uint32_t)(sizeof(*metadata) - written);
        memcpy(page, &src[written], chunk);
        if (!drv_flash_program(offset + written, page, sizeof(page))) {
            return false;
        }
        written += sizeof(page);
    }

    ota_metadata_t readback;
    if (!drv_flash_read(offset, &readback, sizeof(readback))) {
        return false;
    }

    return memcmp(&readback, metadata, sizeof(*metadata)) == 0;
}

bool ota_metadata_mark_pending(ota_slot_t slot, uint32_t image_size, uint32_t image_crc32)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    metadata.sequence++;
    metadata.pending_slot = (uint32_t)slot;
    metadata.boot_attempts = 0u;

    if (slot == OTA_SLOT_A) {
        metadata.slot_a_size = image_size;
        metadata.slot_a_crc32 = image_crc32;
    } else if (slot == OTA_SLOT_B) {
        metadata.slot_b_size = image_size;
        metadata.slot_b_crc32 = image_crc32;
    } else {
        return false;
    }

    metadata.metadata_crc32 = ota_metadata_crc32(&metadata);
    return ota_metadata_store(&metadata);
}
