#ifndef VDC_MODEL_PROJECTION_H
#define VDC_MODEL_PROJECTION_H

#include "vdc_domain.h"
#include "vdc_timestamp_clock.h"

/* An affine rate coordinate, deliberately independent of model base/phase
 * and of each timestamp bridge's read-placement uncertainty. Split first:
 * ticks*q can overflow even when the exact quotient fits uint64_t. */
static inline bool vdc_model_rate_coordinate_ns(int32_t rate_ppb,
    uint32_t tick_hz, uint64_t ticks, bool round_up, uint64_t *out)
{
    if (!out || !tick_hz || tick_hz > 500000000u || rate_ppb <= -1000000000)
        return false;
    const uint64_t q = (uint64_t)(INT64_C(1000000000) + rate_ppb);
    const uint64_t whole = ticks / tick_hz;
    const uint64_t remainder = (ticks % tick_hz) * q;
    if (whole > UINT64_MAX / q) return false;
    const uint64_t base = whole * q;
    const uint64_t fraction = remainder / tick_hz +
        (round_up && remainder % tick_hz != 0u ? 1u : 0u);
    if (base > UINT64_MAX - fraction) return false;
    *out = base + fraction;
    return true;
}

/* Pure Core0 preparation. The bridge observes an integer-us TIMER0 reading
 * somewhere between two raw TIMER1 readings. Preserve the full quantization
 * envelope; this is internal model evidence, not a qualified pad timestamp.
 * The caller independently establishes which committed model covered the
 * event and checks its publication/lifetime again after this calculation. */
static inline bool vdc_model_project_interval(const vdc_dco_control_t *dco,
    const vdc_timestamp_clock_bridge_t *bridge, uint64_t event_lo,
    uint64_t event_hi, uint64_t *output_lo, uint64_t *output_hi)
{
    if (!dco || !bridge || !output_lo || !output_hi || output_lo == output_hi ||
        !bridge->tick_hz || bridge->tick_hz > 500000000u ||
        bridge->raw_before > bridge->raw_after || event_lo > event_hi ||
        event_hi > bridge->raw_before || dco->period_adjust_ppb <= -1000000000 ||
        bridge->local_ns > UINT64_MAX - 999u) return false;
    const uint64_t far = bridge->raw_after - event_lo;
    const uint64_t near = bridge->raw_before - event_hi;
    if (far > (uint64_t)bridge->tick_hz * 2u) return false;
    /* At most 1e18: bounded before multiplication, with outward rounding. */
    const uint64_t far_num = far * UINT64_C(1000000000);
    const uint64_t far_ns = far_num / bridge->tick_hz +
        (far_num % bridge->tick_hz != 0u);
    const uint64_t near_ns = near * UINT64_C(1000000000) / bridge->tick_hz;
    if (bridge->local_ns < far_ns) return false;
    const uint64_t local_lo = bridge->local_ns - far_ns;
    const uint64_t local_hi = bridge->local_ns + 999u - near_ns;
    uint64_t lo, hi;
    if (!vdc_domain_dco_local_to_output_ns(dco, local_lo, &lo) ||
        !vdc_domain_dco_local_to_output_ns(dco, local_hi, &hi) || lo > hi)
        return false;
    *output_lo = lo;
    *output_hi = hi;
    return true;
}

#endif
