#include "calibration_path_snapshot.h"

#include <limits.h>
#include <string.h>

#define CALIBRATION_PATH_SNAPSHOT_VERSION 1u
#define CALIBRATION_PATH_CRC_OFFSET 2166136261u
#define CALIBRATION_PATH_CRC_PRIME 16777619u

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        hash ^= (value >> (i * 8u)) & 0xFFu;
        hash *= CALIBRATION_PATH_CRC_PRIME;
    }
    return hash;
}

static uint32_t hash_u64(uint32_t hash, uint64_t value)
{
    hash = hash_u32(hash, (uint32_t)value);
    return hash_u32(hash, (uint32_t)(value >> 32u));
}

static bool gate_link(const calibration_path_link_evidence_t *link,
                      const calibration_path_gate_t *gate)
{
    const calibration_bidirectional_result_t *measurement =
        &link->measurement;
    if (!measurement->reference_accepted || !measurement->active_eligible) {
        return false;
    }
    if (gate->require_hardware_latch && !measurement->active_eligible) {
        return false;
    }
    if (gate->expected_topology_generation != 0u &&
        link->topology_generation != gate->expected_topology_generation) {
        return false;
    }
    if (gate->expected_bias_generation != 0u &&
        link->bias_generation != gate->expected_bias_generation) {
        return false;
    }
    if (gate->expected_profile_crc32 != 0u &&
        link->profile_crc32 != gate->expected_profile_crc32) {
        return false;
    }
    if (gate->require_repeat_statistics &&
        (link->sample_count == 0u ||
         link->accepted_count != link->sample_count)) {
        return false;
    }
    if (gate->require_asymmetry_bound &&
        gate->max_asymmetry_ns != 0u &&
        link->asymmetry_ns > gate->max_asymmetry_ns) {
        return false;
    }
    return gate->max_jitter_ns == 0u || link->jitter_ns <= gate->max_jitter_ns;
}

uint32_t calibration_path_snapshot_crc32(
    const calibration_path_snapshot_t *snapshot)
{
    uint32_t hash = CALIBRATION_PATH_CRC_OFFSET;
    if (snapshot == NULL) return 0u;
    hash = hash_u32(hash, snapshot->version);
    hash = hash_u32(hash, snapshot->valid);
    hash = hash_u32(hash, snapshot->active);
    hash = hash_u32(hash, snapshot->flags);
    hash = hash_u32(hash, snapshot->link_count);
    hash = hash_u32(hash, snapshot->topology_generation);
    hash = hash_u32(hash, snapshot->bias_generation);
    hash = hash_u32(hash, snapshot->profile_crc32);
    hash = hash_u32(hash, snapshot->calibration_generation);
    hash = hash_u32(hash, snapshot->freshness_us);
    hash = hash_u64(hash, snapshot->cumulative_delay_ns);
    hash = hash_u64(hash, snapshot->ring_round_trip_ns);
    hash = hash_u64(hash, snapshot->residual_ns);
    for (uint32_t i = 0u; i < CALIBRATION_PATH_MAX_LINKS; i++) {
        const calibration_path_link_evidence_t *link = &snapshot->links[i];
        hash = hash_u32(hash, link->source_slot_id);
        hash = hash_u32(hash, link->destination_slot_id);
        hash = hash_u32(hash, link->profile_crc32);
        hash = hash_u32(hash, link->topology_generation);
        hash = hash_u32(hash, link->bias_generation);
        hash = hash_u32(hash, link->sample_count);
        hash = hash_u32(hash, link->accepted_count);
        hash = hash_u32(hash, link->jitter_ns);
        hash = hash_u32(hash, link->asymmetry_ns);
        hash = hash_u64(hash, (uint64_t)link->measurement.delay_estimate_ns);
        hash = hash_u64(hash, (uint64_t)link->measurement.corrected_path_sum_ns);
    }
    return hash;
}

