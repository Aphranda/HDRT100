#include "trigger_reservation_planner.h"

#include <stddef.h>

static bool identity_equal(const trigger_reservation_identity_t *left,
                           const trigger_reservation_identity_t *right)
{
    return left->run_id == right->run_id &&
        left->deployment_generation == right->deployment_generation &&
        left->plan_generation == right->plan_generation;
}

trigger_reservation_plan_result_t trigger_reservation_planner_init(
    const trigger_reservation_config_t *config, uint32_t run_id,
    uint64_t anchor_count, uint64_t anchor_vdc_ns,
    const trigger_reservation_clock_snapshot_t *clock,
    trigger_reservation_planner_t *planner)
{
    if (clock == NULL || planner == NULL || run_id == 0u ||
        trigger_reservation_config_validate(config, PROJECT_NODE_CAPACITY) !=
            TRIGGER_RESERVATION_CONFIG_OK)
        return TRIGGER_RESERVATION_PLAN_ARGUMENT;
    const trigger_reservation_identity_t identity = {
        .run_id = run_id,
        .deployment_generation = config->deployment_generation,
        .plan_generation = config->plan_generation,
    };
    if (!identity_equal(&identity, &clock->identity))
        return TRIGGER_RESERVATION_PLAN_IDENTITY;
    const uint64_t pulses =
        (uint64_t)config->position_threshold * config->observation_positions;
    if (pulses > UINT64_MAX - anchor_count ||
        pulses > (UINT64_MAX - anchor_vdc_ns) / config->pulse_period_ns)
        return TRIGGER_RESERVATION_PLAN_OVERFLOW;
    *planner = (trigger_reservation_planner_t){
        .identity = identity,
        .clock = *clock,
        .anchor_count = anchor_count,
        .anchor_vdc_ns = anchor_vdc_ns,
        .position_threshold = config->position_threshold,
        .pulse_period_ns = config->pulse_period_ns,
        .prediction_tolerance_ns = config->prediction_tolerance_ns,
        .observation_positions = config->observation_positions,
        .leading_vdc_positions = config->leading_vdc_positions,
        .trailing_vdc_positions = config->trailing_vdc_positions,
    };
    return TRIGGER_RESERVATION_PLAN_OK;
}

trigger_reservation_plan_result_t trigger_reservation_plan_position(
    const trigger_reservation_planner_t *planner, uint32_t observation_index,
    trigger_reservation_position_plan_t *position)
{
    if (planner == NULL || position == NULL ||
        planner->position_threshold == 0u || planner->pulse_period_ns == 0u ||
        planner->prediction_tolerance_ns == 0u ||
        planner->leading_vdc_positions > planner->observation_positions ||
        planner->trailing_vdc_positions > planner->observation_positions -
            planner->leading_vdc_positions)
        return TRIGGER_RESERVATION_PLAN_ARGUMENT;
    if (observation_index >= planner->observation_positions)
        return TRIGGER_RESERVATION_PLAN_RANGE;
    const uint64_t position_number = (uint64_t)observation_index + 1u;
    const uint64_t pulses = position_number * planner->position_threshold;
    if (pulses > UINT64_MAX - planner->anchor_count ||
        pulses > (UINT64_MAX - planner->anchor_vdc_ns) /
            planner->pulse_period_ns)
        return TRIGGER_RESERVATION_PLAN_OVERFLOW;
    trigger_reservation_position_phase_t phase;
    uint32_t business_index = UINT32_MAX;
    if (observation_index < planner->leading_vdc_positions) {
        phase = TRIGGER_RESERVATION_POSITION_LEADING_VDC;
    } else if (observation_index < planner->observation_positions -
                   planner->trailing_vdc_positions) {
        phase = TRIGGER_RESERVATION_POSITION_BUSINESS;
        business_index = observation_index - planner->leading_vdc_positions;
    } else {
        phase = TRIGGER_RESERVATION_POSITION_TRAILING_VDC;
    }
    *position = (trigger_reservation_position_plan_t){
        .identity = planner->identity,
        .observation_index = observation_index,
        .business_index = business_index,
        .phase = phase,
        .target_count = planner->anchor_count + pulses,
        .predicted_vdc_ns = planner->anchor_vdc_ns +
            pulses * planner->pulse_period_ns,
        .clock_model_seq = planner->clock.clock_model_seq,
        .dco_update_seq = planner->clock.dco_update_seq,
        .calibration_generation = planner->clock.calibration_generation,
        .calibration_crc32 = planner->clock.calibration_crc32,
        .schedule_crc32 = planner->clock.schedule_crc32,
        .clock_epoch_id = planner->clock.clock_epoch_id,
        .clock_run_id = planner->clock.clock_run_id,
    };
    return TRIGGER_RESERVATION_PLAN_OK;
}

