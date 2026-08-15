#include "refmem_sync_frame.h"

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

static int test_hello_encode_decode(void)
{
    int failed = 0;
    refmem_sync_hello_payload_t hello;
    refmem_sync_frame_header_t header;
    refmem_sync_frame_header_t decoded;
    uint8_t frame[128];
    size_t frame_size = 0u;
    const uint8_t *payload = NULL;
    uint16_t payload_size = 0u;

    (void)memset(&hello, 0, sizeof(hello));
    hello.build_id_crc32 = 0x20260814u;
    hello.layout_version = 1u;
    hello.application_crc32 = 0x11112222u;
    hello.config_crc32 = 0x33334444u;
    hello.capability_mask = 0x0000C130u;
    hello.adapter_id = 1u;
    hello.adapter_caps = 0x03u;
    hello.max_payload_size = REFMEM_SYNC_FRAME_PAYLOAD_MAX;
    hello.preferred_mtu = 128u;

    failed += expect_bool("hello header init",
                          refmem_sync_frame_header_init(&header,
                                                        REFMEM_SYNC_FRAME_HELLO,
                                                        REFMEM_SYNC_FRAME_FLAG_ACK_REQUEST,
                                                        1u,
                                                        0x02u,
                                                        7u,
                                                        8u,
                                                        9u,
                                                        6u,
                                                        12345u,
                                                        &hello,
                                                        sizeof(hello)),
                          true);
    failed += expect_bool("hello encode",
                          refmem_sync_frame_encode(&header,
                                                   &hello,
                                                   sizeof(hello),
                                                   frame,
                                                   sizeof(frame),
                                                   &frame_size),
                          true);
    failed += expect_u32("hello frame size",
                         (uint32_t)frame_size,
                         REFMEM_SYNC_FRAME_HEADER_SIZE + (uint32_t)sizeof(hello));
    failed += expect_u32("hello validate",
                         (uint32_t)refmem_sync_frame_validate(frame,
                                                              frame_size,
                                                              &decoded,
                                                              &payload,
                                                              &payload_size),
                         REFMEM_SYNC_FRAME_OK);
    failed += expect_u32("hello decoded type", decoded.frame_type, REFMEM_SYNC_FRAME_HELLO);
    failed += expect_u32("hello decoded source", decoded.source_slot, 1u);
    failed += expect_u32("hello decoded target", decoded.target_mask, 0x02u);
    failed += expect_u32("hello payload size", payload_size, (uint32_t)sizeof(hello));
    failed += expect_bool("hello payload pointer", payload != NULL, true);
    failed += expect_u32("hello payload crc",
                         decoded.payload_crc32,
                         refmem_sync_frame_payload_crc32(payload, payload_size));
    return failed;
}

static int test_crc_and_header_failures(void)
{
    int failed = 0;
    uint8_t payload[4] = {1u, 2u, 3u, 4u};
    refmem_sync_frame_header_t header;
    uint8_t frame[96];
    size_t frame_size = 0u;

    (void)refmem_sync_frame_header_init(&header,
                                        REFMEM_SYNC_FRAME_DELTA,
                                        0u,
                                        0u,
                                        0x02u,
                                        1u,
                                        2u,
                                        3u,
                                        0u,
                                        4u,
                                        payload,
                                        sizeof(payload));
    (void)refmem_sync_frame_encode(&header,
                                   payload,
                                   sizeof(payload),
                                   frame,
                                   sizeof(frame),
                                   &frame_size);

    frame[REFMEM_SYNC_FRAME_HEADER_SIZE + 1u] ^= 0x55u;
    failed += expect_u32("payload crc detects mutation",
                         (uint32_t)refmem_sync_frame_validate(frame,
                                                              frame_size,
                                                              NULL,
                                                              NULL,
                                                              NULL),
                         REFMEM_SYNC_FRAME_BAD_PAYLOAD_CRC);
    frame[REFMEM_SYNC_FRAME_HEADER_SIZE + 1u] ^= 0x55u;

    frame[18] ^= 0x01u;
    failed += expect_u32("header crc detects mutation",
                         (uint32_t)refmem_sync_frame_validate(frame,
                                                              frame_size,
                                                              NULL,
                                                              NULL,
                                                              NULL),
                         REFMEM_SYNC_FRAME_BAD_HEADER_CRC);
    frame[18] ^= 0x01u;

    frame[0] = 0u;
    failed += expect_u32("magic detects mutation",
                         (uint32_t)refmem_sync_frame_validate(frame,
                                                              frame_size,
                                                              NULL,
                                                              NULL,
                                                              NULL),
                         REFMEM_SYNC_FRAME_BAD_MAGIC);
    return failed;
}

