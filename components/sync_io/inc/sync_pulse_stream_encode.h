#ifndef SYNC_PULSE_STREAM_ENCODE_H
#define SYNC_PULSE_STREAM_ENCODE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

/* Cycle offsets measured from the execution tick of the previous falling
 * SET, or from the first instruction tick for a new run. No ns conversion,
 * divider rounding, CPU enable-timestamp estimate or propagation offset. */
#define SYNC_PULSE_STREAM_FIRST_LOW_OVERHEAD 5u
#define SYNC_PULSE_STREAM_NEXT_LOW_OVERHEAD 6u
#define SYNC_PULSE_STREAM_HIGH_OVERHEAD 2u

typedef struct {
    uint32_t high_count; /* FIFO word zero: loaded into Y while low. */
    uint32_t low_count;  /* FIFO word one: loaded into X while low. */
} sync_pulse_stream_words_t;
_Static_assert(sizeof(sync_pulse_stream_words_t) == 8u, "exactly two DMA words");

/* Pure proposal. First call uses origin_tick; later calls use the last
 * irrevocably committed falling tick. All coordinates are one monotone u64
 * PIO-divider-one tick timeline, with no wrap acceptance. Only the owner may
 * advance last_committed_fall after the entire finite DMA pair/block becomes
 * immutable and admitted. Model changes affect only an unsubmitted tail.
 * Failure leaves *words byte-identical; no state or hardware is changed.
 * No starvation may occur before either pull if absolute ticks are promised.
 * On starvation, both pulls stay low, but deadlines shift: never silently
 * resume stale pairs. Abort DMA, force low, flush, use a NEW run origin. */
static inline bool sync_pulse_stream_encode(uint64_t origin_tick,
        bool first, uint64_t last_committed_fall,
        uint64_t rising_tick, uint64_t falling_tick,
        sync_pulse_stream_words_t *words)
{
    if (words == NULL || (!first && last_committed_fall < origin_tick)) return false;
    const uint64_t reference = first ? origin_tick : last_committed_fall;
    const uint32_t overhead = first ? SYNC_PULSE_STREAM_FIRST_LOW_OVERHEAD :
                                     SYNC_PULSE_STREAM_NEXT_LOW_OVERHEAD;
    if (rising_tick < reference || falling_tick < rising_tick) return false;
    const uint64_t low = rising_tick - reference;
    const uint64_t high = falling_tick - rising_tick;
    if (low < overhead || low - overhead > UINT32_MAX ||
        high < SYNC_PULSE_STREAM_HIGH_OVERHEAD ||
        high - SYNC_PULSE_STREAM_HIGH_OVERHEAD > UINT32_MAX) return false;
    const sync_pulse_stream_words_t result = {
        .high_count = (uint32_t)(high - SYNC_PULSE_STREAM_HIGH_OVERHEAD),
        .low_count = (uint32_t)(low - overhead),
    };
    *words = result;
    return true;
}
#endif
