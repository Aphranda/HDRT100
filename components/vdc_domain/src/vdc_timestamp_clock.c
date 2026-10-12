#include "vdc_timestamp_clock.h"
#include <stddef.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#define VDC_TIMESTAMP_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_TIMESTAMP_TIME_CRITICAL(name) name
#endif

#define VDC_TIMESTAMP_CLOCK_DEFAULT_HZ 1000000u

static bool s_vdc_timestamp_clock_initialized;
static bool s_vdc_timestamp_clock_ready;
static uint32_t s_vdc_timestamp_clock_tick_hz;
static uint32_t s_vdc_timestamp_clock_resolution_ns;

static uint32_t vdc_timestamp_clock_resolution_from_hz(uint32_t tick_hz)
{
    if (tick_hz == 0u) {
        return 0u;
    }
    const uint64_t resolution =
        (1000000000ull + (uint64_t)tick_hz - 1ull) / (uint64_t)tick_hz;
    return resolution > UINT32_MAX ? UINT32_MAX : (uint32_t)resolution;
}

bool vdc_timestamp_clock_init(void)
{
    if (__atomic_load_n(&s_vdc_timestamp_clock_initialized, __ATOMIC_ACQUIRE)) {
        return true;
    }

    __atomic_store_n(&s_vdc_timestamp_clock_ready, false, __ATOMIC_RELEASE);

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    timer1_hw->pause = 1u;
    timer1_hw->source = TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS;
    timer1_hw->timelw = 0u;
    timer1_hw->timehw = 0u;
    timer1_hw->pause = 0u;
    s_vdc_timestamp_clock_tick_hz = clock_get_hz(clk_sys);
#else
    s_vdc_timestamp_clock_tick_hz = VDC_TIMESTAMP_CLOCK_DEFAULT_HZ;
#endif

    s_vdc_timestamp_clock_resolution_ns =
        vdc_timestamp_clock_resolution_from_hz(s_vdc_timestamp_clock_tick_hz);
    const bool ready = s_vdc_timestamp_clock_tick_hz != 0u &&
                       s_vdc_timestamp_clock_resolution_ns != 0u;
    __atomic_store_n(&s_vdc_timestamp_clock_initialized, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_vdc_timestamp_clock_ready, ready, __ATOMIC_RELEASE);
    return ready;
}

uint32_t vdc_timestamp_clock_tick_hz(void)
{
    if (!__atomic_load_n(&s_vdc_timestamp_clock_initialized, __ATOMIC_ACQUIRE)) {
        (void)vdc_timestamp_clock_init();
    }
    return s_vdc_timestamp_clock_tick_hz;
}

uint32_t vdc_timestamp_clock_resolution_ns(void)
{
    if (!__atomic_load_n(&s_vdc_timestamp_clock_initialized, __ATOMIC_ACQUIRE)) {
        (void)vdc_timestamp_clock_init();
    }
    return s_vdc_timestamp_clock_resolution_ns;
}

uint64_t VDC_TIMESTAMP_TIME_CRITICAL(vdc_timestamp_clock_read_ticks64)(void)
{
    if (!__atomic_load_n(&s_vdc_timestamp_clock_initialized, __ATOMIC_ACQUIRE)) {
        (void)vdc_timestamp_clock_init();
    }

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    uint32_t hi = timer1_hw->timerawh;
    uint32_t lo;
    do {
        lo = timer1_hw->timerawl;
        const uint32_t next_hi = timer1_hw->timerawh;
        if (hi == next_hi) {
            break;
        }
        hi = next_hi;
    } while (true);
    return ((uint64_t)hi << 32u) | lo;
#else
    static uint64_t host_ticks;
    host_ticks += 1u;
    return host_ticks;
#endif
}

