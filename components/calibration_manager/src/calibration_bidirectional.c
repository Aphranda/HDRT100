#include "calibration_bidirectional.h"

#include <limits.h>
#include <string.h>

static bool calibration_bidirectional_has_all(
    uint32_t value,
    uint32_t required)
{
    return (value & required) == required;
}

static bool calibration_bidirectional_subtract_bias(
    uint64_t path_sum_ns,
    int64_t endpoint_bias_ns,
    int64_t *corrected_path_sum_ns)
{
    if (corrected_path_sum_ns == NULL ||
        path_sum_ns > (uint64_t)INT64_MAX) {
        return false;
    }

    const int64_t raw_path_sum_ns = (int64_t)path_sum_ns;
    if (endpoint_bias_ns > 0 &&
        raw_path_sum_ns < endpoint_bias_ns) {
        return false;
    }
    if (endpoint_bias_ns < 0 &&
        raw_path_sum_ns > INT64_MAX + endpoint_bias_ns) {
        return false;
    }

    *corrected_path_sum_ns = raw_path_sum_ns - endpoint_bias_ns;
    return *corrected_path_sum_ns >= 0;
}

bool calibration_bidirectional_evaluate(
    const calibration_bidirectional_sample_t *sample,
    const calibration_bidirectional_gate_t *gate,
    calibration_bidirectional_result_t *result)
{
    if (result == NULL) {
        return false;
    }
    memset(result, 0, sizeof(*result));
    result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_BAD_ARGUMENT;
    if (sample == NULL || gate == NULL) {
        return false;
    }

    if (!calibration_bidirectional_has_all(sample->edge_mask,
                                           gate->required_edge_mask)) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_MISSING_EDGE;
        return false;
    }
    /* t1/t4 are compared only in A's clock domain and t2/t3 only in B's
     * clock domain. Comparing t1 with t2 would incorrectly require a shared
     * absolute clock origin, which the two-way method is designed to avoid. */
    if (sample->t3_data_tx < sample->t2_clk_rx ||
        sample->t4_data_rx < sample->t1_clk_tx) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_EDGE_ORDER;
        return false;
    }
    if (!calibration_bidirectional_has_all(sample->sample_flags,
                                           gate->required_sample_flags)) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_SAMPLE_FLAGS;
        return false;
    }
    if (gate->expected_persona_generation != 0u &&
        sample->persona_generation != gate->expected_persona_generation) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_PERSONA;
        return false;
    }
    if (gate->expected_topology_generation != 0u &&
        sample->topology_generation != gate->expected_topology_generation) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_TOPOLOGY;
        return false;
    }
    if (gate->require_bias_generation &&
        (sample->bias_generation == 0u ||
         (sample->sample_flags & CALIBRATION_BIDIRECTIONAL_FLAG_BIAS_VALID) == 0u)) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_BIAS;
        return false;
    }
    if (gate->expected_bias_generation != 0u &&
        sample->bias_generation != gate->expected_bias_generation) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_BIAS;
        return false;
    }
    if (gate->require_fresh_topology &&
        (sample->topology_generation == 0u ||
         (sample->sample_flags & CALIBRATION_BIDIRECTIONAL_FLAG_TOPOLOGY_FRESH) == 0u)) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_TOPOLOGY;
        return false;
    }
    if (gate->require_repeat_statistics &&
        ((sample->sample_flags & CALIBRATION_BIDIRECTIONAL_FLAG_REPEAT_GATE) == 0u ||
         sample->repeat_count == 0u ||
         sample->accepted_repeat_count != sample->repeat_count)) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_SAMPLE_FLAGS;
        return false;
    }
    if (gate->require_asymmetry_bound &&
        ((sample->sample_flags & CALIBRATION_BIDIRECTIONAL_FLAG_ASYMMETRY_VALID) == 0u ||
         sample->asymmetry_ns > gate->max_asymmetry_ns)) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_SAMPLE_FLAGS;
        return false;
    }
    if (gate->max_jitter_ns != 0u && sample->jitter_ns > gate->max_jitter_ns) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_CLOCK_RATE;
        return false;
    }
    if (sample->dma_status != 0u) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_DMA;
        return false;
    }
    if (sample->clock_rate_error_bound_ns > gate->max_clock_rate_error_bound_ns) {
        result->clock_rate_error_bound_ns = sample->clock_rate_error_bound_ns;
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_CLOCK_RATE;
        return false;
    }
    if (sample->reference_loopback && !gate->allow_reference_loopback) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_LOOPBACK_POLICY;
        return false;
    }

    result->residence_ns = sample->t3_data_tx - sample->t2_clk_rx;
    const uint64_t elapsed_ns = sample->t4_data_rx - sample->t1_clk_tx;
    if (elapsed_ns < result->residence_ns) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_NEGATIVE_PATH;
        return false;
    }
    result->raw_path_sum_ns = elapsed_ns - result->residence_ns;
    if (!calibration_bidirectional_subtract_bias(result->raw_path_sum_ns,
                                                 sample->endpoint_bias_ns,
                                                 &result->corrected_path_sum_ns)) {
        result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_NEGATIVE_PATH;
        return false;
    }

    result->delay_estimate_ns = result->corrected_path_sum_ns / 2;
    result->clock_rate_error_bound_ns = sample->clock_rate_error_bound_ns;
    result->reject_reason = CALIBRATION_BIDIRECTIONAL_REJECT_NONE;
    result->reference_accepted = true;
    result->active_eligible = !sample->reference_loopback &&
                              (sample->sample_flags &
                               CALIBRATION_BIDIRECTIONAL_FLAG_DIAGNOSTIC_ONLY) == 0u &&
                               (sample->sample_flags &
                                CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED) != 0u &&
                               sample->bias_generation != 0u &&
                               sample->topology_generation != 0u &&
                               (sample->sample_flags &
                                CALIBRATION_BIDIRECTIONAL_FLAG_BIAS_VALID) != 0u &&
                               (sample->sample_flags &
                                CALIBRATION_BIDIRECTIONAL_FLAG_TOPOLOGY_FRESH) != 0u &&
                               (!gate->require_repeat_statistics ||
                                (sample->sample_flags &
                                 CALIBRATION_BIDIRECTIONAL_FLAG_REPEAT_GATE) != 0u) &&
                               (!gate->require_asymmetry_bound ||
                                (sample->sample_flags &
                                 CALIBRATION_BIDIRECTIONAL_FLAG_ASYMMETRY_VALID) != 0u);
    return true;
}
