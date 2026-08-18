#ifndef VDC_TIMESTAMP_CLOCK_H
#define VDC_TIMESTAMP_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

bool vdc_timestamp_clock_init(void);
uint32_t vdc_timestamp_clock_tick_hz(void);
uint32_t vdc_timestamp_clock_resolution_ns(void);
uint64_t vdc_timestamp_clock_read_ticks64(void);
uint64_t vdc_timestamp_clock_ticks_to_ns(uint64_t ticks);
uint64_t vdc_timestamp_clock_now_ns(void);

#endif
