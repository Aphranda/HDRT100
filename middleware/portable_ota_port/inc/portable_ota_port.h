#ifndef PORTABLE_OTA_PORT_H
#define PORTABLE_OTA_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_package.h"

bool portable_ota_port_parse_package_header(const uint8_t *data,
                                            uint32_t length,
                                            ota_package_manifest_t *manifest);
const ota_package_image_t *portable_ota_port_find_package_image(const ota_package_manifest_t *manifest,
                                                                ota_slot_t slot);
uint32_t portable_ota_port_crc32_update(uint32_t crc, const uint8_t *data, size_t length);
uint32_t portable_ota_port_crc32_compute(const uint8_t *data, size_t length);

#endif
