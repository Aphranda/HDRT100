#include "sma_cable_delay.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static int64_t sma_cable_delay_round_i64(double value)
{
    return value >= 0.0 ? (int64_t)(value + 0.5) : (int64_t)(value - 0.5);
}

static int32_t sma_cable_delay_wrap_delta_mdeg(int64_t delta)
{
    while (delta > SMA_CABLE_DELAY_HALF_TURN_MDEG) {
        delta -= SMA_CABLE_DELAY_FULL_TURN_MDEG;
    }
    while (delta <= -SMA_CABLE_DELAY_HALF_TURN_MDEG) {
        delta += SMA_CABLE_DELAY_FULL_TURN_MDEG;
    }
    return (int32_t)delta;
}

sma_cable_delay_status_t sma_cable_delay_fit_phase_slope(
    const sma_cable_delay_phase_point_t *points,
    size_t point_count,
    sma_cable_delay_fit_t *fit)
{
    if (fit != NULL) {
        memset(fit, 0, sizeof(*fit));
    }
    if (points == NULL || fit == NULL ||
        point_count > SMA_CABLE_DELAY_MAX_FIT_POINTS) {
        return SMA_CABLE_DELAY_STATUS_INVALID_ARGUMENT;
    }
    if (point_count < SMA_CABLE_DELAY_MIN_FIT_POINTS) {
        return SMA_CABLE_DELAY_STATUS_NOT_ENOUGH_POINTS;
    }

    int64_t unwrapped[SMA_CABLE_DELAY_MAX_FIT_POINTS];
    unwrapped[0] = points[0].phase_mdeg;
    uint32_t max_step_hz = 0u;
    for (size_t i = 1u; i < point_count; ++i) {
        if (points[i].frequency_hz <= points[i - 1u].frequency_hz) {
            return SMA_CABLE_DELAY_STATUS_FREQUENCY_NOT_INCREASING;
        }
        const uint32_t step_hz =
            points[i].frequency_hz - points[i - 1u].frequency_hz;
        if (step_hz > max_step_hz) {
            max_step_hz = step_hz;
        }

        const int64_t raw_delta =
            (int64_t)points[i].phase_mdeg - points[i - 1u].phase_mdeg;
        const int32_t wrapped_delta =
            sma_cable_delay_wrap_delta_mdeg(raw_delta);
        if (wrapped_delta == SMA_CABLE_DELAY_HALF_TURN_MDEG ||
            wrapped_delta == -SMA_CABLE_DELAY_HALF_TURN_MDEG) {
            return SMA_CABLE_DELAY_STATUS_PHASE_UNWRAP_AMBIGUOUS;
        }
        unwrapped[i] = unwrapped[i - 1u] + wrapped_delta;
    }

    double mean_frequency = 0.0;
    double mean_phase = 0.0;
    for (size_t i = 0u; i < point_count; ++i) {
        mean_frequency += (double)points[i].frequency_hz;
        mean_phase += (double)unwrapped[i];
    }
    mean_frequency /= (double)point_count;
    mean_phase /= (double)point_count;

    double covariance = 0.0;
    double frequency_variance = 0.0;
    for (size_t i = 0u; i < point_count; ++i) {
        const double dx = (double)points[i].frequency_hz - mean_frequency;
        const double dy = (double)unwrapped[i] - mean_phase;
        covariance += dx * dy;
        frequency_variance += dx * dx;
    }
    if (frequency_variance <= 0.0 || max_step_hz == 0u) {
        return SMA_CABLE_DELAY_STATUS_DEGENERATE_FIT;
    }

    const double slope_mdeg_per_hz = covariance / frequency_variance;
    const double intercept_mdeg = mean_phase - slope_mdeg_per_hz * mean_frequency;
    const double delay_ps =
        -slope_mdeg_per_hz * 1000000000000.0 /
        (double)SMA_CABLE_DELAY_FULL_TURN_MDEG;

    double residual_square_sum = 0.0;
    for (size_t i = 0u; i < point_count; ++i) {
        const double expected = intercept_mdeg +
                                slope_mdeg_per_hz *
                                    (double)points[i].frequency_hz;
        const double residual = (double)unwrapped[i] - expected;
        residual_square_sum += residual * residual;
    }

    fit->total_delay_ps = sma_cable_delay_round_i64(delay_ps);
    fit->intercept_mdeg = (int32_t)sma_cable_delay_round_i64(intercept_mdeg);
    fit->phase_rms_mdeg = (uint32_t)sma_cable_delay_round_i64(
        sqrt(residual_square_sum / (double)point_count));
    fit->max_unambiguous_delay_ps = 500000000000ull / max_step_hz;
    fit->point_count = (uint32_t)point_count;
    fit->valid = true;
    return SMA_CABLE_DELAY_STATUS_OK;
}

