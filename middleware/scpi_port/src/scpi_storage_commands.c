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
#define SCPI_STORAGE_WRITE_BYTES_MAX 256u
#define SCPI_STORAGE_JOB_WAIT_LOOPS 10000u

typedef enum {
    SCPI_STORAGE_WRITE_ERR_NONE = 0u,
    SCPI_STORAGE_WRITE_ERR_NOT_ACTIVE = 1u,
    SCPI_STORAGE_WRITE_ERR_BAD_SIZE = 2u,
    SCPI_STORAGE_WRITE_ERR_BAD_OFFSET = 3u,
    SCPI_STORAGE_WRITE_ERR_BAD_HEX = 4u,
    SCPI_STORAGE_WRITE_ERR_INCOMPLETE = 5u,
    SCPI_STORAGE_WRITE_ERR_CRC = 6u,
    SCPI_STORAGE_WRITE_ERR_STORAGE = 7u,
} scpi_storage_write_error_t;

typedef struct {
    uint32_t txn_id;
    uint32_t job_id;
    uint32_t last_error;
} scpi_storage_write_state_t;

static scpi_storage_write_state_t s_storage_write_state;

static bool scpi_storage_wait_job(uint32_t job_id);
static void scpi_storage_hex_encode(const uint8_t *data, size_t data_size, char *hex, size_t hex_size);
static bool scpi_storage_hex_decode(const char *hex,
                                    size_t hex_len,
                                    uint8_t *output,
                                    size_t output_size,
                                    size_t *decoded_size);
