#include "calibration_training_data.h"

#include <stdbool.h>
#include <stdio.h>

#include "calibration_clk_marker.h"

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

static int expect_i32(const char *name, int32_t actual, int32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %ld got %ld\n", name,
                     (long)expected, (long)actual);
        return 1;
    }
    return 0;
}

static calibration_training_data_request_t make_request(void)
{
    const calibration_training_data_request_t request = {
        .board_unique_id = 0xFB276192BEF9CCE1ull,
        .build_id = 0x20260824090429ull,
        .source_node = 0u,
        .destination_node = 1u,
        .train_epoch = 92u,
        .train_sequence = 92u,
        .data_codebook_id = 1u,
        .data_crc32 = 0xA5A55A5Au,
        .calibration_generation = 62u,
        .topology_generation = 13u,
        .topology_crc32 = 0x23456789u,
        .profile_crc32 = 0x3456789Au,
        .schedule_crc32 = 0x456789ABu,
        .sample_period_ns = 4u,
        .marker_to_data_samples = 6420u,
        .link_base_delay_ns = 40u,
        .marker_offset_sample_count = -1,
        .configured_data_offset_sample_count = 0,
        .search_start_offset_sample = -2,
        .search_end_offset_sample = 2,
        .guard_sample_count = 1u,
        .expected_polarity = 0u,
        .max_best_distance = 32u,
        .min_margin = 8u,
    };
    return request;
}

static calibration_training_data_evidence_t make_evidence(void)
{
    const calibration_training_data_evidence_t evidence = {
        .train_epoch = 92u,
        .train_sequence = 92u,
        .observed_crc32 = 0xA5A55A5Au,
        .calibration_generation = 62u,
        .topology_generation = 13u,
        .topology_crc32 = 0x23456789u,
        .profile_crc32 = 0x3456789Au,
        .schedule_crc32 = 0x456789ABu,
        .flags = CALIBRATION_TRAINING_DATA_REQUIRED_FLAGS,
        .polarity = 0u,
        .correlation_reject_reason = 0u,
        .best_lag_sample = 2u,
        .best_distance = 8u,
        .second_lag_sample = 3u,
        .second_distance = 40u,
        .margin = 32u,
        .captured_sample_count = 6460u,
        .expected_sample_count = 6420u,
        .marker_capture_tick = 100u,
        .data_capture_tick = 6560u,
    };
    return evidence;
}

static int expect_reject(
    const char *name,
    calibration_training_data_request_t request,
    calibration_training_data_evidence_t evidence,
    uint32_t reason)
{
    calibration_training_data_store_t store;
    calibration_training_data_snapshot_t snapshot;
    calibration_training_data_store_init(&store);
    int failed = expect_bool(
        name, calibration_training_data_evaluate_core1(
                  &store, &request, &evidence), false);
    failed += expect_bool(
        "rejected snapshot read",
        calibration_training_data_get_snapshot(&store, &snapshot), true);
    failed += expect_u32(name, snapshot.state,
                         CALIBRATION_TRAINING_DATA_REJECTED);
    failed += expect_u32(name, snapshot.reject_reason, reason);
    return failed;
}

static int test_normal_codeword_accepts(void)
{
    calibration_training_data_store_t store;
    calibration_training_data_snapshot_t snapshot;
    const calibration_training_data_request_t request = make_request();
    const calibration_training_data_evidence_t evidence = make_evidence();
    int failed = 0;
    calibration_training_data_store_init(&store);
    failed += expect_bool(
        "prepare", calibration_training_data_prepare_core1(
                       &store, &request), true);
    failed += expect_bool(
        "accept", calibration_training_data_evaluate_core1(
                      &store, &request, &evidence), true);
    failed += expect_bool(
        "snapshot", calibration_training_data_get_snapshot(
                        &store, &snapshot), true);
    failed += expect_u32("state", snapshot.state,
                         CALIBRATION_TRAINING_DATA_ACCEPTED);
    failed += expect_i32("resolved offset", snapshot.resolved_offset_ns, 0);
    failed += expect_i32("window start", snapshot.training_window_start_ns,
                         36);
    failed += expect_i32("window end", snapshot.training_window_end_ns, 44);
    failed += expect_i32("marker offset",
                         snapshot.marker_offset_sample_count, -1);
    failed += expect_i32("configured DATA offset",
                         snapshot.configured_data_offset_sample_count, 0);
    failed += expect_u32("max distance readback", snapshot.max_best_distance,
                         request.max_best_distance);
    failed += expect_u32("minimum margin readback", snapshot.min_margin,
                         request.min_margin);
    failed += expect_i32("marker data skew", snapshot.marker_data_skew_ns, 0);
    failed += expect_u32(
        "diagnostic only",
        snapshot.flags & CALIBRATION_TRAINING_DATA_FLAG_DIAGNOSTIC_ONLY,
        CALIBRATION_TRAINING_DATA_FLAG_DIAGNOSTIC_ONLY);
    return failed;
}

