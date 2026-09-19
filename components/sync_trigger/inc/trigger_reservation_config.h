#ifndef TRIGGER_RESERVATION_CONFIG_H
#define TRIGGER_RESERVATION_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define TRIGGER_RESERVATION_SLOT_COUNT 4u
#define TRIGGER_RESERVATION_ITEM_COUNT 8u
#define TRIGGER_RESERVATION_COUNTER_INPUT 1u
#define TRIGGER_RESERVATION_READY_INPUT 2u
#define TRIGGER_RESERVATION_SP8T_OUTPUT_MASK 0x03u
#define TRIGGER_RESERVATION_SP2T_OUTPUT_MASK 0x04u
#define TRIGGER_RESERVATION_VNA_OUTPUT_MASK 0x08u
#define TRIGGER_RESERVATION_OUTPUT_MASK 0x0fu

typedef enum {
    TRIGGER_RESERVATION_CONFIG_OK = 0u,
    TRIGGER_RESERVATION_CONFIG_INVALID,
    TRIGGER_RESERVATION_CONFIG_SLOT,
    TRIGGER_RESERVATION_CONFIG_LEVEL,
    TRIGGER_RESERVATION_CONFIG_TIMING,
    TRIGGER_RESERVATION_CONFIG_WINDOW,
    TRIGGER_RESERVATION_CONFIG_IDENTITY,
} trigger_reservation_config_result_t;

typedef enum {
    TRIGGER_RESERVATION_SLOT_COUNTER = 0u,
    TRIGGER_RESERVATION_SLOT_SP8T,
    TRIGGER_RESERVATION_SLOT_SP2T,
    TRIGGER_RESERVATION_SLOT_VNA,
} trigger_reservation_slot_kind_t;

typedef enum {
    TRIGGER_RESERVATION_POLARIZATION_H = 0u,
    TRIGGER_RESERVATION_POLARIZATION_V = 1u,
} trigger_reservation_polarization_t;

typedef struct {
    uint32_t slot[TRIGGER_RESERVATION_SLOT_COUNT];
    uint32_t position_threshold;
    uint32_t pulse_period_ns;
    uint32_t prediction_tolerance_ns;
    uint32_t h_level;
    uint32_t v_level;
    uint32_t sp8t_safe_state;
    uint32_t sp2t_safe_level;
    uint32_t sp8t_settle_us;
    uint32_t sp2t_settle_us;
    uint32_t vna_pulse_us;
    uint32_t ready_timeout_ms;
    uint32_t reservation_lead_us;
    uint32_t vdc_max_age_us;
    uint32_t observation_positions;
    uint32_t leading_vdc_positions;
    uint32_t trailing_vdc_positions;
    uint32_t plan_generation;
    uint32_t deployment_generation;
} trigger_reservation_config_t;

typedef struct {
    uint32_t index;
    uint32_t sp8t_state;
    trigger_reservation_polarization_t polarization;
    uint32_t polarization_level;
    uint32_t output_code;
} trigger_reservation_item_t;

trigger_reservation_config_result_t trigger_reservation_config_validate(
    const trigger_reservation_config_t *config, uint32_t node_capacity);
bool trigger_reservation_build_plan(const trigger_reservation_config_t *config,
    trigger_reservation_item_t items[TRIGGER_RESERVATION_ITEM_COUNT]);
uint32_t trigger_reservation_business_positions(
    const trigger_reservation_config_t *config);
uint32_t trigger_reservation_safe_output_code(
    const trigger_reservation_config_t *config);
const char *trigger_reservation_config_result_name(
    trigger_reservation_config_result_t result);

#endif
