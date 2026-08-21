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
        .master_slot = 3u,
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

int main(void)
{
    int failed = 0;
    failed += test_golden_vector();
    failed += test_bounded_correlation_accepts_exact_lag();
    failed += test_rejects_inverted_and_stale_marker();
    failed += test_rejects_missing_and_repeated_samples();
    failed += test_capture_and_search_bounds();
    failed += test_coded_snapshot_store();
    if (failed != 0) return 1;
    puts("calibration_clk_marker tests passed");
    return 0;
}
