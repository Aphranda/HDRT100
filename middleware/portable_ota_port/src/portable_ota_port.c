#include "portable_ota_port.h"

#include <stddef.h>
#include <string.h>

#include "pota_package.h"
#include "project_config.h"

static pota_slot_t portable_ota_port_to_pota_slot(ota_slot_t slot)
{
    if (slot == OTA_SLOT_A) {
        return POTA_SLOT_A;
    }
    if (slot == OTA_SLOT_B) {
        return POTA_SLOT_B;
    }
    return POTA_SLOT_NONE;
}

static void portable_ota_port_copy_manifest(ota_package_manifest_t *destination,
                                            const pota_package_manifest_t *source)
{
    destination->magic = source->magic;
    destination->version = source->version;
    destination->header_size = source->header_size;
    destination->package_size = source->package_size;
    destination->package_crc32 = source->package_crc32;
    destination->image_count = source->image_count;
    memcpy(destination->product_id, source->product_id, sizeof(destination->product_id));
    memcpy(destination->hardware_id, source->hardware_id, sizeof(destination->hardware_id));
    destination->app_version_major = source->app_version_major;
    destination->app_version_minor = source->app_version_minor;
    destination->app_version_patch = source->app_version_patch;
    destination->min_bootloader_version = source->min_bootloader_version;
    memcpy(destination->build_id, source->build_id, sizeof(destination->build_id));
    memcpy(destination->payload_sha256, source->payload_sha256, sizeof(destination->payload_sha256));

    for (uint32_t i = 0u; i < OTA_PACKAGE_IMAGE_COUNT; i++) {
        destination->images[i].slot = source->images[i].slot;
        destination->images[i].offset = source->images[i].offset;
        destination->images[i].size = source->images[i].size;
        destination->images[i].crc32 = source->images[i].crc32;
        destination->images[i].run_offset = source->images[i].run_offset;
        destination->images[i].flags = source->images[i].flags;
    }
}

bool portable_ota_port_parse_package_header(const uint8_t *data,
                                            uint32_t length,
                                            ota_package_manifest_t *manifest)
{
    if (manifest == NULL) {
        return false;
    }

    pota_package_manifest_t portable_manifest;
    const pota_package_constraints_t constraints = {
        .product_id = PROJECT_PRODUCT_ID,
        .hardware_id = PROJECT_HARDWARE_ID,
        .bootloader_version =
            OTA_PACKAGE_BOOTLOADER_VERSION(PROJECT_BOOTLOADER_VERSION_MAJOR,
                                           PROJECT_BOOTLOADER_VERSION_MINOR,
                                           PROJECT_BOOTLOADER_VERSION_PATCH),
    };

    const pota_error_t error =
        pota_package_parse_header(data, length, &constraints, &portable_manifest);
    if (error != POTA_ERR_NONE) {
        return false;
    }

    portable_ota_port_copy_manifest(manifest, &portable_manifest);
    return true;
}

const ota_package_image_t *portable_ota_port_find_package_image(const ota_package_manifest_t *manifest,
                                                                ota_slot_t slot)
{
    if (manifest == NULL) {
        return NULL;
    }

    const pota_slot_t portable_slot = portable_ota_port_to_pota_slot(slot);
    for (uint32_t i = 0u; i < manifest->image_count; i++) {
        if (manifest->images[i].slot == (uint32_t)portable_slot) {
            return &manifest->images[i];
        }
    }

    return NULL;
}
