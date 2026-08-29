#include "vdc_domain.h"

#include <limits.h>
#include <string.h>

#include "tdma_operating_profile.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define VDC_DOMAIN_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_DOMAIN_TIME_CRITICAL(name) name
#endif

#define VDC_DOMAIN_CRC_OFFSET 2166136261u
#define VDC_DOMAIN_CRC_PRIME 16777619u
#define VDC_DOMAIN_INITIAL_LOCK_SAMPLES 1u
#define VDC_DOMAIN_FREQ_LOCK_SAMPLES 2u
#define VDC_DOMAIN_PHASE_LOCK_SAMPLES 3u
#define VDC_DOMAIN_DEFAULT_FRESHNESS_LIMIT_US 1000000u
#define VDC_DOMAIN_DEFAULT_HOLDOVER_DRIFT_BOUND_NS_S 1000u
#define VDC_DOMAIN_ACQUISITION_GUARD_NS 0u
#define VDC_DOMAIN_DEFAULT_SANITY_FREQ_LIMIT_PPB 50000u
#define VDC_DOMAIN_RATE_MIN_OBSERVATION_CYCLES 8u
#define VDC_DOMAIN_RATE_SLEW_DIVISOR 8u
#define VDC_DOMAIN_RATE_MIN_SLEW_LIMIT_PPB 1000u

static uint32_t vdc_domain_hash_u32(uint32_t hash, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        hash ^= (value >> (i * 8u)) & 0xFFu;
        hash *= VDC_DOMAIN_CRC_PRIME;
    }
    return hash;
}

static uint32_t vdc_domain_abs_i32(int32_t value)
{
    if (value == INT32_MIN) {
        return (uint32_t)INT32_MAX + 1u;
    }
    return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
}

static uint64_t vdc_domain_mul_u64_u32_saturate(uint64_t value,
                                                uint32_t factor)
{
    uint64_t product = 0u;
    if (__builtin_mul_overflow(value, (uint64_t)factor, &product)) {
        return UINT64_MAX;
    }
    return product;
}

