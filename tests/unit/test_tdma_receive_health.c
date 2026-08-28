#include "tdma_receive_health.h"

#include "tdma_profile.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) {
        return 0;
    }
    (void)fprintf(stderr, "FAIL %s: got %u expected %u\n",
                  name, actual ? 1u : 0u, expected ? 1u : 0u);
    return 1;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    (void)fprintf(stderr, "FAIL %s: got %lu expected %lu\n",
                  name, (unsigned long)actual, (unsigned long)expected);
    return 1;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual == expected) {
        return 0;
    }
    (void)fprintf(stderr, "FAIL %s: got %llu expected %llu\n",
                  name,
                  (unsigned long long)actual,
                  (unsigned long long)expected);
    return 1;
}

static tdma_transport_frame_view_t make_view(uint8_t *payload,
                                             size_t payload_size,
                                             uint32_t sequence)
{
    tdma_transport_frame_view_t view;
    memset(&view, 0, sizeof(view));
    view.transport_sequence = sequence;
    view.identity_crc32 = 0x50000000u + sequence;
    view.payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE;
    view.flags = TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK |
                 TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE;
    view.schedule_crc32 = 0x11223344u;
    view.ring_profile_crc32 = 0x55667788u;
    view.payload = payload;
    view.payload_size = payload_size;
    return view;
}

