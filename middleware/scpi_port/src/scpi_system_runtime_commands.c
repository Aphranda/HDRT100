#include "scpi_system_runtime_commands.h"

#include <stdint.h>

#include "diagnostics.h"
#include "board_identity.h"
#include "led_manager.h"
#include "osal.h"
#include "ota_ao.h"
#include "project_build_info.h"
#include "project_config.h"
#include "product_config.h"
#include "scpi_port_internal.h"
#include "storage_manager.h"
#include "ui_manager.h"

scpi_result_t scpi_cmd_core_tst_q(scpi_t *context)
{
    SCPI_ResultInt32(context, 0);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_firmware_version_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, PROJECT_VERSION_MAJOR);
    SCPI_ResultUInt32(context, PROJECT_VERSION_MINOR);
    SCPI_ResultUInt32(context, PROJECT_VERSION_PATCH);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_firmware_build_q(scpi_t *context)
{
    SCPI_ResultText(context, g_project_build_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_bootloader_version_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, PROJECT_BOOTLOADER_VERSION_MAJOR);
    SCPI_ResultUInt32(context, PROJECT_BOOTLOADER_VERSION_MINOR);
    SCPI_ResultUInt32(context, PROJECT_BOOTLOADER_VERSION_PATCH);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_bootloader_capability_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.boot_capabilities);
    return SCPI_RES_OK;
}

static const char *scpi_diag_level_to_string(diag_level_t level)
{
    switch (level) {
    case DIAG_LEVEL_DEBUG: return "DEBUG";
    case DIAG_LEVEL_INFO:  return "INFO";
    case DIAG_LEVEL_WARN:  return "WARN";
    case DIAG_LEVEL_ERROR: return "ERROR";
    default:               return "UNKNOWN";
    }
}