static uint32_t vdc_domain_gcd_u32(uint32_t left, uint32_t right)
{
    while (right != 0u) {
        const uint32_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

/* RP2350 has no native 64-bit divide. Servo ratios are always clamped to a
 * small configured ppb limit, so a bounded binary search is both exact and
 * cheaper than __aeabi_ldivmod. Negative results retain C's toward-zero
 * division semantics by searching the unsigned magnitude first. */
static int32_t VDC_DOMAIN_TIME_CRITICAL(
    vdc_domain_scaled_ratio_clamped_i32)(int64_t value,
                                         uint32_t scale,
                                         uint64_t divisor,
                                         uint32_t limit)
{
    if (value == 0 || scale == 0u || divisor == 0u || limit == 0u) {
        return 0;
    }

    const bool negative = value < 0;
    const uint64_t magnitude = negative
        ? (uint64_t)(-(value + 1ll)) + 1u
        : (uint64_t)value;
    const uint64_t scaled_value =
        vdc_domain_mul_u64_u32_saturate(magnitude, scale);
    const uint64_t scaled_limit =
        vdc_domain_mul_u64_u32_saturate(divisor, limit);
    if (scaled_value >= scaled_limit) {
        return negative ? -(int32_t)limit : (int32_t)limit;
    }

    /* Normal TDMA evidence deltas fit a 32-bit ratio after cancelling the
     * common ns/us scale. Cortex-M33 executes this division in hardware. */
    if (magnitude <= UINT32_MAX && divisor <= UINT32_MAX) {
        const uint32_t divisor32 = (uint32_t)divisor;
        const uint32_t common = vdc_domain_gcd_u32(scale, divisor32);
        const uint32_t reduced_scale = scale / common;
        const uint32_t reduced_divisor = divisor32 / common;
        uint32_t numerator = 0u;
        if (!__builtin_mul_overflow((uint32_t)magnitude,
                                    reduced_scale,
                                    &numerator)) {
            const uint32_t quotient = numerator / reduced_divisor;
            return negative ? -(int32_t)quotient : (int32_t)quotient;
        }
    }

    uint32_t low = 0u;
    uint32_t high = limit - 1u;
    while (low < high) {
        const uint32_t middle = low + (high - low + 1u) / 2u;
        const uint64_t threshold =
            vdc_domain_mul_u64_u32_saturate(divisor, middle);
        if (threshold <= scaled_value) {
            low = middle;
        } else {
            high = middle - 1u;
        }
    }
    return negative ? -(int32_t)low : (int32_t)low;
}

static int32_t VDC_DOMAIN_TIME_CRITICAL(
    vdc_domain_ppb_elapsed_correction_ns)(int32_t ppb,
                                           uint32_t elapsed_ns)
{
    const uint32_t elapsed_us = elapsed_ns / 1000u;
    const uint32_t magnitude = vdc_domain_abs_i32(ppb);
    if (elapsed_us == 0u || magnitude == 0u) {
        return 0;
    }

    /* The normal DPLL interval is milliseconds.  Converting elapsed time to
     * microseconds keeps the product in 32 bits through 85 ms at the 50 ppm
     * servo limit, so Cortex-M33 can use its native divide in the hot path.
     * Sub-microsecond truncation contributes less than one nanosecond. */
    if (magnitude <= UINT32_MAX / elapsed_us) {
        const uint32_t correction_ns =
            (magnitude * elapsed_us) / 1000000u;
        return ppb < 0 ? -(int32_t)correction_ns
                       : (int32_t)correction_ns;
    }

    return vdc_domain_scaled_ratio_clamped_i32(
        ppb, elapsed_ns, 1000000000ull, (uint32_t)INT32_MAX);
}

static void vdc_domain_gate_fail(vdc_gate_result_t *gate,
                                 vdc_domain_gate_code_t code,
                                 uint32_t slot,
                                 uint32_t evidence)
{
    if (gate == NULL) {
        return;
    }
    gate->passed = 0u;
    gate->reject_code = (uint32_t)code;
    gate->reject_slot = slot;
    gate->reject_evidence = evidence;
}

static vdc_domain_gate_code_t vdc_domain_timestamp_admission_to_gate(
    vdc_timestamp_admission_code_t code)
{
    switch (code) {
    case VDC_TIMESTAMP_ADMISSION_PASS:
        return VDC_DOMAIN_GATE_PASS;
    case VDC_TIMESTAMP_ADMISSION_RESOLUTION:
        return VDC_DOMAIN_GATE_TIMESTAMP_RESOLUTION;
    case VDC_TIMESTAMP_ADMISSION_BAD_ARGUMENT:
        return VDC_DOMAIN_GATE_BAD_ARGUMENT;
    case VDC_TIMESTAMP_ADMISSION_WINDOW_BOUND:
        return VDC_DOMAIN_GATE_WINDOW_BOUND;
    case VDC_TIMESTAMP_ADMISSION_NOT_ELIGIBLE:
    default:
        return VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE;
    }
}

static bool vdc_domain_range_in_period(uint32_t offset_ns,
                                       uint32_t width_ns,
                                       uint32_t period_ns)
{
    if (period_ns == 0u || width_ns == 0u || offset_ns >= period_ns) {
        return false;
    }
    return (uint64_t)offset_ns + (uint64_t)width_ns <= (uint64_t)period_ns;
}

static bool vdc_domain_ranges_overlap(uint32_t offset_a_ns,
                                      uint32_t width_a_ns,
                                      uint32_t offset_b_ns,
                                      uint32_t width_b_ns)
{
    const uint64_t a_begin = offset_a_ns;
    const uint64_t a_end = a_begin + width_a_ns;
    const uint64_t b_begin = offset_b_ns;
    const uint64_t b_end = b_begin + width_b_ns;
    return a_begin < b_end && b_begin < a_end;
}

static uint32_t vdc_domain_ring_forward_distance(uint32_t from_slot_id,
                                                 uint32_t to_slot_id,
                                                 uint32_t node_count)
{
    if (node_count == 0u || from_slot_id >= node_count ||
        to_slot_id >= node_count) {
        return 0u;
    }
    return to_slot_id >= from_slot_id
               ? to_slot_id - from_slot_id
               : node_count - from_slot_id + to_slot_id;
}

static bool vdc_domain_guarded_observation_overlaps(uint32_t obs_offset_ns,
                                                    uint32_t obs_width_ns,
                                                    uint32_t guard_before_ns,
                                                    uint32_t guard_after_ns,
                                                    uint32_t other_offset_ns,
                                                    uint32_t other_width_ns)
{
    const uint32_t guarded_offset =
        obs_offset_ns > guard_before_ns ? obs_offset_ns - guard_before_ns : 0u;
    const uint64_t guarded_end =
        (uint64_t)obs_offset_ns + (uint64_t)obs_width_ns +
        (uint64_t)guard_after_ns;
    const uint64_t guarded_width =
        guarded_end > guarded_offset ? guarded_end - guarded_offset : 0u;
    if (guarded_width > UINT32_MAX) {
        return true;
    }
    return vdc_domain_ranges_overlap(guarded_offset,
                                     (uint32_t)guarded_width,
                                     other_offset_ns,
                                     other_width_ns);
}

static bool vdc_domain_window_contract(
    const vdc_tdma_schedule_profile_t *profile,
    uint32_t window_class,
    uint32_t *offset_ns,
    uint32_t *width_ns)
{
    if (profile == NULL || offset_ns == NULL || width_ns == NULL) {
        return false;
    }

    switch ((vdc_domain_tdma_window_class_t)window_class) {
    case VDC_DOMAIN_WINDOW_VDC_OBSERVATION:
        *offset_ns = profile->observation_window_offset_ns;
        *width_ns = profile->observation_window_width_ns;
        return true;
    case VDC_DOMAIN_WINDOW_REFMEM_DATA:
        *offset_ns = profile->refmem_data_window_offset_ns;
        *width_ns = profile->refmem_data_window_width_ns;
        return true;
    case VDC_DOMAIN_WINDOW_IDLE_BEACON:
        *offset_ns = profile->idle_beacon_window_offset_ns;
        *width_ns = profile->idle_beacon_window_width_ns;
        return true;
    default:
        return false;
    }
}

static bool vdc_domain_payload_allowed_for_window(uint32_t window_class,
                                                  uint32_t payload_class)
{
    switch ((vdc_domain_tdma_window_class_t)window_class) {
    case VDC_DOMAIN_WINDOW_VDC_OBSERVATION:
        return payload_class == VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE ||
               payload_class == VDC_DOMAIN_PAYLOAD_IDLE_BEACON;
    case VDC_DOMAIN_WINDOW_REFMEM_DATA:
        return payload_class == VDC_DOMAIN_PAYLOAD_REFMEM_DELTA ||
               payload_class == VDC_DOMAIN_PAYLOAD_ACK_NACK_FENCE_QUALITY;
    case VDC_DOMAIN_WINDOW_IDLE_BEACON:
        return payload_class == VDC_DOMAIN_PAYLOAD_IDLE_BEACON;
    default:
        return false;
    }
}

static bool vdc_domain_payload_requires_reference_sync(uint32_t payload_class)
{
    return payload_class == VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE ||
           payload_class == VDC_DOMAIN_PAYLOAD_IDLE_BEACON;
}

static uint32_t vdc_domain_saturate_u64_to_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static uint64_t vdc_domain_window_start_minus_phase(uint64_t window_start_ns,
                                                    int32_t phase_offset_ns)
{
    if (phase_offset_ns < 0) {
        const uint64_t add_ns = (uint64_t)vdc_domain_abs_i32(phase_offset_ns);
        return UINT64_MAX - window_start_ns < add_ns
                   ? UINT64_MAX
                   : window_start_ns + add_ns;
    }
    const uint64_t subtract_ns = (uint64_t)phase_offset_ns;
    return window_start_ns > subtract_ns ? window_start_ns - subtract_ns : 0u;
}

static uint64_t vdc_domain_tracking_window_start_ns(
    const vdc_domain_context_t *context,
    uint64_t expected_window_start_ns,
    uint32_t half_width_ns)
{
    const uint64_t predicted_arrival_ns =
        vdc_domain_window_start_minus_phase(
            expected_window_start_ns, context->clock.phase_offset_ns);
    return predicted_arrival_ns > half_width_ns
        ? predicted_arrival_ns - half_width_ns : 0u;
}

static uint32_t vdc_domain_tracking_window_width_ns(uint32_t half_width_ns)
{
    return half_width_ns > UINT32_MAX / 2u
        ? UINT32_MAX : half_width_ns * 2u;
}

static uint32_t vdc_domain_avg_u32(uint32_t previous, uint32_t sample)
{
    return previous == 0u ? sample : (previous + sample) / 2u;
}

static int32_t vdc_domain_clamp_i64_to_i32(int64_t value)
{
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    if (value < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
}

static int32_t vdc_domain_phase_error_ns(
    const vdc_tdma_schedule_profile_t *profile,
    uint64_t expected_window_start_ns,
    uint32_t path_delay_ns,
    uint64_t observed_time_ns)
{
    const uint64_t reference_arrival_ns =
        UINT64_MAX - expected_window_start_ns < (uint64_t)path_delay_ns
            ? UINT64_MAX
            : expected_window_start_ns + (uint64_t)path_delay_ns;
    int64_t delta =
        (int64_t)observed_time_ns - (int64_t)reference_arrival_ns;

    if (profile != NULL && profile->period_ns != 0u) {
        const int64_t period = (int64_t)profile->period_ns;
        const int64_t half_period = period / 2ll;
        while (delta > half_period) {
            delta -= period;
        }
        while (delta < -half_period) {
            delta += period;
        }
    }

    return vdc_domain_clamp_i64_to_i32(delta);
}

static uint32_t vdc_domain_acquisition_window_width_ns(
    const vdc_tdma_schedule_profile_t *profile)
{
    if (profile == NULL || profile->period_ns == 0u) {
        return VDC_DOMAIN_DEFAULT_OBSERVATION_WIDTH_NS;
    }
    return profile->period_ns;
}

static bool vdc_domain_context_uses_acquisition_window(
    const vdc_domain_context_t *context)
{
    if (context == NULL || context->ready == 0u) {
        return false;
    }

    switch ((vdc_domain_lock_state_t)context->dpll.state) {
    case VDC_DOMAIN_LOCK_CHECKING:
    case VDC_DOMAIN_LOCK_INITIAL_SYNC:
    case VDC_DOMAIN_LOCK_FREQ_LOCK:
    case VDC_DOMAIN_LOCK_PHASE_LOCK:
    case VDC_DOMAIN_LOCK_RELOCKING:
        return true;
    default:
        return context->dpll.accepted_sample_count <
               context->servo.lock_sample_count;
    }
}

static int32_t vdc_domain_corrected_phase_error_ns(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    if (context == NULL || evidence == NULL) {
        return 0;
    }
    int32_t rate_correction_ns = 0;
    if (context->dpll.accepted_sample_count != 0u &&
        context->clock.valid != 0u &&
        context->clock.period_adjust_ppb != 0 &&
        evidence->observed_time_ns > context->clock.base_local_tick64) {
        const uint64_t elapsed_ns64 =
            evidence->observed_time_ns - context->clock.base_local_tick64;
        const uint32_t elapsed_ns = elapsed_ns64 > UINT32_MAX
            ? UINT32_MAX : (uint32_t)elapsed_ns64;
        rate_correction_ns = vdc_domain_ppb_elapsed_correction_ns(
            context->clock.period_adjust_ppb, elapsed_ns);
    }

    int64_t corrected =
        (int64_t)evidence->phase_error_ns +
        (int64_t)context->clock.phase_offset_ns +
        (int64_t)rate_correction_ns;
    if (context->schedule.period_ns != 0u) {
        const int64_t period = (int64_t)context->schedule.period_ns;
        const int64_t half_period = period / 2ll;
        while (corrected > half_period) {
            corrected -= period;
        }
        while (corrected < -half_period) {
            corrected += period;
        }
    }
    return vdc_domain_clamp_i64_to_i32(corrected);
}

static uint32_t vdc_domain_lock_acceptance_threshold_ns(
    const vdc_servo_profile_t *servo)
{
    uint32_t threshold_ns = 0u;
    uint32_t coarse_ns = VDC_DOMAIN_LOCK_TIER_COARSE_NS;

    if (servo == NULL) {
        return VDC_DOMAIN_LOCK_TIER_FINE_NS;
    }

    threshold_ns = servo->lock_acceptance_threshold_ns != 0u
        ? servo->lock_acceptance_threshold_ns
        : servo->offset_lock_threshold_ns;
    if (threshold_ns == 0u) {
        threshold_ns = VDC_DOMAIN_LOCK_TIER_FINE_NS;
    }

    if (servo->coarse_lock_threshold_ns != 0u) {
        coarse_ns = servo->coarse_lock_threshold_ns;
    }
    return threshold_ns > coarse_ns ? coarse_ns : threshold_ns;
}

static uint32_t vdc_domain_effective_lock_acceptance_threshold_ns(
    const vdc_domain_context_t *context)
{
    if (context == NULL) {
        return VDC_DOMAIN_LOCK_TIER_FINE_NS;
    }

    uint32_t threshold_ns =
        vdc_domain_lock_acceptance_threshold_ns(&context->servo);
    if ((context->path_delay.flags &
         VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) != 0u &&
        threshold_ns < VDC_DOMAIN_LOCK_TIER_COARSE_NS) {
        threshold_ns = VDC_DOMAIN_LOCK_TIER_COARSE_NS;
    }
    return threshold_ns;
}

static uint32_t vdc_domain_effective_outlier_threshold_ns(
    const vdc_domain_context_t *context)
{
    if (context == NULL || context->servo.outlier_threshold_ns == 0u) {
        return 0u;
    }

    uint32_t threshold_ns = context->servo.outlier_threshold_ns;
    const uint32_t provisional_threshold_ns =
        VDC_DOMAIN_LOCK_TIER_COARSE_NS + VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    if ((context->path_delay.flags &
         VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) != 0u &&
        threshold_ns < provisional_threshold_ns) {
        threshold_ns = provisional_threshold_ns;
    }
    return threshold_ns;
}

static uint32_t vdc_domain_lock_quality_tier_for_error(
    const vdc_servo_profile_t *servo,
    uint32_t error_ns)
{
    uint32_t fine_ns = VDC_DOMAIN_LOCK_TIER_FINE_NS;
    uint32_t debug_ns = VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    uint32_t coarse_ns = VDC_DOMAIN_LOCK_TIER_COARSE_NS;

    if (servo != NULL) {
        if (servo->offset_lock_threshold_ns != 0u) {
            fine_ns = servo->offset_lock_threshold_ns;
        }
        if (servo->debug_lock_threshold_ns != 0u) {
            debug_ns = servo->debug_lock_threshold_ns;
        }
        if (servo->coarse_lock_threshold_ns != 0u) {
            coarse_ns = servo->coarse_lock_threshold_ns;
        }
    }

    if (error_ns <= fine_ns) {
        return VDC_DOMAIN_LOCK_QUALITY_FINE_100NS;
    }
    if (error_ns <= debug_ns) {
        return VDC_DOMAIN_LOCK_QUALITY_DEBUG_1US;
    }
    if (error_ns <= coarse_ns) {
        return VDC_DOMAIN_LOCK_QUALITY_COARSE_10US;
    }
    return VDC_DOMAIN_LOCK_QUALITY_NONE;
}

static uint32_t vdc_domain_lock_quality_error_ns(
    const vdc_domain_context_t *context)
{
    uint32_t error_ns = 0u;

    if (context == NULL) {
        return 0u;
    }
    error_ns = context->quality.rms_offset_ns;
    if (vdc_domain_abs_i32(context->quality.last_offset_ns) > error_ns) {
        error_ns = vdc_domain_abs_i32(context->quality.last_offset_ns);
    }
    return error_ns;
}

static uint32_t vdc_domain_required_quality_samples(
    const vdc_servo_profile_t *servo)
{
    if (servo == NULL || servo->lock_sample_count == 0u) {
        return 1u;
    }
    return servo->lock_sample_count;
}

static uint32_t vdc_domain_phase_slew_limit_ns(
    const vdc_domain_context_t *context,
    uint32_t abs_phase_ns)
{
    if (context == NULL) {
        return 0u;
    }

    uint32_t fine_ns = context->servo.offset_lock_threshold_ns != 0u
        ? context->servo.offset_lock_threshold_ns
        : VDC_DOMAIN_LOCK_TIER_FINE_NS;
    uint32_t debug_ns = context->servo.debug_lock_threshold_ns != 0u
        ? context->servo.debug_lock_threshold_ns
        : VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    uint32_t base_step_ns = context->servo.step_threshold_ns;

    if (abs_phase_ns <= fine_ns) {
        return base_step_ns;
    }

    const bool acquisition_window =
        vdc_domain_context_uses_acquisition_window(context);
    if (!acquisition_window) {
        return base_step_ns;
    }

    uint32_t coarse_step_ns = context->servo.first_step_threshold_ns;
    if (coarse_step_ns < base_step_ns) {
        coarse_step_ns = base_step_ns;
    }
    if (coarse_step_ns == 0u) {
        coarse_step_ns = debug_ns;
    }
    return coarse_step_ns;
}

static int32_t vdc_domain_clamp_ppb(int64_t value, uint32_t limit_ppb)
{
    const int64_t limit = (int64_t)limit_ppb;
    if (value > limit) {
        return (int32_t)limit;
    }
    if (value < -limit) {
        return (int32_t)-limit;
    }
    return vdc_domain_clamp_i64_to_i32(value);
}

static int32_t vdc_domain_scale_q16_i32(int32_t value, int32_t gain_q16)
{
    const int64_t scaled =
        ((int64_t)value * (int64_t)gain_q16) / 65536ll;
    return vdc_domain_clamp_i64_to_i32(scaled);
}

static int32_t vdc_domain_negate_scaled_q16_i32(int32_t value, int32_t gain_q16)
{
    const int64_t scaled =
        -(((int64_t)value * (int64_t)gain_q16) / 65536ll);
    return vdc_domain_clamp_i64_to_i32(scaled);
}

static int32_t vdc_domain_slew_i32(int32_t current,
                                   int32_t target,
                                   uint32_t max_step)
{
    const int64_t delta = (int64_t)target - (int64_t)current;
    if (max_step == 0u || delta == 0ll) {
        return target;
    }
    if (delta > (int64_t)max_step) {
        return current + (int32_t)max_step;
    }
    if (delta < -(int64_t)max_step) {
        return current - (int32_t)max_step;
    }
    return target;
}

static uint64_t vdc_domain_rate_observation_min_ns(
    const vdc_domain_context_t *context)
{
    if (context == NULL) {
        return 0u;
    }

    uint64_t min_ns =
        (uint64_t)context->schedule.period_ns *
        (uint64_t)VDC_DOMAIN_RATE_MIN_OBSERVATION_CYCLES;
    const uint64_t servo_update_ns =
        (uint64_t)context->servo.update_period_us * 1000ull;
    if (servo_update_ns > min_ns) {
        min_ns = servo_update_ns;
    }
    return min_ns;
}

static uint32_t vdc_domain_frequency_slew_limit_ppb(
    const vdc_domain_context_t *context)
{
    if (context == NULL || context->servo.sanity_freq_limit_ppb == 0u) {
        return 0u;
    }

    uint32_t limit =
        context->servo.sanity_freq_limit_ppb / VDC_DOMAIN_RATE_SLEW_DIVISOR;
    if (limit < VDC_DOMAIN_RATE_MIN_SLEW_LIMIT_PPB) {
        limit = VDC_DOMAIN_RATE_MIN_SLEW_LIMIT_PPB;
    }
    if (limit > context->servo.sanity_freq_limit_ppb) {
        limit = context->servo.sanity_freq_limit_ppb;
    }
    return limit;
}

static uint64_t vdc_domain_evidence_time_ns(
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    if (evidence == NULL) {
        return 0u;
    }
    if (evidence->apply_time_ns != 0u) {
        return evidence->apply_time_ns;
    }
    if (evidence->done_time_ns != 0u) {
        return evidence->done_time_ns;
    }
    return evidence->observed_time_ns;
}

static void VDC_DOMAIN_TIME_CRITICAL(vdc_domain_refresh_quality_state)(
    vdc_domain_context_t *context)
{
    if (context == NULL) {
        return;
    }

    context->quality.valid = 1u;
    context->quality.lock_state = context->dpll.state;
    context->quality.lock_acceptance_threshold_ns =
        vdc_domain_effective_lock_acceptance_threshold_ns(context);
    context->quality.fine_lock_threshold_ns =
        context->servo.offset_lock_threshold_ns != 0u
            ? context->servo.offset_lock_threshold_ns
            : VDC_DOMAIN_LOCK_TIER_FINE_NS;
    context->quality.debug_lock_threshold_ns =
        context->servo.debug_lock_threshold_ns != 0u
            ? context->servo.debug_lock_threshold_ns
            : VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    context->quality.coarse_lock_threshold_ns =
        context->servo.coarse_lock_threshold_ns != 0u
            ? context->servo.coarse_lock_threshold_ns
            : VDC_DOMAIN_LOCK_TIER_COARSE_NS;
    const uint32_t required_quality_samples =
        vdc_domain_required_quality_samples(&context->servo);
    if (context->quality.accepted_sample_count == 0u) {
        context->quality.lock_quality_tier = VDC_DOMAIN_LOCK_QUALITY_NONE;
    } else if (context->quality.consecutive_fine_samples >=
               required_quality_samples) {
        context->quality.lock_quality_tier =
            VDC_DOMAIN_LOCK_QUALITY_FINE_100NS;
    } else if (context->quality.consecutive_debug_samples >=
               required_quality_samples) {
        context->quality.lock_quality_tier =
            VDC_DOMAIN_LOCK_QUALITY_DEBUG_1US;
    } else if (context->quality.consecutive_coarse_samples >=
               required_quality_samples) {
        context->quality.lock_quality_tier =
            VDC_DOMAIN_LOCK_QUALITY_COARSE_10US;
    } else {
        context->quality.lock_quality_tier =
            vdc_domain_lock_quality_tier_for_error(
                &context->servo,
                vdc_domain_lock_quality_error_ns(context));
    }
    if (context->dpll.state == VDC_DOMAIN_LOCK_FAULT) {
        context->quality.health_state = VDC_DOMAIN_HEALTH_FAULT;
    } else if (context->dpll.state == VDC_DOMAIN_LOCK_LOCKED &&
               context->gate.passed != 0u &&
               context->quality.lock_quality_tier >=
                   VDC_DOMAIN_LOCK_QUALITY_FINE_100NS &&
               context->quality.freshness_limit_us != 0u &&
               context->quality.last_sample_age_us <=
                   context->quality.freshness_limit_us) {
        context->quality.health_state = VDC_DOMAIN_HEALTH_HEALTHY;
    } else if (context->dpll.state == VDC_DOMAIN_LOCK_PHASE_LOCK ||
               context->dpll.state == VDC_DOMAIN_LOCK_FREQ_LOCK) {
        context->quality.health_state = VDC_DOMAIN_HEALTH_LOCK_CANDIDATE;
    } else if (context->dpll.state == VDC_DOMAIN_LOCK_OFF ||
               context->dpll.state == VDC_DOMAIN_LOCK_CHECKING ||
               context->dpll.state == VDC_DOMAIN_LOCK_INITIAL_SYNC ||
               context->dpll.state == VDC_DOMAIN_LOCK_RELOCKING) {
        context->quality.health_state = VDC_DOMAIN_HEALTH_CHECKING;
    } else {
        context->quality.health_state = VDC_DOMAIN_HEALTH_DEGRADED;
    }
}

static void vdc_domain_sync_dco_lock_state(vdc_domain_context_t *context)
{
    if (context == NULL ||
        context->dco.valid == 0u ||
        context->dco.lock_state == context->dpll.state) {
        return;
    }

    context->dco.lock_state = context->dpll.state;
    context->dco.dco_update_seq++;
    context->dpll.update_seq++;
}

static void vdc_domain_refresh_quality_age(vdc_domain_context_t *context,
                                           uint64_t now_ns)
{
    if (context == NULL || context->quality.last_sample_time_ns == 0u) {
        return;
    }
    const uint64_t age_ns =
        now_ns >= context->quality.last_sample_time_ns
            ? now_ns - context->quality.last_sample_time_ns
            : 0u;
    context->quality.last_sample_age_us =
        vdc_domain_saturate_u64_to_u32(age_ns / 1000ull);
    if (context->dpll.state == VDC_DOMAIN_LOCK_HOLDOVER) {
        context->dpll.holdover_age_us = context->quality.last_sample_age_us;
    }
    vdc_domain_refresh_quality_state(context);
}

static void vdc_domain_init_quality(vdc_domain_context_t *context)
{
    if (context == NULL) {
        return;
    }
    memset(&context->quality, 0, sizeof(context->quality));
    memset(&context->error_budget, 0, sizeof(context->error_budget));
    context->quality.valid = 1u;
    context->quality.health_state = VDC_DOMAIN_HEALTH_UNKNOWN;
    context->quality.lock_state = VDC_DOMAIN_LOCK_OFF;
    context->quality.freshness_limit_us =
        VDC_DOMAIN_DEFAULT_FRESHNESS_LIMIT_US;
    context->error_budget.valid = 1u;
    context->error_budget.holdover_drift_bound_ns_s =
        VDC_DOMAIN_DEFAULT_HOLDOVER_DRIFT_BOUND_NS_S;
}

static void vdc_domain_record_rejected_sample(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    const vdc_gate_result_t *gate)
{
    if (context == NULL || gate == NULL) {
        return;
    }
    context->quality.valid = 1u;
    context->quality.update_seq++;
    context->quality.rejected_sample_count = context->dpll.rejected_sample_count;
    context->quality.consecutive_good_samples = 0u;
    context->quality.consecutive_bad_samples++;
    context->quality.consecutive_coarse_samples = 0u;
    context->quality.consecutive_debug_samples = 0u;
    context->quality.consecutive_fine_samples = 0u;
    context->quality.last_reject_code = gate->reject_code;
    context->quality.gate_reject_code = gate->reject_code;
    context->quality.gate_reject_slot = gate->reject_slot;
    context->quality.gate_reject_evidence = gate->reject_evidence;
    if (evidence != NULL) {
        context->quality.last_sample_seq = evidence->sample_seq;
        context->quality.last_timestamp_source = evidence->timestamp_source;
        context->quality.last_timestamp_resolution_ns =
            evidence->timestamp_resolution_ns;
        context->quality.last_timestamp_flags = evidence->timestamp_flags;
    }
    vdc_domain_refresh_quality_state(context);
}

static void vdc_domain_reset_lock_acquisition(vdc_domain_context_t *context)
{
    if (context == NULL) {
        return;
    }

    context->dpll.accepted_sample_count = 0u;
    context->dpll.last_raw_phase_error_ns = 0;
    context->dpll.last_expected_window_start_ns = 0u;
    context->dpll.last_observed_time_ns = 0u;
    context->quality.accepted_sample_count = 0u;
    context->quality.consecutive_good_samples = 0u;
    context->quality.consecutive_coarse_samples = 0u;
    context->quality.consecutive_debug_samples = 0u;
    context->quality.consecutive_fine_samples = 0u;
}

static bool vdc_domain_reject_requires_reacquire(uint32_t reject_code)
{
    switch ((vdc_domain_gate_code_t)reject_code) {
    case VDC_DOMAIN_GATE_WINDOW_BOUND:
    case VDC_DOMAIN_GATE_SERVO_OUTLIER:
        return false;
    default:
        return true;
    }
}

static uint32_t vdc_domain_lock_state_after_reject(
    const vdc_domain_context_t *context,
    bool reacquire)
{
    if (context == NULL || context->ready == 0u) {
        return VDC_DOMAIN_LOCK_OFF;
    }
    if (reacquire) {
        return VDC_DOMAIN_LOCK_CHECKING;
    }

    const uint32_t reject_limit = context->servo.lock_sample_count != 0u
        ? context->servo.lock_sample_count : 1u;
    const uint32_t next_bad_count =
        context->quality.consecutive_bad_samples == UINT32_MAX
            ? UINT32_MAX
            : context->quality.consecutive_bad_samples + 1u;
    if (context->dpll.state == VDC_DOMAIN_LOCK_LOCKED &&
        next_bad_count < reject_limit) {
        return VDC_DOMAIN_LOCK_LOCKED;
    }
    return VDC_DOMAIN_LOCK_RELOCKING;
}

static void vdc_domain_record_accepted_sample(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    if (context == NULL || evidence == NULL) {
        return;
    }

    const uint32_t abs_phase = vdc_domain_abs_i32(evidence->phase_error_ns);
    const uint32_t root_distance =
        (evidence->delay_ns / 2u) +
        context->error_budget.dispersion_ns +
        abs_phase;

    context->quality.valid = 1u;
    context->quality.update_seq++;
    context->quality.accepted_sample_count = context->dpll.accepted_sample_count;
    context->quality.rejected_sample_count = context->dpll.rejected_sample_count;
    context->quality.consecutive_good_samples++;
    context->quality.consecutive_bad_samples = 0u;
    const uint32_t fine_ns =
        context->servo.offset_lock_threshold_ns != 0u
            ? context->servo.offset_lock_threshold_ns
            : VDC_DOMAIN_LOCK_TIER_FINE_NS;
    const uint32_t debug_ns =
        context->servo.debug_lock_threshold_ns != 0u
            ? context->servo.debug_lock_threshold_ns
            : VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    const uint32_t coarse_ns =
        context->servo.coarse_lock_threshold_ns != 0u
            ? context->servo.coarse_lock_threshold_ns
            : VDC_DOMAIN_LOCK_TIER_COARSE_NS;
    if (abs_phase <= fine_ns) {
        context->quality.consecutive_fine_samples++;
        context->quality.consecutive_debug_samples++;
        context->quality.consecutive_coarse_samples++;
    } else if (abs_phase <= debug_ns) {
        context->quality.consecutive_fine_samples = 0u;
        context->quality.consecutive_debug_samples++;
        context->quality.consecutive_coarse_samples++;
    } else if (abs_phase <= coarse_ns) {
        context->quality.consecutive_fine_samples = 0u;
        context->quality.consecutive_debug_samples = 0u;
        context->quality.consecutive_coarse_samples++;
    } else {
        context->quality.consecutive_fine_samples = 0u;
        context->quality.consecutive_debug_samples = 0u;
        context->quality.consecutive_coarse_samples = 0u;
    }
    context->quality.last_sample_seq = evidence->sample_seq;
    context->quality.last_reject_code = VDC_DOMAIN_GATE_PASS;
    context->quality.last_timestamp_source = evidence->timestamp_source;
    context->quality.last_timestamp_resolution_ns =
        evidence->timestamp_resolution_ns;
    context->quality.last_timestamp_flags = evidence->timestamp_flags;
    context->quality.last_sample_time_ns = vdc_domain_evidence_time_ns(evidence);
    context->quality.last_sample_age_us = 0u;
    context->quality.last_offset_ns = evidence->phase_error_ns;
    context->quality.rms_offset_ns = context->dpll.rms_offset_ns;
    context->quality.max_abs_offset_ns = context->dpll.max_abs_offset_ns;
    context->quality.last_jitter_ns = evidence->jitter_ns;
    context->quality.jitter_rms_ns =
        vdc_domain_avg_u32(context->quality.jitter_rms_ns, evidence->jitter_ns);
    context->quality.jitter_pk_ns = context->dpll.jitter_pk_ns;
    context->quality.gate_reject_code = VDC_DOMAIN_GATE_PASS;
    context->quality.gate_reject_slot = evidence->source_slot_id;
    context->quality.gate_reject_evidence = evidence->sample_seq;

    context->error_budget.valid = 1u;
    context->error_budget.update_seq++;
    context->error_budget.last_offset_ns = evidence->phase_error_ns;
    context->error_budget.rms_offset_ns = context->dpll.rms_offset_ns;
    context->error_budget.max_abs_offset_ns = context->dpll.max_abs_offset_ns;
    context->error_budget.path_delay_ns = evidence->delay_ns;
    context->error_budget.delay_stddev_ns =
        vdc_domain_avg_u32(context->error_budget.delay_stddev_ns,
                           evidence->jitter_ns);
    context->error_budget.dispersion_ns = evidence->jitter_ns;
    context->error_budget.root_distance_ns = root_distance;
    vdc_domain_refresh_quality_state(context);
}

static void vdc_domain_update_clock_from_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    int32_t input_residual_ns)
{
    if (context == NULL || evidence == NULL) {
        return;
    }

    const uint32_t next_dco_seq = context->dco.dco_update_seq + 1u;
    int32_t frequency_error_ppb = context->dpll.last_frequency_error_ppb;
    int32_t phase_rate_pull_ppb = 0;
    bool update_rate_anchor = context->dpll.accepted_sample_count <= 1u;
    if (context->dpll.accepted_sample_count > 1u &&
        evidence->expected_window_start_ns >
            context->dpll.last_expected_window_start_ns &&
        evidence->observed_time_ns > context->dpll.last_observed_time_ns) {
        const uint64_t expected_delta =
            evidence->expected_window_start_ns -
            context->dpll.last_expected_window_start_ns;
        if (expected_delta >= vdc_domain_rate_observation_min_ns(context)) {
            int64_t delta_error =
                (int64_t)evidence->phase_error_ns -
                (int64_t)context->dpll.last_raw_phase_error_ns;
            if (context->schedule.period_ns != 0u) {
                const int64_t period = (int64_t)context->schedule.period_ns;
                const int64_t half_period = period / 2ll;
                while (delta_error > half_period) {
                    delta_error -= period;
                }
                while (delta_error < -half_period) {
                    delta_error += period;
                }
            }
            const int32_t target_frequency_error_ppb =
                vdc_domain_scaled_ratio_clamped_i32(
                    delta_error,
                    1000000000u,
                    expected_delta,
                    context->servo.sanity_freq_limit_ppb);
            frequency_error_ppb =
                vdc_domain_slew_i32(
                    context->dpll.last_frequency_error_ppb,
                    target_frequency_error_ppb,
                    vdc_domain_frequency_slew_limit_ppb(context));
            update_rate_anchor = true;
        }
    } else if (context->dpll.accepted_sample_count > 1u) {
        update_rate_anchor = true;
    }

    if (context->servo.ki_q16 != 0 &&
        context->servo.update_period_us != 0u &&
        context->dpll.accepted_sample_count >= context->servo.lock_sample_count) {
        phase_rate_pull_ppb = vdc_domain_scale_q16_i32(
            vdc_domain_scaled_ratio_clamped_i32(
                input_residual_ns,
                1000000u,
                context->servo.update_period_us,
                context->servo.sanity_freq_limit_ppb),
            context->servo.ki_q16);
    }

    const int32_t phase_target_ns =
        vdc_domain_clamp_i64_to_i32(
            (int64_t)context->clock.phase_offset_ns +
            (int64_t)vdc_domain_negate_scaled_q16_i32(
                input_residual_ns,
                context->servo.kp_q16));
    const uint32_t abs_phase_ns = vdc_domain_abs_i32(input_residual_ns);
    const bool allow_first_step =
        context->dpll.accepted_sample_count <= 1u &&
        abs_phase_ns <= context->servo.first_step_threshold_ns;
    const uint32_t phase_slew_limit_ns =
        vdc_domain_phase_slew_limit_ns(context, abs_phase_ns);
    const int32_t phase_offset_ns = allow_first_step
        ? phase_target_ns
        : vdc_domain_slew_i32(context->clock.phase_offset_ns,
                              phase_target_ns,
                              phase_slew_limit_ns);
    const int32_t period_adjust_ppb =
        -vdc_domain_clamp_ppb((int64_t)frequency_error_ppb +
                                  (int64_t)phase_rate_pull_ppb,
                              context->servo.sanity_freq_limit_ppb);

    context->dpll.last_frequency_error_ppb = frequency_error_ppb;
    if (update_rate_anchor) {
        context->dpll.last_raw_phase_error_ns = evidence->phase_error_ns;
        context->dpll.last_expected_window_start_ns =
            evidence->expected_window_start_ns;
        context->dpll.last_observed_time_ns = evidence->observed_time_ns;
    }

    context->clock.valid = 1u;
    context->clock.model_seq++;
    context->clock.epoch_id = context->schedule.schedule_epoch;
    context->clock.base_local_tick64 = evidence->observed_time_ns;
    context->clock.base_vdc_time64_ns = evidence->observed_time_ns;
    context->clock.nominal_period_ns = context->schedule.period_ns;
    context->clock.phase_offset_ns = phase_offset_ns;
    context->clock.period_adjust_ppb = period_adjust_ppb;
    context->clock.slew_limit_ppb = context->servo.sanity_freq_limit_ppb;
    context->clock.tdma_schedule_crc32 = context->schedule.schedule_crc32;
    context->clock.servo_profile_crc32 = context->servo.servo_profile_crc32;

    context->dco.valid = 1u;
    context->dco.dco_update_seq = next_dco_seq;
    context->dco.source_model_seq = context->clock.model_seq;
    context->dco.epoch_id = context->clock.epoch_id;
    context->dco.run_id = context->clock.run_id;
    context->dco.base_local_tick64 = context->clock.base_local_tick64;
    context->dco.base_vdc_time64_ns = context->clock.base_vdc_time64_ns;
    context->dco.nominal_period_ns = context->clock.nominal_period_ns;
    context->dco.period_adjust_ppb = context->clock.period_adjust_ppb;
    context->dco.phase_offset_ns = context->clock.phase_offset_ns;
    context->dco.slew_limit_ppb = context->clock.slew_limit_ppb;
    context->dco.lock_state = context->dpll.state;
    context->dco.tdma_schedule_crc32 = context->clock.tdma_schedule_crc32;
    context->dco.servo_profile_crc32 = context->clock.servo_profile_crc32;
    context->dpll.update_seq++;

    context->error_budget.freq_offset_ppb = -period_adjust_ppb;
    context->error_budget.freq_skew_ppb =
        vdc_domain_abs_i32(-period_adjust_ppb);
}

void vdc_domain_default_schedule(vdc_tdma_schedule_profile_t *profile,
                                 uint32_t local_slot_id,
                                 uint32_t reference_slot_id)
{
    if (profile == NULL) {
        return;
    }
    if (!vdc_domain_default_schedule_for_topology(
            profile,
            local_slot_id,
            reference_slot_id,
            VDC_DOMAIN_DEFAULT_ACTIVE_NODE_COUNT)) {
        memset(profile, 0, sizeof(*profile));
    }
}

bool vdc_domain_default_schedule_for_topology(
    vdc_tdma_schedule_profile_t *profile,
    uint32_t local_slot_id,
    uint32_t reference_slot_id,
    uint32_t node_count)
{
    if (profile == NULL || node_count < 2u ||
        node_count > VDC_DOMAIN_NODE_COUNT || local_slot_id >= node_count ||
        reference_slot_id >= node_count) {
        return false;
    }

    memset(profile, 0, sizeof(*profile));
    profile->enabled = 1u;
    profile->schedule_version = 1u;
    profile->schedule_epoch = 1u;
    profile->period_ns = VDC_DOMAIN_DEFAULT_PERIOD_NS;
    profile->observation_window_offset_ns = 0u;
    profile->observation_window_width_ns = VDC_DOMAIN_DEFAULT_OBSERVATION_WIDTH_NS;
    profile->refmem_data_window_offset_ns = VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_OFFSET_NS;
    profile->refmem_data_window_width_ns = VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_WIDTH_NS;
    profile->idle_beacon_window_offset_ns = VDC_DOMAIN_DEFAULT_IDLE_WINDOW_OFFSET_NS;
    profile->idle_beacon_window_width_ns = VDC_DOMAIN_DEFAULT_IDLE_WINDOW_WIDTH_NS;
    profile->guard_before_ns = VDC_DOMAIN_DEFAULT_GUARD_NS;
    profile->guard_after_ns = VDC_DOMAIN_DEFAULT_GUARD_NS;
    profile->reference_slot_id = reference_slot_id;
    profile->local_slot_id = local_slot_id;
    if (!tdma_ring_profile_default(&profile->ring_binding,
                                   profile->local_slot_id,
                                   profile->reference_slot_id,
                                   node_count)) {
        memset(profile, 0, sizeof(*profile));
        return false;
    }
    profile->schedule_crc32 = vdc_domain_schedule_crc32(profile);
    return true;
}

void vdc_domain_set_schedule_local_slot(vdc_domain_context_t *context,
                                        uint32_t local_slot_id)
{
    if (context == NULL) {
        return;
    }
    (void)vdc_domain_set_schedule_ring_topology(
        context,
        local_slot_id,
        context->schedule.reference_slot_id,
        context->schedule.ring_binding.node_count);
}

bool vdc_domain_set_schedule_ring_topology(vdc_domain_context_t *context,
                                           uint32_t local_slot_id,
                                           uint32_t reference_slot_id,
                                           uint32_t node_count)
{
    if (context == NULL || node_count < 2u ||
        node_count > VDC_DOMAIN_NODE_COUNT || local_slot_id >= node_count ||
        reference_slot_id >= node_count) {
        return false;
    }
    vdc_tdma_schedule_profile_t updated = context->schedule;
    updated.operating_profile_crc32 = 0u;
    updated.local_slot_id = local_slot_id;
    updated.reference_slot_id = reference_slot_id;
    if (!tdma_ring_profile_default(&updated.ring_binding,
                                   updated.local_slot_id,
                                   updated.reference_slot_id,
                                   node_count)) {
        return false;
    }
    updated.schedule_crc32 = vdc_domain_schedule_crc32(&updated);
    context->schedule = updated;
    return true;
}

bool vdc_domain_build_tdma_runtime_schedule(
    const vdc_tdma_schedule_profile_t *base,
    const vdc_tdma_runtime_binding_t *binding,
    vdc_tdma_schedule_profile_t *runtime_schedule)
{
    if (base == NULL || binding == NULL || runtime_schedule == NULL ||
        binding->node_count < 2u ||
        binding->node_count > VDC_DOMAIN_NODE_COUNT ||
        binding->local_slot_id >= binding->node_count ||
        binding->reference_slot_id >= binding->node_count ||
        binding->ring_profile_crc32 == 0u ||
        binding->operating_profile_crc32 == 0u ||
        binding->cycle_period_ns == 0u ||
        binding->effective_schedule_crc32 == 0u) {
        return false;
    }

    vdc_tdma_schedule_profile_t updated = *base;
    updated.operating_profile_crc32 = 0u;
    updated.local_slot_id = binding->local_slot_id;
    updated.reference_slot_id = binding->reference_slot_id;
    if (updated.period_ns != binding->cycle_period_ns ||
        !tdma_ring_profile_default(&updated.ring_binding,
                                   binding->local_slot_id,
                                   binding->reference_slot_id,
                                   binding->node_count) ||
        updated.ring_binding.profile_crc32 != binding->ring_profile_crc32) {
        return false;
    }
    updated.operating_profile_crc32 = binding->operating_profile_crc32;
    updated.schedule_crc32 = vdc_domain_schedule_crc32(&updated);
    if (updated.schedule_crc32 != binding->effective_schedule_crc32 ||
        !vdc_domain_schedule_validate(&updated)) {
        return false;
    }
    *runtime_schedule = updated;
    return true;
}

void vdc_domain_default_servo(vdc_servo_profile_t *profile)
{
    if (profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    profile->enabled = 1u;
    profile->servo_type = 1u;
    profile->kp_q16 = 65536;
    profile->ki_q16 = 4096;
    profile->update_period_us = 1000u;
    profile->first_step_threshold_ns = 100000u;
    profile->step_threshold_ns = 10000u;
    profile->sanity_freq_limit_ppb = VDC_DOMAIN_DEFAULT_SANITY_FREQ_LIMIT_PPB;
    profile->offset_lock_threshold_ns = VDC_DOMAIN_LOCK_TIER_FINE_NS;
    profile->debug_lock_threshold_ns = VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    profile->coarse_lock_threshold_ns = VDC_DOMAIN_LOCK_TIER_COARSE_NS;
    profile->lock_acceptance_threshold_ns = VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    profile->lock_sample_count = 4u;
    profile->outlier_threshold_ns = 10000u;
    profile->reset_policy = 0u;
    profile->servo_profile_crc32 = VDC_DOMAIN_DEFAULT_SERVO_PROFILE_CRC32;
}

uint32_t vdc_domain_ring_profile_crc32(const vdc_tdma_schedule_profile_t *profile)
{
    if (profile == NULL) {
        return 0u;
    }
    return tdma_ring_profile_crc32(&profile->ring_binding);
}

uint32_t vdc_domain_schedule_crc32(const vdc_tdma_schedule_profile_t *profile)
{
    uint32_t hash = VDC_DOMAIN_CRC_OFFSET;
    if (profile == NULL) {
        return 0u;
    }

    hash = vdc_domain_hash_u32(hash, profile->enabled);
    hash = vdc_domain_hash_u32(hash, profile->schedule_version);
    hash = vdc_domain_hash_u32(hash, profile->schedule_epoch);
    hash = vdc_domain_hash_u32(hash, profile->period_ns);
    hash = vdc_domain_hash_u32(hash, profile->observation_window_offset_ns);
    hash = vdc_domain_hash_u32(hash, profile->observation_window_width_ns);
    hash = vdc_domain_hash_u32(hash, profile->refmem_data_window_offset_ns);
    hash = vdc_domain_hash_u32(hash, profile->refmem_data_window_width_ns);
    hash = vdc_domain_hash_u32(hash, profile->idle_beacon_window_offset_ns);
    hash = vdc_domain_hash_u32(hash, profile->idle_beacon_window_width_ns);
    hash = vdc_domain_hash_u32(hash, profile->guard_before_ns);
    hash = vdc_domain_hash_u32(hash, profile->guard_after_ns);
    hash = vdc_domain_hash_u32(hash, profile->reference_slot_id);
    /* The schedule CRC identifies the ring-wide schedule (windows, period,
     * reference and ring topology). local_slot_id is the per-board ring
     * identity and must NOT be part of it, otherwise two boards of the same
     * ring compute different schedule CRCs and reject each other's TDMA
     * frames on the wire (P0.5-3 two-board resident ring). */
    hash = vdc_domain_hash_u32(hash, profile->ring_binding.profile_crc32);
    if (profile->operating_profile_crc32 != 0u) {
        return tdma_operating_profile_compose_schedule_crc32(
            hash, profile->operating_profile_crc32);
    }
    return hash;
}

bool vdc_domain_schedule_validate(const vdc_tdma_schedule_profile_t *profile)
{
    if (profile == NULL ||
        profile->enabled == 0u ||
        profile->period_ns == 0u ||
        profile->reference_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        profile->local_slot_id >= VDC_DOMAIN_NODE_COUNT) {
        return false;
    }
    if (!tdma_ring_profile_validate(&profile->ring_binding, NULL) ||
        profile->local_slot_id != profile->ring_binding.local_index ||
        profile->reference_slot_id != profile->ring_binding.reference_index) {
        return false;
    }
    if (!vdc_domain_range_in_period(profile->observation_window_offset_ns,
                                    profile->observation_window_width_ns,
                                    profile->period_ns) ||
        !vdc_domain_range_in_period(profile->refmem_data_window_offset_ns,
                                    profile->refmem_data_window_width_ns,
                                    profile->period_ns) ||
        !vdc_domain_range_in_period(profile->idle_beacon_window_offset_ns,
                                    profile->idle_beacon_window_width_ns,
                                    profile->period_ns)) {
        return false;
    }
    if (vdc_domain_guarded_observation_overlaps(
            profile->observation_window_offset_ns,
            profile->observation_window_width_ns,
            profile->guard_before_ns,
            profile->guard_after_ns,
            profile->refmem_data_window_offset_ns,
            profile->refmem_data_window_width_ns) ||
        vdc_domain_guarded_observation_overlaps(
            profile->observation_window_offset_ns,
            profile->observation_window_width_ns,
            profile->guard_before_ns,
            profile->guard_after_ns,
            profile->idle_beacon_window_offset_ns,
            profile->idle_beacon_window_width_ns) ||
        vdc_domain_ranges_overlap(profile->refmem_data_window_offset_ns,
                                  profile->refmem_data_window_width_ns,
                                  profile->idle_beacon_window_offset_ns,
                                  profile->idle_beacon_window_width_ns)) {
        return false;
    }
    return profile->schedule_crc32 == vdc_domain_schedule_crc32(profile);
}

static bool vdc_domain_active_schedule_validate(
    const vdc_tdma_schedule_profile_t *profile)
{
    if (profile == NULL || profile->enabled == 0u ||
        profile->period_ns == 0u || profile->schedule_crc32 == 0u ||
        profile->observation_window_width_ns == 0u ||
        profile->observation_window_offset_ns >= profile->period_ns ||
        profile->observation_window_width_ns >
            profile->period_ns - profile->observation_window_offset_ns ||
        profile->ring_binding.node_count < 2u ||
        profile->ring_binding.node_count > VDC_DOMAIN_NODE_COUNT ||
        profile->local_slot_id >= profile->ring_binding.node_count ||
        profile->reference_slot_id >= profile->ring_binding.node_count ||
        profile->local_slot_id != profile->ring_binding.local_index ||
        profile->reference_slot_id != profile->ring_binding.reference_index) {
        return false;
    }
    return true;
}

void vdc_domain_default_clock_model(vdc_clock_model_t *model,
                                    uint32_t epoch_id,
                                    uint32_t run_id,
                                    uint64_t base_local_tick64,
                                    uint64_t base_vdc_time64_ns,
                                    uint32_t schedule_crc32)
{
    if (model == NULL) {
        return;
    }

    memset(model, 0, sizeof(*model));
    model->valid = 1u;
    model->model_seq = 1u;
    model->epoch_id = epoch_id;
    model->run_id = run_id;
    model->base_local_tick64 = base_local_tick64;
    model->base_vdc_time64_ns = base_vdc_time64_ns;
    model->nominal_period_ns = VDC_DOMAIN_DEFAULT_PERIOD_NS;
    model->period_adjust_ppb = 0;
    model->phase_offset_ns = 0;
    model->slew_limit_ppb = VDC_DOMAIN_DEFAULT_SANITY_FREQ_LIMIT_PPB;
    model->tdma_schedule_crc32 = schedule_crc32;
    model->servo_profile_crc32 = VDC_DOMAIN_DEFAULT_SERVO_PROFILE_CRC32;
}

void vdc_domain_default_dco_control(vdc_dco_control_t *dco,
                                    const vdc_clock_model_t *model,
                                    uint32_t lock_state)
{
    if (dco == NULL) {
        return;
    }

    memset(dco, 0, sizeof(*dco));
    if (model == NULL || model->valid == 0u) {
        return;
    }

    dco->valid = 1u;
    dco->dco_update_seq = 1u;
    dco->source_model_seq = model->model_seq;
    dco->epoch_id = model->epoch_id;
    dco->run_id = model->run_id;
    dco->base_local_tick64 = model->base_local_tick64;
    dco->base_vdc_time64_ns = model->base_vdc_time64_ns;
    dco->nominal_period_ns = model->nominal_period_ns;
    dco->period_adjust_ppb = model->period_adjust_ppb;
    dco->phase_offset_ns = model->phase_offset_ns;
    dco->slew_limit_ppb = model->slew_limit_ppb;
    dco->lock_state = lock_state;
    dco->tdma_schedule_crc32 = model->tdma_schedule_crc32;
    dco->servo_profile_crc32 = model->servo_profile_crc32;
}

uint32_t vdc_domain_path_delay_table_crc32(
    const vdc_path_delay_table_t *table)
{
    uint32_t hash = VDC_DOMAIN_CRC_OFFSET;
    if (table == NULL) {
        return 0u;
    }

    hash = vdc_domain_hash_u32(hash, table->valid);
    hash = vdc_domain_hash_u32(hash, table->version);
    hash = vdc_domain_hash_u32(hash, table->update_seq);
    hash = vdc_domain_hash_u32(hash, table->entry_count);
    hash = vdc_domain_hash_u32(hash, table->schedule_crc32);
    hash = vdc_domain_hash_u32(hash, table->calibration_generation);
    hash = vdc_domain_hash_u32(hash, table->topology_generation);
    hash = vdc_domain_hash_u32(hash, table->bias_generation);
    hash = vdc_domain_hash_u32(hash, table->freshness_us);
    hash = vdc_domain_hash_u32(hash, table->flags);
    for (uint32_t i = 0u; i < VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT; i++) {
        const vdc_path_delay_entry_t *entry = &table->entries[i];
        hash = vdc_domain_hash_u32(hash, entry->valid);
        hash = vdc_domain_hash_u32(hash, entry->source_slot_id);
        hash = vdc_domain_hash_u32(hash, entry->reference_slot_id);
        hash = vdc_domain_hash_u32(hash, entry->direction);
        hash = vdc_domain_hash_u32(hash, entry->delay_ns);
        hash = vdc_domain_hash_u32(hash, entry->jitter_ns);
        hash = vdc_domain_hash_u32(hash, entry->stddev_ns);
        hash = vdc_domain_hash_u32(hash, entry->cal_crc32);
        hash = vdc_domain_hash_u32(hash, entry->freshness_us);
        hash = vdc_domain_hash_u32(hash, entry->writer);
        hash = vdc_domain_hash_u32(hash, entry->update_seq);
    }
    hash = vdc_domain_hash_u32(hash, table->observation_matrix.valid);
    hash = vdc_domain_hash_u32(hash, table->observation_matrix.node_count);
    hash = vdc_domain_hash_u32(hash, table->observation_matrix.entry_count);
    for (uint32_t i = 0u;
         i < VDC_DOMAIN_OBSERVATION_PATH_MATRIX_BITMAP_WORD_COUNT; i++) {
        hash = vdc_domain_hash_u32(
            hash, table->observation_matrix.valid_bitmap[i]);
    }
    for (uint32_t i = 0u;
         i < VDC_DOMAIN_OBSERVATION_PATH_MATRIX_ENTRY_COUNT; i++) {
        hash = vdc_domain_hash_u32(hash,
                                   table->observation_matrix.delay_ns[i]);
    }
    return hash;
}

void vdc_domain_default_path_delay_table(
    vdc_path_delay_table_t *table,
    const vdc_tdma_schedule_profile_t *schedule)
{
    if (table == NULL || schedule == NULL) {
        return;
    }

    memset(table, 0, sizeof(*table));
    /* A zero-delay table is a staging placeholder, never DPLL input. */
    table->valid = 0u;
    table->version = VDC_DOMAIN_PATH_DELAY_TABLE_VERSION;
    table->update_seq = 1u;
    table->entry_count = 0u;
    table->schedule_crc32 = schedule->schedule_crc32;
    table->observation_matrix.valid = 0u;
    table->observation_matrix.node_count = 0u;
    table->observation_matrix.entry_count = 0u;
    for (uint32_t i = 0u; i < VDC_DOMAIN_NODE_COUNT; i++) {
        vdc_path_delay_entry_t *entry = &table->entries[i];
        entry->valid = 0u;
        entry->source_slot_id = i;
        entry->reference_slot_id = schedule->reference_slot_id;
        entry->direction = 0u;
        entry->delay_ns = 0u;
        entry->jitter_ns = 0u;
        entry->stddev_ns = 0u;
        entry->cal_crc32 = schedule->schedule_crc32;
        entry->freshness_us = 0u;
        entry->writer = schedule->reference_slot_id;
        entry->update_seq = 1u;
    }
    table->table_crc32 = vdc_domain_path_delay_table_crc32(table);
}

static bool vdc_domain_path_delay_table_validate_shape(
    const vdc_path_delay_table_t *table)
{
    uint32_t valid_count = 0u;
    if (table == NULL ||
        table->valid == 0u ||
        table->version != VDC_DOMAIN_PATH_DELAY_TABLE_VERSION ||
        table->entry_count > VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT ||
        table->schedule_crc32 == 0u ||
        table->entry_count == 0u ||
        table->calibration_generation == 0u ||
        table->topology_generation == 0u ||
        table->freshness_us == 0u ||
        table->table_crc32 != vdc_domain_path_delay_table_crc32(table)) {
        return false;
    }

    if ((table->flags & VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID) != 0u) {
        const vdc_observation_path_matrix_t *matrix =
            &table->observation_matrix;
        if (matrix->valid == 0u || matrix->node_count < 2u ||
            matrix->node_count > VDC_DOMAIN_NODE_COUNT ||
            matrix->entry_count != matrix->node_count *
                                        matrix->node_count) {
            return false;
        }
        for (uint32_t source = 0u; source < VDC_DOMAIN_NODE_COUNT; source++) {
            for (uint32_t reference = 0u;
                 reference < VDC_DOMAIN_NODE_COUNT; reference++) {
                const uint32_t index = source * VDC_DOMAIN_NODE_COUNT +
                                       reference;
                const bool present =
                    (matrix->valid_bitmap[index / 32u] &
                     (1u << (index % 32u))) != 0u;
                /* The diagonal is the complete physical loop return path
                 * used by the reference node.  Off-diagonal entries are the
                 * directed path from the origin/reference node to the local
                 * observing node.  Every node therefore has one immutable
                 * matrix entry, including the reference. */
                const bool expected = source < matrix->node_count &&
                                      reference < matrix->node_count;
                if (present != expected) {
                    return false;
                }
            }
        }
    } else if (table->observation_matrix.valid != 0u ||
               table->observation_matrix.entry_count != 0u) {
        return false;
    }

    for (uint32_t i = 0u; i < VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT; i++) {
        const vdc_path_delay_entry_t *entry = &table->entries[i];
        if (entry->valid == 0u) {
            continue;
        }
        if (entry->source_slot_id >= VDC_DOMAIN_NODE_COUNT ||
            entry->reference_slot_id >= VDC_DOMAIN_NODE_COUNT) {
            return false;
        }
        for (uint32_t j = i + 1u; j < VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT; j++) {
            const vdc_path_delay_entry_t *other = &table->entries[j];
            if (other->valid != 0u &&
                other->source_slot_id == entry->source_slot_id &&
                other->reference_slot_id == entry->reference_slot_id &&
                other->direction == entry->direction) {
                return false;
            }
        }
        valid_count++;
    }
    return valid_count == table->entry_count;
}

bool vdc_domain_path_delay_table_validate(
    const vdc_path_delay_table_t *table)
{
    const uint32_t required = VDC_PATH_DELAY_FLAG_ACCEPTED |
                              VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
                              VDC_PATH_DELAY_FLAG_BIAS_VALID |
                              VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;
    return vdc_domain_path_delay_table_validate_shape(table) &&
           table->bias_generation != 0u &&
           (table->flags & required) == required &&
           (table->flags & VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) == 0u;
}

bool vdc_domain_path_delay_table_validate_provisional(
    const vdc_path_delay_table_t *table)
{
    const uint32_t forbidden = VDC_PATH_DELAY_FLAG_ACCEPTED |
                               VDC_PATH_DELAY_FLAG_BIAS_VALID |
                               VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH;
    return vdc_domain_path_delay_table_validate_shape(table) &&
           table->bias_generation == 0u &&
           (table->flags & VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) != 0u &&
           (table->flags & VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED) != 0u &&
           (table->flags & VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID) != 0u &&
           (table->flags & forbidden) == 0u;
}

bool vdc_domain_load_observation_path_matrix(
    vdc_path_delay_table_t *table,
    uint32_t node_count)
{
    if (table == NULL || node_count < 2u ||
        node_count > VDC_DOMAIN_NODE_COUNT ||
        table->entry_count != node_count) {
        return false;
    }

    vdc_observation_path_matrix_t *matrix = &table->observation_matrix;
    memset(matrix, 0, sizeof(*matrix));
    table->flags &= ~VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID;

    uint32_t source_bitmap = 0u;
    for (uint32_t i = 0u; i < VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT; i++) {
        const vdc_path_delay_entry_t *link = &table->entries[i];
        if (link->valid == 0u) {
            continue;
        }
        if (link->source_slot_id >= node_count ||
            link->reference_slot_id >= node_count ||
            link->source_slot_id == link->reference_slot_id ||
            (source_bitmap & (1u << link->source_slot_id)) != 0u) {
            return false;
        }
        source_bitmap |= 1u << link->source_slot_id;
    }
    if (source_bitmap != ((1u << node_count) - 1u)) {
        return false;
    }

    matrix->valid = 1u;
    matrix->node_count = node_count;
    /* Include the diagonal: source==reference denotes one complete loop
     * traversal, not a zero-length path.  This lets the reference node feed
     * the same DPLL observer/servo path as every forward node. */
    matrix->entry_count = node_count * node_count;

    for (uint32_t source = 0u; source < node_count; source++) {
        for (uint32_t reference = 0u; reference < node_count; reference++) {
            uint32_t current = reference;
            uint32_t visited_mask = 0u;
            uint64_t total_delay_ns = 0ull;
            bool reached_source = false;
            for (uint32_t hop = 0u; hop < node_count; hop++) {
                if (current >= node_count ||
                    (visited_mask & (1u << current)) != 0u) {
                    return false;
                }
                visited_mask |= 1u << current;

                const vdc_path_delay_entry_t *matched = NULL;
                for (uint32_t i = 0u; i < VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT;
                     i++) {
                    const vdc_path_delay_entry_t *candidate =
                        &table->entries[i];
                    if (candidate->valid != 0u &&
                        candidate->source_slot_id == current) {
                        if (matched != NULL ||
                            candidate->reference_slot_id >= node_count) {
                            return false;
                        }
                        matched = candidate;
                    }
                }
                if (matched == NULL ||
                    UINT64_MAX - total_delay_ns < matched->delay_ns) {
                    return false;
                }
                total_delay_ns += matched->delay_ns;
                current = matched->reference_slot_id;
                /* For an off-diagonal observation, stop when the directed
                 * path reaches the local source.  For the diagonal, require
                 * one complete traversal and stop only after returning to
                 * the origin/reference node. */
                if (current == source &&
                    (source != reference || hop + 1u == node_count)) {
                    reached_source = true;
                    break;
                }
            }
            if (!reached_source || total_delay_ns > UINT32_MAX) {
                return false;
            }

            const uint32_t index = source * VDC_DOMAIN_NODE_COUNT + reference;
            matrix->delay_ns[index] = (uint32_t)total_delay_ns;
            matrix->valid_bitmap[index / 32u] |= 1u << (index % 32u);
        }
    }

    table->flags |= VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID;
    return true;
}

bool vdc_domain_path_delay_lookup(const vdc_path_delay_table_t *table,
                                  uint32_t source_slot_id,
                                  uint32_t reference_slot_id,
                                  vdc_path_delay_entry_t *entry)
{
    if (!vdc_domain_path_delay_table_validate(table) ||
        source_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        reference_slot_id >= VDC_DOMAIN_NODE_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT; i++) {
        const vdc_path_delay_entry_t *candidate = &table->entries[i];
        if (candidate->valid != 0u &&
            candidate->source_slot_id == source_slot_id &&
            candidate->reference_slot_id == reference_slot_id) {
            if (entry != NULL) {
                *entry = *candidate;
            }
            return true;
        }
    }
    return false;
}

void vdc_domain_default_timestamp_dictionary(
    vdc_timestamp_dictionary_t *dictionary,
    const vdc_tdma_schedule_profile_t *schedule)
{
    if (dictionary == NULL || schedule == NULL) {
        return;
    }

    vdc_timestamp_dictionary_init(dictionary, schedule->schedule_crc32);
    dictionary->entry_count = 2u;
    for (uint32_t i = 0u; i < dictionary->entry_count; i++) {
        vdc_timestamp_dictionary_entry_t *entry = &dictionary->entries[i];
        entry->valid = 1u;
        entry->event_id = i + 1u;
        entry->source_slot_id = schedule->local_slot_id;
        entry->reference_slot_id = schedule->reference_slot_id;
        entry->source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
        entry->resolution_ns = VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS;
        entry->default_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
        entry->port_id = 0u;
        entry->signal_id = i + 1u;
        entry->payload_class = VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE;
    }
    dictionary->dictionary_crc32 =
        vdc_timestamp_dictionary_crc32(dictionary);
}

bool vdc_domain_clock_model_local_to_vdc_ns(const vdc_clock_model_t *model,
                                            uint64_t local_tick64,
                                            uint64_t *vdc_time64_ns)
{
    if (model == NULL || vdc_time64_ns == NULL || model->valid == 0u) {
        return false;
    }

    if (local_tick64 < model->base_local_tick64) {
        return false;
    }

    const uint64_t delta = local_tick64 - model->base_local_tick64;
    const int64_t correction =
        ((int64_t)delta * (int64_t)model->period_adjust_ppb) / 1000000000ll;
    const int64_t signed_phase = (int64_t)model->phase_offset_ns;
    const int64_t signed_adjust = correction + signed_phase;
    const uint64_t base_time = model->base_vdc_time64_ns + delta;
    if (signed_adjust < 0 && (uint64_t)(-signed_adjust) > base_time) {
        *vdc_time64_ns = 0u;
    } else if (signed_adjust < 0) {
        *vdc_time64_ns = base_time - (uint64_t)(-signed_adjust);
    } else if (UINT64_MAX - base_time < (uint64_t)signed_adjust) {
        *vdc_time64_ns = UINT64_MAX;
    } else {
        *vdc_time64_ns = base_time + (uint64_t)signed_adjust;
    }
    return true;
}

bool vdc_domain_dco_control_validate(const vdc_tdma_schedule_profile_t *schedule,
                                     const vdc_servo_profile_t *servo,
                                     const vdc_dco_control_t *dco)
{
    if (schedule == NULL || servo == NULL || dco == NULL ||
        dco->valid == 0u ||
        dco->nominal_period_ns == 0u ||
        dco->slew_limit_ppb > servo->sanity_freq_limit_ppb ||
        dco->tdma_schedule_crc32 != schedule->schedule_crc32 ||
        dco->servo_profile_crc32 != servo->servo_profile_crc32 ||
        dco->lock_state > VDC_DOMAIN_LOCK_FAULT) {
        return false;
    }
    return true;
}

static bool vdc_domain_validate_tdma_timestamp_evidence_window(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool require_dpll_eligible,
    uint64_t admission_window_start_ns,
    uint32_t observation_window_width_ns,
    uint32_t guard_before_ns,
    uint32_t guard_after_ns,
    bool validate_static_schedule,
    vdc_gate_result_t *gate)
{
    if (gate != NULL) {
        memset(gate, 0, sizeof(*gate));
    }
    if (profile == NULL || evidence == NULL) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }
    if (profile->enabled == 0u) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_DISABLED,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (!(validate_static_schedule
              ? vdc_domain_schedule_validate(profile)
              : vdc_domain_active_schedule_validate(profile))) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_SCHEDULE,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->schedule_crc32 != profile->schedule_crc32) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->schedule_epoch != profile->schedule_epoch) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_EPOCH_MISMATCH,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->reference_slot_id != profile->reference_slot_id) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_REFERENCE_MISMATCH,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->source_slot_id >= VDC_DOMAIN_NODE_COUNT) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_SOURCE_OUT_OF_RANGE,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (require_dpll_eligible &&
        evidence->payload_class != VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE &&
        evidence->payload_class != VDC_DOMAIN_PAYLOAD_IDLE_BEACON) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (require_dpll_eligible) {
        vdc_timestamp_admission_code_t timestamp_code =
            VDC_TIMESTAMP_ADMISSION_PASS;
        if (!vdc_timestamp_dpll_admission_check(
                evidence->timestamp_source,
                evidence->timestamp_resolution_ns,
                evidence->timestamp_flags,
                VDC_DOMAIN_DPLL_ADMISSION_TIMESTAMP_RESOLUTION_LIMIT_NS,
                &timestamp_code)) {
            vdc_domain_gate_fail(
                gate,
                vdc_domain_timestamp_admission_to_gate(timestamp_code),
                evidence->source_slot_id,
                evidence->sample_seq);
            return false;
        }
    }

    const uint64_t cycle_offset =
        evidence->expected_window_start_ns % (uint64_t)profile->period_ns;
    if (cycle_offset != profile->observation_window_offset_ns ||
        !vdc_timestamp_observed_in_window(
            admission_window_start_ns,
            evidence->observed_time_ns,
            observation_window_width_ns,
            guard_before_ns,
            guard_after_ns)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_WINDOW_BOUND,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }

    if (gate != NULL) {
        gate->passed = 1u;
        gate->reject_code = VDC_DOMAIN_GATE_PASS;
        gate->reject_slot = evidence->source_slot_id;
        gate->reject_evidence = evidence->sample_seq;
        gate->last_pass_seq = evidence->sample_seq;
    }
    return true;
}

