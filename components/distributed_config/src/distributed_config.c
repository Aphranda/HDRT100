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

static const distributed_config_nack_reason_table_t s_nack_reason_table = {
    .version = DISTRIBUTED_CONFIG_VERSION,
    .reason_count = DISTRIBUTED_CONFIG_NACK_REASON_COUNT,
    .reason = {
        {
            .reason_id = DISTRIBUTED_CONFIG_NACK_NONE,
            .severity = 0u,
            .retryable = 0u,
            .blocking = 0u,
            .detail_code = 0u,
            .name = "NONE",
        },
        {
            .reason_id = DISTRIBUTED_CONFIG_NACK_CONFIG_CRC_MISMATCH,
            .severity = 2u,
            .retryable = 1u,
            .blocking = 1u,
            .detail_code = 1001u,
            .name = "CONFIG_CRC_MISMATCH",
        },
        {
            .reason_id = DISTRIBUTED_CONFIG_NACK_HW_PROFILE_MISMATCH,
            .severity = 2u,
            .retryable = 0u,
            .blocking = 1u,
            .detail_code = 1002u,
            .name = "HW_PROFILE_MISMATCH",
        },
        {
            .reason_id = DISTRIBUTED_CONFIG_NACK_NODE_STALE,
            .severity = 1u,
            .retryable = 1u,
            .blocking = 1u,
            .detail_code = 1003u,
            .name = "NODE_STALE",
        },
        {
            .reason_id = DISTRIBUTED_CONFIG_NACK_NODE_FAULT,
            .severity = 2u,
            .retryable = 0u,
            .blocking = 1u,
            .detail_code = 1004u,
            .name = "NODE_FAULT",
        },
        {
            .reason_id = DISTRIBUTED_CONFIG_NACK_FLASH_LOCKOUT_UNREADY,
            .severity = 2u,
            .retryable = 1u,
            .blocking = 1u,
            .detail_code = 1005u,
            .name = "FLASH_LOCKOUT_UNREADY",
        },
    },
};