bool sma_cable_delay_resolve_channel(
    sma_cable_delay_mode_t mode,
    const sma_cable_delay_fit_t *fit,
    int64_t reference_total_delay_ps,
    bool reference_valid,
    int64_t channel_fixed_delay_ps,
    bool channel_fixed_valid,
    sma_cable_delay_channel_result_t *result)
{
    if (fit == NULL || result == NULL || !fit->valid ||
        (mode != SMA_CABLE_DELAY_MODE_ABSOLUTE_SELF_LOOP &&
         mode != SMA_CABLE_DELAY_MODE_RELATIVE_FOUR_SOURCE)) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->mode = mode;
    result->total_delay_ps = fit->total_delay_ps;
    result->total_delay_valid = true;
    result->phase_rms_mdeg = fit->phase_rms_mdeg;
    result->channel_fixed_delay_ps = channel_fixed_delay_ps;
    result->channel_fixed_delay_valid = channel_fixed_valid;

    if (reference_valid) {
        result->relative_delay_ps =
            fit->total_delay_ps - reference_total_delay_ps;
        result->relative_delay_valid = true;
    }

    if (mode == SMA_CABLE_DELAY_MODE_ABSOLUTE_SELF_LOOP &&
        channel_fixed_valid) {
        result->cable_delay_ps =
            fit->total_delay_ps - channel_fixed_delay_ps;
        result->cable_delay_valid = true;
    }
    return true;
}

bool sma_cable_delay_coarse_equal_cables(
    uint32_t common_cable_delay_ps,
    bool common_cable_delay_valid,
    uint32_t velocity_factor_ppm,
    const int64_t channel_relative_delay_ps[SMA_CABLE_DELAY_CHANNEL_COUNT],
    sma_cable_delay_coarse_result_t *result)
{
    if (result == NULL ||
        (common_cable_delay_valid && common_cable_delay_ps == 0u) ||
        velocity_factor_ppm > 1000000u) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->common_cable_delay_ps = common_cable_delay_ps;
    result->velocity_factor_ppm = velocity_factor_ppm;
    result->common_cable_delay_valid = common_cable_delay_valid;
    for (size_t channel = 0u;
         channel < SMA_CABLE_DELAY_CHANNEL_COUNT;
         ++channel) {
        sma_cable_delay_channel_result_t *item = &result->channels[channel];
        item->mode = common_cable_delay_valid
                         ? SMA_CABLE_DELAY_MODE_ABSOLUTE_SELF_LOOP
                         : SMA_CABLE_DELAY_MODE_RELATIVE_FOUR_SOURCE;
        item->cable_delay_ps = common_cable_delay_ps;
        item->cable_delay_valid = common_cable_delay_valid;
        if (channel_relative_delay_ps != NULL) {
            item->relative_delay_ps = channel_relative_delay_ps[channel];
            item->relative_delay_valid = true;
        }
    }
    return true;
}

static uint8_t sma_cable_delay_reverse_nibble(uint8_t value)
{
    value = (uint8_t)(((value & 0x5u) << 1u) |
                      ((value & 0xAu) >> 1u));
    return (uint8_t)(((value & 0x3u) << 2u) |
                     ((value & 0xCu) >> 2u));
}

static uint8_t sma_cable_delay_capture_sample(const uint32_t *capture_words,
                                              size_t sample_index,
                                              bool reverse_input_bits)
{
    const size_t word_index = sample_index / 8u;
    const uint32_t shift = 28u - (uint32_t)((sample_index % 8u) * 4u);
    uint8_t sample = (uint8_t)((capture_words[word_index] >> shift) & 0xFu);
    if (reverse_input_bits) {
        sample = sma_cable_delay_reverse_nibble(sample);
    }
    return sample;
}