bool vdc_domain_validate_tdma_timestamp_evidence(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool require_dpll_eligible,
    vdc_gate_result_t *gate)
{
    if (profile == NULL) {
        if (gate != NULL) {
            memset(gate, 0, sizeof(*gate));
        }
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }

    return vdc_domain_validate_tdma_timestamp_evidence_window(
        profile,
        evidence,
        require_dpll_eligible,
        evidence != NULL ? evidence->expected_window_start_ns : 0u,
        profile->observation_window_width_ns,
        profile->guard_before_ns,
        profile->guard_after_ns,
        true,
        gate);
}

bool vdc_domain_validate_tdma_frame_envelope(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_frame_envelope_t *frame,
    bool require_dpll_eligible,
    vdc_gate_result_t *gate)
{
    uint32_t expected_offset_ns = 0u;
    uint32_t expected_width_ns = 0u;

    if (gate != NULL) {
        memset(gate, 0, sizeof(*gate));
    }
    if (profile == NULL || frame == NULL) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }
    if (!vdc_domain_schedule_validate(profile)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_SCHEDULE,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->frame_version != VDC_DOMAIN_TDMA_FRAME_VERSION ||
        frame->frame_crc32 == 0u ||
        frame->payload_crc32 == 0u) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->schedule_crc32 != profile->schedule_crc32) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->schedule_epoch != profile->schedule_epoch) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_EPOCH_MISMATCH,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->reference_slot_id != profile->reference_slot_id) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_REFERENCE_MISMATCH,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->source_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        frame->slot_index >= VDC_DOMAIN_NODE_COUNT) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_SOURCE_OUT_OF_RANGE,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (!vdc_domain_window_contract(profile,
                                    frame->window_class,
                                    &expected_offset_ns,
                                    &expected_width_ns)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_WINDOW_CLASS,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (!vdc_domain_payload_allowed_for_window(frame->window_class,
                                               frame->payload_class)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_PAYLOAD_WINDOW_FORBIDDEN,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (vdc_domain_payload_requires_reference_sync(frame->payload_class)) {
        const uint64_t expected_next_frame_start_ns =
            UINT64_MAX - frame->window_start_ns < (uint64_t)profile->period_ns
                ? UINT64_MAX
                : frame->window_start_ns + (uint64_t)profile->period_ns;
        if (frame->reference_sync_valid == 0u ||
            frame->reference_seq_id != frame->frame_seq ||
            frame->reference_frame_id != frame->frame_seq ||
            frame->reference_sync_slot_id != frame->reference_slot_id ||
            frame->reference_time_ns != frame->window_start_ns ||
            frame->next_frame_start_ns != expected_next_frame_start_ns ||
            frame->reference_schedule_crc32 != profile->schedule_crc32) {
            vdc_domain_gate_fail(gate,
                                 VDC_DOMAIN_GATE_BAD_FRAME,
                                 frame->source_slot_id,
                                 frame->frame_seq);
            return false;
        }
    } else if (frame->reference_sync_valid != 0u) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    const uint64_t cycle_offset =
        frame->window_start_ns % (uint64_t)profile->period_ns;
    if (cycle_offset != expected_offset_ns) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_WINDOW_BOUND,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    if (!vdc_timestamp_observed_in_window(frame->window_start_ns,
                                          frame->timestamp.observed_time_ns,
                                          expected_width_ns,
                                          profile->guard_before_ns,
                                          profile->guard_after_ns) ||
        frame->timestamp.timestamp_source == VDC_DOMAIN_TIMESTAMP_SOURCE_NONE ||
        frame->timestamp.timestamp_resolution_ns == 0u ||
        frame->timestamp.schedule_crc32 != profile->schedule_crc32 ||
        frame->timestamp.schedule_epoch != profile->schedule_epoch ||
        frame->timestamp.source_slot_id != frame->source_slot_id ||
        frame->timestamp.reference_slot_id != frame->reference_slot_id ||
        frame->timestamp.payload_class != frame->payload_class ||
        frame->timestamp.frame_crc32 != frame->frame_crc32 ||
        frame->timestamp.sample_seq != frame->frame_seq ||
        frame->timestamp.sample_crc32 == 0u) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    if (frame->window_class == VDC_DOMAIN_WINDOW_VDC_OBSERVATION) {
        if (frame->timestamp.expected_window_start_ns != frame->window_start_ns) {
            vdc_domain_gate_fail(gate,
                                 VDC_DOMAIN_GATE_WINDOW_BOUND,
                                 frame->source_slot_id,
                                 frame->frame_seq);
            return false;
        }
        return vdc_domain_validate_tdma_timestamp_evidence(profile,
                                                           &frame->timestamp,
                                                           require_dpll_eligible,
                                                           gate);
    }

    if (require_dpll_eligible) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    if (gate != NULL) {
        gate->passed = 1u;
        gate->reject_code = VDC_DOMAIN_GATE_PASS;
        gate->reject_slot = frame->source_slot_id;
        gate->reject_evidence = frame->frame_seq;
        gate->last_pass_seq = frame->frame_seq;
    }
    return true;
}

bool vdc_domain_observation_path_delay_lookup(
    const vdc_path_delay_table_t *table,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    vdc_path_delay_entry_t *entry)
{
    if ((!vdc_domain_path_delay_table_validate(table) &&
         !vdc_domain_path_delay_table_validate_provisional(table)) ||
        (table->flags & VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID) == 0u ||
        table->observation_matrix.valid == 0u ||
        source_slot_id >= table->observation_matrix.node_count ||
        reference_slot_id >= table->observation_matrix.node_count) {
        return false;
    }

    const uint32_t index = source_slot_id * VDC_DOMAIN_NODE_COUNT +
                           reference_slot_id;
    if ((table->observation_matrix.valid_bitmap[index / 32u] &
         (1u << (index % 32u))) == 0u) {
        return false;
    }
    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
        entry->valid = 1u;
        entry->source_slot_id = source_slot_id;
        entry->reference_slot_id = reference_slot_id;
        entry->delay_ns = table->observation_matrix.delay_ns[index];
        entry->cal_crc32 = table->table_crc32;
        entry->freshness_us = table->freshness_us;
        entry->writer = table->bias_generation;
        entry->update_seq = table->update_seq;
    }
    return true;
}

