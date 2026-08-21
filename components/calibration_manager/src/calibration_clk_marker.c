#include "calibration_clk_marker.h"

#include <limits.h>
#include <string.h>

#define CALIBRATION_CLK_MARKER_LOGICAL_BYTES \
    ((CALIBRATION_CLK_MARKER_LOGICAL_BITS + 7u) / 8u)
#define CALIBRATION_CLK_MARKER_SOF_OFFSET 0u
#define CALIBRATION_CLK_MARKER_HEADER_OFFSET \
    (CALIBRATION_CLK_MARKER_SOF_OFFSET + CALIBRATION_CLK_MARKER_BARKER_BITS)
#define CALIBRATION_CLK_MARKER_HEADER_INV_OFFSET \
    (CALIBRATION_CLK_MARKER_HEADER_OFFSET + CALIBRATION_CLK_MARKER_HEADER_BITS)
#define CALIBRATION_CLK_MARKER_CRC_OFFSET \
    (CALIBRATION_CLK_MARKER_HEADER_INV_OFFSET + \
     CALIBRATION_CLK_MARKER_HEADER_BITS)
#define CALIBRATION_CLK_MARKER_TIMING_OFFSET \
    (CALIBRATION_CLK_MARKER_CRC_OFFSET + CALIBRATION_CLK_MARKER_CRC_BITS)
#define CALIBRATION_CLK_MARKER_EOF_OFFSET \
    (CALIBRATION_CLK_MARKER_TIMING_OFFSET + \
     CALIBRATION_CLK_MARKER_TIMING_BITS)

static const uint8_t s_calibration_clk_barker13[
    CALIBRATION_CLK_MARKER_BARKER_BITS] = {
    1u, 1u, 1u, 1u, 1u, 0u, 0u, 1u, 1u, 0u, 1u, 0u, 1u,
};

static uint32_t calibration_clk_marker_half_chip_samples(uint8_t codebook_id)
{
    switch (codebook_id) {
    case CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20:
        return CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_20NS;
    case CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_40:
        return CALIBRATION_CLK_MARKER_HALF_CHIP_SAMPLES_40NS;
    default:
        return 0u;
    }
}

bool calibration_clk_marker_config_valid(
    const calibration_clk_marker_config_t *config)
{
    return config != NULL &&
           config->version <= 3u &&
           calibration_clk_marker_half_chip_samples(config->codebook_id) != 0u &&
           config->master_slot <= 7u &&
           config->polarity <= CALIBRATION_CLK_POLARITY_INVERTED;
}

uint16_t calibration_clk_marker_pack_header(
    const calibration_clk_marker_config_t *config)
{
    if (!calibration_clk_marker_config_valid(config)) {
        return 0u;
    }
    return (uint16_t)(((uint16_t)(config->version & 0x03u) << 14u) |
                      ((uint16_t)(config->codebook_id & 0x03u) << 12u) |
                      ((uint16_t)config->epoch << 4u) |
                      ((uint16_t)(config->master_slot & 0x07u) << 1u) |
                      (uint16_t)(config->polarity & 0x01u));
}

uint8_t calibration_clk_marker_crc8(const uint8_t *bytes, size_t size)
{
    uint8_t crc = 0u;
    if (bytes == NULL && size != 0u) {
        return 0u;
    }
    for (size_t i = 0u; i < size; i++) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            crc = (crc & 0x80u) != 0u
                      ? (uint8_t)((uint8_t)(crc << 1u) ^ 0x07u)
                      : (uint8_t)(crc << 1u);
        }
    }
    return crc;
}

static void calibration_clk_marker_set_logical_bit(uint8_t *bits,
                                                    size_t index,
                                                    uint32_t value)
{
    const uint8_t mask = (uint8_t)(1u << (index & 7u));
    if (value != 0u) {
        bits[index >> 3u] |= mask;
    } else {
        bits[index >> 3u] &= (uint8_t)~mask;
    }
}

static uint32_t calibration_clk_marker_get_logical_bit(const uint8_t *bits,
                                                       size_t index)
{
    return (uint32_t)((bits[index >> 3u] >> (index & 7u)) & 1u);
}

static void calibration_clk_marker_append_msb(uint8_t *bits,
                                              size_t *offset,
                                              uint32_t value,
                                              uint32_t width)
{
    for (uint32_t bit = width; bit > 0u; bit--) {
        calibration_clk_marker_set_logical_bit(
            bits, (*offset)++, (value >> (bit - 1u)) & 1u);
    }
}

