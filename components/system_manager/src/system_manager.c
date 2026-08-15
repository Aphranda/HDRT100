#include "system_manager.h"

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "osal.h"
#include "ota_crc32.h"
#include "project_config.h"
#include "refmem_application_model.h"
#include "refmem_slot_claim.h"
#include "resource_arbiter.h"
#include "sync_io_hw_profile.h"

#define SYSTEM_MANAGER_MODE_TABLE_VERSION 1u
#define SYSTEM_MANAGER_RESOURCE_TABLE_VERSION 1u
#define SYSTEM_MANAGER_FAULT_TABLE_VERSION 1u

typedef struct {
    uint32_t main_input_base_pin;
    uint32_t main_input_pin_count;
    uint32_t main_output_base_pin;
    uint32_t main_output_pin_count;
    uint32_t trig_in_pin;
    uint32_t gate_in_pin;
    uint32_t trig_out_pin;
    uint32_t rj45_trig_in_pin;
    uint32_t rj45_trig_out_pin;
    uint32_t arm_in_pin;
    uint32_t ext_clk_in_pin;
    uint32_t sync_clk_out_pin;
    uint32_t aux0_pin;
    uint32_t aux1_pin;
    uint32_t aux2_pin;
    uint32_t aux3_pin;
} system_manager_hw_profile_blob_t;

static system_manager_config_gate_status_t s_config_gate_status;
static bool s_initialized;

static refmem_command_reason_t system_manager_refmem_reason_from_config(
    distributed_config_nack_reason_t reason)
{
    switch (reason) {
    case DISTRIBUTED_CONFIG_NACK_CONFIG_CRC_MISMATCH:
        return REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH;
    case DISTRIBUTED_CONFIG_NACK_HW_PROFILE_MISMATCH:
        return REFMEM_COMMAND_REASON_HW_PROFILE_MISMATCH;
    case DISTRIBUTED_CONFIG_NACK_NODE_STALE:
        return REFMEM_COMMAND_REASON_NODE_STALE;
    case DISTRIBUTED_CONFIG_NACK_NODE_FAULT:
        return REFMEM_COMMAND_REASON_NODE_FAULT;
    case DISTRIBUTED_CONFIG_NACK_FLASH_LOCKOUT_UNREADY:
        return REFMEM_COMMAND_REASON_FLASH_LOCKOUT_UNREADY;
    case DISTRIBUTED_CONFIG_NACK_NONE:
    default:
        return REFMEM_COMMAND_REASON_NONE;
    }
}

static distributed_config_nack_reason_t system_manager_config_reason_from_refmem(
    uint32_t reason)
{
    switch ((refmem_command_reason_t)reason) {
    case REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH:
        return DISTRIBUTED_CONFIG_NACK_CONFIG_CRC_MISMATCH;
    case REFMEM_COMMAND_REASON_HW_PROFILE_MISMATCH:
        return DISTRIBUTED_CONFIG_NACK_HW_PROFILE_MISMATCH;
    case REFMEM_COMMAND_REASON_NODE_STALE:
        return DISTRIBUTED_CONFIG_NACK_NODE_STALE;
    case REFMEM_COMMAND_REASON_NODE_FAULT:
        return DISTRIBUTED_CONFIG_NACK_NODE_FAULT;
    case REFMEM_COMMAND_REASON_FLASH_LOCKOUT_UNREADY:
        return DISTRIBUTED_CONFIG_NACK_FLASH_LOCKOUT_UNREADY;
    case REFMEM_COMMAND_REASON_NONE:
    default:
        return DISTRIBUTED_CONFIG_NACK_NONE;
    }
}

static distributed_config_nack_reason_t system_manager_select_config_nack_reason(
    bool config_valid,
    bool refmem_claim_valid,
    bool refmem_quality_valid)
{
    if (!config_valid) {
        return DISTRIBUTED_CONFIG_NACK_CONFIG_CRC_MISMATCH;
    }
    if (!refmem_claim_valid || !refmem_quality_valid) {
        return DISTRIBUTED_CONFIG_NACK_NODE_FAULT;
    }
    return DISTRIBUTED_CONFIG_NACK_NONE;
}

