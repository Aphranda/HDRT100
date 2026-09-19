#include "trigger_reservation_config.h"

#include <stddef.h>

trigger_reservation_config_result_t trigger_reservation_config_validate(
    const trigger_reservation_config_t *config, uint32_t node_capacity)
{
    if (config == NULL || node_capacity < TRIGGER_RESERVATION_SLOT_COUNT ||
        config->position_threshold == 0u || config->pulse_period_ns == 0u ||
        config->prediction_tolerance_ns == 0u)
        return TRIGGER_RESERVATION_CONFIG_INVALID;
    for (uint32_t i = 0u; i < TRIGGER_RESERVATION_SLOT_COUNT; ++i) {
        if (config->slot[i] >= node_capacity)
            return TRIGGER_RESERVATION_CONFIG_SLOT;
        for (uint32_t j = i + 1u; j < TRIGGER_RESERVATION_SLOT_COUNT; ++j)
            if (config->slot[i] == config->slot[j])
                return TRIGGER_RESERVATION_CONFIG_SLOT;
    }
    if (config->h_level > 1u || config->v_level > 1u ||
        config->h_level == config->v_level ||
        config->sp8t_safe_state > 3u || config->sp2t_safe_level > 1u)
        return TRIGGER_RESERVATION_CONFIG_LEVEL;
    if (config->vna_pulse_us == 0u || config->ready_timeout_ms == 0u ||
        config->reservation_lead_us == 0u || config->vdc_max_age_us == 0u)
        return TRIGGER_RESERVATION_CONFIG_TIMING;
    if (config->observation_positions == 0u ||
        config->leading_vdc_positions > config->observation_positions ||
        config->trailing_vdc_positions > config->observation_positions -
            config->leading_vdc_positions ||
        config->leading_vdc_positions + config->trailing_vdc_positions ==
            config->observation_positions)
        return TRIGGER_RESERVATION_CONFIG_WINDOW;
    if (config->plan_generation == 0u || config->deployment_generation == 0u)
        return TRIGGER_RESERVATION_CONFIG_IDENTITY;
    return TRIGGER_RESERVATION_CONFIG_OK;
}

bool trigger_reservation_build_plan(const trigger_reservation_config_t *config,
    trigger_reservation_item_t items[TRIGGER_RESERVATION_ITEM_COUNT])
{
    if (config == NULL || items == NULL || config->h_level > 1u ||
        config->v_level > 1u || config->h_level == config->v_level)
        return false;
    for (uint32_t i = 0u; i < TRIGGER_RESERVATION_ITEM_COUNT; ++i) {
        const uint32_t state = i / 2u;
        const trigger_reservation_polarization_t polarization =
            (i & 1u) ? TRIGGER_RESERVATION_POLARIZATION_V
                     : TRIGGER_RESERVATION_POLARIZATION_H;
        const uint32_t level = polarization == TRIGGER_RESERVATION_POLARIZATION_H
            ? config->h_level : config->v_level;
        items[i] = (trigger_reservation_item_t){
            .index = i,
            .sp8t_state = state,
            .polarization = polarization,
            .polarization_level = level,
            .output_code = state | (level << 2u),
        };
    }
    return true;
}

uint32_t trigger_reservation_business_positions(
    const trigger_reservation_config_t *config)
{
    if (config == NULL || config->leading_vdc_positions >
            config->observation_positions ||
        config->trailing_vdc_positions > config->observation_positions -
            config->leading_vdc_positions)
        return 0u;
    return config->observation_positions - config->leading_vdc_positions -
        config->trailing_vdc_positions;
}

uint32_t trigger_reservation_safe_output_code(
    const trigger_reservation_config_t *config)
{
    if (config == NULL || config->sp8t_safe_state > 3u ||
        config->sp2t_safe_level > 1u) return 0u;
    return config->sp8t_safe_state | (config->sp2t_safe_level << 2u);
}

const char *trigger_reservation_config_result_name(
    trigger_reservation_config_result_t result)
{
    static const char *const names[] = {
        "OK", "INVALID", "SLOT", "LEVEL", "TIMING", "WINDOW", "IDENTITY"
    };
    return result < sizeof(names) / sizeof(names[0]) ? names[result] : "UNKNOWN";
}
