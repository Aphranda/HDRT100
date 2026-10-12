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

/* Rolling prediction window (2026-10-12 decisions, see
 * docs/trigger/t2/TRIGGER_T2_TODO.md): T2 predicts only the NEXT ONE OR TWO
 * positions. A 360-degree/1-degree scan is 361 positions, none of them may be
 * predicted while the scan has barely started, and a prediction more than two
 * positions ahead is not accurate - the rate estimate is a single sample
 * (pulses_per_sync_q16), so its error grows with the horizon. */
#define TRIGGER_TURNTABLE_WINDOW_MAX 2u

typedef struct {
    uint32_t valid;            /* target/threshold computed at window entry */
    uint32_t absolute_index;   /* 0-based position ordinal inside the scan */
    int32_t angle_mdeg;        /* absolute angle of this position */
    uint64_t target_count;
    uint64_t threshold_count;
    uint32_t announced;        /* threshold crossed -> FREEZE for this position */
} trigger_turntable_window_entry_t;

typedef enum {
    TRIGGER_TURNTABLE_SCAN_OK = 0u,
    TRIGGER_TURNTABLE_SCAN_ARGUMENT = 1u,          /* missing/zero parameter */
    TRIGGER_TURNTABLE_SCAN_BACKWARD = 2u,          /* terminal < start */
    TRIGGER_TURNTABLE_SCAN_STEP_NOT_DIVISIBLE = 3u,/* step does not fit the span */
    TRIGGER_TURNTABLE_SCAN_TOO_MANY_POSITIONS = 4u,
    TRIGGER_TURNTABLE_SCAN_PULSES_NOT_INTEGER = 5u,/* step is not whole pulses */
    TRIGGER_TURNTABLE_SCAN_SPAN_TOO_SHORT = 6u,    /* step cannot hold the groups */
    TRIGGER_TURNTABLE_SCAN_ROLL_TOO_SHORT = 7u,    /* pre/post roll < one STEP */
} trigger_turntable_scan_result_t;

/* A 361-position scan is the product target; the limit only exists so a typo
 * cannot ask the runtime to walk an unbounded grid. */
#define TRIGGER_TURNTABLE_SCAN_POSITION_LIMIT 4096u