static distributed_config_scpi_run_policy_table_t s_scpi_run_policy_table = {
    .version = DISTRIBUTED_CONFIG_VERSION,
    .entry_count = DISTRIBUTED_CONFIG_SCPI_RUN_POLICY_COUNT,
    .enforced = 1u,
    .policy_crc32 = 0u,
    .entry = {
        {
            .index = 0u,
            .class_id = DISTRIBUTED_CONFIG_SCPI_CLASS_STATUS_QUERY,
            .run_allowed = 1u,
            .query_allowed = 1u,
            .write_allowed = 0u,
            .forbidden_error_code = DISTRIBUTED_CONFIG_SCPI_FORBID_NONE,
            .pattern = "*IDN?/SYST:*/STAT?",
        },
        {
            .index = 1u,
            .class_id = DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_STOP,
            .run_allowed = 1u,
            .query_allowed = 0u,
            .write_allowed = 1u,
            .forbidden_error_code = DISTRIBUTED_CONFIG_SCPI_FORBID_NONE,
            .pattern = "TRIG:DIS/TRIG:FAULT",
        },
        {
            .index = 2u,
            .class_id = DISTRIBUTED_CONFIG_SCPI_CLASS_FAULT_CLEAR,
            .run_allowed = 1u,
            .query_allowed = 0u,
            .write_allowed = 1u,
            .forbidden_error_code = DISTRIBUTED_CONFIG_SCPI_FORBID_NONE,
            .pattern = "*CLS/SYST:ERR?",
        },
        {
            .index = 3u,
            .class_id = DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG,
            .run_allowed = 0u,
            .query_allowed = 1u,
            .write_allowed = 1u,
            .forbidden_error_code = DISTRIBUTED_CONFIG_SCPI_FORBID_RUN_STATE,
            .pattern = "TRIG/PULS/MARK/RJ45/SAMP/OUTP",
        },
        {
            .index = 4u,
            .class_id = DISTRIBUTED_CONFIG_SCPI_CLASS_IO_CONFIG,
            .run_allowed = 0u,
            .query_allowed = 1u,
            .write_allowed = 1u,
            .forbidden_error_code = DISTRIBUTED_CONFIG_SCPI_FORBID_RUN_STATE,
            .pattern = "GPIO/PIO/ROLE/LOOP/ACTION/CAL",
        },
        {
            .index = 5u,
            .class_id = DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT,
            .run_allowed = 0u,
            .query_allowed = 1u,
            .write_allowed = 1u,
            .forbidden_error_code = DISTRIBUTED_CONFIG_SCPI_FORBID_RESOURCE_BUSY,
            .pattern = "MMEM/SD/SNAP/TRAC",
        },
        {
            .index = 6u,
            .class_id = DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT,
            .run_allowed = 0u,
            .query_allowed = 1u,
            .write_allowed = 1u,
            .forbidden_error_code = DISTRIBUTED_CONFIG_SCPI_FORBID_RESOURCE_BUSY,
            .pattern = "SYST:OTA/SYST:BOOT:REPAIR",
        },
    },
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

static uint32_t distributed_config_crc32_string(uint32_t seed, const char *text)
{
    if (text == NULL) {
        return seed;
    }

    uint32_t crc = seed;
    for (const char *p = text; *p != '\0'; p++) {
        crc ^= (uint8_t)*p;
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t distributed_config_nack_reason_crc32(void)
{
    uint32_t crc = distributed_config_crc32(&s_nack_reason_table.version,
                                           2u * sizeof(uint32_t));
    for (uint32_t i = 0u; i < s_nack_reason_table.reason_count; i++) {
        const distributed_config_nack_reason_entry_t *reason =
            &s_nack_reason_table.reason[i];
        const uint32_t fields[] = {
            reason->reason_id,
            reason->severity,
            reason->retryable,
            reason->blocking,
            reason->detail_code,
        };
        crc ^= distributed_config_crc32(fields, sizeof(fields));
        crc = distributed_config_crc32_string(crc, reason->name);
    }
    return crc;
}

static uint32_t distributed_config_scpi_run_policy_crc32(void)
{
    uint32_t crc = distributed_config_crc32(&s_scpi_run_policy_table.version,
                                           3u * sizeof(uint32_t));
    for (uint32_t i = 0u; i < s_scpi_run_policy_table.entry_count; i++) {
        const distributed_config_scpi_run_policy_entry_t *entry =
            &s_scpi_run_policy_table.entry[i];
        const uint32_t fields[] = {
            entry->index,
            entry->class_id,
            entry->run_allowed,
            entry->query_allowed,
            entry->write_allowed,
            entry->forbidden_error_code,
        };
        crc ^= distributed_config_crc32(fields, sizeof(fields));
        crc = distributed_config_crc32_string(crc, entry->pattern);
    }
    return crc;
}

bool distributed_config_init(void)
{
    s_scpi_run_policy_table.policy_crc32 =
        distributed_config_scpi_run_policy_crc32();

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
    s_snapshot.nack_reason_crc32 = distributed_config_nack_reason_crc32();
    s_snapshot.scpi_run_policy_crc32 = s_scpi_run_policy_table.policy_crc32;
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

const distributed_config_nack_reason_table_t *distributed_config_get_nack_reason_table(void)
{
    return &s_nack_reason_table;
}

const distributed_config_scpi_run_policy_table_t *distributed_config_get_scpi_run_policy_table(void)
{
    if (!s_initialized) {
        distributed_config_init();
    }

    return &s_scpi_run_policy_table;
}

const distributed_config_snapshot_t *distributed_config_get_snapshot(void)
{
    if (!s_initialized) {
        distributed_config_init();
    }

    return &s_snapshot;
}

bool distributed_config_get_nack_reason(uint32_t reason_id,
                                        const distributed_config_nack_reason_entry_t **entry)
{
    if (entry == NULL) {
        return false;
    }

    for (uint32_t i = 0u; i < s_nack_reason_table.reason_count; i++) {
        if (s_nack_reason_table.reason[i].reason_id == reason_id) {
            *entry = &s_nack_reason_table.reason[i];
            return true;
        }
    }

    return false;
}

bool distributed_config_get_scpi_run_policy(uint32_t index,
                                            const distributed_config_scpi_run_policy_entry_t **entry)
{
    const distributed_config_scpi_run_policy_table_t *table =
        distributed_config_get_scpi_run_policy_table();
    if (entry == NULL || index >= table->entry_count) {
        return false;
    }

    *entry = &table->entry[index];
    return true;
}

static const distributed_config_scpi_run_policy_entry_t *
distributed_config_find_scpi_class(uint32_t class_id)
{
    const distributed_config_scpi_run_policy_table_t *table =
        distributed_config_get_scpi_run_policy_table();
    for (uint32_t i = 0u; i < table->entry_count; i++) {
        if (table->entry[i].class_id == class_id) {
            return &table->entry[i];
        }
    }

    return NULL;
}

bool distributed_config_scpi_run_class_allowed(uint32_t class_id, bool is_query)
{
    const distributed_config_scpi_run_policy_entry_t *entry =
        distributed_config_find_scpi_class(class_id);
    if (entry == NULL) {
        return false;
    }

    if (is_query) {
        return entry->query_allowed != 0u;
    }

    return entry->run_allowed != 0u;
}

uint32_t distributed_config_scpi_run_class_forbid_code(uint32_t class_id)
{
    const distributed_config_scpi_run_policy_entry_t *entry =
        distributed_config_find_scpi_class(class_id);
    return entry != NULL ? entry->forbidden_error_code :
                           DISTRIBUTED_CONFIG_SCPI_FORBID_RUN_STATE;
}

bool distributed_config_validate(void)
{
    if (s_role_map.version != DISTRIBUTED_CONFIG_VERSION ||
        s_loop_plan.version != DISTRIBUTED_CONFIG_VERSION ||
        s_action_map.version != DISTRIBUTED_CONFIG_VERSION ||
        s_calibration.version != DISTRIBUTED_CONFIG_VERSION ||
        s_nack_reason_table.version != DISTRIBUTED_CONFIG_VERSION ||
        s_scpi_run_policy_table.version != DISTRIBUTED_CONFIG_VERSION) {
        return false;
    }

    if (s_role_map.node_count != DISTRIBUTED_CONFIG_NODE_COUNT ||
        s_loop_plan.layer_count != DISTRIBUTED_CONFIG_LAYER_COUNT ||
        s_action_map.action_count != DISTRIBUTED_CONFIG_ACTION_COUNT ||
        s_calibration.node_count != DISTRIBUTED_CONFIG_NODE_COUNT ||
        s_nack_reason_table.reason_count != DISTRIBUTED_CONFIG_NACK_REASON_COUNT ||
        s_scpi_run_policy_table.entry_count != DISTRIBUTED_CONFIG_SCPI_RUN_POLICY_COUNT) {
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

    for (uint32_t i = 0u; i < s_nack_reason_table.reason_count; i++) {
        const distributed_config_nack_reason_entry_t *reason =
            &s_nack_reason_table.reason[i];
        if (reason->reason_id != i || reason->name == NULL) {
            return false;
        }
    }

    for (uint32_t i = 0u; i < s_scpi_run_policy_table.entry_count; i++) {
        const distributed_config_scpi_run_policy_entry_t *entry =
            &s_scpi_run_policy_table.entry[i];
        if (entry->index != i ||
            entry->pattern == NULL ||
            entry->class_id > (uint32_t)DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT) {
            return false;
        }
        if (entry->run_allowed == 0u &&
            entry->forbidden_error_code == DISTRIBUTED_CONFIG_SCPI_FORBID_NONE) {
            return false;
        }
    }

    return true;
}
