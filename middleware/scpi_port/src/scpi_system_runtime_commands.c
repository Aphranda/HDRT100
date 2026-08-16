#include "scpi_system_runtime_commands.h"

#include <stdint.h>

#include "diagnostics.h"
#include "osal.h"
#include "ota_ao.h"
#include "project_build_info.h"
#include "project_config.h"
#include "scpi_port_internal.h"
#include "storage_manager.h"

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