typedef struct {
    uint32_t positions;        /* inclusive: floor((terminal-start)/step) + 1 */
    uint64_t step_pulses;      /* whole counter pulses per STEP (integer) */
    uint64_t total_pulses;     /* pulses from start to terminal */
    /* The turntable must already be moving before the first position (otherwise
     * position 0 cannot be predicted at all) and must keep moving past the
     * terminal (otherwise the last position cannot be verified). Both rolls are
     * part of the motion, not part of the position grid. */
    uint64_t pre_roll_pulses, post_roll_pulses, motion_pulses;
    /* The travel the machine will actually perform: start-pre_roll .. terminal+post_roll.
     * Read back so an operator can confirm the -5/+5 degree run-up and run-out rather
     * than trusting the validation code. */
    int32_t motion_start_mdeg, motion_stop_mdeg;
} trigger_turntable_scan_geometry_t;

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
    /* Rolling window state (appended: the legacy fields above keep their layout).
     * window[0] is the oldest outstanding position; window_next_index is the
     * ordinal of the LAST position entered, so at most
     * window_next_index + TRIGGER_TURNTABLE_WINDOW_MAX have ever been computed. */
    uint32_t window_next_index, window_entered_count, window_recomputed_count;
    /* Announced positions for which no usable rate existed, so their time could
     * not be predicted: the FREEZE still goes out (the count is the authority),
     * but the loss of the time prediction is counted instead of being silent. */
    uint32_t window_rate_stale_count;
    /* Entries whose target count had already passed when they were entered (the
     * scan is behind the prediction): due immediately, and counted so the loss is
     * visible instead of being hidden by a wrapped threshold. */
    uint32_t window_late_entry_count;
    /* The state's OWN announcement total, so the exactly-once invariant (one FREEZE
     * per entered position) is a fact of the state machine and not of whoever counts
     * the returned mask. window_overwrote_unannounced_count names the only way an
     * entry can leave the window without a FREEZE: being replaced in its slot. */
    uint32_t window_announced_count;
    uint32_t window_overwrote_unannounced_count;
    /* Scan step in millidegrees. Kept here, not in the config, because a new
     * config field breaks every partial initializer (-Werror -Wextra). */
    int32_t window_step_mdeg;
    /* Derived scan geometry and the result of the last validation, so the product
     * path can be read back instead of being re-derived by every reader. */
    trigger_turntable_scan_geometry_t scan_geometry;
    uint32_t scan_result;
    trigger_turntable_window_entry_t window[TRIGGER_TURNTABLE_WINDOW_MAX];
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
    uint64_t origin_count, int32_t step_mdeg)
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
        /* Lead basis (TRIGGER_T2_TODO.md A.2): with a scan step the lead is a fraction of
         * ONE STEP, so every position is frozen equally early; the distance-from-origin
         * rule inflates the lead for later positions (and is what the window no longer
         * uses). step_mdeg == 0 keeps the legacy basis for callers without a scan. */
        const uint64_t step_product =
            step_mdeg > 0 ? (uint64_t)step_mdeg * c->pulses_per_degree : 0u;
        const uint64_t lead = (step_product && step_product % 1000u == 0u &&
                               step_product / 1000u <= (UINT64_MAX - 999u) / factor)
            ? ((step_product / 1000u) * factor + 999u) / 1000u
            : (remaining * factor + 999u) / 1000u;
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
        .stage = TRIGGER_POSITION_STAGE_PREVIEW,
    };
    return true;
}
/* Window-aware descriptor builder (migration step 2, TRIGGER_T2_TODO.md A.3.15c/A.3.15d):
 * the legacy builder above validates the three named positions and reads
 * target_count[position_id - 1u], so a scan of arbitrary length cannot use it. Every field
 * is copied from that shape; exactly two things differ - the target count comes from the
 * rolling-window entry (computed when the position ENTERED the window, never from a
 * begin-time origin extrapolated over the whole scan) and the precondition is the entry's
 * own validity instead of the START..TERMINAL range. Whether the position must already be
 * announced is left to the caller. */
static inline bool trigger_turntable_predictor_build_descriptor_window(
    const trigger_turntable_predictor_snapshot_t *s,
    const trigger_turntable_window_entry_t *entry,
    uint32_t run_id, uint32_t sync_epoch, uint32_t position_id,
    uint64_t target_ms_index, uint32_t target_fine_slice_index,
    uint32_t grid_generation, uint32_t deployment_generation,
    uint32_t deployment_crc, uint32_t valid_from_sync, uint32_t expiry_sync,
    trigger_position_descriptor_t *out)
{
    if (!s || !entry || !out || !entry->valid || !s->active ||
        !run_id || !sync_epoch || !grid_generation ||
        !deployment_generation || !deployment_crc ||
        valid_from_sync > expiry_sync) return false;
    *out = (trigger_position_descriptor_t){
        .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
        .run_id = run_id, .sync_epoch = sync_epoch,
        .position_id = position_id, .count_epoch = s->count_epoch,
        .target_count = entry->target_count,
        .prediction_version = s->prediction_version,
        .target_ms_index = target_ms_index,
        .target_fine_slice_index = target_fine_slice_index,
        .grid_generation = grid_generation,
        .deployment_generation = deployment_generation,
        .deployment_crc = deployment_crc,
        .valid_from_sync = valid_from_sync, .expiry_sync = expiry_sync,
        .stage = TRIGGER_POSITION_STAGE_PREVIEW,
    };
    return true;
}

