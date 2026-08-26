#include "calibration_clk_coded.h"
#include "calibration_clk_marker.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST_CAPTURE_SAMPLES \
    (CALIBRATION_CLK_MARKER_MAX_RAW_SAMPLES + 64u)
#define TEST_CAPTURE_WORDS ((TEST_CAPTURE_SAMPLES + 31u) / 32u)

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) return 0;
    (void)printf("%s: expected %lu got %lu\n", name,
                 (unsigned long)expected, (unsigned long)actual);
    return 1;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) return 0;
    (void)printf("%s: expected %d got %d\n", name,
                 expected ? 1 : 0, actual ? 1 : 0);
    return 1;
}

static calibration_clk_marker_config_t make_config(void)
{
    const calibration_clk_marker_config_t config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20,
        .epoch = 0x5Au,
        .source_node = 3u,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    return config;
}

static uint32_t raw_sample(const uint32_t *words,
                           size_t sample_count,
                           size_t index)
{
    uint32_t sample = 0u;
    (void)calibration_clk_marker_get_raw_sample(
        words, sample_count, index, &sample);
    return sample;
}

static void set_sample(uint32_t *words, size_t index, uint32_t value)
{
    const uint32_t mask = 1u << (index & 31u);
    if (value != 0u) {
        words[index >> 5u] |= mask;
    } else {
        words[index >> 5u] &= ~mask;
    }
}

static void make_capture(const uint32_t *marker,
                         size_t marker_samples,
                         uint32_t *capture,
                         size_t capture_samples,
                         size_t lag,
                         bool inverted)
{
    memset(capture, 0, ((capture_samples + 31u) / 32u) * sizeof(uint32_t));
    for (size_t i = 0u; i < marker_samples; i++) {
        set_sample(capture, lag + i,
                   raw_sample(marker, marker_samples, i) ^
                       (inverted ? 1u : 0u));
    }
}

static uint32_t raw_fnv1a32(const uint32_t *words, size_t sample_count)
{
    uint32_t hash = 0x811C9DC5u;
    for (size_t i = 0u; i < sample_count; i++) {
        hash ^= raw_sample(words, sample_count, i);
        hash *= 0x01000193u;
    }
    return hash;
}

