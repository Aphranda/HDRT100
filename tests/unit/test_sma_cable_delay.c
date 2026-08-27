#include "sma_cable_delay.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static int32_t wrap_phase(int64_t phase_mdeg)
{
    while (phase_mdeg >= SMA_CABLE_DELAY_HALF_TURN_MDEG) {
        phase_mdeg -= SMA_CABLE_DELAY_FULL_TURN_MDEG;
    }
    while (phase_mdeg < -SMA_CABLE_DELAY_HALF_TURN_MDEG) {
        phase_mdeg += SMA_CABLE_DELAY_FULL_TURN_MDEG;
    }
    return (int32_t)phase_mdeg;
}

static void test_phase_slope_unwraps_and_fits_delay(void)
{
    static const uint32_t frequencies[] = {
        2000000u, 6000000u, 10000000u, 14000000u, 18000000u,
    };
    sma_cable_delay_phase_point_t points[5];
    const int64_t expected_delay_ps = 32500;
    const int32_t intercept_mdeg = 27000;
    for (size_t i = 0u; i < 5u; ++i) {
        const int64_t phase = intercept_mdeg -
                              ((int64_t)frequencies[i] *
                               expected_delay_ps *
                               SMA_CABLE_DELAY_FULL_TURN_MDEG) /
                                  1000000000000ll;
        points[i].frequency_hz = frequencies[i];
        points[i].phase_mdeg = wrap_phase(phase);
    }

    sma_cable_delay_fit_t fit;
    assert(sma_cable_delay_fit_phase_slope(points, 5u, &fit) ==
           SMA_CABLE_DELAY_STATUS_OK);
    assert(fit.valid);
    assert(fit.total_delay_ps >= expected_delay_ps - 2);
    assert(fit.total_delay_ps <= expected_delay_ps + 2);
    assert(fit.phase_rms_mdeg <= 1u);
    assert(fit.max_unambiguous_delay_ps == 125000u);
}

static void test_invalid_frequency_order_is_rejected(void)
{
    const sma_cable_delay_phase_point_t points[] = {
        {1000000u, 0}, {2000000u, -1000}, {2000000u, -2000},
    };
    sma_cable_delay_fit_t fit;
    assert(sma_cable_delay_fit_phase_slope(points, 3u, &fit) ==
           SMA_CABLE_DELAY_STATUS_FREQUENCY_NOT_INCREASING);
    assert(!fit.valid);
}

static void test_channel_resolution_preserves_measurement_boundary(void)
{
    const sma_cable_delay_fit_t fit = {
        .total_delay_ps = 48500,
        .phase_rms_mdeg = 120,
        .valid = true,
    };
    sma_cable_delay_channel_result_t channel;

    assert(sma_cable_delay_resolve_channel(
        SMA_CABLE_DELAY_MODE_RELATIVE_FOUR_SOURCE,
        &fit,
        47000,
        true,
        8500,
        true,
        &channel));
    assert(channel.total_delay_valid);
    assert(channel.total_delay_ps == 48500);
    assert(channel.relative_delay_valid);
    assert(channel.relative_delay_ps == 1500);
    assert(!channel.cable_delay_valid);

    assert(sma_cable_delay_resolve_channel(
        SMA_CABLE_DELAY_MODE_ABSOLUTE_SELF_LOOP,
        &fit,
        0,
        false,
        8500,
        true,
        &channel));
    assert(channel.cable_delay_valid);
    assert(channel.cable_delay_ps == 40000);
}

static void test_equal_cable_coarse_result_is_explicit(void)
{
    const int64_t residuals[SMA_CABLE_DELAY_CHANNEL_COUNT] = {
        0, 4000, -4000, 0,
    };
    sma_cable_delay_coarse_result_t result;
    assert(sma_cable_delay_coarse_equal_cables(
        25000u, true, 695000u, residuals, &result));
    assert(result.common_cable_delay_ps == 25000u);
    assert(result.velocity_factor_ppm == 695000u);
    for (size_t channel = 0u;
         channel < SMA_CABLE_DELAY_CHANNEL_COUNT;
         ++channel) {
        assert(result.channels[channel].cable_delay_valid);
        assert(result.channels[channel].cable_delay_ps == 25000);
        assert(result.channels[channel].relative_delay_valid);
        assert(result.channels[channel].relative_delay_ps == residuals[channel]);
    }

    assert(sma_cable_delay_coarse_equal_cables(
        0u, false, 0u, residuals, &result));
    assert(!result.common_cable_delay_valid);
    for (size_t channel = 0u;
         channel < SMA_CABLE_DELAY_CHANNEL_COUNT;
         ++channel) {
        assert(!result.channels[channel].cable_delay_valid);
        assert(result.channels[channel].relative_delay_valid);
        assert(result.channels[channel].relative_delay_ps == residuals[channel]);
    }
}

static void test_packed_capture_phase_extracts_logical_channel(void)
{
    uint32_t words[8] = {0u};
    const uint32_t period_samples = 16u;
    const uint32_t delay_samples = 3u;
    for (size_t sample = 0u; sample < 64u; ++sample) {
        const uint32_t in_period = (uint32_t)(sample % period_samples);
        const bool logical_channel_0_high =
            in_period >= delay_samples &&
            in_period < delay_samples + period_samples / 2u;
        /* Physical GPIO20 is logical IN4, so logical IN1 occupies raw bit 3. */
        const uint32_t raw_nibble = logical_channel_0_high ? 0x8u : 0u;
        const size_t word = sample / 8u;
        const uint32_t shift = 28u - (uint32_t)((sample % 8u) * 4u);
        words[word] |= raw_nibble << shift;
    }

    sma_cable_delay_phase_extract_t phase;
    assert(sma_cable_delay_extract_phase_from_capture(
        words, 8u, 0u, period_samples, 250000000u, true, &phase));
    assert(phase.valid);
    assert(phase.rising_edge_count >= 3u);
    assert(phase.falling_edge_count == 4u);
    assert(phase.observed_frequency_hz == 15625000u);
    assert(phase.duty_cycle_ppm == 500000u);
    assert(phase.phase_mdeg == -67500);
}

int main(void)
{
    test_phase_slope_unwraps_and_fits_delay();
    test_invalid_frequency_order_is_rejected();
    test_channel_resolution_preserves_measurement_boundary();
    test_equal_cable_coarse_result_is_explicit();
    test_packed_capture_phase_extracts_logical_channel();
    puts("sma_cable_delay tests passed");
    return 0;
}
