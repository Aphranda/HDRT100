#ifndef VDC_TIMESTAMP_CLOCK_H
#define VDC_TIMESTAMP_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

bool vdc_timestamp_clock_init(void);
uint32_t vdc_timestamp_clock_tick_hz(void);
uint32_t vdc_timestamp_clock_resolution_ns(void);
uint64_t vdc_timestamp_clock_read_ticks64(void);
/* No initialization or retry. The caller binds the expected clk_sys rate;
 * both sides of a single raw high/low/high read must retain that rate and
 * TIMER1's unpaused clk_sys source. False preserves ticks. Initialization is
 * owned by Core0 before the realtime owner starts. Raw TIMER1 has its own
 * origin and is not the manager's time_us_64()-derived local nanoseconds. */
bool vdc_timestamp_clock_try_read_ticks64(uint32_t expected_hz, uint64_t *ticks);
/* Bounded configuration observation only; no timestamp or lazy init. */
bool vdc_timestamp_clock_is_current(uint32_t expected_hz);
uint64_t vdc_timestamp_clock_ticks_to_ns(uint64_t ticks);
uint64_t vdc_timestamp_clock_now_ns(void);

#endif
