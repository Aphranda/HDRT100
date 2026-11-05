#include "vdc_domain.h"
#include "vdc_domain_internal.h"

#include "tdma_ring_runtime.h"

#include <limits.h>
#include <string.h>

#include "tdma_operating_profile.h"
#include "algo_checksum.h"

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
#define VDC_DOMAIN_RATE_MIN_OBSERVATION_CYCLES 8u
#define VDC_DOMAIN_RATE_SLEW_DIVISOR 8u
#define VDC_DOMAIN_RATE_MIN_SLEW_LIMIT_PPB 1000u

void vdc_domain_request_oscillator_discipline(
    vdc_domain_context_t *context,
    int32_t rate_source_ppb,
    uint32_t source_slot_id,
    uint32_t source_command_seq,
    uint64_t now_ns);

uint32_t vdc_domain_hash_u32(uint32_t hash, uint32_t value)
{
    return algo_fnv1a32_u32_le(hash, value);
}

uint32_t vdc_domain_abs_i32(int32_t value)
{
    if (value == INT32_MIN) {
        return (uint32_t)INT32_MAX + 1u;
    }
    return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
}

uint32_t vdc_domain_increment_nonzero(uint32_t value)
{
    value++;
    return value == 0u ? 1u : value;
}

void vdc_domain_increment_saturating(uint32_t *value)
{
    if (value != NULL && *value != UINT32_MAX) {
        (*value)++;
    }
}

