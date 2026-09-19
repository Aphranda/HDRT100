#ifndef VDC_TIMESTAMP_CLOCK_H
#define VDC_TIMESTAMP_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

bool vdc_timestamp_clock_init(void);
uint32_t vdc_timestamp_clock_tick_hz(void);
uint32_t vdc_timestamp_clock_resolution_ns(void);
uint64_t vdc_timestamp_clock_read_ticks64(void);
/* Core0 initializes before the realtime owner starts. No initialization,
 * fallback or retry: one high/low/high read must retain the expected clk_sys
 * rate and TIMER1's unpaused clk_sys source. Failure preserves ticks. The
 * owner excludes timer resets and clock changes throughout each run. */
bool vdc_timestamp_clock_try_read_ticks64(uint32_t expected_hz, uint64_t *ticks);
/* Bounded configuration observation only; no timestamp or lazy init. */
bool vdc_timestamp_clock_is_current(uint32_t expected_hz);
uint64_t vdc_timestamp_clock_ticks_to_ns(uint64_t ticks);
uint64_t vdc_timestamp_clock_now_ns(void);

#endif
