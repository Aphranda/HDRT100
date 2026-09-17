#ifndef VDC_CLOCK_MAPPING_H
#define VDC_CLOCK_MAPPING_H

#include "vdc_model_projection.h"

#define VDC_CLOCK_MAPPING_MAX_CONSTRAINTS 8u
#define VDC_CLOCK_MAPPING_MAX_SECONDS 2u

typedef enum {
    VDC_CLOCK_MAPPING_OK = 0,
    VDC_CLOCK_MAPPING_INVALID,
    VDC_CLOCK_MAPPING_CONTRADICTION,
    VDC_CLOCK_MAPPING_EXHAUSTED,
    VDC_CLOCK_MAPPING_UNAVAILABLE
} vdc_clock_mapping_status_t;

typedef enum {
    VDC_CLOCK_MAPPING_RESET_NONE = 0,
    VDC_CLOCK_MAPPING_RESET_START,
    VDC_CLOCK_MAPPING_RESET_MODEL,
    VDC_CLOCK_MAPPING_RESET_HORIZON
} vdc_clock_mapping_reset_t;

/* One Core1 owner. Offsets are scaled ns*Hz relative to fixed (raw, local)
 * anchors, with an OPEN upper bound. No absolute timestamp products. */
typedef struct {
    uint64_t anchor_raw, anchor_local_ns, last_raw_after, last_local_ns;
    int64_t offset_lo, offset_hi_open;
    uint32_t tick_hz, model_token, count, epoch;
} vdc_clock_mapping_cache_t;

typedef struct {
    vdc_clock_mapping_cache_t next;
    int64_t effective_lo, effective_hi_open;
    uint64_t local_lo, local_hi;
    uint64_t original_output_lo, original_output_hi, output_lo, output_hi;
    uint32_t reset_reason, count_before, retained;
} vdc_clock_mapping_result_t;

static inline int64_t vdc_clock_mapping_floor(int64_t numerator, uint32_t divisor)
{
    return numerator / (int64_t)divisor - (numerator % (int64_t)divisor < 0);
}

static inline int64_t vdc_clock_mapping_ceil(int64_t numerator, uint32_t divisor)
{
    return numerator / (int64_t)divisor + (numerator % (int64_t)divisor > 0);
}

static inline bool vdc_clock_mapping_add(uint64_t base, int64_t offset, uint64_t *out)
{
    if (offset < 0) {
        /* This form is safe even at INT64_MIN. */
        const uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1u;
        if (base < magnitude) return false;
        *out = base - magnitude;
    } else {
        if (base > UINT64_MAX - (uint64_t)offset) return false;
        *out = base + (uint64_t)offset;
    }
    return true;
}

/* Pure tentative projection: cache is never changed; *out is assigned only
 * on OK. The owner commits next only after lifecycle checks AND encoding.
 * The clock relation must retain vdc_timestamp_clock.h's lifetime exclusions.
 * This bounds F(floor(X(event))), not a hypothetical floor-us TIMER0 read.
 * The original absolute projector remains a mandatory admission check. */
