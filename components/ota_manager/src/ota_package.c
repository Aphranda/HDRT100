#include "ota_package.h"

#include <stddef.h>
#include <string.h>

#include "project_config.h"

#define OTA_PACKAGE_PRODUCT_ID_OFFSET          32u
#define OTA_PACKAGE_HARDWARE_ID_OFFSET         64u
#define OTA_PACKAGE_APP_VERSION_MAJOR_OFFSET   96u
#define OTA_PACKAGE_APP_VERSION_MINOR_OFFSET   100u
#define OTA_PACKAGE_APP_VERSION_PATCH_OFFSET   104u
#define OTA_PACKAGE_MIN_BOOTLOADER_OFFSET      108u
#define OTA_PACKAGE_BUILD_ID_OFFSET            112u
#define OTA_PACKAGE_SHA256_OFFSET              144u
#define OTA_PACKAGE_IMAGE_TABLE_OFFSET         192u
#define OTA_PACKAGE_IMAGE_ENTRY_SIZE           32u

static uint32_t ota_package_read_le32(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static void ota_package_copy_text(char *destination, const uint8_t *source)
{
    memcpy(destination, source, OTA_PACKAGE_TEXT_FIELD_SIZE);
    destination[OTA_PACKAGE_TEXT_FIELD_SIZE - 1u] = '\0';
}

static bool ota_package_text_matches(const char *field, const char *expected)
{
    return strncmp(field, expected, OTA_PACKAGE_TEXT_FIELD_SIZE) == 0;
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

    ota_package_copy_text(manifest->product_id, &data[OTA_PACKAGE_PRODUCT_ID_OFFSET]);
    ota_package_copy_text(manifest->hardware_id, &data[OTA_PACKAGE_HARDWARE_ID_OFFSET]);
    manifest->app_version_major = ota_package_read_le32(&data[OTA_PACKAGE_APP_VERSION_MAJOR_OFFSET]);
    manifest->app_version_minor = ota_package_read_le32(&data[OTA_PACKAGE_APP_VERSION_MINOR_OFFSET]);
    manifest->app_version_patch = ota_package_read_le32(&data[OTA_PACKAGE_APP_VERSION_PATCH_OFFSET]);
    manifest->min_bootloader_version = ota_package_read_le32(&data[OTA_PACKAGE_MIN_BOOTLOADER_OFFSET]);
    ota_package_copy_text(manifest->build_id, &data[OTA_PACKAGE_BUILD_ID_OFFSET]);
    memcpy(manifest->payload_sha256,
           &data[OTA_PACKAGE_SHA256_OFFSET],
           sizeof(manifest->payload_sha256));

    if (!ota_package_text_matches(manifest->product_id, PROJECT_PRODUCT_ID) ||
        !ota_package_text_matches(manifest->hardware_id, PROJECT_HARDWARE_ID)) {
        return false;
    }

    const uint32_t bootloader_version =
        OTA_PACKAGE_BOOTLOADER_VERSION(PROJECT_BOOTLOADER_VERSION_MAJOR,
                                       PROJECT_BOOTLOADER_VERSION_MINOR,
                                       PROJECT_BOOTLOADER_VERSION_PATCH);
    if (manifest->min_bootloader_version > bootloader_version) {
        return false;
    }

    uint32_t cursor = OTA_PACKAGE_IMAGE_TABLE_OFFSET;
    for (uint32_t i = 0u; i < OTA_PACKAGE_IMAGE_COUNT; i++) {
        ota_package_image_t *image = &manifest->images[i];
        image->slot = ota_package_read_le32(&data[cursor + 0u]);
        image->offset = ota_package_read_le32(&data[cursor + 4u]);
        image->size = ota_package_read_le32(&data[cursor + 8u]);
        image->crc32 = ota_package_read_le32(&data[cursor + 12u]);
        image->run_offset = ota_package_read_le32(&data[cursor + 16u]);
        image->flags = ota_package_read_le32(&data[cursor + 20u]);
        cursor += OTA_PACKAGE_IMAGE_ENTRY_SIZE;
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