static bool calibration_clk_marker_build_logical(
    const calibration_clk_marker_config_t *config,
    uint8_t *logical_bits,
    calibration_clk_marker_descriptor_t *descriptor)
{
    if (!calibration_clk_marker_config_valid(config) ||
        logical_bits == NULL || descriptor == NULL) {
        return false;
    }

    memset(logical_bits, 0, CALIBRATION_CLK_MARKER_LOGICAL_BYTES);
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->header = calibration_clk_marker_pack_header(config);
    descriptor->header_inverse = (uint16_t)~descriptor->header;
    const uint8_t header_bytes[] = {
        (uint8_t)(descriptor->header >> 8u),
        (uint8_t)descriptor->header,
        (uint8_t)(descriptor->header_inverse >> 8u),
        (uint8_t)descriptor->header_inverse,
    };
    descriptor->header_crc8 = calibration_clk_marker_crc8(
        header_bytes, sizeof(header_bytes));
    descriptor->half_chip_samples = (uint8_t)
        calibration_clk_marker_half_chip_samples(config->codebook_id);
    descriptor->logical_bits = CALIBRATION_CLK_MARKER_LOGICAL_BITS;
    descriptor->raw_samples = descriptor->logical_bits * 2u *
                              descriptor->half_chip_samples;
    descriptor->raw_words = (descriptor->raw_samples + 31u) / 32u;
    descriptor->timing_origin_sample = CALIBRATION_CLK_MARKER_TIMING_OFFSET *
                                       2u * descriptor->half_chip_samples;
    descriptor->timing_samples = CALIBRATION_CLK_MARKER_TIMING_BITS * 2u *
                                 descriptor->half_chip_samples;

    size_t offset = 0u;
    for (size_t i = 0u; i < CALIBRATION_CLK_MARKER_BARKER_BITS; i++) {
        calibration_clk_marker_set_logical_bit(
            logical_bits, offset++, s_calibration_clk_barker13[i]);
    }
    calibration_clk_marker_append_msb(logical_bits, &offset,
                                      descriptor->header, 16u);
    calibration_clk_marker_append_msb(logical_bits, &offset,
                                      descriptor->header_inverse, 16u);
    calibration_clk_marker_append_msb(logical_bits, &offset,
                                      descriptor->header_crc8, 8u);

    uint8_t state = CALIBRATION_CLK_MARKER_LFSR_SEED;
    for (size_t i = 0u; i < CALIBRATION_CLK_MARKER_TIMING_BITS; i++) {
        const uint8_t lsb = state & 1u;
        calibration_clk_marker_set_logical_bit(logical_bits, offset++, lsb);
        state >>= 1u;
        if (lsb != 0u) {
            state ^= CALIBRATION_CLK_MARKER_LFSR_MASK;
        }
    }
    if (state != CALIBRATION_CLK_MARKER_LFSR_SEED) {
        return false;
    }
    for (size_t i = 0u; i < CALIBRATION_CLK_MARKER_BARKER_BITS; i++) {
        calibration_clk_marker_set_logical_bit(
            logical_bits, offset++, 1u - s_calibration_clk_barker13[i]);
    }
    return offset == CALIBRATION_CLK_MARKER_LOGICAL_BITS;
}

bool calibration_clk_marker_build(
    const calibration_clk_marker_config_t *config,
    uint32_t *raw_words,
    size_t raw_word_capacity,
    calibration_clk_marker_descriptor_t *descriptor)
{
    uint8_t logical_bits[CALIBRATION_CLK_MARKER_LOGICAL_BYTES];
    calibration_clk_marker_descriptor_t next;
    if (raw_words == NULL || descriptor == NULL ||
        !calibration_clk_marker_build_logical(config, logical_bits, &next) ||
        next.raw_words > raw_word_capacity) {
        return false;
    }
    memset(raw_words, 0, next.raw_words * sizeof(raw_words[0]));
    size_t raw_index = 0u;
    for (size_t logical = 0u;
         logical < CALIBRATION_CLK_MARKER_LOGICAL_BITS; logical++) {
        const uint32_t first = calibration_clk_marker_get_logical_bit(
            logical_bits, logical);
        for (uint32_t i = 0u; i < next.half_chip_samples; i++, raw_index++) {
            raw_words[raw_index >> 5u] |= first << (raw_index & 31u);
        }
        for (uint32_t i = 0u; i < next.half_chip_samples; i++, raw_index++) {
            raw_words[raw_index >> 5u] |= (1u - first) << (raw_index & 31u);
        }
    }
    *descriptor = next;
    return raw_index == next.raw_samples;
}