static void system_manager_copy_command_snapshot(
    const refmem_command_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    s_config_gate_status.command_seq = snapshot->command_seq;
    s_config_gate_status.target_mask = snapshot->target_mask;
    s_config_gate_status.ack_flags = snapshot->ack_flags;
    s_config_gate_status.nack_flags = snapshot->nack_flags;
    s_config_gate_status.busy_flags = snapshot->busy_flags;
    s_config_gate_status.timeout_flags = snapshot->timeout_flags;
}

static void system_manager_publish_config_command(bool gate_ready,
                                                  distributed_config_nack_reason_t reason,
                                                  uint32_t now_ms)
{
    const uint32_t desired_ack = gate_ready ? s_config_gate_status.target_mask : 0u;
    const uint32_t desired_nack = gate_ready ? 0u : s_config_gate_status.target_mask;
    refmem_command_snapshot_t snapshot;
    const bool has_snapshot = distributed_refmem_get_command_snapshot(&snapshot);

    if (has_snapshot &&
        snapshot.command_seq != 0u &&
        snapshot.command_type != REFMEM_COMMAND_TYPE_CONFIG_ACTIVATE) {
        return;
    }

    if (has_snapshot &&
        snapshot.command_seq != 0u &&
        snapshot.target_mask == s_config_gate_status.target_mask &&
        snapshot.ack_flags == desired_ack &&
        snapshot.nack_flags == desired_nack &&
        snapshot.busy_flags == 0u &&
        snapshot.timeout_flags == 0u) {
        system_manager_copy_command_snapshot(&snapshot);
        return;
    }

    if (has_snapshot && snapshot.command_seq != 0u) {
        (void)distributed_refmem_command_clear(snapshot.command_seq);
        if (s_config_gate_status.command_seq <= snapshot.command_seq) {
            s_config_gate_status.command_seq = snapshot.command_seq + 1u;
        }
    }
    if (s_config_gate_status.command_seq == 0u) {
        s_config_gate_status.command_seq = 1u;
    }

    const refmem_command_request_t request = {
        .command_seq = s_config_gate_status.command_seq,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = 0u,
        .target_mask = s_config_gate_status.target_mask,
        .required_mask = s_config_gate_status.target_mask,
        .command_type = REFMEM_COMMAND_TYPE_CONFIG_ACTIVATE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_STAGING_REF,
        .payload_ref = 0u,
        .payload_size = 0u,
        .payload_crc32 = s_config_gate_status.config_crc32,
        .issue_epoch = s_config_gate_status.epoch,
        .run_id = s_config_gate_status.run_id,
        .timeout_us = 100000u,
    };

    if (!distributed_refmem_command_try_post(&request, now_ms)) {
        if (distributed_refmem_get_command_snapshot(&snapshot)) {
            system_manager_copy_command_snapshot(&snapshot);
        }
        return;
    }

    for (uint32_t node = 0u; node < DISTRIBUTED_REFMEM_NODE_COUNT; node++) {
        if ((s_config_gate_status.target_mask & (1u << node)) == 0u) {
            continue;
        }
        if (gate_ready) {
            (void)distributed_refmem_command_ack(node, 0u);
        } else {
            (void)distributed_refmem_command_nack(
                node,
                system_manager_refmem_reason_from_config(reason),
                0u);
        }
    }

    if (distributed_refmem_get_command_snapshot(&snapshot)) {
        system_manager_copy_command_snapshot(&snapshot);
    }
}

static bool system_manager_refmem_claim_gate_ready(void)
{
    refmem_slot_claim_map_t claim_map;
    refmem_slot_claim_gate_status_t claim_gate;
    if (!refmem_slot_claim_derive_map(refmem_application_model_get_generic_node_table(),
                                      refmem_application_model_get_board_capability_table(),
                                      refmem_application_model_get_node_load_table(),
                                      refmem_application_model_get_fb_instance_table(),
                                      &claim_map)) {
        return false;
    }
    return refmem_slot_claim_gate_evaluate(&claim_map, &claim_gate);
}

static bool system_manager_refmem_quality_gate_ready(void)
{
    return distributed_refmem_quality_gate_ready();
}

static const system_manager_mode_entry_t s_system_mode_template[4] = {
    { .mode_id = RESOURCE_ARBITER_MODE_BOOT, .run_allowed = 0u, .ota_allowed = 0u, .fault_allowed = 1u, .name = "BOOT" },
    { .mode_id = RESOURCE_ARBITER_MODE_RUN, .run_allowed = 1u, .ota_allowed = 0u, .fault_allowed = 1u, .name = "RUN" },
    { .mode_id = RESOURCE_ARBITER_MODE_OTA, .run_allowed = 0u, .ota_allowed = 1u, .fault_allowed = 1u, .name = "OTA" },
    { .mode_id = RESOURCE_ARBITER_MODE_FAULT, .run_allowed = 0u, .ota_allowed = 0u, .fault_allowed = 1u, .name = "FAULT" },
};

