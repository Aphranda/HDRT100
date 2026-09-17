#ifndef VDC_OUTPUT_EDGE_PLAN_H
#define VDC_OUTPUT_EDGE_PLAN_H

#include "vdc_domain.h"

/* Pure preparation for the next NOT YET QUEUED rising edge. The owner must
 * separately bind model/session/clock, latch delay once at start, and commit
 * first_unqueued only after hardware admission. No FIFO is modified here. */
typedef struct {
    uint64_t ordinal;
    uint64_t target_vdc_ns;
    uint64_t model_local_ns;
    uint64_t physical_local_ns;
} vdc_output_edge_plan_t;

/* The common grid is anchor_vdc_ns + ordinal * period_ns. Select a grid
 * point strictly beyond F(max(base_local, not_before_local - delay)) and
 * at least first_unqueued. Skipped ordinals are never replayed as a burst.
 * Positive delay is added ONCE on the local physical axis after inversion;
 * MATCH's link propagation delay is not an input to this planner.
 * Success is a mathematical plan, not raw-TIMER1 or GPIO authorization.
 * All arithmetic is bounded and integer. False preserves *out byte-for-byte.
 * Model, arguments and out must refer to distinct objects. */
static inline bool vdc_output_edge_plan(const vdc_dco_control_t *model,
    uint64_t anchor_vdc_ns, uint32_t period_ns, uint64_t first_unqueued,
    uint64_t not_before_local_ns, int32_t output_delay_ns,
    vdc_output_edge_plan_t *out)
{
    if (!model || !out || !period_ns || model->period_adjust_ppb <= -1000000000)
        return false;
    uint64_t lower = not_before_local_ns;
    if (output_delay_ns >= 0) {
        const uint32_t delay = (uint32_t)output_delay_ns;
        lower = lower >= delay ? lower - delay : 0u;
    } else {
        const uint32_t advance = (uint32_t)(-(int64_t)output_delay_ns);
        if (lower > UINT64_MAX - advance) return false;
        lower += advance;
    }
    if (lower < model->base_local_tick64) lower = model->base_local_tick64;
    uint64_t minimum_vdc;
    if (!vdc_domain_dco_local_to_output_ns(model, lower, &minimum_vdc) ||
        minimum_vdc == UINT64_MAX) return false;
    ++minimum_vdc;
    uint64_t ordinal = 0u;
    if (minimum_vdc > anchor_vdc_ns) {
        const uint64_t distance = minimum_vdc - anchor_vdc_ns;
        ordinal = distance / period_ns + (distance % period_ns != 0u);
    }
    if (ordinal < first_unqueued) ordinal = first_unqueued;
    if (ordinal > (UINT64_MAX - anchor_vdc_ns) / period_ns) return false;
    vdc_output_edge_plan_t plan = {
        .ordinal = ordinal,
        .target_vdc_ns = anchor_vdc_ns + ordinal * period_ns,
    };
    if (!vdc_domain_dco_output_to_local_ns(model, plan.target_vdc_ns,
            &plan.model_local_ns)) return false;
    if (output_delay_ns >= 0) {
        const uint32_t delay = (uint32_t)output_delay_ns;
        if (plan.model_local_ns > UINT64_MAX - delay) return false;
        plan.physical_local_ns = plan.model_local_ns + delay;
    } else {
        const uint32_t advance = (uint32_t)(-(int64_t)output_delay_ns);
        if (plan.model_local_ns < advance) return false;
        plan.physical_local_ns = plan.model_local_ns - advance;
    }
    if (plan.physical_local_ns <= not_before_local_ns) return false;
    *out = plan;
    return true;
}

#endif