static int test_rejects_bad_inputs(void)
{
    int failed = 0;
    uint8_t payload[REFMEM_SYNC_FRAME_PAYLOAD_MAX + 1u];
    refmem_sync_frame_header_t header;
    uint8_t frame[REFMEM_SYNC_FRAME_HEADER_SIZE];
    size_t frame_size = 0u;

    (void)memset(payload, 0xA5, sizeof(payload));
    failed += expect_bool("reject bad type",
                          refmem_sync_frame_header_init(&header,
                                                        99u,
                                                        0u,
                                                        0u,
                                                        1u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        NULL,
                                                        0u),
                          false);
    failed += expect_bool("reject bad source slot",
                          refmem_sync_frame_header_init(&header,
                                                        REFMEM_SYNC_FRAME_HELLO,
                                                        0u,
                                                        8u,
                                                        1u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        NULL,
                                                        0u),
                          false);
    failed += expect_bool("reject oversized payload",
                          refmem_sync_frame_header_init(&header,
                                                        REFMEM_SYNC_FRAME_HELLO,
                                                        0u,
                                                        0u,
                                                        1u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        0u,
                                                        payload,
                                                        sizeof(payload)),
                          false);

    (void)refmem_sync_frame_header_init(&header,
                                        REFMEM_SYNC_FRAME_QUALITY,
                                        0u,
                                        0u,
                                        1u,
                                        0u,
                                        0u,
                                        0u,
                                        0u,
                                        0u,
                                        NULL,
                                        0u);
    failed += expect_bool("reject too small encode buffer",
                          refmem_sync_frame_encode(&header,
                                                   NULL,
                                                   0u,
                                                   frame,
                                                   sizeof(frame) - 1u,
                                                   &frame_size),
                          false);
    failed += expect_u32("reject short decode",
                         (uint32_t)refmem_sync_frame_decode_header(frame,
                                                                    sizeof(frame) - 1u,
                                                                    &header),
                         REFMEM_SYNC_FRAME_BAD_FRAME_SIZE);
    return failed;
}

static int roundtrip_payload(const char *name,
                             uint8_t frame_type,
                             const void *payload_in,
                             uint16_t payload_in_size)
{
    int failed = 0;
    refmem_sync_frame_header_t header;
    refmem_sync_frame_header_t decoded;
    uint8_t frame[REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX];
    size_t frame_size = 0u;
    const uint8_t *payload_out = NULL;
    uint16_t payload_out_size = 0u;

    failed += expect_bool(name,
                          refmem_sync_frame_header_init(&header,
                                                        frame_type,
                                                        REFMEM_SYNC_FRAME_FLAG_TIMESTAMP_VALID,
                                                        2u,
                                                        0x05u,
                                                        10u,
                                                        20u,
                                                        30u,
                                                        29u,
                                                        123456u,
                                                        payload_in,
                                                        payload_in_size),
                          true);
    failed += expect_bool("roundtrip encode",
                          refmem_sync_frame_encode(&header,
                                                   payload_in,
                                                   payload_in_size,
                                                   frame,
                                                   sizeof(frame),
                                                   &frame_size),
                          true);
    failed += expect_u32("roundtrip validate",
                         (uint32_t)refmem_sync_frame_validate(frame,
                                                              frame_size,
                                                              &decoded,
                                                              &payload_out,
                                                              &payload_out_size),
                         REFMEM_SYNC_FRAME_OK);
    failed += expect_u32("roundtrip type", decoded.frame_type, frame_type);
    failed += expect_u32("roundtrip payload size", payload_out_size, payload_in_size);
    failed += expect_bool("roundtrip payload equal",
                          payload_out != NULL &&
                              memcmp(payload_out, payload_in, payload_in_size) == 0,
                          true);
    return failed;
}