static const system_manager_resource_entry_t s_resource_template[10] = {
    { .resource_id = 0u, .mask = RESOURCE_ARBITER_RESOURCE_FLASH, .owner_mode = RESOURCE_ARBITER_MODE_OTA, .active = 0u, .name = "FLASH", .owner_name = "-" },
    { .resource_id = 1u, .mask = RESOURCE_ARBITER_RESOURCE_SPI0, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "SPI0", .owner_name = "-" },
    { .resource_id = 2u, .mask = RESOURCE_ARBITER_RESOURCE_USB, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "USB", .owner_name = "-" },
    { .resource_id = 3u, .mask = RESOURCE_ARBITER_RESOURCE_PIO0, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "PIO0", .owner_name = "-" },
    { .resource_id = 4u, .mask = RESOURCE_ARBITER_RESOURCE_PIO1, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "PIO1", .owner_name = "-" },
    { .resource_id = 5u, .mask = RESOURCE_ARBITER_RESOURCE_PIO2, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "PIO2", .owner_name = "-" },
    { .resource_id = 6u, .mask = RESOURCE_ARBITER_RESOURCE_DMA, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "DMA", .owner_name = "-" },
    { .resource_id = 7u, .mask = RESOURCE_ARBITER_RESOURCE_LCD, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "LCD", .owner_name = "-" },
    { .resource_id = 8u, .mask = RESOURCE_ARBITER_RESOURCE_SD, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "SD", .owner_name = "-" },
    { .resource_id = 9u, .mask = RESOURCE_ARBITER_RESOURCE_AUX, .owner_mode = RESOURCE_ARBITER_MODE_RUN, .active = 0u, .name = "AUX", .owner_name = "-" },
};

static const system_manager_fault_entry_t s_fault_code_template[20] = {
    { .fault_id = 0u, .domain_id = 0u, .severity = 0u, .recoverable = 1u, .sticky = 0u, .name = "NONE" },
    { .fault_id = 1u, .domain_id = 0u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "DIAG_FAULT_LATCHED" },
    { .fault_id = 2u, .domain_id = 1u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "TRIG_INVALID_SEQ_CONFIG" },
    { .fault_id = 3u, .domain_id = 1u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "TRIG_RESOURCE_CONFLICT" },
    { .fault_id = 4u, .domain_id = 1u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "TRIG_IO_ARM_FAILED" },
    { .fault_id = 5u, .domain_id = 1u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "TRIG_IO_LOST" },
    { .fault_id = 10u, .domain_id = 1u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "TRIG_INVALID_ENC_TARGET" },
    { .fault_id = 11u, .domain_id = 1u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "TRIG_INVALID_ENC_PINS" },
    { .fault_id = 20u, .domain_id = 1u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "TRIG_INVALID_BISS_CONFIG" },
    { .fault_id = 100u, .domain_id = 1u, .severity = 3u, .recoverable = 0u, .sticky = 1u, .name = "TRIG_FORCED_FAULT" },
    { .fault_id = 200u, .domain_id = 2u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "OTA_BUSY" },
    { .fault_id = 201u, .domain_id = 2u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "OTA_INVALID_STATE" },
    { .fault_id = 202u, .domain_id = 2u, .severity = 2u, .recoverable = 0u, .sticky = 1u, .name = "OTA_IMAGE_TOO_LARGE" },
    { .fault_id = 203u, .domain_id = 2u, .severity = 2u, .recoverable = 0u, .sticky = 1u, .name = "OTA_BAD_HEADER" },
    { .fault_id = 204u, .domain_id = 2u, .severity = 3u, .recoverable = 0u, .sticky = 1u, .name = "OTA_FLASH_ERASE" },
    { .fault_id = 205u, .domain_id = 2u, .severity = 3u, .recoverable = 0u, .sticky = 1u, .name = "OTA_FLASH_PROGRAM" },
    { .fault_id = 300u, .domain_id = 3u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "STORAGE_PATH_DENIED" },
    { .fault_id = 301u, .domain_id = 3u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "STORAGE_MANIFEST_INVALID" },
    { .fault_id = 304u, .domain_id = 3u, .severity = 1u, .recoverable = 1u, .sticky = 0u, .name = "STORAGE_RESOURCE_BUSY" },
    { .fault_id = 305u, .domain_id = 3u, .severity = 2u, .recoverable = 1u, .sticky = 1u, .name = "STORAGE_PATH_ERROR" },
};