bool VDC_DOMAIN_TIME_CRITICAL(
    vdc_domain_active_observation_path_delay_lookup)(
    const vdc_path_delay_table_t *table,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    vdc_path_delay_entry_t *entry)
{
    if (table == NULL || table->valid == 0u ||
        table->version != VDC_DOMAIN_PATH_DELAY_TABLE_VERSION ||
        table->schedule_crc32 == 0u || table->table_crc32 == 0u ||
        (table->flags & VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID) == 0u ||
        table->observation_matrix.valid == 0u ||
        table->observation_matrix.node_count < 2u ||
        table->observation_matrix.node_count > VDC_DOMAIN_NODE_COUNT ||
        source_slot_id >= table->observation_matrix.node_count ||
        reference_slot_id >= table->observation_matrix.node_count) {
        return false;
    }

    const uint32_t index = source_slot_id * VDC_DOMAIN_NODE_COUNT +
                           reference_slot_id;
    if ((table->observation_matrix.valid_bitmap[index / 32u] &
         (1u << (index % 32u))) == 0u) {
        return false;
    }
    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
        entry->valid = 1u;
        entry->source_slot_id = source_slot_id;
        entry->reference_slot_id = reference_slot_id;
        entry->delay_ns = table->observation_matrix.delay_ns[index];
        entry->cal_crc32 = table->table_crc32;
        entry->freshness_us = table->freshness_us;
        entry->writer = table->bias_generation;
        entry->update_seq = table->update_seq;
    }
    return true;
}

