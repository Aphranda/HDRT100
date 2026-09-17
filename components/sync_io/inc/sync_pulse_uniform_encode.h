#ifndef SYNC_PULSE_UNIFORM_ENCODE_H
#define SYNC_PULSE_UNIFORM_ENCODE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

/* Coordinates are the same absolute divider-one PIO ticks as the paired
 * encoder. ISR holds a fixed high countdown for this entire finite run. */
#define SYNC_PULSE_UNIFORM_FIRST_LOW_OVERHEAD 4u
#define SYNC_PULSE_UNIFORM_NEXT_LOW_OVERHEAD 5u
#define SYNC_PULSE_UNIFORM_HIGH_OVERHEAD 2u

/* Pure proposal: rejects malformed width/deadline and preserves *low_word
 * on failure. An exhausted FIFO must retire the run, never resume this axis. */
static inline bool sync_pulse_uniform_encode(uint64_t origin_tick,
        bool first, uint64_t last_committed_fall,
        uint64_t rising_tick, uint64_t falling_tick,
        uint32_t fixed_high_ticks, uint32_t *low_word)
{
    if (!low_word || fixed_high_ticks < SYNC_PULSE_UNIFORM_HIGH_OVERHEAD ||
        (!first && last_committed_fall < origin_tick)) return false;
    const uint64_t reference = first ? origin_tick : last_committed_fall;
    const uint32_t overhead = first ? SYNC_PULSE_UNIFORM_FIRST_LOW_OVERHEAD :
                                     SYNC_PULSE_UNIFORM_NEXT_LOW_OVERHEAD;
    if (rising_tick < reference || falling_tick < rising_tick ||
        falling_tick - rising_tick != fixed_high_ticks) return false;
    const uint64_t low = rising_tick - reference;
    if (low < overhead || low - overhead > UINT32_MAX) return false;
    *low_word = (uint32_t)(low - overhead);
    return true;
}
#endif