static uint32_t system_manager_crc32_blob(const void *data, size_t size)
{
    return ota_crc32_compute((const uint8_t *)data, (uint32_t)size);
}

static uint32_t system_manager_crc32_text(const char *text)
{
    return system_manager_crc32_blob(text, strlen(text));
}

static uint32_t system_manager_crc32_table_entry(const uint32_t *fields,
                                                 size_t field_count,
                                                 const char *name,
                                                 const char *owner)
{
    uint8_t buffer[160];
    size_t used = 0u;

    if (fields != NULL && field_count > 0u) {
        const size_t field_bytes = field_count * sizeof(uint32_t);
        if (field_bytes > sizeof(buffer)) {
            return 0u;
        }
        memcpy(buffer, fields, field_bytes);
        used = field_bytes;
    }

    if (name != NULL) {
        const size_t len = strlen(name);
        if (used + len > sizeof(buffer)) {
            return 0u;
        }
        memcpy(&buffer[used], name, len);
        used += len;
    }

    if (owner != NULL) {
        if (used + 1u > sizeof(buffer)) {
            return 0u;
        }
        buffer[used++] = '|';
        const size_t len = strlen(owner);
        if (used + len > sizeof(buffer)) {
            return 0u;
        }
        memcpy(&buffer[used], owner, len);
        used += len;
    }

    return system_manager_crc32_blob(buffer, used);
}

static uint32_t system_manager_mode_table_crc32(void)
{
    uint32_t crc = SYSTEM_MANAGER_MODE_TABLE_VERSION;
    for (uint32_t i = 0u; i < 4u; i++) {
        const system_manager_mode_entry_t *entry = &s_system_mode_template[i];
        const uint32_t fields[] = {
            entry->mode_id,
            entry->run_allowed,
            entry->ota_allowed,
            entry->fault_allowed,
        };
        crc ^= system_manager_crc32_table_entry(fields,
                                                sizeof(fields) / sizeof(fields[0]),
                                                entry->name,
                                                NULL);
    }
    return crc;
}

static uint32_t system_manager_resource_table_crc32(void)
{
    uint32_t crc = SYSTEM_MANAGER_RESOURCE_TABLE_VERSION;
    for (uint32_t i = 0u; i < 10u; i++) {
        const system_manager_resource_entry_t *entry = &s_resource_template[i];
        const uint32_t fields[] = {
            entry->resource_id,
            entry->mask,
            entry->owner_mode,
        };
        crc ^= system_manager_crc32_table_entry(fields,
                                                sizeof(fields) / sizeof(fields[0]),
                                                entry->name,
                                                NULL);
    }
    return crc;
}

static uint32_t system_manager_fault_table_crc32(void)
{
    uint32_t crc = SYSTEM_MANAGER_FAULT_TABLE_VERSION;
    for (uint32_t i = 0u; i < 20u; i++) {
        const system_manager_fault_entry_t *entry = &s_fault_code_template[i];
        const uint32_t fields[] = {
            entry->fault_id,
            entry->domain_id,
            entry->severity,
            entry->recoverable,
            entry->sticky,
        };
        crc ^= system_manager_crc32_table_entry(fields,
                                                sizeof(fields) / sizeof(fields[0]),
                                                entry->name,
                                                NULL);
    }
    return crc;
}