/* Rolling prediction window: the legacy arrays (target_count[3]/
 * threshold_count[3], announced_mask) stay for the START/ZERO/TERMINAL bench path
 * and for the existing consumers; the window is the scan-capable path:
 *   - begin_window() enters at most TRIGGER_TURNTABLE_WINDOW_MAX positions,
 *   - each entry's target/threshold is computed when the position ENTERS the
 *     window, from the count observed at that moment (never from a begin-time
 *     origin extrapolated over the whole scan),
 *   - no fact is produced for a position outside the window,
 *   - a time prediction requires a usable rate: without one the entry reports
 *     "time not ready" instead of extrapolating from a stale estimate.
 */
static inline int32_t trigger_turntable_window_position_angle_mdeg(
    const trigger_turntable_predictor_config_t *c, int32_t step_mdeg,
    uint32_t absolute_index)
{
    return c->start_angle_mdeg + (int32_t)absolute_index * step_mdeg;
}

static inline uint32_t trigger_turntable_window_position_count(
    const trigger_turntable_predictor_config_t *c, int32_t step_mdeg)
{
    if (!c || step_mdeg <= 0 || c->terminal_angle_mdeg < c->start_angle_mdeg)
        return 0u;
    return (uint32_t)((c->terminal_angle_mdeg - c->start_angle_mdeg) /
                      step_mdeg) + 1u;
}

/* Set the scan step before beginning a window; 0 disables the window path. */
static inline void trigger_turntable_window_configure(
    trigger_turntable_predictor_snapshot_t *s, int32_t step_mdeg)
{
    if (s) s->window_step_mdeg = step_mdeg > 0 ? step_mdeg : 0;
}

/* Compute one window entry from the count observed NOW. Refuses (no fact) when
 * the ordinal is past the scan end, so the window can never walk into a position
 * that does not exist. */
static inline bool trigger_turntable_window_enter(
    trigger_turntable_predictor_snapshot_t *s,
    const trigger_turntable_predictor_config_t *c,
    uint32_t slot, uint32_t absolute_index, uint64_t observed_count)
{
    if (!s || !c || slot >= TRIGGER_TURNTABLE_WINDOW_MAX || !s->window_step_mdeg)
        return false;
    const uint32_t positions =
        trigger_turntable_window_position_count(c, s->window_step_mdeg);
    if (s->window[slot].valid && !s->window[slot].announced)
        s->window_overwrote_unannounced_count++;
    if (absolute_index >= positions) {
        s->window[slot] = (trigger_turntable_window_entry_t){0};
        return false;
    }
    const int64_t base_angle = (int64_t)c->start_angle_mdeg - c->pre_roll_mdeg;
    const int64_t delta_mdeg =
        (int64_t)trigger_turntable_window_position_angle_mdeg(
            c, s->window_step_mdeg, absolute_index) - base_angle;
    /* The target is where the counter will be when the turntable reaches this
     * angle, measured from the SCAN ORIGIN - not from whichever count happened to
     * be current when the position entered the window. Re-basing it on the entry
     * moment would push every later position further away by the distance already
     * travelled, which is exactly the extrapolation this window exists to avoid. */
    if (delta_mdeg < 0 ||
        (uint64_t)delta_mdeg > (UINT64_MAX - s->origin_count) / c->pulses_per_degree) {
        s->last_error = TRIGGER_TURNTABLE_OVERFLOW;
        return false;
    }
    const uint64_t product = (uint64_t)delta_mdeg * c->pulses_per_degree;
    const uint64_t target = s->origin_count + (product + 999u) / 1000u;
    /* The threshold is an absolute count computed from the target. The target is
     * always at or after observed_count by construction, so the lead never
     * underflows; the guard stays because the arithmetic is unsigned. */
    /* The lead is a fraction of ONE position period (the STEP), so every position
     * in the scan is frozen equally early. Measuring it from the scan origin would
     * make later positions freeze progressively earlier. */
    const uint64_t factor = 1000u - c->threshold_per_mille;
    const uint64_t step_product =
        (uint64_t)s->window_step_mdeg * c->pulses_per_degree;
    if (step_product % 1000u != 0u) {
        s->last_error = TRIGGER_TURNTABLE_ARGUMENT;
        return false;
    }
    const uint64_t step_pulses = step_product / 1000u;
    if (!step_pulses || step_pulses > (UINT64_MAX - 999u) / factor) {
        s->last_error = TRIGGER_TURNTABLE_OVERFLOW;
        return false;
    }
    const uint64_t lead = (step_pulses * factor + 999u) / 1000u;
    const uint64_t threshold = target > lead ? target - lead : 0u;
    s->window[slot] = (trigger_turntable_window_entry_t){
        .valid = 1u,
        .absolute_index = absolute_index,
        .angle_mdeg = trigger_turntable_window_position_angle_mdeg(
            c, s->window_step_mdeg, absolute_index),
        .target_count = target,
        /* A position entered with the count already past its target is DUE NOW.
         * Clamping to the TARGET (not to the observed count) keeps the invariant
         * threshold <= target while still announcing on the next sample, so the
         * lead stays a meaningful, checkable quantity. */
        .threshold_count = observed_count > target ? target : threshold,
        .announced = 0u,
    };
    if (observed_count > target) s->window_late_entry_count++;
    return true;
}

