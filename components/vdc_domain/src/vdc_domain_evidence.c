#include "vdc_domain.h"

#include "tdma_ring_runtime.h"

#include <limits.h>
#include <string.h>

#include "tdma_operating_profile.h"

#include "vdc_domain_internal.h"

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

bool vdc_domain_active_schedule_validate(
    const vdc_tdma_schedule_profile_t *profile);

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
    /* Run the VDC/DPLL chain at the ring's own slot period (500/750 us).  The
     * authored windows are absolute nanoseconds laid out for the 1 ms default,
     * so they are re-derived proportionally whenever the period is adopted; the
     * ring's effective schedule CRC is adopted as authoritative and checked by
     * the period-agnostic active validator (the self-hashing one folds
     * period_ns into the CRC and can never accept an adopted period). */
    if (binding->cycle_period_ns != VDC_DOMAIN_DEFAULT_PERIOD_NS) {
        const uint32_t period_ns = binding->cycle_period_ns;
        updated.observation_window_offset_ns = 0u;
        updated.observation_window_width_ns = period_ns / 100u;
        updated.refmem_data_window_offset_ns = period_ns * 2u / 100u;
        updated.refmem_data_window_width_ns = period_ns * 80u / 100u;
        updated.idle_beacon_window_offset_ns = period_ns * 90u / 100u;
        updated.idle_beacon_window_width_ns = period_ns * 5u / 100u;
    }
    updated.period_ns = binding->cycle_period_ns;
    if (!tdma_ring_profile_default(&updated.ring_binding,
                                   binding->local_slot_id,
                                   binding->reference_slot_id,
                                   binding->node_count) ||
        updated.ring_binding.profile_crc32 != binding->ring_profile_crc32) {
        return false;
    }
    updated.operating_profile_crc32 = binding->operating_profile_crc32;
    /* Restore the effective-CRC cross-check: the ring derives its effective
     * schedule CRC by composing its authored base identity with the operating
     * profile CRC, so a tampered binding must still be rejected (the adopted
     * period cannot be validated by the self-hash below). */
    vdc_tdma_schedule_profile_t identity = *base;
    if (identity.operating_profile_crc32 != 0u &&
        !vdc_domain_default_schedule_for_topology(&identity,
                                                  binding->local_slot_id,
                                                  binding->reference_slot_id,
                                                  binding->node_count)) {
        return false;
    }
    identity.operating_profile_crc32 = 0u;
    if (tdma_operating_profile_compose_schedule_crc32(
            vdc_domain_schedule_crc32(&identity),
            binding->operating_profile_crc32) !=
        binding->effective_schedule_crc32) {
        return false;
    }
    updated.schedule_crc32 = binding->effective_schedule_crc32;
    if (!vdc_domain_active_schedule_validate(&updated)) {
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
    /* Start with a conservative loop. Debug tuning can replace these
     * coefficients at runtime without changing product gates. */
    profile->kp_q16 = VDC_DOMAIN_DEFAULT_SERVO_KP_Q16;
    profile->ki_q16 = VDC_DOMAIN_DEFAULT_SERVO_KI_Q16;
    profile->update_period_us = 1000u;
    profile->first_step_threshold_ns = 100000u;
    profile->step_threshold_ns = 10000u;
    profile->sanity_freq_limit_ppb = VDC_DOMAIN_DEFAULT_SANITY_FREQ_LIMIT_PPB;
    profile->offset_lock_threshold_ns = VDC_DOMAIN_LOCK_TIER_FINE_NS;
    profile->debug_lock_threshold_ns = VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    profile->coarse_lock_threshold_ns = VDC_DOMAIN_LOCK_TIER_COARSE_NS;
    profile->lock_acceptance_threshold_ns = VDC_DOMAIN_LOCK_TIER_DEBUG_NS;
    profile->lock_sample_count = 4u;
    profile->phase_diagnostic_threshold_ns = 10000u;
    profile->reset_policy = 0u;
    profile->servo_profile_crc32 = vdc_domain_servo_profile_crc32(profile);
}

uint32_t vdc_domain_servo_profile_crc32(const vdc_servo_profile_t *profile)
{
    if (profile == NULL) {
        return 0u;
    }
    uint32_t hash = VDC_DOMAIN_CRC_OFFSET;
    hash = vdc_domain_hash_u32(hash, profile->enabled);
    hash = vdc_domain_hash_u32(hash, profile->servo_type);
    hash = vdc_domain_hash_u32(hash, (uint32_t)profile->kp_q16);
    hash = vdc_domain_hash_u32(hash, (uint32_t)profile->ki_q16);
    hash = vdc_domain_hash_u32(hash, profile->update_period_us);
    hash = vdc_domain_hash_u32(hash, profile->first_step_threshold_ns);
    hash = vdc_domain_hash_u32(hash, profile->step_threshold_ns);
    hash = vdc_domain_hash_u32(hash, profile->sanity_freq_limit_ppb);
    hash = vdc_domain_hash_u32(hash, profile->offset_lock_threshold_ns);
    hash = vdc_domain_hash_u32(hash, profile->debug_lock_threshold_ns);
    hash = vdc_domain_hash_u32(hash, profile->coarse_lock_threshold_ns);
    hash = vdc_domain_hash_u32(hash, profile->lock_acceptance_threshold_ns);
    hash = vdc_domain_hash_u32(hash, profile->lock_sample_count);
    hash = vdc_domain_hash_u32(hash, profile->phase_diagnostic_threshold_ns);
    return vdc_domain_hash_u32(hash, profile->reset_policy);
}

