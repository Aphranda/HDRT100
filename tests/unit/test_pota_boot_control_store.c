#include "pota_boot_control_store.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_LANE_PAGES 5u

typedef struct {
    uint8_t pages[POTA_BCB_LANE_COUNT][TEST_LANE_PAGES][POTA_BCB_PAGE_SIZE];
    uint32_t fail_lane;
    uint32_t fail_page;
    bool fail_program;
    bool fail_erase;
    uint32_t corrupt_lane;
    uint32_t corrupt_page;
    bool corrupt_once;
} fake_flash_t;

static bool fake_read(void *context, uint32_t lane, uint32_t page,
                      uint8_t *data, uint32_t length)
{
    fake_flash_t *flash = context;
    assert(flash != NULL && data != NULL && length == POTA_BCB_PAGE_SIZE);
    if (lane >= POTA_BCB_LANE_COUNT || page >= TEST_LANE_PAGES) {
        return false;
    }
    memcpy(data, flash->pages[lane][page], length);
    if (flash->corrupt_once && lane == flash->corrupt_lane &&
        page == flash->corrupt_page) {
        data[0] ^= 0x01u;
        flash->corrupt_once = false;
    }
    return true;
}

static bool fake_program(void *context, uint32_t lane, uint32_t page,
                         const uint8_t *data, uint32_t length)
{
    fake_flash_t *flash = context;
    assert(flash != NULL && data != NULL && length == POTA_BCB_PAGE_SIZE);
    if (lane >= POTA_BCB_LANE_COUNT || page >= TEST_LANE_PAGES ||
        (flash->fail_program && lane == flash->fail_lane &&
         page == flash->fail_page)) {
        return false;
    }
    memcpy(flash->pages[lane][page], data, length);
    return true;
}

static bool fake_erase(void *context, uint32_t lane)
{
    fake_flash_t *flash = context;
    assert(flash != NULL);
    if (lane >= POTA_BCB_LANE_COUNT || (flash->fail_erase && lane == flash->fail_lane)) {
        return false;
    }
    memset(flash->pages[lane], 0xFF, sizeof(flash->pages[lane]));
    return true;
}

static void fake_init(fake_flash_t *flash)
{
    memset(flash, 0, sizeof(*flash));
    memset(flash->pages, 0xFF, sizeof(flash->pages));
    flash->fail_lane = UINT32_MAX;
    flash->fail_page = UINT32_MAX;
    flash->corrupt_lane = UINT32_MAX;
    flash->corrupt_page = UINT32_MAX;
}

static pota_bcb_store_t make_store(fake_flash_t *flash)
{
    const pota_bcb_platform_t platform = {
        .context = flash,
        .read_page = fake_read,
        .program_page = fake_program,
        .erase_lane = fake_erase,
    };
    pota_bcb_store_t store;
    assert(pota_bcb_store_init(&store, &platform, 1u, 2u,
                               TEST_LANE_PAGES) == POTA_BCB_RESULT_OK);
    return store;
}

static pota_bcb_update_t update(uint32_t sequence, uint8_t value)
{
    pota_bcb_update_t result;
    memset(&result, 0, sizeof(result));
    result.sequence = sequence;
    result.boot_generation = sequence;
    result.security_counter = sequence;
    result.payload_length = 4u;
    memset(result.payload, value, result.payload_length);
    return result;
}