static bool vdc_domain_expand_compact_observation_window(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay,
    vdc_wrap_tracker_t *wrap_tracker,
    const vdc_compact_observation_sample_t *compact,
    vdc_tdma_timestamp_evidence_t *evidence,
    uint64_t admission_window_start_ns,
    uint32_t observation_window_width_ns,
    uint32_t guard_before_ns,
    uint32_t guard_after_ns,
    vdc_gate_result_t *gate)
{
    vdc_timestamp_dictionary_entry_t entry;
    vdc_path_delay_entry_t path_entry;
    uint64_t observed_tick64 = 0u;
    uint32_t active_path_delay_ns = 0u;

    if (gate != NULL) {
        memset(gate, 0, sizeof(*gate));
    }
    if (evidence != NULL) {
        memset(evidence, 0, sizeof(*evidence));
    }
    if (profile == NULL || dictionary == NULL || wrap_tracker == NULL ||
        compact == NULL || evidence == NULL || compact->valid == 0u) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }
    if (!vdc_domain_schedule_validate(profile) ||
        !vdc_timestamp_dictionary_find(dictionary, compact->event_id, &entry)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             profile->local_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (compact->frame_crc32 == 0u || compact->sample_crc32 == 0u ||
        compact->timestamp_source == VDC_DOMAIN_TIMESTAMP_SOURCE_NONE ||
        compact->timestamp_resolution_ns == 0u ||
        entry.source_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        entry.reference_slot_id != profile->reference_slot_id ||
        compact->timestamp_source != entry.source ||
        (entry.payload_class != VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE &&
         entry.payload_class != VDC_DOMAIN_PAYLOAD_IDLE_BEACON)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             entry.source_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (!vdc_wrap_tracker_extend_tick(wrap_tracker,
                                      compact->tick_l32,
                                      compact->max_backward_ticks,
                                      &observed_tick64)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             entry.source_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (path_delay != NULL &&
        !vdc_domain_path_delay_lookup(path_delay,
                                      entry.source_slot_id,
                                      entry.reference_slot_id,
                                      &path_entry) &&
        !vdc_domain_observation_path_delay_lookup(
            path_delay, entry.source_slot_id, entry.reference_slot_id,
            &path_entry)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             entry.source_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (path_delay != NULL) {
        active_path_delay_ns = path_entry.delay_ns;
    }

    evidence->sample_seq = compact->sample_seq;
    evidence->schedule_epoch = profile->schedule_epoch;
    evidence->slot_index = entry.source_slot_id;
    evidence->source_slot_id = entry.source_slot_id;
    evidence->reference_slot_id = entry.reference_slot_id;
    evidence->payload_class = entry.payload_class;
    evidence->expected_window_start_ns = compact->expected_window_start_ns;
    evidence->arm_time_ns = observed_tick64;
    evidence->start_time_ns = observed_tick64;
    evidence->observed_time_ns = observed_tick64;
    evidence->done_time_ns = observed_tick64;
    evidence->apply_time_ns = observed_tick64;
    evidence->late_ns =
        observed_tick64 > compact->expected_window_start_ns
            ? vdc_domain_saturate_u64_to_u32(
                  observed_tick64 - compact->expected_window_start_ns)
            : 0u;
    evidence->jitter_ns = compact->jitter_ns;
    evidence->delay_ns =
        path_delay != NULL ? active_path_delay_ns : compact->delay_ns;
    evidence->phase_error_ns =
        vdc_domain_phase_error_ns(profile,
                                  compact->expected_window_start_ns,
                                  active_path_delay_ns,
                                  observed_tick64);
    evidence->timestamp_source = compact->timestamp_source;
    evidence->timestamp_resolution_ns = compact->timestamp_resolution_ns;
    evidence->timestamp_flags = compact->timestamp_flags;
    evidence->schedule_crc32 = profile->schedule_crc32;
    evidence->frame_crc32 = compact->frame_crc32;
    evidence->sample_crc32 = compact->sample_crc32;
    evidence->quality_flags = compact->quality_flags;

    return vdc_domain_validate_tdma_timestamp_evidence_window(
        profile,
        evidence,
        true,
        admission_window_start_ns,
        observation_window_width_ns,
        guard_before_ns,
        guard_after_ns,
        true,
        gate);
}

bool vdc_domain_expand_compact_observation(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_timestamp_dictionary_t *dictionary,
    vdc_wrap_tracker_t *wrap_tracker,
    const vdc_compact_observation_sample_t *compact,
    vdc_tdma_timestamp_evidence_t *evidence,
    vdc_gate_result_t *gate)
{
    if (profile == NULL) {
        if (gate != NULL) {
            memset(gate, 0, sizeof(*gate));
        }
        if (evidence != NULL) {
            memset(evidence, 0, sizeof(*evidence));
        }
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }

    return vdc_domain_expand_compact_observation_window(
        profile,
        dictionary,
        NULL,
        wrap_tracker,
        compact,
        evidence,
        compact != NULL ? compact->expected_window_start_ns : 0u,
        profile->observation_window_width_ns,
        profile->guard_before_ns,
        profile->guard_after_ns,
        gate);
}

bool vdc_domain_plan_tdma_window(const vdc_tdma_schedule_profile_t *profile,
                                 uint32_t window_class,
                                 uint64_t now_ns,
                                 vdc_tdma_window_plan_t *plan,
                                 vdc_gate_result_t *gate)
{
    uint32_t offset_ns = 0u;
    uint32_t width_ns = 0u;

    if (gate != NULL) {
        memset(gate, 0, sizeof(*gate));
    }
    if (plan != NULL) {
        memset(plan, 0, sizeof(*plan));
    }
    if (profile == NULL || plan == NULL) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }
    if (!vdc_domain_schedule_validate(profile)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_SCHEDULE,
                             profile->local_slot_id,
                             profile->schedule_epoch);
        return false;
    }
    if (!vdc_domain_window_contract(profile,
                                    window_class,
                                    &offset_ns,
                                    &width_ns)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_WINDOW_CLASS,
                             profile->local_slot_id,
                             profile->schedule_epoch);
        return false;
    }

    const uint64_t period_ns = profile->period_ns;
    uint64_t cycle_start_ns = now_ns - (now_ns % period_ns);
    uint64_t window_start_ns = cycle_start_ns + offset_ns;
    uint64_t window_end_ns = window_start_ns + width_ns;
    uint64_t guard_start_ns =
        window_start_ns > profile->guard_before_ns
            ? window_start_ns - profile->guard_before_ns
            : 0u;
    uint64_t guard_end_ns = window_end_ns + profile->guard_after_ns;

    if (now_ns > guard_end_ns) {
        cycle_start_ns += period_ns;
        window_start_ns = cycle_start_ns + offset_ns;
        window_end_ns = window_start_ns + width_ns;
        guard_start_ns = window_start_ns > profile->guard_before_ns
                             ? window_start_ns - profile->guard_before_ns
                             : 0u;
        guard_end_ns = window_end_ns + profile->guard_after_ns;
        plan->missed_current_window = 1u;
    }

    plan->valid = 1u;
    plan->window_class = window_class;
    plan->schedule_epoch = profile->schedule_epoch;
    plan->slot_index = profile->local_slot_id;
    plan->source_slot_id = profile->local_slot_id;
    plan->reference_slot_id = profile->reference_slot_id;
    plan->now_ns = now_ns;
    plan->window_start_ns = window_start_ns;
    plan->window_end_ns = window_end_ns;
    plan->guard_start_ns = guard_start_ns;
    plan->guard_end_ns = guard_end_ns;
    plan->schedule_crc32 = profile->schedule_crc32;

    if (now_ns < window_start_ns) {
        plan->wait_ns =
            vdc_domain_saturate_u64_to_u32(window_start_ns - now_ns);
    } else {
        plan->late_ns =
            vdc_domain_saturate_u64_to_u32(now_ns - window_start_ns);
    }
    if (now_ns >= guard_start_ns && now_ns <= guard_end_ns) {
        plan->in_guarded_window = 1u;
    }
    if (now_ns >= window_start_ns && now_ns <= window_end_ns) {
        plan->inside_payload_window = 1u;
    }

    if (gate != NULL) {
        gate->passed = 1u;
        gate->reject_code = VDC_DOMAIN_GATE_PASS;
        gate->reject_slot = profile->local_slot_id;
        gate->reject_evidence = profile->schedule_epoch;
        gate->last_pass_seq = profile->schedule_epoch;
    }
    return true;
}