bool vdc_domain_apply_debug_servo_profile(
    vdc_domain_context_t *context,
    const vdc_servo_profile_t *profile)
{
    if (context == NULL || profile == NULL) {
        return false;
    }

    /* Debug tuning deliberately retains every representable coefficient.  It
     * resets acquisition and marks the snapshot CHECKING so a tuned loop has
     * to earn fresh evidence; it never promotes an existing LOCKED state. */
    vdc_servo_profile_t updated = *profile;
    updated.servo_profile_crc32 = vdc_domain_servo_profile_crc32(&updated);
    context->servo = updated;
    vdc_domain_reset_lock_acquisition(context);
    context->dpll.last_frequency_error_ppb = 0;
    context->dpll.last_phase_error_ns = 0;
    context->dpll.last_offset_ns = 0;
    context->dpll.rms_offset_ns = 0u;
    context->dpll.max_abs_offset_ns = 0u;
    context->dpll.jitter_pk_ns = 0u;
    context->quality.rejected_sample_count = 0u;
    context->quality.last_reject_code = VDC_DOMAIN_GATE_PASS;
    context->clock.model_seq++;
    context->clock.slew_limit_ppb = updated.sanity_freq_limit_ppb;
    context->clock.servo_profile_crc32 = updated.servo_profile_crc32;
    context->dpll.servo_profile_crc32 = updated.servo_profile_crc32;
    context->dpll.state = context->ready != 0u
        ? VDC_DOMAIN_LOCK_CHECKING : VDC_DOMAIN_LOCK_OFF;
    context->dpll.update_seq++;
    context->dco.dco_update_seq++;
    context->dco.source_model_seq = context->clock.model_seq;
    context->dco.slew_limit_ppb = updated.sanity_freq_limit_ppb;
    context->dco.servo_profile_crc32 = updated.servo_profile_crc32;
    context->dco.lock_state = context->dpll.state;
    vdc_domain_refresh_quality_state(context);
    return true;
}

bool vdc_domain_set_debug_continue(vdc_domain_context_t *context,
                                   bool enabled)
{
    if (context == NULL) {
        return false;
    }
    const uint32_t next_enabled = enabled ? 1u : 0u;
    if (context->dpll.debug_continue_enabled == next_enabled) {
        return true;
    }

    context->dpll.debug_continue_enabled = next_enabled;
    context->dpll.debug_continue_generation++;
    if (context->dpll.debug_continue_generation == 0u) {
        context->dpll.debug_continue_generation = 1u;
    }
    context->dpll.debug_continue_count = 0u;
    context->dpll.last_debug_gate_code = VDC_DOMAIN_GATE_PASS;
    context->dpll.last_debug_gate_slot = 0u;
    context->dpll.last_debug_gate_evidence = 0u;
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
    return true;
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
    if (profile->operating_profile_crc32 != 0u) {
        /* A run-time schedule carries the ring-authoritative effective CRC
         * (composed from the authored 1 ms identity), so the self-hash below
         * cannot apply; the period-aware active validator covers it and every
         * other runtime consumer reaches this same function. */
        return vdc_domain_active_schedule_validate(profile);
    }
    return profile->schedule_crc32 == vdc_domain_schedule_crc32(profile);
}

