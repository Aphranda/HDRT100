#include "drv_flash_write.h"

#include <stdint.h>
#include <stdio.h>

static int s_failures;
static uint32_t s_lockout_result;
static bool s_lockout_begin_ok;
static uint8_t s_jedec_response[4] = {0u, 0xEFu, 0x40u, 0x18u};

void watchdog_update(void)
{
}

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
    return s_lockout_begin_ok;
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
    if (status != NULL) {
        status->core1_lockout_requested = false;
        status->core1_lockout_acknowledged = false;
        status->last_result = s_lockout_result;
    }
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

void flash_do_cmd(const uint8_t *txbuf, uint8_t *rxbuf, size_t count)
{
    CHECK_TRUE(txbuf != NULL);
    CHECK_TRUE(rxbuf != NULL);
    CHECK_TRUE(count == sizeof(s_jedec_response));
    CHECK_TRUE(txbuf[0] == DRV_FLASH_JEDEC_RDID_COMMAND);
    if (rxbuf != NULL && count == sizeof(s_jedec_response)) {
        for (size_t index = 0u; index < count; ++index) {
            rxbuf[index] = s_jedec_response[index];
        }
    }
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

static void test_parked_write_requires_session(void)
{
    s_lockout_begin_ok = false;
    CHECK_FALSE(drv_flash_write_session_begin());
    CHECK_FALSE(drv_flash_erase_parked(0u, DRV_FLASH_SECTOR_SIZE));
    CHECK_FALSE(drv_flash_program_parked(0u, (const uint8_t *)1,
                                         DRV_FLASH_PAGE_SIZE));

    s_lockout_begin_ok = true;
    s_lockout_result = DRV_FLASH_LOCKOUT_RESULT_ACKED;
    CHECK_TRUE(drv_flash_write_session_begin());
    CHECK_FALSE(drv_flash_write_session_begin());
    CHECK_TRUE(drv_flash_write_session_end());
    CHECK_FALSE(drv_flash_write_session_end());
}

static void test_jedec_id_uses_lockout_and_matches_geometry(void)
{
    drv_flash_jedec_id_t jedec = {
        .raw_id = UINT32_MAX,
        .capacity_bytes = UINT32_MAX,
        .manufacturer_id = UINT8_MAX,
        .memory_type = UINT8_MAX,
        .capacity_code = UINT8_MAX,
        .capacity_matches_geometry = true,
    };
    s_lockout_begin_ok = false;
    CHECK_FALSE(drv_flash_read_jedec_id(&jedec));
    CHECK_TRUE(jedec.raw_id == 0u);
    CHECK_TRUE(jedec.capacity_bytes == 0u);
    CHECK_FALSE(jedec.capacity_matches_geometry);

    s_lockout_begin_ok = true;
    s_lockout_result = DRV_FLASH_LOCKOUT_RESULT_ACKED;
    CHECK_TRUE(drv_flash_read_jedec_id(&jedec));
    CHECK_TRUE(jedec.raw_id == 0x00EF4018u);
    CHECK_TRUE(jedec.manufacturer_id == 0xEFu);
    CHECK_TRUE(jedec.memory_type == 0x40u);
    CHECK_TRUE(jedec.capacity_code == 0x18u);
    CHECK_TRUE(jedec.capacity_bytes == DRV_FLASH_TOTAL_SIZE_BYTES);
    CHECK_TRUE(jedec.capacity_matches_geometry);

    s_jedec_response[1] = 0xFFu;
    s_jedec_response[2] = 0xFFu;
    s_jedec_response[3] = 0xFFu;
    CHECK_FALSE(drv_flash_read_jedec_id(&jedec));
}

int main(void)
{
    test_geometry_contract();
    test_overflow_safe_ranges();
    test_pointer_and_invalid_write_boundaries();
    test_parked_write_requires_session();
    test_jedec_id_uses_lockout_and_matches_geometry();
    if (s_failures != 0) {
        (void)printf("drv_flash geometry tests failed: %d\n", s_failures);
        return 1;
    }
    (void)printf("drv_flash geometry tests passed\n");
    return 0;
}
