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

static uint32_t vdc_domain_saturate_u64_to_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
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
    profile->refmem_data_window_offset_ns = VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_OFFSET_NS;
    profile->refmem_data_window_width_ns = VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_WIDTH_NS;
    profile->idle_beacon_window_offset_ns = VDC_DOMAIN_DEFAULT_IDLE_WINDOW_OFFSET_NS;
    profile->idle_beacon_window_width_ns = VDC_DOMAIN_DEFAULT_IDLE_WINDOW_WIDTH_NS;
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
    hash = vdc_domain_hash_u32(hash, profile->refmem_data_window_offset_ns);
    hash = vdc_domain_hash_u32(hash, profile->refmem_data_window_width_ns);
    hash = vdc_domain_hash_u32(hash, profile->idle_beacon_window_offset_ns);
    hash = vdc_domain_hash_u32(hash, profile->idle_beacon_window_width_ns);
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
        profile->reference_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        profile->local_slot_id >= VDC_DOMAIN_NODE_COUNT) {
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

    const uint64_t cycle_offset =
        frame->window_start_ns % (uint64_t)profile->period_ns;
    if (cycle_offset != expected_offset_ns) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_WINDOW_BOUND,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    const uint64_t window_min =
        frame->window_start_ns > profile->guard_before_ns
            ? frame->window_start_ns - profile->guard_before_ns
            : 0u;
    const uint64_t window_max =
        frame->window_start_ns + (uint64_t)expected_width_ns +
        (uint64_t)profile->guard_after_ns;
    if (frame->timestamp.observed_time_ns < window_min ||
        frame->timestamp.observed_time_ns > window_max ||
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
    vdc_domain_default_dco_control(&context->dco,
                                   &context->clock,
                                   context->dpll.state);
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
    const uint32_t next_dco_seq =
        context != NULL ? context->dco.dco_update_seq + 1u : 0u;

    if (context == NULL || model == NULL || model->valid == 0u ||
        model->tdma_schedule_crc32 != context->schedule.schedule_crc32 ||
        model->servo_profile_crc32 != context->servo.servo_profile_crc32) {
        return false;
    }

    context->clock = *model;
    context->clock.model_seq++;
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
    snapshot->dco = context->dco;
    snapshot->dpll = context->dpll;
    snapshot->gate = context->gate;
    return true;
}
