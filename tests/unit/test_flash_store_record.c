#include "flash_store_record.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_result(flash_store_record_result_t actual,
                          flash_store_record_result_t expected)
{
    assert(actual == expected);
}

int main(void)
{
    const uint8_t payload[] = {0x10u, 0x20u, 0x30u, 0x40u};
    uint8_t record[FLASH_STORE_RECORD_HEADER_SIZE + sizeof(payload)];
    uint8_t decoded[sizeof(payload)] = {0u};
    size_t record_size = 0u;
    flash_store_record_header_t header;

    expect_result(flash_store_record_encode(
                      3u, 7u, 11u, 5u, 1u, payload, sizeof(payload), record,
                      sizeof(record), &record_size),
                  FLASH_STORE_RECORD_OK);
    assert(record_size == sizeof(record));
    expect_result(flash_store_record_decode(
                      record, record_size, 3u, 7u, 1u, decoded,
                      sizeof(decoded), &header),
                  FLASH_STORE_RECORD_OK);
    assert(memcmp(decoded, payload, sizeof(payload)) == 0);
    assert(header.generation == 11u && header.sequence == 5u);

    expect_result(flash_store_record_decode(
                      record, record_size, 4u, 7u, 1u, decoded,
                      sizeof(decoded), &header),
                  FLASH_STORE_RECORD_SCHEMA_MISMATCH);
    expect_result(flash_store_record_decode(
                      record, record_size, 3u, 8u, 1u, decoded,
                      sizeof(decoded), &header),
                  FLASH_STORE_RECORD_OBJECT_MISMATCH);
    expect_result(flash_store_record_decode(
                      record, record_size, 3u, 7u, 0u, decoded,
                      sizeof(decoded), &header),
                  FLASH_STORE_RECORD_FLAGS);
    expect_result(flash_store_record_decode(
                      record, FLASH_STORE_RECORD_HEADER_SIZE - 1u, 3u, 7u,
                      1u, decoded, sizeof(decoded), &header),
                  FLASH_STORE_RECORD_TRUNCATED);

    record[FLASH_STORE_RECORD_HEADER_SIZE] ^= 0x01u;
    expect_result(flash_store_record_decode(
                      record, record_size, 3u, 7u, 1u, decoded,
                      sizeof(decoded), &header),
                  FLASH_STORE_RECORD_PAYLOAD_CRC);
    record[FLASH_STORE_RECORD_HEADER_SIZE] ^= 0x01u;
    record[32u] ^= 0x01u;
    expect_result(flash_store_record_decode(
                      record, record_size, 3u, 7u, 1u, decoded,
                      sizeof(decoded), &header),
                  FLASH_STORE_RECORD_HEADER_CRC);
    record[32u] ^= 0x01u;
    record[36u] ^= 0x01u;
    expect_result(flash_store_record_decode(
                      record, record_size, 3u, 7u, 1u, decoded,
                      sizeof(decoded), &header),
                  FLASH_STORE_RECORD_COMMIT);

    flash_store_record_header_t older = {.generation = 11u, .sequence = 5u};
    flash_store_record_header_t newer = {.generation = 11u, .sequence = 6u};
    assert(flash_store_record_is_newer(&newer, &older));
    assert(!flash_store_record_is_newer(&older, &newer));
    older.generation = UINT32_MAX;
    newer.generation = 1u;
    newer.sequence = 1u;
    assert(flash_store_record_is_newer(&newer, &older));
    assert(!flash_store_record_is_newer(NULL, &older));

    puts("flash store record host tests passed");
    return 0;
}