bool system_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_config_gate_status, 0, sizeof(s_config_gate_status));
    s_config_gate_status.last_service_ms = now_ms;
    s_config_gate_status.epoch = now_ms;
    s_config_gate_status.config_version = 1u;
    s_config_gate_status.calibration_version = 1u;
    s_config_gate_status.loop_plan_version = 1u;
    s_config_gate_status.action_map_version = 1u;
    s_config_gate_status.command_seq = 1u;
    s_config_gate_status.target_mask = 0x0Fu;
    s_config_gate_status.build_crc32 = system_manager_crc32_text(g_project_build_id);

    const system_manager_hw_profile_blob_t hw_profile = {
        .main_input_base_pin = SYNC_IO_HW_MAIN_INPUT_BASE_PIN,
        .main_input_pin_count = SYNC_IO_HW_MAIN_INPUT_PIN_COUNT,
        .main_output_base_pin = SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN,
        .main_output_pin_count = SYNC_IO_HW_MAIN_OUTPUT_PIN_COUNT,
        .trig_in_pin = SYNC_IO_HW_TRIG_IN_PIN,
        .gate_in_pin = SYNC_IO_HW_GATE_IN_PIN,
        .trig_out_pin = SYNC_IO_HW_TRIG_OUT_PIN,
        .rj45_trig_in_pin = SYNC_IO_HW_RJ45_TRIG_IN_PIN,
        .rj45_trig_out_pin = SYNC_IO_HW_RJ45_TRIG_OUT_PIN,
        .arm_in_pin = SYNC_IO_HW_ARM_IN_PIN,
        .ext_clk_in_pin = SYNC_IO_HW_EXT_CLK_IN_PIN,
        .sync_clk_out_pin = SYNC_IO_HW_SYNC_CLK_OUT_PIN,
        .aux0_pin = SYNC_IO_HW_AUX0_PIN,
        .aux1_pin = SYNC_IO_HW_AUX1_PIN,
        .aux2_pin = SYNC_IO_HW_AUX2_PIN,
        .aux3_pin = SYNC_IO_HW_AUX3_PIN,
    };
    s_config_gate_status.hw_profile_crc32 =
        system_manager_crc32_blob(&hw_profile, sizeof(hw_profile));

    const distributed_config_snapshot_t *config_snapshot =
        distributed_config_get_snapshot();
    s_config_gate_status.config_version = config_snapshot->config_version;
    s_config_gate_status.calibration_version = config_snapshot->calibration_version;
    s_config_gate_status.loop_plan_version = config_snapshot->loop_plan_version;
    s_config_gate_status.action_map_version = config_snapshot->action_map_version;
    s_config_gate_status.target_mask = config_snapshot->target_mask;
    s_config_gate_status.role_map_crc32 = config_snapshot->role_map_crc32;
    s_config_gate_status.loop_plan_crc32 = config_snapshot->loop_plan_crc32;
    s_config_gate_status.action_map_crc32 = config_snapshot->action_map_crc32;
    s_config_gate_status.calibration_crc32 = config_snapshot->calibration_crc32;
    s_config_gate_status.config_crc32 = config_snapshot->config_crc32;

    const bool config_valid = distributed_config_validate();
    const bool refmem_claim_valid = system_manager_refmem_claim_gate_ready();
    const bool refmem_quality_valid = system_manager_refmem_quality_gate_ready();
    s_config_gate_status.run_id = s_config_gate_status.build_crc32 ^
                                  s_config_gate_status.hw_profile_crc32 ^
                                  s_config_gate_status.config_crc32 ^
                                  s_config_gate_status.epoch;
    s_config_gate_status.ready = config_valid && refmem_claim_valid && refmem_quality_valid;
    s_config_gate_status.gate_state = s_config_gate_status.ready ? 1u : 2u;
    (void)distributed_refmem_command_set_reason_table_crc32(
        config_snapshot->nack_reason_crc32);
    system_manager_publish_config_command(
        s_config_gate_status.ready,
        system_manager_select_config_nack_reason(config_valid,
                                                 refmem_claim_valid,
                                                 refmem_quality_valid),
        now_ms);
    s_initialized = true;
    return config_valid;
}

void system_manager_service(void)
{
    if (!s_initialized) {
        return;
    }

    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_config_gate_status.service_count == 0u) {
        s_config_gate_status.first_service_ms = now_ms;
    }
    s_config_gate_status.service_count++;
    s_config_gate_status.last_service_ms = now_ms;
    const bool refmem_claim_valid = system_manager_refmem_claim_gate_ready();
    const bool refmem_quality_valid = system_manager_refmem_quality_gate_ready();
    const bool gate_ready = refmem_claim_valid &&
                            refmem_quality_valid &&
                            s_config_gate_status.config_crc32 != 0u;
    s_config_gate_status.ready = gate_ready;
    s_config_gate_status.gate_state = gate_ready ? 1u : 2u;
    system_manager_publish_config_command(
        gate_ready,
        system_manager_select_config_nack_reason(s_config_gate_status.config_crc32 != 0u,
                                                 refmem_claim_valid,
                                                 refmem_quality_valid),
        now_ms);
    osal_critical_exit();
}

