#include "refmem_sync.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static bool make_frame(uint8_t frame_type,
                       uint8_t source_slot,
                       uint8_t target_mask,
                       uint32_t epoch_id,
                       uint32_t run_id,
                       uint32_t seq32,
                       const void *payload,
                       uint16_t payload_size,
                       uint8_t *frame,
                       size_t frame_capacity,
                       size_t *frame_size)
{
    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(&header,
                                       frame_type,
                                       0u,
                                       source_slot,
                                       target_mask,
                                       epoch_id,
                                       run_id,
                                       seq32,
                                       0u,
                                       1000u + seq32,
                                       payload,
                                       payload_size)) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    payload,
                                    payload_size,
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static int test_accepts_hello_and_epoch(void)
{
    int failed = 0;
    refmem_sync_context_t context;
    refmem_sync_rx_snapshot_t snapshot;
    refmem_sync_quality_counters_t quality;
    refmem_sync_hello_payload_t hello;
    refmem_sync_epoch_payload_t epoch;
    uint8_t frame[128];
    size_t frame_size = 0u;

    failed += expect_bool("sync init", refmem_sync_init(&context, 1u, 7u, 8u), true);

    (void)memset(&hello, 0, sizeof(hello));
    hello.layout_version = 1u;
    hello.adapter_id = 1u;
    failed += expect_bool("make hello",
                          make_frame(REFMEM_SYNC_FRAME_HELLO,
                                     0u,
                                     0x02u,
                                     0u,
                                     0u,
                                     10u,
                                     &hello,
                                     sizeof(hello),
                                     frame,
                                     sizeof(frame),
                                     &frame_size),
                          true);
    failed += expect_u32("recv hello",
                         refmem_sync_receive_frame(&context, frame, frame_size, &snapshot),
                         REFMEM_SYNC_RX_ACCEPTED);
    failed += expect_u32("hello snapshot accepted", snapshot.accepted, 1u);
    failed += expect_u32("hello peer seen", context.peer[0].hello_seen, 1u);

    (void)memset(&epoch, 0, sizeof(epoch));
    epoch.table_seq = 123u;
    failed += expect_bool("make epoch",
                          make_frame(REFMEM_SYNC_FRAME_EPOCH,
                                     0u,
                                     0x02u,
                                     7u,
                                     8u,
                                     11u,
                                     &epoch,
                                     sizeof(epoch),
                                     frame,
                                     sizeof(frame),
                                     &frame_size),
                          true);
    failed += expect_u32("recv epoch",
                         refmem_sync_receive_frame(&context, frame, frame_size, &snapshot),
                         REFMEM_SYNC_RX_ACCEPTED);
    failed += expect_u32("epoch peer seen", context.peer[0].epoch_seen, 1u);

    refmem_sync_get_quality(&context, &quality);
    failed += expect_u32("accepted count", quality.accepted_count, 2u);
    failed += expect_u32("rx count", quality.frame_rx_count, 2u);
    return failed;
}

static int test_rejects_target_and_epoch_mismatch(void)
{
    int failed = 0;
    refmem_sync_context_t context;
    refmem_sync_quality_counters_t quality;
    refmem_sync_epoch_payload_t epoch;
    uint8_t frame[128];
    size_t frame_size = 0u;

    (void)refmem_sync_init(&context, 2u, 7u, 8u);
    (void)memset(&epoch, 0, sizeof(epoch));

    (void)make_frame(REFMEM_SYNC_FRAME_EPOCH,
                     0u,
                     0x02u,
                     7u,
                     8u,
                     1u,
                     &epoch,
                     sizeof(epoch),
                     frame,
                     sizeof(frame),
                     &frame_size);
    failed += expect_u32("target mismatch",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_TARGET_MISMATCH);

    (void)make_frame(REFMEM_SYNC_FRAME_EPOCH,
                     0u,
                     0x04u,
                     70u,
                     8u,
                     2u,
                     &epoch,
                     sizeof(epoch),
                     frame,
                     sizeof(frame),
                     &frame_size);
    failed += expect_u32("epoch mismatch",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_EPOCH_MISMATCH);

    refmem_sync_get_quality(&context, &quality);
    failed += expect_u32("target mismatch count", quality.target_mismatch_count, 1u);
    failed += expect_u32("epoch mismatch count", quality.epoch_mismatch_count, 1u);
    failed += expect_u32("accepted after rejects", quality.accepted_count, 0u);
    return failed;
}

