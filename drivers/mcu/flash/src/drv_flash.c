#include "drv_flash.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"

static bool drv_flash_is_aligned(uint32_t value, uint32_t alignment)
{
    return (value % alignment) == 0u;
}

bool drv_flash_is_range_valid(uint32_t flash_offset, size_t length)
{
    if (length == 0u) {
        return false;
    }

    if (flash_offset >= DRV_FLASH_TOTAL_SIZE_BYTES) {
        return false;
    }

    return length <= (size_t)(DRV_FLASH_TOTAL_SIZE_BYTES - flash_offset);
}

const uint8_t *drv_flash_xip_ptr(uint32_t flash_offset)
{
    if (flash_offset >= DRV_FLASH_TOTAL_SIZE_BYTES) {
        return NULL;
    }

    return (const uint8_t *)(uintptr_t)(DRV_FLASH_XIP_BASE + flash_offset);
}

bool drv_flash_read(uint32_t flash_offset, void *data, size_t length)
{
    if (data == NULL || !drv_flash_is_range_valid(flash_offset, length)) {
        return false;
    }

    memcpy(data, drv_flash_xip_ptr(flash_offset), length);
    return true;
}

bool drv_flash_is_erased(uint32_t flash_offset, size_t length)
{
    if (!drv_flash_is_range_valid(flash_offset, length)) {
        return false;
    }

    const uint8_t *ptr = drv_flash_xip_ptr(flash_offset);
    for (size_t i = 0u; i < length; i++) {
        if (ptr[i] != 0xFFu) {
            return false;
        }
    }

    return true;
}

bool drv_flash_erase(uint32_t flash_offset, size_t length)
{
    if (!drv_flash_is_range_valid(flash_offset, length)) {
        return false;
    }

    if (!drv_flash_is_aligned(flash_offset, DRV_FLASH_SECTOR_SIZE) ||
        !drv_flash_is_aligned((uint32_t)length, DRV_FLASH_SECTOR_SIZE)) {
        return false;
    }

    const uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(flash_offset, length);
    restore_interrupts(irq_state);

    return drv_flash_is_erased(flash_offset, length);
}

bool drv_flash_program(uint32_t flash_offset, const uint8_t *data, size_t length)
{
    if (data == NULL || !drv_flash_is_range_valid(flash_offset, length)) {
        return false;
    }

    if (!drv_flash_is_aligned(flash_offset, DRV_FLASH_PAGE_SIZE) ||
        !drv_flash_is_aligned((uint32_t)length, DRV_FLASH_PAGE_SIZE)) {
        return false;
    }

    const uint32_t irq_state = save_and_disable_interrupts();
    flash_range_program(flash_offset, data, length);
    restore_interrupts(irq_state);

    return memcmp(drv_flash_xip_ptr(flash_offset), data, length) == 0;
}
