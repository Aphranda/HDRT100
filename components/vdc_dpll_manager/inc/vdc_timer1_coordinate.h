#ifndef VDC_TIMER1_COORDINATE_H
#define VDC_TIMER1_COORDINATE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* TIMER1 origin, nanosecond units. No TIMER0 bridge or DCO state. */
static inline bool vdc_timer1_ticks_to_ns(uint32_t hz, uint64_t ticks,
    bool round_up, uint64_t *out)
{
    if (!out || !hz || hz>500000000u) return false;
    const uint64_t seconds=ticks/hz;
    const uint64_t fraction=(ticks%hz)*UINT64_C(1000000000);
    if (seconds>UINT64_MAX/UINT64_C(1000000000)) return false;
    const uint64_t base=seconds*UINT64_C(1000000000);
    const uint64_t ns=fraction/hz+(round_up && fraction%hz!=0u);
    if (base>UINT64_MAX-ns) return false;
    *out=base+ns;
    return true;
}

static inline bool vdc_timer1_ns_to_ticks(uint32_t hz, uint64_t ns,
    bool round_up, uint64_t *out)
{
    if (!out || !hz || hz>500000000u) return false;
    const uint64_t seconds=ns/UINT64_C(1000000000);
    const uint64_t fraction=(ns%UINT64_C(1000000000))*hz;
    if (seconds>UINT64_MAX/hz) return false;
    const uint64_t base=seconds*hz;
    const uint64_t ticks=fraction/UINT64_C(1000000000)+
        (round_up && fraction%UINT64_C(1000000000)!=0u);
    if (base>UINT64_MAX-ticks) return false;
    *out=base+ticks;
    return true;
}

/* Raw intervals retain the observer's physical uncertainty. */
static inline bool vdc_timer1_interval_to_ns(uint32_t hz, uint64_t raw_lo,
    uint64_t raw_hi, uint64_t *lo, uint64_t *hi)
{
    uint64_t lower,upper;
    if (!lo || !hi || lo==hi || raw_lo>raw_hi ||
        !vdc_timer1_ticks_to_ns(hz,raw_lo,false,&lower) ||
        !vdc_timer1_ticks_to_ns(hz,raw_hi,true,&upper)) return false;
    *lo=lower; *hi=upper;
    return true;
}

#endif
