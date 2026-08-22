#include "pota_stream_session.h"

#include <stdio.h>
#include <string.h>

#define MOCK_FLASH_SIZE 8192u
#define MOCK_SLOT_A_OFFSET 0u
#define MOCK_SLOT_B_OFFSET 4096u
#define MOCK_SLOT_SIZE 2048u
#define MOCK_PAGE_SIZE 16u
#define MOCK_SECTOR_SIZE 256u

static uint8_t s_flash[MOCK_FLASH_SIZE];
static uint32_t s_pending_count;

static bool flash_read(uint32_t offset, void *buffer, uint32_t size)
{
    if (buffer == NULL || offset > MOCK_FLASH_SIZE || size > MOCK_FLASH_SIZE - offset) {
        return false;
    }
    memcpy(buffer, &s_flash[offset], size);
    return true;
}

static bool flash_erase(uint32_t offset, uint32_t size)
{
    if (offset > MOCK_FLASH_SIZE || size > MOCK_FLASH_SIZE - offset ||
        (offset % MOCK_SECTOR_SIZE) != 0u || (size % MOCK_SECTOR_SIZE) != 0u) {
        return false;
    }
    memset(&s_flash[offset], 0xFF, size);
    return true;
}

static bool flash_program(uint32_t offset, const void *data, uint32_t size)
{
    if (data == NULL || offset > MOCK_FLASH_SIZE || size > MOCK_FLASH_SIZE - offset ||
        (offset % MOCK_PAGE_SIZE) != 0u || (size % MOCK_PAGE_SIZE) != 0u) {
        return false;
    }
    memcpy(&s_flash[offset], data, size);
    return true;
}

static bool mark_pending(pota_slot_t slot, uint32_t size, uint32_t crc32)
{
    (void)slot;
    (void)size;
    (void)crc32;
    s_pending_count++;
    return true;
}

static bool confirm_active(void)
{
    return true;
}

static bool validate_vector(uint32_t offset, uint32_t size, uint32_t run_offset)
{
    return offset == MOCK_SLOT_B_OFFSET && size != 0u && run_offset != 0u;
}

static bool expect(const char *name, bool condition)
{
    if (!condition) {
        (void)printf("FAIL %s\n", name);
        return false;
    }
    return true;
}

int main(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    const pota_platform_t platform = {
        .info = {
            .product_id = "DHRT100",
            .hardware_id = "dhrt100",
            .bootloader_version = POTA_PACK_VERSION(0, 1, 0),
            .boot_mode = POTA_BOOT_MODE_DIRECT_AB,
            .active_slot = POTA_SLOT_A,
            .slot_a = {MOCK_SLOT_A_OFFSET, MOCK_SLOT_SIZE, 0x10040000u},
            .slot_b = {MOCK_SLOT_B_OFFSET, MOCK_SLOT_SIZE, 0x101C0000u},
            .flash_page_size = MOCK_PAGE_SIZE,
            .flash_sector_size = MOCK_SECTOR_SIZE,
        },
        .ops = {
            .flash_read = flash_read,
            .flash_erase = flash_erase,
            .flash_program = flash_program,
            .mark_pending = mark_pending,
            .confirm_active = confirm_active,
            .validate_vector = validate_vector,
        },
    };
    pota_stream_session_t session;
    int failed = 0;
    failed += !expect("init", pota_stream_session_init(&session, &platform));

    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    open.session_id = 7u;
    open.generation = 3u;
    open.capability_mask = POTA_STREAM_CAP_INACTIVE_WRITE | POTA_STREAM_CAP_DURABLE_ACK;
    open.map_version = 1u;
    open.partition_id = POTA_STREAM_PARTITION_APP_B;
    open.destination_slot = POTA_SLOT_B;
    open.object_id = 11u;
    open.total_size = 32u;
    open.package_crc32 = pota_crc32_compute("01234567890123456789012345678901", 32u);
    open.identity[0] = 0xA5u;
    open.package_hash[0] = 0x5Au;
    failed += !expect("open", pota_stream_session_open(&session, &open) == POTA_STREAM_RESULT_OK);
    failed += !expect("wrong state before service",
                      pota_stream_session_write(&session, 0u, (const uint8_t *)"01234567890123456789012345678901", 16u) ==
                          POTA_STREAM_RESULT_INVALID_STATE);
    while (pota_stream_session_state(&session) == POTA_STREAM_STATE_OPEN) {
        failed += !expect("service", pota_stream_session_service(&session, 100u) == POTA_STREAM_RESULT_OK);
    }

    const uint8_t first[17] = "0123456789012345";
    const uint8_t second[17] = "6789012345678901";
    const uint32_t stream_token = pota_stream_session_token(&session);
    failed += !expect("first write", pota_stream_session_write(&session, 0u, first, 16u) == POTA_STREAM_RESULT_OK);
    failed += !expect("offset reject", pota_stream_session_write(&session, 32u, second, 16u) == POTA_STREAM_RESULT_OFFSET);
    failed += !expect("second write", pota_stream_session_write(&session, 16u, second, 16u) == POTA_STREAM_RESULT_OK);
    failed += !expect("duplicate accepted", pota_stream_session_write(&session, 16u, second, 16u) == POTA_STREAM_RESULT_OK);
    failed += !expect("conflict reject", pota_stream_session_write(&session, 16u, first, 16u) == POTA_STREAM_RESULT_CONFLICT);
    failed += !expect("durable offset", pota_stream_session_durable_offset(&session) == 32u);
    failed += !expect("stable token", stream_token != 0u &&
                      pota_stream_session_token(&session) == stream_token);
    failed += !expect("close", pota_stream_session_close(&session) == POTA_STREAM_RESULT_OK);
    failed += !expect("pending once", s_pending_count == 1u);
    failed += !expect("abort after close rejected", pota_stream_session_abort(&session) == POTA_STREAM_RESULT_INVALID_STATE);
    if (failed != 0) {
        return 1;
    }
    (void)printf("pota stream session tests passed\n");
    return 0;
}
