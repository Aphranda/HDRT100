#include "vdc_timestamp_clock.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/clocks.h"
#include "hardware/timer.h"
#endif

#define VDC_TIMESTAMP_CLOCK_DEFAULT_HZ 1000000u

static bool s_vdc_timestamp_clock_initialized;
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
    if (s_vdc_timestamp_clock_initialized) {
        return true;
    }

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
    s_vdc_timestamp_clock_initialized = true;
    return s_vdc_timestamp_clock_tick_hz != 0u &&
           s_vdc_timestamp_clock_resolution_ns != 0u;
}

uint32_t vdc_timestamp_clock_tick_hz(void)
{
    if (!s_vdc_timestamp_clock_initialized) {
        (void)vdc_timestamp_clock_init();
    }
    return s_vdc_timestamp_clock_tick_hz;
}

uint32_t vdc_timestamp_clock_resolution_ns(void)
{
    if (!s_vdc_timestamp_clock_initialized) {
        (void)vdc_timestamp_clock_init();
    }
    return s_vdc_timestamp_clock_resolution_ns;
}

uint64_t vdc_timestamp_clock_read_ticks64(void)
{
    if (!s_vdc_timestamp_clock_initialized) {
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
    const uint64_t seconds = ticks / (uint64_t)hz;
    const uint64_t remainder = ticks % (uint64_t)hz;
    return seconds * 1000000000ull +
           (remainder * 1000000000ull) / (uint64_t)hz;
}

uint64_t vdc_timestamp_clock_now_ns(void)
{
    return vdc_timestamp_clock_ticks_to_ns(vdc_timestamp_clock_read_ticks64());
}
