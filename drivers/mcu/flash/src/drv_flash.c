#include "drv_flash.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

#if PROJECT_USE_MULTICORE
#define DRV_FLASH_CORE1_LOCKOUT_WAIT_LOOPS 1000000u

static volatile bool s_core1_lockout_online;
static volatile bool s_core1_lockout_request;
static volatile bool s_core1_lockout_ack;
#endif

static bool drv_flash_is_aligned(uint32_t value, uint32_t alignment)
{
    return (value % alignment) == 0u;
}

void __not_in_flash_func(drv_flash_core1_lockout_poll)(void)
{
#if PROJECT_USE_MULTICORE
    s_core1_lockout_online = true;
    if (!s_core1_lockout_request) {
        return;
    }

    s_core1_lockout_ack = true;
    __asm volatile("sev");
    while (s_core1_lockout_request) {
        __asm volatile("wfe");
    }
    s_core1_lockout_ack = false;
    __asm volatile("sev");
#endif
}

void drv_flash_get_lockout_status(drv_flash_lockout_status_t *status)
{
    if (status == NULL) {
        return;
    }

#if PROJECT_USE_MULTICORE
    status->core1_lockout_supported = true;
    status->core1_lockout_online = s_core1_lockout_online;
    status->core1_lockout_requested = s_core1_lockout_request;
    status->core1_lockout_acknowledged = s_core1_lockout_ack;
    status->wait_loop_budget = DRV_FLASH_CORE1_LOCKOUT_WAIT_LOOPS;
#else
    status->core1_lockout_supported = false;
    status->core1_lockout_online = false;
    status->core1_lockout_requested = false;
    status->core1_lockout_acknowledged = false;
    status->wait_loop_budget = 0u;
#endif
}

static bool drv_flash_begin_write(void)
{
#if PROJECT_USE_MULTICORE
    if (!s_core1_lockout_online) {
        return true;
    }

    s_core1_lockout_request = true;
    __asm volatile("sev");
    for (uint32_t i = 0u; i < DRV_FLASH_CORE1_LOCKOUT_WAIT_LOOPS; i++) {
        if (s_core1_lockout_ack) {
            return true;
        }
        __asm volatile("nop");
    }

    s_core1_lockout_request = false;
    __asm volatile("sev");
    return false;
#else
    return true;
#endif
}

static void drv_flash_end_write(void)
{
#if PROJECT_USE_MULTICORE
    s_core1_lockout_request = false;
    __asm volatile("sev");
    for (uint32_t i = 0u; i < DRV_FLASH_CORE1_LOCKOUT_WAIT_LOOPS; i++) {
        if (!s_core1_lockout_ack) {
            break;
        }
        __asm volatile("nop");
    }
#endif
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