static int test_sequence_quality(void)
{
    int failed = 0;
    refmem_sync_context_t context;
    refmem_sync_quality_counters_t quality;
    refmem_sync_delta_header_t delta;
    uint8_t frame[128];
    size_t frame_size = 0u;

    (void)refmem_sync_init(&context, 1u, 7u, 8u);
    (void)memset(&delta, 0, sizeof(delta));
    delta.slot_id = 4u;
    delta.slot_seq = 1u;

    (void)make_frame(REFMEM_SYNC_FRAME_DELTA,
                     0u,
                     0x02u,
                     7u,
                     8u,
                     10u,
                     &delta,
                     sizeof(delta),
                     frame,
                     sizeof(frame),
                     &frame_size);
    failed += expect_u32("seq first",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);
    failed += expect_u32("seq duplicate",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_DUPLICATE_SEQ);

    (void)make_frame(REFMEM_SYNC_FRAME_DELTA,
                     0u,
                     0x02u,
                     7u,
                     8u,
                     9u,
                     &delta,
                     sizeof(delta),
                     frame,
                     sizeof(frame),
                     &frame_size);
    failed += expect_u32("seq stale",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_STALE_SEQ);

    (void)make_frame(REFMEM_SYNC_FRAME_DELTA,
                     0u,
                     0x02u,
                     7u,
                     8u,
                     13u,
                     &delta,
                     sizeof(delta),
                     frame,
                     sizeof(frame),
                     &frame_size);
    failed += expect_u32("seq gap accepted",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);

    refmem_sync_get_quality(&context, &quality);
    failed += expect_u32("duplicate count", quality.duplicate_count, 1u);
    failed += expect_u32("stale count", quality.stale_count, 1u);
    failed += expect_u32("drop count", quality.drop_count, 2u);
    failed += expect_u32("accepted count seq", quality.accepted_count, 2u);
    return failed;
}

static int test_delta_mirror_commit(void)
{
    int failed = 0;
    refmem_sync_context_t context;
    refmem_sync_delta_header_t delta;
    uint8_t payload[sizeof(refmem_sync_delta_header_t) + 4u];
    uint8_t frame[128];
    size_t frame_size = 0u;

    (void)refmem_sync_init(&context, 1u, 7u, 8u);
    (void)memset(&delta, 0, sizeof(delta));
    delta.delta_id = 3u;
    delta.slot_id = 4u;
    delta.payload_kind = 1u;
    delta.slot_seq = 55u;
    delta.field_id = 9u;
    delta.field_offset = 0u;
    delta.field_width = 4u;
    delta.dirty_mask = 0x10u;
    (void)memcpy(payload, &delta, sizeof(delta));
    payload[sizeof(delta) + 0u] = 0x78u;
    payload[sizeof(delta) + 1u] = 0x56u;
    payload[sizeof(delta) + 2u] = 0x34u;
    payload[sizeof(delta) + 3u] = 0x12u;

    failed += expect_bool("make delta mirror",
                          make_frame(REFMEM_SYNC_FRAME_DELTA,
                                     0u,
                                     0x02u,
                                     7u,
                                     8u,
                                     12u,
                                     payload,
                                     sizeof(payload),
                                     frame,
                                     sizeof(frame),
                                     &frame_size),
                          true);
    failed += expect_u32("recv delta mirror",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);

    const refmem_sync_mirror_snapshot_t *mirror = refmem_sync_get_mirror(&context, 0u);
    failed += expect_bool("mirror present", mirror != NULL, true);
    if (mirror != NULL) {
        failed += expect_u32("mirror visible", mirror->visible, 1u);
        failed += expect_u32("mirror source", mirror->source_slot, 0u);
        failed += expect_u32("mirror slot", mirror->slot_id, 4u);
        failed += expect_u32("mirror slot seq", mirror->slot_seq, 55u);
        failed += expect_u32("mirror field", mirror->field_id, 9u);
        failed += expect_u32("mirror width", mirror->field_width, 4u);
        failed += expect_u32("mirror dirty", mirror->dirty_mask, 0x10u);
        failed += expect_u32("mirror value", mirror->value_u32, 0x12345678u);
        failed += expect_u32("mirror frame seq", mirror->last_frame_seq32, 12u);
        failed += expect_u32("mirror committed", mirror->committed_count, 1u);
        failed += expect_u32("mirror visible count", mirror->visible_count, 1u);
    }
    return failed;
}