static inline vdc_clock_mapping_status_t vdc_clock_mapping_project(
    const vdc_clock_mapping_cache_t *cache, const vdc_dco_control_t *dco,
    const vdc_timestamp_clock_bridge_t *bridge, uint32_t model_token,
    uint64_t raw_lo, uint64_t raw_hi, vdc_clock_mapping_result_t *out)
{
    if (!cache || !bridge || !out || !model_token || !bridge->tick_hz ||
        bridge->tick_hz > 500000000u || bridge->local_ns % 1000u ||
        cache->count > VDC_CLOCK_MAPPING_MAX_CONSTRAINTS)
        return VDC_CLOCK_MAPPING_INVALID;
    vdc_clock_mapping_result_t result = {0};
    if (!vdc_model_project_interval(dco, bridge, raw_lo, raw_hi,
            &result.original_output_lo, &result.original_output_hi))
        return VDC_CLOCK_MAPPING_INVALID;
    const uint64_t horizon = (uint64_t)bridge->tick_hz * VDC_CLOCK_MAPPING_MAX_SECONDS;
    result.next = *cache;
    result.count_before = cache->count;
    if (cache->count) {
        /* A clock rollback or corrupt anchor is never an ordinary renewal,
         * including when the committed DCO token has just changed. */
        if (!cache->epoch || !cache->model_token || cache->tick_hz != bridge->tick_hz ||
            cache->anchor_raw > raw_lo || cache->anchor_raw > cache->last_raw_after ||
            cache->last_raw_after - cache->anchor_raw > horizon ||
            bridge->raw_before < cache->last_raw_after ||
            cache->anchor_local_ns > cache->last_local_ns ||
            cache->last_local_ns - cache->anchor_local_ns > UINT64_C(2000001000) ||
            bridge->local_ns < cache->last_local_ns ||
            cache->anchor_local_ns % 1000u || cache->last_local_ns % 1000u ||
            cache->offset_lo >= cache->offset_hi_open ||
            cache->offset_lo < -INT64_C(2000000000000000000) ||
            cache->offset_hi_open > INT64_C(2000000000000000000))
            return VDC_CLOCK_MAPPING_INVALID;
        if (cache->model_token != model_token)
            result.reset_reason = VDC_CLOCK_MAPPING_RESET_MODEL;
        else if (bridge->raw_after - cache->anchor_raw > horizon)
            result.reset_reason = VDC_CLOCK_MAPPING_RESET_HORIZON;
    } else {
        result.reset_reason = VDC_CLOCK_MAPPING_RESET_START;
    }
    if (result.reset_reason) {
        if (cache->epoch == UINT32_MAX) return VDC_CLOCK_MAPPING_EXHAUSTED;
        result.next = (vdc_clock_mapping_cache_t){
            .anchor_raw = raw_lo, .anchor_local_ns = bridge->local_ns,
            .tick_hz = bridge->tick_hz, .model_token = model_token,
            .epoch = cache->epoch + 1u};
    }
    vdc_clock_mapping_cache_t *next = &result.next;
    if (raw_lo < next->anchor_raw || bridge->raw_after - next->anchor_raw > horizon ||
        bridge->local_ns < next->anchor_local_ns ||
        bridge->local_ns - next->anchor_local_ns > UINT64_C(2000001000))
        return VDC_CLOCK_MAPPING_INVALID;
    /* Each product <= about 1e18; every signed sum below stays within 3e18.
     * Keep +1000 OPEN through intersection; +999 here would lose fractional
     * placement and break equivalence with the original single-bridge map. */
    const uint64_t local_delta = bridge->local_ns - next->anchor_local_ns;
    int64_t lo = (int64_t)(local_delta * bridge->tick_hz) -
        (int64_t)((bridge->raw_after - next->anchor_raw) * UINT64_C(1000000000));
    int64_t hi = (int64_t)((local_delta + 1000u) * bridge->tick_hz) -
        (int64_t)((bridge->raw_before - next->anchor_raw) * UINT64_C(1000000000));
    if (next->count) {
        if (next->offset_lo > lo) lo = next->offset_lo;
        if (next->offset_hi_open < hi) hi = next->offset_hi_open;
    }
    if (lo >= hi) return VDC_CLOCK_MAPPING_CONTRADICTION;
    result.effective_lo = lo;
    result.effective_hi_open = hi;
    const int64_t event_lo = lo +
        (int64_t)((raw_lo - next->anchor_raw) * UINT64_C(1000000000));
    const int64_t event_hi = hi +
        (int64_t)((raw_hi - next->anchor_raw) * UINT64_C(1000000000));
    if (!vdc_clock_mapping_add(next->anchor_local_ns,
            vdc_clock_mapping_floor(event_lo, bridge->tick_hz), &result.local_lo) ||
        !vdc_clock_mapping_add(next->anchor_local_ns,
            vdc_clock_mapping_ceil(event_hi, bridge->tick_hz) - 1, &result.local_hi) ||
        result.local_lo > result.local_hi ||
        !vdc_domain_dco_local_to_output_ns(dco, result.local_lo, &result.output_lo) ||
        !vdc_domain_dco_local_to_output_ns(dco, result.local_hi, &result.output_hi) ||
        result.output_lo > result.output_hi)
        return VDC_CLOCK_MAPPING_INVALID;
    if (result.output_lo < result.original_output_lo)
        result.output_lo = result.original_output_lo;
    if (result.output_hi > result.original_output_hi)
        result.output_hi = result.original_output_hi;
    if (result.output_lo > result.output_hi) return VDC_CLOCK_MAPPING_CONTRADICTION;
    if (next->count < VDC_CLOCK_MAPPING_MAX_CONSTRAINTS) {
        next->offset_lo = lo;
        next->offset_hi_open = hi;
        ++next->count;
        result.retained = 1u;
    }
    next->last_raw_after = bridge->raw_after;
    next->last_local_ns = bridge->local_ns;
    *out = result;
    return VDC_CLOCK_MAPPING_OK;
}

#endif
