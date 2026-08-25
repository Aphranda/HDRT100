#ifndef CALIBRATION_TRAINING_PHASE_H
#define CALIBRATION_TRAINING_PHASE_H

#include <stdbool.h>
#include <stdint.h>

#define CALIBRATION_TRAINING_PHASE_MIN_OFFSET_SAMPLES (-10)
#define CALIBRATION_TRAINING_PHASE_MAX_OFFSET_SAMPLES 10
#define CALIBRATION_TRAINING_PHASE_MAX_NODES 8u

bool calibration_training_phase_delay_samples(
    uint32_t link_base_delay_ns,
    uint32_t sample_period_ns,
    int32_t node_offset_sample_count,
    uint32_t max_delay_samples,
    uint32_t *phase_delay_samples);
bool calibration_training_phase_delay_from_base_samples(
    uint32_t link_base_delay_samples,
    int32_t node_offset_sample_count,
    uint32_t max_delay_samples,
    uint32_t *phase_delay_samples);

#endif
