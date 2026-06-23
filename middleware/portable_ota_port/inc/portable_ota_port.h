#ifndef PORTABLE_OTA_PORT_H
#define PORTABLE_OTA_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_package.h"
#include "ota_metadata.h"
#include "ota_vector.h"

bool portable_ota_port_parse_package_header(const uint8_t *data,
                                            uint32_t length,
                                            ota_package_manifest_t *manifest);
const ota_package_image_t *portable_ota_port_find_package_image(const ota_package_manifest_t *manifest,
                                                                ota_slot_t slot);
uint32_t portable_ota_port_crc32_update(uint32_t crc, const uint8_t *data, size_t length);
uint32_t portable_ota_port_crc32_compute(const uint8_t *data, size_t length);
bool portable_ota_port_validate_app_vector(uint32_t app_flash_offset,
                                           uint32_t app_size,
                                           uint32_t run_flash_offset);
bool portable_ota_port_core_begin(const ota_metadata_t *metadata,
                                  uint32_t size,
                                  uint32_t crc32,
                                  bool package_mode,
                                  ota_vector_t *vector);
bool portable_ota_port_core_service(uint32_t budget_us, ota_vector_t *vector);
bool portable_ota_port_core_write(const uint8_t *data, uint32_t length, ota_vector_t *vector);
bool portable_ota_port_core_end(ota_vector_t *vector);
bool portable_ota_port_core_abort(ota_vector_t *vector);

#endif
