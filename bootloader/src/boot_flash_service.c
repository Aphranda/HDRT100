#include "boot_flash_service.h"

#include "drv_flash_write.h"
#include "flash_deployment_map.h"

static bool boot_write_range_valid(uint32_t flash_offset, size_t length,
                                   uint32_t alignment)
{
    if (length == 0u || length > UINT32_MAX ||
        (flash_offset % alignment) != 0u ||
        ((uint32_t)length % alignment) != 0u) {
        return false;
    }

    const uint32_t ranges[][2] = {
        {FLASH_DEPLOYMENT_MAP_APP_A_OFFSET, FLASH_DEPLOYMENT_MAP_APP_A_SIZE},
        {FLASH_DEPLOYMENT_MAP_APP_B_OFFSET, FLASH_DEPLOYMENT_MAP_APP_B_SIZE},
        {FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_OFFSET, FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_SIZE},
    };
    for (size_t index = 0u; index < sizeof(ranges) / sizeof(ranges[0]); ++index) {
        const uint32_t origin = ranges[index][0];
        const uint32_t size = ranges[index][1];
        if (flash_offset >= origin && flash_offset - origin <= size &&
            (uint32_t)length <= size - (flash_offset - origin)) {
            return true;
        }
    }
    return false;
}

bool boot_flash_service_erase(uint32_t flash_offset, size_t length)
{
    return boot_write_range_valid(flash_offset, length,
                                  FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE) &&
           drv_flash_erase(flash_offset, length);
}

bool boot_flash_service_program(uint32_t flash_offset, const uint8_t *data,
                                size_t length)
{
    return data != NULL &&
           boot_write_range_valid(flash_offset, length,
                                  FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE) &&
           drv_flash_program(flash_offset, data, length);
}
