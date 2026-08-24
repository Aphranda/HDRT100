#include "calibration_training_marker.h"

#include <stdbool.h>
#include <stdio.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n", name,
                     expected ? 1 : 0, actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n", name,
                     (unsigned long)expected, (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %llu got %llu\n", name,
                     (unsigned long long)expected,
                     (unsigned long long)actual);
        return 1;
    }
    return 0;
}

static calibration_training_marker_request_t make_request(void)
{
    const calibration_training_marker_request_t request = {
        .board_unique_id = 0x0010071E65B5CB38ull,
        .build_id = 0x20260824010101ull,
        .role = CALIBRATION_TRAINING_MARKER_ROLE_FOLLOWER,
        .logical_slot = 1u,
        .reference_slot = 0u,
        .predecessor_slot = 0u,
        .successor_slot = 2u,
        .train_epoch = 7u,
        .train_sequence = 11u,
        .marker_id = 3u,
        .marker_codebook_id = 1u,
        .marker_crc32 = 0x12345678u,
        .calibration_generation = 4u,
        .topology_generation = 5u,
        .topology_crc32 = 0x23456789u,
        .profile_crc32 = 0x3456789Au,
        .schedule_crc32 = 0x456789ABu,
        .tick_resolution_ns = 4u,
    };
    return request;
}

static calibration_training_marker_evidence_t make_evidence(void)
{
    const calibration_training_marker_evidence_t evidence = {
        .train_epoch = 7u,
        .train_sequence = 11u,
        .marker_id = 3u,
        .observed_crc32 = 0x12345678u,
        .polarity = 0u,
        .marker_flags = 0x3Fu,
        .correlation_reject_reason = 2u,
        .best_lag_sample = 17u,
        .best_distance = 9u,
        .calibration_generation = 4u,
        .topology_generation = 5u,
        .topology_crc32 = 0x23456789u,
        .profile_crc32 = 0x3456789Au,
        .schedule_crc32 = 0x456789ABu,
        .flags = CALIBRATION_TRAINING_MARKER_REQUIRED_FLAGS,
        .marker_capture_tick = 1000u,
        .marker_forward_tick = 1012u,
        .dma_capture_count = 1u,
    };
    return evidence;
}

static int test_prepare_and_accept(void)
{
    calibration_training_marker_store_t store;
    calibration_training_marker_snapshot_t snapshot;
    const calibration_training_marker_request_t request = make_request();
    const calibration_training_marker_evidence_t evidence = make_evidence();
    int failed = 0;

    calibration_training_marker_store_init(&store);
    failed += expect_bool("prepare",
                          calibration_training_marker_prepare_core1(
                              &store, &request), true);
    failed += expect_bool("prepared snapshot",
                          calibration_training_marker_get_snapshot(
                              &store, &snapshot), true);
    failed += expect_u32("prepared state", snapshot.state,
                         CALIBRATION_TRAINING_MARKER_PREPARED);
    failed += expect_bool("evaluate accepted",
                          calibration_training_marker_evaluate_core1(
                              &store, &request, &evidence), true);
    (void)calibration_training_marker_get_snapshot(&store, &snapshot);
    failed += expect_u32("accepted state", snapshot.state,
                         CALIBRATION_TRAINING_MARKER_ACCEPTED);
    failed += expect_u32("accepted reason", snapshot.reject_reason,
                         CALIBRATION_TRAINING_MARKER_REJECT_NONE);
    failed += expect_u64("capture tick", snapshot.marker_capture_tick, 1000u);
    failed += expect_u64("forward tick", snapshot.marker_forward_tick, 1012u);
    failed += expect_u64("residence ticks", snapshot.forward_residence_ticks,
                         12u);
    failed += expect_u32("marker flags", snapshot.marker_flags, 0x3Fu);
    failed += expect_u32("correlation reject reason",
                         snapshot.correlation_reject_reason, 2u);
    failed += expect_u32("best lag sample", snapshot.best_lag_sample, 17u);
    failed += expect_u32("best distance", snapshot.best_distance, 9u);
    failed += expect_u32("diagnostic retained",
                         snapshot.flags &
                             CALIBRATION_TRAINING_MARKER_FLAG_DIAGNOSTIC_ONLY,
                         CALIBRATION_TRAINING_MARKER_FLAG_DIAGNOSTIC_ONLY);
    return failed;
}

