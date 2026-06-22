#ifndef OTA_PACKAGE_H
#define OTA_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_partition.h"

#define OTA_PACKAGE_MAGIC       0x474B5054u
#define OTA_PACKAGE_VERSION     1u
#define OTA_PACKAGE_HEADER_SIZE 512u
#define OTA_PACKAGE_IMAGE_COUNT 2u

typedef struct {
    uint32_t slot;
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    uint32_t run_offset;
    uint32_t flags;
} ota_package_image_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t package_size;
    uint32_t package_crc32;
    uint32_t image_count;
    ota_package_image_t images[OTA_PACKAGE_IMAGE_COUNT];
} ota_package_manifest_t;

bool ota_package_parse_header(const uint8_t *data,
                              uint32_t length,
                              ota_package_manifest_t *manifest);
const ota_package_image_t *ota_package_find_image(const ota_package_manifest_t *manifest,
                                                  ota_slot_t slot);

#endif