uint64_t vdc_timestamp_clock_ticks_to_ns(uint64_t ticks)
{
    const uint32_t hz = vdc_timestamp_clock_tick_hz();
    if (hz == 0u) {
        return 0u;
    }
    /* Resolution is rounded up for general clocks. Use it as a scale only
     * when one second is exactly hz periods. Unsigned multiplication keeps
     * the same modulo-uint64 result as the quotient/remainder expression. */
    const uint32_t period_ns = s_vdc_timestamp_clock_resolution_ns;
    if ((uint64_t)hz * period_ns == 1000000000ull) {
        return ticks * period_ns;
    }
    const uint64_t seconds = ticks / (uint64_t)hz;
    const uint64_t remainder = ticks % (uint64_t)hz;
    return seconds * 1000000000ull +
           (remainder * 1000000000ull) / (uint64_t)hz;
}

uint64_t vdc_timestamp_clock_now_ns(void)
{
    return vdc_timestamp_clock_ticks_to_ns(vdc_timestamp_clock_read_ticks64());
}

bool vdc_timestamp_clock_is_current(uint32_t expected_hz)
{
    if (!__atomic_load_n(&s_vdc_timestamp_clock_ready, __ATOMIC_ACQUIRE) ||
        expected_hz == 0u || s_vdc_timestamp_clock_tick_hz != expected_hz) {
        return false;
    }
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    return timer1_hw->source == TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS &&
           timer1_hw->pause == 0u && clock_get_hz(clk_sys) == expected_hz;
#else
    return false;
#endif
}

bool VDC_TIMESTAMP_TIME_CRITICAL(vdc_timestamp_clock_try_read_ticks64)(
    uint32_t expected_hz, uint64_t *ticks)
{
    if (ticks == NULL || !vdc_timestamp_clock_is_current(expected_hz)) {
        return false;
    }
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    const uint32_t hi = timer1_hw->timerawh;
    const uint32_t lo = timer1_hw->timerawl;
    const uint32_t after_hi = timer1_hw->timerawh;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (hi != after_hi || !vdc_timestamp_clock_is_current(expected_hz)) {
        return false;
    }
    *ticks = ((uint64_t)hi << 32u) | lo;
    return true;
#else
    return false;
#endif
}

bool VDC_TIMESTAMP_TIME_CRITICAL(vdc_timestamp_clock_try_read_ns)(
    uint32_t expected_hz, uint64_t *out)
{
    uint64_t ticks;
    if (out == NULL || expected_hz == 0u ||
        !vdc_timestamp_clock_try_read_ticks64(expected_hz, &ticks)) return false;
    if (UINT32_C(1000000000) % expected_hz == 0u) {
        const uint32_t scale = UINT32_C(1000000000) / expected_hz;
        if (ticks > UINT64_MAX / scale) return false;
        *out = ticks * scale;
        return true;
    }
    const uint64_t seconds = ticks / expected_hz;
    const uint64_t fraction =
        (ticks % expected_hz) * UINT64_C(1000000000) / expected_hz;
    if (seconds > (UINT64_MAX - fraction) / UINT64_C(1000000000)) return false;
    *out = seconds * UINT64_C(1000000000) + fraction;
    return true;
}

/* ── Core0 kernel tick audit ──
 * The writer is the Core0 diagnostics service and the reader is the Core0 SCPI
 * task, so each 32-bit field is published atomically without a lock. The
 * dynamic window is only closed by a timed sample; an untimed sample keeps the
 * window shut so the next one cannot compare against a foreign origin. */

#define VDC_TICK_AUDIT_NS_PER_MS UINT64_C(1000000)

static vdc_tick_audit_t s_vdc_tick_audit = {.version = VDC_TICK_AUDIT_VERSION};
static uint32_t s_vdc_tick_audit_start_ms;
static uint64_t s_vdc_tick_audit_start_ns;
static bool s_vdc_tick_audit_window_open;

static bool vdc_tick_audit_port_complete(const vdc_tick_audit_port_t *port)
{
    return port != NULL && port->clk_sys_hz != NULL && port->tick_rate_hz != NULL &&
           port->tick_ms != NULL && port->systick_reload != NULL &&
           port->systick_control != NULL && port->time_ns != NULL;
}