static int expect_reject(
    const char *name,
    calibration_training_marker_request_t request,
    calibration_training_marker_evidence_t evidence,
    uint32_t expected_reason)
{
    calibration_training_marker_store_t store;
    calibration_training_marker_snapshot_t snapshot;
    int failed = 0;
    calibration_training_marker_store_init(&store);
    failed += expect_bool(name,
                          calibration_training_marker_evaluate_core1(
                              &store, &request, &evidence), false);
    (void)calibration_training_marker_get_snapshot(&store, &snapshot);
    failed += expect_u32(name, snapshot.state,
                         CALIBRATION_TRAINING_MARKER_REJECTED);
    failed += expect_u32(name, snapshot.reject_reason, expected_reason);
    return failed;
}

static int test_reject_matrix(void)
{
    calibration_training_marker_request_t request = make_request();
    calibration_training_marker_evidence_t evidence = make_evidence();
    int failed = 0;

    evidence.topology_generation++;
    failed += expect_reject("generation", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_GENERATION);
    evidence = make_evidence();
    evidence.train_epoch++;
    failed += expect_reject("epoch", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_EPOCH);
    evidence = make_evidence();
    evidence.train_sequence++;
    failed += expect_reject("sequence", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_SEQUENCE);
    evidence = make_evidence();
    evidence.marker_id++;
    failed += expect_reject("marker id", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_MARKER_ID);
    evidence = make_evidence();
    evidence.observed_crc32++;
    failed += expect_reject("crc", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_CRC);
    evidence = make_evidence();
    evidence.flags &= ~CALIBRATION_TRAINING_MARKER_FLAG_HARDWARE_LATCHED;
    failed += expect_reject("flags", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_EVIDENCE_FLAGS);
    evidence = make_evidence();
    evidence.marker_forward_tick = evidence.marker_capture_tick - 1u;
    failed += expect_reject("edge order", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_EDGE_ORDER);
    evidence = make_evidence();
    evidence.dma_overrun_count = 1u;
    failed += expect_reject("dma", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_DMA);
    evidence = make_evidence();
    evidence.pio_stall_count = 1u;
    failed += expect_reject("pio stall", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_PIO_STALL);
    evidence = make_evidence();
    evidence.timeout_count = 1u;
    failed += expect_reject("timeout", request, evidence,
                            CALIBRATION_TRAINING_MARKER_REJECT_TIMEOUT);
    return failed;
}

static int test_rejects_invalid_request(void)
{
    calibration_training_marker_store_t store;
    calibration_training_marker_request_t request = make_request();
    const calibration_training_marker_evidence_t evidence = make_evidence();
    int failed = 0;
    calibration_training_marker_store_init(&store);
    request.successor_slot = request.logical_slot;
    failed += expect_bool("invalid prepare",
                          calibration_training_marker_prepare_core1(
                              &store, &request), false);
    failed += expect_bool("invalid evaluate",
                          calibration_training_marker_evaluate_core1(
                              &store, &request, &evidence), false);
    return failed;
}

static int test_originator_return_order(void)
{
    calibration_training_marker_store_t store;
    calibration_training_marker_snapshot_t snapshot;
    calibration_training_marker_request_t request = make_request();
    calibration_training_marker_evidence_t evidence = make_evidence();
    request.role = CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR;
    request.logical_slot = 0u;
    request.reference_slot = 0u;
    request.predecessor_slot = 3u;
    request.successor_slot = 1u;
    evidence.marker_forward_tick = 100u;
    evidence.marker_capture_tick = 220u;
    evidence.marker_return_tick = 220u;
    calibration_training_marker_store_init(&store);
    int failed = expect_bool(
        "originator accepted",
        calibration_training_marker_evaluate_core1(
            &store, &request, &evidence), true);
    (void)calibration_training_marker_get_snapshot(&store, &snapshot);
    failed += expect_u64("originator loop rtt", snapshot.loop_rtt_ticks,
                         120u);
    failed += expect_u64("originator residence",
                         snapshot.forward_residence_ticks, 0u);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_prepare_and_accept();
    failed += test_reject_matrix();
    failed += test_rejects_invalid_request();
    failed += test_originator_return_order();
    if (failed != 0) {
        (void)printf("calibration_training_marker tests failed: %d\n", failed);
        return 1;
    }
    puts("calibration_training_marker tests passed");
    return 0;
}
