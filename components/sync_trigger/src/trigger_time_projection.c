#include "trigger_time_projection.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define TRIGGER_PROJECTION_HOT(name) __not_in_flash_func(name)
#else
#define TRIGGER_PROJECTION_HOT(name) name
#endif

__attribute__((noinline,optimize("Os")))
bool TRIGGER_PROJECTION_HOT(trigger_time_projection_fast)(
    const vdc_dco_control_t *model, uint64_t local_ns, uint64_t *output_ns)
{
    if (!model || !output_ns || !model->valid || !model->nominal_period_ns ||
        model->lock_state > VDC_DOMAIN_LOCK_FAULT ||
        local_ns < model->base_local_tick64) return false;
    const uint64_t delta = local_ns - model->base_local_tick64;
    const uint32_t magnitude = model->period_adjust_ppb < 0 ?
        (uint32_t)(-(int64_t)model->period_adjust_ppb) : (uint32_t)model->period_adjust_ppb;
    uint64_t product, value;
    if (__builtin_mul_overflow(delta, (uint64_t)magnitude, &product)) goto fallback;
    const uint64_t rate = product / UINT64_C(1000000000);
    if (__builtin_add_overflow(model->base_vdc_time64_ns, delta, &value)) goto fallback;
    if (model->period_adjust_ppb < 0) {
        if (__builtin_sub_overflow(value, rate, &value)) goto fallback;
    } else if (__builtin_add_overflow(value, rate, &value)) goto fallback;
    const uint32_t phase = model->phase_offset_ns < 0 ?
        (uint32_t)(-(int64_t)model->phase_offset_ns) : (uint32_t)model->phase_offset_ns;
    if (model->phase_offset_ns < 0) {
        if (__builtin_sub_overflow(value, (uint64_t)phase, &value)) goto fallback;
    } else if (__builtin_add_overflow(value, (uint64_t)phase, &value)) goto fallback;
    *output_ns = value;
    return true;

fallback:
    /* Overflow of one intermediate is not failure of the full signed sum.
     * The Domain retains carries so later negative terms can cancel them.
     * No partial output is published before that full decision. */
    return vdc_domain_dco_local_to_output_ns(model, local_ns, output_ns);
}
