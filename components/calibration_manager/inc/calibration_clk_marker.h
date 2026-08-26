#ifndef CALIBRATION_CLK_MARKER_H
#define CALIBRATION_CLK_MARKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* P2 candidate marker. These values are deliberately named C symbols so the
 * firmware, host evaluator and documentation can share one candidate source.
 * They are not an active wire contract until HIL acceptance and registration. */
#define CALIBRATION_CLK_MARKER_CANDIDATE_VERSION 0u
#define CALIBRATION_CLK_MARKER_BARKER_BITS 13u
#define CALIBRATION_CLK_MARKER_HEADER_BITS 16u
#define CALIBRATION_CLK_MARKER_CRC_BITS 8u
#define CALIBRATION_CLK_MARKER_TIMING_BITS 255u
#define CALIBRATION_CLK_MARKER_LOGICAL_BITS 321u
#define CALIBRATION_CLK_MARKER_LFSR_MASK 0x8Eu
#define CALIBRATION_CLK_MARKER_LFSR_SEED 0x01u
#define CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_20NS 5u
#define CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_24NS 6u
#define CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_32NS 8u
#define CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_40NS 10u
#define CALIBRATION_CLK_MARKER_MAX_HALF_CHIP_SAMPLES \
    CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_40NS
#define CALIBRATION_CLK_MARKER_MAX_RAW_SAMPLES \
    (CALIBRATION_CLK_MARKER_LOGICAL_BITS * 2u * \
     CALIBRATION_CLK_MARKER_MAX_HALF_CHIP_SAMPLES)
#define CALIBRATION_CLK_MARKER_MAX_RAW_WORDS \
    ((CALIBRATION_CLK_MARKER_MAX_RAW_SAMPLES + 31u) / 32u)
/* Bounded search window for the first-stage RTT bracket.  The window is
 * intentionally large enough to cover the four-node coarse bracket at the
 * CLK_SYS sample period, while remaining a fixed, allocation-free loop. */
#define CALIBRATION_CLK_CORRELATION_MAX_LAGS 256u

typedef enum {
    CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20 = 0u,
    CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_40 = 1u,
    CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_24 = 2u,
    CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32 = 3u,
} calibration_clk_codebook_t;

typedef enum {
    CALIBRATION_CLK_POLARITY_NORMAL = 0u,
    CALIBRATION_CLK_POLARITY_INVERTED = 1u,
} calibration_clk_polarity_t;

typedef struct {
    uint8_t version;
    uint8_t codebook_id;
    uint8_t epoch;
    uint8_t source_node;
    uint8_t polarity;
} calibration_clk_marker_config_t;

typedef struct {
    uint16_t header;
    uint16_t header_inverse;
    uint8_t header_crc8;
    uint8_t half_chip_samples;
    uint32_t logical_bits;
    uint32_t raw_samples;
    uint32_t raw_words;
    uint32_t timing_origin_sample;
    uint32_t timing_samples;
} calibration_clk_marker_descriptor_t;

typedef struct {
    uint32_t fields_valid;
    uint16_t header;
    uint16_t header_inverse;
    uint8_t header_crc8;
} calibration_clk_marker_observation_t;

#define CALIBRATION_CLK_MARKER_FAULT_EPOCH_OVERRIDE (1u << 0u)
#define CALIBRATION_CLK_MARKER_FAULT_HEADER_CRC8_XOR (1u << 1u)
#define CALIBRATION_CLK_MARKER_FAULT_ALL \
    (CALIBRATION_CLK_MARKER_FAULT_EPOCH_OVERRIDE | \
     CALIBRATION_CLK_MARKER_FAULT_HEADER_CRC8_XOR)

typedef struct {
    uint32_t flags;
    uint8_t epoch_override;
    uint8_t header_crc8_xor;
} calibration_clk_marker_fault_config_t;

#define CALIBRATION_CLK_MARKER_FLAG_SOF_VALID (1u << 0u)
#define CALIBRATION_CLK_MARKER_FLAG_MANCHESTER_VALID (1u << 1u)
#define CALIBRATION_CLK_MARKER_FLAG_HEADER_INVERSE_VALID (1u << 2u)
#define CALIBRATION_CLK_MARKER_FLAG_HEADER_CRC_VALID (1u << 3u)
#define CALIBRATION_CLK_MARKER_FLAG_HEADER_MATCH (1u << 4u)
#define CALIBRATION_CLK_MARKER_FLAG_EOF_VALID (1u << 5u)
#define CALIBRATION_CLK_MARKER_FLAG_ALL \
    (CALIBRATION_CLK_MARKER_FLAG_SOF_VALID | \
     CALIBRATION_CLK_MARKER_FLAG_MANCHESTER_VALID | \
     CALIBRATION_CLK_MARKER_FLAG_HEADER_INVERSE_VALID | \
     CALIBRATION_CLK_MARKER_FLAG_HEADER_CRC_VALID | \
     CALIBRATION_CLK_MARKER_FLAG_HEADER_MATCH | \
     CALIBRATION_CLK_MARKER_FLAG_EOF_VALID)