bool vdc_tick_audit_sample(const vdc_tick_audit_port_t *port)
{
    if (!vdc_tick_audit_port_complete(port)) {
        return false;
    }

    const uint32_t clk_sys_hz = port->clk_sys_hz();
    const uint32_t tick_rate_hz = port->tick_rate_hz();
    if (clk_sys_hz == 0u || tick_rate_hz == 0u || clk_sys_hz % tick_rate_hz != 0u) {
        return false;
    }

    const uint32_t control = port->systick_control();
    const uint32_t reload = port->systick_reload();
    const uint32_t expected_reload = clk_sys_hz / tick_rate_hz - 1u;
    uint32_t static_flags = 0u;
    if ((control & VDC_TICK_AUDIT_SYSTICK_ENABLE_BIT) != 0u) {
        static_flags |= VDC_TICK_AUDIT_STATIC_ARMED;
    }
    if ((control & VDC_TICK_AUDIT_SYSTICK_CLKSOURCE_BIT) != 0u) {
        static_flags |= VDC_TICK_AUDIT_STATIC_CLKSOURCE;
    }
    if (reload == expected_reload) {
        static_flags |= VDC_TICK_AUDIT_STATIC_RELOAD;
    }

    const uint32_t tick_ms = port->tick_ms();
    uint64_t now_ns = 0u;
    const bool timed = port->time_ns(&now_ns);

    s_vdc_tick_audit.clk_sys_hz = clk_sys_hz;
    s_vdc_tick_audit.tick_rate_hz = tick_rate_hz;
    s_vdc_tick_audit.expected_reload = expected_reload;
    s_vdc_tick_audit.actual_reload = reload;
    s_vdc_tick_audit.static_flags = static_flags;
    ++s_vdc_tick_audit.samples;

    if (!timed) {
        /* Report the missing timebase explicitly and drop any earlier window
         * instead of leaving stale numbers that could read as current. */
        s_vdc_tick_audit_window_open = false;
        s_vdc_tick_audit.dynamic_flags = VDC_TICK_AUDIT_DYNAMIC_NO_TIMEBASE;
        s_vdc_tick_audit.window_nominal_ms = 0u;
        s_vdc_tick_audit.window_real_ms = 0u;
        s_vdc_tick_audit.ratio_ppm = 0;
        return true;
    }
    if (!s_vdc_tick_audit_window_open) {
        s_vdc_tick_audit.dynamic_flags = 0u;
        s_vdc_tick_audit_start_ms = tick_ms;
        s_vdc_tick_audit_start_ns = now_ns;
        s_vdc_tick_audit_window_open = true;
        return true;
    }

    /* Unsigned differences stay correct across both counter wraps. */
    const uint32_t nominal_ms = tick_ms - s_vdc_tick_audit_start_ms;
    if (nominal_ms < VDC_TICK_AUDIT_WINDOW_MS) {
        return true;
    }
    const uint64_t nominal_ns = (uint64_t)nominal_ms * VDC_TICK_AUDIT_NS_PER_MS;
    const uint64_t real_ns = now_ns - s_vdc_tick_audit_start_ns;
    if (real_ns != 0u) {
        const int64_t error_ns = (int64_t)nominal_ns - (int64_t)real_ns;
        int64_t ppm = error_ns * (int64_t)VDC_TICK_AUDIT_NS_PER_MS / (int64_t)real_ns;
        if (ppm > INT32_MAX) {
            ppm = INT32_MAX;
        } else if (ppm < INT32_MIN) {
            ppm = INT32_MIN;
        }
        s_vdc_tick_audit.ratio_ppm = (int32_t)ppm;
        s_vdc_tick_audit.window_nominal_ms = nominal_ms;
        s_vdc_tick_audit.window_real_ms = (uint32_t)(real_ns / VDC_TICK_AUDIT_NS_PER_MS);
        s_vdc_tick_audit.dynamic_flags = VDC_TICK_AUDIT_DYNAMIC_VALID;
    }
    s_vdc_tick_audit_start_ms = tick_ms;
    s_vdc_tick_audit_start_ns = now_ns;
    return true;
}

bool vdc_tick_audit_get(vdc_tick_audit_t *audit)
{
    if (audit == NULL) {
        return false;
    }
    *audit = s_vdc_tick_audit;
    return audit->samples != 0u;
}