static int test_shifted_codeword_resolves_offset(void)
{
    calibration_training_data_store_t store;
    calibration_training_data_snapshot_t snapshot;
    const calibration_training_data_request_t request = make_request();
    calibration_training_data_evidence_t evidence = make_evidence();
    evidence.best_lag_sample = 3u;
    evidence.second_lag_sample = 4u;
    evidence.data_capture_tick = 6561u;
    calibration_training_data_store_init(&store);
    int failed = expect_bool(
        "shift accepted", calibration_training_data_evaluate_core1(
                              &store, &request, &evidence), true);
    (void)calibration_training_data_get_snapshot(&store, &snapshot);
    failed += expect_i32("shift samples",
                         snapshot.resolved_offset_sample_count, 1);
    failed += expect_i32("shift ns", snapshot.resolved_offset_ns, 4);
    failed += expect_i32("shift skew", snapshot.marker_data_skew_ns, 0);
    return failed;
}

static int test_configured_offset_centers_residual_search(void)
{
    calibration_training_data_store_t store;
    calibration_training_data_snapshot_t snapshot;
    calibration_training_data_request_t request = make_request();
    calibration_training_data_evidence_t evidence = make_evidence();
    request.configured_data_offset_sample_count = 1;
    evidence.data_capture_tick = 6561u;
    calibration_training_data_store_init(&store);
    int failed = expect_bool(
        "configured offset accepted", calibration_training_data_evaluate_core1(
                                          &store, &request, &evidence), true);
    (void)calibration_training_data_get_snapshot(&store, &snapshot);
    failed += expect_i32("configured residual",
                         snapshot.resolved_offset_sample_count, 0);
    failed += expect_i32("configured window start",
                         snapshot.training_window_start_ns, 40);
    failed += expect_i32("configured window end",
                         snapshot.training_window_end_ns, 48);
    failed += expect_i32("configured skew", snapshot.marker_data_skew_ns, 0);
    return failed;
}

static int test_required_reject_matrix(void)
{
    calibration_training_data_request_t request = make_request();
    calibration_training_data_evidence_t evidence = make_evidence();
    int failed = 0;

    evidence.polarity = 1u;
    failed += expect_reject("inverted", request, evidence,
                            CALIBRATION_TRAINING_DATA_REJECT_POLARITY);
    evidence = make_evidence();
    evidence.captured_sample_count = evidence.expected_sample_count - 1u;
    failed += expect_reject(
        "truncated", request, evidence,
        CALIBRATION_TRAINING_DATA_REJECT_CAPTURE_TRUNCATED);
    evidence = make_evidence();
    evidence.second_distance = evidence.best_distance;
    evidence.margin = 0u;
    failed += expect_reject("duplicate", request, evidence,
                            CALIBRATION_TRAINING_DATA_REJECT_MARGIN);
    evidence = make_evidence();
    evidence.train_epoch--;
    failed += expect_reject("old epoch", request, evidence,
                            CALIBRATION_TRAINING_DATA_REJECT_EPOCH);
    evidence = make_evidence();
    evidence.best_lag_sample = 5u;
    failed += expect_reject("outside search", request, evidence,
                            CALIBRATION_TRAINING_DATA_REJECT_SEARCH_RANGE);
    evidence = make_evidence();
    evidence.dma_overrun_count = 1u;
    failed += expect_reject("dma", request, evidence,
                            CALIBRATION_TRAINING_DATA_REJECT_DMA);
    evidence = make_evidence();
    evidence.timeout_count = 1u;
    failed += expect_reject("offset timeout", request, evidence,
                            CALIBRATION_TRAINING_DATA_REJECT_TIMEOUT);
    evidence = make_evidence();
    evidence.observed_crc32 = 0u;
    evidence.flags = 0u;
    evidence.correlation_reject_reason =
        CALIBRATION_CLK_CORRELATION_REJECT_BAD_ARGUMENT;
    evidence.timeout_count = 1u;
    failed += expect_reject(
        "timeout precedes unavailable correlation", request, evidence,
        CALIBRATION_TRAINING_DATA_REJECT_TIMEOUT);
    evidence = make_evidence();
    evidence.dma_overrun_count = 1u;
    evidence.pio_stall_count = 1u;
    failed += expect_reject(
        "pio stall precedes derivative dma overrun", request, evidence,
        CALIBRATION_TRAINING_DATA_REJECT_PIO_STALL);
    return failed;
}