bool calibration_clk_marker_get_raw_sample(const uint32_t *raw_words,
                                           size_t raw_sample_count,
                                           size_t sample_index,
                                           uint32_t *sample)
{
    if (raw_words == NULL || sample == NULL ||
        sample_index >= raw_sample_count) {
        return false;
    }
    *sample = (raw_words[sample_index >> 5u] >> (sample_index & 31u)) & 1u;
    return true;
}

static bool calibration_clk_marker_decode_bit(const uint32_t *capture_words,
                                              size_t capture_sample_count,
                                              size_t marker_start_sample,
                                              size_t logical_index,
                                              uint32_t half_chip_samples,
                                              uint32_t *value)
{
    uint32_t first_ones = 0u;
    uint32_t second_ones = 0u;
    const size_t raw_start = marker_start_sample +
        logical_index * 2u * half_chip_samples;
    for (uint32_t i = 0u; i < half_chip_samples; i++) {
        uint32_t sample = 0u;
        if (!calibration_clk_marker_get_raw_sample(
                capture_words, capture_sample_count, raw_start + i, &sample)) {
            return false;
        }
        first_ones += sample;
        if (!calibration_clk_marker_get_raw_sample(
                capture_words, capture_sample_count,
                raw_start + half_chip_samples + i, &sample)) {
            return false;
        }
        second_ones += sample;
    }
    if (first_ones * 2u == half_chip_samples ||
        second_ones * 2u == half_chip_samples ||
        (first_ones > half_chip_samples / 2u) ==
            (second_ones > half_chip_samples / 2u)) {
        return false;
    }
    *value = first_ones > half_chip_samples / 2u ? 1u : 0u;
    return true;
}

static bool calibration_clk_marker_decode_msb_field(
    const uint32_t *capture_words,
    size_t capture_sample_count,
    size_t marker_start_sample,
    size_t logical_offset,
    uint32_t width,
    uint32_t half_chip_samples,
    uint32_t *value)
{
    uint32_t decoded = 0u;
    for (uint32_t bit = 0u; bit < width; bit++) {
        uint32_t sample = 0u;
        if (!calibration_clk_marker_decode_bit(
                capture_words, capture_sample_count, marker_start_sample,
                logical_offset + bit, half_chip_samples, &sample)) {
            return false;
        }
        decoded = (decoded << 1u) | sample;
    }
    *value = decoded;
    return true;
}

