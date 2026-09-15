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
        assert(refmem_sync_vdc_reset(context, 2u, 7u, 8u));
        if (s_publish_after_reset) {
            assert(refmem_sync_vdc_receive_frame(context, s_next_frame,
                       s_next_frame_size, NULL) == REFMEM_SYNC_RX_ACCEPTED);
        }
        s_interleavings++;
    }
    for (; copied < size; copied++) dst[copied] = src[copied];
    return destination;
}

static size_t frame(uint32_t epoch, uint32_t run, uint32_t seq, uint8_t *out)
{
    refmem_sync_vdc_command_payload_t command = {0};
    command.version = REFMEM_SYNC_VDC_COMMAND_VERSION;
    command.source_slot = 0u;
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
        0u, 0u, 0x04u, epoch, run, seq, 0u, 1000u + seq,
        &command, sizeof(command)));
    size_t size = 0u;
    assert(refmem_sync_frame_encode(&header, &command, sizeof(command), out,
        REFMEM_SYNC_FRAME_HEADER_SIZE + sizeof(command), &size));
    return size;
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

int main(void)
{
    test_reset_during_copy(true);
    test_reset_during_copy(false);
    test_identity_retirement_and_invalid_reset();
    assert(s_interleavings == 2u);
    puts("refmem VDC lifecycle/interleaving tests passed");
    return 0;
}
