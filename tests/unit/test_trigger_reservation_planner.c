#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "trigger_reservation_planner.h"

static trigger_reservation_config_t config(void)
{
    return (trigger_reservation_config_t){
        .slot = {0u, 1u, 2u, 3u}, .position_threshold = 1000u,
        .pulse_period_ns = 1000000u, .prediction_tolerance_ns = 100000u,
        .h_level = 0u, .v_level = 1u, .vna_pulse_us = 1u,
        .ready_timeout_ms = 100u, .reservation_lead_us = 100u,
        .vdc_max_age_us = 100u, .observation_positions = 30u,
        .leading_vdc_positions = 5u, .trailing_vdc_positions = 5u,
        .plan_generation = 7u, .deployment_generation = 8u,
    };
}

static trigger_reservation_clock_snapshot_t clock_snapshot(void)
{
    trigger_reservation_clock_snapshot_t clock;
    memset(&clock, 0, sizeof(clock));
    clock.identity = (trigger_reservation_identity_t){9u, 8u, 7u};
    clock.clock_model_seq = 10u;
    clock.dco_update_seq = 11u;
    clock.calibration_generation = 12u;
    return clock;
}

static void test_phases_prediction_and_fact_gate(void)
{
    const trigger_reservation_config_t c = config();
    const trigger_reservation_clock_snapshot_t clock = clock_snapshot();
    trigger_reservation_planner_t planner;
    assert(trigger_reservation_planner_init(&c, 9u, 500u, 2000000000ull,
        &clock, &planner) == TRIGGER_RESERVATION_PLAN_OK);
    trigger_reservation_position_plan_t positions[4];
    assert(trigger_reservation_plan_lookahead(&planner, 4u, 4u, positions, 4u) ==
           TRIGGER_RESERVATION_PLAN_OK);
    assert(positions[0].phase == TRIGGER_RESERVATION_POSITION_LEADING_VDC);
    assert(positions[0].business_index == UINT32_MAX);
    assert(positions[0].target_count == 5500u);
    assert(positions[0].predicted_vdc_ns == 7000000000ull);
    assert(positions[1].phase == TRIGGER_RESERVATION_POSITION_BUSINESS);
    assert(positions[1].business_index == 0u);
    assert(trigger_reservation_accept_position(&planner, &positions[1],
        6500u, 8000100000ull) == TRIGGER_RESERVATION_PLAN_OK);
    assert(trigger_reservation_accept_position(&planner, &positions[1],
        6501u, 8000000000ull) == TRIGGER_RESERVATION_PLAN_COUNT_MISMATCH);
    assert(trigger_reservation_accept_position(&planner, &positions[1],
        6500u, 8000100001ull) == TRIGGER_RESERVATION_PLAN_PREDICTION);
    trigger_reservation_position_plan_t last;
    assert(trigger_reservation_plan_position(&planner, 29u, &last) ==
           TRIGGER_RESERVATION_PLAN_OK);
    assert(last.phase == TRIGGER_RESERVATION_POSITION_TRAILING_VDC);
    assert(last.target_count == 30500u);
}

static void test_bounded_and_identity_rejections(void)
{
    trigger_reservation_config_t c = config();
    trigger_reservation_clock_snapshot_t clock = clock_snapshot();
    trigger_reservation_planner_t planner;
    assert(trigger_reservation_planner_init(&c, 9u, 0u, 0u, &clock, &planner) ==
           TRIGGER_RESERVATION_PLAN_OK);
    trigger_reservation_position_plan_t positions[5];
    assert(trigger_reservation_plan_lookahead(&planner, 0u, 5u, positions, 5u) ==
           TRIGGER_RESERVATION_PLAN_ARGUMENT);
    assert(trigger_reservation_plan_lookahead(&planner, 29u, 2u, positions, 5u) ==
           TRIGGER_RESERVATION_PLAN_RANGE);
    trigger_reservation_position_plan_t position;
    assert(trigger_reservation_plan_position(&planner, 5u, &position) ==
           TRIGGER_RESERVATION_PLAN_OK);
    clock.dco_update_seq++;
    assert(!trigger_reservation_position_identity_matches(&position, &clock));
    clock = clock_snapshot();
    clock.clock_epoch_id++;
    assert(!trigger_reservation_position_identity_matches(&position, &clock));
    clock = clock_snapshot();
    clock.calibration_crc32++;
    assert(!trigger_reservation_position_identity_matches(&position, &clock));
    position.target_count++;
    assert(trigger_reservation_accept_position(&planner, &position,
        position.target_count, position.predicted_vdc_ns) ==
           TRIGGER_RESERVATION_PLAN_ARGUMENT);
    trigger_reservation_planner_t invalid = planner;
    invalid.pulse_period_ns = 0u;
    assert(trigger_reservation_plan_position(&invalid, 5u, &position) ==
           TRIGGER_RESERVATION_PLAN_ARGUMENT);
    c.observation_positions = UINT32_MAX;
    c.position_threshold = UINT32_MAX;
    c.pulse_period_ns = UINT32_MAX;
    clock = clock_snapshot();
    assert(trigger_reservation_planner_init(&c, 9u, UINT64_MAX, UINT64_MAX,
        &clock, &planner) == TRIGGER_RESERVATION_PLAN_OVERFLOW);
}

int main(void)
{
    test_phases_prediction_and_fact_gate();
    test_bounded_and_identity_rejections();
    puts("reservation planner passed");
    return 0;
}
