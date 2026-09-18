#ifndef TDMA_EVENT_ENABLE_ANCHOR_H
#define TDMA_EVENT_ENABLE_ANCHOR_H

/* Private TDMA device helper, not a cross-owner timestamp/enable interface. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include <stdbool.h>
#include <stdint.h>
#include "pico/platform.h"
#include "hardware/pio.h"
#include "hardware/structs/timer.h"
#include "vdc_timestamp_clock.h"

/* The TDMA owner has already admitted pio/sm_mask and prepared disabled SMs.
 * Preserve its one enable operation even when clock/anchor qualification
 * fails. No IRQ masking, retry, PIO program change, timer write or allocation.
 * before/after must be distinct output objects; false leaves both unchanged.
 *
 * Clock qualification runs outside the measured enclosure. Between its raw
 * endpoints only the final low read, fences and the SDK's synchronized
 * enable occur. Interrupt/bus delays remain inside the complete enclosure;
 * no midpoint or maximum-width filter substitutes for the observed bounds.
 * SRAM placement must be checked in the actual linked device image.
 *
 * The two high/low/high checks overlap around enable: H1,H3,L1,E,L2,H2,H4.
 * A low-word rollover anywhere within either triple is rejected without
 * retry, including between the low reads. Moving high reads outside the
 * measured interval tightens its bounds without inventing event precision.
 * Equal/reversed endpoints (including complete
 * 64-bit wrap) are rejected. The owner still excludes hidden clock changes,
 * timer resets and debug stops throughout the anchor lifetime. The two
 * configuration observations cannot detect change-and-restore in between.
 * The caller retains its existing pad/dirty-start, epoch and fault checks. */
static __attribute__((noinline)) bool __not_in_flash_func(tdma_event_enable_anchor_capture)(
    PIO pio, uint32_t sm_mask, uint32_t expected_hz,
    uint64_t *before, uint64_t *after)
{
    const bool clock_before = vdc_timestamp_clock_is_current(expected_hz);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const uint32_t before_hi = timer1_hw->timerawh;
    const uint32_t after_hi = timer1_hw->timerawh;
    const uint32_t before_lo = timer1_hw->timerawl;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    pio_enable_sm_mask_in_sync(pio, sm_mask);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const uint32_t after_lo = timer1_hw->timerawl;
    const uint32_t before_hi_again = timer1_hw->timerawh;
    const uint32_t after_hi_again = timer1_hw->timerawh;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const bool clock_after = vdc_timestamp_clock_is_current(expected_hz);
    const uint64_t lower = ((uint64_t)before_hi << 32u) | before_lo;
    const uint64_t upper = ((uint64_t)after_hi << 32u) | after_lo;
    if (!before || !after || before == after || !clock_before || !clock_after ||
        before_hi != before_hi_again || after_hi != after_hi_again ||
        upper <= lower) return false;
    *before = lower;
    *after = upper;
    return true;
}

#endif
#endif
