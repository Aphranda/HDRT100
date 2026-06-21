#include "ota_image.h"

#include "drv_flash.h"

#define RP2350_SRAM_BASE 0x20000000u
#define RP2350_SRAM_END  0x20082000u

bool ota_image_validate_app_vector(uint32_t app_flash_offset, uint32_t app_size, uint32_t run_flash_offset)
{
    if (app_size < 8u) {
        return false;
    }

    uint32_t vector[2];
    if (!drv_flash_read(app_flash_offset, vector, sizeof(vector))) {
        return false;
    }

    const uint32_t initial_sp = vector[0];
    const uint32_t reset_handler = vector[1];
    const uint32_t app_xip_base = DRV_FLASH_XIP_BASE + run_flash_offset;
    const uint32_t app_xip_end = app_xip_base + app_size;

    if (initial_sp < RP2350_SRAM_BASE || initial_sp > RP2350_SRAM_END) {
        return false;
    }

    if ((reset_handler & 1u) == 0u) {
        return false;
    }

    const uint32_t reset_addr = reset_handler & ~1u;
    return reset_addr >= app_xip_base && reset_addr < app_xip_end;
}
