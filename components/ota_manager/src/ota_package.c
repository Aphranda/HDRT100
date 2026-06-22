#include "ota_package.h"

#include <stddef.h>

static uint32_t ota_package_read_le32(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

bool ota_package_parse_header(const uint8_t *data,
                              uint32_t length,
                              ota_package_manifest_t *manifest)
{
    if (data == NULL || manifest == NULL || length < OTA_PACKAGE_HEADER_SIZE) {
        return false;
    }

    manifest->magic = ota_package_read_le32(&data[0]);
    manifest->version = ota_package_read_le32(&data[4]);
    manifest->header_size = ota_package_read_le32(&data[8]);
    manifest->package_size = ota_package_read_le32(&data[12]);
    manifest->package_crc32 = ota_package_read_le32(&data[16]);
    manifest->image_count = ota_package_read_le32(&data[20]);

    if (manifest->magic != OTA_PACKAGE_MAGIC ||
        manifest->version != OTA_PACKAGE_VERSION ||
        manifest->header_size != OTA_PACKAGE_HEADER_SIZE ||
        manifest->image_count == 0u ||
        manifest->image_count > OTA_PACKAGE_IMAGE_COUNT ||
        manifest->package_size <= OTA_PACKAGE_HEADER_SIZE) {
        return false;
    }

    uint32_t cursor = 32u;
    for (uint32_t i = 0u; i < OTA_PACKAGE_IMAGE_COUNT; i++) {
        ota_package_image_t *image = &manifest->images[i];
        image->slot = ota_package_read_le32(&data[cursor + 0u]);
        image->offset = ota_package_read_le32(&data[cursor + 4u]);
        image->size = ota_package_read_le32(&data[cursor + 8u]);
        image->crc32 = ota_package_read_le32(&data[cursor + 12u]);
        image->run_offset = ota_package_read_le32(&data[cursor + 16u]);
        image->flags = ota_package_read_le32(&data[cursor + 20u]);
        cursor += 32u;
    }

    for (uint32_t i = 0u; i < manifest->image_count; i++) {
        const ota_package_image_t *image = &manifest->images[i];
        if ((image->slot != (uint32_t)OTA_SLOT_A &&
             image->slot != (uint32_t)OTA_SLOT_B) ||
            image->size == 0u ||
            image->offset < OTA_PACKAGE_HEADER_SIZE ||
            image->offset > manifest->package_size ||
            image->size > (manifest->package_size - image->offset)) {
            return false;
        }
    }

    return true;
}

const ota_package_image_t *ota_package_find_image(const ota_package_manifest_t *manifest,
                                                  ota_slot_t slot)
{
    if (manifest == NULL) {
        return NULL;
    }

    for (uint32_t i = 0u; i < manifest->image_count; i++) {
        if (manifest->images[i].slot == (uint32_t)slot) {
            return &manifest->images[i];
        }
    }

    return NULL;
}
