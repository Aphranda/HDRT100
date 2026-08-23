#ifndef OTA_PACKAGE_H
#define OTA_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_partition.h"

#define OTA_PACKAGE_MAGIC       0x474B5054u
#define OTA_PACKAGE_VERSION     2u
#define OTA_PACKAGE_HEADER_SIZE 512u
#define OTA_PACKAGE_IMAGE_COUNT 2u
#define OTA_PACKAGE_TEXT_FIELD_SIZE 32u
#define OTA_PACKAGE_SHA256_SIZE 32u
#define OTA_PACKAGE_SIGNATURE_MAX_SIZE 64u

#define OTA_PACKAGE_BOOTLOADER_VERSION(major, minor, patch) \
    ((((uint32_t)(major) & 0xFFu) << 16u) | \
     (((uint32_t)(minor) & 0xFFu) << 8u) | \
     ((uint32_t)(patch) & 0xFFu))

typedef struct {
    uint32_t slot;
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    uint32_t run_offset;
    uint32_t flags;
    uint8_t sha256[OTA_PACKAGE_SHA256_SIZE];
} ota_package_image_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t package_size;
    uint32_t package_crc32;
    uint32_t image_count;
    char product_id[OTA_PACKAGE_TEXT_FIELD_SIZE];
    char hardware_id[OTA_PACKAGE_TEXT_FIELD_SIZE];
    uint32_t app_version_major;
    uint32_t app_version_minor;
    uint32_t app_version_patch;
    uint32_t min_bootloader_version;
    char build_id[OTA_PACKAGE_TEXT_FIELD_SIZE];
    uint8_t payload_sha256[OTA_PACKAGE_SHA256_SIZE];
    ota_package_image_t images[OTA_PACKAGE_IMAGE_COUNT];
    uint32_t extension_version;
    uint32_t required_flags;
    uint32_t security_counter;
    uint32_t key_id;
    uint32_t signature_length;
    uint8_t signature[OTA_PACKAGE_SIGNATURE_MAX_SIZE];
} ota_package_manifest_t;

bool ota_package_parse_header(const uint8_t *data,
                              uint32_t length,
                              ota_package_manifest_t *manifest);
const ota_package_image_t *ota_package_find_image(const ota_package_manifest_t *manifest,
                                                  ota_slot_t slot);

#endif