static int test_ack_nack_commit(void)
{
    int failed = 0;
    refmem_sync_context_t context;
    refmem_sync_ack_nack_payload_t ack_payload;
    uint8_t frame[128];
    size_t frame_size = 0u;

    (void)refmem_sync_init(&context, 0u, 7u, 8u);
    (void)memset(&ack_payload, 0, sizeof(ack_payload));
    ack_payload.command_seq = 0u;
    ack_payload.delta_seq32 = 12u;
    ack_payload.taken_flags = 0x01u;
    ack_payload.ack_flags = 0x01u;
    ack_payload.nack_flags = 0u;
    ack_payload.last_reason = 0u;
    ack_payload.last_reason_slot = 1u;

    failed += expect_bool("make ack",
                          make_frame(REFMEM_SYNC_FRAME_ACK_NACK,
                                     1u,
                                     0x01u,
                                     7u,
                                     8u,
                                     13u,
                                     &ack_payload,
                                     sizeof(ack_payload),
                                     frame,
                                     sizeof(frame),
                                     &frame_size),
                          true);
    failed += expect_u32("recv ack",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);

    const refmem_sync_ack_snapshot_t *ack = refmem_sync_get_ack(&context, 1u);
    failed += expect_bool("ack present", ack != NULL, true);
    if (ack != NULL) {
        failed += expect_u32("ack seen", ack->seen, 1u);
        failed += expect_u32("ack source", ack->source_slot, 1u);
        failed += expect_u32("ack delta seq", ack->delta_seq32, 12u);
        failed += expect_u32("ack taken flags", ack->taken_flags, 1u);
        failed += expect_u32("ack flags", ack->ack_flags, 1u);
        failed += expect_u32("ack nack flags", ack->nack_flags, 0u);
        failed += expect_u32("ack reason", ack->last_reason, 0u);
        failed += expect_u32("ack reason slot", ack->last_reason_slot, 1u);
        failed += expect_u32("ack frame seq", ack->last_frame_seq32, 13u);
        failed += expect_u32("ack received count", ack->received_count, 1u);
    }
    return failed;
}

