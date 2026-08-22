#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flash_transaction_journal.h"

#define TEST_SLOT_COUNT 4u
#define TEST_SLOT_SIZE ((uint32_t)sizeof(flash_transaction_journal_disk_record_t))

static uint8_t s_flash[TEST_SLOT_COUNT * TEST_SLOT_SIZE];
static uint32_t s_program_calls;
static uint32_t s_fail_program_call;

static bool fake_read(void *context, uint32_t offset, void *data,
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

static bool fake_program(void *context, uint32_t offset, const void *data,
                         uint32_t length)
{
    (void)context;
    s_program_calls++;
    if (s_fail_program_call != 0u && s_program_calls == s_fail_program_call) {
        return false;
    }
    if (data == NULL || offset > sizeof(s_flash) ||
        length > sizeof(s_flash) - offset) {
        return false;
    }
    const uint8_t *source = data;
    for (uint32_t index = 0u; index < length; index++) {
        if ((s_flash[offset + index] & source[index]) != source[index]) {
            return false;
        }
    }
    for (uint32_t index = 0u; index < length; index++) {
        s_flash[offset + index] &= source[index];
    }
    return true;
}

static uint32_t fake_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t index = 0u; index < length; index++) {
        crc ^= data[index];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static flash_transaction_journal_config_t make_config(void)
{
    const flash_transaction_journal_config_t config = {
        .context = s_flash,
        .read = fake_read,
        .program = fake_program,
        .crc32 = fake_crc32,
        .base_offset = 0u,
        .slot_count = TEST_SLOT_COUNT,
        .slot_size = TEST_SLOT_SIZE,
    };
    return config;
}

static flash_transaction_journal_record_t make_record(uint32_t event,
                                                       uint32_t job_id)
{
    const flash_transaction_journal_record_t record = {
        .job_id = job_id,
        .transaction_generation = job_id + 10u,
        .provider_generation = job_id + 20u,
        .store_generation = job_id + 30u,
        .event = event,
        .result = event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED
                      ? FLASH_TRANSACTION_RESULT_COMMITTED
                      : FLASH_TRANSACTION_RESULT_NONE,
        .error = FLASH_TRANSACTION_ERROR_NONE,
        .processed_bytes = 256u,
        .verified_bytes = event >= FLASH_TRANSACTION_JOURNAL_EVENT_VERIFIED
                              ? 256u
                              : 0u,
    };
    return record;
}

static void test_append_and_reset_recovery(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;

    flash_transaction_journal_config_t config = make_config();
    flash_transaction_journal_store_t store;
    assert(flash_transaction_journal_init(&store, &config));
    flash_transaction_completion_lease_t lease;
    assert(flash_transaction_journal_make_completion_lease(&store, &lease));
    assert(lease.retain(lease.context));
    assert(store.retained_refs == 1u);

    const flash_transaction_journal_record_t accepted =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED, 7u);
    assert(lease.append(lease.context, &accepted));
    const flash_transaction_journal_record_t committed =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED, 7u);
    assert(lease.append(lease.context, &committed));
    lease.release(lease.context);
    assert(store.retained_refs == 0u);

    flash_transaction_journal_record_t recovered;
    uint32_t sequence = 0u;
    assert(flash_transaction_journal_recover_latest(&store, &recovered,
                                                    &sequence));
    assert(sequence == 2u);
    assert(recovered.event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);
    assert(recovered.job_id == committed.job_id);

    flash_transaction_journal_store_t restored;
    assert(flash_transaction_journal_init(&restored, &config));
    assert(restored.next_sequence == 3u);
    const flash_transaction_journal_record_t failed =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_FAILED, 8u);
    assert(flash_transaction_journal_append(&restored, &failed));
    assert(flash_transaction_journal_recover_latest(&restored, &recovered,
                                                    &sequence));
    assert(sequence == 3u);
    assert(recovered.event == FLASH_TRANSACTION_JOURNAL_EVENT_FAILED);
}

static void test_torn_commit_and_crc_are_ignored(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 2u;

    flash_transaction_journal_config_t config = make_config();
    flash_transaction_journal_store_t store;
    assert(flash_transaction_journal_init(&store, &config));
    const flash_transaction_journal_record_t torn =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED, 11u);
    assert(!flash_transaction_journal_append(&store, &torn));

    flash_transaction_journal_record_t recovered;
    uint32_t sequence = 0u;
    assert(!flash_transaction_journal_recover_latest(&store, &recovered,
                                                     &sequence));

    s_fail_program_call = 0u;
    const flash_transaction_journal_record_t good =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_VERIFIED, 12u);
    assert(flash_transaction_journal_append(&store, &good));
    assert(flash_transaction_journal_recover_latest(&store, &recovered,
                                                    &sequence));
    assert(sequence == 1u);
    assert(recovered.job_id == good.job_id);

    s_flash[TEST_SLOT_SIZE +
            offsetof(flash_transaction_journal_disk_record_t, record)] ^= 1u;
    assert(!flash_transaction_journal_recover_latest(&store, &recovered,
                                                     &sequence));
}

static void test_full_journal_fails_closed(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    flash_transaction_journal_config_t config = make_config();
    flash_transaction_journal_store_t store;
    assert(flash_transaction_journal_init(&store, &config));
    for (uint32_t index = 0u; index < TEST_SLOT_COUNT; index++) {
        const flash_transaction_journal_record_t record =
            make_record(FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED, index + 1u);
        assert(flash_transaction_journal_append(&store, &record));
    }
    const flash_transaction_journal_record_t extra =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_FAILED, 99u);
    assert(!flash_transaction_journal_append(&store, &extra));
}

int main(void)
{
    test_append_and_reset_recovery();
    test_torn_commit_and_crc_are_ignored();
    test_full_journal_fails_closed();
    puts("flash transaction journal tests passed");
    return 0;
}