void system_manager_get_config_gate_status(system_manager_config_gate_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_config_gate_status;
    osal_critical_exit();
}

void system_manager_get_config_ack_status(system_manager_config_ack_status_t *status)
{
    if (status == NULL) {
        return;
    }

    const distributed_config_snapshot_t *config_snapshot =
        distributed_config_get_snapshot();
    const distributed_config_nack_reason_table_t *reason_table =
        distributed_config_get_nack_reason_table();

    refmem_command_snapshot_t command_snapshot;
    const bool command_snapshot_ok =
        distributed_refmem_get_command_snapshot(&command_snapshot) &&
        command_snapshot.command_type == REFMEM_COMMAND_TYPE_CONFIG_ACTIVATE;

    osal_critical_enter();
    status->version = config_snapshot->config_version;
    status->command_seq = command_snapshot_ok ? command_snapshot.command_seq :
                          s_config_gate_status.command_seq;
    status->target_mask = command_snapshot_ok ? command_snapshot.target_mask :
                          s_config_gate_status.target_mask;
    status->ack_flags = command_snapshot_ok ? command_snapshot.ack_flags :
                        s_config_gate_status.ack_flags;
    status->nack_flags = command_snapshot_ok ? command_snapshot.nack_flags :
                         s_config_gate_status.nack_flags;
    status->busy_flags = command_snapshot_ok ? command_snapshot.busy_flags :
                         s_config_gate_status.busy_flags;
    status->timeout_flags = command_snapshot_ok ? command_snapshot.timeout_flags :
                            s_config_gate_status.timeout_flags;
    status->last_nack_reason =
        command_snapshot_ok && command_snapshot.nack_flags != 0u ?
        (uint32_t)system_manager_config_reason_from_refmem(command_snapshot.last_reason) :
        (uint32_t)DISTRIBUTED_CONFIG_NACK_NONE;
    status->last_nack_node =
        command_snapshot_ok && command_snapshot.nack_flags != 0u ?
        command_snapshot.last_reason_slot :
        UINT32_MAX;
    status->reason_count = reason_table->reason_count;
    status->reason_table_crc32 = config_snapshot->nack_reason_crc32;
    status->config_crc32 = s_config_gate_status.config_crc32;
    osal_critical_exit();
}

void system_manager_get_mode_table(system_manager_mode_table_t *table)
{
    if (table == NULL) {
        return;
    }

    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);

    table->version = SYSTEM_MANAGER_MODE_TABLE_VERSION;
    table->mode_count = 4u;
    table->current_mode = (uint32_t)snapshot.mode;
    table->table_crc32 = system_manager_mode_table_crc32();
    memcpy(table->mode, s_system_mode_template, sizeof(s_system_mode_template));
}

void system_manager_get_resource_table(system_manager_resource_table_t *table)
{
    if (table == NULL) {
        return;
    }

    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);

    table->version = SYSTEM_MANAGER_RESOURCE_TABLE_VERSION;
    table->resource_count = 10u;
    table->current_mode = (uint32_t)snapshot.mode;
    table->active_resources = snapshot.active_resources;
    table->last_conflict_resources = snapshot.last_conflict_resources;
    table->table_crc32 = system_manager_resource_table_crc32();
    memcpy(table->resource, s_resource_template, sizeof(s_resource_template));

    for (uint32_t i = 0u; i < table->resource_count; i++) {
        system_manager_resource_entry_t *entry = &table->resource[i];
        entry->active = (snapshot.active_resources & entry->mask) != 0u ? 1u : 0u;
        entry->owner_name = snapshot.resource_owners[entry->resource_id] != NULL ?
                            snapshot.resource_owners[entry->resource_id] :
                            "-";
    }
}

void system_manager_get_fault_table(system_manager_fault_table_t *table)
{
    if (table == NULL) {
        return;
    }

    table->version = SYSTEM_MANAGER_FAULT_TABLE_VERSION;
    table->fault_count = 20u;
    table->latched = diagnostics_has_fault() ? 1u : 0u;
    table->table_crc32 = system_manager_fault_table_crc32();
    memcpy(table->fault, s_fault_code_template, sizeof(s_fault_code_template));
}