bool calibration_clk_marker_validate_capture(
    const calibration_clk_marker_config_t *expected,
    const uint32_t *capture_words,
    size_t capture_sample_count,
    size_t marker_start_sample,
    uint32_t *marker_flags)
{
    uint8_t ignored[CALIBRATION_CLK_MARKER_LOGICAL_BYTES];
    calibration_clk_marker_descriptor_t descriptor;
    if (marker_flags == NULL || capture_words == NULL ||
        !calibration_clk_marker_build_logical(expected, ignored, &descriptor) ||
        marker_start_sample > capture_sample_count ||
        descriptor.raw_samples > capture_sample_count - marker_start_sample) {
        return false;
    }

    uint32_t flags = CALIBRATION_CLK_MARKER_FLAG_MANCHESTER_VALID;
    bool sof_valid = true;
    bool eof_valid = true;
    for (size_t i = 0u; i < CALIBRATION_CLK_MARKER_BARKER_BITS; i++) {
        uint32_t sof = 0u;
        uint32_t eof = 0u;
        if (!calibration_clk_marker_decode_bit(
                capture_words, capture_sample_count, marker_start_sample,
                CALIBRATION_CLK_MARKER_SOF_OFFSET + i,
                descriptor.half_chip_samples, &sof) ||
            !calibration_clk_marker_decode_bit(
                capture_words, capture_sample_count, marker_start_sample,
                CALIBRATION_CLK_MARKER_EOF_OFFSET + i,
                descriptor.half_chip_samples, &eof)) {
            flags &= ~CALIBRATION_CLK_MARKER_FLAG_MANCHESTER_VALID;
            sof_valid = false;
            eof_valid = false;
            break;
        }
        sof_valid = sof_valid && sof == s_calibration_clk_barker13[i];
        eof_valid = eof_valid && eof == 1u - s_calibration_clk_barker13[i];
    }
    if (sof_valid) flags |= CALIBRATION_CLK_MARKER_FLAG_SOF_VALID;
    if (eof_valid) flags |= CALIBRATION_CLK_MARKER_FLAG_EOF_VALID;

    uint32_t header = 0u;
    uint32_t header_inverse = 0u;
    uint32_t crc = 0u;
    const bool fields_valid = calibration_clk_marker_decode_msb_field(
        capture_words, capture_sample_count, marker_start_sample,
        CALIBRATION_CLK_MARKER_HEADER_OFFSET, 16u,
        descriptor.half_chip_samples, &header) &&
        calibration_clk_marker_decode_msb_field(
            capture_words, capture_sample_count, marker_start_sample,
            CALIBRATION_CLK_MARKER_HEADER_INV_OFFSET, 16u,
            descriptor.half_chip_samples, &header_inverse) &&
        calibration_clk_marker_decode_msb_field(
            capture_words, capture_sample_count, marker_start_sample,
            CALIBRATION_CLK_MARKER_CRC_OFFSET, 8u,
            descriptor.half_chip_samples, &crc);
    if (!fields_valid) {
        flags &= ~CALIBRATION_CLK_MARKER_FLAG_MANCHESTER_VALID;
    } else {
        if ((((uint16_t)header) ^ ((uint16_t)header_inverse)) == 0xFFFFu) {
            flags |= CALIBRATION_CLK_MARKER_FLAG_HEADER_INVERSE_VALID;
        }
        const uint8_t bytes[] = {
            (uint8_t)(header >> 8u), (uint8_t)header,
            (uint8_t)(header_inverse >> 8u), (uint8_t)header_inverse,
        };
        if ((uint8_t)crc == calibration_clk_marker_crc8(bytes, sizeof(bytes))) {
            flags |= CALIBRATION_CLK_MARKER_FLAG_HEADER_CRC_VALID;
        }
        if ((uint16_t)header == descriptor.header) {
            flags |= CALIBRATION_CLK_MARKER_FLAG_HEADER_MATCH;
        }
    }
    *marker_flags = flags;
    return true;
}

static uint32_t calibration_clk_marker_distance(
    const uint32_t *expected_words,
    size_t expected_sample_count,
    const uint32_t *capture_words,
    size_t capture_sample_count,
    size_t lag,
    bool inverted)
{
    uint32_t distance = 0u;
    for (size_t sample_index = 0u;
         sample_index < expected_sample_count; sample_index++) {
        uint32_t expected = 0u;
        uint32_t observed = 0u;
        (void)calibration_clk_marker_get_raw_sample(
            expected_words, expected_sample_count, sample_index, &expected);
        (void)calibration_clk_marker_get_raw_sample(
            capture_words, capture_sample_count, lag + sample_index, &observed);
        if (inverted) observed ^= 1u;
        distance += expected != observed ? 1u : 0u;
    }
    return distance;
}

static uint32_t calibration_clk_marker_reject_from_flags(uint32_t flags)
{
    if ((flags & CALIBRATION_CLK_MARKER_FLAG_MANCHESTER_VALID) == 0u) {
        return CALIBRATION_CLK_CORRELATION_REJECT_MANCHESTER;
    }
    if ((flags & CALIBRATION_CLK_MARKER_FLAG_SOF_VALID) == 0u) {
        return CALIBRATION_CLK_CORRELATION_REJECT_SOF;
    }
    if ((flags & CALIBRATION_CLK_MARKER_FLAG_HEADER_INVERSE_VALID) == 0u) {
        return CALIBRATION_CLK_CORRELATION_REJECT_HEADER_INVERSE;
    }
    if ((flags & CALIBRATION_CLK_MARKER_FLAG_HEADER_CRC_VALID) == 0u) {
        return CALIBRATION_CLK_CORRELATION_REJECT_HEADER_CRC;
    }
    if ((flags & CALIBRATION_CLK_MARKER_FLAG_HEADER_MATCH) == 0u) {
        return CALIBRATION_CLK_CORRELATION_REJECT_HEADER_MISMATCH;
    }
    if ((flags & CALIBRATION_CLK_MARKER_FLAG_EOF_VALID) == 0u) {
        return CALIBRATION_CLK_CORRELATION_REJECT_EOF;
    }
    return CALIBRATION_CLK_CORRELATION_REJECT_NONE;
}