/* Enter the first window: at most TRIGGER_TURNTABLE_WINDOW_MAX positions, and
 * nothing at all for the rest of the scan. */
static inline bool trigger_turntable_window_begin(
    trigger_turntable_predictor_snapshot_t *s,
    const trigger_turntable_predictor_config_t *c, uint64_t observed_count)
{
    if (!s || !c || !s->window_step_mdeg) return false;
    s->origin_count = observed_count;      /* absolute targets are scan based */
    s->window_late_entry_count = 0u;
    s->window_announced_count = 0u;
    s->window_overwrote_unannounced_count = 0u;
    s->window_next_index = 0u;
    s->window_entered_count = 0u;
    s->window_rate_stale_count = 0u;
    s->window_recomputed_count = 0u;
    for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot) {
        if (!trigger_turntable_window_enter(s, c, slot, slot, observed_count))
            break;
        s->window_entered_count++;
    }
    s->window_next_index = s->window_entered_count;
    return s->window_entered_count != 0u;
}

/* Defined after window_advance, which counts the announced positions that had no
 * usable rate. */
static inline bool trigger_turntable_window_time_ready(
    const trigger_turntable_predictor_snapshot_t *s);

/* Observe one count sample: announce the window entries whose threshold was
 * crossed, and roll the window forward by recomputing the entries for the newly
 * entered positions from THIS count. At most one position is added per advance,
 * so the window never grows past the accuracy bound. */
static inline trigger_turntable_result_t trigger_turntable_window_advance(
    trigger_turntable_predictor_snapshot_t *s,
    const trigger_turntable_predictor_config_t *c, uint64_t count,
    uint32_t *new_position_mask)
{
    if (new_position_mask) *new_position_mask = 0u;
    if (!s || !c || !s->active || !s->window_step_mdeg)
        return TRIGGER_TURNTABLE_NOT_ACTIVE;
    if (count < s->last_count) {
        s->last_error = TRIGGER_TURNTABLE_BACKWARD;
        return s->last_error;
    }
    s->last_count = count;
    uint32_t announced_now = 0u;
    for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot) {
        trigger_turntable_window_entry_t *entry = &s->window[slot];
        if (!entry->valid || entry->announced) continue;
        if (count >= entry->threshold_count) {
            entry->announced = 1u;
            s->window_announced_count++;
            announced_now |= 1u << slot;
        }
    }
    if (new_position_mask) *new_position_mask = announced_now;
    if (announced_now && !trigger_turntable_window_time_ready(s))
        s->window_rate_stale_count++;
    /* Roll only when the oldest entry has been announced: the next position is
     * entered now, with the count as it is now. */
    while (s->window[0].valid && s->window[0].announced) {
        for (uint32_t slot = 0u; slot + 1u < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot)
            s->window[slot] = s->window[slot + 1u];
        s->window[TRIGGER_TURNTABLE_WINDOW_MAX - 1u] =
            (trigger_turntable_window_entry_t){0};
        /* The next ordinal to enter is window_next_index: entering it NOW, with
         * the count as it is now, is what keeps the horizon at two positions. */
        if (!trigger_turntable_window_enter(s, c, TRIGGER_TURNTABLE_WINDOW_MAX - 1u,
                                            s->window_next_index, count)) {
            /* Past the scan end (or not computable): the window simply has fewer
             * valid entries - never a fabricated one. */
            break;
        }
        s->window_next_index++;
        s->window_entered_count++;
        s->window_recomputed_count++;
    }
    s->last_error = TRIGGER_TURNTABLE_OK;
    return s->last_error;
}