static int test_golden_vector(void)
{
    const calibration_clk_marker_config_t config = make_config();
    uint32_t marker[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    calibration_clk_marker_descriptor_t descriptor;
    int failed = 0;
    failed += expect_bool("build marker",
                          calibration_clk_marker_build(
                              &config, marker,
                              CALIBRATION_CLK_MARKER_MAX_RAW_WORDS,
                              &descriptor),
                          true);
    failed += expect_u32("header", descriptor.header, 0x05A6u);
    failed += expect_u32("header inverse", descriptor.header_inverse, 0xFA59u);
    failed += expect_u32("header crc", descriptor.header_crc8, 0x65u);
    failed += expect_u32("logical bits", descriptor.logical_bits, 321u);
    failed += expect_u32("raw samples", descriptor.raw_samples, 3210u);
    failed += expect_u32("raw words", descriptor.raw_words, 101u);
    failed += expect_u32("timing origin", descriptor.timing_origin_sample, 530u);
    failed += expect_u32("timing samples", descriptor.timing_samples, 2550u);
    failed += expect_u32("raw FNV", raw_fnv1a32(marker,
                                                 descriptor.raw_samples),
                         0xD89E1248u);
    return failed;
}

static int test_intermediate_codebooks(void)
{
    calibration_clk_marker_config_t config = make_config();
    uint32_t marker[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    calibration_clk_marker_descriptor_t descriptor;
    int failed = 0;

    config.codebook_id = CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_24;
    failed += expect_bool("build 24 ns marker",
                          calibration_clk_marker_build(
                              &config, marker,
                              CALIBRATION_CLK_MARKER_MAX_RAW_WORDS,
                              &descriptor), true);
    failed += expect_u32("24 ns half samples", descriptor.half_chip_samples,
                         CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_24NS);
    failed += expect_u32("24 ns raw samples", descriptor.raw_samples,
                         CALIBRATION_CLK_MARKER_LOGICAL_BITS * 12u);

    config.codebook_id = CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32;
    failed += expect_bool("build 32 ns marker",
                          calibration_clk_marker_build(
                              &config, marker,
                              CALIBRATION_CLK_MARKER_MAX_RAW_WORDS,
                              &descriptor), true);
    failed += expect_u32("32 ns half samples", descriptor.half_chip_samples,
                         CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_32NS);
    failed += expect_u32("32 ns raw samples", descriptor.raw_samples,
                         CALIBRATION_CLK_MARKER_LOGICAL_BITS * 16u);
    return failed;
}

static int test_bounded_correlation_accepts_exact_lag(void)
{
    const calibration_clk_marker_config_t config = make_config();
    uint32_t marker[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    uint32_t capture[TEST_CAPTURE_WORDS];
    calibration_clk_marker_descriptor_t descriptor;
    calibration_clk_correlation_result_t result;
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = 0u,
        .max_lag_sample = 13u,
        .max_best_distance = 0u,
        .min_margin = 1u,
    };
    int failed = 0;
    (void)calibration_clk_marker_build(
        &config, marker, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor);
    make_capture(marker, descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, false);
    failed += expect_bool("correlate exact",
                          calibration_clk_marker_correlate(
                              &config, marker, descriptor.raw_samples,
                              capture, TEST_CAPTURE_SAMPLES, &gate, &result),
                          true);
    failed += expect_u32("accepted", result.accepted, 1u);
    failed += expect_u32("best lag", result.best_lag_sample, 7u);
    failed += expect_u32("best distance", result.best_distance, 0u);
    failed += expect_bool("positive margin", result.margin > 0u, true);
    failed += expect_u32("marker flags", result.marker_flags,
                         CALIBRATION_CLK_MARKER_FLAG_ALL);
    failed += expect_u32("observed fields valid",
                         result.observation.fields_valid, 1u);
    failed += expect_u32("observed header", result.observation.header,
                         descriptor.header);
    failed += expect_u32("observed header inverse",
                         result.observation.header_inverse,
                         descriptor.header_inverse);
    failed += expect_u32("observed header crc",
                         result.observation.header_crc8,
                         descriptor.header_crc8);
    return failed;
}

static int test_rejects_inverted_and_stale_marker(void)
{
    calibration_clk_marker_config_t config = make_config();
    uint32_t marker[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    uint32_t stale[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    uint32_t capture[TEST_CAPTURE_WORDS];
    calibration_clk_marker_descriptor_t descriptor;
    calibration_clk_marker_descriptor_t stale_descriptor;
    calibration_clk_correlation_result_t result;
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = 5u,
        .max_lag_sample = 9u,
        .max_best_distance = 0u,
        .min_margin = 1u,
    };
    int failed = 0;
    (void)calibration_clk_marker_build(
        &config, marker, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor);
    make_capture(marker, descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, true);
    failed += expect_bool("correlate inverted",
                          calibration_clk_marker_correlate(
                              &config, marker, descriptor.raw_samples,
                              capture, TEST_CAPTURE_SAMPLES, &gate, &result),
                          true);
    failed += expect_u32("inverted accepted", result.accepted, 0u);
    failed += expect_u32("inverted reason", result.reject_reason,
                         CALIBRATION_CLK_CORRELATION_REJECT_POLARITY);

    config.epoch++;
    (void)calibration_clk_marker_build(
        &config, stale, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS,
        &stale_descriptor);
    config.epoch--;
    make_capture(stale, stale_descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, false);
    failed += expect_bool("correlate stale",
                          calibration_clk_marker_correlate(
                              &config, marker, descriptor.raw_samples,
                              capture, TEST_CAPTURE_SAMPLES, &gate, &result),
                          true);
    failed += expect_u32("stale accepted", result.accepted, 0u);
    failed += expect_u32("stale reason", result.reject_reason,
                         CALIBRATION_CLK_CORRELATION_REJECT_HEADER_MISMATCH);
    failed += expect_u32("stale observed header",
                         result.observation.header,
                         stale_descriptor.header);
    calibration_clk_marker_config_t observed;
    failed += expect_bool("unpack stale observed header",
                          calibration_clk_marker_unpack_header(
                              result.observation.header, &observed), true);
    failed += expect_u32("stale observed epoch", observed.epoch,
                         (uint32_t)config.epoch + 1u);
    return failed;
}

static int test_diagnostic_faults_are_observed_on_wire(void)
{
    const calibration_clk_marker_config_t config = make_config();
    uint32_t expected[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    uint32_t faulted[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    uint32_t capture[TEST_CAPTURE_WORDS];
    calibration_clk_marker_descriptor_t expected_descriptor;
    calibration_clk_marker_descriptor_t fault_descriptor;
    calibration_clk_correlation_result_t result;
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = 5u,
        .max_lag_sample = 9u,
        .max_best_distance = UINT32_MAX,
        .min_margin = 0u,
    };
    int failed = 0;
    (void)calibration_clk_marker_build(
        &config, expected, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS,
        &expected_descriptor);

    const calibration_clk_marker_fault_config_t epoch_fault = {
        .flags = CALIBRATION_CLK_MARKER_FAULT_EPOCH_OVERRIDE,
        .epoch_override = (uint8_t)(config.epoch - 1u),
    };
    failed += expect_bool(
        "build stale diagnostic marker",
        calibration_clk_marker_build_diagnostic_fault(
            &config, &epoch_fault, faulted,
            CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &fault_descriptor), true);
    make_capture(faulted, fault_descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, false);
    failed += expect_bool(
        "correlate stale diagnostic marker",
        calibration_clk_marker_correlate(
            &config, expected, expected_descriptor.raw_samples,
            capture, TEST_CAPTURE_SAMPLES, &gate, &result), true);
    failed += expect_u32("stale diagnostic reason", result.reject_reason,
                         CALIBRATION_CLK_CORRELATION_REJECT_HEADER_MISMATCH);
    failed += expect_u32("stale diagnostic observed header",
                         result.observation.header, fault_descriptor.header);

    const calibration_clk_marker_fault_config_t crc_fault = {
        .flags = CALIBRATION_CLK_MARKER_FAULT_HEADER_CRC8_XOR,
        .header_crc8_xor = 1u,
    };
    failed += expect_bool(
        "build CRC diagnostic marker",
        calibration_clk_marker_build_diagnostic_fault(
            &config, &crc_fault, faulted,
            CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &fault_descriptor), true);
    make_capture(faulted, fault_descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, false);
    failed += expect_bool(
        "correlate CRC diagnostic marker",
        calibration_clk_marker_correlate(
            &config, expected, expected_descriptor.raw_samples,
            capture, TEST_CAPTURE_SAMPLES, &gate, &result), true);
    failed += expect_u32("CRC diagnostic reason", result.reject_reason,
                         CALIBRATION_CLK_CORRELATION_REJECT_HEADER_CRC);
    failed += expect_u32("CRC diagnostic observed byte",
                         result.observation.header_crc8,
                         fault_descriptor.header_crc8);
    failed += expect_u32("CRC diagnostic inverse remains valid",
                         result.marker_flags &
                             CALIBRATION_CLK_MARKER_FLAG_HEADER_INVERSE_VALID,
                         CALIBRATION_CLK_MARKER_FLAG_HEADER_INVERSE_VALID);

    const calibration_clk_marker_fault_config_t idle_fault = {
        .flags = CALIBRATION_CLK_MARKER_FAULT_IDLE_HIGH,
    };
    failed += expect_bool(
        "build idle-high diagnostic marker",
        calibration_clk_marker_build_diagnostic_fault(
            &config, &idle_fault, faulted,
            CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &fault_descriptor), true);
    for (size_t sample = 0u; sample < fault_descriptor.raw_samples;
         sample++) {
        failed += expect_u32(
            "idle-high diagnostic sample",
            raw_sample(faulted, fault_descriptor.raw_samples, sample), 1u);
    }
    return failed;
}

static int test_rejects_missing_and_repeated_samples(void)
{
    const calibration_clk_marker_config_t config = make_config();
    uint32_t marker[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    uint32_t capture[TEST_CAPTURE_WORDS];
    calibration_clk_marker_descriptor_t descriptor;
    calibration_clk_correlation_result_t result;
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = 5u,
        .max_lag_sample = 9u,
        .max_best_distance = 0u,
        .min_margin = 1u,
    };
    int failed = 0;
    (void)calibration_clk_marker_build(
        &config, marker, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor);
    make_capture(marker, descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, false);

    const size_t fault = 7u + descriptor.timing_origin_sample + 100u;
    for (size_t i = fault; i + 1u < TEST_CAPTURE_SAMPLES; i++) {
        set_sample(capture, i, raw_sample(capture, TEST_CAPTURE_SAMPLES, i + 1u));
    }
    failed += expect_bool("correlate missing sample",
                          calibration_clk_marker_correlate(
                              &config, marker, descriptor.raw_samples,
                              capture, TEST_CAPTURE_SAMPLES, &gate, &result),
                          true);
    failed += expect_u32("missing accepted", result.accepted, 0u);
    failed += expect_bool("missing has reason",
                          result.reject_reason !=
                              CALIBRATION_CLK_CORRELATION_REJECT_NONE,
                          true);

    make_capture(marker, descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, false);
    for (size_t i = TEST_CAPTURE_SAMPLES - 1u; i > fault; i--) {
        set_sample(capture, i, raw_sample(capture, TEST_CAPTURE_SAMPLES, i - 1u));
    }
    failed += expect_bool("correlate repeated sample",
                          calibration_clk_marker_correlate(
                              &config, marker, descriptor.raw_samples,
                              capture, TEST_CAPTURE_SAMPLES, &gate, &result),
                          true);
    failed += expect_u32("repeat accepted", result.accepted, 0u);
    failed += expect_bool("repeat has reason",
                          result.reject_reason !=
                              CALIBRATION_CLK_CORRELATION_REJECT_NONE,
                          true);
    return failed;
}

static int test_capture_and_search_bounds(void)
{
    const calibration_clk_marker_config_t config = make_config();
    uint32_t marker[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    calibration_clk_marker_descriptor_t descriptor;
    calibration_clk_correlation_result_t result;
    calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = 0u,
        .max_lag_sample = 1u,
        .max_best_distance = 0u,
        .min_margin = 1u,
    };
    int failed = 0;
    (void)calibration_clk_marker_build(
        &config, marker, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor);
    failed += expect_bool("truncated calculation",
                          calibration_clk_marker_correlate(
                              &config, marker, descriptor.raw_samples,
                              marker, descriptor.raw_samples, &gate, &result),
                          true);
    failed += expect_u32("truncated reason", result.reject_reason,
                         CALIBRATION_CLK_CORRELATION_REJECT_CAPTURE_TRUNCATED);

    gate.max_lag_sample = CALIBRATION_CLK_CORRELATION_MAX_LAGS;
    failed += expect_bool("range calculation",
                          calibration_clk_marker_correlate(
                              &config, marker, descriptor.raw_samples,
                              marker, descriptor.raw_samples, &gate, &result),
                          true);
    failed += expect_u32("range reason", result.reject_reason,
                         CALIBRATION_CLK_CORRELATION_REJECT_SEARCH_RANGE);
    return failed;
}

static int test_coded_snapshot_store(void)
{
    calibration_clk_coded_store_t store;
    calibration_clk_coded_snapshot_t published;
    calibration_clk_coded_snapshot_t observed;
    int failed = 0;
    calibration_clk_coded_store_init(&store);
    failed += expect_bool("get idle snapshot",
                          calibration_clk_coded_get_snapshot(&store, &observed),
                          true);
    failed += expect_u32("idle version", observed.version,
                         CALIBRATION_CLK_CODED_SNAPSHOT_VERSION);
    failed += expect_u32("idle state", observed.state,
                         CALIBRATION_CLK_CODED_IDLE);

    memset(&published, 0, sizeof(published));
    published.version = CALIBRATION_CLK_CODED_SNAPSHOT_VERSION;
    published.state = CALIBRATION_CLK_CODED_ACCEPTED;
    published.train_epoch = 9u;
    published.best_lag_sample = 7u;
    published.flags = CALIBRATION_CLK_CODED_FLAG_DIAGNOSTIC_ONLY |
                      CALIBRATION_CLK_CODED_FLAG_CORRELATION_VALID;
    failed += expect_bool("publish snapshot",
                          calibration_clk_coded_publish_core1(
                              &store, &published), true);
    failed += expect_bool("get published snapshot",
                          calibration_clk_coded_get_snapshot(&store, &observed),
                          true);
    failed += expect_u32("published epoch", observed.train_epoch, 9u);
    failed += expect_u32("published lag", observed.best_lag_sample, 7u);
    return failed;
}

static calibration_clk_coded_request_t make_coded_request(void)
{
    const calibration_clk_coded_request_t request = {
        .board_unique_id = 0x0010071E65B5CB38ull,
        .build_id = 0x20260821021250ull,
        .local_node = 3u,
        .train_epoch = 0x5Au,
        .train_sequence = 11u,
        .calibration_generation = 4u,
        .topology_generation = 5u,
        .topology_crc32 = 0x12345678u,
        .profile_crc32 = 0x23456789u,
        .schedule_crc32 = 0x3456789Au,
        .baud_hz = 10000000u,
        .codebook_id = CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20,
        .sample_period_ns = 4u,
        .coarse_min_sample = 5u,
        .coarse_max_sample = 9u,
    };
    return request;
}

static calibration_clk_coded_evidence_t make_coded_evidence(
    const calibration_clk_coded_request_t *request,
    const uint32_t *capture,
    uint32_t capture_samples)
{
    const calibration_clk_coded_evidence_t evidence = {
        .capture_words = capture,
        .capture_sample_count = capture_samples,
        .capture_origin_tick = 1000u,
        .timing_field_tx_origin_sample = 530u,
        .tx_dma_count = 101u,
        .rx_dma_count = 103u,
        .train_epoch = request->train_epoch,
        .train_sequence = request->train_sequence,
        .topology_generation = request->topology_generation,
        .topology_crc32 = request->topology_crc32,
        .profile_crc32 = request->profile_crc32,
        .schedule_crc32 = request->schedule_crc32,
        .flags = CALIBRATION_CLK_CODED_FLAG_TX_DMA_COMPLETE |
                 CALIBRATION_CLK_CODED_FLAG_RX_DMA_COMPLETE,
    };
    return evidence;
}

static int test_coded_state_machine_accepts_diagnostic_result(void)
{
    calibration_clk_coded_store_t store;
    calibration_clk_coded_workspace_t workspace;
    calibration_clk_coded_snapshot_t snapshot;
    const calibration_clk_coded_request_t request = make_coded_request();
    calibration_clk_marker_descriptor_t descriptor;
    uint32_t marker[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    uint32_t capture[TEST_CAPTURE_WORDS];
    const calibration_clk_marker_config_t config = make_config();
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = 5u,
        .max_lag_sample = 9u,
        .max_best_distance = 0u,
        .min_margin = 1u,
    };
    int failed = 0;
    calibration_clk_coded_store_init(&store);
    (void)calibration_clk_marker_build(
        &config, marker, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor);
    make_capture(marker, descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, false);
    calibration_clk_coded_evidence_t evidence = make_coded_evidence(
        &request, capture, TEST_CAPTURE_SAMPLES);
    failed += expect_bool("begin coded coarse",
                          calibration_clk_coded_begin_coarse_core1(
                              &store, &request), true);
    failed += expect_bool("process coded evidence",
                          calibration_clk_coded_process_core1(
                              &store, &workspace, &evidence, &gate), true);
    failed += expect_bool("get coded accepted",
                          calibration_clk_coded_get_snapshot(&store, &snapshot),
                          true);
    failed += expect_u32("coded accepted state", snapshot.state,
                         CALIBRATION_CLK_CODED_ACCEPTED);
    failed += expect_u32("coded accepted lag", snapshot.best_lag_sample, 7u);
    failed += expect_u32("coded diagnostic retained",
                         snapshot.flags &
                             CALIBRATION_CLK_CODED_FLAG_DIAGNOSTIC_ONLY,
                         CALIBRATION_CLK_CODED_FLAG_DIAGNOSTIC_ONLY);
    return failed;
}

static int test_coded_state_machine_rejects_stale_and_dma(void)
{
    calibration_clk_coded_store_t store;
    calibration_clk_coded_workspace_t workspace;
    calibration_clk_coded_snapshot_t snapshot;
    const calibration_clk_coded_request_t request = make_coded_request();
    calibration_clk_marker_descriptor_t descriptor;
    uint32_t marker[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    uint32_t capture[TEST_CAPTURE_WORDS];
    const calibration_clk_marker_config_t config = make_config();
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = 5u,
        .max_lag_sample = 9u,
        .max_best_distance = 0u,
        .min_margin = 1u,
    };
    int failed = 0;
    (void)calibration_clk_marker_build(
        &config, marker, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor);
    make_capture(marker, descriptor.raw_samples, capture,
                 TEST_CAPTURE_SAMPLES, 7u, false);

    calibration_clk_coded_store_init(&store);
    (void)calibration_clk_coded_begin_coarse_core1(&store, &request);
    calibration_clk_coded_evidence_t evidence = make_coded_evidence(
        &request, capture, TEST_CAPTURE_SAMPLES);
    evidence.topology_generation++;
    failed += expect_bool("process stale evidence",
                          calibration_clk_coded_process_core1(
                              &store, &workspace, &evidence, &gate), true);
    (void)calibration_clk_coded_get_snapshot(&store, &snapshot);
    failed += expect_u32("stale state", snapshot.state,
                         CALIBRATION_CLK_CODED_REJECTED);
    failed += expect_u32("stale reason", snapshot.reject_reason,
                         CALIBRATION_CLK_CODED_REJECT_GENERATION);

    calibration_clk_coded_store_init(&store);
    (void)calibration_clk_coded_begin_coarse_core1(&store, &request);
    evidence = make_coded_evidence(&request, capture, TEST_CAPTURE_SAMPLES);
    evidence.flags &= ~CALIBRATION_CLK_CODED_FLAG_RX_DMA_COMPLETE;
    failed += expect_bool("process incomplete dma",
                          calibration_clk_coded_process_core1(
                              &store, &workspace, &evidence, &gate), true);
    (void)calibration_clk_coded_get_snapshot(&store, &snapshot);
    failed += expect_u32("dma state", snapshot.state,
                         CALIBRATION_CLK_CODED_REJECTED);
    failed += expect_u32("dma reason", snapshot.reject_reason,
                         CALIBRATION_CLK_CODED_REJECT_DMA);
    return failed;
}

static int test_coded_request_explicit_reject(void)
{
    calibration_clk_coded_store_t store;
    calibration_clk_coded_snapshot_t snapshot;
    const calibration_clk_coded_request_t request = make_coded_request();
    int failed = 0;
    calibration_clk_coded_store_init(&store);
    failed += expect_bool("explicit coded reject",
                          calibration_clk_coded_reject_request_core1(
                              &store, &request,
                              CALIBRATION_CLK_CODED_REJECT_BAD_STATE),
                          true);
    failed += expect_bool("get explicit coded reject",
                          calibration_clk_coded_get_snapshot(&store, &snapshot),
                          true);
    failed += expect_u32("explicit reject state", snapshot.state,
                         CALIBRATION_CLK_CODED_REJECTED);
    failed += expect_u32("explicit reject reason", snapshot.reject_reason,
                         CALIBRATION_CLK_CODED_REJECT_BAD_STATE);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_golden_vector();
    failed += test_intermediate_codebooks();
    failed += test_bounded_correlation_accepts_exact_lag();
    failed += test_rejects_inverted_and_stale_marker();
    failed += test_diagnostic_faults_are_observed_on_wire();
    failed += test_rejects_missing_and_repeated_samples();
    failed += test_capture_and_search_bounds();
    failed += test_coded_snapshot_store();
    failed += test_coded_state_machine_accepts_diagnostic_result();
    failed += test_coded_state_machine_rejects_stale_and_dma();
    failed += test_coded_request_explicit_reject();
    if (failed != 0) return 1;
    puts("calibration_clk_marker tests passed");
    return 0;
}