static void scpi_storage_result_write_status(scpi_t *context);
static bool scpi_storage_read_path_param(scpi_t *context, char *path, size_t path_size);

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
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_FILE_WRITE ||
               job.type == STORAGE_MANAGER_JOB_TYPE_FILE_DELETE ||
               job.type == STORAGE_MANAGER_JOB_TYPE_FILE_RENAME) {
        kind = "FILE";
    } else if (job.type == STORAGE_MANAGER_JOB_TYPE_DIRECTORY_CREATE ||
               job.type == STORAGE_MANAGER_JOB_TYPE_DIRECTORY_DELETE ||
               job.type == STORAGE_MANAGER_JOB_TYPE_DIRECTORY_RENAME) {
        kind = "DIR";
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

scpi_result_t scpi_cmd_storage_file_write_begin(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    char path[96];
    uint32_t size = 0u;
    uint32_t crc32 = 0u;
    if (!scpi_storage_read_path_param(context, path, sizeof(path)) ||
        !scpi_port_read_u32(context, &size) ||
        !scpi_port_read_u32(context, &crc32)) {
        return SCPI_RES_ERR;
    }

    uint32_t txn_id = 0u;
    if (!storage_manager_begin_file_write(path, size, crc32, &txn_id)) {
        s_storage_write_state.last_error = SCPI_STORAGE_WRITE_ERR_BAD_SIZE;
        scpi_port_push_exec_error(context, "STORAGE_WRITE_BEGIN");
        return SCPI_RES_ERR;
    }

    memset(&s_storage_write_state, 0, sizeof(s_storage_write_state));
    s_storage_write_state.txn_id = txn_id;
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, txn_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_file_write_data(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    uint32_t txn_id = 0u;
    uint32_t offset = 0u;
    const char *hex = NULL;
    size_t hex_len = 0u;
    if (!scpi_port_read_u32(context, &txn_id) ||
        !scpi_port_read_u32(context, &offset) ||
        SCPI_ParamCharacters(context, &hex, &hex_len, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    uint8_t chunk[SCPI_STORAGE_WRITE_BYTES_MAX];
    size_t decoded_size = 0u;
    if (!scpi_storage_hex_decode(hex,
                                 hex_len,
                                 chunk,
                                 sizeof(chunk),
                                 &decoded_size)) {
        s_storage_write_state.last_error = SCPI_STORAGE_WRITE_ERR_BAD_HEX;
        scpi_port_push_exec_error(context, "STORAGE_WRITE_HEX");
        return SCPI_RES_ERR;
    }

    if (!storage_manager_write_file_chunk(txn_id, offset, chunk, decoded_size)) {
        storage_manager_write_snapshot_t snapshot;
        storage_manager_get_write_snapshot(&snapshot);
        s_storage_write_state.last_error =
            snapshot.error == 0u ? SCPI_STORAGE_WRITE_ERR_BAD_OFFSET : snapshot.error;
        scpi_port_push_exec_error(context, "STORAGE_WRITE_DATA");
        return SCPI_RES_ERR;
    }

    storage_manager_write_snapshot_t snapshot;
    storage_manager_get_write_snapshot(&snapshot);
    s_storage_write_state.txn_id = txn_id;
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, snapshot.txn_id);
    SCPI_ResultUInt32(context, snapshot.received_size);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_file_write_end(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    uint32_t txn_id = 0u;
    if (!scpi_port_read_u32(context, &txn_id)) {
        return SCPI_RES_ERR;
    }

    storage_manager_write_snapshot_t snapshot;
    storage_manager_get_write_snapshot(&snapshot);
    if (snapshot.txn_id != txn_id || snapshot.state == STORAGE_MANAGER_WRITE_STATE_IDLE) {
        s_storage_write_state.last_error = SCPI_STORAGE_WRITE_ERR_NOT_ACTIVE;
        scpi_port_push_exec_error(context, "STORAGE_WRITE_NOT_ACTIVE");
        return SCPI_RES_ERR;
    }
    if (snapshot.received_size != snapshot.expected_size ||
        snapshot.state != STORAGE_MANAGER_WRITE_STATE_READY) {
        s_storage_write_state.last_error = SCPI_STORAGE_WRITE_ERR_INCOMPLETE;
        scpi_port_push_exec_error(context, "STORAGE_WRITE_INCOMPLETE");
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    if (!storage_manager_commit_file_write(txn_id, &job_id)) {
        storage_manager_get_write_snapshot(&snapshot);
        s_storage_write_state.last_error =
            snapshot.error == 0u ? SCPI_STORAGE_WRITE_ERR_CRC : snapshot.error;
        scpi_port_push_exec_error(context, "STORAGE_WRITE_COMMIT");
        return SCPI_RES_ERR;
    }

    s_storage_write_state.txn_id = txn_id;
    s_storage_write_state.job_id = job_id;
    if (!scpi_storage_wait_job(job_id)) {
        s_storage_write_state.last_error = SCPI_STORAGE_WRITE_ERR_STORAGE;
        scpi_port_push_exec_error(context, "STORAGE_WRITE_JOB");
        return SCPI_RES_ERR;
    }

    s_storage_write_state.last_error = SCPI_STORAGE_WRITE_ERR_NONE;
    SCPI_ResultText(context, "WRITTEN");
    scpi_storage_result_write_status(context);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_file_write_abort(scpi_t *context)
{
    uint32_t txn_id = s_storage_write_state.txn_id;
    (void)SCPI_ParamUInt32(context, &txn_id, FALSE);
    if (txn_id != 0u) {
        (void)storage_manager_abort_file_write(txn_id);
    }
    memset(&s_storage_write_state, 0, sizeof(s_storage_write_state));
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_storage_file_write_status_q(scpi_t *context)
{
    scpi_storage_result_write_status(context);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_file_info_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    char path[96];
    if (!scpi_storage_read_path_param(context, path, sizeof(path))) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    bool ok = storage_manager_post_file_info_job(path, &job_id);
    if (ok) {
        ok = scpi_storage_wait_job(job_id);
    }
    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);

    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultUInt32(context, job.id);
    SCPI_ResultText(context, storage_manager_job_state_string(job.state));
    SCPI_ResultText(context, ok ? (job.is_dir ? "DIR" : "FILE") : "UNKNOWN");
    SCPI_ResultUInt32(context, ok ? job.size : 0u);
    SCPI_ResultUInt32(context, ok ? job.path_hash : 0u);
    SCPI_ResultUInt32(context, vector.storage_error);
    SCPI_ResultText(context, ok ? job.path : path);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_file_read_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    char path[96];
    uint32_t offset = 0u;
    uint32_t length = SCPI_STORAGE_MMEM_READ_BYTES_MAX;
    if (!scpi_storage_read_path_param(context, path, sizeof(path))) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &offset, FALSE);
    (void)SCPI_ParamUInt32(context, &length, FALSE);
    if (length == 0u || length > SCPI_STORAGE_MMEM_READ_BYTES_MAX) {
        length = SCPI_STORAGE_MMEM_READ_BYTES_MAX;
    }

    uint32_t job_id = 0u;
    bool ok = storage_manager_post_file_read_job(path, offset, length, &job_id);
    if (ok) {
        ok = scpi_storage_wait_job(job_id);
    }

    storage_manager_file_read_t read_info;
    uint8_t data[SCPI_STORAGE_MMEM_READ_BYTES_MAX];
    if (ok) {
        ok = storage_manager_get_file_read_job_result(job_id,
                                                      &read_info,
                                                      data,
                                                      sizeof(data));
    } else {
        memset(&read_info, 0, sizeof(read_info));
    }

    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    storage_manager_vector_t vector;
    storage_manager_get_vector(&vector);
    const uint32_t returned = ok ? read_info.returned : 0u;
    char hex[(SCPI_STORAGE_MMEM_READ_BYTES_MAX * 2u) + 1u];
    scpi_storage_hex_encode(data, returned, hex, sizeof(hex));

    SCPI_ResultText(context, ok ? "OK" : storage_manager_state_string(vector.state));
    SCPI_ResultUInt32(context, job.id);
    SCPI_ResultUInt32(context, ok ? read_info.offset : offset);
    SCPI_ResultUInt32(context, length);
    SCPI_ResultUInt32(context, returned);
    SCPI_ResultUInt32(context, ok ? read_info.file_size : 0u);
    SCPI_ResultBool(context, ok && read_info.eof ? TRUE : FALSE);
    SCPI_ResultUInt32(context, ok ? read_info.path_hash : 0u);
    SCPI_ResultUInt32(context, vector.storage_error);
    SCPI_ResultText(context, ok ? hex : "");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_file_delete(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    char path[96];
    if (!scpi_storage_read_path_param(context, path, sizeof(path))) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    if (!storage_manager_post_file_delete_job(path, &job_id)) {
        scpi_port_push_exec_error(context, "STORAGE_DELETE");
        return SCPI_RES_ERR;
    }
    const bool ok = scpi_storage_wait_job(job_id);
    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    SCPI_ResultText(context, ok ? "DELETED" : "ERROR");
    SCPI_ResultUInt32(context, job.id);
    SCPI_ResultText(context, storage_manager_job_state_string(job.state));
    SCPI_ResultUInt32(context, job.error);
    SCPI_ResultText(context, job.path[0] != '\0' ? job.path : path);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_storage_rename_job(scpi_t *context,
                                             bool is_dir)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    char old_path[96];
    char new_path[96];
    if (!scpi_storage_read_path_param(context, old_path, sizeof(old_path)) ||
        !scpi_storage_read_path_param(context, new_path, sizeof(new_path))) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    const bool posted = is_dir ?
        storage_manager_post_directory_rename_job(old_path, new_path, &job_id) :
        storage_manager_post_file_rename_job(old_path, new_path, &job_id);
    if (!posted) {
        scpi_port_push_exec_error(context, is_dir ? "STORAGE_DIR_RENAME" : "STORAGE_FILE_RENAME");
        return SCPI_RES_ERR;
    }

    const bool ok = scpi_storage_wait_job(job_id);
    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    SCPI_ResultText(context, ok ? "RENAMED" : "ERROR");
    SCPI_ResultUInt32(context, job.id);
    SCPI_ResultText(context, storage_manager_job_state_string(job.state));
    SCPI_ResultUInt32(context, job.error);
    SCPI_ResultText(context, job.path[0] != '\0' ? job.path : new_path);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_file_rename(scpi_t *context)
{
    return scpi_storage_rename_job(context, false);
}

static scpi_result_t scpi_storage_directory_job(scpi_t *context,
                                                bool create)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_STORAGE_MAINT)) {
        return SCPI_RES_ERR;
    }

    char path[96];
    if (!scpi_storage_read_path_param(context, path, sizeof(path))) {
        return SCPI_RES_ERR;
    }

    uint32_t job_id = 0u;
    const bool posted = create ?
        storage_manager_post_directory_create_job(path, &job_id) :
        storage_manager_post_directory_delete_job(path, &job_id);
    if (!posted) {
        scpi_port_push_exec_error(context, create ? "STORAGE_DIR_CREATE" : "STORAGE_DIR_DELETE");
        return SCPI_RES_ERR;
    }

    const bool ok = scpi_storage_wait_job(job_id);
    storage_manager_job_result_t job;
    storage_manager_get_job_result(&job);
    SCPI_ResultText(context, ok ? (create ? "CREATED" : "DELETED") : "ERROR");
    SCPI_ResultUInt32(context, job.id);
    SCPI_ResultText(context, storage_manager_job_state_string(job.state));
    SCPI_ResultUInt32(context, job.error);
    SCPI_ResultText(context, job.path[0] != '\0' ? job.path : path);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_storage_directory_create(scpi_t *context)
{
    return scpi_storage_directory_job(context, true);
}

scpi_result_t scpi_cmd_storage_directory_delete(scpi_t *context)
{
    return scpi_storage_directory_job(context, false);
}

scpi_result_t scpi_cmd_storage_directory_rename(scpi_t *context)
{
    return scpi_storage_rename_job(context, true);
}

scpi_result_t scpi_cmd_storage_directory_catalog_q(scpi_t *context)
{
    return scpi_cmd_mmem_catalog_page_q(context);
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

static bool scpi_storage_read_path_param(scpi_t *context, char *path, size_t path_size)
{
    const char *raw_path = NULL;
    size_t raw_path_len = 0u;
    if (SCPI_ParamCharacters(context, &raw_path, &raw_path_len, TRUE) != TRUE ||
        raw_path == NULL ||
        raw_path_len == 0u ||
        raw_path_len >= path_size) {
        return false;
    }

    memcpy(path, raw_path, raw_path_len);
    path[raw_path_len] = '\0';
    return true;
}

static int scpi_storage_hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

static bool scpi_storage_hex_decode(const char *hex,
                                    size_t hex_len,
                                    uint8_t *output,
                                    size_t output_size,
                                    size_t *decoded_size)
{
    if (decoded_size != NULL) {
        *decoded_size = 0u;
    }
    if (hex == NULL || output == NULL || decoded_size == NULL ||
        (hex_len % 2u) != 0u ||
        (hex_len / 2u) > output_size) {
        return false;
    }

    for (size_t i = 0u; i < hex_len; i += 2u) {
        const int high = scpi_storage_hex_nibble(hex[i]);
        const int low = scpi_storage_hex_nibble(hex[i + 1u]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[i / 2u] = (uint8_t)(((uint8_t)high << 4u) | (uint8_t)low);
    }
    *decoded_size = hex_len / 2u;
    return true;
}

static void scpi_storage_result_write_status(scpi_t *context)
{
    storage_manager_write_snapshot_t snapshot;
    storage_manager_get_write_snapshot(&snapshot);
    const bool active = snapshot.state == STORAGE_MANAGER_WRITE_STATE_RECEIVING ||
                        snapshot.state == STORAGE_MANAGER_WRITE_STATE_READY ||
                        snapshot.state == STORAGE_MANAGER_WRITE_STATE_QUEUED ||
                        snapshot.state == STORAGE_MANAGER_WRITE_STATE_WRITING;
    SCPI_ResultBool(context, active ? TRUE : FALSE);
    SCPI_ResultUInt32(context, snapshot.txn_id);
    SCPI_ResultText(context, storage_manager_write_state_string(snapshot.state));
    SCPI_ResultUInt32(context, snapshot.expected_size);
    SCPI_ResultUInt32(context, snapshot.received_size);
    SCPI_ResultUInt32(context, snapshot.expected_crc32);
    SCPI_ResultUInt32(context, snapshot.actual_crc32);
    SCPI_ResultUInt32(context, snapshot.path_hash);
    SCPI_ResultUInt32(context, snapshot.error == 0u ? s_storage_write_state.last_error : snapshot.error);
    SCPI_ResultUInt32(context, s_storage_write_state.job_id);
    SCPI_ResultText(context, snapshot.path);
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
