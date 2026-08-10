#ifndef DISTRIBUTED_CONFIG_H
#define DISTRIBUTED_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "sync_io_hw_profile.h"

#define DISTRIBUTED_CONFIG_VERSION        1u
#define DISTRIBUTED_CONFIG_NODE_COUNT     4u
#define DISTRIBUTED_CONFIG_LAYER_COUNT    4u
#define DISTRIBUTED_CONFIG_ACTION_COUNT   4u

typedef enum {
    DISTRIBUTED_CONFIG_ROLE_A0 = 0u,
    DISTRIBUTED_CONFIG_ROLE_A1 = 1u,
    DISTRIBUTED_CONFIG_ROLE_A2 = 2u,
    DISTRIBUTED_CONFIG_ROLE_A3 = 3u,
} distributed_config_role_t;

typedef enum {
    DISTRIBUTED_CONFIG_EDGE_RISING = 0u,
    DISTRIBUTED_CONFIG_EDGE_FALLING = 1u,
} distributed_config_edge_t;

typedef enum {
    DISTRIBUTED_CONFIG_WAIT_NONE = 0u,
    DISTRIBUTED_CONFIG_WAIT_READY = 1u,
    DISTRIBUTED_CONFIG_WAIT_MEAS_DONE = 2u,
    DISTRIBUTED_CONFIG_WAIT_FAULT_CLEAR = 3u,
} distributed_config_wait_rule_t;

typedef struct {
    uint32_t node_id;
    uint32_t role;
    uint32_t persona;
    uint32_t feature_mask;
} distributed_config_role_entry_t;

typedef struct {
    uint32_t version;
    uint32_t node_count;
    uint32_t target_mask;
    uint32_t input_base_pin;
    uint32_t output_base_pin;
    uint32_t aux_base_pin;
    distributed_config_role_entry_t node[DISTRIBUTED_CONFIG_NODE_COUNT];
} distributed_config_role_map_t;

typedef struct {
    uint32_t layer_id;
    uint32_t node_id;
    uint32_t action_id;
    uint32_t wait_rule;
} distributed_config_layer_entry_t;

typedef struct {
    uint32_t version;
    uint32_t node_loop_count;
    uint32_t array_loop_count;
    uint32_t layer_count;
    uint32_t default_wait_rule;
    distributed_config_layer_entry_t layer[DISTRIBUTED_CONFIG_LAYER_COUNT];
} distributed_config_loop_plan_t;

typedef struct {
    uint32_t action_id;
    uint32_t node_id;
    uint32_t sma_out_pin;
    uint32_t sma_in_pin;
    uint32_t edge;
    uint32_t delay_us;
} distributed_config_action_entry_t;

typedef struct {
    uint32_t version;
    uint32_t action_count;
    distributed_config_action_entry_t action[DISTRIBUTED_CONFIG_ACTION_COUNT];
} distributed_config_action_map_t;

typedef struct {
    uint32_t version;
    uint32_t node_count;
    uint32_t delta_ns[DISTRIBUTED_CONFIG_NODE_COUNT];
    uint32_t sma_hop_ns;
    uint32_t rj45_hop_ns;
    uint32_t device_delay_ns;
    uint32_t tempco_ppb;
    uint32_t valid_window_ns;
} distributed_config_calibration_t;

typedef struct {
    uint32_t config_version;
    uint32_t role_map_version;
    uint32_t loop_plan_version;
    uint32_t action_map_version;
    uint32_t calibration_version;
    uint32_t target_mask;
    uint32_t role_map_crc32;
    uint32_t loop_plan_crc32;
    uint32_t action_map_crc32;
    uint32_t calibration_crc32;
    uint32_t config_crc32;
} distributed_config_snapshot_t;

bool distributed_config_init(void);
const distributed_config_role_map_t *distributed_config_get_role_map(void);
const distributed_config_loop_plan_t *distributed_config_get_loop_plan(void);
const distributed_config_action_map_t *distributed_config_get_action_map(void);
const distributed_config_calibration_t *distributed_config_get_calibration(void);
const distributed_config_snapshot_t *distributed_config_get_snapshot(void);
bool distributed_config_validate(void);

#endif
