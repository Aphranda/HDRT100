#include "ota_metadata_flash.h"

#include "drv_flash.h"

bool ota_metadata_flash_erase(uint32_t flash_offset, size_t length)
{
    return drv_flash_erase(flash_offset, length);
}

bool ota_metadata_flash_program(uint32_t flash_offset, const uint8_t *data,
                                size_t length)
{
    return drv_flash_program(flash_offset, data, length);
}