bool vdc_domain_plan_tdma_ring(const vdc_tdma_schedule_profile_t *profile,
                               vdc_tdma_ring_plan_t *plan)
{
    if (plan != NULL) {
        memset(plan, 0, sizeof(*plan));
    }
    if (profile == NULL || plan == NULL ||
        !vdc_domain_schedule_validate(profile)) {
        return false;
    }

    plan->valid = 1u;
    plan->ring_node_count = profile->ring_binding.node_count;
    plan->local_slot_id = profile->local_slot_id;
    plan->reference_slot_id = profile->reference_slot_id;
    plan->upstream_slot_id = profile->ring_binding.upstream_slot_id;
    plan->downstream_slot_id = profile->ring_binding.downstream_slot_id;
    plan->feedback_slot_id = profile->ring_binding.feedback_slot_id;
    plan->from_reference_hops =
        vdc_domain_ring_forward_distance(profile->reference_slot_id,
                                         profile->local_slot_id,
                                         profile->ring_binding.node_count);
    plan->to_feedback_hops =
        vdc_domain_ring_forward_distance(profile->local_slot_id,
                                         profile->ring_binding.feedback_slot_id,
                                         profile->ring_binding.node_count);
    plan->is_reference_slot =
        profile->local_slot_id == profile->reference_slot_id ? 1u : 0u;
    plan->ring_flags = profile->ring_binding.flags;
    plan->ring_profile_crc32 = profile->ring_binding.profile_crc32;
    plan->schedule_crc32 = profile->schedule_crc32;
    plan->cycle_period_ns = profile->period_ns;
    return true;
}

