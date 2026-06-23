#include "portable_ota_port.h"

#include "drv_flash.h"
#include "pota_image.h"

#define RP2350_SRAM_BASE 0x20000000u
#define RP2350_SRAM_END  0x20082000u

bool portable_ota_port_validate_app_vector(uint32_t app_flash_offset,
                                           uint32_t app_size,
                                           uint32_t run_flash_offset)
{
    const pota_image_vector_constraints_t constraints = {
        .sram_base = RP2350_SRAM_BASE,
        .sram_end = RP2350_SRAM_END,
        .xip_base = DRV_FLASH_XIP_BASE,
        .flash_read = drv_flash_read,
    };

    return pota_image_validate_app_vector(app_flash_offset,
                                          app_size,
                                          run_flash_offset,
                                          &constraints);
}
