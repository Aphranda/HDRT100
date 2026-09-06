#include "ota_metadata_flash.h"

#include "boot_flash_service.h"

bool ota_metadata_flash_erase(uint32_t flash_offset, size_t length)
{
    return boot_flash_service_erase(flash_offset, length);
}

bool ota_metadata_flash_program(uint32_t flash_offset, const uint8_t *data,
                                size_t length)
{
    return boot_flash_service_program(flash_offset, data, length);
}

pota_bcb_step_result_t ota_metadata_flash_program_step(
    uint32_t flash_offset, const uint8_t *data, size_t length)
{
    return ota_metadata_flash_program(flash_offset, data, length)
               ? POTA_BCB_STEP_DONE
               : POTA_BCB_STEP_FAILED;
}

pota_bcb_step_result_t ota_metadata_flash_erase_step(
    uint32_t flash_offset, size_t length)
{
    return ota_metadata_flash_erase(flash_offset, length)
               ? POTA_BCB_STEP_DONE
               : POTA_BCB_STEP_FAILED;
}
