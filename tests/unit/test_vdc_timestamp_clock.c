#include <inttypes.h>
#include <stdio.h>

/* Include the actual implementation to exercise each initialization epoch
 * with a fake device clock and timer, without adding a production reset API. */
#include "../../components/vdc_domain/src/vdc_timestamp_clock.c"

test_timer_hw_t test_timer;
static uint32_t test_clock_hz;
static unsigned test_clock_reads;

uint32_t clock_get_hz(int clock)
{
    (void)clock;
    ++test_clock_reads;
    return test_clock_hz;
}

int main(void)
{
    uint32_t hz;
    uint64_t ticks;
    while (scanf("%" SCNu32 " %" SCNu64, &hz, &ticks) == 2) {
        s_vdc_timestamp_clock_initialized = false;
        test_clock_hz = hz;
        test_clock_reads = 0u;
        test_timer.pause = 9u;
        test_timer.source = 9u;
        test_timer.timelw = test_timer.timehw = 9u;
        const bool initialized = vdc_timestamp_clock_init();
        if (initialized != (hz != 0u) || test_clock_reads != 1u ||
            test_timer.pause != 0u || test_timer.timelw != 0u ||
            test_timer.timehw != 0u || test_timer.source != TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS)
            return 1;
        test_timer.timerawl = (uint32_t)ticks;
        test_timer.timerawh = (uint32_t)(ticks >> 32u);
        const uint64_t converted = vdc_timestamp_clock_ticks_to_ns(ticks);
        const uint64_t now = vdc_timestamp_clock_now_ns();
        /* Repeated reads/init must not reset the timer or change its epoch. */
        test_clock_hz ^= UINT32_MAX;
        (void)vdc_timestamp_clock_init();
        if (vdc_timestamp_clock_tick_hz() != hz || test_clock_reads != 1u ||
            vdc_timestamp_clock_read_ticks64() != ticks)
            return 2;
        printf("%" PRIu64 " %" PRIu64 " %" PRIu32 "\n",
            converted, now, vdc_timestamp_clock_resolution_ns());
    }
    return ferror(stdin) ? 3 : 0;
}
