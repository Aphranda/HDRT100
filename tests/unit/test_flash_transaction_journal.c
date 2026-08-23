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
static uint32_t s_read_calls;
static uint32_t s_fail_read_call;
static bool s_fail_readback;
static bool s_corrupt_readback;
static uint32_t s_erase_calls;

static bool fake_read(void *context, uint32_t offset, void *data,
                      uint32_t length)
{
    (void)context;
    s_read_calls++;
    if (s_fail_read_call != 0u && s_read_calls == s_fail_read_call) {
        return false;
    }
    if (s_fail_readback && s_program_calls >= 2u &&
        length == sizeof(flash_transaction_journal_disk_record_t)) {
        return false;
    }
    if (data == NULL || offset > sizeof(s_flash) ||
        length > sizeof(s_flash) - offset) {
        return false;
    }
    memcpy(data, &s_flash[offset], length);
    if (s_corrupt_readback && s_program_calls >= 2u &&
        length == sizeof(flash_transaction_journal_disk_record_t)) {
        ((uint8_t *)data)[offsetof(flash_transaction_journal_disk_record_t,
                                   record)] ^= 1u;
        s_corrupt_readback = false;
    }
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

static bool fake_erase(void *context, uint32_t offset, uint32_t length)
{
    (void)context;
    if (offset > sizeof(s_flash) || length > sizeof(s_flash) - offset ||
        length != 2u * TEST_SLOT_SIZE || offset % length != 0u) {
        return false;
    }
    s_erase_calls++;
    memset(&s_flash[offset], 0xFF, length);
    return true;
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

static void test_journal_rotates_to_next_erase_block(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    s_erase_calls = 0u;
    flash_transaction_journal_config_t config = make_config();
    config.erase = fake_erase;
    config.erase_size = 2u * TEST_SLOT_SIZE;
    flash_transaction_journal_store_t store;
    assert(flash_transaction_journal_init(&store, &config));
    for (uint32_t index = 0u; index < TEST_SLOT_COUNT; index++) {
        const flash_transaction_journal_record_t record =
            make_record(FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED, index + 1u);
        assert(flash_transaction_journal_append(&store, &record));
    }
    const flash_transaction_journal_record_t rotated =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED, 100u);
    assert(flash_transaction_journal_append(&store, &rotated));
    assert(s_erase_calls == 1u);

    flash_transaction_journal_record_t recovered;
    uint32_t sequence = 0u;
    assert(flash_transaction_journal_recover_latest(&store, &recovered,
                                                    &sequence));
    assert(sequence == TEST_SLOT_COUNT + 1u &&
           recovered.job_id == rotated.job_id &&
           recovered.event == rotated.event);
}

static void test_duplicate_completion_is_idempotent(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    flash_transaction_journal_config_t config = make_config();
    flash_transaction_journal_store_t store;
    assert(flash_transaction_journal_init(&store, &config));

    const flash_transaction_journal_record_t accepted =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED, 23u);
    assert(flash_transaction_journal_append(&store, &accepted));
    assert(s_program_calls == 2u);
    assert(flash_transaction_journal_append(&store, &accepted));
    assert(s_program_calls == 2u);
    assert(store.next_sequence == 2u);

    flash_transaction_journal_record_t conflicting = accepted;
    conflicting.processed_bytes++;
    assert(!flash_transaction_journal_append(&store, &conflicting));
    assert(s_program_calls == 2u);

    flash_transaction_journal_record_t recovered;
    uint32_t sequence = 0u;
    assert(flash_transaction_journal_recover_latest(&store, &recovered,
                                                    &sequence));
    assert(sequence == 1u);
    assert(memcmp(&recovered, &accepted, sizeof(accepted)) == 0);

    /* Provider/store reset must not turn a replayed terminal completion into
     * another physical journal write. */
    flash_transaction_journal_store_t reset_store;
    assert(flash_transaction_journal_init(&reset_store, &config));
    const uint32_t program_calls_before_replay = s_program_calls;
    assert(flash_transaction_journal_append(&reset_store, &accepted));
    assert(s_program_calls == program_calls_before_replay);
    assert(reset_store.next_sequence == 2u);
}

static void test_find_identity_survives_store_reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    flash_transaction_journal_config_t config = make_config();
    flash_transaction_journal_store_t store;
    flash_transaction_journal_store_t reset_store;
    assert(flash_transaction_journal_init(&store, &config));

    const flash_transaction_journal_record_t accepted =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED, 77u);
    const flash_transaction_journal_record_t committed =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED, 77u);
    assert(flash_transaction_journal_append(&store, &accepted));
    assert(flash_transaction_journal_append(&store, &committed));

    flash_transaction_journal_record_t identity = {0};
    identity.job_id = committed.job_id;
    identity.transaction_generation = committed.transaction_generation;
    identity.provider_generation = committed.provider_generation;
    identity.store_generation = committed.store_generation;
    flash_transaction_journal_record_t found;
    assert(flash_transaction_journal_find(&store, &identity, &found));
    assert(found.event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);
    assert(found.processed_bytes == committed.processed_bytes);

    assert(flash_transaction_journal_init(&reset_store, &config));
    memset(&found, 0, sizeof(found));
    assert(flash_transaction_journal_find(&reset_store, &identity, &found));
    assert(found.event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);

    identity.job_id++;
    assert(!flash_transaction_journal_find(&reset_store, &identity, &found));
}

