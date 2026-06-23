#include "ota_image.h"

#include "portable_ota_port.h"

bool ota_image_validate_app_vector(uint32_t app_flash_offset, uint32_t app_size, uint32_t run_flash_offset)
{
    return portable_ota_port_validate_app_vector(app_flash_offset, app_size, run_flash_offset);
}
