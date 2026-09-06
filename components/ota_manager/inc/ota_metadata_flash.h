#ifndef OTA_METADATA_FLASH_H
#define OTA_METADATA_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pota_boot_control_store.h"

/* App and Boot provide different implementations behind this boundary. */
bool ota_metadata_flash_erase(uint32_t flash_offset, size_t length);
bool ota_metadata_flash_program(uint32_t flash_offset,
                                const uint8_t *data,
                                size_t length);
bool ota_metadata_flash_read(uint32_t flash_offset, void *data, size_t length);
pota_bcb_step_result_t ota_metadata_flash_program_step(
    uint32_t flash_offset, const uint8_t *data, size_t length);
pota_bcb_step_result_t ota_metadata_flash_erase_step(
    uint32_t flash_offset, size_t length);

#endif