trigger_reservation_plan_result_t trigger_reservation_plan_lookahead(
    const trigger_reservation_planner_t *planner, uint32_t first_index,
    uint32_t count, trigger_reservation_position_plan_t *positions,
    uint32_t capacity)
{
    if (planner == NULL || positions == NULL || count == 0u ||
        count > TRIGGER_RESERVATION_MAX_LOOKAHEAD || count > capacity)
        return TRIGGER_RESERVATION_PLAN_ARGUMENT;
    if (first_index >= planner->observation_positions ||
        count > planner->observation_positions - first_index)
        return TRIGGER_RESERVATION_PLAN_RANGE;
    for (uint32_t i = 0u; i < count; ++i) {
        const trigger_reservation_plan_result_t result =
            trigger_reservation_plan_position(planner, first_index + i,
                                              &positions[i]);
        if (result != TRIGGER_RESERVATION_PLAN_OK) return result;
    }
    return TRIGGER_RESERVATION_PLAN_OK;
}

bool trigger_reservation_position_identity_matches(
    const trigger_reservation_position_plan_t *position,
    const trigger_reservation_clock_snapshot_t *clock)
{
    return position != NULL && clock != NULL &&
        identity_equal(&position->identity, &clock->identity) &&
        position->clock_model_seq == clock->clock_model_seq &&
        position->dco_update_seq == clock->dco_update_seq &&
        position->calibration_generation == clock->calibration_generation &&
        position->calibration_crc32 == clock->calibration_crc32 &&
        position->schedule_crc32 == clock->schedule_crc32 &&
        position->clock_epoch_id == clock->clock_epoch_id &&
        position->clock_run_id == clock->clock_run_id;
}

trigger_reservation_plan_result_t trigger_reservation_accept_position(
    const trigger_reservation_planner_t *planner,
    const trigger_reservation_position_plan_t *position,
    uint64_t actual_count, uint64_t actual_vdc_ns)
{
    if (planner == NULL || position == NULL)
        return TRIGGER_RESERVATION_PLAN_ARGUMENT;
    trigger_reservation_position_plan_t canonical;
    const trigger_reservation_plan_result_t result =
        trigger_reservation_plan_position(planner, position->observation_index,
                                         &canonical);
    if (result != TRIGGER_RESERVATION_PLAN_OK) return result;
    if (position->target_count != canonical.target_count ||
        position->predicted_vdc_ns != canonical.predicted_vdc_ns ||
        position->business_index != canonical.business_index ||
        position->phase != canonical.phase)
        return TRIGGER_RESERVATION_PLAN_ARGUMENT;
    if (!identity_equal(&planner->identity, &position->identity) ||
        !trigger_reservation_position_identity_matches(position,
                                                       &planner->clock))
        return TRIGGER_RESERVATION_PLAN_IDENTITY;
    if (actual_count != position->target_count)
        return TRIGGER_RESERVATION_PLAN_COUNT_MISMATCH;
    const uint64_t error = actual_vdc_ns >= position->predicted_vdc_ns
        ? actual_vdc_ns - position->predicted_vdc_ns
        : position->predicted_vdc_ns - actual_vdc_ns;
    if (error > planner->prediction_tolerance_ns)
        return TRIGGER_RESERVATION_PLAN_PREDICTION;
    return TRIGGER_RESERVATION_PLAN_OK;
}

const char *trigger_reservation_plan_result_name(
    trigger_reservation_plan_result_t result)
{
    static const char *const names[] = {"OK", "ARGUMENT", "IDENTITY", "RANGE",
        "OVERFLOW", "COUNT_MISMATCH", "PREDICTION"};
    return result < sizeof(names) / sizeof(names[0]) ? names[result] : "UNKNOWN";
}
