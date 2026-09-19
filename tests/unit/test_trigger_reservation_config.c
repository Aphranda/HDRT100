#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "trigger_reservation_config.h"

static trigger_reservation_config_t valid_config(void)
{
    return (trigger_reservation_config_t){
        .slot = {0u, 1u, 2u, 3u},
        .position_threshold = 1000u,
        .pulse_period_ns = 1000000u,
        .prediction_tolerance_ns = 100000u,
        .h_level = 0u,
        .v_level = 1u,
        .sp8t_safe_state = 0u,
        .sp2t_safe_level = 0u,
        .sp8t_settle_us = 10u,
        .sp2t_settle_us = 20u,
        .vna_pulse_us = 5u,
        .ready_timeout_ms = 100u,
        .reservation_lead_us = 1000u,
        .vdc_max_age_us = 10000u,
        .observation_positions = 30u,
        .leading_vdc_positions = 5u,
        .trailing_vdc_positions = 5u,
        .plan_generation = 7u,
        .deployment_generation = 9u,
    };
}

static void test_fixed_plan(void)
{
    trigger_reservation_config_t config = valid_config();
    trigger_reservation_item_t items[TRIGGER_RESERVATION_ITEM_COUNT];
    assert(trigger_reservation_config_validate(&config, 4u) ==
           TRIGGER_RESERVATION_CONFIG_OK);
    assert(trigger_reservation_build_plan(&config, items));
    for (uint32_t i = 0u; i < TRIGGER_RESERVATION_ITEM_COUNT; ++i) {
        assert(items[i].index == i);
        assert(items[i].sp8t_state == i / 2u);
        assert(items[i].polarization == (i & 1u));
        assert(items[i].polarization_level == (i & 1u));
        assert(items[i].output_code == ((i / 2u) | ((i & 1u) << 2u)));
        assert((items[i].output_code & TRIGGER_RESERVATION_VNA_OUTPUT_MASK) == 0u);
    }
    assert(trigger_reservation_business_positions(&config) == 20u);
    assert(trigger_reservation_safe_output_code(&config) == 0u);

    config.h_level = 1u;
    config.v_level = 0u;
    config.sp8t_safe_state = 3u;
    config.sp2t_safe_level = 1u;
    assert(trigger_reservation_build_plan(&config, items));
    assert(items[0].output_code == 4u && items[1].output_code == 0u);
    assert(items[6].output_code == 7u && items[7].output_code == 3u);
    assert(trigger_reservation_safe_output_code(&config) == 7u);
}

static void test_rejections_are_specific(void)
{
    trigger_reservation_config_t c = valid_config();
#define REJECT(statement, reason) do { c = valid_config(); statement; \
    assert(trigger_reservation_config_validate(&c, 4u) == reason); } while (0)
    REJECT(c.position_threshold = 0u, TRIGGER_RESERVATION_CONFIG_INVALID);
    REJECT(c.pulse_period_ns = 0u, TRIGGER_RESERVATION_CONFIG_INVALID);
    REJECT(c.prediction_tolerance_ns = 0u, TRIGGER_RESERVATION_CONFIG_INVALID);
    REJECT(c.slot[3] = 4u, TRIGGER_RESERVATION_CONFIG_SLOT);
    REJECT(c.slot[3] = 2u, TRIGGER_RESERVATION_CONFIG_SLOT);
    REJECT(c.h_level = 1u; c.v_level = 1u, TRIGGER_RESERVATION_CONFIG_LEVEL);
    REJECT(c.sp8t_safe_state = 4u, TRIGGER_RESERVATION_CONFIG_LEVEL);
    REJECT(c.vna_pulse_us = 0u, TRIGGER_RESERVATION_CONFIG_TIMING);
    REJECT(c.vdc_max_age_us = 0u, TRIGGER_RESERVATION_CONFIG_TIMING);
    REJECT(c.leading_vdc_positions = 15u; c.trailing_vdc_positions = 15u,
           TRIGGER_RESERVATION_CONFIG_WINDOW);
    REJECT(c.leading_vdc_positions = 31u, TRIGGER_RESERVATION_CONFIG_WINDOW);
    REJECT(c.plan_generation = 0u, TRIGGER_RESERVATION_CONFIG_IDENTITY);
#undef REJECT
    assert(strcmp(trigger_reservation_config_result_name(
        TRIGGER_RESERVATION_CONFIG_WINDOW), "WINDOW") == 0);
}

int main(void)
{
    test_fixed_plan();
    test_rejections_are_specific();
    puts("reservation configuration passed");
    return 0;
}
