#include "refmem_sync.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Compile this harness and the production receiver with memcpy redirected
 * here. A reset/publication in the middle of the actual reader copy models
 * the other core without adding a hook to production firmware. */
static refmem_sync_vdc_context_t *s_interleaved_context;
static const uint8_t *s_next_frame;
static size_t s_next_frame_size;
static bool s_publish_after_reset;
static uint32_t s_change_consumer_generation;
static unsigned s_interleavings;

/* Same wire CRC fixture as the RefMem frame/receiver host suite. */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

uint32_t ota_crc32_compute(const uint8_t *data, size_t length)
{
    return ota_crc32_update(0u, data, length);
}

void *refmem_vdc_test_memcpy(void *destination, const void *source, size_t size)
{
    volatile unsigned char *dst = destination;
    const volatile unsigned char *src = source;
    size_t copied = 0u;
    if (s_interleaved_context != NULL &&
        source == &s_interleaved_context->vdc_command[0] &&
        size == sizeof(refmem_sync_vdc_command_snapshot_t)) {
        refmem_sync_vdc_context_t *context = s_interleaved_context;
        s_interleaved_context = NULL;
        const size_t split = offsetof(refmem_sync_vdc_command_snapshot_t,
                                      effective_vdc_time_ns);
        for (; copied < split; copied++) dst[copied] = src[copied];
        if (s_change_consumer_generation != 0u) {
            assert(refmem_sync_vdc_set_consumer_generation(
                context, s_change_consumer_generation));
        } else {
            assert(refmem_sync_vdc_reset(context, 2u, 7u, 8u));
        }
        if (s_publish_after_reset) {
            assert(refmem_sync_vdc_receive_frame(context, s_next_frame,
                       s_next_frame_size, NULL) == REFMEM_SYNC_RX_ACCEPTED);
        }
        s_interleavings++;
    }
    for (; copied < size; copied++) dst[copied] = src[copied];
    return destination;
}

static size_t frame_from(uint32_t source, uint32_t epoch, uint32_t run,
                         uint32_t seq, uint32_t transport_seq, uint8_t *out)
{
    refmem_sync_vdc_command_payload_t command = {0};
    command.version = REFMEM_SYNC_VDC_COMMAND_VERSION;
    command.source_slot = source;
    command.target_slot = 2u;
    command.control_generation = 3u;
    command.command_seq = seq;
    command.schedule_crc32 = 0xA5A5u;
    command.epoch_id = epoch;
    command.run_id = run;
    command.effective_vdc_time_ns = 1000000u + seq;
    command.period_adjust_ppb = (int32_t)seq;
    command.phase_offset_ns = -(int32_t)seq;
    command.lock_state = 5u;
    command.quality = 4u;
    command.payload_crc32 = refmem_sync_vdc_command_payload_crc32(&command);
    refmem_sync_frame_header_t header;
    assert(refmem_sync_frame_header_init(&header, REFMEM_SYNC_FRAME_COMMAND,
        0u, (uint8_t)source, 0x04u, epoch, run, transport_seq, 0u, 1000u + seq,
        &command, sizeof(command)));
    size_t size = 0u;
    assert(refmem_sync_frame_encode(&header, &command, sizeof(command), out,
        REFMEM_SYNC_FRAME_HEADER_SIZE + sizeof(command), &size));
    return size;
}

static size_t frame(uint32_t epoch, uint32_t run, uint32_t seq, uint8_t *out)
{
    return frame_from(0u, epoch, run, seq, seq, out);
}

static void test_reset_during_copy(bool publish)
{
    refmem_sync_vdc_context_t context;
    uint8_t old_frame[REFMEM_SYNC_FRAME_HEADER_SIZE +
                      sizeof(refmem_sync_vdc_command_payload_t)];
    uint8_t new_frame[sizeof(old_frame)];
    assert(refmem_sync_vdc_init(&context, 2u, 7u, 8u));
    size_t old_size = frame(7u, 8u, 11u, old_frame);
    s_next_frame_size = frame(7u, 8u, 22u, new_frame);
    assert(refmem_sync_vdc_receive_frame(&context, old_frame, old_size, NULL)
           == REFMEM_SYNC_RX_ACCEPTED);
    const uint32_t before = context.vdc_command_guard;
    s_interleaved_context = &context;
    s_next_frame = new_frame;
    s_publish_after_reset = publish;
    refmem_sync_vdc_command_snapshot_t copied;
    assert(refmem_sync_vdc_copy_command(&context, 0u, &copied));
    assert(s_interleaved_context == NULL);
    if (publish) {
        if (copied.command_seq != 22u || copied.phase_offset_ns != -22) {
            fprintf(stderr, "torn command accepted: seq=%u phase=%d\n",
                    (unsigned)copied.command_seq, (int)copied.phase_offset_ns);
        }
        assert(copied.valid == 1u && copied.command_seq == 22u);
        assert(copied.phase_offset_ns == -22);
    } else {
        assert(copied.valid == 0u && copied.command_seq == 0u);
    }
    assert(context.vdc_command_guard != before);
}