static void test_append_select_and_replay(void)
{
    fake_flash_t flash;
    fake_init(&flash);
    pota_bcb_store_t store = make_store(&flash);
    pota_bcb_view_t view;

    pota_bcb_update_t first = update(1u, 0xA1u);
    assert(pota_bcb_store_append(&store, &first, &view) == POTA_BCB_RESULT_OK);
    assert(view.lane == 0u && view.record_page == 0u);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_OK);
    assert(view.update.sequence == 1u && view.lane_generation == 1u);

    pota_bcb_update_t second = update(2u, 0xB2u);
    assert(pota_bcb_store_append(&store, &second, &view) == POTA_BCB_RESULT_OK);
    assert(view.record_page == 2u);
    assert(pota_bcb_store_append(&store, &second, &view) == POTA_BCB_RESULT_REPLAY);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_OK);
    assert(view.update.sequence == 2u && view.update.payload[0] == 0xB2u);

    pota_bcb_wear_snapshot_t wear;
    assert(pota_bcb_store_get_wear_snapshot(&store, &wear));
    assert(wear.program_page_count == 5u);
    assert(wear.erase_lane_count == 1u);

    pota_bcb_update_t rollback = update(3u, 0xC3u);
    rollback.security_counter = 1u;
    assert(pota_bcb_store_append(&store, &rollback, &view) == POTA_BCB_RESULT_POLICY);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_OK);
    assert(view.update.sequence == 2u && view.update.security_counter == 2u);
}

static void test_fault_boundaries_fail_closed(void)
{
    fake_flash_t flash;
    pota_bcb_view_t view;
    pota_bcb_update_t first = update(1u, 0x11u);

    fake_init(&flash);
    pota_bcb_store_t store = make_store(&flash);
    flash.fail_program = true;
    flash.fail_lane = 0u;
    flash.fail_page = 0u;
    assert(pota_bcb_store_append(&store, &first, &view) == POTA_BCB_RESULT_VERIFY);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_NO_VALID);

    fake_init(&flash);
    store = make_store(&flash);
    flash.fail_program = true;
    flash.fail_lane = 0u;
    flash.fail_page = 1u;
    assert(pota_bcb_store_append(&store, &first, &view) == POTA_BCB_RESULT_VERIFY);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_NO_VALID);

    fake_init(&flash);
    store = make_store(&flash);
    flash.corrupt_once = true;
    flash.corrupt_lane = 0u;
    flash.corrupt_page = 0u;
    assert(pota_bcb_store_append(&store, &first, &view) == POTA_BCB_RESULT_VERIFY);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_NO_VALID);

    fake_init(&flash);
    store = make_store(&flash);
    flash.fail_program = true;
    flash.fail_lane = 0u;
    flash.fail_page = TEST_LANE_PAGES - 1u;
    assert(pota_bcb_store_append(&store, &first, &view) == POTA_BCB_RESULT_VERIFY);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_NO_VALID);
}

static void test_gc_preserves_old_until_new_lane_sealed(void)
{
    fake_flash_t flash;
    fake_init(&flash);
    pota_bcb_store_t store = make_store(&flash);
    pota_bcb_view_t view;
    for (uint32_t sequence = 1u; sequence <= 4u; sequence++) {
        pota_bcb_update_t next = update(sequence, (uint8_t)sequence);
        assert(pota_bcb_store_append(&store, &next, &view) == POTA_BCB_RESULT_OK);
    }
    assert(view.lane == 1u && view.record_page == 2u);

    pota_bcb_update_t fifth = update(5u, 5u);
    assert(pota_bcb_store_append(&store, &fifth, &view) == POTA_BCB_RESULT_OK);
    assert(view.lane == 0u && view.record_page == 0u && view.lane_generation == 3u);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_OK);
    assert(view.update.sequence == 5u);

    fake_init(&flash);
    store = make_store(&flash);
    pota_bcb_update_t seed = update(1u, 1u);
    assert(pota_bcb_store_append(&store, &seed, &view) == POTA_BCB_RESULT_OK);
    seed = update(2u, 2u);
    assert(pota_bcb_store_append(&store, &seed, &view) == POTA_BCB_RESULT_OK);
    flash.fail_erase = true;
    flash.fail_lane = 1u;
    seed = update(3u, 3u);
    assert(pota_bcb_store_append(&store, &seed, &view) == POTA_BCB_RESULT_IO);
    assert(pota_bcb_store_select_newest(&store, &view) == POTA_BCB_RESULT_OK);
    assert(view.update.sequence == 2u);
}

int main(void)
{
    test_append_select_and_replay();
    test_fault_boundaries_fail_closed();
    test_gc_preserves_old_until_new_lane_sealed();
    puts("portable BCB store tests passed");
    return 0;
}
