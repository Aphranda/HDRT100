#include "pota_package.h"

#include <string.h>

#define POTA_PACKAGE_PRODUCT_ID_OFFSET        32u
#define POTA_PACKAGE_HARDWARE_ID_OFFSET       64u
#define POTA_PACKAGE_VERSION_MAJOR_OFFSET     96u
#define POTA_PACKAGE_VERSION_MINOR_OFFSET     100u
#define POTA_PACKAGE_VERSION_PATCH_OFFSET     104u
#define POTA_PACKAGE_MIN_BOOTLOADER_OFFSET    108u
#define POTA_PACKAGE_BUILD_ID_OFFSET          112u
#define POTA_PACKAGE_SHA256_OFFSET            144u

static uint32_t pota_read_le32(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static void pota_copy_text(char *destination, const uint8_t *source)
{
    memcpy(destination, source, POTA_TEXT_FIELD_SIZE);
    destination[POTA_TEXT_FIELD_SIZE - 1u] = '\0';
}

static bool pota_text_matches(const char *field, const char *expected)
{
    if (expected == NULL) {
        return true;
    }
    return strncmp(field, expected, POTA_TEXT_FIELD_SIZE) == 0;
}

pota_error_t pota_package_parse_header(const uint8_t *data,
                                       uint32_t length,
                                       const pota_package_constraints_t *constraints,
                                       pota_package_manifest_t *manifest)
{
    if (data == NULL || manifest == NULL || length < POTA_PACKAGE_HEADER_SIZE) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    manifest->magic = pota_read_le32(&data[0]);
    manifest->version = pota_read_le32(&data[4]);
    manifest->header_size = pota_read_le32(&data[8]);
    manifest->package_size = pota_read_le32(&data[12]);
    manifest->package_crc32 = pota_read_le32(&data[16]);
    manifest->image_count = pota_read_le32(&data[20]);

    if (manifest->magic != POTA_PACKAGE_MAGIC ||
        manifest->version != POTA_PACKAGE_VERSION ||
        manifest->header_size != POTA_PACKAGE_HEADER_SIZE ||
        manifest->image_count == 0u ||
        manifest->image_count > POTA_PACKAGE_MAX_IMAGES ||
        manifest->package_size <= POTA_PACKAGE_HEADER_SIZE) {
        return POTA_ERR_BAD_HEADER;
    }

    pota_copy_text(manifest->product_id, &data[POTA_PACKAGE_PRODUCT_ID_OFFSET]);
    pota_copy_text(manifest->hardware_id, &data[POTA_PACKAGE_HARDWARE_ID_OFFSET]);
    manifest->app_version_major = pota_read_le32(&data[POTA_PACKAGE_VERSION_MAJOR_OFFSET]);
    manifest->app_version_minor = pota_read_le32(&data[POTA_PACKAGE_VERSION_MINOR_OFFSET]);
    manifest->app_version_patch = pota_read_le32(&data[POTA_PACKAGE_VERSION_PATCH_OFFSET]);
    manifest->min_bootloader_version = pota_read_le32(&data[POTA_PACKAGE_MIN_BOOTLOADER_OFFSET]);
    pota_copy_text(manifest->build_id, &data[POTA_PACKAGE_BUILD_ID_OFFSET]);
    memcpy(manifest->payload_sha256,
           &data[POTA_PACKAGE_SHA256_OFFSET],
           sizeof(manifest->payload_sha256));

    if (constraints != NULL) {
        if (!pota_text_matches(manifest->product_id, constraints->product_id)) {
            return POTA_ERR_PRODUCT_MISMATCH;
        }
        if (!pota_text_matches(manifest->hardware_id, constraints->hardware_id)) {
            return POTA_ERR_HARDWARE_MISMATCH;
        }
        if (manifest->min_bootloader_version > constraints->bootloader_version) {
            return POTA_ERR_BOOTLOADER_TOO_OLD;
        }
    }

    uint32_t cursor = POTA_IMAGE_TABLE_OFFSET;
    for (uint32_t i = 0u; i < POTA_PACKAGE_MAX_IMAGES; i++) {
        pota_package_image_t *image = &manifest->images[i];
        image->slot = pota_read_le32(&data[cursor + 0u]);
        image->offset = pota_read_le32(&data[cursor + 4u]);
        image->size = pota_read_le32(&data[cursor + 8u]);
        image->crc32 = pota_read_le32(&data[cursor + 12u]);
        image->run_offset = pota_read_le32(&data[cursor + 16u]);
        image->flags = pota_read_le32(&data[cursor + 20u]);
        cursor += POTA_IMAGE_ENTRY_SIZE;
    }

    for (uint32_t i = 0u; i < manifest->image_count; i++) {
        const pota_package_image_t *image = &manifest->images[i];
        if ((image->slot != (uint32_t)POTA_SLOT_A &&
             image->slot != (uint32_t)POTA_SLOT_B) ||
            image->size == 0u ||
            image->offset < POTA_PACKAGE_HEADER_SIZE ||
            image->offset > manifest->package_size ||
            image->size > (manifest->package_size - image->offset)) {
            return POTA_ERR_BAD_HEADER;
        }
    }

    return POTA_ERR_NONE;
}

const pota_package_image_t *pota_package_find_image(const pota_package_manifest_t *manifest,
                                                    pota_slot_t slot)
{
    uint32_t index = 0u;
    if (!pota_package_find_image_index(manifest, slot, &index)) {
        return NULL;
    }

    return &manifest->images[index];
}

bool pota_package_find_image_index(const pota_package_manifest_t *manifest,
                                   pota_slot_t slot,
                                   uint32_t *index)
{
    if (manifest == NULL) {
        return false;
    }

    for (uint32_t i = 0u; i < manifest->image_count; i++) {
        if (manifest->images[i].slot == (uint32_t)slot) {
            if (index != NULL) {
                *index = i;
            }
            return true;
        }
    }

    return false;
}
