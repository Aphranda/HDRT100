#ifndef VDC_FUTURE_RAW_H
#define VDC_FUTURE_RAW_H

#include <stdbool.h>
#include <stdint.h>
#include "vdc_timestamp_clock.h"

typedef struct {
    uint64_t lo;
    uint64_t hi;
} vdc_local_raw_bracket_t;

/* Pure bridge conversion. Given one read-only bridge enclosure, return a raw
 * TIMER1 tick bracket that conservatively contains the earliest tick at which
 * a local target can be reached. TIMER0 local time at the enclosed read is
 * [bridge.local_ns, bridge.local_ns+1000), while raw read uncertainty is kept
 * as the complete [raw_before, raw_after+1) interval. The target must be at
 * least 1000 ns after the enclosed local read. The result is an enclosure,
 * not a midpoint or exact timestamp; hi is the conservative PIO deadline.
 * Requires a fixed common clock tree for the bridge lifetime. No sampling,
 * ownership, model mutation, floating point, search or dynamic allocation.
 * False leaves *out byte-for-byte unchanged. The ordinary wrapper retains
 * its 2 s horizon; the finite timeline wrapper uses 32 s, at <=500 MHz. */
static inline bool vdc_timestamp_bridge_local_to_raw_bounded(
    const vdc_timestamp_clock_bridge_t *bridge, uint64_t target_local_ns,
    uint64_t maximum_delta_ns, vdc_local_raw_bracket_t *out)
{
    if (!bridge || !out) return false;
    const uint64_t raw_before = bridge->raw_before;
    const uint64_t raw_after = bridge->raw_after;
    const uint64_t local_ns = bridge->local_ns;
    const uint32_t tick_hz = bridge->tick_hz;
    if (!maximum_delta_ns || maximum_delta_ns > UINT64_C(32000000000) ||
        !tick_hz || tick_hz > 500000000u || raw_after < raw_before ||
        raw_after == UINT64_MAX || local_ns > UINT64_MAX - 1000u ||
        target_local_ns < local_ns + 1000u ||
        target_local_ns - local_ns > maximum_delta_ns) return false;
    const uint64_t d = target_local_ns - local_ns;
    /* At most 32 s * 500 MHz = 1.6e19, below UINT64_MAX. */
    const uint64_t upper_product = d * tick_hz;
    const uint64_t upper_delta = upper_product / UINT64_C(1000000000) +
        (upper_product % UINT64_C(1000000000) != 0u);
    const uint64_t lower_delta = ((d - 1000u) * tick_hz) / UINT64_C(1000000000);
    if (raw_before > UINT64_MAX - lower_delta - 1u ||
        raw_after > UINT64_MAX - upper_delta - 1u) return false;
    vdc_local_raw_bracket_t candidate = {
        .lo = raw_before + lower_delta + 1u,
        .hi = raw_after + upper_delta + 1u,
    };
    if (candidate.hi < candidate.lo) return false;
    *out = candidate;
    return true;
}

/* Preserve the ordinary two-second bridge API and its rejection boundary. */
static inline bool vdc_timestamp_bridge_local_to_raw(
    const vdc_timestamp_clock_bridge_t *bridge, uint64_t target_local_ns,
    vdc_local_raw_bracket_t *out)
{
    return vdc_timestamp_bridge_local_to_raw_bounded(
        bridge,target_local_ns,UINT64_C(2000000000),out);
}

/* One fixed common-clock mapping for a finite RUN lease. The limit covers
 * the 20 s lease, startup lead and configurable future window; it does not
 * authorize an unbounded clock lifetime or remove reset/pause exclusion. */
static inline bool vdc_timestamp_timeline_local_to_raw(
    const vdc_timestamp_clock_bridge_t *bridge, uint64_t target_local_ns,
    vdc_local_raw_bracket_t *out)
{
    return vdc_timestamp_bridge_local_to_raw_bounded(
        bridge,target_local_ns,UINT64_C(32000000000),out);
}

/* A fresh raw observation supplies an upper local-time bound for admission.
 * The fixed bridge is a mapping anchor, never a replacement for 'now'. */
static inline bool vdc_timestamp_timeline_local_now_upper(
    const vdc_timestamp_clock_bridge_t *bridge, uint64_t raw_now,
    uint64_t *out)
{
    if (!bridge || !out || !bridge->tick_hz || bridge->tick_hz>500000000u ||
        bridge->raw_after<bridge->raw_before || raw_now<bridge->raw_after ||
        bridge->local_ns>UINT64_MAX-1000u) return false;
    const uint64_t delta=raw_now-bridge->raw_before;
    const uint64_t seconds=delta/bridge->tick_hz;
    const uint64_t fraction=delta%bridge->tick_hz;
    if (seconds>32u || (seconds==32u && fraction!=0u)) return false;
    const uint64_t product=fraction*UINT64_C(1000000000);
    const uint64_t ns=seconds*UINT64_C(1000000000)+product/bridge->tick_hz+
        (product%bridge->tick_hz!=0u);
    if (bridge->local_ns>UINT64_MAX-1000u-ns) return false;
    *out=bridge->local_ns+1000u+ns;
    return true;
}

#endif
