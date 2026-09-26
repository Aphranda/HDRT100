#ifndef TRIGGER_TIME_GRID_H
#define TRIGGER_TIME_GRID_H

#include "vdc_domain.h"
#include "vdc_timer1_coordinate.h"

/* Shared experiment identity and absolute VDC grid; neither identity value
 * grants clock quality, execution authority, or a local model lifetime. */
typedef struct {
    uint64_t anchor_vdc_ns;
    uint32_t epoch, generation, period_ns, subdivisions;
} trigger_time_grid_t;

typedef enum {
    TRIGGER_TIME_GRID_OK = 0,
    TRIGGER_TIME_GRID_ARGUMENT,
    TRIGGER_TIME_GRID_RESERVED,
    TRIGGER_TIME_GRID_OVERFLOW,
    TRIGGER_TIME_GRID_MAPPING,
    TRIGGER_TIME_GRID_LATE,
    TRIGGER_TIME_GRID_HORIZON,
    TRIGGER_TIME_GRID_RESOLUTION,
} trigger_time_grid_result_t;

typedef struct {
    uint64_t ordinal, target_vdc_ns, model_local_ns;
    uint64_t deadline_ticks, projected_vdc_ns;
} trigger_time_grid_plan_t;

static inline bool trigger_time_grid_valid(const trigger_time_grid_t *grid)
{
    return grid && grid->epoch && grid->generation && grid->subdivisions >= 2u &&
        grid->period_ns >= grid->subdivisions;
}

/* Attribute one hardware sync event to its nearest shared coarse boundary.
 * Half a fine slice is the largest unambiguous attribution error. */
static inline bool trigger_time_grid_sync_index(
    const trigger_time_grid_t *grid, uint64_t event_vdc_ns, uint64_t *index)
{
    if (!index || !trigger_time_grid_valid(grid)) return false;
    const uint64_t tolerance =
        (grid->period_ns / grid->subdivisions) / 2u;
    if (event_vdc_ns < grid->anchor_vdc_ns) {
        if (grid->anchor_vdc_ns - event_vdc_ns > tolerance) return false;
        *index = 0u;
        return true;
    }
    const uint64_t delta = event_vdc_ns - grid->anchor_vdc_ns;
    uint64_t nearest = delta / grid->period_ns;
    const uint64_t remainder = delta % grid->period_ns;
    const bool round_up = remainder > grid->period_ns / 2u;
    const uint64_t error = round_up ? grid->period_ns - remainder : remainder;
    if (error > tolerance || (round_up && nearest == UINT64_MAX)) return false;
    *index = nearest + (round_up ? 1u : 0u);
    return true;
}

/* Arithmetic only: the reserved position is still a real mathematical grid
 * boundary, needed to prove resolution of its immediate predecessor. */
static inline trigger_time_grid_result_t trigger_time_grid_boundary(
    const trigger_time_grid_t *grid, uint64_t q, uint64_t *out)
{
    if (!out || !trigger_time_grid_valid(grid)) return TRIGGER_TIME_GRID_ARGUMENT;
    const uint64_t cycle = q / grid->subdivisions;
    const uint64_t numerator = (q % grid->subdivisions) * grid->period_ns;
    const uint64_t offset = numerator / grid->subdivisions +
        (numerator % grid->subdivisions != 0u);
    if (cycle > (UINT64_MAX - grid->anchor_vdc_ns) / grid->period_ns) {
        return TRIGGER_TIME_GRID_OVERFLOW;
    }
    const uint64_t base = grid->anchor_vdc_ns + cycle * grid->period_ns;
    if (offset > UINT64_MAX - base) return TRIGGER_TIME_GRID_OVERFLOW;
    *out = base + offset;
    return TRIGGER_TIME_GRID_OK;
}