bool vdc_domain_init(vdc_domain_context_t *context)
{
    if (context == NULL) {
        return false;
    }

    memset(context, 0, sizeof(*context));
    vdc_domain_default_schedule(&context->schedule, 0u, 0u);
    vdc_domain_default_servo(&context->servo);
    vdc_domain_default_clock_model(&context->clock,
                                   context->schedule.schedule_epoch,
                                   0u,
                                   0u,
                                   0u,
                                   context->schedule.schedule_crc32);
    context->clock.slew_limit_ppb = context->servo.sanity_freq_limit_ppb;
    context->dpll.state = VDC_DOMAIN_LOCK_OFF;
    context->dpll.schedule_crc32 = context->schedule.schedule_crc32;
    context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
    vdc_domain_init_quality(context);
    vdc_domain_default_dco_control(&context->dco,
                                   &context->clock,
                                   context->dpll.state);
    vdc_domain_default_timestamp_dictionary(&context->timestamp_dictionary,
                                            &context->schedule);
    vdc_domain_default_path_delay_table(&context->path_delay,
                                        &context->schedule);
    vdc_wrap_tracker_init_open(&context->wrap_tracker);
    return true;
}

static bool vdc_domain_activate_tdma_configuration_checked(
    vdc_domain_context_t *context,
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay,
    bool provisional)
{
    if (context == NULL || schedule == NULL || dictionary == NULL ||
        path_delay == NULL || !vdc_domain_schedule_validate(schedule) ||
        !vdc_timestamp_dictionary_validate(dictionary) ||
        !(provisional
              ? vdc_domain_path_delay_table_validate_provisional(path_delay)
              : vdc_domain_path_delay_table_validate(path_delay)) ||
        dictionary->profile_crc32 != schedule->schedule_crc32 ||
        path_delay->schedule_crc32 != schedule->schedule_crc32 ||
        path_delay->observation_matrix.node_count !=
            schedule->ring_binding.node_count) {
        return false;
    }

    const uint32_t next_run_id = context->clock.run_id + 1u;
    const uint32_t ready = context->ready;
    context->schedule = *schedule;
    context->timestamp_dictionary = *dictionary;
    context->path_delay = *path_delay;
    memset(&context->dpll, 0, sizeof(context->dpll));
    memset(&context->gate, 0, sizeof(context->gate));
    vdc_domain_default_clock_model(&context->clock,
                                   schedule->schedule_epoch,
                                   next_run_id,
                                   0u,
                                   0u,
                                   schedule->schedule_crc32);
    context->clock.nominal_period_ns = schedule->period_ns;
    context->clock.slew_limit_ppb = context->servo.sanity_freq_limit_ppb;
    context->dpll.state = ready != 0u ? VDC_DOMAIN_LOCK_CHECKING
                                     : VDC_DOMAIN_LOCK_OFF;
    context->dpll.schedule_crc32 = schedule->schedule_crc32;
    context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
    vdc_domain_init_quality(context);
    vdc_domain_default_dco_control(&context->dco,
                                   &context->clock,
                                   context->dpll.state);
    vdc_wrap_tracker_init_open(&context->wrap_tracker);
    vdc_domain_refresh_quality_state(context);
    return true;
}

bool vdc_domain_activate_tdma_configuration(
    vdc_domain_context_t *context,
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay)
{
    return vdc_domain_activate_tdma_configuration_checked(
        context, schedule, dictionary, path_delay, false);
}

bool vdc_domain_activate_tdma_provisional_configuration(
    vdc_domain_context_t *context,
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay)
{
    return vdc_domain_activate_tdma_configuration_checked(
        context, schedule, dictionary, path_delay, true);
}

void vdc_domain_set_ready(vdc_domain_context_t *context, bool ready)
{
    if (context == NULL) {
        return;
    }
    context->ready = ready ? 1u : 0u;
    if (!ready) {
        context->dpll.state = VDC_DOMAIN_LOCK_OFF;
    } else if (context->dpll.state == VDC_DOMAIN_LOCK_OFF) {
        context->dpll.state = VDC_DOMAIN_LOCK_CHECKING;
    }
    vdc_domain_sync_dco_lock_state(context);
}

void vdc_domain_service(vdc_domain_context_t *context, uint64_t now_ns)
{
    if (context == NULL) {
        return;
    }

    if (context->service_count == 0u) {
        context->first_service_time_ns = now_ns;
    }
    context->service_count++;
    context->last_service_time_ns = now_ns;

    if (context->ready == 0u) {
        context->dpll.state = VDC_DOMAIN_LOCK_OFF;
        vdc_domain_sync_dco_lock_state(context);
        vdc_domain_refresh_quality_state(context);
        return;
    }
    if (context->dpll.state == VDC_DOMAIN_LOCK_OFF) {
        context->dpll.state = VDC_DOMAIN_LOCK_CHECKING;
        vdc_domain_sync_dco_lock_state(context);
    }
    vdc_domain_refresh_quality_age(context, now_ns);
}

bool vdc_domain_publish_clock_model(vdc_domain_context_t *context,
                                    const vdc_clock_model_t *model)
{
    const uint32_t next_dco_seq =
        context != NULL ? context->dco.dco_update_seq + 1u : 0u;

    if (context == NULL || model == NULL || model->valid == 0u ||
        model->tdma_schedule_crc32 != context->schedule.schedule_crc32 ||
        model->servo_profile_crc32 != context->servo.servo_profile_crc32) {
        return false;
    }

    context->clock = *model;
    context->clock.model_seq++;
    context->clock.slew_limit_ppb = context->servo.sanity_freq_limit_ppb;
    vdc_domain_default_dco_control(&context->dco,
                                   &context->clock,
                                   context->dpll.state);
    context->dco.dco_update_seq = next_dco_seq;
    context->dpll.update_seq++;
    return true;
}

bool vdc_domain_publish_dco_control(vdc_domain_context_t *context,
                                    const vdc_dco_control_t *dco)
{
    const uint32_t next_dco_seq =
        context != NULL ? context->dco.dco_update_seq + 1u : 0u;

    if (context == NULL || dco == NULL ||
        !vdc_domain_dco_control_validate(&context->schedule,
                                         &context->servo,
                                         dco)) {
        return false;
    }

    context->dco = *dco;
    context->dco.dco_update_seq = next_dco_seq;
    context->dpll.update_seq++;
    return true;
}

bool vdc_domain_publish_timestamp_dictionary(
    vdc_domain_context_t *context,
    const vdc_timestamp_dictionary_t *dictionary,
    uint32_t initial_tick_l32)
{
    if (context == NULL || dictionary == NULL ||
        !vdc_timestamp_dictionary_validate(dictionary) ||
        dictionary->profile_crc32 != context->schedule.schedule_crc32) {
        return false;
    }

    context->timestamp_dictionary = *dictionary;
    vdc_wrap_tracker_reanchor(&context->wrap_tracker, initial_tick_l32);
    return true;
}

bool vdc_domain_publish_path_delay_table(
    vdc_domain_context_t *context,
    const vdc_path_delay_table_t *table)
{
    if (context == NULL || table == NULL ||
        !vdc_domain_path_delay_table_validate(table) ||
        table->schedule_crc32 != context->schedule.schedule_crc32) {
        return false;
    }

    context->path_delay = *table;
    return true;
}

