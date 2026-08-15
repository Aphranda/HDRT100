#include "vdc_domain.h"

#include <limits.h>
#include <string.h>

#define VDC_DOMAIN_CRC_OFFSET 2166136261u
#define VDC_DOMAIN_CRC_PRIME 16777619u
#define VDC_DOMAIN_INITIAL_LOCK_SAMPLES 1u
#define VDC_DOMAIN_FREQ_LOCK_SAMPLES 2u
#define VDC_DOMAIN_PHASE_LOCK_SAMPLES 3u

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

void vdc_domain_default_schedule(vdc_tdma_schedule_profile_t *profile,
                                 uint32_t local_slot_id,
                                 uint32_t reference_slot_id)
{
    if (profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    profile->enabled = 1u;
    profile->schedule_version = 1u;
    profile->schedule_epoch = 1u;
    profile->period_ns = VDC_DOMAIN_DEFAULT_PERIOD_NS;
    profile->observation_window_offset_ns = 0u;
    profile->observation_window_width_ns = VDC_DOMAIN_DEFAULT_OBSERVATION_WIDTH_NS;
    profile->guard_before_ns = VDC_DOMAIN_DEFAULT_GUARD_NS;
    profile->guard_after_ns = VDC_DOMAIN_DEFAULT_GUARD_NS;
    profile->reference_slot_id =
        reference_slot_id < VDC_DOMAIN_NODE_COUNT ? reference_slot_id : 0u;
    profile->local_slot_id =
        local_slot_id < VDC_DOMAIN_NODE_COUNT ? local_slot_id : 0u;
    profile->schedule_crc32 = vdc_domain_schedule_crc32(profile);
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
    profile->update_period_1e3ns = 1000u;
    profile->first_step_threshold_ns = 100000u;
    profile->step_threshold_ns = 10000u;
    profile->sanity_freq_limit_ppb = 50000u;
    profile->offset_lock_threshold_ns = 100u;
    profile->lock_sample_count = 4u;
    profile->outlier_threshold_ns = 10000u;
    profile->reset_policy = 0u;
    profile->servo_profile_crc32 = VDC_DOMAIN_DEFAULT_SERVO_PROFILE_CRC32;
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
    hash = vdc_domain_hash_u32(hash, profile->guard_before_ns);
    hash = vdc_domain_hash_u32(hash, profile->guard_after_ns);
    hash = vdc_domain_hash_u32(hash, profile->reference_slot_id);
    hash = vdc_domain_hash_u32(hash, profile->local_slot_id);
    return hash;
}

bool vdc_domain_schedule_validate(const vdc_tdma_schedule_profile_t *profile)
{
    if (profile == NULL ||
        profile->enabled == 0u ||
        profile->period_ns == 0u ||
        profile->observation_window_width_ns == 0u ||
        profile->reference_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        profile->local_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        profile->observation_window_offset_ns >= profile->period_ns) {
        return false;
    }
    const uint64_t window_end =
        (uint64_t)profile->observation_window_offset_ns +
        (uint64_t)profile->observation_window_width_ns;
    if (window_end > profile->period_ns) {
        return false;
    }
    return profile->schedule_crc32 == vdc_domain_schedule_crc32(profile);
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
    model->slew_limit_ppb = 50000u;
    model->tdma_schedule_crc32 = schedule_crc32;
    model->servo_profile_crc32 = VDC_DOMAIN_DEFAULT_SERVO_PROFILE_CRC32;
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

bool vdc_domain_validate_tdma_timestamp_evidence(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool require_dpll_eligible,
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
    if (!vdc_domain_schedule_validate(profile)) {
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
    if (require_dpll_eligible &&
        ((evidence->timestamp_flags & VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) == 0u ||
         (evidence->timestamp_flags & VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u ||
         evidence->timestamp_source != VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (require_dpll_eligible &&
        (evidence->timestamp_resolution_ns == 0u ||
         evidence->timestamp_resolution_ns >
             VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_TIMESTAMP_RESOLUTION,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }

    const uint64_t cycle_offset =
        evidence->expected_window_start_ns % (uint64_t)profile->period_ns;
    const uint64_t window_min = evidence->expected_window_start_ns >
                                        profile->guard_before_ns
                                    ? evidence->expected_window_start_ns -
                                          profile->guard_before_ns
                                    : 0u;
    const uint64_t window_max =
        evidence->expected_window_start_ns +
        (uint64_t)profile->observation_window_width_ns +
        (uint64_t)profile->guard_after_ns;
    if (cycle_offset != profile->observation_window_offset_ns ||
        evidence->observed_time_ns < window_min ||
        evidence->observed_time_ns > window_max) {
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
    context->dpll.state = VDC_DOMAIN_LOCK_OFF;
    context->dpll.schedule_crc32 = context->schedule.schedule_crc32;
    context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
    return true;
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
        return;
    }
    if (context->dpll.state == VDC_DOMAIN_LOCK_OFF) {
        context->dpll.state = VDC_DOMAIN_LOCK_CHECKING;
    }
}

bool vdc_domain_publish_clock_model(vdc_domain_context_t *context,
                                    const vdc_clock_model_t *model)
{
    if (context == NULL || model == NULL || model->valid == 0u ||
        model->tdma_schedule_crc32 != context->schedule.schedule_crc32 ||
        model->servo_profile_crc32 != context->servo.servo_profile_crc32) {
        return false;
    }

    context->clock = *model;
    context->clock.model_seq++;
    context->dpll.update_seq++;
    return true;
}

bool vdc_domain_submit_tdma_evidence(vdc_domain_context_t *context,
                                     const vdc_tdma_timestamp_evidence_t *evidence)
{
    vdc_gate_result_t gate;
    if (context == NULL || evidence == NULL) {
        return false;
    }

    if (!vdc_domain_validate_tdma_timestamp_evidence(&context->schedule,
                                                     evidence,
                                                     true,
                                                     &gate)) {
        context->gate = gate;
        context->dpll.rejected_sample_count++;
        context->dpll.last_reject_code = gate.reject_code;
        context->dpll.update_seq++;
        if (context->ready != 0u) {
            context->dpll.state = VDC_DOMAIN_LOCK_CHECKING;
        }
        return false;
    }

    const uint32_t abs_phase = vdc_domain_abs_i32(evidence->phase_error_ns);
    context->gate = gate;
    context->dpll.accepted_sample_count++;
    context->dpll.last_sample_seq = evidence->sample_seq;
    context->dpll.last_reject_code = VDC_DOMAIN_GATE_PASS;
    context->dpll.last_phase_error_ns = evidence->phase_error_ns;
    context->dpll.last_offset_ns = evidence->phase_error_ns;
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
               abs_phase > context->servo.offset_lock_threshold_ns) {
        context->dpll.state = VDC_DOMAIN_LOCK_PHASE_LOCK;
    } else {
        context->dpll.state = VDC_DOMAIN_LOCK_LOCKED;
    }

    return true;
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
    snapshot->dpll = context->dpll;
    snapshot->gate = context->gate;
    return true;
}
