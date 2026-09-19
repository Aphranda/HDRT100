#ifndef SYNC_IO_REFERENCE_MATH_H
#define SYNC_IO_REFERENCE_MATH_H
#include <limits.h>
#include "sync_io_reference.h"
static inline bool sync_io_reference_validate(const sync_io_reference_config_t *c,
    uint32_t *periods)
{
    if(!c || !periods || c->input_port<1u || c->input_port>4u || c->edge>1u ||
        c->nominal_hz<1000u || c->nominal_hz>20000000u ||
        c->window_ms<100u || c->window_ms>5000u ||
        c->timeout_ms<=c->window_ms || c->timeout_ms>10000u) return false;
    const uint64_t n=(uint64_t)c->nominal_hz*c->window_ms;
    if(n%1000u || n/1000u==0u || n/1000u>UINT32_MAX) return false;
    *periods=(uint32_t)(n/1000u);return true;
}
/* FIFO token end is one instruction later than start, relative to its edge.
 * DMA delay differences are deliberately not represented as zero uncertainty.
 * Positive ppb means reference input measured faster than configured nominal,
 * when elapsed ticks are interpreted using the configured nominal local Hz. */
static inline bool sync_io_reference_evaluate(const sync_io_reference_config_t *c,
    uint32_t hz,uint32_t start,uint32_t end,uint32_t *ticks,int32_t *ppb)
{
    uint32_t n;
    if(!ticks || !ppb || !sync_io_reference_validate(c,&n) || !hz || hz%1000u ||
        (uint64_t)hz*c->timeout_ms/1000u>=UINT32_MAX) return false;
    const uint32_t raw_delta=end-start;
    if(raw_delta<=SYNC_IO_REFERENCE_PIO_BIAS_TICKS ||
        raw_delta>(uint64_t)hz*c->timeout_ms/1000u) return false;
    const uint32_t dt=raw_delta-SYNC_IO_REFERENCE_PIO_BIAS_TICKS;
    const int64_t expected=(uint64_t)hz*c->window_ms/1000u;
    const int64_t value=(expected-(int64_t)dt)*INT64_C(1000000000)/(int64_t)dt;
    if(value<INT32_MIN || value>INT32_MAX) return false;
    *ticks=dt;*ppb=(int32_t)value;return true;
}
#endif
