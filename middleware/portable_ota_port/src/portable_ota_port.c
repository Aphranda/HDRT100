#include "portable_ota_port.h"

#include <stddef.h>
#include <string.h>

#include "pota_package.h"
#include "project_config.h"

_Static_assert(OTA_PACKAGE_MAGIC == POTA_PACKAGE_MAGIC, "OTA package magic mismatch");
_Static_assert(OTA_PACKAGE_VERSION == POTA_PACKAGE_VERSION, "OTA package version mismatch");
_Static_assert(OTA_PACKAGE_HEADER_SIZE == POTA_PACKAGE_HEADER_SIZE, "OTA package header size mismatch");
_Static_assert(OTA_PACKAGE_IMAGE_COUNT == POTA_PACKAGE_MAX_IMAGES, "OTA package image count mismatch");
_Static_assert(OTA_PACKAGE_TEXT_FIELD_SIZE == POTA_TEXT_FIELD_SIZE, "OTA package text field size mismatch");
_Static_assert(OTA_PACKAGE_SHA256_SIZE == POTA_SHA256_SIZE, "OTA package SHA256 size mismatch");
_Static_assert((uint32_t)OTA_SLOT_NONE == (uint32_t)POTA_SLOT_NONE, "OTA none slot mismatch");
_Static_assert((uint32_t)OTA_SLOT_A == (uint32_t)POTA_SLOT_A, "OTA slot A mismatch");
_Static_assert((uint32_t)OTA_SLOT_B == (uint32_t)POTA_SLOT_B, "OTA slot B mismatch");
_Static_assert(sizeof(ota_package_image_t) == sizeof(pota_package_image_t),
               "OTA package image layout size mismatch");
_Static_assert(offsetof(ota_package_image_t, slot) == offsetof(pota_package_image_t, slot),
               "OTA package image slot offset mismatch");
_Static_assert(offsetof(ota_package_image_t, offset) == offsetof(pota_package_image_t, offset),
               "OTA package image offset offset mismatch");
_Static_assert(offsetof(ota_package_image_t, size) == offsetof(pota_package_image_t, size),
               "OTA package image size offset mismatch");
_Static_assert(offsetof(ota_package_image_t, crc32) == offsetof(pota_package_image_t, crc32),
               "OTA package image crc32 offset mismatch");
_Static_assert(offsetof(ota_package_image_t, run_offset) == offsetof(pota_package_image_t, run_offset),
               "OTA package image run offset mismatch");
_Static_assert(offsetof(ota_package_image_t, flags) == offsetof(pota_package_image_t, flags),
               "OTA package image flags offset mismatch");
_Static_assert(sizeof(ota_package_manifest_t) == sizeof(pota_package_manifest_t),
               "OTA package manifest layout size mismatch");
_Static_assert(offsetof(ota_package_manifest_t, magic) == offsetof(pota_package_manifest_t, magic),
               "OTA package manifest magic offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, version) == offsetof(pota_package_manifest_t, version),
               "OTA package manifest version offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, header_size) == offsetof(pota_package_manifest_t, header_size),
               "OTA package manifest header size offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, package_size) == offsetof(pota_package_manifest_t, package_size),
               "OTA package manifest package size offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, package_crc32) == offsetof(pota_package_manifest_t, package_crc32),
               "OTA package manifest package crc32 offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, image_count) == offsetof(pota_package_manifest_t, image_count),
               "OTA package manifest image count offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, product_id) == offsetof(pota_package_manifest_t, product_id),
               "OTA package manifest product id offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, hardware_id) == offsetof(pota_package_manifest_t, hardware_id),
               "OTA package manifest hardware id offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, app_version_major) ==
                   offsetof(pota_package_manifest_t, app_version_major),
               "OTA package manifest app major offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, app_version_minor) ==
                   offsetof(pota_package_manifest_t, app_version_minor),
               "OTA package manifest app minor offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, app_version_patch) ==
                   offsetof(pota_package_manifest_t, app_version_patch),
               "OTA package manifest app patch offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, min_bootloader_version) ==
                   offsetof(pota_package_manifest_t, min_bootloader_version),
               "OTA package manifest bootloader version offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, build_id) == offsetof(pota_package_manifest_t, build_id),
               "OTA package manifest build id offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, payload_sha256) ==
                   offsetof(pota_package_manifest_t, payload_sha256),
               "OTA package manifest sha256 offset mismatch");
_Static_assert(offsetof(ota_package_manifest_t, images) == offsetof(pota_package_manifest_t, images),
               "OTA package manifest images offset mismatch");

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

    memcpy(manifest, &portable_manifest, sizeof(*manifest));
    return true;
}

const ota_package_image_t *portable_ota_port_find_package_image(const ota_package_manifest_t *manifest,
                                                                ota_slot_t slot)
{
    if (manifest == NULL) {
        return NULL;
    }

    pota_package_manifest_t portable_manifest;
    memcpy(&portable_manifest, manifest, sizeof(portable_manifest));

    uint32_t image_index = 0u;
    if (!pota_package_find_image_index(&portable_manifest, (pota_slot_t)slot, &image_index) ||
        image_index >= OTA_PACKAGE_IMAGE_COUNT) {
        return NULL;
    }

    return &manifest->images[image_index];
}