int main(void)
{
    int failed = 0;
    tdma_receive_health_t health;
    tdma_receive_health_snapshot_t snapshot;
    tdma_receive_image_t image;
    tdma_receive_reason_t reason = TDMA_RECEIVE_REASON_NONE;
    uint8_t payload[64];
    memset(payload, 0x3cu, sizeof(payload));
    const tdma_receive_health_config_t config = {
        .schedule_crc32 = 0x11223344u,
        .ring_profile_crc32 = 0x55667788u,
        .map_generation = 7u,
        .expected_payload_size = sizeof(payload),
        .expected_segment_mask = (1u << 0u) | (1u << 2u),
        .stale_timeout_ns = 1000ull,
    };

    failed += expect_bool("init", tdma_receive_health_init(&health), true);
    failed += expect_bool("empty image",
                          tdma_receive_health_read_image(&health, 1ull, &image),
                          false);
    failed += expect_bool("configure",
                          tdma_receive_health_configure_stopped(&health,
                                                                &config),
                          true);

    tdma_transport_frame_view_t view = make_view(payload, sizeof(payload), 10u);
    failed += expect_bool("accept first image",
                          tdma_receive_health_evaluate(
                              &health, &view, TDMA_TRANSPORT_OK,
                              config.expected_segment_mask, 100ull, &reason),
                          true);
    failed += expect_u32("first reason", reason, TDMA_RECEIVE_REASON_NONE);
    failed += expect_bool("read valid image",
                          tdma_receive_health_read_image(&health, 150ull, &image),
                          true);
    failed += expect_u32("valid state", image.health.state,
                         TDMA_RECEIVE_STATE_VALID);
    failed += expect_u32("first generation", image.health.image_generation, 1u);
    failed += expect_u32("first sequence", image.health.accepted_sequence, 10u);
    failed += expect_u32("first WKC", image.health.accepted_wkc, 2u);
    failed += expect_u32("expected WKC", image.health.expected_wkc, 2u);
    failed += expect_u64("valid age", image.health.stale_age_ns, 50ull);
    failed += expect_bool("first payload retained",
                          memcmp(image.data, payload, sizeof(payload)) == 0,
                          true);

    payload[0] = 0xa5u;
    failed += expect_bool("duplicate rejected",
                          tdma_receive_health_evaluate(
                              &health, &view, TDMA_TRANSPORT_OK,
                              config.expected_segment_mask, 200ull, &reason),
                          false);
    failed += expect_u32("duplicate reason", reason,
                         TDMA_RECEIVE_REASON_SEQUENCE_DUPLICATE);
    failed += expect_bool("read stale duplicate image",
                          tdma_receive_health_read_image(&health, 250ull, &image),
                          true);
    failed += expect_u32("duplicate stale state", image.health.state,
                         TDMA_RECEIVE_STATE_STALE);
    failed += expect_u32("duplicate keeps generation",
                         image.health.image_generation, 1u);
    failed += expect_u32("duplicate keeps accepted sequence",
                         image.health.accepted_sequence, 10u);
    failed += expect_u32("duplicate keeps old payload", image.data[0], 0x3cu);

    view.transport_sequence = 9u;
    failed += expect_bool("stale sequence rejected",
                          tdma_receive_health_evaluate(
                              &health, &view, TDMA_TRANSPORT_OK,
                              config.expected_segment_mask, 300ull, &reason),
                          false);
    failed += expect_u32("stale sequence reason", reason,
                         TDMA_RECEIVE_REASON_SEQUENCE_STALE);

    view.transport_sequence = 11u;
    failed += expect_bool("incomplete bitmap rejected",
                          tdma_receive_health_evaluate(
                              &health, &view, TDMA_TRANSPORT_OK,
                              1u << 0u, 400ull, &reason),
                          false);
    failed += expect_u32("bitmap reason", reason,
                         TDMA_RECEIVE_REASON_SEGMENT_BITMAP);

    view.payload_size = sizeof(payload) - 1u;
    failed += expect_bool("short payload rejected",
                          tdma_receive_health_evaluate(
                              &health, &view, TDMA_TRANSPORT_OK,
                              config.expected_segment_mask, 500ull, &reason),
                          false);
    failed += expect_u32("short payload reason", reason,
                         TDMA_RECEIVE_REASON_PAYLOAD_SIZE);

    view.payload_size = sizeof(payload);
    failed += expect_bool("next valid image recovers",
                          tdma_receive_health_evaluate(
                              &health, &view, TDMA_TRANSPORT_OK,
                              config.expected_segment_mask, 600ull, &reason),
                          true);
    failed += expect_bool("recovery snapshot",
                          tdma_receive_health_get_snapshot(
                              &health, 650ull, &snapshot),
                          true);
    failed += expect_u32("recovered state", snapshot.state,
                         TDMA_RECEIVE_STATE_VALID);
    failed += expect_u32("recovered generation", snapshot.image_generation, 2u);
    failed += expect_u32("accepted count", snapshot.accepted_count, 2u);
    failed += expect_u32("rejected count", snapshot.rejected_count, 4u);
    failed += expect_u32("last rejected reason",
                         snapshot.last_rejected_reason,
                         TDMA_RECEIVE_REASON_PAYLOAD_SIZE);
    failed += expect_u32("last rejected sequence",
                         snapshot.last_rejected_sequence,
                         11u);
    failed += expect_u32("last rejected observed mask",
                         snapshot.last_rejected_observed_segment_mask,
                         config.expected_segment_mask);
    failed += expect_u32("last rejected expected mask",
                         snapshot.last_rejected_expected_segment_mask,
                         config.expected_segment_mask);
    failed += expect_u32("failure streak cleared",
                         snapshot.consecutive_failure_count, 0u);

    tdma_receive_health_observe_missing(&health, 1600ull);
    failed += expect_bool("timeout boundary stays valid",
                          tdma_receive_health_get_snapshot(
                              &health, 1600ull, &snapshot),
                          true);
    failed += expect_u32("timeout boundary state", snapshot.state,
                         TDMA_RECEIVE_STATE_VALID);
    tdma_receive_health_observe_missing(&health, 1601ull);
    tdma_receive_health_observe_missing(&health, 1700ull);
    failed += expect_bool("missing snapshot",
                          tdma_receive_health_get_snapshot(
                              &health, 1700ull, &snapshot),
                          true);
    failed += expect_u32("missing state", snapshot.state,
                         TDMA_RECEIVE_STATE_STALE);
    failed += expect_u32("missing reason", snapshot.last_reason,
                         TDMA_RECEIVE_REASON_MISSING);
    failed += expect_u32("missing counted once", snapshot.missing_count, 1u);
    failed += expect_u32("missing failure streak",
                         snapshot.consecutive_failure_count, 1u);

    failed += expect_bool("reconfigure resets lifecycle",
                          tdma_receive_health_configure_stopped(&health,
                                                                &config),
                          true);
    failed += expect_bool("reconfigured snapshot",
                          tdma_receive_health_get_snapshot(
                              &health, 1800ull, &snapshot),
                          true);
    failed += expect_u32("reconfigured state", snapshot.state,
                         TDMA_RECEIVE_STATE_EMPTY);
    failed += expect_u32("reconfigured generation", snapshot.image_generation,
                         0u);
    failed += expect_u32("reconfigured accepted count", snapshot.accepted_count,
                         0u);
    failed += expect_u32("reconfigured rejected count", snapshot.rejected_count,
                         0u);
    failed += expect_bool("reconfigured image empty",
                          tdma_receive_health_read_image(
                              &health, 1800ull, &image),
                          false);

    if (failed != 0) {
        (void)fprintf(stderr, "tdma_receive_health tests failed: %d\n", failed);
        return 1;
    }
    (void)puts("tdma_receive_health tests passed");
    return 0;
}
