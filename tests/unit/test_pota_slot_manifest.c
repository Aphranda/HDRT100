#include "pota_slot_manifest.h"

#include <stdio.h>
#include <string.h>

#define FLASH_BYTES (POTA_SLOT_MANIFEST_LANE_COUNT * POTA_SLOT_MANIFEST_RECORD_SIZE)

static uint8_t s_flash[FLASH_BYTES];
static uint32_t s_program_calls;
static uint32_t s_fail_program_call;
static uint32_t s_partial_bytes;
static uint32_t s_erase_calls;

static bool flash_read(void *context, uint32_t offset, void *data,
                       uint32_t length)
{
    (void)context;
    if (data == NULL || offset > sizeof(s_flash) ||
        length > sizeof(s_flash) - offset) {
        return false;
    }
    memcpy(data, &s_flash[offset], length);
    return true;
}

static bool flash_program(void *context, uint32_t offset, const void *data,
                          uint32_t length)
{
    (void)context;
    if (data == NULL || offset > sizeof(s_flash) ||
        length > sizeof(s_flash) - offset) {
        return false;
    }
    s_program_calls++;
    if (s_fail_program_call != 0u && s_program_calls == s_fail_program_call) {
        if (s_partial_bytes != 0u) {
            const uint32_t count = s_partial_bytes < length ?
                s_partial_bytes : length;
            memcpy(&s_flash[offset], data, count);
        }
        return false;
    }
    memcpy(&s_flash[offset], data, length);
    return true;
}

static bool flash_erase(void *context, uint32_t offset, uint32_t length)
{
    (void)context;
    if (offset > sizeof(s_flash) || length > sizeof(s_flash) - offset) {
        return false;
    }
    s_erase_calls++;
    memset(&s_flash[offset], 0xFF, length);
    return true;
}

static bool expect(const char *name, bool condition)
{
    if (!condition) {
        (void)printf("FAIL %s\n", name);
        return false;
    }
    return true;
}

static pota_slot_manifest_config_t config(void)
{
    return (pota_slot_manifest_config_t){
        .context = NULL,
        .read = flash_read,
        .program = flash_program,
        .erase = flash_erase,
        .base_offset = 0u,
        .lane_size = POTA_SLOT_MANIFEST_RECORD_SIZE,
        .page_size = 256u,
        .erase_size = POTA_SLOT_MANIFEST_RECORD_SIZE,
        .map_version = 2u,
        .slot = POTA_SLOT_A,
    };
}

static bool test_async_transaction_is_one_physical_step(void)
{
    uint8_t header[POTA_PACKAGE_HEADER_SIZE];
    memset(header, 0x33, sizeof(header));
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    s_partial_bytes = 0u;
    s_erase_calls = 0u;

    pota_slot_manifest_store_t store;
    const pota_slot_manifest_config_t cfg = config();
    if (!expect("async init", pota_slot_manifest_init(&store, &cfg) ==
                POTA_SLOT_MANIFEST_OK)) {
        return false;
    }

    pota_slot_manifest_txn_t txn;
    if (!expect("async begin", pota_slot_manifest_txn_begin(
                    &txn, &store, header) == POTA_SLOT_MANIFEST_OK)) {
        return false;
    }

    uint32_t previous_physical = 0u;
    pota_slot_manifest_step_result_t result =
        POTA_SLOT_MANIFEST_STEP_PENDING;
    for (uint32_t step = 0u;
         step < 32u && result == POTA_SLOT_MANIFEST_STEP_PENDING; step++) {
        result = pota_slot_manifest_txn_step(&txn);
        const uint32_t physical = s_program_calls + s_erase_calls;
        if (!expect("async physical bound",
                    physical <= previous_physical + 1u)) {
            return false;
        }
        previous_physical = physical;
    }
    if (!expect("async done", result == POTA_SLOT_MANIFEST_STEP_DONE)) {
        return false;
    }

    pota_slot_manifest_t loaded;
    return expect("async load", pota_slot_manifest_load(&store, &loaded) ==
                  POTA_SLOT_MANIFEST_OK && loaded.sequence == 1u &&
                  memcmp(loaded.header, header, sizeof(header)) == 0);
}

int main(void)
{
    uint8_t header_a[POTA_PACKAGE_HEADER_SIZE];
    uint8_t header_b[POTA_PACKAGE_HEADER_SIZE];
    memset(header_a, 0x11, sizeof(header_a));
    memset(header_b, 0x22, sizeof(header_b));
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    s_partial_bytes = 0u;
    s_erase_calls = 0u;

    pota_slot_manifest_store_t store;
    const pota_slot_manifest_config_t cfg = config();
    int failed = 0;
    failed += !expect("init", pota_slot_manifest_init(&store, &cfg) ==
                      POTA_SLOT_MANIFEST_OK);

    pota_slot_manifest_t loaded;
    failed += !expect("empty", pota_slot_manifest_load(&store, &loaded) ==
                      POTA_SLOT_MANIFEST_NO_VALID);
    failed += !expect("append first", pota_slot_manifest_append(
                      &store, header_a, &loaded) == POTA_SLOT_MANIFEST_OK);
    failed += !expect("first sequence", loaded.sequence == 1u && loaded.lane == 0u &&
                      memcmp(loaded.header, header_a, sizeof(header_a)) == 0);

    const uint32_t erase_after_first = s_erase_calls;
    failed += !expect("idempotent", pota_slot_manifest_append(
                      &store, header_a, NULL) == POTA_SLOT_MANIFEST_OK &&
                      s_erase_calls == erase_after_first);
    failed += !expect("append second", pota_slot_manifest_append(
                      &store, header_b, &loaded) == POTA_SLOT_MANIFEST_OK);
    failed += !expect("second sequence", loaded.sequence == 2u && loaded.lane == 1u &&
                      memcmp(loaded.header, header_b, sizeof(header_b)) == 0);

    /* A torn commit marker must leave the previous lane as the newest valid one. */
    s_fail_program_call = s_program_calls + 4u;
    s_partial_bytes = 8u;
    failed += !expect("torn append", pota_slot_manifest_append(
                      &store, header_a, NULL) == POTA_SLOT_MANIFEST_IO);
    s_fail_program_call = 0u;
    s_partial_bytes = 0u;
    failed += !expect("rollback to prior lane", pota_slot_manifest_load(
                      &store, &loaded) == POTA_SLOT_MANIFEST_OK &&
                      loaded.sequence == 2u && loaded.lane == 1u &&
                      memcmp(loaded.header, header_b, sizeof(header_b)) == 0);

    failed += !expect("append after torn", pota_slot_manifest_append(
                      &store, header_a, &loaded) == POTA_SLOT_MANIFEST_OK &&
                      loaded.sequence == 3u && loaded.lane == 0u);

    /* A corrupt commit marker is ignored rather than accepted as a record. */
    s_flash[POTA_SLOT_MANIFEST_BODY_SIZE + 24u] ^= 0x01u;
    failed += !expect("corrupt newest lane", pota_slot_manifest_load(
                      &store, &loaded) == POTA_SLOT_MANIFEST_OK &&
                      loaded.sequence == 2u && loaded.lane == 1u &&
                      memcmp(loaded.header, header_b, sizeof(header_b)) == 0);

    pota_slot_manifest_config_t bad = cfg;
    bad.page_size = 128u;
    failed += !expect("bad geometry", pota_slot_manifest_init(
                      &store, &bad) == POTA_SLOT_MANIFEST_BAD_ARGUMENT);
    failed += !test_async_transaction_is_one_physical_step();
    return failed == 0 ? 0 : 1;
}
