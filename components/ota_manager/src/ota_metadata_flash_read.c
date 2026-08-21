#include "ota_metadata_flash.h"

#include "drv_flash.h"

bool ota_metadata_flash_read(uint32_t flash_offset, void *data, size_t length)
{
    return drv_flash_read(flash_offset, data, length);
}
