#ifndef CALIBRATION_PATH_SNAPSHOT_H
#define CALIBRATION_PATH_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "calibration_bidirectional.h"

#define CALIBRATION_PATH_MAX_LINKS 8u

typedef enum {
    CALIBRATION_PATH_REJECT_NONE = 0u,
    CALIBRATION_PATH_REJECT_ARGUMENT = 1u,
    CALIBRATION_PATH_REJECT_LINK = 2u,
    CALIBRATION_PATH_REJECT_TOPOLOGY = 3u,
    CALIBRATION_PATH_REJECT_GENERATION = 4u,
    CALIBRATION_PATH_REJECT_RING_RESIDUAL = 5u,
    CALIBRATION_PATH_REJECT_FRESHNESS = 6u,
} calibration_path_reject_reason_t;

#define CALIBRATION_PATH_FLAG_VALID (1u << 0u)
#define CALIBRATION_PATH_FLAG_ACTIVE (1u << 1u)
#define CALIBRATION_PATH_FLAG_HARDWARE_LATCHED (1u << 2u)
#define CALIBRATION_PATH_FLAG_BIAS_VALID (1u << 3u)
#define CALIBRATION_PATH_FLAG_TOPOLOGY_FRESH (1u << 4u)
#define CALIBRATION_PATH_FLAG_REPEAT_GATE (1u << 5u)
#define CALIBRATION_PATH_FLAG_ASYMMETRY_VALID (1u << 6u)
#define CALIBRATION_PATH_FLAG_DIAGNOSTIC_ONLY (1u << 7u)
#define CALIBRATION_PATH_FLAG_CANDIDATE (1u << 8u)
#define CALIBRATION_PATH_FLAG_ROLLBACKABLE (1u << 9u)

typedef struct {
    uint32_t source_node;
    uint32_t destination_node;
    uint32_t profile_crc32;
    uint32_t topology_generation;
    uint32_t bias_generation;
    uint32_t sample_count;
    uint32_t accepted_count;
    uint32_t jitter_ns;
    uint32_t asymmetry_ns;
    calibration_bidirectional_result_t measurement;
} calibration_path_link_evidence_t;

typedef struct {
    uint32_t version;
    uint32_t valid;
    uint32_t active;
    uint32_t flags;
    uint32_t reject_reason;
    uint32_t link_count;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t bias_generation;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t calibration_generation;
    uint32_t freshness_us;
    uint64_t cumulative_delay_ns;
    uint64_t ring_round_trip_ns;
    uint64_t residual_ns;
    uint32_t table_crc32;
    calibration_path_link_evidence_t links[CALIBRATION_PATH_MAX_LINKS];
} calibration_path_snapshot_t;

typedef struct {
    uint32_t expected_topology_generation;
    uint32_t expected_topology_crc32;
    uint32_t expected_bias_generation;
    uint32_t expected_profile_crc32;
    uint32_t expected_schedule_crc32;
    uint32_t calibration_generation;
    uint32_t freshness_us;
    uint32_t max_residual_ns;
    uint32_t max_jitter_ns;
    uint32_t max_asymmetry_ns;
    bool require_hardware_latch;
    bool require_repeat_statistics;
    bool require_asymmetry_bound;
    bool require_ring_round_trip;
} calibration_path_gate_t;

typedef struct {
    uint32_t expected_topology_generation;
    uint32_t expected_topology_crc32;
    uint32_t expected_bias_generation;
    uint32_t expected_profile_crc32;
    uint32_t expected_schedule_crc32;
    uint32_t calibration_generation;
    uint32_t evidence_age_us;
} calibration_path_activation_gate_t;

uint32_t calibration_path_snapshot_crc32(
    const calibration_path_snapshot_t *snapshot);
bool calibration_path_snapshot_build(
    const calibration_path_link_evidence_t *links,
    uint32_t link_count,
    uint64_t ring_round_trip_ns,
    const calibration_path_gate_t *gate,
    calibration_path_snapshot_t *snapshot);
bool calibration_path_snapshot_validate_candidate(
    const calibration_path_snapshot_t *snapshot);
bool calibration_path_snapshot_validate(
    const calibration_path_snapshot_t *snapshot);
bool calibration_path_snapshot_validate_rollbackable(
    const calibration_path_snapshot_t *snapshot);
bool calibration_path_snapshot_activate(
    const calibration_path_snapshot_t *candidate,
    const calibration_path_snapshot_t *current_active,
    const calibration_path_activation_gate_t *gate,
    calibration_path_snapshot_t *next_active,
    calibration_path_snapshot_t *rollbackable);
bool calibration_path_snapshot_rollback(
    const calibration_path_snapshot_t *current_active,
    const calibration_path_snapshot_t *rollbackable,
    const calibration_path_activation_gate_t *gate,
    calibration_path_snapshot_t *next_active,
    calibration_path_snapshot_t *next_rollbackable);

#endif
