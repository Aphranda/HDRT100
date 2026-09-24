#ifndef TRIGGER_TURNTABLE_PREDICTOR_H
#define TRIGGER_TURNTABLE_PREDICTOR_H

#include <stdbool.h>
#include <stdint.h>
#include "trigger_position_descriptor.h"

typedef enum {
    TRIGGER_TURNTABLE_POSITION_START = 1u,
    TRIGGER_TURNTABLE_POSITION_ZERO = 2u,
    TRIGGER_TURNTABLE_POSITION_TERMINAL = 3u,
} trigger_turntable_position_id_t;

typedef enum {
    TRIGGER_TURNTABLE_OK = 0u,
    TRIGGER_TURNTABLE_ARGUMENT = 1u,
    TRIGGER_TURNTABLE_NOT_ACTIVE = 2u,
    TRIGGER_TURNTABLE_BACKWARD = 3u,
    TRIGGER_TURNTABLE_OVERFLOW = 4u,
} trigger_turntable_result_t;

typedef struct {
    uint32_t pulses_per_degree;
    int32_t start_angle_mdeg, zero_angle_mdeg, terminal_angle_mdeg;
    uint32_t pre_roll_mdeg, post_roll_mdeg;
    uint32_t threshold_per_mille;
} trigger_turntable_predictor_config_t;

typedef struct {
    uint32_t configured, active, count_epoch, prediction_version;
    uint64_t origin_count, last_count;
    uint64_t target_count[3], threshold_count[3];
    uint32_t announced_mask, terminal_verified;
    /* Runtime rate estimate is measured in counter pulses per VDC sync,
     * represented as unsigned Q16.16.  It is prediction input only; the
     * count itself remains the position authority. */
    uint32_t rate_valid, last_observed_sync;
    uint64_t last_observed_count, pulses_per_sync_q16;
    trigger_turntable_result_t last_error;
} trigger_turntable_predictor_snapshot_t;

static inline bool trigger_turntable_predictor_config_valid(
    const trigger_turntable_predictor_config_t *c)
{
    return c && c->pulses_per_degree && c->start_angle_mdeg <= c->zero_angle_mdeg &&
        c->zero_angle_mdeg <= c->terminal_angle_mdeg && c->pre_roll_mdeg &&
        c->post_roll_mdeg && c->threshold_per_mille > 0u &&
        c->threshold_per_mille < 1000u;
}

static inline bool trigger_turntable_predictor_configure(
    trigger_turntable_predictor_snapshot_t *s,
    const trigger_turntable_predictor_config_t *c)
{
    if (!s || !trigger_turntable_predictor_config_valid(c)) return false;
    *s = (trigger_turntable_predictor_snapshot_t){.configured=1u,
        .last_error=TRIGGER_TURNTABLE_OK};
    return true;
}

static inline bool trigger_turntable_predictor_begin(
    trigger_turntable_predictor_snapshot_t *s,
    const trigger_turntable_predictor_config_t *c, uint32_t epoch,
    uint64_t origin_count)
{
    if (!s || !trigger_turntable_predictor_config_valid(c) || !epoch) return false;
    const int64_t base_angle = (int64_t)c->start_angle_mdeg - c->pre_roll_mdeg;
    const int64_t angles[3] = {c->start_angle_mdeg, c->zero_angle_mdeg,
        c->terminal_angle_mdeg};
    const uint64_t max = UINT64_MAX;
    trigger_turntable_predictor_snapshot_t next = *s;
    next.configured = 1u; next.active = 1u; next.count_epoch = epoch;
    next.origin_count = origin_count; next.last_count = origin_count;
    next.prediction_version = s->prediction_version + 1u;
    if (!next.prediction_version) next.prediction_version = 1u;
    next.announced_mask = 0u; next.terminal_verified = 0u;
    next.rate_valid = 0u; next.last_observed_sync = 0u;
    next.last_observed_count = origin_count; next.pulses_per_sync_q16 = 0u;
    for (uint32_t i=0u; i<3u; ++i) {
        const int64_t delta_mdeg = angles[i] - base_angle;
        if (delta_mdeg < 0 || (uint64_t)delta_mdeg >
            (max - origin_count) / c->pulses_per_degree) {
            s->last_error = TRIGGER_TURNTABLE_OVERFLOW; return false;
        }
        const uint64_t product = (uint64_t)delta_mdeg * c->pulses_per_degree;
        if (product > UINT64_MAX - 999u) {
            s->last_error = TRIGGER_TURNTABLE_OVERFLOW; return false;
        }
        const uint64_t target = origin_count + (product + 999u) / 1000u;
        if (target < origin_count) {
            s->last_error = TRIGGER_TURNTABLE_OVERFLOW; return false;
        }
        const uint64_t remaining = target - origin_count;
        const uint64_t factor = 1000u - c->threshold_per_mille;
        if (remaining > (UINT64_MAX - 999u) / factor) {
            s->last_error = TRIGGER_TURNTABLE_OVERFLOW; return false;
        }
        const uint64_t lead = (remaining * factor + 999u) / 1000u;
        next.target_count[i] = target;
        next.threshold_count[i] = target - lead;
    }
    next.last_error = TRIGGER_TURNTABLE_OK; *s = next; return true;
}

