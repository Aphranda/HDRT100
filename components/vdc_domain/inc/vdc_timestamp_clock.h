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
/* TIMER1-origin nanoseconds, floor-rounded from the raw tick. No lazy init,
 * fallback clock or retry; failure preserves out. */
bool vdc_timestamp_clock_try_read_ns(uint32_t expected_hz, uint64_t *out);
/* ── Core0 kernel tick audit ──
 * The kernel port derives the SysTick reload from clk_sys when the scheduler
 * starts, and SysTick counts the processor clock. A clock change afterwards
 * leaves that reload stale, so every millisecond the kernel reports, delays and
 * times out is scaled by boot/final clock. The static part compares the
 * installed reload against the current clock; the dynamic part compares the
 * software tick against this module's TIMER1 nanosecond origin. Both are
 * read-only, Core0 only, and never fabricate a value: flags, not zeroed
 * durations, identify what could not be measured. */
#define VDC_TICK_AUDIT_VERSION 1u
#define VDC_TICK_AUDIT_WINDOW_MS 4000u
/* ARMv8-M SysTick control bits: enable and processor-clock source. */
#define VDC_TICK_AUDIT_SYSTICK_ENABLE_BIT 0x1u
#define VDC_TICK_AUDIT_SYSTICK_CLKSOURCE_BIT 0x4u
#define VDC_TICK_AUDIT_STATIC_ARMED 0x1u
#define VDC_TICK_AUDIT_STATIC_CLKSOURCE 0x2u
#define VDC_TICK_AUDIT_STATIC_RELOAD 0x4u
#define VDC_TICK_AUDIT_STATIC_OK \
    (VDC_TICK_AUDIT_STATIC_ARMED | VDC_TICK_AUDIT_STATIC_CLKSOURCE | \
     VDC_TICK_AUDIT_STATIC_RELOAD)
#define VDC_TICK_AUDIT_DYNAMIC_VALID 0x1u
/* The TIMER1 origin was unavailable for the last sample. This audit never
 * initializes the timestamp clock: doing so resets the TIMER1 epoch that the
 * sequence and VDC paths already share. */
#define VDC_TICK_AUDIT_DYNAMIC_NO_TIMEBASE 0x2u

typedef struct {
    uint32_t (*clk_sys_hz)(void);
    uint32_t (*tick_rate_hz)(void);
    uint32_t (*tick_ms)(void);
    uint32_t (*systick_reload)(void);
    uint32_t (*systick_control)(void);
    bool (*time_ns)(uint64_t *out);
} vdc_tick_audit_port_t;

typedef struct {
    uint32_t version;
    uint32_t clk_sys_hz;
    uint32_t tick_rate_hz;
    uint32_t expected_reload;
    uint32_t actual_reload;
    uint32_t static_flags;
    /* Nominal (software tick) and real (TIMER1) spans of the last completed
     * window, and the signed error of the software tick in ppm: positive means
     * the kernel runs fast, negative means slow. */
    uint32_t window_nominal_ms;
    uint32_t window_real_ms;
    int32_t ratio_ppm;
    uint32_t samples;
    uint32_t dynamic_flags;
} vdc_tick_audit_t;

/* Bounded Core0 sampler; returns false when the port is incomplete or the
 * clock/rate pair cannot produce a whole tick period. */
bool vdc_tick_audit_sample(const vdc_tick_audit_port_t *port);
/* Latest published snapshot. False only means no sample has been taken yet;
 * the snapshot still carries its version so missing data cannot read as a pass. */
bool vdc_tick_audit_get(vdc_tick_audit_t *audit);

/* Bounded configuration observation only; no timestamp or lazy init. */
bool vdc_timestamp_clock_is_current(uint32_t expected_hz);
uint64_t vdc_timestamp_clock_ticks_to_ns(uint64_t ticks);
uint64_t vdc_timestamp_clock_now_ns(void);

#endif
