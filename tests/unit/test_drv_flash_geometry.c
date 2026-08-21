#include "drv_flash.h"

#include <stdint.h>
#include <stdio.h>

static int s_failures;

#define CHECK_TRUE(expression) do { \
    if (!(expression)) { \
        (void)printf("FAIL line %d: %s\n", __LINE__, #expression); \
        s_failures++; \
    } \
} while (0)

#define CHECK_FALSE(expression) CHECK_TRUE(!(expression))

void drv_flash_lockout_init(bool supported)
{
    (void)supported;
}

bool drv_flash_lockout_begin(uint32_t wait_loop_budget)
{
    (void)wait_loop_budget;
    return false;
}

void drv_flash_lockout_end(uint32_t wait_loop_budget)
{
    (void)wait_loop_budget;
}

void drv_flash_lockout_core1_poll(void)
{
}

void drv_flash_lockout_get_status(drv_flash_lockout_status_t *status)
{
    (void)status;
}

void drv_flash_lockout_set_fault_injection(uint32_t flags)
{
    (void)flags;
}

void drv_flash_lockout_clear_fault_injection(void)
{
}

uint32_t save_and_disable_interrupts(void)
{
    return 0u;
}

void restore_interrupts(uint32_t status)
{
    (void)status;
}

void flash_range_erase(uint32_t flash_offset, size_t length)
{
    (void)flash_offset;
    (void)length;
}

void flash_range_program(uint32_t flash_offset, const uint8_t *data, size_t length)
{
    (void)flash_offset;
    (void)data;
    (void)length;
}

static void test_geometry_contract(void)
{
    CHECK_TRUE(DRV_FLASH_TOTAL_SIZE_BYTES == 16u * 1024u * 1024u);
    CHECK_TRUE(DRV_FLASH_SECTOR_SIZE == 4096u);
    CHECK_TRUE(DRV_FLASH_PAGE_SIZE == 256u);
    CHECK_TRUE(DRV_FLASH_XIP_BASE == 0x10000000u);
}

static void test_overflow_safe_ranges(void)
{
    CHECK_FALSE(drv_flash_is_range_valid(0u, 0u));
    CHECK_TRUE(drv_flash_is_range_valid(0u, 1u));
    CHECK_TRUE(drv_flash_is_range_valid(DRV_FLASH_TOTAL_SIZE_BYTES - 1u, 1u));
    CHECK_FALSE(drv_flash_is_range_valid(DRV_FLASH_TOTAL_SIZE_BYTES - 1u, 2u));
    CHECK_FALSE(drv_flash_is_range_valid(DRV_FLASH_TOTAL_SIZE_BYTES, 1u));
    CHECK_FALSE(drv_flash_is_range_valid(UINT32_MAX, 1u));
    CHECK_FALSE(drv_flash_is_range_valid(DRV_FLASH_TOTAL_SIZE_BYTES - 1u, SIZE_MAX));
    CHECK_TRUE(drv_flash_is_range_valid(4u * 1024u * 1024u, DRV_FLASH_SECTOR_SIZE));
}

static void test_pointer_and_invalid_write_boundaries(void)
{
    CHECK_TRUE((uintptr_t)drv_flash_xip_ptr(DRV_FLASH_TOTAL_SIZE_BYTES - 1u) ==
               (uintptr_t)DRV_FLASH_XIP_BASE + DRV_FLASH_TOTAL_SIZE_BYTES - 1u);
    CHECK_TRUE(drv_flash_xip_ptr(DRV_FLASH_TOTAL_SIZE_BYTES) == NULL);
    CHECK_FALSE(drv_flash_read(0u, NULL, 1u));
    CHECK_FALSE(drv_flash_erase(1u, DRV_FLASH_SECTOR_SIZE));
    CHECK_FALSE(drv_flash_erase(0u, DRV_FLASH_SECTOR_SIZE - 1u));
    CHECK_FALSE(drv_flash_erase(0u, 0u));
    CHECK_FALSE(drv_flash_program(0u, NULL, DRV_FLASH_PAGE_SIZE));
    CHECK_FALSE(drv_flash_program(1u, (const uint8_t *)1, DRV_FLASH_PAGE_SIZE));
    CHECK_FALSE(drv_flash_program(0u, (const uint8_t *)1, DRV_FLASH_PAGE_SIZE - 1u));
    CHECK_FALSE(drv_flash_program(DRV_FLASH_TOTAL_SIZE_BYTES, (const uint8_t *)1,
                                  DRV_FLASH_PAGE_SIZE));
}

int main(void)
{
    test_geometry_contract();
    test_overflow_safe_ranges();
    test_pointer_and_invalid_write_boundaries();
    if (s_failures != 0) {
        (void)printf("drv_flash geometry tests failed: %d\n", s_failures);
        return 1;
    }
    (void)printf("drv_flash geometry tests passed\n");
    return 0;
}
