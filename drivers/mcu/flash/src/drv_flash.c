#include "drv_flash.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

#ifndef PROJECT_USE_MULTICORE
#define PROJECT_USE_MULTICORE 0
#endif

static bool s_lockout_initialized;

static void drv_flash_ensure_lockout_initialized(void)
{
    if (!s_lockout_initialized) {
        drv_flash_lockout_init(PROJECT_USE_MULTICORE != 0);
        s_lockout_initialized = true;
    }
}

static bool drv_flash_is_aligned(uint32_t value, uint32_t alignment)
{
    return (value % alignment) == 0u;
}

void __not_in_flash_func(drv_flash_core1_lockout_poll)(void)
{
    drv_flash_lockout_core1_poll();
}

void drv_flash_get_lockout_status(drv_flash_lockout_status_t *status)
{
    drv_flash_ensure_lockout_initialized();
    drv_flash_lockout_get_status(status);
}

static bool drv_flash_begin_write(void)
{
    drv_flash_ensure_lockout_initialized();
    return drv_flash_lockout_begin(DRV_FLASH_LOCKOUT_DEFAULT_WAIT_LOOPS);
}

static void drv_flash_end_write(void)
{
    drv_flash_lockout_end(DRV_FLASH_LOCKOUT_DEFAULT_WAIT_LOOPS);
}

void drv_flash_set_lockout_fault_injection(uint32_t flags)
{
    drv_flash_ensure_lockout_initialized();
    drv_flash_lockout_set_fault_injection(flags);
}

void drv_flash_clear_lockout_fault_injection(void)
{
    drv_flash_ensure_lockout_initialized();
    drv_flash_lockout_clear_fault_injection();
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

    if (!drv_flash_begin_write()) {
        return false;
    }

    const uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(flash_offset, length);
    restore_interrupts(irq_state);
    drv_flash_end_write();

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

    if (!drv_flash_begin_write()) {
        return false;
    }

    const uint32_t irq_state = save_and_disable_interrupts();
    flash_range_program(flash_offset, data, length);
    restore_interrupts(irq_state);
    drv_flash_end_write();

    return memcmp(drv_flash_xip_ptr(flash_offset), data, length) == 0;
}