typedef enum {
    CALIBRATION_CLK_CORRELATION_REJECT_NONE = 0u,
    CALIBRATION_CLK_CORRELATION_REJECT_BAD_ARGUMENT = 1u,
    CALIBRATION_CLK_CORRELATION_REJECT_SEARCH_RANGE = 2u,
    CALIBRATION_CLK_CORRELATION_REJECT_CAPTURE_TRUNCATED = 3u,
    CALIBRATION_CLK_CORRELATION_REJECT_POLARITY = 4u,
    CALIBRATION_CLK_CORRELATION_REJECT_SOF = 5u,
    CALIBRATION_CLK_CORRELATION_REJECT_MANCHESTER = 6u,
    CALIBRATION_CLK_CORRELATION_REJECT_HEADER_INVERSE = 7u,
    CALIBRATION_CLK_CORRELATION_REJECT_HEADER_CRC = 8u,
    CALIBRATION_CLK_CORRELATION_REJECT_HEADER_MISMATCH = 9u,
    CALIBRATION_CLK_CORRELATION_REJECT_EOF = 10u,
    CALIBRATION_CLK_CORRELATION_REJECT_DISTANCE = 11u,
    CALIBRATION_CLK_CORRELATION_REJECT_MARGIN = 12u,
} calibration_clk_correlation_reject_reason_t;

typedef struct {
    uint32_t min_lag_sample;
    uint32_t max_lag_sample;
    uint32_t max_best_distance;
    uint32_t min_margin;
} calibration_clk_correlation_gate_t;

typedef struct {
    uint32_t accepted;
    uint32_t reject_reason;
    uint32_t marker_flags;
    uint32_t candidate_count;
    uint32_t best_lag_sample;
    uint32_t best_distance;
    uint32_t second_lag_sample;
    uint32_t second_distance;
    uint32_t margin;
    uint32_t inverted_best_lag_sample;
    uint32_t inverted_best_distance;
    uint32_t detected_polarity;
    calibration_clk_marker_observation_t observation;
} calibration_clk_correlation_result_t;

bool calibration_clk_marker_config_valid(
    const calibration_clk_marker_config_t *config);
uint16_t calibration_clk_marker_pack_header(
    const calibration_clk_marker_config_t *config);
bool calibration_clk_marker_unpack_header(
    uint16_t header,
    calibration_clk_marker_config_t *config);
uint8_t calibration_clk_marker_crc8(const uint8_t *bytes, size_t size);
bool calibration_clk_marker_build(
    const calibration_clk_marker_config_t *config,
    uint32_t *raw_words,
    size_t raw_word_capacity,
    calibration_clk_marker_descriptor_t *descriptor);
bool calibration_clk_marker_build_diagnostic_fault(
    const calibration_clk_marker_config_t *config,
    const calibration_clk_marker_fault_config_t *fault,
    uint32_t *raw_words,
    size_t raw_word_capacity,
    calibration_clk_marker_descriptor_t *descriptor);
bool calibration_clk_marker_get_raw_sample(const uint32_t *raw_words,
                                           size_t raw_sample_count,
                                           size_t sample_index,
                                           uint32_t *sample);
bool calibration_clk_marker_validate_capture(
    const calibration_clk_marker_config_t *expected,
    const uint32_t *capture_words,
    size_t capture_sample_count,
    size_t marker_start_sample,
    uint32_t *marker_flags);
bool calibration_clk_marker_observe_capture(
    const calibration_clk_marker_config_t *expected,
    const uint32_t *capture_words,
    size_t capture_sample_count,
    size_t marker_start_sample,
    uint32_t *marker_flags,
    calibration_clk_marker_observation_t *observation);
bool calibration_clk_marker_correlate(
    const calibration_clk_marker_config_t *expected,
    const uint32_t *expected_words,
    size_t expected_sample_count,
    const uint32_t *capture_words,
    size_t capture_sample_count,
    const calibration_clk_correlation_gate_t *gate,
    calibration_clk_correlation_result_t *result);

#endif
