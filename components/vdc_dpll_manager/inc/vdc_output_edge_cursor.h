#ifndef VDC_OUTPUT_EDGE_CURSOR_H
#define VDC_OUTPUT_EDGE_CURSOR_H

#include "vdc_output_edge_plan.h"

/* A private cursor over one immutable affine model and one common grid.
 * init returns the first plan; next returns its immediate successor. The
 * owner must discard this cursor on every model/session/clock invalidation.
 * It is mathematical preparation only, never hardware admission.
 *
 * Cursor fields are private implementation state. Only a successful init
 * creates a usable cursor; zero initialization is an inactive cursor. Model,
 * cursor and plan must be distinct, nonoverlapping objects. All failures
 * preserve cursor and plan byte-for-byte. No model pointer is retained. */
typedef struct {
    uint64_t quotient;
    uint64_t step_quotient;
    uint64_t base_local_ns;
    uint64_t ordinal;
    uint64_t target_vdc_ns;
    uint64_t physical_local_ns;
    uint32_t remainder;
    uint32_t step_remainder;
    uint32_t divisor;
    uint32_t period_ns;
    uint32_t high_ns;
    int32_t delay_ns;
    bool negative_rate;
    bool initialized;
} vdc_output_edge_cursor_t;

/* Sufficient, deliberately conservative eligibility for consecutive
 * ordinals: floor(period * B / D) >= high + 1. This is the minimum local
 * spacing of either exact inverse rounding rule, so the following grid
 * target is strictly beyond F(previous physical fall - delay).
 * Ineligible callers retain the scalar planner's ordinal-skipping path.
 * Both products fit uint64_t for the full uint32_t period/high range and
 * int32_t rate range. This check contains no division. */
static inline bool vdc_output_edge_cursor_supported(
    const vdc_dco_control_t *model, uint32_t period_ns, uint32_t high_ns)
{
    if (!model || !model->valid || !model->nominal_period_ns ||
        model->lock_state > VDC_DOMAIN_LOCK_FAULT || !period_ns ||
        model->period_adjust_ppb <= -1000000000) return false;
    const uint32_t divisor = (uint32_t)(INT64_C(1000000000) +
        model->period_adjust_ppb);
    return (uint64_t)period_ns * UINT64_C(1000000000) >=
        ((uint64_t)high_ns + 1u) * divisor;
}

static inline bool vdc_output_edge_cursor_init(
    const vdc_dco_control_t *model, uint64_t anchor_vdc_ns,
    uint32_t period_ns, uint64_t first_unqueued,
    uint64_t not_before_local_ns, int32_t output_delay_ns, uint32_t high_ns,
    vdc_output_edge_cursor_t *cursor, vdc_output_edge_plan_t *first_plan)
{
    if (!cursor || !first_plan ||
        !vdc_output_edge_cursor_supported(model, period_ns, high_ns)) return false;
    vdc_output_edge_plan_t first;
    if (!vdc_output_edge_plan(model, anchor_vdc_ns, period_ns, first_unqueued,
            not_before_local_ns, output_delay_ns, &first)) return false;

    /* H = target - (base + phase) is strictly positive: the seed selected
     * target > F(lower), with lower >= base_local. A negative intercept may
     * make H require 65 bits; retain it as need + extra without adding them. */
    uint64_t need;
    uint32_t extra = 0u;
    if (model->phase_offset_ns >= 0) {
        const uint32_t phase = (uint32_t)model->phase_offset_ns;
        if (model->base_vdc_time64_ns > UINT64_MAX - phase) return false;
        const uint64_t intercept = model->base_vdc_time64_ns + phase;
        if (first.target_vdc_ns <= intercept) return false;
        need = first.target_vdc_ns - intercept;
    } else {
        const uint32_t phase = (uint32_t)(-(int64_t)model->phase_offset_ns);
        if (model->base_vdc_time64_ns >= phase) {
            const uint64_t intercept = model->base_vdc_time64_ns - phase;
            if (first.target_vdc_ns <= intercept) return false;
            need = first.target_vdc_ns - intercept;
        } else {
            need = first.target_vdc_ns;
            extra = phase - (uint32_t)model->base_vdc_time64_ns;
        }
    }
    const bool negative = model->period_adjust_ppb < 0;
    if (negative) {
        if (need) --need;
        else if (extra) --extra;
        else return false;
    }
    const uint32_t divisor = (uint32_t)(INT64_C(1000000000) +
        model->period_adjust_ppb);
    uint64_t remainder = need % divisor;
    if (extra) remainder = (remainder + extra) % divisor;
    remainder = (remainder * UINT64_C(1000000000)) % divisor;
    const uint64_t delta = first.model_local_ns - model->base_local_tick64;
    const uint32_t rounding = negative || remainder != 0u ? 1u : 0u;
    if (delta < rounding) return false;
    const uint64_t step = (uint64_t)period_ns * UINT64_C(1000000000);
    const vdc_output_edge_cursor_t candidate = {
        .quotient = delta - rounding,
        .step_quotient = step / divisor,
        .base_local_ns = model->base_local_tick64,
        .ordinal = first.ordinal,
        .target_vdc_ns = first.target_vdc_ns,
        .physical_local_ns = first.physical_local_ns,
        .remainder = (uint32_t)remainder,
        .step_remainder = (uint32_t)(step % divisor),
        .divisor = divisor,
        .period_ns = period_ns,
        .high_ns = high_ns,
        .delay_ns = output_delay_ns,
        .negative_rate = negative,
        .initialized = true,
    };
    *cursor = candidate;
    *first_plan = first;
    return true;
}

