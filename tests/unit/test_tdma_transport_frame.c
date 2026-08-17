#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_transport_frame.h"

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr,
            "FAIL %s: got %u expected %u\n",
            name,
            actual ? 1u : 0u,
            expected ? 1u : 0u);
    return 1;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr,
            "FAIL %s: got %lu expected %lu\n",
            name,
            (unsigned long)actual,
            (unsigned long)expected);
    return 1;
}

int main(void)
{
    int failed = 0;
    const uint8_t payload[] = {0x10u, 0x20u, 0x30u, 0x40u, 0x50u};
    uint8_t packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE + sizeof(payload)];
    size_t packet_size = 0u;
    tdma_transport_result_t result = TDMA_TRANSPORT_BAD_ARGUMENT;
    tdma_transport_frame_view_t view;
    const tdma_transport_frame_build_t build = {
        .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id = 2u,
        .transport_sequence = 17u,
        .payload_class = 2u,
        .flags = TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK |
                 TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE,
        .schedule_crc32 = 0x11223344u,
        .ring_profile_crc32 = 0x55667788u,
        .hop_limit = 4u,
        .payload = payload,
        .payload_size = sizeof(payload),
    };

    failed += expect_u32("header size",
                         TDMA_TRANSPORT_FRAME_HEADER_SIZE,
                         32u);
    failed += expect_bool("encode",
                          tdma_transport_frame_encode(&build,
                                                      packet,
                                                      sizeof(packet),
                                                      &packet_size,
                                                      &result),
                          true);
    failed += expect_u32("encode result", result, TDMA_TRANSPORT_OK);
    failed += expect_u32("packet size",
                         (uint32_t)packet_size,
                         TDMA_TRANSPORT_FRAME_HEADER_SIZE + sizeof(payload));
    failed += expect_bool("decode",
                          tdma_transport_frame_decode(packet,
                                                      packet_size,
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("origin", view.origin_slot_id, 2u);
    failed += expect_u32("short class",
                         view.frame_class,
                         TDMA_TRANSPORT_FRAME_CLASS_SHORT);
    failed += expect_u32("sequence", view.transport_sequence, 17u);
    failed += expect_u32("payload class", view.payload_class, 2u);
    failed += expect_u32("hop count initial", view.hop_count, 0u);
    failed += expect_u32("hop limit", view.hop_limit, 4u);
    failed += expect_bool("payload equal",
                          memcmp(view.payload, payload, sizeof(payload)) == 0,
                          true);
    failed += expect_u32("local route",
                         tdma_transport_frame_route(&view, 2u),
                         TDMA_TRANSPORT_ROUTE_LOCAL_TX);
    failed += expect_u32("forward route",
                         tdma_transport_frame_route(&view, 3u),
                         TDMA_TRANSPORT_ROUTE_FORWARD);

    const uint32_t identity_crc32 = view.identity_crc32;
    const uint32_t transport_crc32 = view.transport_crc32;
    const uint8_t flight_patch[] = {0xA5u, 0x5Au};
    failed += expect_bool("patch flight payload",
                          tdma_transport_frame_patch_flight_payload(
                              packet,
                              packet_size,
                              1u,
                              flight_patch,
                              sizeof(flight_patch),
                              &result),
                          true);
    failed += expect_bool("decode patched payload",
                          tdma_transport_frame_decode(packet,
                                                      packet_size,
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("flight identity stable",
                         view.identity_crc32,
                         identity_crc32);
    failed += expect_bool("flight transport crc changes",
                          view.transport_crc32 != transport_crc32,
                          true);
    failed += expect_u32("flight payload byte 1", view.payload[1], 0xA5u);
    failed += expect_u32("flight payload byte 2", view.payload[2], 0x5Au);
    const uint32_t patched_transport_crc32 = view.transport_crc32;
    failed += expect_bool("advance hop 1",
                          tdma_transport_frame_advance_hop(packet,
                                                           packet_size,
                                                           &result),
                          true);
    failed += expect_bool("decode hop 1",
                          tdma_transport_frame_decode(packet,
                                                      packet_size,
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("hop count 1", view.hop_count, 1u);
    failed += expect_u32("identity stable",
                         view.identity_crc32,
                         identity_crc32);
    failed += expect_bool("transport crc changes",
                          view.transport_crc32 != patched_transport_crc32,
                          true);
    failed += expect_u32("origin feedback route",
                         tdma_transport_frame_route(&view, 2u),
                         TDMA_TRANSPORT_ROUTE_FEEDBACK);

    for (uint32_t hop = 2u; hop <= 4u; hop++) {
        failed += expect_bool("advance remaining hop",
                              tdma_transport_frame_advance_hop(packet,
                                                               packet_size,
                                                               &result),
                              true);
    }
    failed += expect_bool("decode hop limit",
                          tdma_transport_frame_decode(packet,
                                                      packet_size,
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("hop count limit", view.hop_count, 4u);
    failed += expect_u32("non-origin drop",
                         tdma_transport_frame_route(&view, 3u),
                         TDMA_TRANSPORT_ROUTE_DROP);
    failed += expect_bool("reject hop overflow",
                          tdma_transport_frame_advance_hop(packet,
                                                           packet_size,
                                                           &result),
                          false);
    failed += expect_u32("hop overflow result",
                         result,
                         TDMA_TRANSPORT_HOP_LIMIT_REACHED);

    uint8_t corrupted[sizeof(packet)];
    memcpy(corrupted, packet, sizeof(corrupted));
    corrupted[TDMA_TRANSPORT_FRAME_HEADER_SIZE] ^= 0x01u;
    failed += expect_bool("reject corrupt packet",
                          tdma_transport_frame_decode(corrupted,
                                                      sizeof(corrupted),
                                                      &view,
                                                      &result),
                          false);
    failed += expect_u32("corrupt transport result",
                         result,
                         TDMA_TRANSPORT_CRC_MISMATCH);

    uint8_t idle_packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE];
    const tdma_transport_frame_build_t idle = {
        .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id = 0u,
        .transport_sequence = 18u,
        .payload_class = 4u,
        .flags = TDMA_TRANSPORT_FLAG_IDLE_BEACON |
                 TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK,
        .schedule_crc32 = 0x11223344u,
        .ring_profile_crc32 = 0x55667788u,
        .hop_limit = 2u,
    };
    failed += expect_bool("encode zero payload idle",
                          tdma_transport_frame_encode(&idle,
                                                      idle_packet,
                                                      sizeof(idle_packet),
                                                      &packet_size,
                                                      &result),
                          true);
    failed += expect_bool("decode zero payload idle",
                          tdma_transport_frame_decode(idle_packet,
                                                      packet_size,
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("idle payload size", view.payload_size, 0u);

    uint8_t oversized_payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX + 1u] = {0u};
    tdma_transport_frame_build_t oversized = build;
    oversized.payload = oversized_payload;
    oversized.payload_size = sizeof(oversized_payload);
    failed += expect_bool("short payload bounded",
                          tdma_transport_frame_encode(&oversized,
                                                      packet,
                                                      sizeof(packet),
                                                      &packet_size,
                                                      &result),
                          false);
    failed += expect_u32("short capacity result",
                         result,
                         TDMA_TRANSPORT_CAPACITY_REJECTED);

    uint8_t long_packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE + sizeof(payload)];
    tdma_transport_frame_build_t long_build = build;
    long_build.frame_class = TDMA_TRANSPORT_FRAME_CLASS_LONG;
    long_build.flags &= ~TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE;
    failed += expect_bool("encode long",
                          tdma_transport_frame_encode(&long_build,
                                                      long_packet,
                                                      sizeof(long_packet),
                                                      &packet_size,
                                                      &result),
                          true);
    failed += expect_bool("decode long",
                          tdma_transport_frame_decode(long_packet,
                                                      packet_size,
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("long class",
                         view.frame_class,
                         TDMA_TRANSPORT_FRAME_CLASS_LONG);

    if (failed != 0) {
        return 1;
    }
    puts("tdma_transport_frame tests passed");
    return 0;
}