bool calibration_path_snapshot_build(
    const calibration_path_link_evidence_t *links,
    uint32_t link_count,
    uint64_t ring_round_trip_ns,
    const calibration_path_gate_t *gate,
    calibration_path_snapshot_t *snapshot)
{
    if (snapshot == NULL || gate == NULL || links == NULL ||
        link_count < 2u || link_count > CALIBRATION_PATH_MAX_LINKS) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = CALIBRATION_PATH_SNAPSHOT_VERSION;
    snapshot->link_count = link_count;
    snapshot->ring_round_trip_ns = ring_round_trip_ns;
    snapshot->freshness_us = gate->freshness_us;
    snapshot->calibration_generation = gate->calibration_generation;
    for (uint32_t i = 0u; i < link_count; i++) {
        const calibration_path_link_evidence_t *link = &links[i];
        if (link->source_slot_id >= link_count ||
            link->destination_slot_id != ((link->source_slot_id + 1u) % link_count) ||
            !gate_link(link, gate) ||
            link->measurement.corrected_path_sum_ns < 0 ||
            link->measurement.delay_estimate_ns < 0) {
            snapshot->reject_reason = CALIBRATION_PATH_REJECT_LINK;
            snapshot->table_crc32 = calibration_path_snapshot_crc32(snapshot);
            return false;
        }
        if (i == 0u) {
            snapshot->topology_generation = link->topology_generation;
            snapshot->bias_generation = link->bias_generation;
            snapshot->profile_crc32 = link->profile_crc32;
        } else if (link->topology_generation != snapshot->topology_generation ||
                   link->bias_generation != snapshot->bias_generation ||
                   link->profile_crc32 != snapshot->profile_crc32) {
            snapshot->reject_reason = CALIBRATION_PATH_REJECT_GENERATION;
            snapshot->table_crc32 = calibration_path_snapshot_crc32(snapshot);
            return false;
        }
        snapshot->links[i] = *link;
        const uint64_t delay = (uint64_t)link->measurement.delay_estimate_ns;
        if (UINT64_MAX - snapshot->cumulative_delay_ns < delay) {
            snapshot->reject_reason = CALIBRATION_PATH_REJECT_ARGUMENT;
            snapshot->table_crc32 = calibration_path_snapshot_crc32(snapshot);
            return false;
        }
        snapshot->cumulative_delay_ns += delay;
    }
    if (gate->require_ring_round_trip && ring_round_trip_ns == 0u) {
        snapshot->reject_reason = CALIBRATION_PATH_REJECT_ARGUMENT;
        snapshot->table_crc32 = calibration_path_snapshot_crc32(snapshot);
        return false;
    }
    snapshot->residual_ns = ring_round_trip_ns >= snapshot->cumulative_delay_ns
                                ? ring_round_trip_ns - snapshot->cumulative_delay_ns
                                : snapshot->cumulative_delay_ns - ring_round_trip_ns;
    if (gate->max_residual_ns != 0u &&
        snapshot->residual_ns > gate->max_residual_ns) {
        snapshot->reject_reason = CALIBRATION_PATH_REJECT_RING_RESIDUAL;
        snapshot->table_crc32 = calibration_path_snapshot_crc32(snapshot);
        return false;
    }
    if (gate->freshness_us == 0u) {
        snapshot->reject_reason = CALIBRATION_PATH_REJECT_FRESHNESS;
        snapshot->table_crc32 = calibration_path_snapshot_crc32(snapshot);
        return false;
    }
    snapshot->valid = 1u;
    snapshot->active = 1u;
    snapshot->flags = CALIBRATION_PATH_FLAG_VALID |
                      CALIBRATION_PATH_FLAG_ACTIVE |
                      CALIBRATION_PATH_FLAG_HARDWARE_LATCHED |
                      CALIBRATION_PATH_FLAG_BIAS_VALID |
                      CALIBRATION_PATH_FLAG_TOPOLOGY_FRESH |
                      CALIBRATION_PATH_FLAG_REPEAT_GATE |
                      CALIBRATION_PATH_FLAG_ASYMMETRY_VALID;
    snapshot->table_crc32 = calibration_path_snapshot_crc32(snapshot);
    return true;
}

bool calibration_path_snapshot_validate(
    const calibration_path_snapshot_t *snapshot)
{
    return snapshot != NULL && snapshot->version == CALIBRATION_PATH_SNAPSHOT_VERSION &&
           snapshot->valid != 0u && snapshot->active != 0u &&
           (snapshot->flags & (CALIBRATION_PATH_FLAG_VALID |
                               CALIBRATION_PATH_FLAG_ACTIVE |
                               CALIBRATION_PATH_FLAG_HARDWARE_LATCHED |
                               CALIBRATION_PATH_FLAG_BIAS_VALID |
                               CALIBRATION_PATH_FLAG_TOPOLOGY_FRESH |
                               CALIBRATION_PATH_FLAG_REPEAT_GATE |
                               CALIBRATION_PATH_FLAG_ASYMMETRY_VALID)) ==
               (CALIBRATION_PATH_FLAG_VALID |
                CALIBRATION_PATH_FLAG_ACTIVE |
                CALIBRATION_PATH_FLAG_HARDWARE_LATCHED |
                CALIBRATION_PATH_FLAG_BIAS_VALID |
                CALIBRATION_PATH_FLAG_TOPOLOGY_FRESH |
                CALIBRATION_PATH_FLAG_REPEAT_GATE |
                CALIBRATION_PATH_FLAG_ASYMMETRY_VALID) &&
           snapshot->link_count >= 2u &&
           snapshot->link_count <= CALIBRATION_PATH_MAX_LINKS &&
           snapshot->topology_generation != 0u &&
           snapshot->bias_generation != 0u &&
           snapshot->profile_crc32 != 0u &&
           snapshot->freshness_us != 0u &&
           snapshot->table_crc32 == calibration_path_snapshot_crc32(snapshot);
}