static inline trigger_time_grid_result_t trigger_time_grid_target(
    const trigger_time_grid_t *grid, uint64_t q, uint64_t *out)
{
    if (!out || !trigger_time_grid_valid(grid)) return TRIGGER_TIME_GRID_ARGUMENT;
    if (q % grid->subdivisions == grid->subdivisions - 1u) {
        return TRIGGER_TIME_GRID_RESERVED;
    }
    return trigger_time_grid_boundary(grid, q, out);
}

/* Pure planning of exactly q, never a search for a later executable ordinal.
 * All objects must be distinct. Rejection preserves *out byte-for-byte.
 * The owner separately freezes/rechecks local identities and evidence. */
static inline trigger_time_grid_result_t trigger_time_grid_prepare(
    const trigger_time_grid_t *grid, uint64_t q, const vdc_dco_control_t *model,
    uint32_t hz, uint64_t now_ticks, uint64_t minimum_lead_ticks,
    uint64_t maximum_ahead_ticks, trigger_time_grid_plan_t *out)
{
    if (!out || !model || !hz || hz > 500000000u || !minimum_lead_ticks ||
        minimum_lead_ticks > maximum_ahead_ticks) return TRIGGER_TIME_GRID_ARGUMENT;
    trigger_time_grid_plan_t plan = {.ordinal = q};
    trigger_time_grid_result_t result = trigger_time_grid_target(grid, q, &plan.target_vdc_ns);
    if (result != TRIGGER_TIME_GRID_OK) return result;
    /* A plan must also prove the following boundary is distinguishable. */
    if (q == UINT64_MAX) return TRIGGER_TIME_GRID_OVERFLOW;
    /* The Domain inverse lower-bounds at the model origin. An earlier target
     * must not become an apparently valid exact-q plan at that origin. A
     * negative, unrepresentable origin can still map later positive targets. */
    uint64_t origin_vdc_ns;
    if (vdc_domain_dco_local_to_output_ns(model, model->base_local_tick64,
            &origin_vdc_ns) && plan.target_vdc_ns < origin_vdc_ns) {
        return TRIGGER_TIME_GRID_MAPPING;
    }
    if (!vdc_domain_dco_output_to_local_ns(model, plan.target_vdc_ns, &plan.model_local_ns) ||
        !vdc_timer1_ns_to_ticks(hz, plan.model_local_ns, true, &plan.deadline_ticks)) {
        return TRIGGER_TIME_GRID_MAPPING;
    }
    if (plan.deadline_ticks < now_ticks ||
        plan.deadline_ticks - now_ticks < minimum_lead_ticks) return TRIGGER_TIME_GRID_LATE;
    if (plan.deadline_ticks - now_ticks > maximum_ahead_ticks) return TRIGGER_TIME_GRID_HORIZON;
    uint64_t physical_local_ns;
    if (!vdc_timer1_ticks_to_ns(hz, plan.deadline_ticks, false, &physical_local_ns) ||
        !vdc_domain_dco_local_to_output_ns(model, physical_local_ns, &plan.projected_vdc_ns) ||
        plan.projected_vdc_ns < plan.target_vdc_ns) return TRIGGER_TIME_GRID_MAPPING;

    /* Do not skip the reserved cell to manufacture a coarser passing result.
     * q==MAX cannot prove its next boundary, even if q itself was representable. */
    uint64_t next_target, next_local, next_deadline;
    result = trigger_time_grid_boundary(grid, q + 1u, &next_target);
    if (result != TRIGGER_TIME_GRID_OK) return result;
    if (!vdc_domain_dco_output_to_local_ns(model, next_target, &next_local) ||
        !vdc_timer1_ns_to_ticks(hz, next_local, true, &next_deadline)) {
        return TRIGGER_TIME_GRID_MAPPING;
    }
    if (next_deadline <= plan.deadline_ticks) return TRIGGER_TIME_GRID_RESOLUTION;
    *out = plan;
    return TRIGGER_TIME_GRID_OK;
}

#endif
