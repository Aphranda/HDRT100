#include "trigger_reservation_clock.h"
#include "vdc_timer1_coordinate.h"

#include <stddef.h>

static bool identity_valid(const trigger_reservation_identity_t *identity)
{
    return identity != NULL && identity->run_id != 0u &&
        identity->deployment_generation != 0u && identity->plan_generation != 0u;
}

static bool formal_timestamp(const vdc_quality_table_t *quality)
{
    return quality->last_timestamp_source == VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK &&
        quality->last_sample_time_ns != 0u &&
        quality->last_timestamp_resolution_ns != 0u &&
        quality->last_timestamp_resolution_ns <=
            VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS &&
        (quality->last_timestamp_flags & VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0u &&
        (quality->last_timestamp_flags & VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u;
}

trigger_reservation_clock_result_t trigger_reservation_clock_snapshot_from_vector(
    const vdc_dpll_manager_vector_snapshot_t *vector,
    const trigger_reservation_identity_t *identity,
    uint32_t maximum_age_us,
    trigger_reservation_clock_snapshot_t *snapshot)
{
    if (vector == NULL || snapshot == NULL || maximum_age_us == 0u)
        return TRIGGER_RESERVATION_CLOCK_ARGUMENT;
    if (!identity_valid(identity)) return TRIGGER_RESERVATION_CLOCK_IDENTITY;
    if (vector->publication_revision == 0u ||
        (vector->publication_revision & 1u) != 0u || vector->ready == 0u)
        return TRIGGER_RESERVATION_CLOCK_NOT_READY;
    const vdc_clock_model_t *clock = &vector->clock;
    const vdc_dco_control_t *dco = &vector->dco;
    if (vector->schedule.enabled == 0u || vector->schedule.schedule_crc32 == 0u ||
        clock->valid == 0u || clock->model_seq == 0u || clock->epoch_id == 0u ||
        clock->run_id == 0u || dco->valid == 0u || dco->dco_update_seq == 0u ||
        dco->source_model_seq != clock->model_seq ||
        dco->epoch_id != clock->epoch_id || dco->run_id != clock->run_id ||
        dco->tdma_schedule_crc32 != vector->schedule.schedule_crc32 ||
        clock->tdma_schedule_crc32 != vector->schedule.schedule_crc32 ||
        dco->servo_profile_crc32 == 0u ||
        dco->servo_profile_crc32 != vector->servo.servo_profile_crc32 ||
        vector->dpll.schedule_crc32 != vector->schedule.schedule_crc32 ||
        vector->dpll.servo_profile_crc32 != vector->servo.servo_profile_crc32)
        return TRIGGER_RESERVATION_CLOCK_MODEL;
    const vdc_quality_table_t *quality = &vector->quality;
    if (vector->dpll.state != VDC_DOMAIN_LOCK_LOCKED ||
        vector->dpll.debug_continue_enabled != 0u ||
        dco->lock_state != VDC_DOMAIN_LOCK_LOCKED || quality->valid == 0u ||
        quality->health_state != VDC_DOMAIN_HEALTH_HEALTHY ||
        quality->lock_state != VDC_DOMAIN_LOCK_LOCKED ||
        quality->lock_quality_tier < VDC_DOMAIN_LOCK_QUALITY_FINE_100NS ||
        quality->freshness_limit_us == 0u ||
        quality->last_sample_age_us > quality->freshness_limit_us ||
        quality->last_sample_age_us > maximum_age_us || !formal_timestamp(quality) ||
        vector->gate.passed == 0u || vector->gate.reject_code != VDC_DOMAIN_GATE_PASS)
        return TRIGGER_RESERVATION_CLOCK_QUALITY;
    const uint32_t required_path_flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
        VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED | VDC_PATH_DELAY_FLAG_BIAS_VALID |
        VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH |
        VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID;
    if (vector->path_delay.valid == 0u || vector->path_delay.table_crc32 == 0u ||
        vector->path_delay.schedule_crc32 != vector->schedule.schedule_crc32 ||
        vector->path_delay.calibration_generation == 0u ||
        (vector->path_delay.flags & required_path_flags) != required_path_flags ||
        (vector->path_delay.flags & VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) != 0u)
        return TRIGGER_RESERVATION_CLOCK_CALIBRATION;
    *snapshot = (trigger_reservation_clock_snapshot_t){
        .identity = *identity,
        .publication_revision = vector->publication_revision,
        .schedule_crc32 = vector->schedule.schedule_crc32,
        .clock_model_seq = clock->model_seq,
        .clock_epoch_id = clock->epoch_id,
        .clock_run_id = clock->run_id,
        .dco_update_seq = dco->dco_update_seq,
        .calibration_generation = vector->path_delay.calibration_generation,
        .calibration_crc32 = vector->path_delay.table_crc32,
        .quality_update_seq = quality->update_seq,
        .quality_age_us = quality->last_sample_age_us,
        .maximum_age_us = maximum_age_us < quality->freshness_limit_us
            ? maximum_age_us : quality->freshness_limit_us,
        .last_accepted_local_ns = quality->last_sample_time_ns,
        .valid_from_local_ns = dco->base_local_tick64,
        .dco = *dco,
    };
    return TRIGGER_RESERVATION_CLOCK_OK;
}

trigger_reservation_clock_result_t trigger_reservation_clock_capture(
    const trigger_reservation_identity_t *identity,
    uint32_t maximum_age_us,
    trigger_reservation_clock_snapshot_t *snapshot)
{
    vdc_dpll_manager_vector_snapshot_t vector;
    if (!vdc_dpll_manager_get_vector_snapshot(&vector))
        return TRIGGER_RESERVATION_CLOCK_NOT_READY;
    return trigger_reservation_clock_snapshot_from_vector(
        &vector, identity, maximum_age_us, snapshot);
}

trigger_reservation_clock_result_t trigger_reservation_clock_prepare(
    const trigger_reservation_clock_snapshot_t *snapshot,
    uint64_t target_vdc_ns, uint32_t timer1_hz,
    uint64_t now_ticks, uint64_t minimum_lead_ticks,
    trigger_reservation_clock_prepared_t *prepared)
{
    if (snapshot == NULL || prepared == NULL || timer1_hz == 0u ||
        minimum_lead_ticks == 0u)
        return TRIGGER_RESERVATION_CLOCK_ARGUMENT;
    uint64_t local_ns, deadline, now_ns;
    if (!vdc_timer1_ticks_to_ns(timer1_hz, now_ticks, true, &now_ns))
        return TRIGGER_RESERVATION_CLOCK_MAPPING;
    if (!identity_valid(&snapshot->identity))
        return TRIGGER_RESERVATION_CLOCK_IDENTITY;
    /* A saved snapshot cannot extend the lifetime of its accepted evidence. */
    if (snapshot->last_accepted_local_ns == 0u ||
        snapshot->maximum_age_us == 0u ||
        now_ns < snapshot->last_accepted_local_ns ||
        now_ns - snapshot->last_accepted_local_ns >
            (uint64_t)snapshot->maximum_age_us * 1000u)
        return TRIGGER_RESERVATION_CLOCK_QUALITY;
    if (!vdc_domain_dco_output_to_local_ns(
            &snapshot->dco, target_vdc_ns, &local_ns) ||
        !vdc_timer1_ns_to_ticks(timer1_hz, local_ns, true, &deadline))
        return TRIGGER_RESERVATION_CLOCK_MAPPING;
    if (UINT64_MAX - now_ticks < minimum_lead_ticks ||
        deadline < now_ticks + minimum_lead_ticks)
        return TRIGGER_RESERVATION_CLOCK_LATE;
    *prepared = (trigger_reservation_clock_prepared_t){
        .clock = *snapshot,
        .target_vdc_ns = target_vdc_ns,
        .model_local_ns = local_ns,
        .timer1_deadline_ticks = deadline,
        .prepared_at_ticks = now_ticks,
        .minimum_lead_ticks = minimum_lead_ticks,
    };
    return TRIGGER_RESERVATION_CLOCK_OK;
}

bool trigger_reservation_clock_identity_matches(
    const trigger_reservation_clock_prepared_t *prepared,
    const trigger_reservation_clock_snapshot_t *current)
{
    if (prepared == NULL || current == NULL) return false;
    const trigger_reservation_clock_snapshot_t *bound = &prepared->clock;
    return bound->identity.run_id == current->identity.run_id &&
        bound->identity.deployment_generation ==
            current->identity.deployment_generation &&
        bound->identity.plan_generation == current->identity.plan_generation &&
        bound->schedule_crc32 == current->schedule_crc32 &&
        bound->clock_model_seq == current->clock_model_seq &&
        bound->clock_epoch_id == current->clock_epoch_id &&
        bound->clock_run_id == current->clock_run_id &&
        bound->dco_update_seq == current->dco_update_seq &&
        bound->calibration_generation == current->calibration_generation &&
        bound->calibration_crc32 == current->calibration_crc32;
}

const char *trigger_reservation_clock_result_name(
    trigger_reservation_clock_result_t result)
{
    static const char *const names[] = {"OK", "ARGUMENT", "IDENTITY", "NOT_READY",
        "MODEL", "QUALITY", "CALIBRATION", "MAPPING", "LATE", "CHANGED"};
    return result < sizeof(names) / sizeof(names[0]) ? names[result] : "UNKNOWN";
}
