#ifndef TRIGGER_RESERVATION_PLANNER_H
#define TRIGGER_RESERVATION_PLANNER_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_reservation_clock.h"
#include "trigger_reservation_config.h"

#define TRIGGER_RESERVATION_MAX_LOOKAHEAD 4u

typedef enum {
    TRIGGER_RESERVATION_POSITION_LEADING_VDC = 0u,
    TRIGGER_RESERVATION_POSITION_BUSINESS,
    TRIGGER_RESERVATION_POSITION_TRAILING_VDC,
} trigger_reservation_position_phase_t;

typedef enum {
    TRIGGER_RESERVATION_PLAN_OK = 0u,
    TRIGGER_RESERVATION_PLAN_ARGUMENT,
    TRIGGER_RESERVATION_PLAN_IDENTITY,
    TRIGGER_RESERVATION_PLAN_RANGE,
    TRIGGER_RESERVATION_PLAN_OVERFLOW,
    TRIGGER_RESERVATION_PLAN_COUNT_MISMATCH,
    TRIGGER_RESERVATION_PLAN_PREDICTION,
} trigger_reservation_plan_result_t;

typedef struct {
    trigger_reservation_identity_t identity;
    trigger_reservation_clock_snapshot_t clock;
    uint64_t anchor_count;
    uint64_t anchor_vdc_ns;
    uint32_t position_threshold;
    uint32_t pulse_period_ns;
    uint32_t prediction_tolerance_ns;
    uint32_t observation_positions;
    uint32_t leading_vdc_positions;
    uint32_t trailing_vdc_positions;
} trigger_reservation_planner_t;

typedef struct {
    trigger_reservation_identity_t identity;
    uint32_t observation_index;
    uint32_t business_index;
    trigger_reservation_position_phase_t phase;
    uint64_t target_count;
    uint64_t predicted_vdc_ns;
    uint32_t clock_model_seq;
    uint32_t dco_update_seq;
    uint32_t calibration_generation;
    uint32_t calibration_crc32;
    uint32_t schedule_crc32;
    uint32_t clock_epoch_id;
    uint32_t clock_run_id;
} trigger_reservation_position_plan_t;

trigger_reservation_plan_result_t trigger_reservation_planner_init(
    const trigger_reservation_config_t *config, uint32_t run_id,
    uint64_t anchor_count, uint64_t anchor_vdc_ns,
    const trigger_reservation_clock_snapshot_t *clock,
    trigger_reservation_planner_t *planner);
trigger_reservation_plan_result_t trigger_reservation_plan_position(
    const trigger_reservation_planner_t *planner, uint32_t observation_index,
    trigger_reservation_position_plan_t *position);
trigger_reservation_plan_result_t trigger_reservation_plan_lookahead(
    const trigger_reservation_planner_t *planner, uint32_t first_index,
    uint32_t count, trigger_reservation_position_plan_t *positions,
    uint32_t capacity);
trigger_reservation_plan_result_t trigger_reservation_accept_position(
    const trigger_reservation_planner_t *planner,
    const trigger_reservation_position_plan_t *position,
    uint64_t actual_count, uint64_t actual_vdc_ns);
bool trigger_reservation_position_identity_matches(
    const trigger_reservation_position_plan_t *position,
    const trigger_reservation_clock_snapshot_t *clock);
const char *trigger_reservation_plan_result_name(
    trigger_reservation_plan_result_t result);

#endif
