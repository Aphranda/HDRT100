#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_codec.h"

static void expect_bytes(const uint8_t *actual, size_t actual_len,
                         const uint8_t *expected, size_t expected_len)
{
    assert(actual_len == expected_len);
    assert(memcmp(actual, expected, expected_len) == 0);
}

static void test_golden_data(void)
{
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    const uint8_t payload[] = {0xDEu, 0xADu};
    const uint8_t expected[] = {
        0x02u, 0x03u, 0x00u, 0x01u,
        0x44u, 0x33u, 0x22u, 0x11u,
        0xDDu, 0xCCu, 0xBBu, 0xAAu,
        0xDEu, 0xADu,
    };
    const size_t len = ota_tdma_encode_data(frame, sizeof(frame), 0x11223344u,
                                            0xAABBCCDDu, payload,
                                            sizeof(payload), true);
    expect_bytes(frame, len, expected, sizeof(expected));

    ota_tdma_frame_t decoded;
    assert(ota_tdma_decode(&decoded, frame, len));
    assert(decoded.type == (uint8_t)OTA_TDMA_FRAME_DATA);
    assert(decoded.offset == 0x11223344u);
    assert(decoded.crc32 == 0xAABBCCDDu);
    assert(decoded.payload_len == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
    assert((decoded.flags & OTA_TDMA_FLAG_FINAL) != 0u);
}

static void test_golden_ack_close_abort(void)
{
    uint8_t frame[OTA_TDMA_FRAME_MAX];

    const uint8_t ack_expected[] = {
        0x03u, 0x00u, 0x00u, 0x01u,
        0x04u, 0x03u, 0x02u, 0x01u, 0x00u, 0x0Du,
    };
    size_t len = ota_tdma_encode_ack(frame, sizeof(frame), 0x01020304u, 0u, 13u);
    expect_bytes(frame, len, ack_expected, sizeof(ack_expected));
    ota_tdma_frame_t decoded;
    assert(ota_tdma_decode(&decoded, frame, len));
    assert(decoded.offset == 0x01020304u && decoded.reason == 13u);

    const uint8_t close_expected[] = {
        0x04u, 0x02u, 0x00u, 0x01u,
        0xCDu, 0xABu, 0x00u, 0x00u,
        0x78u, 0x56u, 0x34u, 0x12u,
    };
    len = ota_tdma_encode_close(frame, sizeof(frame), 0x0000ABCDu, 0x12345678u);
    expect_bytes(frame, len, close_expected, sizeof(close_expected));

    const uint8_t abort_expected[] = {0x05u, 0x00u, 0x00u, 0x01u, 0x07u};
    len = ota_tdma_encode_abort(frame, sizeof(frame), 7u);
    expect_bytes(frame, len, abort_expected, sizeof(abort_expected));
}

static void test_open_fragments(void)
{
    uint8_t wire[OTA_TDMA_OPEN_WIRE_SIZE];
    for (size_t i = 0u; i < sizeof(wire); ++i) {
        wire[i] = (uint8_t)(i * 3u + 1u);
    }
    const uint8_t total = 2u;
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    const size_t first = ota_tdma_encode_open(frame, sizeof(frame), wire,
                                              sizeof(wire), 0u, total);
    assert(first == OTA_TDMA_FRAME_MAX);
    const size_t second = ota_tdma_encode_open(frame, sizeof(frame), wire,
                                               sizeof(wire), 1u, total);
    assert(second == OTA_TDMA_HEADER_SIZE + (OTA_TDMA_OPEN_WIRE_SIZE -
                                             OTA_TDMA_OPEN_FRAGMENT_MAX));

    ota_tdma_open_reassembly_t reassembly;
    ota_tdma_open_reassembly_reset(&reassembly);
    ota_tdma_frame_t decoded;
    assert(ota_tdma_decode(&decoded, frame, second));
    assert(!ota_tdma_open_reassembly_step(&reassembly, &decoded));

    uint8_t frame0[OTA_TDMA_FRAME_MAX];
    const size_t len0 = ota_tdma_encode_open(frame0, sizeof(frame0), wire,
                                             sizeof(wire), 0u, total);
    assert(ota_tdma_decode(&decoded, frame0, len0));
    assert(ota_tdma_open_reassembly_step(&reassembly, &decoded));
    assert(reassembly.length == OTA_TDMA_OPEN_WIRE_SIZE);
    assert(memcmp(reassembly.buffer, wire, sizeof(wire)) == 0);
}

static void test_rejections(void)
{
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    ota_tdma_frame_t decoded;

    /* Unknown type. */
    const uint8_t unknown[] = {0x09u, 0x00u, 0x00u, 0x01u};
    assert(!ota_tdma_decode(&decoded, unknown, sizeof(unknown)));

    /* DATA without CRC flag / too short. */
    const uint8_t data_no_crc[] = {0x02u, 0x00u, 0x00u, 0x01u, 0, 0, 0, 0,
                                   0, 0, 0, 0};
    assert(!ota_tdma_decode(&decoded, data_no_crc, sizeof(data_no_crc)));
    const uint8_t data_short[] = {0x02u, 0x02u, 0x00u, 0x01u, 0, 0, 0, 0};
    assert(!ota_tdma_decode(&decoded, data_short, sizeof(data_short)));

    /* ACK wrong length. */
    const uint8_t ack_short[] = {0x03u, 0x00u, 0x00u, 0x01u, 1, 2, 3};
    assert(!ota_tdma_decode(&decoded, ack_short, sizeof(ack_short)));

    /* OPEN with seq >= total. */
    const uint8_t open_bad[] = {0x01u, 0x00u, 0x02u, 0x02u, 0xAAu};
    assert(!ota_tdma_decode(&decoded, open_bad, sizeof(open_bad)));

    /* Oversized frame and truncated header. */
    uint8_t oversized[OTA_TDMA_FRAME_MAX + 1u] = {0};
    assert(!ota_tdma_decode(&decoded, oversized, sizeof(oversized)));
    assert(!ota_tdma_decode(&decoded, oversized, 3u));

    /* Capacity and argument failures. */
    assert(ota_tdma_encode_data(frame, 4u, 0u, 0u, NULL, 0u, false) == 0u);
    assert(ota_tdma_encode_data(frame, sizeof(frame), 0u, 0u, NULL,
                                OTA_TDMA_DATA_MAX_PAYLOAD + 1u, false) == 0u);
    assert(ota_tdma_encode_ack(NULL, 16u, 0u, 0u, 0u) == 0u);
    assert(ota_tdma_encode_open(frame, sizeof(frame), NULL,
                                OTA_TDMA_OPEN_WIRE_SIZE, 0u, 2u) == 0u);
}

static uint32_t lcg_next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void test_fuzz_decode_is_bounded(void)
{
    uint32_t state = 0xC0FFEEu;
    uint8_t buffer[OTA_TDMA_FRAME_MAX + 8u];
    for (unsigned iteration = 0u; iteration < 20000u; ++iteration) {
        const size_t len = (size_t)(lcg_next(&state) % (sizeof(buffer) + 1u));
        for (size_t i = 0u; i < len; ++i) {
            buffer[i] = (uint8_t)lcg_next(&state);
        }
        ota_tdma_frame_t frame;
        const bool ok = ota_tdma_decode(&frame, buffer, len);
        if (ok) {
            assert(frame.type >= (uint8_t)OTA_TDMA_FRAME_OPEN);
            assert(frame.type <= (uint8_t)OTA_TDMA_FRAME_ABORT);
            assert(frame.payload_len <= OTA_TDMA_DATA_MAX_PAYLOAD);
        }
    }
}

static void test_fuzz_roundtrip(void)
{
    uint32_t state = 0x5EEDu;
    for (unsigned iteration = 0u; iteration < 5000u; ++iteration) {
        uint8_t payload[OTA_TDMA_DATA_MAX_PAYLOAD];
        const size_t payload_len =
            (size_t)(lcg_next(&state) % (OTA_TDMA_DATA_MAX_PAYLOAD + 1u));
        for (size_t i = 0u; i < payload_len; ++i) {
            payload[i] = (uint8_t)lcg_next(&state);
        }
        const uint32_t offset = lcg_next(&state);
        const uint32_t crc = lcg_next(&state);
        uint8_t frame[OTA_TDMA_FRAME_MAX];
        const size_t len = ota_tdma_encode_data(frame, sizeof(frame), offset,
                                                crc, payload, payload_len,
                                                (iteration & 1u) != 0u);
        assert(len != 0u && len <= OTA_TDMA_FRAME_MAX);
        ota_tdma_frame_t decoded;
        assert(ota_tdma_decode(&decoded, frame, len));
        assert(decoded.offset == offset && decoded.crc32 == crc);
        assert(decoded.payload_len == payload_len);
        assert(memcmp(decoded.payload, payload, payload_len) == 0);
    }
}

int main(void)
{
    test_golden_data();
    test_golden_ack_close_abort();
    test_open_fragments();
    test_rejections();
    test_fuzz_decode_is_bounded();
    test_fuzz_roundtrip();
    printf("OTA TDMA codec host unit tests passed\n");
    return 0;
}
