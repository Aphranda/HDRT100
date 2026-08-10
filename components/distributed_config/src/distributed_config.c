#include "distributed_config.h"

#include <stddef.h>

#include "ota_crc32.h"

static const distributed_config_role_map_t s_role_map = {
    .version = DISTRIBUTED_CONFIG_VERSION,
    .node_count = DISTRIBUTED_CONFIG_NODE_COUNT,
    .target_mask = 0x0Fu,
    .input_base_pin = SYNC_IO_HW_MAIN_INPUT_BASE_PIN,
    .output_base_pin = SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN,
    .aux_base_pin = SYNC_IO_HW_AUX0_PIN,
    .node = {
        { .node_id = 0u, .role = DISTRIBUTED_CONFIG_ROLE_A0, .persona = 0u, .feature_mask = 0x0Fu },
        { .node_id = 1u, .role = DISTRIBUTED_CONFIG_ROLE_A1, .persona = 1u, .feature_mask = 0x06u },
        { .node_id = 2u, .role = DISTRIBUTED_CONFIG_ROLE_A2, .persona = 2u, .feature_mask = 0x06u },
        { .node_id = 3u, .role = DISTRIBUTED_CONFIG_ROLE_A3, .persona = 3u, .feature_mask = 0x09u },
    },
};

static const distributed_config_loop_plan_t s_loop_plan = {
    .version = DISTRIBUTED_CONFIG_VERSION,
    .node_loop_count = 4u,
    .array_loop_count = 1u,
    .layer_count = DISTRIBUTED_CONFIG_LAYER_COUNT,
    .default_wait_rule = DISTRIBUTED_CONFIG_WAIT_READY,
    .layer = {
        { .layer_id = 0u, .node_id = 0u, .action_id = 0u, .wait_rule = DISTRIBUTED_CONFIG_WAIT_NONE },
        { .layer_id = 1u, .node_id = 1u, .action_id = 1u, .wait_rule = DISTRIBUTED_CONFIG_WAIT_READY },
        { .layer_id = 2u, .node_id = 2u, .action_id = 2u, .wait_rule = DISTRIBUTED_CONFIG_WAIT_READY },
        { .layer_id = 3u, .node_id = 3u, .action_id = 3u, .wait_rule = DISTRIBUTED_CONFIG_WAIT_MEAS_DONE },
    },
};

static const distributed_config_action_map_t s_action_map = {
    .version = DISTRIBUTED_CONFIG_VERSION,
    .action_count = DISTRIBUTED_CONFIG_ACTION_COUNT,
    .action = {
        { .action_id = 0u, .node_id = 0u, .sma_out_pin = SYNC_IO_HW_TRIG_OUT_PIN, .sma_in_pin = SYNC_IO_HW_TRIG_IN_PIN, .edge = DISTRIBUTED_CONFIG_EDGE_RISING, .delay_us = 0u },
        { .action_id = 1u, .node_id = 1u, .sma_out_pin = BOARD_SYNC_PULSE_OUT_PIN, .sma_in_pin = SYNC_IO_HW_ARM_IN_PIN, .edge = DISTRIBUTED_CONFIG_EDGE_RISING, .delay_us = 12u },
        { .action_id = 2u, .node_id = 2u, .sma_out_pin = BOARD_SYNC_MODE_OUT2_PIN, .sma_in_pin = SYNC_IO_HW_EXT_CLK_IN_PIN, .edge = DISTRIBUTED_CONFIG_EDGE_RISING, .delay_us = 18u },
        { .action_id = 3u, .node_id = 3u, .sma_out_pin = SYNC_IO_HW_RJ45_TRIG_OUT_PIN, .sma_in_pin = SYNC_IO_HW_RJ45_TRIG_IN_PIN, .edge = DISTRIBUTED_CONFIG_EDGE_FALLING, .delay_us = 24u },
    },
};

static const distributed_config_calibration_t s_calibration = {
    .version = DISTRIBUTED_CONFIG_VERSION,
    .node_count = DISTRIBUTED_CONFIG_NODE_COUNT,
    .delta_ns = { 0u, 12000u, 18000u, 24000u },
    .sma_hop_ns = 800u,
    .rj45_hop_ns = 1200u,
    .device_delay_ns = 0u,
    .tempco_ppb = 50u,
    .valid_window_ns = 50000u,
};

static distributed_config_snapshot_t s_snapshot;
static bool s_initialized;

