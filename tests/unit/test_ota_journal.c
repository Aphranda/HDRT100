#include "ota_journal.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drv_flash.h"
#include "flash_deployment_map.h"
#include "flash_transaction.h"

static uint8_t s_flash[FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_SIZE];
static uint32_t s_program_calls;
static uint32_t s_fail_program_call;
static bool s_program_alignment_valid;
static bool s_program_one_to_zero_valid;

bool drv_flash_read(uint32_t flash_offset, void *data, size_t length)
{
    (void)flash_offset;
    (void)data;
    (void)length;
    return false;
}

bool flash_transaction_ao_execute(const flash_transaction_request_t *request,
                                  flash_transaction_completion_t *completion)
{
    (void)request;
    (void)completion;
    return false;
}

bool pota_stream_session_set_checkpoint_store(
    pota_stream_session_t *session,
    pota_stream_checkpoint_store_t *store,
    const pota_stream_checkpoint_policy_t *policy)
{
    (void)session;
    (void)store;
    (void)policy;
    return false;
}

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

static bool fake_program_page(void *context, uint32_t offset,
                              const uint8_t *data, uint32_t length)
{
    (void)context;
    s_program_calls++;
    if (data == NULL || offset > sizeof(s_flash) ||
        length > sizeof(s_flash) - offset ||
        offset % FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE != 0u ||
        length != FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE) {
        s_program_alignment_valid = false;
        return false;
    }
    for (uint32_t index = 0u; index < length; index++) {
        if ((s_flash[offset + index] & data[index]) != data[index]) {
            s_program_one_to_zero_valid = false;
            return false;
        }
    }
    if (s_program_calls == s_fail_program_call) {
        memcpy(&s_flash[offset], data,
               POTA_STREAM_CHECKPOINT_RECORD_SIZE - 16u);
        return false;
    }
    for (uint32_t index = 0u; index < length; index++) {
        s_flash[offset + index] &= data[index];
    }
    return true;
}

static pota_stream_checkpoint_t checkpoint(uint32_t durable_offset,
                                           uint32_t chunk_crc32)
{
    const pota_stream_checkpoint_t value = {
        .session_id = 11u,
        .generation = 22u,
        .token = 33u,
        .object_id = 44u,
        .durable_offset = durable_offset,
        .total_size = 16384u,
        .package_crc32 = 55u,
        .chunk_crc32 = chunk_crc32,
        .durable_crc32 = 66u + durable_offset,
    };
    return value;
}

int main(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_alignment_valid = true;
    s_program_one_to_zero_valid = true;
    const ota_journal_platform_t platform = {
        .read = fake_read,
        .program_page = fake_program_page,
    };

    assert(ota_journal_init_with_platform(&platform));
    const pota_stream_checkpoint_t first = checkpoint(8192u, 0x11111111u);
    assert(ota_journal_append(&first) == POTA_STREAM_CHECKPOINT_OK);
    assert(s_program_calls == 2u);

    pota_stream_checkpoint_t recovered;
    uint32_t sequence = 0u;
    assert(ota_journal_init_with_platform(&platform));
    assert(ota_journal_recover_latest(&recovered, &sequence) ==
           POTA_STREAM_CHECKPOINT_OK);
    assert(sequence == 1u && recovered.durable_offset == first.durable_offset);

    s_fail_program_call = s_program_calls + 2u;
    const pota_stream_checkpoint_t second = checkpoint(12288u, 0x22222222u);
    assert(ota_journal_append(&second) == POTA_STREAM_CHECKPOINT_IO);

    assert(ota_journal_init_with_platform(&platform));
    assert(ota_journal_recover_latest(&recovered, &sequence) ==
           POTA_STREAM_CHECKPOINT_OK);
    assert(sequence == 1u && recovered.durable_offset == first.durable_offset);

    s_fail_program_call = 0u;
    assert(ota_journal_append(&second) == POTA_STREAM_CHECKPOINT_OK);
    assert(ota_journal_init_with_platform(&platform));
    assert(ota_journal_recover_latest(&recovered, &sequence) ==
           POTA_STREAM_CHECKPOINT_OK);
    assert(sequence == 2u && recovered.durable_offset == second.durable_offset);
    assert(s_program_alignment_valid && s_program_one_to_zero_valid);

    ota_journal_snapshot_t snapshot;
    assert(ota_journal_get_snapshot(&snapshot));
    assert(snapshot.valid && snapshot.result == POTA_STREAM_CHECKPOINT_OK);
    assert(snapshot.sequence == 2u);
    assert(snapshot.checkpoint.durable_offset == second.durable_offset);
    assert(!ota_journal_get_snapshot(NULL));

    assert(!ota_journal_init_with_platform(NULL));
    assert(ota_journal_recover_latest(&recovered, &sequence) ==
           POTA_STREAM_CHECKPOINT_BAD_ARGUMENT);
    assert(ota_journal_get_snapshot(&snapshot));
    assert(!snapshot.valid &&
           snapshot.result == POTA_STREAM_CHECKPOINT_BAD_ARGUMENT);

    puts("ota journal adapter tests passed");
    return 0;
}
