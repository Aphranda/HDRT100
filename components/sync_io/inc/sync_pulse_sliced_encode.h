#ifndef SYNC_PULSE_SLICED_ENCODE_H
#define SYNC_PULSE_SLICED_ENCODE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SYNC_PULSE_SLICED_FIRST_LOW_OVERHEAD 6u
#define SYNC_PULSE_SLICED_NEXT_LOW_OVERHEAD 8u
#define SYNC_PULSE_SLICED_INSTRUCTION_WORDS 20u
#define SYNC_PULSE_SLICED_HIGH_INSTRUCTION 16u

typedef struct {
    uint32_t period_ticks, interval_ticks, high_ticks, marker_ticks;
    uint32_t subdivisions, geometry;
    uint64_t tail_ticks;
} sync_pulse_sliced_config_t;

/* This diagnostic variant admits only an exact nominal integer grid. Marker
 * deadlines remain absolute DCO plans. Quality deviation from that nominal
 * period is NOT encoding authority: phase/rate updates may move the next
 * marker. Only overlap, insufficient instruction time and overflow reject.
 * Measure per-cycle phase/spacing against the reference; no fixed physical
 * precision is implied by successful admission. */
static inline bool sync_pulse_sliced_configure(uint32_t hz, uint32_t period,
    uint32_t interval, uint32_t high, uint32_t marker, sync_pulse_sliced_config_t *out)
{
    if (!out || hz != 250000000u || !interval || period % interval ||
        high < 3u || high > 34u || marker < 2u || marker > 65u ||
        marker <= high || interval < marker + 4u || interval < high + 6u ||
        interval - marker - 4u > 255u || interval - high - 6u > 127u) return false;
    const uint32_t n = period / interval;
    if (n < 3u || n > 2050u) return false;
    *out = (sync_pulse_sliced_config_t){
        .period_ticks=period, .interval_ticks=interval, .high_ticks=high,
        .marker_ticks=marker, .subdivisions=n,
        .geometry=(n-3u) | ((marker-2u)<<11u) |
            ((interval-marker-4u)<<17u) | ((interval-high-6u)<<25u),
        .tail_ticks=(uint64_t)(n-2u)*interval+high,
    };
    return true;
}

static inline bool sync_pulse_sliced_encode(const sync_pulse_sliced_config_t *c,
    uint64_t origin, bool first, uint64_t previous_marker, uint64_t rise,
    uint64_t fall, uint32_t *word)
{
    if (!c || !word || rise < origin || fall < rise || fall-rise != c->marker_ticks ||
        rise > UINT64_MAX-c->tail_ticks) return false;
    uint64_t reference=origin;
    uint32_t overhead=SYNC_PULSE_SLICED_FIRST_LOW_OVERHEAD;
    if (!first) {
        if (previous_marker < origin || rise < previous_marker ||
            previous_marker > UINT64_MAX-c->tail_ticks) return false;
        reference=previous_marker+c->tail_ticks;
        overhead=SYNC_PULSE_SLICED_NEXT_LOW_OVERHEAD;
    }
    if (rise<reference || rise-reference<overhead || rise-reference-overhead>UINT32_MAX) return false;
    *word=(uint32_t)(rise-reference-overhead);
    return true;
}
#endif
