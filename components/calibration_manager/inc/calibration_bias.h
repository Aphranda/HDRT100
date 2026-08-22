#ifndef CALIBRATION_BIAS_H
#define CALIBRATION_BIAS_H

#include <stdbool.h>
#include <stdint.h>

/* Bias is derived only from completed reference-loopback evidence.  The
 * expected reference path is supplied by the fixture/profile owner; it is
 * never inferred from a software timestamp. */
typedef struct {
    uint64_t expected_path_sum_ns;
    uint32_t expected_persona_generation;
    uint32_t expected_profile_crc32;
    uint32_t expected_topology_generation;
    uint32_t minimum_samples;
    uint32_t maximum_samples;
    uint32_t maximum_spread_ns;
    uint64_t maximum_clock_error_ns;
    bool require_hardware_latched;
    bool require_sync_match;
} calibration_bias_gate_t;

typedef struct {
    uint64_t raw_path_sum_ns;
    uint64_t clock_error_bound_ns;
    uint32_t persona_generation;
    uint32_t profile_crc32;
    uint32_t topology_generation;
    uint32_t sample_flags;
    uint32_t epoch;
    bool reference_loopback;
} calibration_bias_sample_t;

typedef enum {
    CALIBRATION_BIAS_REJECT_NONE = 0u,
    CALIBRATION_BIAS_REJECT_ARGUMENT = 1u,
    CALIBRATION_BIAS_REJECT_REFERENCE_POLICY = 2u,
    CALIBRATION_BIAS_REJECT_HARDWARE_LATCH = 3u,
    CALIBRATION_BIAS_REJECT_SYNC = 4u,
    CALIBRATION_BIAS_REJECT_PERSONA = 5u,
    CALIBRATION_BIAS_REJECT_PROFILE = 6u,
    CALIBRATION_BIAS_REJECT_TOPOLOGY = 7u,
    CALIBRATION_BIAS_REJECT_CLOCK = 8u,
    CALIBRATION_BIAS_REJECT_SAMPLE_COUNT = 9u,
    CALIBRATION_BIAS_REJECT_SPREAD = 10u,
} calibration_bias_reject_reason_t;

#define CALIBRATION_BIAS_FLAG_VALID (1u << 0u)
#define CALIBRATION_BIAS_FLAG_HARDWARE_LATCHED (1u << 1u)
#define CALIBRATION_BIAS_FLAG_SYNC_MATCH (1u << 2u)
#define CALIBRATION_BIAS_FLAG_REFERENCE_LOOPBACK (1u << 3u)
#define CALIBRATION_BIAS_FLAG_REPEAT_GATE (1u << 4u)

typedef struct {
    calibration_bias_gate_t gate;
    uint32_t generation;
    uint32_t sample_count;
    uint32_t accepted_count;
    uint32_t rejected_count;
    int64_t sum_bias_ns;
    int64_t min_bias_ns;
    int64_t max_bias_ns;
    uint32_t first_epoch;
    uint32_t last_epoch;
    uint32_t last_reject_reason;
} calibration_bias_accumulator_t;

typedef struct {
    uint32_t valid;
    uint32_t flags;
    uint32_t reject_reason;
    uint32_t generation;
    uint32_t sample_count;
    uint32_t accepted_count;
    uint32_t rejected_count;
    uint32_t persona_generation;
    uint32_t profile_crc32;
    uint32_t topology_generation;
    uint32_t first_epoch;
    uint32_t last_epoch;
    int64_t mean_bias_ns;
    uint32_t spread_ns;
    uint32_t table_crc32;
} calibration_bias_snapshot_t;

void calibration_bias_begin(calibration_bias_accumulator_t *accumulator,
                            const calibration_bias_gate_t *gate,
                            uint32_t generation);
bool calibration_bias_add(calibration_bias_accumulator_t *accumulator,
                           const calibration_bias_sample_t *sample);
bool calibration_bias_finalize(const calibration_bias_accumulator_t *accumulator,
                               calibration_bias_snapshot_t *snapshot);
uint32_t calibration_bias_snapshot_crc32(
    const calibration_bias_snapshot_t *snapshot);
bool calibration_bias_snapshot_validate(
    const calibration_bias_snapshot_t *snapshot);

#endif
