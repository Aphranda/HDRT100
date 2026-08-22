#include "calibration_bias.h"

#include <limits.h>
#include <string.h>

#define CALIBRATION_BIAS_CRC_OFFSET 2166136261u
#define CALIBRATION_BIAS_CRC_PRIME 16777619u

#define CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED (1u << 0u)
#define CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH (1u << 2u)

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        hash ^= (value >> (i * 8u)) & 0xFFu;
        hash *= CALIBRATION_BIAS_CRC_PRIME;
    }
    return hash;
}

static uint32_t hash_i64(uint32_t hash, int64_t value)
{
    const uint64_t encoded = (uint64_t)value;
    hash = hash_u32(hash, (uint32_t)encoded);
    return hash_u32(hash, (uint32_t)(encoded >> 32u));
}

static bool signed_difference(uint64_t observed,
                              uint64_t expected,
                              int64_t *difference)
{
    if (difference == NULL) return false;
    if (observed >= expected) {
        const uint64_t delta = observed - expected;
        if (delta > (uint64_t)INT64_MAX) return false;
        *difference = (int64_t)delta;
    } else {
        const uint64_t delta = expected - observed;
        if (delta > (uint64_t)INT64_MAX + 1ull) return false;
        if (delta == (uint64_t)INT64_MAX + 1ull) {
            *difference = INT64_MIN;
        } else {
            *difference = -(int64_t)delta;
        }
    }
    return true;
}

void calibration_bias_begin(calibration_bias_accumulator_t *accumulator,
                            const calibration_bias_gate_t *gate,
                            uint32_t generation)
{
    if (accumulator == NULL) return;
    memset(accumulator, 0, sizeof(*accumulator));
    if (gate != NULL) accumulator->gate = *gate;
    accumulator->generation = generation;
    accumulator->min_bias_ns = INT64_MAX;
    accumulator->max_bias_ns = INT64_MIN;
}

static uint32_t reject_sample(calibration_bias_accumulator_t *accumulator,
                              uint32_t reason)
{
    accumulator->rejected_count++;
    accumulator->last_reject_reason = reason;
    return reason;
}

bool calibration_bias_add(calibration_bias_accumulator_t *accumulator,
                           const calibration_bias_sample_t *sample)
{
    if (accumulator == NULL || sample == NULL || accumulator->generation == 0u) {
        return false;
    }
    accumulator->sample_count++;
    const calibration_bias_gate_t *gate = &accumulator->gate;
    if (!sample->reference_loopback) {
        (void)reject_sample(accumulator,
                            CALIBRATION_BIAS_REJECT_REFERENCE_POLICY);
        return false;
    }
    if (gate->require_hardware_latched &&
        (sample->sample_flags & CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED) == 0u) {
        (void)reject_sample(accumulator, CALIBRATION_BIAS_REJECT_HARDWARE_LATCH);
        return false;
    }
    if (gate->require_sync_match &&
        (sample->sample_flags & CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH) == 0u) {
        (void)reject_sample(accumulator, CALIBRATION_BIAS_REJECT_SYNC);
        return false;
    }
    if (gate->expected_persona_generation != 0u &&
        sample->persona_generation != gate->expected_persona_generation) {
        (void)reject_sample(accumulator, CALIBRATION_BIAS_REJECT_PERSONA);
        return false;
    }
    if (gate->expected_profile_crc32 != 0u &&
        sample->profile_crc32 != gate->expected_profile_crc32) {
        (void)reject_sample(accumulator, CALIBRATION_BIAS_REJECT_PROFILE);
        return false;
    }
    if (gate->expected_topology_generation != 0u &&
        sample->topology_generation != gate->expected_topology_generation) {
        (void)reject_sample(accumulator, CALIBRATION_BIAS_REJECT_TOPOLOGY);
        return false;
    }
    if (gate->maximum_clock_error_ns != 0u &&
        sample->clock_error_bound_ns > gate->maximum_clock_error_ns) {
        (void)reject_sample(accumulator, CALIBRATION_BIAS_REJECT_CLOCK);
        return false;
    }

    int64_t bias_ns = 0;
    if (!signed_difference(sample->raw_path_sum_ns,
                           gate->expected_path_sum_ns,
                           &bias_ns) ||
        (bias_ns > 0 && accumulator->sum_bias_ns > INT64_MAX - bias_ns) ||
        (bias_ns < 0 && accumulator->sum_bias_ns < INT64_MIN - bias_ns)) {
        (void)reject_sample(accumulator, CALIBRATION_BIAS_REJECT_ARGUMENT);
        return false;
    }
    if (accumulator->accepted_count == 0u) {
        accumulator->min_bias_ns = bias_ns;
        accumulator->max_bias_ns = bias_ns;
        accumulator->first_epoch = sample->epoch;
    } else {
        if (bias_ns < accumulator->min_bias_ns) accumulator->min_bias_ns = bias_ns;
        if (bias_ns > accumulator->max_bias_ns) accumulator->max_bias_ns = bias_ns;
    }
    accumulator->sum_bias_ns += bias_ns;
    accumulator->accepted_count++;
    accumulator->last_epoch = sample->epoch;
    return true;
}