bool vdc_domain_active_schedule_validate(
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
    vdc_servo_profile_t default_servo;
    vdc_domain_default_servo(&default_servo);
    model->servo_profile_crc32 = default_servo.servo_profile_crc32;
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

void vdc_domain_default_dpll_control_profile(
    vdc_dpll_control_profile_t *profile)
{
    if (profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    /* Legacy records have no cluster-wide role fact. MASTER preserves the
     * existing local-servo behaviour until a control-plane transaction
     * explicitly installs a follower profile. */
    profile->valid = 1u;
    profile->version = VDC_DPLL_CONTROL_PROFILE_VERSION;
    profile->mode = VDC_DPLL_CONTROL_MODE_MASTER;
    profile->generation = 1u;
}

void vdc_domain_default_oscillator_discipline_profile(
    vdc_oscillator_discipline_profile_t *profile)
{
    if (profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    profile->valid = 1u;
    profile->version = VDC_OSCILLATOR_DISCIPLINE_PROFILE_VERSION;
    profile->actuator_polarity = 1;
    profile->generation = 1u;
}

static bool vdc_domain_validate_dpll_control_profile(
    const vdc_domain_context_t *context,
    const vdc_dpll_control_profile_t *profile)
{
    if (context == NULL || profile == NULL || profile->valid != 1u ||
        profile->version != VDC_DPLL_CONTROL_PROFILE_VERSION ||
        (profile->mode != VDC_DPLL_CONTROL_MODE_MASTER &&
         profile->mode != VDC_DPLL_CONTROL_MODE_FOLLOWER)) {
        return false;
    }
    if (profile->mode == VDC_DPLL_CONTROL_MODE_FOLLOWER &&
        (profile->follow_master_slot_id >=
             context->schedule.ring_binding.node_count ||
         profile->follow_master_slot_id == context->schedule.local_slot_id)) {
        return false;
    }
    return true;
}

bool vdc_domain_set_dpll_control_profile(
    vdc_domain_context_t *context,
    const vdc_dpll_control_profile_t *profile)
{
    if (!vdc_domain_validate_dpll_control_profile(context, profile)) {
        return false;
    }

    const bool role_or_source_changed =
        context->control.profile.mode != profile->mode ||
        context->control.profile.follow_master_slot_id !=
            profile->follow_master_slot_id;
    if (!role_or_source_changed) {
        return true;
    }

    vdc_dpll_control_profile_t applied = *profile;
    applied.generation =
        vdc_domain_increment_nonzero(context->control.profile.generation);
    context->control.profile = applied;
    /* The held clock/DCO already contains the last physical rate. Retire
     * its reference attribution on a role/source change without changing
     * output coordinates; the new role acquires its own correction history. */
    context->reference_baseline_ppb = 0;
    context->control.last_follower_source_slot_id = 0u;
    context->control.last_follower_control_generation = 0u;
    context->control.last_follower_command_seq = 0u;
    context->control.last_follower_quality = 0u;
    context->control.last_follower_effective_vdc_time_ns = 0u;

    /* Role changes keep the physical output rate/phase intact, but all local
     * PI and promotion history belongs to the previous control role. */
    vdc_domain_reset_lock_acquisition(context);
    context->dpll.rejected_sample_count = 0u;
    context->dpll.last_reject_code = VDC_DOMAIN_GATE_PASS;
    context->dpll.last_frequency_error_ppb = 0;
    context->dpll.last_phase_error_ns = 0;
    context->dpll.last_offset_ns = 0;
    context->dpll.rms_offset_ns = 0u;
    context->dpll.max_abs_offset_ns = 0u;
    context->dpll.jitter_pk_ns = 0u;
    memset(&context->gate, 0, sizeof(context->gate));
    vdc_domain_init_quality(context);
    context->dpll.state = context->ready != 0u
        ? VDC_DOMAIN_LOCK_CHECKING : VDC_DOMAIN_LOCK_OFF;
    context->dpll.schedule_crc32 = context->schedule.schedule_crc32;
    context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
    context->dpll.update_seq++;
    vdc_domain_sync_dco_lock_state(context);
    return true;
}

static bool vdc_domain_validate_oscillator_discipline_profile(
    const vdc_oscillator_discipline_profile_t *profile)
{
    if (profile == NULL || profile->valid != 1u ||
        profile->version != VDC_OSCILLATOR_DISCIPLINE_PROFILE_VERSION ||
        profile->enabled > 1u) {
        return false;
    }
    if (profile->enabled != 0u &&
        (profile->minimum_update_interval_us == 0u ||
         profile->max_abs_trim_ppb == 0u || profile->max_step_ppb == 0u ||
         (profile->actuator_polarity != 1 &&
          profile->actuator_polarity != -1))) {
        return false;
    }
    return true;
}

bool vdc_domain_set_oscillator_discipline_profile(
    vdc_domain_context_t *context,
    const vdc_oscillator_discipline_profile_t *profile)
{
    if (context == NULL ||
        !vdc_domain_validate_oscillator_discipline_profile(profile)) {
        return false;
    }

    vdc_oscillator_discipline_profile_t applied = *profile;
    applied.generation = vdc_domain_increment_nonzero(
        context->oscillator_discipline.profile.generation);
    context->oscillator_discipline.profile = applied;
    context->oscillator_discipline.freeze_reason = applied.enabled == 0u
        ? VDC_OSCILLATOR_DISCIPLINE_FREEZE_DISABLED
        : VDC_OSCILLATOR_DISCIPLINE_FREEZE_NONE;
    return true;
}

bool vdc_domain_report_oscillator_discipline_actuator(
    vdc_domain_context_t *context,
    const vdc_oscillator_discipline_actuator_report_t *report)
{
    if (context == NULL || report == NULL || report->valid != 1u ||
        report->available > 1u || report->healthy > 1u ||
        (report->available == 0u && report->healthy != 0u) ||
        report->applied_generation >
            context->oscillator_discipline.request_generation) {
        return false;
    }

    context->oscillator_discipline.actuator_available = report->available;
    context->oscillator_discipline.actuator_healthy = report->healthy;
    context->oscillator_discipline.applied_generation =
        report->applied_generation;
    context->oscillator_discipline.applied_trim_ppb =
        report->applied_trim_ppb;
    return true;
}

static void vdc_domain_oscillator_freeze(vdc_domain_context_t *context,
                                         uint32_t reason)
{
    if (context == NULL) {
        return;
    }
    context->oscillator_discipline.freeze_reason = reason;
    vdc_domain_increment_saturating(
        &context->oscillator_discipline.freeze_count);
}

void vdc_domain_request_oscillator_discipline(
    vdc_domain_context_t *context,
    int32_t rate_source_ppb,
    uint32_t source_slot_id,
    uint32_t source_command_seq,
    uint64_t now_ns)
{
    if (context == NULL) {
        return;
    }

    vdc_oscillator_discipline_status_t *discipline =
        &context->oscillator_discipline;
    const vdc_oscillator_discipline_profile_t *profile =
        &discipline->profile;
    if (profile->enabled == 0u) {
        vdc_domain_oscillator_freeze(
            context, VDC_OSCILLATOR_DISCIPLINE_FREEZE_DISABLED);
        return;
    }
    if (discipline->actuator_available == 0u) {
        vdc_domain_increment_saturating(&discipline->unavailable_count);
        vdc_domain_oscillator_freeze(
            context, VDC_OSCILLATOR_DISCIPLINE_FREEZE_UNAVAILABLE);
        return;
    }
    if (discipline->actuator_healthy == 0u) {
        vdc_domain_increment_saturating(&discipline->fault_count);
        vdc_domain_oscillator_freeze(
            context, VDC_OSCILLATOR_DISCIPLINE_FREEZE_FAULT);
        return;
    }
    if (vdc_domain_abs_i32(rate_source_ppb) > profile->max_abs_trim_ppb) {
        vdc_domain_increment_saturating(&discipline->limit_count);
        vdc_domain_oscillator_freeze(
            context, VDC_OSCILLATOR_DISCIPLINE_FREEZE_LIMIT);
        return;
    }
    if (discipline->request_generation != 0u) {
        if (now_ns < discipline->last_request_time_ns) {
            vdc_domain_increment_saturating(&discipline->stale_count);
            vdc_domain_oscillator_freeze(
                context, VDC_OSCILLATOR_DISCIPLINE_FREEZE_STALE_SOURCE);
            return;
        }
        if (now_ns - discipline->last_request_time_ns <
            (uint64_t)profile->minimum_update_interval_us * 1000ull) {
            return;
        }
    }

    const int32_t target_ppb = vdc_domain_clamp_ppb(
        (int64_t)rate_source_ppb * (int64_t)profile->actuator_polarity,
        profile->max_abs_trim_ppb);
    discipline->requested_trim_ppb = vdc_domain_slew_i32(
        discipline->requested_trim_ppb, target_ppb, profile->max_step_ppb);
    discipline->request_generation =
        vdc_domain_increment_nonzero(discipline->request_generation);
    discipline->last_request_time_ns = now_ns;
    discipline->last_source_slot_id = source_slot_id;
    discipline->last_source_command_seq = source_command_seq;
    discipline->freeze_reason = VDC_OSCILLATOR_DISCIPLINE_FREEZE_NONE;
}

bool vdc_domain_apply_follower_command(
    vdc_domain_context_t *context,
    const vdc_dpll_follower_command_t *command)
{
    if (context == NULL || command == NULL || !vdc_domain_is_follower(context)) {
        return false;
    }

    const uint32_t expected_source =
        context->control.profile.follow_master_slot_id;
    if (command->source_slot_id >= context->schedule.ring_binding.node_count ||
        command->source_slot_id != expected_source) {
        vdc_domain_increment_saturating(
            &context->control.follower_wrong_source_count);
        return false;
    }
    if (command->valid != 1u || command->control_generation == 0u ||
        command->command_seq == 0u ||
        command->effective_vdc_time_ns == 0u ||
        command->schedule_crc32 != context->schedule.schedule_crc32 ||
        command->lock_state > VDC_DOMAIN_LOCK_FAULT ||
        command->quality > VDC_DOMAIN_HEALTH_FAULT ||
        vdc_domain_abs_i32(command->period_adjust_ppb) >
            context->servo.sanity_freq_limit_ppb) {
        vdc_domain_increment_saturating(
            &context->control.follower_invalid_command_count);
        return false;
    }
    /* control_generation belongs to the publishing master's command stream;
     * it is deliberately independent from this node's local role-profile
     * generation. Sequence and effective VDC time provide stale detection. */
    if (context->control.last_follower_command_seq != 0u &&
        (int32_t)(command->command_seq -
                  context->control.last_follower_command_seq) <= 0) {
        vdc_domain_increment_saturating(
            &context->control.follower_stale_command_count);
        return false;
    }
    if (context->control.last_follower_effective_vdc_time_ns != 0u &&
        command->effective_vdc_time_ns <=
            context->control.last_follower_effective_vdc_time_ns) {
        vdc_domain_increment_saturating(
            &context->control.follower_stale_command_count);
        return false;
    }

    /* A peer's local-tick anchor is not meaningful on this board. Preserve
     * the local map and commit only the command fields that drive the signal
     * DCO at the already-selected absolute service boundary. */
    context->dco.valid = 1u;
    context->dco.dco_update_seq =
        vdc_domain_increment_nonzero(context->dco.dco_update_seq);
    context->dco.period_adjust_ppb = command->period_adjust_ppb;
    context->dco.phase_offset_ns = command->phase_offset_ns;
    context->dco.lock_state = command->lock_state;
    context->dco.slew_limit_ppb = context->servo.sanity_freq_limit_ppb;
    context->dco.tdma_schedule_crc32 = context->schedule.schedule_crc32;
    context->dco.servo_profile_crc32 = context->servo.servo_profile_crc32;
    vdc_domain_reset_lock_acquisition(context);
    context->dpll.last_frequency_error_ppb = 0;
    context->dpll.update_seq++;
    vdc_domain_increment_saturating(&context->control.follower_apply_count);
    context->control.last_follower_source_slot_id = command->source_slot_id;
    context->control.last_follower_control_generation =
        command->control_generation;
    context->control.last_follower_command_seq = command->command_seq;
    context->control.last_follower_quality = command->quality;
    context->control.last_follower_effective_vdc_time_ns =
        command->effective_vdc_time_ns;
    vdc_domain_request_oscillator_discipline(
        context,
        command->period_adjust_ppb,
        command->source_slot_id,
        command->command_seq,
        command->effective_vdc_time_ns);
    return true;
}

/* Keep this bounded, infrequent control operation in XIP, including when its
 * caller lives in RAM. The Core1 outer owner guard covers the final commit. */
static bool vdc_domain_local_follow_delta_identity_valid(
    const vdc_domain_context_t *context,
    const vdc_dpll_local_rate_delta_t *command,
    uint64_t local_now_ns)
{
    if (context == NULL || command == NULL || context->ready == 0u ||
        !vdc_domain_is_follower(context)) {
        return false;
    }

    const uint32_t node_count = context->schedule.ring_binding.node_count;
    if (context->schedule.enabled == 0u || context->servo.enabled == 0u ||
        node_count == 0u || node_count > VDC_DOMAIN_NODE_COUNT ||
        command->source_slot_id >= node_count ||
        command->target_slot_id >= node_count ||
        command->source_slot_id == command->target_slot_id ||
        command->source_slot_id != context->control.profile.follow_master_slot_id ||
        command->target_slot_id != context->schedule.local_slot_id ||
        command->expected_control_generation == 0u ||
        command->expected_control_generation != context->control.profile.generation ||
        command->schedule_crc32 != context->schedule.schedule_crc32 ||
        command->servo_profile_crc32 != context->servo.servo_profile_crc32 ||
        context->clock.valid == 0u ||
        command->clock_epoch_id != context->clock.epoch_id ||
        command->clock_run_id != context->clock.run_id ||
        command->clock_epoch_id != context->dco.epoch_id ||
        command->clock_run_id != context->dco.run_id ||
        context->dco.dco_update_seq == 0u ||
        context->dco.dco_update_seq == UINT32_MAX ||
        command->expected_dco_update_seq != context->dco.dco_update_seq ||
        local_now_ns < context->dco.base_local_tick64 ||
        context->dco.period_adjust_ppb <= -1000000000) {
        return false;
    }

    return true;
}

static bool __attribute__((noinline)) vdc_domain_commit_rate_delta(
    vdc_domain_context_t *context,
    const vdc_dpll_local_rate_delta_t *command,
    uint64_t local_now_ns)
{
    if (!vdc_domain_local_follow_delta_identity_valid(
            context, command, local_now_ns)) return false;

    const int64_t next_rate = (int64_t)context->dco.period_adjust_ppb +
                              (int64_t)command->delta_rate_ppb;
    if (next_rate < INT32_MIN || next_rate > INT32_MAX ||
        next_rate <= -1000000000 ||
        vdc_domain_abs_i32((int32_t)next_rate) >
            context->servo.sanity_freq_limit_ppb) {
        return false;
    }

    uint64_t old_output_ns = 0u;
    if (!vdc_domain_dco_local_to_output_ns(&context->dco, local_now_ns,
                                           &old_output_ns)) {
        return false;
    }
    vdc_dco_control_t candidate = context->dco;
    candidate.base_local_tick64 = local_now_ns;
    candidate.base_vdc_time64_ns = old_output_ns;
    candidate.phase_offset_ns = 0;
    candidate.period_adjust_ppb = (int32_t)next_rate;
    candidate.dco_update_seq++;

    uint64_t new_output_ns = 0u;
    if (!vdc_domain_dco_control_validate(&context->schedule, &context->servo,
                                        &candidate) ||
        !vdc_domain_dco_local_to_output_ns(&candidate, local_now_ns,
                                           &new_output_ns) ||
        new_output_ns != old_output_ns) {
        return false;
    }

    context->dco = candidate;
    return true;
}

/* Checked unsigned coordinate translation accepts INT64_MIN without ever
 * negating it in signed arithmetic. The caller owns the destination. */
static bool vdc_domain_translate_u64(uint64_t value, int64_t delta,
                                     uint64_t *translated)
{
    if (delta < 0) {
        const uint64_t magnitude = (uint64_t)(-(delta + 1)) + 1u;
        if (value < magnitude) return false;
        *translated = value - magnitude;
    } else {
        const uint64_t magnitude = (uint64_t)delta;
        if (UINT64_MAX - value < magnitude) return false;
        *translated = value + magnitude;
    }
    return true;
}

bool __attribute__((noinline)) vdc_domain_apply_reference_baseline(
    vdc_domain_context_t *context, int32_t absolute_ppb, uint64_t local_now_ns)
{
    if (context == NULL || !context->ready || !context->schedule.enabled ||
        !context->servo.enabled || context->control.profile.valid != 1u ||
        context->control.profile.mode != VDC_DPLL_CONTROL_MODE_MASTER ||
        context->schedule.local_slot_id != context->schedule.reference_slot_id ||
        !context->clock.valid || !context->dco.valid ||
        context->clock.period_adjust_ppb <= -1000000000 ||
        context->dco.period_adjust_ppb <= -1000000000 || absolute_ppb <= -1000000000 ||
        context->clock.epoch_id != context->dco.epoch_id ||
        context->clock.run_id != context->dco.run_id ||
        context->clock.tdma_schedule_crc32 != context->schedule.schedule_crc32 ||
        context->clock.servo_profile_crc32 != context->servo.servo_profile_crc32 ||
        local_now_ns < context->clock.base_local_tick64 ||
        local_now_ns < context->dco.base_local_tick64 ||
        vdc_domain_abs_i32(absolute_ppb) > context->servo.sanity_freq_limit_ppb ||
        !vdc_domain_dco_control_validate(&context->schedule, &context->servo,
                                         &context->dco)) return false;
    const int64_t delta = (int64_t)absolute_ppb - context->reference_baseline_ppb;
    const int64_t clock_rate = (int64_t)context->clock.period_adjust_ppb + delta;
    const int64_t dco_rate = (int64_t)context->dco.period_adjust_ppb + delta;
    if (clock_rate <= -1000000000 || clock_rate > INT32_MAX ||
        dco_rate <= -1000000000 || dco_rate > INT32_MAX ||
        vdc_domain_abs_i32((int32_t)clock_rate) > context->servo.sanity_freq_limit_ppb ||
        vdc_domain_abs_i32((int32_t)dco_rate) > context->servo.sanity_freq_limit_ppb)
        return false;
    if (!delta) return true;
    if (context->clock.model_seq == UINT32_MAX ||
        context->dco.dco_update_seq == UINT32_MAX) return false;

    vdc_clock_model_t clock = context->clock;
    vdc_dco_control_t dco = context->dco, clock_projection;
    vdc_domain_default_dco_control(&clock_projection, &clock, context->dpll.state);
    uint64_t old_clock, old_output;
    if (!vdc_domain_dco_local_to_output_ns(&clock_projection, local_now_ns, &old_clock) ||
        !vdc_domain_dco_local_to_output_ns(&dco, local_now_ns, &old_output) ||
        !vdc_domain_translate_u64(old_clock, -(int64_t)clock.phase_offset_ns,
                                  &clock.base_vdc_time64_ns) ||
        !vdc_domain_translate_u64(old_output, -(int64_t)dco.phase_offset_ns,
                                  &dco.base_vdc_time64_ns)) return false;
    clock.base_local_tick64 = dco.base_local_tick64 = local_now_ns;
    clock.period_adjust_ppb = (int32_t)clock_rate;
    dco.period_adjust_ppb = (int32_t)dco_rate;
    ++clock.model_seq;
    ++dco.dco_update_seq;
    dco.source_model_seq = clock.model_seq;
    uint64_t new_clock, new_output;
    vdc_domain_default_dco_control(&clock_projection, &clock, context->dpll.state);
    if (!vdc_domain_dco_local_to_output_ns(&clock_projection, local_now_ns, &new_clock) ||
        !vdc_domain_dco_local_to_output_ns(&dco, local_now_ns, &new_output) ||
        new_clock != old_clock || new_output != old_output) return false;
    context->clock = clock;
    context->dco = dco;
    context->reference_baseline_ppb = absolute_ppb;
    return true;
}

bool __attribute__((noinline)) vdc_domain_apply_local_follow_phase_delta(
    vdc_domain_context_t *context,
    const vdc_dpll_local_phase_delta_t *command,
    uint64_t local_now_ns)
{
    if (command == NULL || command->delta_phase_ns == 0) return false;
    const vdc_dpll_local_rate_delta_t identity = {
        .source_slot_id = command->source_slot_id,
        .target_slot_id = command->target_slot_id,
        .expected_control_generation = command->expected_control_generation,
        .schedule_crc32 = command->schedule_crc32,
        .servo_profile_crc32 = command->servo_profile_crc32,
        .clock_epoch_id = command->clock_epoch_id,
        .clock_run_id = command->clock_run_id,
        .expected_dco_update_seq = command->expected_dco_update_seq,
        .delta_rate_ppb = 0,
    };
    if (!vdc_domain_local_follow_delta_identity_valid(context, &identity, local_now_ns) ||
        vdc_domain_abs_i32(context->dco.period_adjust_ppb) >
            context->servo.sanity_freq_limit_ppb ||
        !vdc_domain_dco_control_validate(&context->schedule, &context->servo,
                                          &context->dco)) return false;

    uint64_t old_output_ns, expected_output_ns, new_output_ns;
    vdc_dco_control_t candidate = context->dco;
    if (!vdc_domain_translate_u64(candidate.base_vdc_time64_ns,
                                  command->delta_phase_ns,
                                  &candidate.base_vdc_time64_ns) ||
        !vdc_domain_dco_local_to_output_ns(&context->dco, local_now_ns, &old_output_ns) ||
        !vdc_domain_translate_u64(old_output_ns, command->delta_phase_ns,
                                  &expected_output_ns) ||
        !vdc_domain_dco_local_to_output_ns(&candidate, local_now_ns, &new_output_ns) ||
        new_output_ns != expected_output_ns) return false;

    candidate.dco_update_seq++;
    context->dco = candidate;
    return true;
}


bool __attribute__((noinline)) vdc_domain_apply_local_follow_rate_delta(
    vdc_domain_context_t *context,
    const vdc_dpll_local_rate_delta_t *candidate,
    uint64_t local_now_ns)
{
    return vdc_domain_commit_rate_delta(context, candidate, local_now_ns);
}

bool __attribute__((noinline)) vdc_domain_apply_follower_rate_delta(
    vdc_domain_context_t *context,
    const vdc_dpll_follower_rate_delta_t *command,
    uint64_t local_now_ns)
{
    if (context == NULL || command == NULL ||
        command->expected_applied_command_seq != context->control.last_follower_command_seq ||
        command->command_seq == 0u ||
        command->command_seq <= context->control.last_follower_command_seq) return false;
    const vdc_dpll_local_rate_delta_t candidate = {
        .source_slot_id = command->source_slot_id, .target_slot_id = command->target_slot_id,
        .expected_control_generation = command->expected_control_generation,
        .schedule_crc32 = command->schedule_crc32,
        .servo_profile_crc32 = command->servo_profile_crc32,
        .clock_epoch_id = command->clock_epoch_id, .clock_run_id = command->clock_run_id,
        .expected_dco_update_seq = command->expected_dco_update_seq,
        .delta_rate_ppb = command->delta_rate_ppb};
    if (!vdc_domain_commit_rate_delta(context, &candidate, local_now_ns)) return false;
    context->control.last_follower_source_slot_id = command->source_slot_id;
    context->control.last_follower_command_seq = command->command_seq;
    context->control.last_follower_control_generation = 0u;
    context->control.last_follower_quality = 0u;
    context->control.last_follower_effective_vdc_time_ns = 0u;
    vdc_domain_increment_saturating(&context->control.follower_apply_count);
    return true;
}

void vdc_domain_note_follower_command_missing(vdc_domain_context_t *context)
{
    if (vdc_domain_is_follower(context)) {
        vdc_domain_increment_saturating(
            &context->control.follower_no_command_count);
    }
}

void vdc_domain_note_follower_command_late(vdc_domain_context_t *context)
{
    if (vdc_domain_is_follower(context)) {
        vdc_domain_increment_saturating(
            &context->control.follower_late_command_count);
    }
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
                 * used by the reference node. */
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

/* The T2 time cursor projects through this on every SYNC_TRIGGER slice. */
bool VDC_DOMAIN_TIME_CRITICAL(vdc_domain_dco_local_to_output_ns)(const vdc_dco_control_t *dco,
                                       uint64_t local_ns,
                                       uint64_t *output_ns)
{
    if (dco == NULL || output_ns == NULL || dco->valid == 0u ||
        dco->nominal_period_ns == 0u || dco->lock_state > VDC_DOMAIN_LOCK_FAULT ||
        local_ns < dco->base_local_tick64) return false;
    const uint64_t delta = local_ns - dco->base_local_tick64;
    const uint32_t magnitude = vdc_domain_abs_i32(dco->period_adjust_ppb);
    const uint64_t whole = delta / 1000000000ull;
    const uint64_t fraction = (delta % 1000000000ull) * magnitude / 1000000000ull;

    /* Exact unsigned magnitude of trunc(delta * rate / 1e9), without a
     * 96-bit multiplication or signed overflow. whole fits in 35 bits;
     * each split product fits uint64_t, including an INT32_MIN rate. */
    const uint64_t product_low = (uint64_t)(uint32_t)whole * magnitude;
    const uint64_t product_high = (whole >> 32u) * magnitude;
    uint64_t rate_low = product_low + (product_high << 32u);
    uint32_t rate_high = (uint32_t)(product_high >> 32u) +
        (rate_low < product_low ? 1u : 0u);
    const uint64_t old_rate_low = rate_low;
    rate_low += fraction;
    rate_high += rate_low < old_rate_low ? 1u : 0u;

    /* A signed high word retains carries until ALL signed terms have been
     * added. This accepts cancellation such as base+delta overflowing before
     * a negative rate brings the exact final result back into uint64_t. The
     * high word remains within a few units for the full int32_t rate range. */
    uint64_t value = dco->base_vdc_time64_ns + delta;
    int32_t high = value < delta ? 1 : 0;
    const uint64_t old_value = value;
    if (dco->period_adjust_ppb < 0) {
        value -= rate_low;
        high -= (int32_t)rate_high + (old_value < rate_low ? 1 : 0);
    } else {
        value += rate_low;
        high += (int32_t)rate_high + (value < old_value ? 1 : 0);
    }
    const uint32_t phase = vdc_domain_abs_i32(dco->phase_offset_ns);
    const uint64_t before_phase = value;
    if (dco->phase_offset_ns < 0) {
        value -= phase;
        high -= before_phase < phase ? 1 : 0;
    } else {
        value += phase;
        high += value < before_phase ? 1 : 0;
    }
    if (high != 0) return false;
    *output_ns = value;
    return true;
}


/* Compute floor/ceil((n + extra) * 1e9 / divisor) exactly, even if
 * n+extra overflows uint64. extra is at most 2^31; divisor <= 3147483647.
 * The quotient split avoids a 96-bit product. Any rejected overflow is
 * positive and cannot be cancelled by a subsequent term. */
static bool vdc_domain_inverse_scale(uint64_t n, uint32_t extra,
                                     uint32_t divisor, bool round_up,
                                     uint64_t *scaled)
{
    const uint64_t billion = UINT64_C(1000000000);
    uint64_t whole = n / divisor;
    uint64_t remainder = n % divisor;
    if (extra != 0u) {
        remainder += extra;
        const uint64_t carry = remainder / divisor;
        remainder %= divisor;
        if (UINT64_MAX - whole < carry) return false;
        whole += carry;
    }
    if (whole > UINT64_MAX / billion) return false;
    whole *= billion;
    const uint64_t product = remainder * billion;
    uint64_t fraction = product / divisor;
    if (round_up && product % divisor != 0u) ++fraction;
    if (UINT64_MAX - whole < fraction) return false;
    *scaled = whole + fraction;
    return true;
}

bool vdc_domain_dco_output_to_local_ns(const vdc_dco_control_t *dco,
                                      uint64_t target_output_ns,
                                      uint64_t *local_ns)
{
    if (dco == NULL || local_ns == NULL || dco->valid == 0u ||
        dco->nominal_period_ns == 0u || dco->lock_state > VDC_DOMAIN_LOCK_FAULT ||
        dco->period_adjust_ppb <= -1000000000) return false;
    const uint64_t base = dco->base_vdc_time64_ns;
    uint64_t need = 0u;
    uint32_t extra = 0u;
    if (dco->phase_offset_ns >= 0) {
        const uint32_t phase = (uint32_t)dco->phase_offset_ns;
        if (UINT64_MAX - base < phase) return false;
        const uint64_t intercept = base + phase;
        if (target_output_ns > intercept) need = target_output_ns - intercept;
    } else {
        const uint32_t phase = (uint32_t)(-(int64_t)dco->phase_offset_ns);
        if (base >= phase) {
            const uint64_t intercept = base - phase;
            if (target_output_ns > intercept) need = target_output_ns - intercept;
        } else {
            /* A negative intercept is legal: later outputs may be valid.
             * Preserve need+extra even when it needs 65 unsigned bits. */
            need = target_output_ns;
            extra = phase - (uint32_t)base;
        }
    }
    uint64_t delta = 0u;
    if (need != 0u || extra != 0u) {
        const uint32_t divisor = (uint32_t)(INT64_C(1000000000) +
                                            dco->period_adjust_ppb);
        if (dco->period_adjust_ppb >= 0) {
            /* F-C = floor(delta * divisor / 1e9).
             * floor(x) >= H iff x >= H for positive integral H. */
            if (!vdc_domain_inverse_scale(need, extra, divisor, true, &delta))
                return false;
        } else {
            /* F-C = ceil(delta * divisor / 1e9).
             * ceil(x) >= H iff x > H-1, hence floor((H-1)*1e9/D)+1.
             * Near -1e9 ppb, using the positive-rate formula would be wrong
             * by nearly 1e9 local ns; no fixed small correction can repair it. */
            if (need != 0u) --need;
            else --extra;
            if (!vdc_domain_inverse_scale(need, extra, divisor, false, &delta) ||
                delta == UINT64_MAX) return false;
            ++delta;
        }
    }
    if (UINT64_MAX - dco->base_local_tick64 < delta) return false;
    const uint64_t candidate = dco->base_local_tick64 + delta;
    uint64_t output;
    if (!vdc_domain_dco_local_to_output_ns(dco, candidate, &output) ||
        output < target_output_ns) return false;
    if (delta != 0u) {
        uint64_t previous;
        if (vdc_domain_dco_local_to_output_ns(dco, candidate - 1u, &previous) &&
            previous >= target_output_ns) return false;
        /* With a valid immutable model and rate > -1e9, F is nondecreasing.
         * Candidate is representable; a nonrepresentable predecessor can
         * therefore only be a negative output, which is below every target. */
    }
    *local_ns = candidate;
    return true;
}