static inline trigger_turntable_result_t trigger_turntable_predictor_observe(
    trigger_turntable_predictor_snapshot_t *s, const trigger_turntable_predictor_config_t *c,
    uint64_t count, uint32_t *new_position_mask)
{
    if (new_position_mask) *new_position_mask = 0u;
    if (!s || !c || !s->active) return TRIGGER_TURNTABLE_NOT_ACTIVE;
    if (count < s->last_count) {s->last_error=TRIGGER_TURNTABLE_BACKWARD;return s->last_error;}
    s->last_count = count;
    for (uint32_t i=0u;i<3u;++i) if (!(s->announced_mask&(1u<<i)) &&
        count >= s->threshold_count[i]) {
        s->announced_mask |= 1u<<i;
        if (new_position_mask) *new_position_mask |= 1u<<i;
    }
    const uint64_t post_product = (uint64_t)c->post_roll_mdeg *
        c->pulses_per_degree;
    if (post_product > UINT64_MAX - 999u ||
        s->target_count[2] > UINT64_MAX - (post_product + 999u) / 1000u) {
        s->last_error = TRIGGER_TURNTABLE_OVERFLOW;
        return s->last_error;
    }
    if (count >= s->target_count[2] + (post_product + 999u) / 1000u)
        s->terminal_verified = 1u;
    s->last_error=TRIGGER_TURNTABLE_OK; return s->last_error;
}

/* Observe one counter sample together with the VDC sync sequence.  The
 * threshold/position decision remains count based; this bounded estimate only
 * predicts how many future 1 kHz slices remain before the target count. */
static inline trigger_turntable_result_t
trigger_turntable_predictor_observe_at_sync(
    trigger_turntable_predictor_snapshot_t *s,
    const trigger_turntable_predictor_config_t *c,
    uint32_t sync_sequence, uint64_t count, uint32_t *new_position_mask)
{
    const trigger_turntable_result_t result =
        trigger_turntable_predictor_observe(s, c, count, new_position_mask);
    if (result != TRIGGER_TURNTABLE_OK || !sync_sequence ||
        sync_sequence <= s->last_observed_sync ||
        count < s->last_observed_count) {
        if (result == TRIGGER_TURNTABLE_OK && sync_sequence &&
            sync_sequence > s->last_observed_sync) {
            s->last_observed_sync = sync_sequence;
            s->last_observed_count = count;
        }
        return result;
    }
    const uint32_t sync_delta = sync_sequence - s->last_observed_sync;
    const uint64_t count_delta = count - s->last_observed_count;
    s->last_observed_sync = sync_sequence;
    s->last_observed_count = count;
    if (count_delta && sync_delta &&
        count_delta <= (UINT64_MAX >> 16u)) {
        const uint64_t measured = (count_delta << 16u) / sync_delta;
        if (measured) {
            /* A single-sample estimate is deterministic and avoids adding an
             * unbounded filter to the realtime path. */
            s->pulses_per_sync_q16 = measured;
            s->rate_valid = 1u;
        }
    }
    return result;
}

static inline bool trigger_turntable_predictor_build_descriptor(
    const trigger_turntable_predictor_snapshot_t *s,
    uint32_t run_id, uint32_t sync_epoch, uint32_t position_id,
    uint64_t target_ms_index, uint32_t target_fine_slice_index,
    uint32_t grid_generation, uint32_t deployment_generation,
    uint32_t deployment_crc, uint32_t valid_from_sync, uint32_t expiry_sync,
    trigger_position_descriptor_t *out)
{
    if (!s || !out || position_id < TRIGGER_TURNTABLE_POSITION_START ||
        position_id > TRIGGER_TURNTABLE_POSITION_TERMINAL || !s->active ||
        !(s->announced_mask & (1u << (position_id - 1u))) ||
        !run_id || !sync_epoch || !grid_generation ||
        !deployment_generation || !deployment_crc ||
        valid_from_sync > expiry_sync) return false;
    *out = (trigger_position_descriptor_t){
        .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
        .run_id = run_id, .sync_epoch = sync_epoch,
        .position_id = position_id, .count_epoch = s->count_epoch,
        .target_count = s->target_count[position_id - 1u],
        .prediction_version = s->prediction_version,
        .target_ms_index = target_ms_index,
        .target_fine_slice_index = target_fine_slice_index,
        .grid_generation = grid_generation,
        .deployment_generation = deployment_generation,
        .deployment_crc = deployment_crc,
        .valid_from_sync = valid_from_sync, .expiry_sync = expiry_sync,
    };
    return true;
}

#endif