static int test_correlation_root_cause_precedes_crc(void)
{
    const calibration_training_data_request_t request = make_request();
    calibration_training_data_evidence_t evidence = make_evidence();
    int failed = 0;

    evidence.observed_crc32 = 0u;
    evidence.flags &= ~CALIBRATION_TRAINING_DATA_FLAG_CRC_VALID;
    evidence.correlation_reject_reason =
        CALIBRATION_CLK_CORRELATION_REJECT_MARGIN;
    failed += expect_reject(
        "correlation margin precedes unavailable CRC", request, evidence,
        CALIBRATION_TRAINING_DATA_REJECT_MARGIN);

    evidence = make_evidence();
    evidence.observed_crc32 = 0u;
    evidence.flags &= ~CALIBRATION_TRAINING_DATA_FLAG_CRC_VALID;
    evidence.correlation_reject_reason =
        CALIBRATION_CLK_CORRELATION_REJECT_DISTANCE;
    failed += expect_reject(
        "correlation distance precedes unavailable CRC", request, evidence,
        CALIBRATION_TRAINING_DATA_REJECT_DISTANCE);

    evidence = make_evidence();
    evidence.observed_crc32 = 0u;
    evidence.flags &= ~CALIBRATION_TRAINING_DATA_FLAG_CRC_VALID;
    evidence.correlation_reject_reason =
        CALIBRATION_CLK_CORRELATION_REJECT_HEADER_CRC;
    failed += expect_reject(
        "wire header CRC precedes generic correlation", request, evidence,
        CALIBRATION_TRAINING_DATA_REJECT_CRC);

    evidence = make_evidence();
    evidence.train_epoch--;
    evidence.correlation_reject_reason =
        CALIBRATION_CLK_CORRELATION_REJECT_HEADER_MISMATCH;
    failed += expect_reject(
        "observed wire epoch precedes generic correlation", request, evidence,
        CALIBRATION_TRAINING_DATA_REJECT_EPOCH);

    evidence = make_evidence();
    evidence.observed_crc32 ^= 1u;
    failed += expect_reject(
        "CRC mismatch after usable correlation", request, evidence,
        CALIBRATION_TRAINING_DATA_REJECT_CRC);
    return failed;
}

static int test_bad_request_rejected(void)
{
    calibration_training_data_store_t store;
    calibration_training_data_request_t request = make_request();
    calibration_training_data_store_init(&store);
    request.destination_node = request.source_node;
    int failed = expect_bool(
        "same node", calibration_training_data_prepare_core1(
                         &store, &request), false);
    request = make_request();
    request.link_base_delay_ns = 41u;
    failed += expect_bool(
        "41 ns base maps to nearest 4 ns sample",
        calibration_training_data_prepare_core1(&store, &request), true);
    request = make_request();
    request.configured_data_offset_sample_count = 11;
    failed += expect_bool(
        "configured offset range", calibration_training_data_prepare_core1(
                                       &store, &request), false);
    request = make_request();
    request.guard_sample_count = 9u;
    request.search_start_offset_sample = -10;
    failed += expect_bool(
        "negative window", calibration_training_data_prepare_core1(
                               &store, &request), false);
    request = make_request();
    request.diagnostic_fault_flags =
        CALIBRATION_TRAINING_DATA_FAULT_WIRE_EPOCH_OVERRIDE;
    failed += expect_bool(
        "epoch fault requires explicit wire epoch",
        calibration_training_data_prepare_core1(&store, &request), false);
    request.diagnostic_wire_epoch = 91u;
    failed += expect_bool(
        "explicit epoch fault request",
        calibration_training_data_prepare_core1(&store, &request), true);
    request = make_request();
    request.diagnostic_fault_flags =
        1u << 4u;
    failed += expect_bool(
        "MARK idle-high fault is rejected by DATA training",
        calibration_training_data_prepare_core1(&store, &request), false);
    request = make_request();
    request.diagnostic_fault_flags =
        CALIBRATION_TRAINING_DATA_FAULT_WIRE_HEADER_CRC8_XOR;
    request.diagnostic_header_crc8_xor = 0x100u;
    failed += expect_bool(
        "CRC fault mask is uint8",
        calibration_training_data_prepare_core1(&store, &request), false);
    request = make_request();
    request.diagnostic_fault_flags = CALIBRATION_TRAINING_DATA_FAULT_PIO_STALL;
    failed += expect_bool(
        "PIO stall transport fault is accepted",
        calibration_training_data_prepare_core1(&store, &request), true);
    request = make_request();
    request.diagnostic_fault_flags = CALIBRATION_TRAINING_DATA_FAULT_DMA_OVERRUN;
    failed += expect_bool(
        "DMA overrun transport fault is accepted",
        calibration_training_data_prepare_core1(&store, &request), true);
    request = make_request();
    request.diagnostic_fault_flags =
        CALIBRATION_TRAINING_DATA_FAULT_PIO_STALL |
        CALIBRATION_TRAINING_DATA_FAULT_DMA_OVERRUN;
    failed += expect_bool(
        "multiple transport faults are rejected",
        calibration_training_data_prepare_core1(&store, &request), false);
    request = make_request();
    request.diagnostic_fault_flags =
        CALIBRATION_TRAINING_DATA_FAULT_WIRE_HEADER_CRC8_XOR |
        CALIBRATION_TRAINING_DATA_FAULT_DMA_OVERRUN;
    request.diagnostic_header_crc8_xor = 1u;
    failed += expect_bool(
        "wire and transport faults are rejected",
        calibration_training_data_prepare_core1(&store, &request), false);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_normal_codeword_accepts();
    failed += test_shifted_codeword_resolves_offset();
    failed += test_configured_offset_centers_residual_search();
    failed += test_required_reject_matrix();
    failed += test_correlation_root_cause_precedes_crc();
    failed += test_bad_request_rejected();
    if (failed != 0) {
        (void)printf("calibration_training_data tests failed: %d\n", failed);
        return 1;
    }
    puts("calibration_training_data tests passed");
    return 0;
}