uint32_t calibration_bias_snapshot_crc32(
    const calibration_bias_snapshot_t *snapshot)
{
    if (snapshot == NULL) return 0u;
    uint32_t hash = CALIBRATION_BIAS_CRC_OFFSET;
    hash = hash_u32(hash, snapshot->valid);
    hash = hash_u32(hash, snapshot->flags);
    hash = hash_u32(hash, snapshot->generation);
    hash = hash_u32(hash, snapshot->sample_count);
    hash = hash_u32(hash, snapshot->accepted_count);
    hash = hash_u32(hash, snapshot->rejected_count);
    hash = hash_u32(hash, snapshot->persona_generation);
    hash = hash_u32(hash, snapshot->profile_crc32);
    hash = hash_u32(hash, snapshot->topology_generation);
    hash = hash_u32(hash, snapshot->first_epoch);
    hash = hash_u32(hash, snapshot->last_epoch);
    hash = hash_i64(hash, snapshot->mean_bias_ns);
    hash = hash_u32(hash, snapshot->spread_ns);
    return hash;
}

bool calibration_bias_finalize(const calibration_bias_accumulator_t *accumulator,
                               calibration_bias_snapshot_t *snapshot)
{
    if (accumulator == NULL || snapshot == NULL) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->generation = accumulator->generation;
    snapshot->sample_count = accumulator->sample_count;
    snapshot->accepted_count = accumulator->accepted_count;
    snapshot->rejected_count = accumulator->rejected_count;
    snapshot->persona_generation = accumulator->gate.expected_persona_generation;
    snapshot->profile_crc32 = accumulator->gate.expected_profile_crc32;
    snapshot->topology_generation = accumulator->gate.expected_topology_generation;
    snapshot->first_epoch = accumulator->first_epoch;
    snapshot->last_epoch = accumulator->last_epoch;
    if (accumulator->accepted_count == 0u) {
        snapshot->reject_reason = CALIBRATION_BIAS_REJECT_SAMPLE_COUNT;
        snapshot->table_crc32 = calibration_bias_snapshot_crc32(snapshot);
        return false;
    }
    snapshot->mean_bias_ns = accumulator->sum_bias_ns /
                             (int64_t)accumulator->accepted_count;
    const int64_t spread = accumulator->max_bias_ns - accumulator->min_bias_ns;
    if (spread < 0 || (uint64_t)spread > UINT32_MAX) {
        snapshot->reject_reason = CALIBRATION_BIAS_REJECT_ARGUMENT;
        snapshot->table_crc32 = calibration_bias_snapshot_crc32(snapshot);
        return false;
    }
    snapshot->spread_ns = (uint32_t)spread;
    if (accumulator->accepted_count < accumulator->gate.minimum_samples) {
        snapshot->reject_reason = CALIBRATION_BIAS_REJECT_SAMPLE_COUNT;
        snapshot->table_crc32 = calibration_bias_snapshot_crc32(snapshot);
        return false;
    }
    if (accumulator->gate.maximum_spread_ns != 0u &&
        snapshot->spread_ns > accumulator->gate.maximum_spread_ns) {
        snapshot->reject_reason = CALIBRATION_BIAS_REJECT_SPREAD;
        snapshot->table_crc32 = calibration_bias_snapshot_crc32(snapshot);
        return false;
    }
    snapshot->valid = 1u;
    snapshot->flags = CALIBRATION_BIAS_FLAG_VALID |
                      CALIBRATION_BIAS_FLAG_HARDWARE_LATCHED |
                      CALIBRATION_BIAS_FLAG_SYNC_MATCH |
                      CALIBRATION_BIAS_FLAG_REFERENCE_LOOPBACK |
                      CALIBRATION_BIAS_FLAG_REPEAT_GATE;
    snapshot->reject_reason = CALIBRATION_BIAS_REJECT_NONE;
    snapshot->table_crc32 = calibration_bias_snapshot_crc32(snapshot);
    return true;
}

bool calibration_bias_snapshot_validate(
    const calibration_bias_snapshot_t *snapshot)
{
    return snapshot != NULL && snapshot->valid != 0u &&
           snapshot->generation != 0u && snapshot->sample_count != 0u &&
           snapshot->accepted_count != 0u &&
           snapshot->accepted_count <= snapshot->sample_count &&
           snapshot->rejected_count ==
               snapshot->sample_count - snapshot->accepted_count &&
           (snapshot->flags & (CALIBRATION_BIAS_FLAG_VALID |
                               CALIBRATION_BIAS_FLAG_HARDWARE_LATCHED |
                               CALIBRATION_BIAS_FLAG_SYNC_MATCH |
                               CALIBRATION_BIAS_FLAG_REFERENCE_LOOPBACK |
                               CALIBRATION_BIAS_FLAG_REPEAT_GATE)) ==
               (CALIBRATION_BIAS_FLAG_VALID |
                CALIBRATION_BIAS_FLAG_HARDWARE_LATCHED |
                CALIBRATION_BIAS_FLAG_SYNC_MATCH |
                CALIBRATION_BIAS_FLAG_REFERENCE_LOOPBACK |
                CALIBRATION_BIAS_FLAG_REPEAT_GATE) &&
           snapshot->table_crc32 == calibration_bias_snapshot_crc32(snapshot);
}