static void test_identity_retirement_and_invalid_reset(void)
{
    refmem_sync_vdc_context_t context;
    uint8_t buffer[REFMEM_SYNC_FRAME_HEADER_SIZE +
                   sizeof(refmem_sync_vdc_command_payload_t)];
    assert(refmem_sync_vdc_init(&context, 2u, 7u, 8u));
    size_t size = frame(7u, 8u, 11u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL)
           == REFMEM_SYNC_RX_ACCEPTED);
    refmem_sync_vdc_context_t before;
    memcpy(&before, &context, sizeof(before));
    assert(!refmem_sync_vdc_reset(&context, REFMEM_SYNC_NODE_COUNT, 9u, 10u));
    assert(memcmp(&before, &context, sizeof(before)) == 0);
    assert(refmem_sync_vdc_set_epoch(&context, 7u, 8u));
    assert(memcmp(&before, &context, sizeof(before)) == 0);
    assert(refmem_sync_vdc_reset(&context, 2u, 9u, 10u));
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL)
           == REFMEM_SYNC_RX_EPOCH_MISMATCH);
    size = frame(9u, 10u, 1u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL)
           == REFMEM_SYNC_RX_ACCEPTED);
    refmem_sync_vdc_command_snapshot_t copied;
    assert(refmem_sync_vdc_copy_command(&context, 0u, &copied));
    assert(copied.command_seq == 1u && copied.epoch_id == 9u && copied.run_id == 10u);
    context.vdc_command_guard = UINT32_MAX - 1u;
    assert(refmem_sync_vdc_reset(&context, 2u, 9u, 10u));
    assert(context.vdc_command_guard == 2u);
    assert(refmem_sync_vdc_copy_command(&context, 0u, &copied) && copied.valid == 0u);
}

