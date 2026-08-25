#include "calibration_training_phase.h"

#include <stddef.h>

bool calibration_training_phase_delay_samples(
    uint32_t link_base_delay_ns,
    uint32_t sample_period_ns,
    int32_t node_offset_sample_count,
    uint32_t max_delay_samples,
    uint32_t *phase_delay_samples)
{
    if (phase_delay_samples == NULL || link_base_delay_ns == 0u ||
        sample_period_ns == 0u ||
        node_offset_sample_count <
            CALIBRATION_TRAINING_PHASE_MIN_OFFSET_SAMPLES ||
        node_offset_sample_count >
            CALIBRATION_TRAINING_PHASE_MAX_OFFSET_SAMPLES) {
        return false;
    }
    return calibration_training_phase_delay_from_base_samples(
        (uint32_t)(((uint64_t)link_base_delay_ns +
                    sample_period_ns / 2u) / sample_period_ns),
        node_offset_sample_count, max_delay_samples, phase_delay_samples);
}

bool calibration_training_phase_delay_from_base_samples(
    uint32_t link_base_delay_samples,
    int32_t node_offset_sample_count,
    uint32_t max_delay_samples,
    uint32_t *phase_delay_samples)
{
    if (phase_delay_samples == NULL ||
        node_offset_sample_count <
            CALIBRATION_TRAINING_PHASE_MIN_OFFSET_SAMPLES ||
        node_offset_sample_count >
            CALIBRATION_TRAINING_PHASE_MAX_OFFSET_SAMPLES) {
        return false;
    }
    const int64_t delay_samples =
        (int64_t)link_base_delay_samples + node_offset_sample_count;
    if (delay_samples < 0 || delay_samples > (int64_t)max_delay_samples) {
        return false;
    }
    *phase_delay_samples = (uint32_t)delay_samples;
    return true;
}
