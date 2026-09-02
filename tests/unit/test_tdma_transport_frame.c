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

    uint8_t corpus_packet[sizeof(packet)];
    memcpy(corpus_packet, packet, sizeof(corpus_packet));
    for (size_t length = 0u; length < packet_size; length++) {
        failed += expect_bool("reject truncated packet",
                              tdma_transport_frame_decode(corpus_packet,
                                                          length,
                                                          &view,
                                                          &result),
                              false);
    }
    for (size_t offset = 0u;
         offset < TDMA_TRANSPORT_FRAME_HEADER_SIZE;
         offset++) {
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            corpus_packet[offset] ^= (uint8_t)(1u << bit);
            failed += expect_bool("reject single-bit packet mutation",
                                  tdma_transport_frame_decode(corpus_packet,
                                                              packet_size,
                                                              &view,
                                                              &result),
                                  false);
            corpus_packet[offset] ^= (uint8_t)(1u << bit);
        }
    }
    corpus_packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE] ^= 0x01u;
    failed += expect_bool("accept mutable payload mutation in flight",
                          tdma_transport_frame_decode(corpus_packet,
                                                      packet_size,
                                                      &view,
                                                      &result),
                          true);
    uint32_t calculated_transport_crc32 = 0u;
    failed += expect_bool("calculate transport crc",
                          tdma_transport_frame_calculate_transport_crc32(
                              packet, packet_size,
                              &calculated_transport_crc32),
                          true);
    failed += expect_u32("calculated transport crc",
                         calculated_transport_crc32,
                         view.transport_crc32);
    failed += expect_u32("payload crc helper deterministic",
                         tdma_transport_crc32_compute(payload,
                                                      sizeof(payload)),
                         tdma_transport_crc32_compute(
                             packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
                             sizeof(payload)));
    failed += expect_bool("reject short crc calculation",
                          tdma_transport_frame_calculate_transport_crc32(
                              packet,
                              TDMA_TRANSPORT_FRAME_HEADER_SIZE - 1u,
                              &calculated_transport_crc32),
                          false);
    corpus_packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE] ^= 0x01u;
    failed += expect_bool("decode after mutation corpus",
                          tdma_transport_frame_decode(packet,
                                                      packet_size,
                                                      &view,
                                                      &result),
                          true);

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
    failed += expect_u32("flight transport crc stable",
                         view.transport_crc32,
                         transport_crc32);
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

    uint8_t incomplete_packet[sizeof(packet)];
    size_t incomplete_packet_size = 0u;
    failed += expect_bool("encode incomplete returned frame",
                          tdma_transport_frame_encode(
                              &build,
                              incomplete_packet,
                              sizeof(incomplete_packet),
                              &incomplete_packet_size,
                              &result),
                          true);
    uint8_t unchanged_incomplete_packet[sizeof(incomplete_packet)];
    memcpy(unchanged_incomplete_packet,
           incomplete_packet,
           sizeof(unchanged_incomplete_packet));
    failed += expect_bool("reject incomplete returned frame",
                          tdma_transport_frame_begin_next_cycle(
                              incomplete_packet,
                              incomplete_packet_size,
                              2u,
                              0u,
                              NULL,
                              0u,
                              &result),
                          false);
    failed += expect_u32("incomplete returned frame result",
                         result,
                         TDMA_TRANSPORT_BAD_ROUTE);
    failed += expect_bool("incomplete returned frame unchanged",
                          memcmp(incomplete_packet,
                                 unchanged_incomplete_packet,
                                 sizeof(incomplete_packet)) == 0,
                          true);

    uint8_t returned_packet[sizeof(packet)];
    memcpy(returned_packet, packet, sizeof(returned_packet));
    uint8_t unchanged_returned_packet[sizeof(packet)];
    memcpy(unchanged_returned_packet, packet, sizeof(unchanged_returned_packet));
    failed += expect_bool("reject next cycle at wrong reference",
                          tdma_transport_frame_begin_next_cycle(
                              returned_packet,
                              sizeof(returned_packet),
                              3u,
                              0u,
                              NULL,
                              0u,
                              &result),
                          false);
    failed += expect_u32("wrong reference result",
                         result,
                         TDMA_TRANSPORT_BAD_ROUTE);
    failed += expect_bool("wrong reference leaves frame unchanged",
                          memcmp(returned_packet,
                                 unchanged_returned_packet,
                                 sizeof(returned_packet)) == 0,
                          true);

    failed += expect_bool("reject next cycle patch overflow",
                          tdma_transport_frame_begin_next_cycle(
                              returned_packet,
                              sizeof(returned_packet),
                              2u,
                              sizeof(payload),
                              flight_patch,
                              1u,
                              &result),
                          false);
    failed += expect_u32("next cycle patch overflow result",
                         result,
                         TDMA_TRANSPORT_CAPACITY_REJECTED);
    failed += expect_bool("patch overflow leaves frame unchanged",
                          memcmp(returned_packet,
                                 unchanged_returned_packet,
                                 sizeof(returned_packet)) == 0,
                          true);

    const uint8_t reference_patch[] = {0xC3u, 0x3Cu};
    failed += expect_bool("begin next resident cycle",
                          tdma_transport_frame_begin_next_cycle(
                              returned_packet,
                              sizeof(returned_packet),
                              2u,
                              0u,
                              reference_patch,
                              sizeof(reference_patch),
                              &result),
                          true);
    failed += expect_bool("decode next resident cycle",
                          tdma_transport_frame_decode(returned_packet,
                                                      sizeof(returned_packet),
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("next cycle sequence", view.transport_sequence, 18u);
    failed += expect_u32("next cycle hop reset", view.hop_count, 0u);
    failed += expect_u32("next cycle local route",
                         tdma_transport_frame_route(&view, 2u),
                         TDMA_TRANSPORT_ROUTE_LOCAL_TX);
    failed += expect_bool("next cycle identity refreshed",
                          view.identity_crc32 != identity_crc32,
                          true);
    failed += expect_bool("next cycle transport crc refreshed",
                          view.transport_crc32 != transport_crc32,
                          true);
    failed += expect_bool("next cycle local payload byte 0",
                          view.payload[0] == reference_patch[0],
                          true);
    failed += expect_bool("next cycle local payload byte 1",
                          view.payload[1] == reference_patch[1],
                          true);
    failed += expect_u32("next cycle preserves payload byte 2",
                         view.payload[2],
                         0x5Au);
    failed += expect_u32("next cycle preserves payload byte 3",
                         view.payload[3],
                         payload[3]);

    uint8_t positioned_packet[sizeof(returned_packet)];
    memcpy(positioned_packet, returned_packet, sizeof(positioned_packet));
    uint8_t positioned_payload[sizeof(payload)];
    memcpy(positioned_payload,
           positioned_packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
           sizeof(positioned_payload));
    failed += expect_bool("prepare resident position",
                          tdma_transport_frame_prepare_resident_position(
                              positioned_packet,
                              sizeof(positioned_packet),
                              23u,
                              3u,
                              &result),
                          true);
    failed += expect_bool("decode resident position",
                          tdma_transport_frame_decode(positioned_packet,
                                                      sizeof(positioned_packet),
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("resident position sequence",
                         view.transport_sequence,
                         23u);
    failed += expect_u32("resident position hop", view.hop_count, 3u);
    failed += expect_bool("resident position payload preserved",
                          memcmp(view.payload,
                                 positioned_payload,
                                 sizeof(positioned_payload)) == 0,
                          true);
    failed += expect_bool("reject resident position overflow",
                          tdma_transport_frame_prepare_resident_position(
                              positioned_packet,
                              sizeof(positioned_packet),
                              24u,
                              5u,
                              &result),
                          false);
    failed += expect_u32("resident position overflow result",
                         result,
                         TDMA_TRANSPORT_BAD_ROUTE);

    uint8_t no_update_packet[sizeof(packet)];
    memcpy(no_update_packet, packet, sizeof(no_update_packet));
    uint8_t returned_payload[sizeof(payload)];
    memcpy(returned_payload,
           packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
           sizeof(returned_payload));
    failed += expect_bool("begin next cycle without local update",
                          tdma_transport_frame_begin_next_cycle(
                              no_update_packet,
                              sizeof(no_update_packet),
                              2u,
                              0u,
                              NULL,
                              0u,
                              &result),
                          true);
    failed += expect_bool("decode no-update next cycle",
                          tdma_transport_frame_decode(no_update_packet,
                                                      sizeof(no_update_packet),
                                                      &view,
                                                      &result),
                          true);
    failed += expect_bool("no-update cycle preserves entire payload",
                          memcmp(view.payload,
                                 returned_payload,
                                 sizeof(returned_payload)) == 0,
                          true);

    tdma_transport_frame_build_t wrapping_build = build;
    wrapping_build.transport_sequence = UINT32_MAX;
    uint8_t wrapping_packet[sizeof(packet)];
    size_t wrapping_packet_size = 0u;
    failed += expect_bool("encode wrapping cycle",
                          tdma_transport_frame_encode(
                              &wrapping_build,
                              wrapping_packet,
                              sizeof(wrapping_packet),
                              &wrapping_packet_size,
                              &result),
                          true);
    for (uint32_t hop = 0u; hop < wrapping_build.hop_limit; hop++) {
        failed += expect_bool("advance wrapping cycle",
                              tdma_transport_frame_advance_hop(
                                  wrapping_packet,
                                  wrapping_packet_size,
                                  &result),
                              true);
    }
    failed += expect_bool("begin wrapped resident cycle",
                          tdma_transport_frame_begin_next_cycle(
                              wrapping_packet,
                              wrapping_packet_size,
                              wrapping_build.origin_slot_id,
                              0u,
                              NULL,
                              0u,
                              &result),
                          true);
    failed += expect_bool("decode wrapped resident cycle",
                          tdma_transport_frame_decode(wrapping_packet,
                                                      wrapping_packet_size,
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("wrapped cycle sequence",
                         view.transport_sequence,
                         0u);
    failed += expect_u32("wrapped cycle hop reset", view.hop_count, 0u);

    uint8_t corrupted[sizeof(packet)];
    memcpy(corrupted, packet, sizeof(corrupted));
    corrupted[TDMA_TRANSPORT_FRAME_HEADER_SIZE] ^= 0x01u;
    failed += expect_bool("accept later mutable payload overlay",
                          tdma_transport_frame_decode(corrupted,
                                                      sizeof(corrupted),
                                                      &view,
                                                      &result),
                          true);
    failed += expect_u32("mutable overlay transport result",
                         result,
                         TDMA_TRANSPORT_OK);

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