/* Let B=1e9, D=B+rate and H=target-(base+phase).
 * For rate>=0, F-C=floor(delta*D/B), whose least inverse is ceil(H*B/D).
 * For rate<0, F-C=ceil(delta*D/B), whose least inverse is
 * floor((H-1)*B/D)+1. Advancing target by period adds period*B to either
 * numerator. Quotient/remainder addition therefore gives the exact least
 * inverse with no division, forward projection, search or correction loop.
 *
 * Positive-rate quantization can overshoot target by floor((D-r)/B) when
 * r!=0, at most three ns for an int32_t rate. Check that final output for
 * overflow, just as the scalar inverse does. Negative-rate outputs hit
 * the target exactly. These facts also prove minimality at local-1. */
static inline bool vdc_output_edge_cursor_next(
    vdc_output_edge_cursor_t *cursor, vdc_output_edge_plan_t *out)
{
    if (!cursor || !out || !cursor->initialized) return false;
    vdc_output_edge_cursor_t candidate = *cursor;
    if (candidate.ordinal == UINT64_MAX ||
        candidate.target_vdc_ns > UINT64_MAX - candidate.period_ns ||
        candidate.quotient > UINT64_MAX - candidate.step_quotient) return false;
    ++candidate.ordinal;
    candidate.target_vdc_ns += candidate.period_ns;
    candidate.quotient += candidate.step_quotient;
    /* D may exceed INT32_MAX; the sum of two remainders needs 33 bits. */
    uint64_t remainder = (uint64_t)candidate.remainder + candidate.step_remainder;
    if (remainder >= candidate.divisor) {
        if (candidate.quotient == UINT64_MAX) return false;
        ++candidate.quotient;
        remainder -= candidate.divisor;
    }
    candidate.remainder = (uint32_t)remainder;
    const uint32_t rounding = candidate.negative_rate || remainder != 0u ? 1u : 0u;
    if (candidate.quotient > UINT64_MAX - rounding) return false;
    const uint64_t delta = candidate.quotient + rounding;
    if (candidate.base_local_ns > UINT64_MAX - delta) return false;
    vdc_output_edge_plan_t plan = {
        .ordinal = candidate.ordinal,
        .target_vdc_ns = candidate.target_vdc_ns,
        .model_local_ns = candidate.base_local_ns + delta,
    };
    if (!candidate.negative_rate && remainder != 0u) {
        const uint32_t excess = candidate.divisor - (uint32_t)remainder;
        const uint32_t overshoot = (excess >= 1000000000u ? 1u : 0u) +
            (excess >= 2000000000u ? 1u : 0u) + (excess >= 3000000000u ? 1u : 0u);
        if (plan.target_vdc_ns > UINT64_MAX - overshoot) return false;
    }
    if (candidate.delay_ns >= 0) {
        const uint32_t delay = (uint32_t)candidate.delay_ns;
        if (plan.model_local_ns > UINT64_MAX - delay) return false;
        plan.physical_local_ns = plan.model_local_ns + delay;
    } else {
        const uint32_t advance = (uint32_t)(-(int64_t)candidate.delay_ns);
        if (plan.model_local_ns < advance) return false;
        plan.physical_local_ns = plan.model_local_ns - advance;
    }
    if (candidate.physical_local_ns > UINT64_MAX - candidate.high_ns ||
        plan.physical_local_ns <= candidate.physical_local_ns + candidate.high_ns)
        return false;
    candidate.physical_local_ns = plan.physical_local_ns;
    *cursor = candidate;
    *out = plan;
    return true;
}

#endif
