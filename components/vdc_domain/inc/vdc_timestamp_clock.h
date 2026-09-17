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
typedef struct {
    uint64_t raw_before;
    uint64_t local_ns;
    uint64_t raw_after;
    uint32_t tick_hz;
} vdc_timestamp_clock_bridge_t;
/* One XIP attempt, without initialization, retry or hardware writes. TIMER0
 * integer microseconds are enclosed by two bounded TIMER1 observations.
 * At the enclosed TIMER0 read, local time is [local_ns, local_ns + 999] ns;
 * callers must also propagate the full raw_before/raw_after bracket.
 * Supported: SDK TIMER0, 12 MHz XOSC -> integer clk_ref/tick divider, and
 * locked integer PLL_SYS -> integer clk_sys divider. Both clock trees and
 * timer controls must remain unchanged. False preserves the complete out.
 * This is a configuration observation, not a reset/rate-change epoch: the
 * owner must exclude hidden change-and-restore, timer writes and debug stops
 * throughout its admitted measurement lifetime. No physical SI accuracy or
 * cross-board phase relationship is established by this local bridge. */
bool vdc_timestamp_clock_try_read_bridge(uint32_t expected_hz,
    vdc_timestamp_clock_bridge_t *out);
/* One read-only observation of the bridge's complete supported configuration
 * and clock readiness, without counter sampling, initialization or retry.
 * This is the diagnostic's configuration_supported && clock_ready predicate,
 * not bridge validity or a reset/rate-change epoch. The caller still owns
 * clock/timer lifetime exclusion and any required before/after validation. */
bool vdc_timestamp_clock_configuration_supported(uint32_t expected_hz);
/* Stopped diagnostic only. The caller owns the STOP barrier. Configuration
 * and counter snapshots are independent observations; only bridge_valid
 * grants validity to bridge. No initialization, retry, writes or static RAM.
 * configuration_supported is the production configuration check result,
 * while platform_supported reports availability of the device implementation.
 * Non-NULL output is always filled, including unsupported/failed observations. */
typedef struct {
    uint32_t schema, platform_supported, configuration_supported, bridge_valid;
    uint32_t clock_ready, expected_hz, cached_hz, sdk_sys_hz, default_timer;
    uint32_t proc_config;
    uint32_t timer0_source, timer0_pause, timer0_dbgpause;
    uint32_t timer1_source, timer1_pause, timer1_dbgpause;
    uint32_t ref_ctrl, ref_div, ref_selected;
    uint32_t sys_ctrl, sys_div, sys_selected, resus_ctrl, resus_status;
    uint32_t pll_cs, pll_pwr, pll_fbdiv, pll_prim;
    uint32_t tick_ctrl, tick_cycles;
    uint32_t xosc_ctrl, xosc_status, xosc_dormant;
    uint32_t timer0_sample_valid, timer1_sample_valid;
    uint64_t timer0_sample_us, timer1_sample_ticks;
    vdc_timestamp_clock_bridge_t bridge;
} vdc_timestamp_clock_bridge_diagnostic_t;
bool vdc_timestamp_clock_read_bridge_diagnostic(uint32_t expected_hz,
    vdc_timestamp_clock_bridge_diagnostic_t *out);
/* Bounded configuration observation only; no timestamp or lazy init. */
bool vdc_timestamp_clock_is_current(uint32_t expected_hz);
uint64_t vdc_timestamp_clock_ticks_to_ns(uint64_t ticks);
uint64_t vdc_timestamp_clock_now_ns(void);

#endif
