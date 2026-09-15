#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vdc_time_mapping.h"

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s: expected %d got %d\n", name, expected, actual);
    return 1;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s: expected=%" PRIu64 " got=%" PRIu64 "\n",
            name, expected, actual);
    return 1;
}

static tdma_ring_clock_snapshot_t valid_snapshot(void)
{
    tdma_ring_clock_snapshot_t ring;
    memset(&ring, 0, sizeof(ring));
    ring.enabled = 1u;
    ring.adapter_started = 1u;
    ring.cycle_period_ns = 1000000u;
    ring.feedback_timeout_ns = 500000u;
    ring.schedule_crc32 = 0x12345678u;
    ring.clock_observation.valid = 1u;
    ring.clock_observation.correlated_frame_evidence = 1u;
    ring.clock_observation.correlation_flags =
        TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
        TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME;
    ring.clock_observation.timestamp_flags =
        TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    ring.clock_observation.local_rx_timestamp_ns = 1000000000ull;
    ring.clock_observation.common_effective_time_ns = 2000000000ull;
    return ring;
}

int main(void)
{
    int failed = 0;
    uint64_t common = 0u;
    tdma_ring_clock_snapshot_t ring = valid_snapshot();

    failed += expect_bool("valid mapping",
                          vdc_time_mapping_map_local_to_common_time(
                              &ring, ring.schedule_crc32, 1000000123ull,
                              &common), true);
    failed += expect_u64("mapped common time", common, 2000000123ull);

    failed += expect_bool("null ring",
                          vdc_time_mapping_map_local_to_common_time(
                              NULL, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    failed += expect_bool("null output",
                          vdc_time_mapping_map_local_to_common_time(
                              &ring, ring.schedule_crc32, 1000000000ull,
                              NULL), false);

    tdma_ring_clock_snapshot_t rejected = ring;
    rejected.enabled = 0u;
    failed += expect_bool("disabled ring", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    rejected = ring;
    rejected.adapter_started = 0u;
    failed += expect_bool("adapter stopped", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    rejected = ring;
    rejected.cycle_period_ns = 0u;
    failed += expect_bool("missing cycle period", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    rejected = ring;
    rejected.feedback_timeout_ns = 0u;
    failed += expect_bool("missing feedback timeout", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    rejected = ring;
    rejected.schedule_crc32 ^= 1u;
    failed += expect_bool("schedule mismatch", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);

    rejected = ring;
    rejected.clock_observation.valid = 0u;
    failed += expect_bool("invalid observation", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    rejected = ring;
    rejected.clock_observation.correlated_frame_evidence = 0u;
    failed += expect_bool("uncorrelated observation", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    rejected = ring;
    rejected.clock_observation.correlation_flags = 0u;
    failed += expect_bool("missing common phase flags", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    rejected = ring;
    rejected.clock_observation.timestamp_flags = 0u;
    failed += expect_bool("software timestamp", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);

    rejected = ring;
    rejected.clock_observation.local_rx_timestamp_ns = 0u;
    failed += expect_bool("missing local anchor", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    rejected = ring;
    rejected.clock_observation.common_effective_time_ns = 0u;
    failed += expect_bool("missing common anchor", vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000000ull,
                              &common), false);
    failed += expect_bool("local time reversal",
                          vdc_time_mapping_map_local_to_common_time(
                              &ring, ring.schedule_crc32, 999999999ull,
                              &common), false);
    failed += expect_bool("stale anchor",
                          vdc_time_mapping_map_local_to_common_time(
                              &ring, ring.schedule_crc32, 1000500001ull,
                              &common), false);

    rejected = ring;
    rejected.clock_observation.common_effective_time_ns = UINT64_MAX - 10u;
    failed += expect_bool("common time overflow",
                          vdc_time_mapping_map_local_to_common_time(
                              &rejected, ring.schedule_crc32, 1000000011ull,
                              &common), false);
    failed += expect_bool("zero expected schedule crc",
                          vdc_time_mapping_map_local_to_common_time(
                              &ring, 0u, 1000000000ull, &common), false);

    uint64_t late_ns = UINT64_MAX;
    failed += expect_bool("effective time not due",
                          vdc_time_mapping_classify_effective_time(
                              2000u, 3000u, 1000u, &late_ns) ==
                              VDC_TIME_MAPPING_EFFECTIVE_TIME_NOT_DUE,
                          true);
    failed += expect_u64("not due lateness", late_ns, 0u);
    failed += expect_bool("effective time due",
                          vdc_time_mapping_classify_effective_time(
                              3500u, 3000u, 500u, &late_ns) ==
                              VDC_TIME_MAPPING_EFFECTIVE_TIME_DUE,
                          true);
    failed += expect_u64("due lateness", late_ns, 500u);
    failed += expect_bool("effective time too late",
                          vdc_time_mapping_classify_effective_time(
                              3501u, 3000u, 500u, &late_ns) ==
                              VDC_TIME_MAPPING_EFFECTIVE_TIME_TOO_LATE,
                          true);
    failed += expect_u64("too late amount", late_ns, 501u);
    failed += expect_bool("invalid effective time",
                          vdc_time_mapping_classify_effective_time(
                              3500u, 0u, 500u, &late_ns) ==
                              VDC_TIME_MAPPING_EFFECTIVE_TIME_INVALID,
                          true);
    failed += expect_bool("null lateness output",
                          vdc_time_mapping_classify_effective_time(
                              3500u, 3000u, 500u, NULL) ==
                              VDC_TIME_MAPPING_EFFECTIVE_TIME_INVALID,
                          true);
    failed += expect_bool("sequence first value",
                          vdc_time_mapping_sequence_is_newer(1u, 0u), true);
    failed += expect_bool("sequence duplicate",
                          vdc_time_mapping_sequence_is_newer(7u, 7u), false);
    failed += expect_bool("sequence older",
                          vdc_time_mapping_sequence_is_newer(6u, 7u), false);
    failed += expect_bool("sequence wraps",
                          vdc_time_mapping_sequence_is_newer(1u,
                                                              UINT32_MAX - 1u),
                          true);
    failed += expect_bool("sequence rejects zero",
                          vdc_time_mapping_sequence_is_newer(0u, UINT32_MAX),
                          false);

    if (failed != 0) {
        return 1;
    }
    puts("vdc_time_mapping tests passed");
    return 0;
}