static bool distributed_config_has_node(uint32_t node_id)
{
    return node_id < s_role_map.node_count;
}

static bool distributed_config_has_action(uint32_t action_id)
{
    return action_id < s_action_map.action_count;
}

static uint32_t distributed_config_crc32(const void *data, size_t size)
{
    return ota_crc32_compute((const uint8_t *)data, (uint32_t)size);
}

bool distributed_config_init(void)
{
    s_snapshot.config_version = DISTRIBUTED_CONFIG_VERSION;
    s_snapshot.role_map_version = s_role_map.version;
    s_snapshot.loop_plan_version = s_loop_plan.version;
    s_snapshot.action_map_version = s_action_map.version;
    s_snapshot.calibration_version = s_calibration.version;
    s_snapshot.target_mask = s_role_map.target_mask;
    s_snapshot.role_map_crc32 = distributed_config_crc32(&s_role_map, sizeof(s_role_map));
    s_snapshot.loop_plan_crc32 = distributed_config_crc32(&s_loop_plan, sizeof(s_loop_plan));
    s_snapshot.action_map_crc32 = distributed_config_crc32(&s_action_map, sizeof(s_action_map));
    s_snapshot.calibration_crc32 = distributed_config_crc32(&s_calibration, sizeof(s_calibration));
    s_snapshot.config_crc32 = distributed_config_crc32(&s_snapshot,
                                                       offsetof(distributed_config_snapshot_t,
                                                                config_crc32));
    s_initialized = true;
    return true;
}

const distributed_config_role_map_t *distributed_config_get_role_map(void)
{
    return &s_role_map;
}

const distributed_config_loop_plan_t *distributed_config_get_loop_plan(void)
{
    return &s_loop_plan;
}

const distributed_config_action_map_t *distributed_config_get_action_map(void)
{
    return &s_action_map;
}

const distributed_config_calibration_t *distributed_config_get_calibration(void)
{
    return &s_calibration;
}

const distributed_config_snapshot_t *distributed_config_get_snapshot(void)
{
    if (!s_initialized) {
        distributed_config_init();
    }

    return &s_snapshot;
}

bool distributed_config_validate(void)
{
    if (s_role_map.version != DISTRIBUTED_CONFIG_VERSION ||
        s_loop_plan.version != DISTRIBUTED_CONFIG_VERSION ||
        s_action_map.version != DISTRIBUTED_CONFIG_VERSION ||
        s_calibration.version != DISTRIBUTED_CONFIG_VERSION) {
        return false;
    }

    if (s_role_map.node_count != DISTRIBUTED_CONFIG_NODE_COUNT ||
        s_loop_plan.layer_count != DISTRIBUTED_CONFIG_LAYER_COUNT ||
        s_action_map.action_count != DISTRIBUTED_CONFIG_ACTION_COUNT ||
        s_calibration.node_count != DISTRIBUTED_CONFIG_NODE_COUNT) {
        return false;
    }

    if (s_role_map.target_mask != ((1u << DISTRIBUTED_CONFIG_NODE_COUNT) - 1u)) {
        return false;
    }

    for (uint32_t i = 0u; i < s_role_map.node_count; i++) {
        const distributed_config_role_entry_t *node = &s_role_map.node[i];
        if (node->node_id != i ||
            node->role > (uint32_t)DISTRIBUTED_CONFIG_ROLE_A3 ||
            (s_role_map.target_mask & (1u << node->node_id)) == 0u) {
            return false;
        }
    }

    for (uint32_t i = 0u; i < s_loop_plan.layer_count; i++) {
        const distributed_config_layer_entry_t *layer = &s_loop_plan.layer[i];
        if (layer->layer_id != i ||
            !distributed_config_has_node(layer->node_id) ||
            !distributed_config_has_action(layer->action_id) ||
            layer->wait_rule > (uint32_t)DISTRIBUTED_CONFIG_WAIT_FAULT_CLEAR) {
            return false;
        }
    }

    for (uint32_t i = 0u; i < s_action_map.action_count; i++) {
        const distributed_config_action_entry_t *action = &s_action_map.action[i];
        if (action->action_id != i ||
            !distributed_config_has_node(action->node_id) ||
            action->edge > (uint32_t)DISTRIBUTED_CONFIG_EDGE_FALLING) {
            return false;
        }
    }

    return true;
}
