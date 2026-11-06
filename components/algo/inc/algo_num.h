#ifndef ALGO_NUM_H
#define ALGO_NUM_H

#include <stdint.h>

/*
 * Shared numeric primitives: saturation and clamping.
 *
 * These are behaviour-only helpers (no state, no platform dependency). They
 * exist because the same few lines were copy-pasted across domains; the
 * semantics below are the ones already used on the wire and in counters:
 * saturate at the bound, never wrap.
 */

static inline uint32_t algo_sat_u64_to_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static inline uint32_t algo_sat_add_u32(uint32_t value, uint32_t add)
{
    const uint64_t sum = (uint64_t)value + add;
    return sum > UINT32_MAX ? UINT32_MAX : (uint32_t)sum;
}

static inline int32_t algo_clamp_i64_to_i32(int64_t value)
{
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    if (value < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
}

#endif /* ALGO_NUM_H */