static int test_fence_commit(void)
{
    int failed = 0;
    refmem_sync_context_t context;
    refmem_sync_delta_header_t delta;
    refmem_sync_fence_payload_t fence_payload;
    uint8_t delta_payload[sizeof(refmem_sync_delta_header_t) + 4u];
    uint8_t delta_frame[128];
    uint8_t fence_frame[128];
    size_t delta_frame_size = 0u;
    size_t fence_frame_size = 0u;

    (void)refmem_sync_init(&context, 0u, 7u, 8u);
    (void)memset(&delta, 0, sizeof(delta));
    delta.slot_id = 1u;
    delta.slot_seq = 1u;
    delta.field_width = sizeof(uint32_t);
    (void)memcpy(delta_payload, &delta, sizeof(delta));
    delta_payload[sizeof(delta) + 0u] = 0x01u;
    delta_payload[sizeof(delta) + 1u] = 0x00u;
    delta_payload[sizeof(delta) + 2u] = 0x00u;
    delta_payload[sizeof(delta) + 3u] = 0x00u;
    failed += expect_bool("make delta before fence",
                          make_frame(REFMEM_SYNC_FRAME_DELTA,
                                     1u,
                                     0x01u,
                                     7u,
                                     8u,
                                     1u,
                                     delta_payload,
                                     sizeof(delta_payload),
                                     delta_frame,
                                     sizeof(delta_frame),
                                     &delta_frame_size),
                          true);
    failed += expect_u32("recv delta before fence",
                         refmem_sync_receive_frame(&context, delta_frame, delta_frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);

    (void)memset(&fence_payload, 0, sizeof(fence_payload));
    fence_payload.fence_seq = 10u;
    fence_payload.fence_scope = 1u;
    fence_payload.required_mask = 0x01u;
    fence_payload.min_table_seq = 1u;
    fence_payload.deadline_1e3ns = 1000u;
    failed += expect_bool("make pass fence",
                          make_frame(REFMEM_SYNC_FRAME_FENCE,
                                     1u,
                                     0x01u,
                                     7u,
                                     8u,
                                     2u,
                                     &fence_payload,
                                     sizeof(fence_payload),
                                     fence_frame,
                                     sizeof(fence_frame),
                                     &fence_frame_size),
                          true);
    failed += expect_u32("recv pass fence",
                         refmem_sync_receive_frame(&context, fence_frame, fence_frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);

    const refmem_sync_fence_snapshot_t *fence = refmem_sync_get_fence(&context, 1u);
    failed += expect_bool("fence present", fence != NULL, true);
    if (fence != NULL) {
        failed += expect_u32("fence seen", fence->seen, 1u);
        failed += expect_u32("fence passed", fence->passed, 1u);
        failed += expect_u32("fence visible mask", fence->required_visible_mask, 1u);
        failed += expect_u32("fence missing mask", fence->missing_mask, 0u);
        failed += expect_u32("fence received count", fence->received_count, 1u);
    }

    fence_payload.fence_seq = 11u;
    fence_payload.min_table_seq = 99u;
    fence_payload.deadline_1e3ns = 0u;
    failed += expect_bool("make failed fence",
                          make_frame(REFMEM_SYNC_FRAME_FENCE,
                                     1u,
                                     0x01u,
                                     7u,
                                     8u,
                                     3u,
                                     &fence_payload,
                                     sizeof(fence_payload),
                                     fence_frame,
                                     sizeof(fence_frame),
                                     &fence_frame_size),
                          true);
    failed += expect_u32("recv failed fence",
                         refmem_sync_receive_frame(&context, fence_frame, fence_frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);
    fence = refmem_sync_get_fence(&context, 1u);
    if (fence != NULL) {
        failed += expect_u32("fence failed passed", fence->passed, 0u);
        failed += expect_u32("fence failed missing", fence->missing_mask, 1u);
        failed += expect_u32("fence failed reason", fence->last_reason, 3u);
        failed += expect_u32("fence failed timeout", fence->timed_out, 1u);
        failed += expect_u32("fence failed count", fence->received_count, 2u);
    }
    return failed;
}

static int test_quality_commit(void)
{
    int failed = 0;
    refmem_sync_context_t context;
    refmem_sync_quality_payload_t quality_payload;
    uint8_t frame[128];
    size_t frame_size = 0u;

    (void)refmem_sync_init(&context, 0u, 7u, 8u);
    (void)memset(&quality_payload, 0, sizeof(quality_payload));
    quality_payload.quality_id = 21u;
    quality_payload.scope = 1u;
    quality_payload.source_slot = 1u;
    quality_payload.target_slot = 0u;
    quality_payload.seq_expected = 9u;
    quality_payload.seq_last = 8u;
    quality_payload.crc_error_count = 2u;
    quality_payload.stale_count = 3u;
    quality_payload.drop_count = 4u;
    quality_payload.timeout_count = 5u;
    quality_payload.last_error = 9u;
    quality_payload.evidence_index = 6u;

    failed += expect_bool("make quality",
                          make_frame(REFMEM_SYNC_FRAME_QUALITY,
                                     1u,
                                     0x01u,
                                     7u,
                                     8u,
                                     1u,
                                     &quality_payload,
                                     sizeof(quality_payload),
                                     frame,
                                     sizeof(frame),
                                     &frame_size),
                          true);
    failed += expect_u32("recv quality",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);

    const refmem_sync_remote_quality_snapshot_t *quality =
        refmem_sync_get_remote_quality(&context, 1u);
    failed += expect_bool("quality present", quality != NULL, true);
    if (quality != NULL) {
        failed += expect_u32("quality seen", quality->seen, 1u);
        failed += expect_u32("quality id", quality->quality_id, 21u);
        failed += expect_u32("quality target", quality->target_slot, 0u);
        failed += expect_u32("quality expected", quality->seq_expected, 9u);
        failed += expect_u32("quality last", quality->seq_last, 8u);
        failed += expect_u32("quality crc", quality->crc_error_count, 2u);
        failed += expect_u32("quality stale", quality->stale_count, 3u);
        failed += expect_u32("quality drop", quality->drop_count, 4u);
        failed += expect_u32("quality timeout", quality->timeout_count, 5u);
        failed += expect_u32("quality error", quality->last_error, 9u);
        failed += expect_u32("quality evidence", quality->evidence_index, 6u);
        failed += expect_u32("quality frame seq", quality->last_frame_seq32, 1u);
        failed += expect_u32("quality received count", quality->received_count, 1u);
    }
    return failed;
}

static int test_frame_error_quality(void)
{
    int failed = 0;
    refmem_sync_context_t context;
    refmem_sync_rx_snapshot_t snapshot;
    refmem_sync_quality_counters_t quality;
    refmem_sync_delta_header_t delta;
    uint8_t frame[128];
    size_t frame_size = 0u;

    (void)refmem_sync_init(&context, 1u, 7u, 8u);
    (void)memset(&delta, 0, sizeof(delta));
    (void)make_frame(REFMEM_SYNC_FRAME_DELTA,
                     0u,
                     0x02u,
                     7u,
                     8u,
                     1u,
                     &delta,
                     sizeof(delta),
                     frame,
                     sizeof(frame),
                     &frame_size);
    frame[REFMEM_SYNC_FRAME_HEADER_SIZE] ^= 0x55u;
    failed += expect_u32("bad payload frame",
                         refmem_sync_receive_frame(&context, frame, frame_size, &snapshot),
                         REFMEM_SYNC_RX_FRAME_INVALID);
    failed += expect_u32("bad payload frame result",
                         snapshot.frame_result,
                         REFMEM_SYNC_FRAME_BAD_PAYLOAD_CRC);
    failed += expect_u32("bad payload source preserved", snapshot.header.source_slot, 0u);
    failed += expect_u32("bad payload seq preserved", snapshot.header.seq32, 1u);
    refmem_sync_get_quality(&context, &quality);
    failed += expect_u32("bad frame count", quality.bad_frame_count, 1u);
    failed += expect_u32("crc error count", quality.crc_error_count, 1u);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_accepts_hello_and_epoch();
    failed += test_rejects_target_and_epoch_mismatch();
    failed += test_sequence_quality();
    failed += test_delta_mirror_commit();
    failed += test_ack_nack_commit();
    failed += test_fence_commit();
    failed += test_quality_commit();
    failed += test_frame_error_quality();

    if (failed != 0) {
        (void)printf("refmem_sync tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_sync tests passed\n");
    return 0;
}
