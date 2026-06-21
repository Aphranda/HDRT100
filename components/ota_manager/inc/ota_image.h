#ifndef OTA_IMAGE_H
#define OTA_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

bool ota_image_validate_app_vector(uint32_t app_flash_offset, uint32_t app_size, uint32_t run_flash_offset);

#endif