scpi_result_t scpi_cmd_log_level(scpi_t *context)
{
    uint32_t level;
    if (!scpi_port_read_u32(context, &level) ||
        level >= (uint32_t)DIAG_LEVEL_COUNT ||
        !diagnostics_set_min_level((diag_level_t)level)) {
        return SCPI_RES_ERR;
    }

    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_log_level_q(scpi_t *context)
{
    const diag_level_t level = diagnostics_get_min_level();
    SCPI_ResultText(context, scpi_diag_level_to_string(level));
    SCPI_ResultUInt32(context, (uint32_t)level);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_log_status_q(scpi_t *context)
{
    diagnostics_status_t status;
    diagnostics_get_status(&status);

    SCPI_ResultText(context, scpi_diag_level_to_string(status.min_level));
    SCPI_ResultUInt32(context, (uint32_t)status.min_level);
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        SCPI_ResultUInt32(context, status.emitted_count[i]);
    }
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        SCPI_ResultUInt32(context, status.dropped_count[i]);
    }
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        SCPI_ResultUInt32(context, status.truncated_count[i]);
    }
    for (uint32_t i = 0u; i < (uint32_t)DIAG_LEVEL_COUNT; i++) {
        SCPI_ResultUInt32(context, status.emit_failed_count[i]);
    }
    SCPI_ResultUInt32(context, status.queue_dropped_count);
    SCPI_ResultUInt32(context, status.queue_bytes);
    SCPI_ResultUInt32(context, status.queue_high_watermark);
    SCPI_ResultUInt32(context, status.persistent_queue_dropped_count);
    SCPI_ResultUInt32(context, status.persistent_queue_dropped_bytes);
    SCPI_ResultUInt32(context, status.persistent_queue_bytes);
    SCPI_ResultUInt32(context, status.persistent_queue_high_watermark);

    storage_manager_vector_t storage;
    storage_manager_get_vector(&storage);
    SCPI_ResultUInt32(context, storage.last_log_id);
    SCPI_ResultUInt32(context, storage.last_log_bytes);
    SCPI_ResultUInt32(context, storage.last_log_path_hash);
    SCPI_ResultUInt32(context, storage.last_log_error);
    SCPI_ResultUInt32(context, storage.log_segment_count);
    SCPI_ResultUInt32(context, storage.log_flushed_bytes);
    SCPI_ResultUInt32(context, storage.log_attempt_error);
    SCPI_ResultText(context, storage.last_log_path);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_core_status_q(scpi_t *context)
{
    diagnostics_core_status_t status;
    diagnostics_get_core_status(&status);

    SCPI_ResultBool(context, status.core1_enabled ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.core0_loop_count);
    SCPI_ResultUInt32(context, status.core1_loop_count);
    SCPI_ResultUInt32(context, status.core0_last_ms);
    SCPI_ResultUInt32(context, status.core1_last_ms);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_diagnostic_sensors_q(scpi_t *context)
{
    diagnostics_sensor_status_t status;
    diagnostics_get_sensor_status(&status);

    SCPI_ResultUInt32(context, status.version);
    SCPI_ResultUInt32(context, status.valid_mask);
    SCPI_ResultUInt32(context, status.flags);
    SCPI_ResultUInt32(context, status.sample_count);
    SCPI_ResultUInt32(context, status.sample_time_ms);
    SCPI_ResultUInt32(context, status.adc_reference_uv);
    SCPI_ResultUInt32(context, status.board_temp_raw);
    SCPI_ResultUInt32(context, status.board_temp_uv);
    SCPI_ResultInt32(context, status.board_temp_mdeg_c);
    SCPI_ResultUInt32(context, status.chip_temp_raw);
    SCPI_ResultUInt32(context, status.chip_temp_uv);
    SCPI_ResultInt32(context, status.chip_temp_mdeg_c);
    SCPI_ResultUInt32(context, status.current_output_raw);
    SCPI_ResultUInt32(context, status.current_output_uv);
    SCPI_ResultInt32(context, status.current_nominal_ma);
    SCPI_ResultBool(context, status.current_calibrated ? TRUE : FALSE);
    SCPI_ResultInt32(context, status.current_zero_uv);
    SCPI_ResultUInt32(context, status.current_gain_milli);
    SCPI_ResultUInt32(context, status.current_shunt_uohm);
    SCPI_ResultBool(context, status.current_frontend_healthy ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.current_output_plausible_min_uv);
    SCPI_ResultUInt32(context, status.current_output_plausible_max_uv);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_rtos_status_q(scpi_t *context)
{
    uint32_t heap_free = 0u;
    uint32_t heap_min_free = 0u;
    osal_task_stats_t task_stats[16];
    const uint32_t task_count =
        osal_task_get_stats(task_stats,
                            (uint32_t)(sizeof(task_stats) / sizeof(task_stats[0])));

    osal_heap_get_status(&heap_free, &heap_min_free);
    SCPI_ResultUInt32(context, heap_free);
    SCPI_ResultUInt32(context, heap_min_free);
    SCPI_ResultUInt32(context, task_count);

    for (uint32_t i = 0u; i < task_count; i++) {
        const osal_task_stats_t *task = &task_stats[i];
        const uint32_t used_words = task->stack_words > task->stack_free_words ?
                                    task->stack_words - task->stack_free_words :
                                    0u;

        SCPI_ResultText(context, task->name != NULL ? task->name : "-");
        SCPI_ResultUInt32(context, task->stack_words);
        SCPI_ResultUInt32(context, task->stack_free_words);
        SCPI_ResultUInt32(context, used_words);
        SCPI_ResultUInt32(context, task->priority);
    }

    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ui_keys_q(scpi_t *context)
{
    ui_manager_key_status_t status;
    ui_manager_get_key_status(&status);

    SCPI_ResultUInt32(context, status.raw_mask);
    SCPI_ResultUInt32(context, status.stable_mask);
    SCPI_ResultUInt32(context, status.event_sequence);
    SCPI_ResultUInt32(context, status.last_event_key);
    SCPI_ResultUInt32(context, (uint32_t)status.last_event_type);
    for (uint32_t key = 0u; key < UI_MANAGER_KEY_COUNT; key++) {
        SCPI_ResultUInt32(context, status.short_count[key]);
        SCPI_ResultUInt32(context, status.long_count[key]);
        SCPI_ResultUInt32(context, status.repeat_count[key]);
    }

    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_led_status_q(scpi_t *context)
{
    led_manager_status_t status;
    led_manager_get_status(&status);

    SCPI_ResultText(context, led_manager_policy_string(status.policy));
    SCPI_ResultUInt32(context, (uint32_t)status.policy);
    SCPI_ResultText(context, led_manager_pattern_string(status.system_pattern));
    SCPI_ResultUInt32(context, (uint32_t)status.system_pattern);
    SCPI_ResultBool(context, status.system_level ? TRUE : FALSE);
    SCPI_ResultText(context, led_manager_pattern_string(status.arm_pattern));
    SCPI_ResultUInt32(context, (uint32_t)status.arm_pattern);
    SCPI_ResultBool(context, status.arm_level ? TRUE : FALSE);
    SCPI_ResultText(context, led_manager_pattern_string(status.fault_pattern));
    SCPI_ResultUInt32(context, (uint32_t)status.fault_pattern);
    SCPI_ResultBool(context, status.fault_level ? TRUE : FALSE);
    SCPI_ResultBool(context, status.fault_latched ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.trigger_state);
    SCPI_ResultUInt32(context, status.ota_state);
    SCPI_ResultBool(context, status.config_ready ? TRUE : FALSE);
    SCPI_ResultBool(context, status.sd_ready ? TRUE : FALSE);
    SCPI_ResultBool(context, status.core1_stale ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.health_flags);
    SCPI_ResultUInt32(context, status.event_sequence);
    SCPI_ResultUInt32(context, status.trigger_pulse_count);
    SCPI_ResultUInt32(context, status.fault_transition_count);
    SCPI_ResultUInt32(context, status.pattern_transition_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_watchdog_status_q(scpi_t *context)
{
    diagnostics_watchdog_status_t status;
    diagnostics_get_watchdog_status(&status);

    SCPI_ResultBool(context, status.enabled ? TRUE : FALSE);
    SCPI_ResultBool(context, status.last_reset_watchdog ? TRUE : FALSE);
    SCPI_ResultBool(context, status.last_reset_timeout ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.timeout_ms);
    SCPI_ResultUInt32(context, status.reset_reason);
    SCPI_ResultUInt32(context, status.evidence_magic);
    SCPI_ResultUInt32(context, status.evidence_expected_mask);
    SCPI_ResultUInt32(context, status.evidence_seen_mask);
    SCPI_ResultUInt32(context, status.evidence_stale_mask);
    SCPI_ResultUInt32(context, status.evidence_core0_loop_count);
    SCPI_ResultUInt32(context, status.evidence_core1_loop_count);
    SCPI_ResultUInt32(context, status.evidence_core0_progress);
    SCPI_ResultUInt32(context, status.evidence_core1_progress);
    SCPI_ResultUInt32(context, status.gate_required_mask);
    SCPI_ResultUInt32(context, status.last_seen_mask);
    SCPI_ResultUInt32(context, status.last_stale_mask);
    SCPI_ResultUInt32(context, status.supervisor_count);
    return SCPI_RES_OK;
}

static const char *scpi_watchdog_reset_type(const diagnostics_watchdog_status_t *status)
{
    if (!status->last_reset_watchdog) {
        return "POWER_OR_EXTERNAL";
    }
    return status->last_reset_timeout ? "WATCHDOG_TIMEOUT" : "SOFTWARE_REBOOT";
}

static const char *scpi_watchdog_task_name(uint32_t mask)
{
    static const char *const names[DIAGNOSTICS_WATCHDOG_TASK_COUNT] = {
        "SYSTEM", "USB_DEVICE", "SCPI", "REFMEM_SYNC", "LOOP_ENGINE",
        "CALIBRATION", "CONFIG_GATE", "OTA", "STORAGE", "UI", "CORE1",
    };

    if (mask == 0u) {
        return "NONE";
    }
    if ((mask & (mask - 1u)) != 0u) {
        return "MULTIPLE";
    }
    for (uint32_t i = 0u; i < (uint32_t)DIAGNOSTICS_WATCHDOG_TASK_COUNT; i++) {
        if (mask == (1u << i)) {
            return names[i];
        }
    }
    return "UNKNOWN";
}

static const char *scpi_watchdog_cause(const diagnostics_watchdog_status_t *status)
{
    const uint32_t critical_stale = status->evidence_stale_mask &
        ((1u << DIAGNOSTICS_WATCHDOG_TASK_SYSTEM) |
         (1u << DIAGNOSTICS_WATCHDOG_TASK_CORE1));

    if (!status->last_reset_timeout) {
        return "NONE";
    }
    if (status->evidence_magic != DIAGNOSTICS_WATCHDOG_EVIDENCE_MAGIC) {
        return "EVIDENCE_MISSING";
    }
    if (critical_stale == 0u) {
        return "CORE0_SUPERVISOR_STALL";
    }
    if (critical_stale ==
        (1u << DIAGNOSTICS_WATCHDOG_TASK_CORE1)) {
        return "CORE1_STALL";
    }
    return "TASK_STALL";
}

scpi_result_t scpi_cmd_watchdog_log_q(scpi_t *context)
{
    diagnostics_watchdog_status_t status;
    diagnostics_get_watchdog_status(&status);
    const uint32_t critical_stale = status.evidence_stale_mask &
        ((1u << DIAGNOSTICS_WATCHDOG_TASK_SYSTEM) |
         (1u << DIAGNOSTICS_WATCHDOG_TASK_CORE1));
    const bool evidence_valid = status.last_reset_timeout &&
        status.evidence_magic == DIAGNOSTICS_WATCHDOG_EVIDENCE_MAGIC;

    SCPI_ResultText(context, scpi_watchdog_reset_type(&status));
    SCPI_ResultText(context, scpi_watchdog_cause(&status));
    SCPI_ResultText(context, scpi_watchdog_task_name(critical_stale));
    SCPI_ResultUInt32(context, status.reset_reason);
    SCPI_ResultBool(context, evidence_valid ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.evidence_expected_mask);
    SCPI_ResultUInt32(context, status.evidence_seen_mask);
    SCPI_ResultUInt32(context, status.evidence_stale_mask);
    SCPI_ResultUInt32(context, status.evidence_core0_loop_count);
    SCPI_ResultUInt32(context, status.evidence_core1_loop_count);
    SCPI_ResultUInt32(context, status.evidence_core0_progress);
    SCPI_ResultUInt32(context, status.evidence_core1_progress);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_board_id_q(scpi_t *context)
{
    SCPI_ResultText(context, board_identity_serial());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_board_no(scpi_t *context)
{
    uint32_t logical_no = 0u;
    if (!scpi_port_read_u32(context, &logical_no) ||
        !product_config_set_board_no(logical_no) ||
        !board_identity_set_no(logical_no)) {
        scpi_port_push_exec_error(context, "BOARD_NO");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, logical_no);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_board_no_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, board_identity_get_no());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_board_map_q(scpi_t *context)
{
    /* Stable machine-readable binding: unique address first, logical NO
     * second.  The address is the only board identity; USB COM numbers are
     * intentionally absent from this interface. */
    SCPI_ResultText(context, board_identity_serial());
    SCPI_ResultUInt32(context, board_identity_get_no());
    return SCPI_RES_OK;
}

#if PROJECT_ENABLE_WATCHDOG_TEST
scpi_result_t scpi_cmd_watchdog_test(scpi_t *context)
{
    diagnostics_watchdog_request_test_stall();
    return scpi_port_result_ok(context);
}
#endif
