#include "pota_stream_checkpoint.h"

#include <stdio.h>
#include <string.h>

#define MOCK_SLOTS 8u
#define MOCK_SLOT_SIZE POTA_STREAM_CHECKPOINT_RECORD_SIZE

static uint8_t s_flash[MOCK_SLOTS * MOCK_SLOT_SIZE];
static uint32_t s_program_call;
static uint32_t s_fail_call;
static uint32_t s_partial_bytes;

static bool read_flash(void *context, uint32_t offset, void *data, uint32_t length)
{
    (void)context;
    if (data == NULL || offset > sizeof(s_flash) || length > sizeof(s_flash) - offset) {
        return false;
    }
    memcpy(data, &s_flash[offset], length);
    return true;
}

static bool program_flash(void *context, uint32_t offset, const void *data, uint32_t length)
{
    (void)context;
    if (data == NULL || offset > sizeof(s_flash) || length > sizeof(s_flash) - offset) {
        return false;
    }
    s_program_call++;
    if (s_partial_bytes != UINT32_MAX) {
        const uint32_t count = s_partial_bytes < length ? s_partial_bytes : length;
        memcpy(&s_flash[offset], data, count);
        s_partial_bytes = 0u;
        return false;
    }
    if (s_fail_call != 0u && s_program_call == s_fail_call) {
        return false;
    }
    memcpy(&s_flash[offset], data, length);
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

static int test_checkpoint_policy(void)
{
    const pota_stream_checkpoint_policy_t policy = {
        .interval_bytes = 128u,
        .checkpoint_on_final = true,
    };
    int failed = 0;
    failed += !expect("policy valid", pota_stream_checkpoint_policy_valid(&policy));
    failed += !expect("policy below interval",
                      !pota_stream_checkpoint_should_append(&policy, 0u, 64u, 512u));
    failed += !expect("policy interval",
                      pota_stream_checkpoint_should_append(&policy, 0u, 128u, 512u));
    failed += !expect("policy not every chunk",
                      !pota_stream_checkpoint_should_append(&policy, 128u, 192u, 512u));
    failed += !expect("policy final",
                      pota_stream_checkpoint_should_append(&policy, 128u, 512u, 512u));
    failed += !expect("policy backwards",
                      !pota_stream_checkpoint_should_append(&policy, 256u, 128u, 512u));
    const pota_stream_checkpoint_policy_t invalid = {0u, true};
    failed += !expect("policy invalid", !pota_stream_checkpoint_policy_valid(&invalid));
    return failed;
}

int main(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_call = 0u;
    s_fail_call = 0u;
    s_partial_bytes = UINT32_MAX;
    const pota_stream_checkpoint_config_t config = {
        .context = s_flash,
        .read = read_flash,
        .program = program_flash,
        .slot_count = MOCK_SLOTS,
        .slot_size = MOCK_SLOT_SIZE,
    };
    pota_stream_checkpoint_store_t store;
    int failed = test_checkpoint_policy();
    failed += !expect("init", pota_stream_checkpoint_init(&store, &config) == POTA_STREAM_CHECKPOINT_OK);
    pota_stream_checkpoint_t checkpoint = {
        .session_id = 1u,
        .generation = 2u,
        .token = 3u,
        .object_id = 4u,
        .durable_offset = 128u,
        .total_size = 512u,
        .package_crc32 = 5u,
        .chunk_crc32 = 6u,
    };
    failed += !expect("append", pota_stream_checkpoint_append(&store, &checkpoint) == POTA_STREAM_CHECKPOINT_OK);
    failed += !expect("replay idempotent", pota_stream_checkpoint_append(&store, &checkpoint) == POTA_STREAM_CHECKPOINT_OK);
    pota_stream_checkpoint_t recovered;
    uint32_t sequence = 0u;
    failed += !expect("recover", pota_stream_checkpoint_recover_latest(&store, &recovered, &sequence) == POTA_STREAM_CHECKPOINT_OK);
    failed += !expect("payload", memcmp(&recovered, &checkpoint, sizeof(checkpoint)) == 0 && sequence == 1u);
    failed += !expect("match", pota_stream_checkpoint_matches(&recovered, 1u, 2u, 3u, 4u, 512u, 5u));
    failed += !expect("mismatch", !pota_stream_checkpoint_matches(&recovered, 1u, 2u, 99u, 4u, 512u, 5u));
    checkpoint.durable_offset = 256u;
    failed += !expect("append next", pota_stream_checkpoint_append(&store, &checkpoint) == POTA_STREAM_CHECKPOINT_OK);
    checkpoint.durable_offset = 384u;
    failed += !expect("append third", pota_stream_checkpoint_append(&store, &checkpoint) == POTA_STREAM_CHECKPOINT_OK);
    checkpoint.durable_offset = 512u;
    failed += !expect("append fourth", pota_stream_checkpoint_append(&store, &checkpoint) == POTA_STREAM_CHECKPOINT_OK);
    checkpoint.durable_offset = 0u;
    failed += !expect("stale replay rejected", pota_stream_checkpoint_append(&store, &checkpoint) == POTA_STREAM_CHECKPOINT_CONFLICT);
    checkpoint.session_id = 9u;
    const pota_stream_checkpoint_config_t full_config = {
        .context = s_flash,
        .read = read_flash,
        .program = program_flash,
        .slot_count = 4u,
        .slot_size = MOCK_SLOT_SIZE,
    };
    pota_stream_checkpoint_store_t full_store;
    failed += !expect("full init", pota_stream_checkpoint_init(&full_store, &full_config) == POTA_STREAM_CHECKPOINT_OK);
    failed += !expect("full fails closed", pota_stream_checkpoint_append(&full_store, &checkpoint) == POTA_STREAM_CHECKPOINT_FULL);

    /* A reset reconstructs the next sequence from the durable records. */
    pota_stream_checkpoint_store_t reset_store;
    failed += !expect("reset init", pota_stream_checkpoint_init(&reset_store, &config) == POTA_STREAM_CHECKPOINT_OK);
    failed += !expect("reset recover", pota_stream_checkpoint_recover_latest(&reset_store, &recovered, &sequence) == POTA_STREAM_CHECKPOINT_OK && sequence == 4u && recovered.durable_offset == 512u);

    /* A torn body and a torn commit marker are ignored, then the next slot is used. */
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_call = 0u;
    s_fail_call = 0u;
    s_partial_bytes = 12u;
    pota_stream_checkpoint_store_t torn_store;
    failed += !expect("torn body", pota_stream_checkpoint_init(&torn_store, &config) == POTA_STREAM_CHECKPOINT_OK);
    failed += !expect("torn body write", pota_stream_checkpoint_append(&torn_store, &checkpoint) == POTA_STREAM_CHECKPOINT_IO);
    s_partial_bytes = UINT32_MAX;
    failed += !expect("after torn body", pota_stream_checkpoint_append(&torn_store, &checkpoint) == POTA_STREAM_CHECKPOINT_OK);
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_call = 0u;
    s_fail_call = 2u;
    failed += !expect("torn marker write", pota_stream_checkpoint_append(&torn_store, &checkpoint) == POTA_STREAM_CHECKPOINT_IO);
    s_fail_call = 0u;
    failed += !expect("after torn marker", pota_stream_checkpoint_append(&torn_store, &checkpoint) == POTA_STREAM_CHECKPOINT_OK);

    /* Readback corruption is detected during recovery. */
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_call = 0u;
    failed += !expect("corruption init", pota_stream_checkpoint_init(&torn_store, &config) == POTA_STREAM_CHECKPOINT_OK);
    failed += !expect("corruption append", pota_stream_checkpoint_append(&torn_store, &checkpoint) == POTA_STREAM_CHECKPOINT_OK);
    s_flash[0] ^= 0x01u;
    failed += !expect("corruption rejected", pota_stream_checkpoint_recover_latest(&torn_store, &recovered, &sequence) == POTA_STREAM_CHECKPOINT_NO_VALID);
    if (failed != 0) {
        return 1;
    }
    (void)printf("pota stream checkpoint tests passed\n");
    return 0;
}