bool vdc_domain_is_follower(const vdc_domain_context_t *context)
{
    return context != NULL && context->control.profile.valid == 1u &&
           context->control.profile.version ==
               VDC_DPLL_CONTROL_PROFILE_VERSION &&
           context->control.profile.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER;
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
int32_t VDC_DOMAIN_TIME_CRITICAL(
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

int32_t VDC_DOMAIN_TIME_CRITICAL(
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

void vdc_domain_gate_fail(vdc_gate_result_t *gate,
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

vdc_domain_gate_code_t vdc_domain_timestamp_admission_to_gate(
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

bool vdc_domain_range_in_period(uint32_t offset_ns,
                                       uint32_t width_ns,
                                       uint32_t period_ns)
{
    if (period_ns == 0u || width_ns == 0u || offset_ns >= period_ns) {
        return false;
    }
    return (uint64_t)offset_ns + (uint64_t)width_ns <= (uint64_t)period_ns;
}

bool vdc_domain_ranges_overlap(uint32_t offset_a_ns,
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

uint32_t vdc_domain_ring_forward_distance(uint32_t from_slot_id,
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

bool vdc_domain_guarded_observation_overlaps(uint32_t obs_offset_ns,
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

bool vdc_domain_window_contract(
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

bool vdc_domain_payload_allowed_for_window(uint32_t window_class,
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

bool vdc_domain_payload_requires_reference_sync(uint32_t payload_class)
{
    return payload_class == VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE ||
           payload_class == VDC_DOMAIN_PAYLOAD_IDLE_BEACON;
}

uint32_t vdc_domain_saturate_u64_to_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

uint64_t vdc_domain_tracking_window_start_ns(
    const vdc_domain_context_t *context,
    uint64_t expected_window_start_ns,
    uint32_t half_width_ns)
{
    (void)context;
    /* The timestamp is already in the local TDMA observation epoch.  The
     * servo phase model is a correction input, not a reason to move the
     * admission window by milliseconds and reject the samples that would
     * correct that model.  Keep tracking symmetric around this cycle. */
    return expected_window_start_ns > half_width_ns
        ? expected_window_start_ns - half_width_ns : 0u;
}

uint32_t vdc_domain_tracking_window_width_ns(uint32_t half_width_ns)
{
    return half_width_ns > UINT32_MAX / 2u
        ? UINT32_MAX : half_width_ns * 2u;
}

uint32_t vdc_domain_avg_u32(uint32_t previous, uint32_t sample)
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

int32_t vdc_domain_phase_error_ns(
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

uint32_t vdc_domain_acquisition_window_width_ns(
    const vdc_tdma_schedule_profile_t *profile)
{
    if (profile == NULL || profile->period_ns == 0u) {
        return VDC_DOMAIN_DEFAULT_OBSERVATION_WIDTH_NS;
    }
    return profile->period_ns;
}

bool vdc_domain_context_uses_acquisition_window(
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

int32_t vdc_domain_corrected_phase_error_ns(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    if (context == NULL || evidence == NULL) {
        return 0;
    }
    int32_t rate_correction_ns = 0;
    if (context->dpll.accepted_sample_count != 0u &&
        context->phase_observed_anchor_valid != 0u &&
        context->clock.valid != 0u &&
        context->clock.period_adjust_ppb != context->reference_baseline_ppb &&
        evidence->observed_time_ns > context->phase_observed_anchor_ns) {
        const uint64_t elapsed_ns64 =
            evidence->observed_time_ns - context->phase_observed_anchor_ns;
        const uint32_t elapsed_ns = elapsed_ns64 > UINT32_MAX
            ? UINT32_MAX : (uint32_t)elapsed_ns64;
        rate_correction_ns = vdc_domain_ppb_elapsed_correction_ns(
            vdc_domain_clamp_i64_to_i32(
                (int64_t)context->clock.period_adjust_ppb -
                context->reference_baseline_ppb), elapsed_ns);
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

uint32_t vdc_domain_effective_lock_acceptance_threshold_ns(
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

uint32_t vdc_domain_effective_phase_diagnostic_threshold_ns(
    const vdc_domain_context_t *context)
{
    if (context == NULL ||
        context->servo.phase_diagnostic_threshold_ns == 0u) {
        return 0u;
    }

    uint32_t threshold_ns = context->servo.phase_diagnostic_threshold_ns;
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

int32_t vdc_domain_clamp_ppb(int64_t value, uint32_t limit_ppb)
{
    const int64_t limit = limit_ppb > (uint32_t)INT32_MAX
        ? (int64_t)INT32_MAX : (int64_t)limit_ppb;
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

int32_t vdc_domain_slew_i32(int32_t current,
                                   int32_t target,
                                   uint32_t max_step)
{
    const int64_t delta = (int64_t)target - (int64_t)current;
    const int64_t bounded_step = max_step > (uint32_t)INT32_MAX
        ? (int64_t)INT32_MAX : (int64_t)max_step;
    if (max_step == 0u || delta == 0ll) {
        return target;
    }
    if (delta > bounded_step) {
        return vdc_domain_clamp_i64_to_i32((int64_t)current + bounded_step);
    }
    if (delta < -bounded_step) {
        return vdc_domain_clamp_i64_to_i32((int64_t)current - bounded_step);
    }
    return target;
}

static int32_t vdc_domain_integrator_delta_ppb(
    int32_t phase_error_ns,
    int32_t ki_q16,
    uint32_t update_period_us,
    uint32_t limit_ppb)
{
    if (phase_error_ns == 0 || ki_q16 == 0 ||
        update_period_us == 0u || limit_ppb == 0u) {
        return 0;
    }

    /* The legacy high-gain debug profile is sampled every 1 ms. Its fixed
     * Ki/update pair has a cheaper exact integer form; all other profiles use
     * the generic bounded ratio helper below. */
    int32_t phase_to_rate_ppb;
    if (update_period_us == 1000u && ki_q16 == 4096) {
        int64_t scaled = (int64_t)phase_error_ns * 62ll;
        scaled += phase_error_ns / 2;
        const int64_t limit = limit_ppb > (uint32_t)INT32_MAX
            ? (int64_t)INT32_MAX : (int64_t)limit_ppb;
        if (scaled > limit) {
            phase_to_rate_ppb = (int32_t)limit;
        } else if (scaled < -limit) {
            phase_to_rate_ppb = (int32_t)-limit;
        } else {
            phase_to_rate_ppb = (int32_t)scaled;
        }
    } else {
        phase_to_rate_ppb = vdc_domain_scaled_ratio_clamped_i32(
            phase_error_ns, 1000000u, update_period_us, limit_ppb);
    }
    return vdc_domain_scale_q16_i32(phase_to_rate_ppb, ki_q16);
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

uint64_t vdc_domain_evidence_time_ns(
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

static uint32_t vdc_domain_quality_health(const vdc_quality_table_t *quality,
                                         uint32_t lock_state, bool gate_passed)
{
    if (lock_state == VDC_DOMAIN_LOCK_FAULT) {
        return VDC_DOMAIN_HEALTH_FAULT;
    }
    if (quality->last_sample_age_us == UINT32_MAX) {
        return VDC_DOMAIN_HEALTH_DEGRADED;
    }
    if (lock_state == VDC_DOMAIN_LOCK_LOCKED && gate_passed &&
        quality->lock_quality_tier >= VDC_DOMAIN_LOCK_QUALITY_FINE_100NS &&
        quality->freshness_limit_us != 0u &&
        quality->last_sample_age_us <= quality->freshness_limit_us) {
        return VDC_DOMAIN_HEALTH_HEALTHY;
    }
    if (lock_state == VDC_DOMAIN_LOCK_PHASE_LOCK ||
        lock_state == VDC_DOMAIN_LOCK_FREQ_LOCK) {
        return VDC_DOMAIN_HEALTH_LOCK_CANDIDATE;
    }
    if (lock_state == VDC_DOMAIN_LOCK_OFF ||
        lock_state == VDC_DOMAIN_LOCK_CHECKING ||
        lock_state == VDC_DOMAIN_LOCK_INITIAL_SYNC ||
        lock_state == VDC_DOMAIN_LOCK_RELOCKING) {
        return VDC_DOMAIN_HEALTH_CHECKING;
    }
    return VDC_DOMAIN_HEALTH_DEGRADED;
}

void VDC_DOMAIN_TIME_CRITICAL(vdc_domain_refresh_quality_state)(
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
    context->quality.health_state = vdc_domain_quality_health(
        &context->quality, context->dpll.state, context->gate.passed != 0u);
}

bool vdc_domain_age_quality(vdc_quality_table_t *quality,
                           uint32_t lock_state, bool gate_passed,
                           uint64_t now_ns)
{
    if (quality == NULL || quality->last_sample_time_ns == 0u) {
        return false;
    }
    const uint32_t old_age = quality->last_sample_age_us;
    const uint32_t old_health = quality->health_state;
    /* Unknown/saturated age stays fail-closed for this evidence. A later
     * successful clock read alone cannot renew it; accepted evidence owns
     * the existing age=0 reset. Compare in the stored microsecond unit. */
    uint32_t age = UINT32_MAX;
    if (old_age != UINT32_MAX && now_ns != UINT64_MAX &&
        now_ns >= quality->last_sample_time_ns) {
        age = vdc_domain_saturate_u64_to_u32(
            (now_ns - quality->last_sample_time_ns) / 1000ull);
        if (age < old_age) age = UINT32_MAX;
    }
    quality->last_sample_age_us = age;
    quality->health_state = vdc_domain_quality_health(quality, lock_state, gate_passed);
    return old_age != quality->last_sample_age_us || old_health != quality->health_state;
}

void vdc_domain_sync_dco_lock_state(vdc_domain_context_t *context)
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

void vdc_domain_refresh_quality_age(vdc_domain_context_t *context,
                                           uint64_t now_ns)
{
    if (context == NULL || context->quality.last_sample_time_ns == 0u) {
        return;
    }
    (void)vdc_domain_age_quality(&context->quality, context->dpll.state,
                                 context->gate.passed != 0u, now_ns);
    if (context->dpll.state == VDC_DOMAIN_LOCK_HOLDOVER) {
        context->dpll.holdover_age_us = context->quality.last_sample_age_us;
    }
    vdc_domain_refresh_quality_state(context);
}

void vdc_domain_init_quality(vdc_domain_context_t *context)
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

/* A rejected observation does not establish a new control context. In
 * particular, old provenance on one sample does not invalidate the active
 * configuration. Context activation and role/profile changes own their
 * explicit resets; sample misses only update rejection diagnostics. */
bool vdc_domain_reject_requires_reacquire(uint32_t reject_code)
{
    switch ((vdc_domain_gate_code_t)reject_code) {
    case VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH:
    case VDC_DOMAIN_GATE_EPOCH_MISMATCH:
    case VDC_DOMAIN_GATE_REFERENCE_MISMATCH:
    case VDC_DOMAIN_GATE_SOURCE_OUT_OF_RANGE:
    case VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE:
    case VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE:
    case VDC_DOMAIN_GATE_TIMESTAMP_RESOLUTION:
    case VDC_DOMAIN_GATE_WINDOW_BOUND:
    case VDC_DOMAIN_GATE_BAD_FRAME:
    case VDC_DOMAIN_GATE_BAD_WINDOW_CLASS:
    case VDC_DOMAIN_GATE_PAYLOAD_WINDOW_FORBIDDEN:
    case VDC_DOMAIN_GATE_DELAY_GENERATION:
    case VDC_DOMAIN_GATE_BIAS_GENERATION:
    case VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED:
        return false;
    default:
        return true;
    }
}

void vdc_domain_record_rejected_sample(
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
    context->quality.consecutive_bad_samples++;
    context->quality.last_reject_code = gate->reject_code;
    context->quality.gate_reject_code = gate->reject_code;
    context->quality.gate_reject_slot = gate->reject_slot;
    context->quality.gate_reject_evidence = gate->reject_evidence;
    if (vdc_domain_reject_requires_reacquire(gate->reject_code)) {
        context->quality.consecutive_good_samples = 0u;
        context->quality.consecutive_coarse_samples = 0u;
        context->quality.consecutive_debug_samples = 0u;
        context->quality.consecutive_fine_samples = 0u;
        if (evidence != NULL) {
            context->quality.last_sample_seq = evidence->sample_seq;
            context->quality.last_timestamp_source = evidence->timestamp_source;
            context->quality.last_timestamp_resolution_ns =
                evidence->timestamp_resolution_ns;
            context->quality.last_timestamp_flags = evidence->timestamp_flags;
        }
    }
    vdc_domain_refresh_quality_state(context);
}

bool vdc_domain_debug_gate_recoverable(uint32_t gate_code)
{
    switch ((vdc_domain_gate_code_t)gate_code) {
    case VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE:
    case VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE:
    case VDC_DOMAIN_GATE_TIMESTAMP_RESOLUTION:
    case VDC_DOMAIN_GATE_WINDOW_BOUND:
    case VDC_DOMAIN_GATE_BAD_FRAME:
        return true;
    case VDC_DOMAIN_GATE_PASS:
    case VDC_DOMAIN_GATE_DISABLED:
    case VDC_DOMAIN_GATE_BAD_ARGUMENT:
    case VDC_DOMAIN_GATE_BAD_SCHEDULE:
    case VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH:
    case VDC_DOMAIN_GATE_EPOCH_MISMATCH:
    case VDC_DOMAIN_GATE_REFERENCE_MISMATCH:
    case VDC_DOMAIN_GATE_SOURCE_OUT_OF_RANGE:
    case VDC_DOMAIN_GATE_BAD_WINDOW_CLASS:
    case VDC_DOMAIN_GATE_PAYLOAD_WINDOW_FORBIDDEN:
    default:
        return false;
    }
}

/* Generation metadata is the provenance fence between a live evidence sample
 * and the immutable directed path table used to correct it.  Provisional
 * training tables intentionally have no bias generation and remain
 * diagnostic-only; they must not be turned into a formal admission failure
 * merely because that generation has not been minted yet. */
bool vdc_domain_validate_evidence_generations(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_gate_result_t *gate)
{
    if (context == NULL || evidence == NULL) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }

    const vdc_path_delay_table_t *table = &context->path_delay;
    if (table->bias_generation == 0u ||
        (table->flags & VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) != 0u) {
        return true;
    }

    if (table->calibration_generation == 0u ||
        evidence->delay_generation == 0u ||
        evidence->delay_generation != table->calibration_generation) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_DELAY_GENERATION,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->bias_generation == 0u ||
        evidence->bias_generation != table->bias_generation) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BIAS_GENERATION,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    return true;
}

/* A process image carries an origin phase and a receiver-local latch. The
 * latter cannot enter a MASTER servo until it has a common-time mapping. The
 * reference node's returned-loop sample is the exception because both ends
 * use the same counter. FOLLOWER still retains raw diagnostic evidence while
 * its PI/DCO path remains bypassed. */
bool vdc_domain_validate_evidence_phase_domain(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_gate_result_t *gate)
{
    if (context == NULL || evidence == NULL) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }

    const uint32_t flags = evidence->correlation_flags;
    if (flags == 0u || vdc_domain_is_follower(context)) {
        return true;
    }
    const uint32_t required = TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
                              TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME;
    const bool common_time = (flags & required) == required;
    const bool same_clock =
        (flags & TDMA_RING_CLOCK_OBSERVATION_FLAG_LOCAL_PHASE_SAME_CLOCK) != 0u;
    const bool common_mapped = (flags &
        TDMA_RING_CLOCK_OBSERVATION_FLAG_LOCAL_PHASE_COMMON_MAPPED) != 0u;
    if (!common_time || (!same_clock && !common_mapped)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    return true;
}

/* Debug continuation is deliberately not a hidden acceptance.  The failed
 * evidence stays out of the PI/DCO path and the raw gate is retained in the
 * DPLL snapshot.  The product rejection fields remain PASS so debug tooling
 * can continue collecting and tuning without treating a recoverable sample
 * miss as a node failure. */
void vdc_domain_record_debug_continue(
    vdc_domain_context_t *context,
    const vdc_gate_result_t *gate)
{
    if (context == NULL || gate == NULL) {
        return;
    }
    if (context->dpll.debug_continue_count != UINT32_MAX) {
        context->dpll.debug_continue_count++;
    }
    context->dpll.last_debug_gate_code = gate->reject_code;
    context->dpll.last_debug_gate_slot = gate->reject_slot;
    context->dpll.last_debug_gate_evidence = gate->reject_evidence;
    context->dpll.last_reject_code = VDC_DOMAIN_GATE_PASS;
    context->quality.last_reject_code = VDC_DOMAIN_GATE_PASS;
    context->quality.gate_reject_code = VDC_DOMAIN_GATE_PASS;
    context->quality.gate_reject_slot = 0u;
    context->quality.gate_reject_evidence = 0u;
    context->gate.passed = 1u;
    context->gate.reject_code = VDC_DOMAIN_GATE_PASS;
    context->gate.reject_slot = 0u;
    context->gate.reject_evidence = 0u;
    context->dpll.update_seq++;
    vdc_domain_refresh_quality_state(context);
}

void vdc_domain_reset_lock_acquisition(vdc_domain_context_t *context)
{
    if (context == NULL) {
        return;
    }

    context->dpll.accepted_sample_count = 0u;
    context->dpll.loop_filter_integrator_ppb = 0;
    context->dpll.last_raw_phase_error_ns = 0;
    context->dpll.last_expected_window_start_ns = 0u;
    context->dpll.last_observed_time_ns = 0u;
    context->phase_observed_anchor_ns = 0u;
    context->phase_observed_anchor_valid = 0u;
    context->quality.accepted_sample_count = 0u;
    context->quality.consecutive_good_samples = 0u;
    context->quality.consecutive_coarse_samples = 0u;
    context->quality.consecutive_debug_samples = 0u;
    context->quality.consecutive_fine_samples = 0u;
}

/* Keep the physical identity and raw timestamp measurements alongside the
 * derived DPLL state.  The maintenance capture consumes these fields to
 * distinguish an observed TDMA source from a FOLLOWER's selected master. */
void vdc_domain_record_observation_metadata(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    if (context == NULL || evidence == NULL) {
        return;
    }
    context->dpll.last_observed_source_slot_id = evidence->source_slot_id;
    context->dpll.last_observed_reference_slot_id =
        evidence->reference_slot_id;
    context->dpll.last_observed_payload_class = evidence->payload_class;
    context->dpll.last_observed_delay_ns = evidence->delay_ns;
    context->dpll.last_observed_jitter_ns = evidence->jitter_ns;
    context->dpll.last_observed_frame_crc32 = evidence->frame_crc32;
    context->dpll.last_observed_sample_crc32 = evidence->sample_crc32;
    context->dpll.last_observed_timestamp_source = evidence->timestamp_source;
    context->dpll.last_observed_timestamp_resolution_ns =
        evidence->timestamp_resolution_ns;
    context->dpll.last_observed_timestamp_flags = evidence->timestamp_flags;
    context->dpll.last_observed_raw_phase_error_ns = evidence->phase_error_ns;
    context->dpll.last_observed_delay_generation = evidence->delay_generation;
    context->dpll.last_observed_bias_generation = evidence->bias_generation;
    context->dpll.last_observed_correlation_flags = evidence->correlation_flags;
    context->dpll.last_observed_reference_tx_phase_ns =
        evidence->reference_tx_phase_ns;
    context->dpll.last_observed_local_rx_phase_ns = evidence->local_rx_phase_ns;
    context->dpll.last_observed_common_effective_time_ns =
        evidence->common_effective_time_ns;
    context->dpll.last_observed_reference_tx_timestamp_ns =
        evidence->reference_tx_timestamp_ns;
    context->dpll.last_observed_local_rx_timestamp_ns =
        evidence->local_rx_timestamp_ns;
}

uint32_t vdc_domain_lock_state_after_reject(
    const vdc_domain_context_t *context,
    bool reacquire)
{
    if (context == NULL || context->ready == 0u) {
        return VDC_DOMAIN_LOCK_OFF;
    }
    if (reacquire) {
        return VDC_DOMAIN_LOCK_CHECKING;
    }

    return context->dpll.state;
}

void vdc_domain_record_accepted_sample(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    uint64_t local_apply_time_ns)
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
    context->quality.last_sample_time_ns = local_apply_time_ns != 0u
        ? local_apply_time_ns : vdc_domain_evidence_time_ns(evidence);
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
    const uint32_t lock_threshold_ns =
        vdc_domain_effective_lock_acceptance_threshold_ns(context);
    if (abs_phase <= lock_threshold_ns) {
        if (context->quality.consecutive_good_samples != UINT32_MAX) {
            context->quality.consecutive_good_samples++;
        }
    } else {
        context->quality.consecutive_good_samples = 0u;
    }

    context->quality.quality_flags = 0u;
    if (abs_phase > vdc_domain_effective_lock_acceptance_threshold_ns(context)) {
        context->quality.quality_flags |=
            VDC_DOMAIN_QUALITY_FLAG_PHASE_OUT_OF_LOCK;
    }
    const uint32_t diagnostic_threshold_ns =
        vdc_domain_effective_phase_diagnostic_threshold_ns(context);
    if (diagnostic_threshold_ns != 0u && abs_phase > diagnostic_threshold_ns) {
        context->quality.quality_flags |= VDC_DOMAIN_QUALITY_FLAG_PHASE_LARGE;
    }
    if (vdc_domain_abs_i32(context->clock.period_adjust_ppb) >=
        context->servo.sanity_freq_limit_ppb) {
        context->quality.quality_flags |= VDC_DOMAIN_QUALITY_FLAG_RATE_LIMITED;
    }

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

void vdc_domain_update_clock_from_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    int32_t input_residual_ns,
    uint64_t local_apply_time_ns)
{
    if (context == NULL || evidence == NULL) {
        return;
    }

    const uint32_t next_dco_seq = context->dco.dco_update_seq + 1u;
    int32_t frequency_error_ppb = context->dpll.last_frequency_error_ppb;
    int32_t integrator_delta_ppb = 0;
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
        integrator_delta_ppb = vdc_domain_integrator_delta_ppb(
            input_residual_ns,
            context->servo.ki_q16,
            context->servo.update_period_us,
            context->servo.sanity_freq_limit_ppb);
    }

    /* Type-II loop filter: the FLL slope estimate handles fast acquisition;
     * Ki accumulates a phase-derived rate correction across accepted samples.
     * Do not integrate farther into a saturated correction (anti-windup).
     * All intermediate arithmetic is signed 64-bit so debug SCPI values may
     * be deliberately extreme without overflowing the realtime state. */
    const uint32_t limit_u32 = context->servo.sanity_freq_limit_ppb;
    const int64_t limit_ppb = limit_u32 > (uint32_t)INT32_MAX
        ? (int64_t)INT32_MAX : (int64_t)limit_u32;
    const int64_t unsaturated_correction =
        (int64_t)frequency_error_ppb +
        (int64_t)context->dpll.loop_filter_integrator_ppb +
        (int64_t)integrator_delta_ppb;
    const int64_t negative_rate_limit = context->reference_baseline_ppb != 0 &&
        limit_ppb > 999999999 ? 999999999 : limit_ppb;
    const bool winds_further_positive =
        unsaturated_correction > (int64_t)context->reference_baseline_ppb + negative_rate_limit &&
        integrator_delta_ppb > 0;
    const bool winds_further_negative =
        unsaturated_correction < (int64_t)context->reference_baseline_ppb - limit_ppb &&
        integrator_delta_ppb < 0;
    if (!winds_further_positive && !winds_further_negative) {
        context->dpll.loop_filter_integrator_ppb = vdc_domain_clamp_ppb(
            (int64_t)context->dpll.loop_filter_integrator_ppb +
                (int64_t)integrator_delta_ppb,
            limit_u32);
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
    const int32_t residual_rate_ppb =
        -vdc_domain_clamp_ppb((int64_t)frequency_error_ppb +
                                  (int64_t)context->dpll.loop_filter_integrator_ppb,
                              context->servo.sanity_freq_limit_ppb);
    int32_t period_adjust_ppb = vdc_domain_clamp_ppb(
        (int64_t)context->reference_baseline_ppb + residual_rate_ppb,
        context->servo.sanity_freq_limit_ppb);
    if (context->reference_baseline_ppb != 0 && period_adjust_ppb <= -1000000000)
        period_adjust_ppb = -999999999;

    context->dpll.last_frequency_error_ppb = frequency_error_ppb;
    if (update_rate_anchor) {
        context->dpll.last_raw_phase_error_ns = evidence->phase_error_ns;
        context->dpll.last_expected_window_start_ns =
            evidence->expected_window_start_ns;
        context->dpll.last_observed_time_ns = evidence->observed_time_ns;
    }

    context->phase_observed_anchor_ns = evidence->observed_time_ns;
    context->phase_observed_anchor_valid = 1u;

    /* observed_time_ns is a logical TDMA coordinate. The output planner
     * consumes TIMER1-origin nanoseconds, so rebase at the service boundary.
     * Direct host callers without an owner timestamp retain the legacy
     * synthetic coordinate for compatibility; the live manager always fills
     * local_apply_time_ns before this function is reached. */
    uint64_t base_local_tick64 = local_apply_time_ns;
    uint64_t base_vdc_time64_ns = evidence->observed_time_ns;
    if (base_local_tick64 == 0u) {
        base_local_tick64 = evidence->observed_time_ns;
    } else if (context->dco.valid != 0u &&
               base_local_tick64 >= context->dco.base_local_tick64) {
        uint64_t old_output_ns = 0u;
        if (vdc_domain_dco_local_to_output_ns(
                &context->dco, base_local_tick64, &old_output_ns)) {
            /* Preserve the old physical output at the rebase instant; only
             * the deliberately computed phase correction may step it. */
            base_vdc_time64_ns = old_output_ns;
        }
    }

    context->clock.valid = 1u;
    context->clock.model_seq++;
    context->clock.epoch_id = context->schedule.schedule_epoch;
    context->clock.base_local_tick64 = base_local_tick64;
    context->clock.base_vdc_time64_ns = base_vdc_time64_ns;
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
    vdc_domain_request_oscillator_discipline(
        context,
        period_adjust_ppb,
        context->schedule.local_slot_id,
        evidence->sample_seq,
        evidence->observed_time_ns);
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