bool sma_cable_delay_extract_phase_from_capture(
    const uint32_t *capture_words,
    size_t capture_word_count,
    uint32_t logical_channel,
    uint32_t period_samples,
    uint32_t sample_rate_hz,
    bool reverse_input_bits,
    sma_cable_delay_phase_extract_t *phase)
{
    if (phase != NULL) {
        memset(phase, 0, sizeof(*phase));
    }
    if (capture_words == NULL || phase == NULL || capture_word_count == 0u ||
        logical_channel >= SMA_CABLE_DELAY_CHANNEL_COUNT ||
        period_samples < 4u || sample_rate_hz == 0u) {
        return false;
    }

    const size_t sample_count = capture_word_count * 8u;
    const uint8_t channel_mask = (uint8_t)(1u << logical_channel);
    bool previous = (sma_cable_delay_capture_sample(capture_words,
                                                    0u,
                                                    reverse_input_bits) &
                     channel_mask) != 0u;
    int64_t phase_position_sum = 0;
    int32_t anchor_position = -1;
    uint32_t edge_count = 0u;
    uint32_t falling_edge_count = 0u;
    size_t first_rising_sample = 0u;
    size_t last_rising_sample = 0u;
    size_t high_sample_count = previous ? 1u : 0u;

    for (size_t sample_index = 1u;
         sample_index < sample_count;
         ++sample_index) {
        const bool level =
            (sma_cable_delay_capture_sample(capture_words,
                                            sample_index,
                                            reverse_input_bits) &
             channel_mask) != 0u;
        if (level) {
            high_sample_count++;
        }
        if (level && !previous) {
            if (edge_count == 0u) {
                first_rising_sample = sample_index;
            }
            last_rising_sample = sample_index;
            const int32_t position =
                (int32_t)(sample_index % period_samples);
            if (anchor_position < 0) {
                anchor_position = position;
            }
            int32_t delta = position - anchor_position;
            const int32_t half_period = (int32_t)(period_samples / 2u);
            while (delta > half_period) {
                delta -= (int32_t)period_samples;
            }
            while (delta < -half_period) {
                delta += (int32_t)period_samples;
            }
            phase_position_sum += (int64_t)anchor_position + delta;
            edge_count++;
        } else if (!level && previous) {
            falling_edge_count++;
        }
        previous = level;
    }

    if (edge_count < 2u) {
        return false;
    }

    int64_t mean_position_milli =
        (phase_position_sum * 1000ll) / (int64_t)edge_count;
    const int64_t period_milli = (int64_t)period_samples * 1000ll;
    mean_position_milli %= period_milli;
    if (mean_position_milli < 0) {
        mean_position_milli += period_milli;
    }

    int64_t phase_mdeg =
        -(mean_position_milli * SMA_CABLE_DELAY_FULL_TURN_MDEG) /
        period_milli;
    if (phase_mdeg < -SMA_CABLE_DELAY_HALF_TURN_MDEG) {
        phase_mdeg += SMA_CABLE_DELAY_FULL_TURN_MDEG;
    }

    phase->phase_mdeg = (int32_t)phase_mdeg;
    phase->rising_edge_count = edge_count;
    phase->falling_edge_count = falling_edge_count;
    phase->period_samples = period_samples;
    const size_t rising_span = last_rising_sample - first_rising_sample;
    phase->observed_frequency_hz = rising_span == 0u
        ? 0u
        : (uint32_t)((((uint64_t)edge_count - 1ull) * sample_rate_hz +
                      rising_span / 2u) /
                     rising_span);
    phase->duty_cycle_ppm = (uint32_t)(
        ((uint64_t)high_sample_count * 1000000ull + sample_count / 2u) /
        sample_count);
    phase->valid = true;
    return true;
}

bool sma_cable_delay_resolve_symmetric_rtt(
    uint32_t raw_round_trip_cycles,
    uint32_t responder_turnaround_cycles,
    uint32_t sample_period_ps,
    sma_cable_delay_symmetric_rtt_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (result == NULL || sample_period_ps == 0u ||
        raw_round_trip_cycles <= responder_turnaround_cycles) {
        return false;
    }

    const uint64_t path_cycles =
        (uint64_t)raw_round_trip_cycles - responder_turnaround_cycles;
    result->raw_round_trip_cycles = raw_round_trip_cycles;
    result->responder_turnaround_cycles = responder_turnaround_cycles;
    result->sample_period_ps = sample_period_ps;
    result->path_sum_ps = path_cycles * sample_period_ps;
    result->mean_leg_delay_ps = result->path_sum_ps / 2u;
    result->valid = true;
    return true;
}

const char *sma_cable_delay_status_string(sma_cable_delay_status_t status)
{
    switch (status) {
    case SMA_CABLE_DELAY_STATUS_OK:
        return "ok";
    case SMA_CABLE_DELAY_STATUS_INVALID_ARGUMENT:
        return "invalid_argument";
    case SMA_CABLE_DELAY_STATUS_NOT_ENOUGH_POINTS:
        return "not_enough_points";
    case SMA_CABLE_DELAY_STATUS_FREQUENCY_NOT_INCREASING:
        return "frequency_not_increasing";
    case SMA_CABLE_DELAY_STATUS_PHASE_UNWRAP_AMBIGUOUS:
        return "phase_unwrap_ambiguous";
    case SMA_CABLE_DELAY_STATUS_DEGENERATE_FIT:
        return "degenerate_fit";
    default:
        return "unknown";
    }
}