static bool vdc_domain_prepare_tdma_evidence_checked(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool validate_static_schedule,
    vdc_tdma_evidence_preparation_t *preparation)
{
    if (context == NULL || evidence == NULL || preparation == NULL) {
        return false;
    }
    memset(preparation, 0, sizeof(*preparation));

    vdc_gate_result_t gate;
    const bool acquisition_window =
        vdc_domain_context_uses_acquisition_window(context);
    const uint32_t outlier_threshold_ns =
        vdc_domain_effective_outlier_threshold_ns(context);
    const uint32_t tracking_half_width_ns =
        outlier_threshold_ns > context->schedule.observation_window_width_ns
            ? outlier_threshold_ns
            : context->schedule.observation_window_width_ns;
    const uint32_t admission_window_width_ns =
        acquisition_window
            ? vdc_domain_acquisition_window_width_ns(&context->schedule)
            : vdc_domain_tracking_window_width_ns(tracking_half_width_ns);
    const uint32_t admission_guard_before_ns =
        acquisition_window ? VDC_DOMAIN_ACQUISITION_GUARD_NS : 0u;
    const uint32_t admission_guard_after_ns =
        acquisition_window ? VDC_DOMAIN_ACQUISITION_GUARD_NS : 0u;
    const uint64_t admission_window_start_ns =
        acquisition_window
            ? evidence->expected_window_start_ns
            : vdc_domain_tracking_window_start_ns(
                  context, evidence->expected_window_start_ns,
                  tracking_half_width_ns);

    if (!vdc_domain_validate_tdma_timestamp_evidence_window(
            &context->schedule,
            evidence,
            true,
            admission_window_start_ns,
            admission_window_width_ns,
            admission_guard_before_ns,
            admission_guard_after_ns,
            validate_static_schedule,
            &gate)) {
        preparation->valid = 1u;
        preparation->schedule_crc32 = context->schedule.schedule_crc32;
        preparation->dpll_update_seq = context->dpll.update_seq;
        preparation->gate = gate;
        return true;
    }

    const int32_t input_residual_ns =
        vdc_domain_corrected_phase_error_ns(context, evidence);
    const uint32_t abs_predicted_residual =
        vdc_domain_abs_i32(input_residual_ns);
    if (!acquisition_window &&
        context->dpll.accepted_sample_count != 0u &&
        outlier_threshold_ns != 0u &&
        abs_predicted_residual > outlier_threshold_ns) {
        vdc_domain_gate_fail(&gate,
                             VDC_DOMAIN_GATE_SERVO_OUTLIER,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        preparation->valid = 1u;
        preparation->schedule_crc32 = context->schedule.schedule_crc32;
        preparation->dpll_update_seq = context->dpll.update_seq;
        preparation->input_residual_ns = input_residual_ns;
        preparation->gate = gate;
        return true;
    }

    preparation->valid = 1u;
    preparation->schedule_crc32 = context->schedule.schedule_crc32;
    preparation->dpll_update_seq = context->dpll.update_seq;
    preparation->accepted = 1u;
    preparation->input_residual_ns = input_residual_ns;
    preparation->gate = gate;
    return true;
}

bool VDC_DOMAIN_TIME_CRITICAL(vdc_domain_apply_prepared_tdma_evidence_servo)(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation)
{
    if (context == NULL || evidence == NULL || preparation == NULL ||
        preparation->valid == 0u ||
        preparation->servo_applied != 0u ||
        preparation->applied != 0u ||
        preparation->schedule_crc32 != context->schedule.schedule_crc32 ||
        preparation->schedule_crc32 != evidence->schedule_crc32 ||
        preparation->dpll_update_seq != context->dpll.update_seq) {
        return false;
    }

    if (preparation->accepted != 0u) {
        context->dpll.accepted_sample_count++;
        vdc_domain_update_clock_from_evidence(
            context, evidence, preparation->input_residual_ns);
    }
    preparation->servo_applied = 1u;
    preparation->post_servo_dpll_update_seq = context->dpll.update_seq;
    return true;
}

bool VDC_DOMAIN_TIME_CRITICAL(vdc_domain_apply_prepared_tdma_evidence_state)(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted)
{
    if (accepted != NULL) {
        *accepted = false;
    }
    if (context == NULL || evidence == NULL || preparation == NULL ||
        preparation->valid == 0u ||
        preparation->servo_applied == 0u ||
        preparation->applied != 0u ||
        preparation->schedule_crc32 != context->schedule.schedule_crc32 ||
        preparation->schedule_crc32 != evidence->schedule_crc32 ||
        preparation->post_servo_dpll_update_seq !=
            context->dpll.update_seq) {
        return false;
    }

    const vdc_gate_result_t gate = preparation->gate;
    context->gate = gate;
    if (preparation->accepted == 0u) {
        const bool reacquire =
            vdc_domain_reject_requires_reacquire(gate.reject_code);
        context->dpll.rejected_sample_count++;
        context->dpll.last_reject_code = gate.reject_code;
        context->dpll.update_seq++;
        if (reacquire) {
            vdc_domain_reset_lock_acquisition(context);
        }
        context->dpll.state =
            vdc_domain_lock_state_after_reject(context, reacquire);
        vdc_domain_sync_dco_lock_state(context);
        preparation->applied = 1u;
        preparation->post_apply_dpll_update_seq = context->dpll.update_seq;
        return true;
    }

    vdc_tdma_timestamp_evidence_t input_evidence = *evidence;
    input_evidence.phase_error_ns = preparation->input_residual_ns;
    const uint32_t abs_phase =
        vdc_domain_abs_i32(input_evidence.phase_error_ns);

    context->dpll.last_sample_seq = evidence->sample_seq;
    context->dpll.last_reject_code = VDC_DOMAIN_GATE_PASS;
    context->dpll.last_phase_error_ns = input_evidence.phase_error_ns;
    context->dpll.last_offset_ns = input_evidence.phase_error_ns;
    context->dpll.jitter_pk_ns =
        evidence->jitter_ns > context->dpll.jitter_pk_ns
            ? evidence->jitter_ns
            : context->dpll.jitter_pk_ns;
    context->dpll.max_abs_offset_ns =
        abs_phase > context->dpll.max_abs_offset_ns
            ? abs_phase
            : context->dpll.max_abs_offset_ns;
    context->dpll.rms_offset_ns =
        (context->dpll.rms_offset_ns == 0u)
            ? abs_phase
            : (context->dpll.rms_offset_ns + abs_phase) / 2u;
    context->dpll.schedule_crc32 = context->schedule.schedule_crc32;
    context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
    context->dpll.update_seq++;

    if (context->dpll.accepted_sample_count < VDC_DOMAIN_INITIAL_LOCK_SAMPLES) {
        context->dpll.state = VDC_DOMAIN_LOCK_CHECKING;
    } else if (context->dpll.accepted_sample_count < VDC_DOMAIN_FREQ_LOCK_SAMPLES) {
        context->dpll.state = VDC_DOMAIN_LOCK_INITIAL_SYNC;
    } else if (context->dpll.accepted_sample_count < VDC_DOMAIN_PHASE_LOCK_SAMPLES) {
        context->dpll.state = VDC_DOMAIN_LOCK_FREQ_LOCK;
    } else if (context->dpll.accepted_sample_count < context->servo.lock_sample_count ||
               abs_phase >
                   vdc_domain_effective_lock_acceptance_threshold_ns(context)) {
        context->dpll.state = VDC_DOMAIN_LOCK_PHASE_LOCK;
    } else {
        context->dpll.state = VDC_DOMAIN_LOCK_LOCKED;
    }

    vdc_domain_sync_dco_lock_state(context);
    preparation->applied = 1u;
    preparation->post_apply_dpll_update_seq = context->dpll.update_seq;
    if (accepted != NULL) {
        *accepted = true;
    }
    return true;
}

bool vdc_domain_apply_prepared_tdma_evidence_core(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted)
{
    return vdc_domain_apply_prepared_tdma_evidence_servo(
               context, evidence, preparation) &&
           vdc_domain_apply_prepared_tdma_evidence_state(
               context, evidence, preparation, accepted);
}

bool VDC_DOMAIN_TIME_CRITICAL(vdc_domain_finalize_prepared_tdma_evidence)(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    const vdc_tdma_evidence_preparation_t *preparation)
{
    if (context == NULL || evidence == NULL || preparation == NULL ||
        preparation->valid == 0u || preparation->applied == 0u ||
        preparation->schedule_crc32 != context->schedule.schedule_crc32 ||
        preparation->schedule_crc32 != evidence->schedule_crc32 ||
        preparation->post_apply_dpll_update_seq != context->dpll.update_seq) {
        return false;
    }
    if (preparation->accepted == 0u) {
        vdc_domain_record_rejected_sample(
            context, evidence, &preparation->gate);
        return true;
    }

    vdc_tdma_timestamp_evidence_t input_evidence = *evidence;
    input_evidence.phase_error_ns = preparation->input_residual_ns;
    vdc_domain_record_accepted_sample(context, &input_evidence);
    return true;
}

bool vdc_domain_apply_prepared_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted)
{
    return vdc_domain_apply_prepared_tdma_evidence_core(
               context, evidence, preparation, accepted) &&
           vdc_domain_finalize_prepared_tdma_evidence(
               context, evidence, preparation);
}

static bool vdc_domain_submit_tdma_evidence_checked(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool validate_static_schedule)
{
    vdc_tdma_evidence_preparation_t preparation;
    bool accepted = false;
    return vdc_domain_prepare_tdma_evidence_checked(
               context, evidence, validate_static_schedule, &preparation) &&
           vdc_domain_apply_prepared_tdma_evidence(
               context, evidence, &preparation, &accepted) && accepted;
}

bool vdc_domain_submit_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    return vdc_domain_submit_tdma_evidence_checked(context, evidence, true);
}

bool vdc_domain_submit_active_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    vdc_tdma_evidence_preparation_t preparation;
    bool accepted = false;
    return vdc_domain_prepare_active_tdma_evidence(
               context, evidence, &preparation) &&
           vdc_domain_apply_prepared_tdma_evidence(
               context, evidence, &preparation, &accepted) && accepted;
}

bool vdc_domain_prepare_active_tdma_evidence(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation)
{
    if (context == NULL || evidence == NULL ||
        preparation == NULL ||
        context->path_delay.valid == 0u ||
        context->path_delay.table_crc32 == 0u ||
        context->schedule.schedule_crc32 == 0u ||
        context->dpll.schedule_crc32 != context->schedule.schedule_crc32 ||
        context->clock.tdma_schedule_crc32 !=
            context->schedule.schedule_crc32 ||
        context->timestamp_dictionary.profile_crc32 !=
            context->schedule.schedule_crc32 ||
        context->path_delay.schedule_crc32 !=
            context->schedule.schedule_crc32) {
        return false;
    }
    return vdc_domain_prepare_tdma_evidence_checked(
        context, evidence, false, preparation);
}

bool vdc_domain_submit_compact_observation(
    vdc_domain_context_t *context,
    const vdc_compact_observation_sample_t *compact)
{
    vdc_tdma_timestamp_evidence_t evidence;
    vdc_gate_result_t gate;

    if (context == NULL || compact == NULL) {
        return false;
    }

    const bool acquisition_window =
        vdc_domain_context_uses_acquisition_window(context);
    const uint32_t outlier_threshold_ns =
        vdc_domain_effective_outlier_threshold_ns(context);
    const uint32_t tracking_half_width_ns =
        outlier_threshold_ns > context->schedule.observation_window_width_ns
            ? outlier_threshold_ns
            : context->schedule.observation_window_width_ns;
    const uint32_t admission_window_width_ns =
        acquisition_window
            ? vdc_domain_acquisition_window_width_ns(&context->schedule)
            : vdc_domain_tracking_window_width_ns(tracking_half_width_ns);
    const uint32_t admission_guard_before_ns =
        acquisition_window ? VDC_DOMAIN_ACQUISITION_GUARD_NS : 0u;
    const uint32_t admission_guard_after_ns =
        acquisition_window ? VDC_DOMAIN_ACQUISITION_GUARD_NS : 0u;
    const uint64_t admission_window_start_ns =
        acquisition_window
            ? compact->expected_window_start_ns
            : vdc_domain_tracking_window_start_ns(
                  context, compact->expected_window_start_ns,
                  tracking_half_width_ns);

    if (!vdc_domain_expand_compact_observation_window(
            &context->schedule,
            &context->timestamp_dictionary,
            &context->path_delay,
            &context->wrap_tracker,
            compact,
            &evidence,
            admission_window_start_ns,
            admission_window_width_ns,
            admission_guard_before_ns,
            admission_guard_after_ns,
            &gate)) {
        context->gate = gate;
        context->dpll.rejected_sample_count++;
        context->dpll.last_reject_code = gate.reject_code;
        context->dpll.update_seq++;
        context->dpll.state = vdc_domain_lock_state_after_reject(
            context, vdc_domain_reject_requires_reacquire(gate.reject_code));
        vdc_domain_record_rejected_sample(context, &evidence, &gate);
        return false;
    }

    return vdc_domain_submit_tdma_evidence(context, &evidence);
}

bool vdc_domain_get_snapshot(const vdc_domain_context_t *context,
                             vdc_domain_snapshot_t *snapshot)
{
    if (context == NULL || snapshot == NULL) {
        return false;
    }
    snapshot->ready = context->ready;
    snapshot->service_count = context->service_count;
    snapshot->first_service_time_ns = context->first_service_time_ns;
    snapshot->last_service_time_ns = context->last_service_time_ns;
    snapshot->schedule = context->schedule;
    snapshot->servo = context->servo;
    snapshot->clock = context->clock;
    snapshot->dco = context->dco;
    snapshot->dpll = context->dpll;
    snapshot->quality = context->quality;
    snapshot->error_budget = context->error_budget;
    snapshot->path_delay = context->path_delay;
    snapshot->gate = context->gate;
    return true;
}