static int test_all_payload_contracts_roundtrip(void)
{
    int failed = 0;
    refmem_sync_epoch_payload_t epoch;
    refmem_sync_delta_header_t delta;
    refmem_sync_command_payload_t command;
    refmem_sync_ack_nack_payload_t ack;
    refmem_sync_fence_payload_t fence;
    refmem_sync_quality_payload_t quality;

    (void)memset(&epoch, 0, sizeof(epoch));
    epoch.table_seq = 100u;
    epoch.layout_crc32 = 0x11111111u;
    epoch.application_crc32 = 0x22222222u;
    epoch.config_crc32 = 0x33333333u;
    epoch.calibration_crc32 = 0x44444444u;
    epoch.sync_profile_crc32 = 0x55555555u;
    epoch.quality_epoch = 7u;
    failed += roundtrip_payload("epoch header init",
                                REFMEM_SYNC_FRAME_EPOCH,
                                &epoch,
                                sizeof(epoch));

    (void)memset(&delta, 0, sizeof(delta));
    delta.delta_id = 1u;
    delta.slot_id = 3u;
    delta.payload_kind = 2u;
    delta.slot_seq = 123u;
    delta.field_id = 45u;
    delta.field_offset = 64u;
    delta.field_width = 4u;
    delta.dirty_mask = 0x00000010u;
    failed += roundtrip_payload("delta header init",
                                REFMEM_SYNC_FRAME_DELTA,
                                &delta,
                                sizeof(delta));

    (void)memset(&command, 0, sizeof(command));
    command.command_seq = 9u;
    command.command_type = 4u;
    command.command_class = 2u;
    command.source_instance = 1u;
    command.target_mask = 0x03u;
    command.required_mask = 0x01u;
    command.payload_kind = 1u;
    command.payload_ref = 0xC000u;
    command.payload_size = 16u;
    command.payload_crc32 = 0xAABBCCDDu;
    command.timeout_1e3ns = 10000u;
    failed += roundtrip_payload("command header init",
                                REFMEM_SYNC_FRAME_COMMAND,
                                &command,
                                sizeof(command));

    (void)memset(&ack, 0, sizeof(ack));
    ack.command_seq = command.command_seq;
    ack.delta_seq32 = 30u;
    ack.taken_flags = 0x03u;
    ack.ack_flags = 0x01u;
    ack.busy_flags = 0x02u;
    ack.last_reason = 0u;
    ack.last_reason_slot = 1u;
    ack.evidence_index = 77u;
    failed += roundtrip_payload("ack header init",
                                REFMEM_SYNC_FRAME_ACK_NACK,
                                &ack,
                                sizeof(ack));

    (void)memset(&fence, 0, sizeof(fence));
    fence.fence_seq = 11u;
    fence.fence_scope = 2u;
    fence.required_mask = 0x03u;
    fence.min_table_seq = 100u;
    fence.layout_crc32 = epoch.layout_crc32;
    fence.application_crc32 = epoch.application_crc32;
    fence.config_crc32 = epoch.config_crc32;
    fence.calibration_crc32 = epoch.calibration_crc32;
    fence.sync_profile_crc32 = epoch.sync_profile_crc32;
    fence.deadline_1e3ns = 50000u;
    failed += roundtrip_payload("fence header init",
                                REFMEM_SYNC_FRAME_FENCE,
                                &fence,
                                sizeof(fence));

    (void)memset(&quality, 0, sizeof(quality));
    quality.quality_id = 5u;
    quality.scope = 2u;
    quality.source_slot = 1u;
    quality.target_slot = 2u;
    quality.seq_expected = 31u;
    quality.seq_last = 30u;
    quality.crc_error_count = 1u;
    quality.drop_count = 2u;
    quality.late_count = 3u;
    quality.timeout_count = 4u;
    quality.last_error = 9u;
    quality.p99_1e3ns = 80u;
    quality.p999_1e3ns = 120u;
    quality.evidence_index = 88u;
    failed += roundtrip_payload("quality header init",
                                REFMEM_SYNC_FRAME_QUALITY,
                                &quality,
                                sizeof(quality));

    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_hello_encode_decode();
    failed += test_crc_and_header_failures();
    failed += test_rejects_bad_inputs();
    failed += test_all_payload_contracts_roundtrip();

    if (failed != 0) {
        (void)printf("refmem_sync_frame tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_sync_frame tests passed\n");
    return 0;
}