/* The entry for one position ordinal, or NULL when that ordinal is not in the
 * window. Consumers work in ordinals (the scan's own numbering) and must not have
 * to know which slot an entry currently occupies, so the slot layout stays private
 * to this window and the migration off the legacy per-position arrays has one
 * lookup instead of an index calculation per caller. */
static inline const trigger_turntable_window_entry_t *trigger_turntable_window_find(
    const trigger_turntable_predictor_snapshot_t *s, uint32_t absolute_index)
{
    if (!s) return NULL;
    for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot)
        if (s->window[slot].valid && s->window[slot].absolute_index == absolute_index)
            return &s->window[slot];
    return NULL;
}

/* The time prediction for a window entry needs a usable rate. Without one the
 * caller must treat the position's time as unknown instead of extrapolating:
 * beyond two positions the single-sample estimate is not accurate anyway. */
static inline bool trigger_turntable_window_time_ready(
    const trigger_turntable_predictor_snapshot_t *s)
{
    return s && s->rate_valid != 0u && s->pulses_per_sync_q16 != 0u;
}

/* Scan geometry validation and derivation (STOP time, fail-closed).
 *
 * The scan is start..terminal by step (millidegrees); a 360-degree scan at 1
 * degree is 361 positions. Everything the runtime needs - the position count it
 * must stay inside, the pulses per position and the span the per-position
 * sequences need - is derived here so the SCPI front end only has to copy the
 * numbers in, and an unusable scan is refused instead of being run. */


/* sequence_span_pulses is the counter span one position's group sequence needs
 * (16 groups as configured for the product); a STEP that cannot hold it would
 * make the groups overlap the next position, so it is refused. Pass 0 when the
 * caller has nothing to check yet. */
