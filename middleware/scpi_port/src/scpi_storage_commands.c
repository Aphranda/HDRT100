#include "scpi_storage_commands.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "distributed_config.h"
#include "fatfs_port.h"
#include "osal.h"
#include "project_config.h"
#include "scpi_port_internal.h"
#include "sd_card.h"
#include "storage_manager.h"

#define SCPI_STORAGE_MMEM_PAGE_LIMIT_MAX 16u
#define SCPI_STORAGE_MMEM_READ_BYTES_MAX 128u
#define SCPI_STORAGE_JOB_WAIT_LOOPS 200u

static bool scpi_storage_wait_job(uint32_t job_id);
static void scpi_storage_hex_encode(const uint8_t *data, size_t data_size, char *hex, size_t hex_size);

scpi_result_t scpi_cmd_storage_status_q(scpi_t *context)
{
    (void)storage_manager_probe();

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, storage_manager_state_string(vector.state));
    SCPI_ResultBool(context, vector.card_present ? TRUE : FALSE);
    SCPI_ResultBool(context, vector.fs_mounted ? TRUE : FALSE);
    SCPI_ResultText(context, sd_card_status_string(vector.card_status));
    SCPI_ResultUInt32(context, vector.storage_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_info_q(scpi_t *context)
{
    (void)storage_manager_probe();

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, storage_manager_state_string(vector.state));
    SCPI_ResultText(context, sd_card_type_string(vector.card_type));
    SCPI_ResultBool(context, vector.high_capacity ? TRUE : FALSE);
    SCPI_ResultUInt32(context, vector.block_count);
    SCPI_ResultUInt32(context, vector.capacity_kib);
    SCPI_ResultBool(context, vector.fatfs_available ? TRUE : FALSE);
    SCPI_ResultBool(context, vector.fs_mounted ? TRUE : FALSE);
    SCPI_ResultUInt32(context, vector.probe_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_raw_clear(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    uint32_t sector_count = 0u;
    const char *confirm = NULL;
    size_t confirm_len = 0u;
    if (SCPI_ParamUInt32(context, &sector_count, TRUE) != TRUE ||
        SCPI_ParamCharacters(context, &confirm, &confirm_len, TRUE) != TRUE ||
        confirm == NULL ||
        confirm_len != 5u ||
        strncmp(confirm, "ERASE", 5u) != 0) {
        return SCPI_RES_ERR;
    }

    uint32_t cleared_count = 0u;
    sd_card_status_t raw_status = SD_CARD_STATUS_BAD_RESPONSE;
    const bool ok = storage_manager_raw_clear_prefix(sector_count, &cleared_count, &raw_status);
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, ok ? "OK" : "ERROR");
    SCPI_ResultUInt32(context, sector_count);
    SCPI_ResultUInt32(context, cleared_count);
    SCPI_ResultText(context, sd_card_status_string(raw_status));
    SCPI_ResultUInt32(context, vector.storage_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_raw_read_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    uint32_t sector = 0u;
    if (SCPI_ParamUInt32(context, &sector, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    uint8_t data[512];
    sd_card_status_t raw_status = SD_CARD_STATUS_BAD_RESPONSE;
    const bool ok = storage_manager_raw_read_sector(sector, data, sizeof(data), &raw_status);
    char hex[129];
    if (ok) {
        for (size_t i = 0u; i < 64u; i++) {
            (void)snprintf(hex + (i * 2u), sizeof(hex) - (i * 2u), "%02X", data[i]);
        }
    } else {
        hex[0] = '\0';
    }

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, ok ? "OK" : "ERROR");
    SCPI_ResultUInt32(context, sector);
    SCPI_ResultText(context, sd_card_status_string(raw_status));
    SCPI_ResultUInt32(context, vector.storage_error);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_mkfs(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    const char *confirm = NULL;
    size_t confirm_len = 0u;
    if (SCPI_ParamCharacters(context, &confirm, &confirm_len, TRUE) != TRUE ||
        confirm == NULL ||
        confirm_len != 5u ||
        strncmp(confirm, "ERASE", 5u) != 0) {
        return SCPI_RES_ERR;
    }

    fatfs_port_status_t format_status = FATFS_PORT_STATUS_FORMAT_FAILED;
    const bool ok = storage_manager_format_volume(&format_status);
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, ok ? "OK" : "ERROR");
    SCPI_ResultText(context, fatfs_port_status_string(format_status));
    SCPI_ResultText(context, storage_manager_state_string(vector.state));
    SCPI_ResultUInt32(context, vector.storage_error);
    SCPI_ResultUInt32(context, vector.block_count);
    SCPI_ResultUInt32(context, vector.capacity_kib);
    SCPI_ResultUInt32(context, fatfs_port_last_mkfs_result());
    SCPI_ResultUInt32(context, fatfs_port_last_mount_result());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_init(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_system_init_job(&job_id)) {
        return SCPI_RES_ERR;
    }
    (void)scpi_storage_wait_job(job_id);

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    if (job.id != job_id ||
        job.state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
        job.state == STORAGE_MANAGER_JOB_STATE_RUNNING) {
        return SCPI_RES_ERR;
    }

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, job.state == STORAGE_MANAGER_JOB_STATE_DONE ? "OK" : "ERROR");
    SCPI_ResultText(context, storage_manager_manifest_status_string(vector.manifest_status));
    SCPI_ResultUInt32(context, vector.manifest_schema);
    SCPI_ResultText(context, vector.manifest_build_id);
    SCPI_ResultUInt32(context, vector.manifest_required_count);
    SCPI_ResultUInt32(context, vector.manifest_missing_count);
    SCPI_ResultUInt32(context, job.error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_manifest_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_manifest_scan_job(&job_id)) {
        return SCPI_RES_ERR;
    }
    (void)scpi_storage_wait_job(job_id);

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    if (job.id != job_id ||
        job.state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
        job.state == STORAGE_MANAGER_JOB_STATE_RUNNING) {
        return SCPI_RES_ERR;
    }

    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    SCPI_ResultText(context, storage_manager_manifest_status_string(vector.manifest_status));
    SCPI_ResultUInt32(context, vector.manifest_schema);
    SCPI_ResultText(context, vector.manifest_product_id);
    SCPI_ResultText(context, vector.manifest_hardware_id);
    SCPI_ResultText(context, vector.manifest_build_id);
    SCPI_ResultUInt32(context, vector.manifest_required_count);
    SCPI_ResultUInt32(context, vector.manifest_missing_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_job_info(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    if (SCPI_ParamCharacters(context, &path, &path_len, TRUE) != TRUE ||
        path == NULL ||
        path_len == 0u ||
        path_len >= 96u) {
        return SCPI_RES_ERR;
    }

    char path_buffer[96];
    memcpy(path_buffer, path, path_len);
    path_buffer[path_len] = '\0';

    uint32_t job_id = 0u;
    if (!storage_manager_post_file_info_job(path_buffer, &job_id)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, job_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_job_q(scpi_t *context)
{
    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    const char *kind = job.is_dir ? "DIR" : "FILE";
    if (job.type == STORAGE_MANAGER_JOB_TYPE_FILE_READ) {
        kind = "READ";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE) {
        kind = "CATALOG";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_SNAPSHOT_WRITE) {
        kind = "SNAPSHOT";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_MANIFEST_SCAN) {
        kind = "MANIFEST";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_FAULT_EVIDENCE) {
        kind = "FAULT_EVIDENCE";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_SYSTEM_INIT) {
        kind = "MANIFEST";
    }

    SCPI_ResultText(context, storage_manager_job_state_string(job.state));
    SCPI_ResultUInt32(context, job.id);
    SCPI_ResultText(context, storage_manager_job_type_string(job.type));
    SCPI_ResultText(context, job.path);
    SCPI_ResultUInt32(context, job.size);
    SCPI_ResultText(context, kind);
    SCPI_ResultUInt32(context, job.path_hash);
    SCPI_ResultUInt32(context, job.error);
    return SCPI_RES_OK;
}

static bool scpi_storage_wait_job(uint32_t job_id)
{
    for (uint32_t i = 0u; i < SCPI_STORAGE_JOB_WAIT_LOOPS; i++) {
#if PROJECT_USE_FREERTOS
        osal_task_delay_ms(1u);
#else
        storage_manager_service(250u);
#endif
        storage_manager_job_result_t job;
        storage_manager_get_job_result(&job);
        if (job.id != job_id) {
            return false;
        }
        if (job.state == STORAGE_MANAGER_JOB_STATE_DONE) {
            return true;
        }
        if (job.state == STORAGE_MANAGER_JOB_STATE_FAILED) {
            return false;
        }
    }
    return false;
}

scpi_result_t scpi_cmd_snapshot_write(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    const char *kind = NULL;
    size_t kind_len = 0u;
    (void)SCPI_ParamCharacters(context, &kind, &kind_len, FALSE);

    char kind_buffer[16];
    if (kind != NULL && kind_len > 0u) {
        if (kind_len >= sizeof(kind_buffer)) {
            return SCPI_RES_ERR;
        }
        memcpy(kind_buffer, kind, kind_len);
        kind_buffer[kind_len] = '\0';
    } else {
        (void)snprintf(kind_buffer, sizeof(kind_buffer), "boot");
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_snapshot_job(kind_buffer, &job_id)) {
        return SCPI_RES_ERR;
    }
    if (!scpi_storage_wait_job(job_id)) {
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_snapshot_last_q(scpi_t *context)
{
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, vector.last_snapshot_error == 0u ? "OK" : "ERROR");
    SCPI_ResultText(context, vector.last_snapshot_kind);
    SCPI_ResultUInt32(context, vector.last_snapshot_id);
    SCPI_ResultText(context, vector.last_snapshot_path);
    SCPI_ResultUInt32(context, vector.last_snapshot_path_hash);
    SCPI_ResultUInt32(context, vector.last_snapshot_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trace_last_q(scpi_t *context)
{
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, vector.last_trace_error == 0u ? "OK" : "ERROR");
    SCPI_ResultText(context, vector.last_trace_kind);
    SCPI_ResultUInt32(context, vector.last_trace_id);
    SCPI_ResultText(context, vector.last_trace_path);
    SCPI_ResultUInt32(context, vector.last_trace_path_hash);
    SCPI_ResultUInt32(context, vector.last_trace_event_count);
    SCPI_ResultUInt32(context, vector.last_trace_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_fault_last_q(scpi_t *context)
{
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, vector.last_fault_report_error == 0u ? "OK" : "ERROR");
    SCPI_ResultUInt32(context, vector.last_fault_report_id);
    SCPI_ResultText(context, vector.last_fault_report_path);
    SCPI_ResultUInt32(context, vector.last_fault_report_path_hash);
    SCPI_ResultUInt32(context, vector.last_fault_snapshot_id);
    SCPI_ResultUInt32(context, vector.last_fault_trace_id);
    SCPI_ResultUInt32(context, vector.last_fault_report_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_mmem_catalog_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    (void)SCPI_ParamCharacters(context, &path, &path_len, FALSE);
    char path_buffer[96];
    if (path != NULL && path_len > 0u) {
        if (path_len >= sizeof(path_buffer)) {
            return SCPI_RES_ERR;
        }
        memcpy(path_buffer, path, path_len);
        path_buffer[path_len] = '\0';
    } else {
        (void)snprintf(path_buffer, sizeof(path_buffer), "/");
    }

    char catalog[384];
    storage_manager_catalog_page_t page;
    uint32_t job_id = 0u;
    bool ok = storage_manager_post_catalog_page_job(path_buffer,
                                                    0u,
                                                    SCPI_STORAGE_MMEM_PAGE_LIMIT_MAX,
                                                    &job_id);
    if (ok) {
        ok = scpi_storage_wait_job(job_id);
    }
    if (ok) {
        ok = storage_manager_get_catalog_page_job_result(job_id,
                                                         &page,
                                                         catalog,
                                                         sizeof(catalog));
    } else {
        memset(&page, 0, sizeof(page));
        catalog[0] = '\0';
    }
    if (ok && !page.complete) {
        const size_t catalog_len = strlen(catalog);
        if (catalog_len > 0u && catalog[catalog_len - 1u] == ';') {
            catalog[catalog_len - 1u] = '\0';
        }
    }
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    if (!ok && vector.state == STORAGE_MANAGER_STATE_PATH_DENIED) {
        (void)snprintf(catalog, sizeof(catalog), "PATH_DENIED");
    }
    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultText(context, catalog);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_mmem_info_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    if (SCPI_ParamCharacters(context, &path, &path_len, TRUE) != TRUE ||
        path == NULL ||
        path_len == 0u ||
        path_len >= 96u) {
        return SCPI_RES_ERR;
    }

    char path_buffer[96];
    memcpy(path_buffer, path, path_len);
    path_buffer[path_len] = '\0';

    uint32_t job_id = 0u;
    bool ok = storage_manager_post_file_info_job(path_buffer, &job_id);
    if (ok) {
        ok = scpi_storage_wait_job(job_id);
    }

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultText(context, ok ? job.path : path_buffer);
    SCPI_ResultUInt32(context, ok ? job.size : 0u);
    SCPI_ResultText(context, ok ? (job.is_dir ? "DIR" : "FILE") : "UNKNOWN");
    SCPI_ResultUInt32(context, ok ? job.path_hash : 0u);
    SCPI_ResultUInt32(context, vector.storage_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_mmem_catalog_page_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    uint32_t offset = 0u;
    uint32_t limit = 0u;
    if (SCPI_ParamCharacters(context, &path, &path_len, TRUE) != TRUE ||
        path == NULL ||
        path_len == 0u ||
        path_len >= 96u ||
        SCPI_ParamUInt32(context, &offset, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &limit, TRUE) != TRUE ||
        limit == 0u) {
        return SCPI_RES_ERR;
    }
    if (limit > SCPI_STORAGE_MMEM_PAGE_LIMIT_MAX) {
        limit = SCPI_STORAGE_MMEM_PAGE_LIMIT_MAX;
    }

    char path_buffer[96];
    memcpy(path_buffer, path, path_len);
    path_buffer[path_len] = '\0';

    char catalog[384];
    storage_manager_catalog_page_t page;
    uint32_t job_id = 0u;
    bool ok = storage_manager_post_catalog_page_job(path_buffer,
                                                    offset,
                                                    limit,
                                                    &job_id);
    if (ok) {
        ok = scpi_storage_wait_job(job_id);
    }
    if (ok) {
        ok = storage_manager_get_catalog_page_job_result(job_id,
                                                         &page,
                                                         catalog,
                                                         sizeof(catalog));
    } else {
        memset(&page, 0, sizeof(page));
        catalog[0] = '\0';
    }
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    if (!ok && vector.state == STORAGE_MANAGER_STATE_PATH_DENIED) {
        (void)snprintf(catalog, sizeof(catalog), "PATH_DENIED");
    }

    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultText(context, ok ? page.path : path_buffer);
    SCPI_ResultUInt32(context, ok ? offset : 0u);
    SCPI_ResultUInt32(context, ok ? page.returned_count : 0u);
    SCPI_ResultUInt32(context, ok ? page.next_offset : 0u);
    SCPI_ResultBool(context, ok && page.complete ? TRUE : FALSE);
    SCPI_ResultBool(context, ok && page.truncated ? TRUE : FALSE);
    SCPI_ResultText(context, catalog);
    return SCPI_RES_OK;
}

static void scpi_storage_hex_encode(const uint8_t *data, size_t data_size, char *hex, size_t hex_size)
{
    static const char digits[] = "0123456789ABCDEF";
    if (hex == NULL || hex_size == 0u) {
        return;
    }
    if (data == NULL || hex_size < (data_size * 2u) + 1u) {
        hex[0] = '\0';
        return;
    }
    for (size_t i = 0u; i < data_size; i++) {
        hex[i * 2u] = digits[(data[i] >> 4u) & 0x0Fu];
        hex[(i * 2u) + 1u] = digits[data[i] & 0x0Fu];
    }
    hex[data_size * 2u] = '\0';
}

scpi_result_t scpi_cmd_mmem_read_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    const char *path = NULL;
    size_t path_len = 0u;
    uint32_t offset = 0u;
    uint32_t length = 0u;
    if (SCPI_ParamCharacters(context, &path, &path_len, TRUE) != TRUE ||
        path == NULL ||
        path_len == 0u ||
        path_len >= 96u ||
        SCPI_ParamUInt32(context, &offset, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &length, TRUE) != TRUE ||
        length == 0u) {
        return SCPI_RES_ERR;
    }
    if (length > SCPI_STORAGE_MMEM_READ_BYTES_MAX) {
        length = SCPI_STORAGE_MMEM_READ_BYTES_MAX;
    }

    char path_buffer[96];
    memcpy(path_buffer, path, path_len);
    path_buffer[path_len] = '\0';

    uint8_t data[SCPI_STORAGE_MMEM_READ_BYTES_MAX];
    char hex[(SCPI_STORAGE_MMEM_READ_BYTES_MAX * 2u) + 1u];
    storage_manager_file_read_t read_info;
    uint32_t job_id = 0u;
    bool ok = storage_manager_post_file_read_job(path_buffer, offset, length, &job_id);
    if (ok) {
        ok = scpi_storage_wait_job(job_id);
    }
    if (ok) {
        ok = storage_manager_get_file_read_job_result(job_id,
                                                      &read_info,
                                                      data,
                                                      sizeof(data));
    } else {
        memset(&read_info, 0, sizeof(read_info));
    }
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    const uint32_t returned = ok ? read_info.returned : 0u;
    scpi_storage_hex_encode(data, returned, hex, sizeof(hex));

    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultText(context, ok ? read_info.path : path_buffer);
    SCPI_ResultUInt32(context, ok ? read_info.offset : 0u);
    SCPI_ResultUInt32(context, length);
    SCPI_ResultUInt32(context, returned);
    SCPI_ResultBool(context, ok && read_info.eof ? TRUE : FALSE);
    SCPI_ResultUInt32(context, ok ? read_info.path_hash : 0u);
    SCPI_ResultUInt32(context, vector.storage_error);
    SCPI_ResultText(context, ok ? hex : "");
    return SCPI_RES_OK;
}