static void test_fingerprint_identity_survives_runtime_generations(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    flash_transaction_journal_config_t config = make_config();
    flash_transaction_journal_store_t store;
    assert(flash_transaction_journal_init(&store, &config));

    flash_transaction_journal_record_t committed =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED, 41u);
    committed.request_fingerprint = 0xA11CE001u;
    assert(flash_transaction_journal_append(&store, &committed));

    flash_transaction_journal_record_t identity = committed;
    identity.job_id = 1u;
    identity.transaction_generation = 1u;
    identity.provider_generation = 1u;
    identity.store_generation = 2u;
    identity.event = 0u;
    flash_transaction_journal_record_t found;
    assert(flash_transaction_journal_find(&store, &identity, &found));
    assert(found.request_fingerprint == committed.request_fingerprint);
}

static void test_recovery_falls_back_to_previous_valid_completion(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    flash_transaction_journal_config_t config = make_config();
    flash_transaction_journal_store_t store;
    assert(flash_transaction_journal_init(&store, &config));

    const flash_transaction_journal_record_t accepted =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED, 31u);
    const flash_transaction_journal_record_t committed =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED, 31u);
    assert(flash_transaction_journal_append(&store, &accepted));
    assert(flash_transaction_journal_append(&store, &committed));

    /* Simulate reset after a torn/corrupted newest slot. The previous
     * accepted record is still a valid fact and must be selected. */
    s_flash[TEST_SLOT_SIZE +
            offsetof(flash_transaction_journal_disk_record_t, record)] ^= 1u;
    flash_transaction_journal_record_t recovered;
    uint32_t sequence = 0u;
    assert(flash_transaction_journal_recover_latest(&store, &recovered,
                                                    &sequence));
    assert(sequence == 1u);
    assert(recovered.event == FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED);
    assert(recovered.job_id == accepted.job_id);
}

static void test_reset_boundary_matrix(void)
{
    const flash_transaction_journal_record_t accepted =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED, 41u);
    const flash_transaction_journal_record_t committed =
        make_record(FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED, 41u);
    flash_transaction_journal_config_t config = make_config();
    flash_transaction_journal_store_t store;
    flash_transaction_journal_store_t reset_store;
    flash_transaction_journal_record_t recovered;
    uint32_t sequence = 0u;

    /* Body write torn: only the previous accepted completion survives. */
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    s_read_calls = 0u;
    s_fail_read_call = 0u;
    s_fail_readback = false;
    s_corrupt_readback = false;
    assert(flash_transaction_journal_init(&store, &config));
    assert(flash_transaction_journal_append(&store, &accepted));
    s_program_calls = 0u;
    s_fail_program_call = 1u;
    assert(!flash_transaction_journal_append(&store, &committed));
    assert(flash_transaction_journal_init(&reset_store, &config));
    assert(flash_transaction_journal_recover_latest(&reset_store, &recovered,
                                                    &sequence));
    assert(recovered.event == FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED);
    assert(sequence == 1u);

    /* Commit marker torn: the body is not a durable completion. */
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    s_read_calls = 0u;
    s_fail_read_call = 0u;
    s_fail_readback = false;
    s_corrupt_readback = false;
    assert(flash_transaction_journal_init(&store, &config));
    assert(flash_transaction_journal_append(&store, &accepted));
    s_program_calls = 0u;
    s_fail_program_call = 2u;
    assert(!flash_transaction_journal_append(&store, &committed));
    assert(flash_transaction_journal_init(&reset_store, &config));
    assert(flash_transaction_journal_recover_latest(&reset_store, &recovered,
                                                    &sequence));
    assert(recovered.event == FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED);
    assert(sequence == 1u);

    /* Readback transport failure: the marker is durable even though the
     * writer cannot verify it; reset therefore selects the new record. */
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    s_read_calls = 0u;
    s_fail_read_call = 0u;
    s_fail_readback = false;
    s_corrupt_readback = false;
    assert(flash_transaction_journal_init(&store, &config));
    assert(flash_transaction_journal_append(&store, &accepted));
    s_program_calls = 0u;
    s_fail_readback = true;
    assert(!flash_transaction_journal_append(&store, &committed));
    s_fail_readback = false;
    assert(flash_transaction_journal_init(&reset_store, &config));
    assert(flash_transaction_journal_recover_latest(&reset_store, &recovered,
                                                    &sequence));
    assert(recovered.event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);
    assert(sequence == 2u);

    /* Readback corruption: the marker was already sealed, therefore reset
     * deterministically selects the new committed completion. */
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    s_read_calls = 0u;
    s_fail_read_call = 0u;
    s_fail_readback = false;
    s_corrupt_readback = false;
    assert(flash_transaction_journal_init(&store, &config));
    assert(flash_transaction_journal_append(&store, &accepted));
    s_program_calls = 0u;
    s_fail_program_call = 0u;
    s_corrupt_readback = true;
    assert(!flash_transaction_journal_append(&store, &committed));
    s_corrupt_readback = false;
    assert(flash_transaction_journal_init(&reset_store, &config));
    assert(flash_transaction_journal_recover_latest(&reset_store, &recovered,
                                                    &sequence));
    assert(recovered.event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);
    assert(sequence == 2u);

    puts("journal reset boundary matrix passed: body=old marker=old readback=new");
}

int main(void)
{
    test_append_and_reset_recovery();
    test_torn_commit_and_crc_are_ignored();
    test_full_journal_fails_closed();
    test_journal_rotates_to_next_erase_block();
    test_duplicate_completion_is_idempotent();
    test_find_identity_survives_store_reset();
    test_fingerprint_identity_survives_runtime_generations();
    test_recovery_falls_back_to_previous_valid_completion();
    test_reset_boundary_matrix();
    puts("flash transaction journal tests passed");
    return 0;
}