static inline trigger_turntable_scan_result_t trigger_turntable_scan_validate(
    const trigger_turntable_predictor_config_t *c, int32_t step_mdeg,
    uint64_t sequence_span_pulses, trigger_turntable_scan_geometry_t *out)
{
    if (out) *out = (trigger_turntable_scan_geometry_t){0};
    if (!c || !c->pulses_per_degree || step_mdeg <= 0)
        return TRIGGER_TURNTABLE_SCAN_ARGUMENT;
    /* Both rolls are required: -5 degrees of run-up to predict the first position
     * and +5 degrees of run-out to verify the terminal one. */
    if (!c->pre_roll_mdeg || !c->post_roll_mdeg)
        return TRIGGER_TURNTABLE_SCAN_ARGUMENT;
    if (c->terminal_angle_mdeg < c->start_angle_mdeg)
        return TRIGGER_TURNTABLE_SCAN_BACKWARD;
    const int64_t span_mdeg =
        (int64_t)c->terminal_angle_mdeg - c->start_angle_mdeg;
    if (span_mdeg % step_mdeg != 0)
        return TRIGGER_TURNTABLE_SCAN_STEP_NOT_DIVISIBLE;
    const uint64_t positions = (uint64_t)(span_mdeg / step_mdeg) + 1u;
    if (positions > TRIGGER_TURNTABLE_SCAN_POSITION_LIMIT)
        return TRIGGER_TURNTABLE_SCAN_TOO_MANY_POSITIONS;
    /* A STEP must be a whole number of counter pulses: a fractional one drifts
     * the whole grid away from the angle it claims to be at. */
    const uint64_t step_product = (uint64_t)step_mdeg * c->pulses_per_degree;
    if (step_product % 1000u != 0u)
        return TRIGGER_TURNTABLE_SCAN_PULSES_NOT_INTEGER;
    const uint64_t step_pulses = step_product / 1000u;
    if (!step_pulses || (sequence_span_pulses && step_pulses < sequence_span_pulses))
        return TRIGGER_TURNTABLE_SCAN_SPAN_TOO_SHORT;
    /* A roll must also be a whole, non-zero number of pulses. */
    const uint64_t pre_product = (uint64_t)c->pre_roll_mdeg * c->pulses_per_degree;
    const uint64_t post_product = (uint64_t)c->post_roll_mdeg * c->pulses_per_degree;
    if (pre_product % 1000u || post_product % 1000u)
        return TRIGGER_TURNTABLE_SCAN_PULSES_NOT_INTEGER;
    const uint64_t pre_pulses = pre_product / 1000u;
    const uint64_t post_pulses = post_product / 1000u;
    /* At least one STEP of run-up is what makes the first position predictable;
     * at least one STEP of run-out is what makes the terminal verifiable. */
    if (pre_pulses < step_pulses || post_pulses < step_pulses)
        return TRIGGER_TURNTABLE_SCAN_ROLL_TOO_SHORT;
    const uint64_t span_product = (uint64_t)span_mdeg * c->pulses_per_degree;
    if (span_product > UINT64_MAX - 999u)
        return TRIGGER_TURNTABLE_SCAN_TOO_MANY_POSITIONS;
    if (out) {
        out->positions = (uint32_t)positions;
        out->step_pulses = step_pulses;
        out->total_pulses = (span_product + 999u) / 1000u;
        out->pre_roll_pulses = pre_pulses;
        out->post_roll_pulses = post_pulses;
        out->motion_pulses = pre_pulses + out->total_pulses + post_pulses;
        out->motion_start_mdeg = c->start_angle_mdeg - c->pre_roll_mdeg;
        out->motion_stop_mdeg = c->terminal_angle_mdeg + c->post_roll_mdeg;
    }
    return TRIGGER_TURNTABLE_SCAN_OK;
}


/* Single product entry point: validate the scan (fail-closed), keep the derived
 * geometry for readback, then enter the first window. An unusable scan leaves no
 * window at all, so it can never start. */
static inline trigger_turntable_scan_result_t trigger_turntable_window_begin_scan(
    trigger_turntable_predictor_snapshot_t *s,
    const trigger_turntable_predictor_config_t *c, int32_t step_mdeg,
    uint64_t sequence_span_pulses, uint64_t observed_count)
{
    if (!s) return TRIGGER_TURNTABLE_SCAN_ARGUMENT;
    const trigger_turntable_scan_result_t result = trigger_turntable_scan_validate(
        c, step_mdeg, sequence_span_pulses, &s->scan_geometry);
    s->scan_result = (uint32_t)result;
    if (result != TRIGGER_TURNTABLE_SCAN_OK) {
        trigger_turntable_window_configure(s, 0);
        s->window_entered_count = 0u;
        s->window_next_index = 0u;
        /* A refused scan must leave NO window facts at all, including the totals a
         * consumer reads back: keeping the previous scan's announcement counters
         * would report a scan that never started as if it had frozen 361 positions. */
        s->window_recomputed_count = 0u;
        s->window_late_entry_count = 0u;
        s->window_announced_count = 0u;
        s->window_overwrote_unannounced_count = 0u;
        for (uint32_t slot = 0u; slot < TRIGGER_TURNTABLE_WINDOW_MAX; ++slot)
            s->window[slot] = (trigger_turntable_window_entry_t){0};
        return result;
    }
    trigger_turntable_window_configure(s, step_mdeg);
    if (!trigger_turntable_window_begin(s, c, observed_count)) {
        s->scan_result = (uint32_t)TRIGGER_TURNTABLE_SCAN_ARGUMENT;
        return TRIGGER_TURNTABLE_SCAN_ARGUMENT;
    }
    return TRIGGER_TURNTABLE_SCAN_OK;
}

#endif