static void test_consumer_generation_and_source_watermarks(void)
{
    refmem_sync_vdc_context_t context, before;
    uint8_t buffer[REFMEM_SYNC_FRAME_HEADER_SIZE + sizeof(refmem_sync_vdc_command_payload_t)];
    refmem_sync_vdc_command_snapshot_t copied;
    assert(refmem_sync_vdc_init(&context, 2u, 7u, 8u));
    assert(context.consumer_generation == 0u);
    before = context;
    assert(!refmem_sync_vdc_set_consumer_generation(NULL, 1u));
    assert(!refmem_sync_vdc_set_consumer_generation(&context, 0u));
    assert(memcmp(&context, &before, sizeof(context)) == 0);
    assert(!refmem_sync_vdc_copy_command_for_generation(&context, 0u, 0u, &copied));
    assert(!refmem_sync_vdc_copy_command_for_generation(&context, 0u, 10u, &copied));
    assert(refmem_sync_vdc_set_consumer_generation(&context, 10u));
    assert(context.consumer_generation == 10u && context.vdc_command_guard != before.vdc_command_guard);
    size_t size = frame_from(0u, 7u, 8u, 11u, 31u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_ACCEPTED);
    size = frame_from(1u, 7u, 8u, 21u, 41u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_ACCEPTED);
    /* A failed old command makes preservation of diagnostic counters visible. */
    size = frame_from(0u, 7u, 8u, 10u, 32u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_STALE_SEQ);
    before = context;
    assert(refmem_sync_vdc_set_consumer_generation(&context, 10u));
    assert(memcmp(&context, &before, sizeof(context)) == 0);
    assert(refmem_sync_vdc_copy_command_for_generation(&context, 0u, 10u, &copied));
    assert(copied.valid == 1u && copied.command_seq == 11u && copied.frame_seq32 == 31u);
    assert(!refmem_sync_vdc_copy_command_for_generation(&context, 0u, 0u, &copied));
    assert(!refmem_sync_vdc_copy_command_for_generation(NULL, 0u, 10u, &copied));
    assert(!refmem_sync_vdc_copy_command_for_generation(&context, REFMEM_SYNC_NODE_COUNT, 10u, &copied));
    assert(!refmem_sync_vdc_copy_command_for_generation(&context, 0u, 10u, NULL));
    assert(refmem_sync_vdc_set_consumer_generation(&context, 11u));
    assert(context.consumer_generation == 11u && context.vdc_command_guard != before.vdc_command_guard);
    assert(context.local_slot == before.local_slot && context.active_epoch_id == before.active_epoch_id &&
           context.active_run_id == before.active_run_id);
    for (uint32_t source = 0; source < REFMEM_SYNC_NODE_COUNT; ++source) {
        refmem_sync_vdc_command_snapshot_t expected = before.vdc_command[source];
        expected.valid = 0u;
        /* The complete previous command, peer generation and counters survive.
         * Only eligibility changes; invalid cannot mean never received. */
        assert(memcmp(&context.vdc_command[source], &expected, sizeof(expected)) == 0);
    }
    assert(!refmem_sync_vdc_copy_command_for_generation(&context, 0u, 10u, &copied));
    assert(refmem_sync_vdc_copy_command_for_generation(&context, 0u, 11u, &copied));
    assert(copied.valid == 0u && copied.command_seq == 11u && copied.control_generation == 3u);
    assert(refmem_sync_vdc_copy_command(&context, 1u, &copied));
    assert(copied.valid == 0u && copied.command_seq == 21u);
    size = frame_from(0u, 7u, 8u, 11u, 31u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_DUPLICATE_SEQ);
    size = frame_from(0u, 7u, 8u, 11u, 32u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_STALE_SEQ);
    size = frame_from(0u, 7u, 8u, 10u, 33u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_STALE_SEQ);
    size = frame_from(0u, 7u, 8u, 12u, 30u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_STALE_SEQ);
    assert(context.vdc_command[0].valid == 0u && context.vdc_command[0].command_seq == 11u);
    assert(context.vdc_command[0].frame_seq32 == 31u);
    size = frame_from(0u, 7u, 8u, 12u, 32u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_ACCEPTED);
    assert(refmem_sync_vdc_copy_command_for_generation(&context, 0u, 11u, &copied));
    assert(copied.valid == 1u && copied.command_seq == 12u && copied.received_count == 2u);
    assert(context.vdc_command[1].valid == 0u && context.vdc_command[1].command_seq == 21u);
    size = frame_from(1u, 7u, 8u, 21u, 42u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_STALE_SEQ);
    size = frame_from(1u, 7u, 8u, 22u, 42u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_ACCEPTED);
    assert(context.vdc_command[1].command_seq == 22u && context.vdc_command[0].command_seq == 12u);
    assert(refmem_sync_vdc_set_consumer_generation(&context, 12u));
    size = frame_from(0u, 7u, 8u, 12u, 1000u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_STALE_SEQ);
    assert(refmem_sync_vdc_reset(&context, 2u, 9u, 10u));
    assert(context.consumer_generation == 12u);
    for (uint32_t source = 0; source < REFMEM_SYNC_NODE_COUNT; ++source)
        assert(context.vdc_command[source].valid == 0u && context.vdc_command[source].command_seq == 0u);
    size = frame_from(0u, 9u, 10u, 1u, 1u, buffer);
    assert(refmem_sync_vdc_receive_frame(&context, buffer, size, NULL) == REFMEM_SYNC_RX_ACCEPTED);
    assert(refmem_sync_vdc_copy_command_for_generation(&context, 0u, 12u, &copied));
    assert(copied.valid == 1u && copied.command_seq == 1u && copied.epoch_id == 9u && copied.run_id == 10u);
}

static void test_generation_change_during_copy(bool publish)
{
    refmem_sync_vdc_context_t context;
    uint8_t old_frame[REFMEM_SYNC_FRAME_HEADER_SIZE + sizeof(refmem_sync_vdc_command_payload_t)];
    uint8_t new_frame[sizeof(old_frame)];
    refmem_sync_vdc_command_snapshot_t copied;
    assert(refmem_sync_vdc_init(&context, 2u, 7u, 8u));
    assert(refmem_sync_vdc_set_consumer_generation(&context, 10u));
    size_t size = frame(7u, 8u, 11u, old_frame);
    s_next_frame_size = frame(7u, 8u, 22u, new_frame);
    assert(refmem_sync_vdc_receive_frame(&context, old_frame, size, NULL) == REFMEM_SYNC_RX_ACCEPTED);
    s_interleaved_context = &context;
    s_change_consumer_generation = 11u;
    s_publish_after_reset = publish;
    s_next_frame = new_frame;
    assert(!refmem_sync_vdc_copy_command_for_generation(&context, 0u, 10u, &copied));
    /* On false, copied is deliberately unspecified and must not be consumed. */
    assert(s_interleaved_context == NULL && context.consumer_generation == 11u);
    assert(refmem_sync_vdc_copy_command_for_generation(&context, 0u, 11u, &copied));
    assert(copied.valid == (publish ? 1u : 0u));
    assert(copied.command_seq == (publish ? 22u : 11u));
    s_change_consumer_generation = 0u;
}

int main(void)
{
    test_reset_during_copy(true);
    test_reset_during_copy(false);
    test_identity_retirement_and_invalid_reset();
    test_consumer_generation_and_source_watermarks();
    test_generation_change_during_copy(false);
    test_generation_change_during_copy(true);
    assert(s_interleavings == 4u);
    puts("refmem VDC lifecycle/interleaving tests passed");
    return 0;
}