bool calibration_clk_marker_correlate(
    const calibration_clk_marker_config_t *expected,
    const uint32_t *expected_words,
    size_t expected_sample_count,
    const uint32_t *capture_words,
    size_t capture_sample_count,
    const calibration_clk_correlation_gate_t *gate,
    calibration_clk_correlation_result_t *result)
{
    if (result == NULL) return false;
    memset(result, 0, sizeof(*result));
    result->reject_reason = CALIBRATION_CLK_CORRELATION_REJECT_BAD_ARGUMENT;
    result->best_distance = UINT32_MAX;
    result->second_distance = UINT32_MAX;
    result->inverted_best_distance = UINT32_MAX;
    if (!calibration_clk_marker_config_valid(expected) ||
        expected_words == NULL || capture_words == NULL || gate == NULL ||
        expected_sample_count == 0u ||
        gate->min_lag_sample > gate->max_lag_sample) {
        return false;
    }
    const uint64_t candidate_count =
        (uint64_t)gate->max_lag_sample - gate->min_lag_sample + 1ull;
    if (candidate_count < 2ull ||
        candidate_count > CALIBRATION_CLK_CORRELATION_MAX_LAGS) {
        result->reject_reason = CALIBRATION_CLK_CORRELATION_REJECT_SEARCH_RANGE;
        return true;
    }
    if (gate->max_lag_sample > capture_sample_count ||
        expected_sample_count > capture_sample_count - gate->max_lag_sample) {
        result->reject_reason =
            CALIBRATION_CLK_CORRELATION_REJECT_CAPTURE_TRUNCATED;
        return true;
    }
    result->candidate_count = (uint32_t)candidate_count;

    for (uint32_t lag = gate->min_lag_sample;
         lag <= gate->max_lag_sample; lag++) {
        const uint32_t normal = calibration_clk_marker_distance(
            expected_words, expected_sample_count,
            capture_words, capture_sample_count, lag, false);
        const uint32_t inverted = calibration_clk_marker_distance(
            expected_words, expected_sample_count,
            capture_words, capture_sample_count, lag, true);
        if (normal < result->best_distance) {
            result->second_distance = result->best_distance;
            result->second_lag_sample = result->best_lag_sample;
            result->best_distance = normal;
            result->best_lag_sample = lag;
        } else if (normal < result->second_distance) {
            result->second_distance = normal;
            result->second_lag_sample = lag;
        }
        if (inverted < result->inverted_best_distance) {
            result->inverted_best_distance = inverted;
            result->inverted_best_lag_sample = lag;
        }
        if (lag == UINT32_MAX) break;
    }
    result->margin = result->second_distance >= result->best_distance
                         ? result->second_distance - result->best_distance
                         : 0u;
    result->detected_polarity =
        result->inverted_best_distance < result->best_distance
            ? CALIBRATION_CLK_POLARITY_INVERTED
            : CALIBRATION_CLK_POLARITY_NORMAL;
    if (result->detected_polarity != CALIBRATION_CLK_POLARITY_NORMAL) {
        result->reject_reason = CALIBRATION_CLK_CORRELATION_REJECT_POLARITY;
        return true;
    }
    if (!calibration_clk_marker_validate_capture(
            expected, capture_words, capture_sample_count,
            result->best_lag_sample, &result->marker_flags)) {
        result->reject_reason =
            CALIBRATION_CLK_CORRELATION_REJECT_CAPTURE_TRUNCATED;
        return true;
    }
    result->reject_reason = calibration_clk_marker_reject_from_flags(
        result->marker_flags);
    if (result->reject_reason != CALIBRATION_CLK_CORRELATION_REJECT_NONE) {
        return true;
    }
    if (result->best_distance > gate->max_best_distance) {
        result->reject_reason = CALIBRATION_CLK_CORRELATION_REJECT_DISTANCE;
        return true;
    }
    if (result->margin < gate->min_margin) {
        result->reject_reason = CALIBRATION_CLK_CORRELATION_REJECT_MARGIN;
        return true;
    }
    result->accepted = 1u;
    return true;
}
