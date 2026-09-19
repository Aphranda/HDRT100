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
