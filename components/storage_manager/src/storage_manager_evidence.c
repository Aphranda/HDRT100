#include "storage_manager_internal.h"

static void storage_manager_service_job(void)
{
    if (s_storage_job.result.state != STORAGE_MANAGER_JOB_STATE_QUEUED &&
        s_storage_job.result.state != STORAGE_MANAGER_JOB_STATE_RUNNING) {
        return;
    }

    if (s_storage_job.result.state == STORAGE_MANAGER_JOB_STATE_QUEUED) {
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_RUNNING;
        storage_publish_job_result();
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_FILE_INFO) {
        storage_manager_file_info_t info;
        const bool ok = storage_manager_file_info(s_storage_job.result.path, &info);
        if (ok) {
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = info.size;
            s_storage_job.result.is_dir = info.is_dir;
            s_storage_job.result.path_hash = info.path_hash;
            storage_copy_field(s_storage_job.result.path,
                               sizeof(s_storage_job.result.path),
                               info.path);
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = false;
            s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_FILE_READ) {
        const bool ok = storage_manager_read_file_range(s_storage_job.result.path,
                                                        s_storage_job.offset,
                                                        s_write_buffer,
                                                        s_storage_job.length,
                                                       &s_storage_job.read_info);
        if (ok) {
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = s_storage_job.read_info.returned;
            s_storage_job.result.is_dir = false;
            s_storage_job.result.path_hash = s_storage_job.read_info.path_hash;
            storage_copy_field(s_storage_job.result.path,
                               sizeof(s_storage_job.result.path),
                               s_storage_job.read_info.path);
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = false;
            s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_FILE_WRITE) {
        if (s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_QUEUED ||
            s_write_snapshot.path[0] == '\0' ||
            s_write_snapshot.tmp_path[0] == '\0') {
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_SEQUENCE;
            s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_FAILED;
            s_write_snapshot.error = STORAGE_MANAGER_ERROR_SEQUENCE;
            storage_publish_job_result();
            return;
        }

        s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_WRITING;
        bool ok = false;
        if (storage_manager_probe() && storage_acquire_sd_resource()) {
            fatfs_port_status_t status = s_write_snapshot.direct_write
                ? fatfs_port_write_binary_file(
                    s_write_snapshot.path, s_write_buffer,
                    s_write_snapshot.received_size)
                : fatfs_port_write_binary_file_atomic(
                    s_write_snapshot.path, s_write_snapshot.tmp_path,
                    s_write_buffer, s_write_snapshot.received_size);
            storage_release_sd_resource();
            ok = status == FATFS_PORT_STATUS_OK;
            if (!ok) {
                s_storage_vector.storage_error =
                    status == FATFS_PORT_STATUS_RENAME_FAILED ?
                        STORAGE_MANAGER_ERROR_RENAME_FAILED :
                        STORAGE_MANAGER_ERROR_WRITE_FAILED;
            }
        }

        if (ok) {
            s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_DONE;
            s_write_snapshot.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
            s_storage_vector.fs_mounted = true;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = s_write_snapshot.received_size;
            s_storage_job.result.is_dir = false;
            s_storage_job.result.path_hash = s_write_snapshot.path_hash;
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_QUEUED;
                return;
            }
            s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_FAILED;
            s_write_snapshot.error = s_storage_vector.storage_error;
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = false;
            s_storage_job.result.path_hash = s_write_snapshot.path_hash;
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_FILE_DELETE) {
        bool ok = false;
        if (storage_manager_probe() && storage_acquire_sd_resource()) {
            const fatfs_port_status_t status = fatfs_port_delete(s_storage_job.result.path);
            storage_release_sd_resource();
            ok = status == FATFS_PORT_STATUS_OK || status == FATFS_PORT_STATUS_PATH_NOT_FOUND;
            if (!ok) {
                s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
            }
        }

        if (ok) {
            s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = false;
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = false;
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_FILE_RENAME ||
        s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_DIRECTORY_RENAME) {
        const bool is_dir = s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_DIRECTORY_RENAME;
        bool ok = false;
        if (s_storage_job.target_path[0] == '\0') {
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        } else if (storage_manager_probe() && storage_acquire_sd_resource()) {
            const fatfs_port_status_t status = fatfs_port_rename(s_storage_job.result.path,
                                                                 s_storage_job.target_path);
            storage_release_sd_resource();
            ok = status == FATFS_PORT_STATUS_OK;
            if (!ok) {
                s_storage_vector.storage_error =
                    status == FATFS_PORT_STATUS_RENAME_FAILED ?
                        STORAGE_MANAGER_ERROR_RENAME_FAILED :
                        STORAGE_MANAGER_ERROR_WRITE_FAILED;
            }
        }

        if (ok) {
            s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = is_dir;
            storage_copy_field(s_storage_job.result.path,
                               sizeof(s_storage_job.result.path),
                               s_storage_job.target_path);
            s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = is_dir;
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_DIRECTORY_CREATE) {
        bool ok = false;
        if (storage_manager_probe() && storage_acquire_sd_resource()) {
            const fatfs_port_status_t status = fatfs_port_make_directory(s_storage_job.result.path);
            storage_release_sd_resource();
            ok = status == FATFS_PORT_STATUS_OK;
            if (!ok) {
                s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
            }
        }

        if (ok) {
            s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = true;
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = true;
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_DIRECTORY_DELETE) {
        bool ok = false;
        if (storage_manager_probe() && storage_acquire_sd_resource()) {
            const fatfs_port_status_t status = fatfs_port_delete(s_storage_job.result.path);
            storage_release_sd_resource();
            ok = status == FATFS_PORT_STATUS_OK || status == FATFS_PORT_STATUS_PATH_NOT_FOUND;
            if (!ok) {
                s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
            }
        }

        if (ok) {
            s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = true;
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = true;
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE) {
        const bool ok = storage_manager_catalog_page(s_storage_job.result.path,
                                                     s_storage_job.offset,
                                                     s_storage_job.limit,
                                                     s_storage_job.catalog_buffer,
                                                     sizeof(s_storage_job.catalog_buffer),
                                                     &s_storage_job.catalog_page);
        if (ok) {
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = s_storage_job.catalog_page.returned_count;
            s_storage_job.result.is_dir = true;
            s_storage_job.result.path_hash = s_storage_job.catalog_page.path_hash;
            storage_copy_field(s_storage_job.result.path,
                               sizeof(s_storage_job.result.path),
                               s_storage_job.catalog_page.path);
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = true;
            s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_SNAPSHOT_WRITE) {
        const bool ok = storage_manager_write_snapshot(s_storage_job.argument);
        if (ok) {
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_DONE;
            s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
            s_storage_job.result.size = s_storage_vector.last_snapshot_id;
            s_storage_job.result.is_dir = false;
            s_storage_job.result.path_hash = s_storage_vector.last_snapshot_path_hash;
            storage_copy_field(s_storage_job.result.path,
                               sizeof(s_storage_job.result.path),
                               s_storage_vector.last_snapshot_path);
        } else {
            if (storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.last_snapshot_error != STORAGE_MANAGER_ERROR_NONE ?
                                             s_storage_vector.last_snapshot_error :
                                             s_storage_vector.storage_error;
            s_storage_job.result.size = 0u;
            s_storage_job.result.is_dir = false;
            s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        }
        storage_publish_job_result();
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_MANIFEST_SCAN) {
        if (s_storage_job.step == 0u) {
            const bool ok = storage_manager_scan_manifest();
            if (!ok && storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            if (ok || s_storage_vector.manifest_status != STORAGE_MANAGER_MANIFEST_NOT_FOUND) {
                storage_job_complete_manifest_scan(ok);
                return;
            }
            s_storage_job.step = 1u;
            storage_publish_job_result();
            return;
        }

        if (s_storage_job.step == 1u) {
            s_storage_job.step_ok0 = storage_manager_initialize_system_pack();
            if (!s_storage_job.step_ok0 &&
                storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.step = 2u;
            storage_publish_job_result();
            return;
        }

        const bool ok = s_storage_job.step_ok0 && storage_manager_scan_manifest();
        if (!ok && storage_error_is_retryable(s_storage_vector.storage_error)) {
            return;
        }
        storage_job_complete_manifest_scan(ok);
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_SYSTEM_INIT) {
        if (s_storage_job.step == 0u) {
            s_storage_job.step_ok0 = storage_manager_initialize_system_pack();
            if (!s_storage_job.step_ok0 &&
                storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.step = 1u;
            storage_publish_job_result();
            return;
        }

        const bool ok = s_storage_job.step_ok0 && storage_manager_scan_manifest();
        if (!ok && storage_error_is_retryable(s_storage_vector.storage_error)) {
            return;
        }
        if (!ok && !s_storage_job.step_ok0) {
            s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
            s_storage_job.result.error = s_storage_vector.storage_error;
            s_storage_job.result.size = s_storage_vector.manifest_required_count;
            s_storage_job.result.is_dir = false;
            storage_copy_field(s_storage_job.result.path,
                               sizeof(s_storage_job.result.path),
                               "/manifest.idx");
            s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
            storage_publish_job_result();
        } else {
            storage_job_complete_manifest_scan(ok);
        }
        return;
    }

    if (s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_FAULT_EVIDENCE) {
        if (s_storage_job.step == 0u) {
            s_storage_job.step_ok0 = storage_manager_write_snapshot("fault");
            if (!s_storage_job.step_ok0 &&
                storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.step = 1u;
            storage_publish_job_result();
            return;
        }

        if (s_storage_job.step == 1u) {
            s_storage_job.step_ok1 = storage_manager_write_trace("fault");
            if (!s_storage_job.step_ok1 &&
                storage_error_is_retryable(s_storage_vector.storage_error)) {
                return;
            }
            s_storage_job.step = 2u;
            storage_publish_job_result();
            return;
        }

        s_storage_job.step_ok2 = storage_manager_write_fault_report();
        if (!s_storage_job.step_ok2 &&
            storage_error_is_retryable(s_storage_vector.storage_error)) {
            return;
        }
        storage_job_complete_fault_evidence();
        return;
    }

    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH;
    storage_publish_job_result();
}

bool storage_manager_scan_manifest(void)
{
    storage_reset_manifest_summary(STORAGE_MANAGER_MANIFEST_UNKNOWN);

    if (!storage_manager_probe()) {
        s_storage_vector.manifest_status = s_storage_vector.state == STORAGE_MANAGER_STATE_PATH_DENIED ?
                                               STORAGE_MANAGER_MANIFEST_PATH_DENIED :
                                               STORAGE_MANAGER_MANIFEST_IO_ERROR;
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_IO_ERROR;
        return false;
    }

    char manifest[STORAGE_MANAGER_MANIFEST_MAX_BYTES + 1u];
    size_t bytes_read = 0u;
    const fatfs_port_status_t read_status = fatfs_port_read_text_file("/manifest.idx",
                                                                     manifest,
                                                                     sizeof(manifest),
                                                                     &bytes_read);
    if (read_status != FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        s_storage_vector.manifest_status = read_status == FATFS_PORT_STATUS_PATH_NOT_FOUND ?
                                               STORAGE_MANAGER_MANIFEST_NOT_FOUND :
                                               STORAGE_MANAGER_MANIFEST_IO_ERROR;
        return false;
    }

    if (bytes_read >= STORAGE_MANAGER_MANIFEST_MAX_BYTES) {
        storage_release_sd_resource();
        s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_INVALID;
        return false;
    }

    bool has_magic = false;
    bool has_schema = false;
    bool has_product = false;
    bool has_hardware = false;
    bool manifest_invalid = false;

    const char *cursor = manifest;
    while (*cursor != '\0') {
        const char *line_end = strchr(cursor, '\n');
        size_t line_len = line_end != NULL ? (size_t)(line_end - cursor) : strlen(cursor);
        while (line_len > 0u && (cursor[line_len - 1u] == '\r' ||
                                 cursor[line_len - 1u] == ' ' ||
                                 cursor[line_len - 1u] == '\t')) {
            line_len--;
        }

        if (line_len > 0u) {
            if (line_len > STORAGE_MANAGER_MANIFEST_MAX_LINE) {
                manifest_invalid = true;
                break;
            }

            char line[STORAGE_MANAGER_MANIFEST_MAX_LINE + 1u];
            memcpy(line, cursor, line_len);
            line[line_len] = '\0';

            char *equals = strchr(line, '=');
            if (equals == NULL) {
                manifest_invalid = true;
                break;
            }
            *equals = '\0';
            const char *key = line;
            const char *value = equals + 1;

            if (strcmp(key, "magic") == 0) {
                has_magic = strcmp(value, "RP2350_TRIG_SD") == 0;
            } else if (strcmp(key, "schema") == 0) {
                has_schema = storage_parse_u32(value, &s_storage_vector.manifest_schema);
                if (!has_schema) {
                    manifest_invalid = true;
                    break;
                }
            } else if (strcmp(key, "product_id") == 0) {
                storage_copy_field(s_storage_vector.manifest_product_id,
                                   sizeof(s_storage_vector.manifest_product_id),
                                   value);
                has_product = true;
            } else if (strcmp(key, "hardware_id") == 0) {
                storage_copy_field(s_storage_vector.manifest_hardware_id,
                                   sizeof(s_storage_vector.manifest_hardware_id),
                                   value);
                has_hardware = true;
            } else if (strcmp(key, "build_id") == 0) {
                storage_copy_field(s_storage_vector.manifest_build_id,
                                   sizeof(s_storage_vector.manifest_build_id),
                                   value);
            } else if (strcmp(key, "required") == 0) {
                s_storage_vector.manifest_required_count++;
                (void)storage_manifest_check_required(cursor);
            }
        }

        if (line_end == NULL) {
            break;
        }
        cursor = line_end + 1;
    }

    storage_release_sd_resource();

    if (manifest_invalid || !has_magic || !has_schema || !has_product || !has_hardware) {
        s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_INVALID;
        return false;
    }

    if (s_storage_vector.manifest_schema != 1u) {
        s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_SCHEMA_UNSUPPORTED;
        return false;
    }

    if (strcmp(s_storage_vector.manifest_product_id, STORAGE_MANAGER_PRODUCT_ID) != 0) {
        s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_PRODUCT_MISMATCH;
        return false;
    }

    if (strcmp(s_storage_vector.manifest_hardware_id, STORAGE_MANAGER_HARDWARE_ID) != 0) {
        s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_HARDWARE_MISMATCH;
        return false;
    }

    if (s_storage_vector.manifest_required_count == 0u ||
        s_storage_vector.manifest_missing_count != 0u) {
        s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_REQUIRED_MISSING;
        return false;
    }

    s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_OK;
    return true;
}

bool storage_manager_write_snapshot(const char *kind)
{
    const char *snapshot_kind = (kind == NULL || kind[0] == '\0') ? "boot" : kind;
    if (!storage_snapshot_kind_is_valid(snapshot_kind)) {
        storage_publish_snapshot_result(snapshot_kind, 0u, "", STORAGE_MANAGER_ERROR_PATH);
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }

    if (!storage_manager_probe()) {
        storage_publish_snapshot_result(snapshot_kind, 0u, "", s_storage_vector.storage_error);
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        storage_publish_snapshot_result(snapshot_kind, 0u, "", STORAGE_MANAGER_ERROR_RESOURCE_BUSY);
        return false;
    }

    char directory[40];
    char prefix[24];
    (void)snprintf(directory, sizeof(directory), "/snapshots/%s", snapshot_kind);
    (void)snprintf(prefix, sizeof(prefix), "%s_", snapshot_kind);

    fatfs_port_status_t status = fatfs_port_make_directory("/snapshots");
    if (status == FATFS_PORT_STATUS_OK) {
        status = fatfs_port_make_directory(directory);
    }
    if (status != FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        storage_publish_snapshot_result(snapshot_kind, 0u, "", STORAGE_MANAGER_ERROR_WRITE_FAILED);
        return false;
    }

    uint32_t max_sequence = 0u;
    status = fatfs_port_find_max_sequence(directory, prefix, ".json", &max_sequence);
    if (status != FATFS_PORT_STATUS_OK || max_sequence >= 999999u) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_SEQUENCE;
        storage_publish_snapshot_result(snapshot_kind, 0u, "", STORAGE_MANAGER_ERROR_SEQUENCE);
        return false;
    }

    const uint32_t sequence = max_sequence + 1u;
    char final_path[96];
    char tmp_path[96];
    (void)snprintf(final_path,
                   sizeof(final_path),
                   "%s/%s_%06lu.json",
                   directory,
                   snapshot_kind,
                   (unsigned long)sequence);
    (void)snprintf(tmp_path, sizeof(tmp_path), "%s/%s.tmp", directory, snapshot_kind);

    fatfs_port_file_info_t existing_info;
    if (fatfs_port_file_info(final_path, &existing_info) == FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_SEQUENCE;
        storage_publish_snapshot_result(snapshot_kind, 0u, "", STORAGE_MANAGER_ERROR_SEQUENCE);
        return false;
    }

    char payload[STORAGE_MANAGER_SNAPSHOT_MAX_BYTES];
    const int written = snprintf(payload,
                                 sizeof(payload),
                                 "{\n"
                                 "  \"magic\": \"RP2350_TRIG_SNAPSHOT\",\n"
                                 "  \"schema\": 1,\n"
                                 "  \"kind\": \"%s\",\n"
                                 "  \"sequence\": %lu,\n"
                                 "  \"build_id\": \"%s\",\n"
                                 "  \"uptime_ms\": %lu,\n"
                                 "  \"storage\": {\n"
                                 "    \"state\": \"%s\",\n"
                                 "    \"card_present\": %u,\n"
                                 "    \"fs_mounted\": %u,\n"
                                 "    \"error\": %lu\n"
                                 "  },\n"
                                 "  \"manifest\": {\n"
                                 "    \"status\": \"%s\",\n"
                                 "    \"schema\": %lu,\n"
                                 "    \"product_id\": \"%s\",\n"
                                 "    \"hardware_id\": \"%s\",\n"
                                 "    \"build_id\": \"%s\",\n"
                                 "    \"required_count\": %lu,\n"
                                 "    \"missing_count\": %lu\n"
                                 "  }\n"
                                 "}\n",
                                 snapshot_kind,
                                 (unsigned long)sequence,
                                 g_project_build_id,
                                 (unsigned long)storage_now_ms(),
                                 storage_manager_state_string(s_storage_vector.state),
                                 s_storage_vector.card_present ? 1u : 0u,
                                 s_storage_vector.fs_mounted ? 1u : 0u,
                                 (unsigned long)s_storage_vector.storage_error,
                                 storage_manager_manifest_status_string(s_storage_vector.manifest_status),
                                 (unsigned long)s_storage_vector.manifest_schema,
                                 s_storage_vector.manifest_product_id,
                                 s_storage_vector.manifest_hardware_id,
                                 s_storage_vector.manifest_build_id,
                                 (unsigned long)s_storage_vector.manifest_required_count,
                                 (unsigned long)s_storage_vector.manifest_missing_count);

    if (written <= 0 || (size_t)written >= sizeof(payload)) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        storage_publish_snapshot_result(snapshot_kind, 0u, "", STORAGE_MANAGER_ERROR_WRITE_FAILED);
        return false;
    }

    status = fatfs_port_write_text_file_atomic(final_path, tmp_path, payload);
    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        const uint32_t error = status == FATFS_PORT_STATUS_RENAME_FAILED ?
                                   STORAGE_MANAGER_ERROR_RENAME_FAILED :
                                   STORAGE_MANAGER_ERROR_WRITE_FAILED;
        s_storage_vector.storage_error = error;
        storage_publish_snapshot_result(snapshot_kind, 0u, final_path, error);
        return false;
    }

    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.fs_mounted = true;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    storage_publish_snapshot_result(snapshot_kind, sequence, final_path, STORAGE_MANAGER_ERROR_NONE);
    return true;
}

bool storage_manager_write_trace(const char *kind)
{
    const char *trace_kind = (kind == NULL || kind[0] == '\0') ? "fault" : kind;
    if (!storage_trace_kind_is_valid(trace_kind)) {
        storage_publish_trace_result(trace_kind, 0u, "", 0u, STORAGE_MANAGER_ERROR_PATH);
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }

    if (!storage_manager_probe()) {
        storage_publish_trace_result(trace_kind, 0u, "", 0u, s_storage_vector.storage_error);
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        storage_publish_trace_result(trace_kind, 0u, "", 0u, STORAGE_MANAGER_ERROR_RESOURCE_BUSY);
        return false;
    }

    char directory[32];
    char prefix[24];
    (void)snprintf(directory, sizeof(directory), "/traces/%s", trace_kind);
    (void)snprintf(prefix, sizeof(prefix), "%s_", trace_kind);

    fatfs_port_status_t status = fatfs_port_make_directory("/traces");
    if (status == FATFS_PORT_STATUS_OK) {
        status = fatfs_port_make_directory(directory);
    }
    if (status != FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        storage_publish_trace_result(trace_kind, 0u, "", 0u, STORAGE_MANAGER_ERROR_WRITE_FAILED);
        return false;
    }

    uint32_t max_sequence = 0u;
    status = fatfs_port_find_max_sequence(directory, prefix, ".bin", &max_sequence);
    if (status != FATFS_PORT_STATUS_OK || max_sequence >= 999999u) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_SEQUENCE;
        storage_publish_trace_result(trace_kind, 0u, "", 0u, STORAGE_MANAGER_ERROR_SEQUENCE);
        return false;
    }

    const uint32_t sequence = max_sequence + 1u;
    uint8_t payload[sizeof(storage_trace_header_t) +
                    (STORAGE_MANAGER_TRACE_RING_COUNT * sizeof(storage_trace_record_t))];
    storage_trace_record_t ordered[STORAGE_MANAGER_TRACE_RING_COUNT];

    osal_critical_enter();
    const uint32_t event_count = s_trace_count;
    const uint32_t first = event_count == STORAGE_MANAGER_TRACE_RING_COUNT ? s_trace_next : 0u;
    for (uint32_t i = 0u; i < event_count; i++) {
        ordered[i] = s_trace_ring[(first + i) % STORAGE_MANAGER_TRACE_RING_COUNT];
    }
    osal_critical_exit();

    const size_t event_bytes = (size_t)event_count * sizeof(storage_trace_record_t);

    const uint32_t start_ms = event_count > 0u ? ordered[0].timestamp_ms : storage_now_ms();
    const uint32_t end_ms = event_count > 0u ? ordered[event_count - 1u].timestamp_ms : start_ms;
    storage_trace_header_t header = {
        .magic = STORAGE_MANAGER_TRACE_MAGIC,
        .schema = STORAGE_MANAGER_TRACE_SCHEMA,
        .header_len = (uint16_t)sizeof(storage_trace_header_t),
        .sequence = sequence,
        .event_count = event_count,
        .start_ms = start_ms,
        .end_ms = end_ms,
        .tick_hz = STORAGE_MANAGER_TRACE_TICK_HZ,
        .flags = 0u,
        .crc32 = ota_crc32_compute((const uint8_t *)ordered, event_bytes),
    };

    memcpy(payload, &header, sizeof(header));
    memcpy(payload + sizeof(header), ordered, event_bytes);
    const size_t payload_size = sizeof(header) + event_bytes;

    char bin_path[96];
    char bin_tmp_path[96];
    char idx_path[96];
    char idx_tmp_path[96];
    (void)snprintf(bin_path,
                   sizeof(bin_path),
                   "%s/%s_%06lu.bin",
                   directory,
                   trace_kind,
                   (unsigned long)sequence);
    (void)snprintf(bin_tmp_path, sizeof(bin_tmp_path), "%s/%s.tmp", directory, trace_kind);
    (void)snprintf(idx_path,
                   sizeof(idx_path),
                   "%s/%s_%06lu.idx",
                   directory,
                   trace_kind,
                   (unsigned long)sequence);
    (void)snprintf(idx_tmp_path, sizeof(idx_tmp_path), "%s/%s.idx.tmp", directory, trace_kind);

    status = fatfs_port_write_binary_file_atomic(bin_path, bin_tmp_path, payload, payload_size);
    if (status == FATFS_PORT_STATUS_OK) {
        char index_text[384];
        const int index_len = snprintf(index_text,
                                       sizeof(index_text),
                                       "magic=RP2350_TRIG_TRACE_IDX\n"
                                       "schema=1\n"
                                       "kind=%s\n"
                                       "sequence=%lu\n"
                                       "bin=%s\n"
                                       "size=%lu\n"
                                       "event_count=%lu\n"
                                       "crc32=%08lX\n"
                                       "complete=1\n",
                                       trace_kind,
                                       (unsigned long)sequence,
                                       bin_path,
                                       (unsigned long)payload_size,
                                       (unsigned long)event_count,
                                       (unsigned long)header.crc32);
        if (index_len <= 0 || (size_t)index_len >= sizeof(index_text)) {
            status = FATFS_PORT_STATUS_WRITE_FAILED;
        } else {
            status = fatfs_port_write_text_file_atomic(idx_path, idx_tmp_path, index_text);
        }
    }

    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        const uint32_t error = status == FATFS_PORT_STATUS_RENAME_FAILED ?
                                   STORAGE_MANAGER_ERROR_RENAME_FAILED :
                                   STORAGE_MANAGER_ERROR_WRITE_FAILED;
        s_storage_vector.storage_error = error;
        storage_publish_trace_result(trace_kind, 0u, bin_path, event_count, error);
        return false;
    }

    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.fs_mounted = true;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    storage_publish_trace_result(trace_kind, sequence, bin_path, event_count, STORAGE_MANAGER_ERROR_NONE);
    return true;
}

static bool storage_manager_write_runtime_log_segment(void)
{
    char payload[STORAGE_MANAGER_RUNTIME_LOG_CHUNK_MAX_BYTES + 1u];
    const size_t payload_size =
        portable_log_port_persistent_peek(payload, STORAGE_MANAGER_RUNTIME_LOG_CHUNK_MAX_BYTES);
    if (payload_size == 0u) {
        storage_publish_runtime_log_queue_status();
        return true;
    }
    payload[payload_size] = '\0';

    if (!storage_manager_probe()) {
        storage_publish_runtime_log_result(0u, "", 0u, s_storage_vector.storage_error);
        storage_publish_runtime_log_queue_status();
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        storage_publish_runtime_log_result(0u, "", 0u, STORAGE_MANAGER_ERROR_RESOURCE_BUSY);
        storage_publish_runtime_log_queue_status();
        return false;
    }

    fatfs_port_status_t status = fatfs_port_make_directory("/logs");
    if (status == FATFS_PORT_STATUS_OK) {
        status = fatfs_port_make_directory(STORAGE_MANAGER_RUNTIME_LOG_DIRECTORY);
    }
    if (status != FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        storage_publish_runtime_log_result(0u, "", 0u, STORAGE_MANAGER_ERROR_WRITE_FAILED);
        storage_publish_runtime_log_queue_status();
        return false;
    }

    /* The legacy /logs/run directory may contain thousands of files and take
     * seconds to scan or traverse.  Never append to it on product hardware.
     * Recover a monotonic cursor from the new bounded ring directory instead. */
    if (!s_runtime_log_sequence_initialized) {
        char cursor_text[16];
        size_t cursor_size = 0u;
        status = fatfs_port_read_text_file(STORAGE_MANAGER_RUNTIME_LOG_CURSOR_PATH,
                                           cursor_text,
                                           sizeof(cursor_text),
                                           &cursor_size);
        uint32_t last_sequence = 0u;
        if (status == FATFS_PORT_STATUS_OK) {
            if (cursor_size == 0u ||
                !storage_parse_u32(cursor_text, &last_sequence)) {
                storage_release_sd_resource();
                s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_SEQUENCE;
                storage_publish_runtime_log_result(0u, "", 0u, STORAGE_MANAGER_ERROR_SEQUENCE);
                storage_publish_runtime_log_queue_status();
                return false;
            }
        } else if (status != FATFS_PORT_STATUS_PATH_NOT_FOUND) {
            storage_release_sd_resource();
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_SEQUENCE;
            storage_publish_runtime_log_result(0u, "", 0u, STORAGE_MANAGER_ERROR_SEQUENCE);
            storage_publish_runtime_log_queue_status();
            return false;
        }
        s_runtime_log_next_sequence = last_sequence == UINT32_MAX ? 1u : last_sequence + 1u;
        s_runtime_log_sequence_initialized = true;
    }

    if (s_runtime_log_next_sequence == 0u) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_SEQUENCE;
        storage_publish_runtime_log_result(0u, "", 0u, STORAGE_MANAGER_ERROR_SEQUENCE);
        storage_publish_runtime_log_queue_status();
        return false;
    }

    const uint32_t sequence = s_runtime_log_next_sequence;
    const uint32_t slot = (sequence - 1u) % STORAGE_MANAGER_RUNTIME_LOG_SLOT_COUNT;
    char final_path[96];
    char tmp_path[96];
    (void)snprintf(final_path,
                   sizeof(final_path),
                   STORAGE_MANAGER_RUNTIME_LOG_DIRECTORY "/log_%03lu.log",
                   (unsigned long)slot);
    (void)snprintf(tmp_path,
                   sizeof(tmp_path),
                   STORAGE_MANAGER_RUNTIME_LOG_DIRECTORY "/log.tmp");

    status = fatfs_port_write_text_file_atomic(final_path, tmp_path, payload);
    if (status == FATFS_PORT_STATUS_OK) {
        char cursor_text[16];
        (void)snprintf(cursor_text, sizeof(cursor_text), "%lu", (unsigned long)sequence);
        status = fatfs_port_write_text_file_atomic(STORAGE_MANAGER_RUNTIME_LOG_CURSOR_PATH,
                                                   STORAGE_MANAGER_RUNTIME_LOG_CURSOR_TMP_PATH,
                                                   cursor_text);
    }
    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        const uint32_t error = status == FATFS_PORT_STATUS_RENAME_FAILED ?
                                   STORAGE_MANAGER_ERROR_RENAME_FAILED :
                                   STORAGE_MANAGER_ERROR_WRITE_FAILED;
        s_storage_vector.storage_error = error;
        storage_publish_runtime_log_result(sequence, final_path, (uint32_t)payload_size, error);
        storage_publish_runtime_log_queue_status();
        return false;
    }

    portable_log_port_persistent_discard(payload_size);
    s_runtime_log_next_sequence = sequence == UINT32_MAX ? 1u : sequence + 1u;
    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.fs_mounted = true;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    storage_publish_runtime_log_result(sequence,
                                       final_path,
                                       (uint32_t)payload_size,
                                       STORAGE_MANAGER_ERROR_NONE);
    storage_publish_runtime_log_queue_status();
    return true;
}

static void storage_manager_service_runtime_log(uint64_t start_us, uint32_t budget_us)
{
    storage_publish_runtime_log_queue_status();

    if (storage_budget_elapsed(start_us, budget_us)) {
        return;
    }

    if (s_storage_vector.log_pending_bytes == 0u) {
        return;
    }

    const uint32_t now_ms = storage_now_ms();
    const bool threshold_reached =
        s_storage_vector.log_pending_bytes >= STORAGE_MANAGER_RUNTIME_LOG_FLUSH_THRESHOLD_BYTES;
    const bool period_elapsed =
        (uint32_t)(now_ms - s_runtime_log_last_attempt_ms) >=
        STORAGE_MANAGER_RUNTIME_LOG_FLUSH_PERIOD_MS;
    if (!threshold_reached && !period_elapsed) {
        return;
    }

    s_runtime_log_last_attempt_ms = now_ms;
    (void)storage_manager_write_runtime_log_segment();
}

bool storage_manager_write_fault_report(void)
{
    if (!storage_manager_probe()) {
        storage_publish_fault_report_result(0u, "", s_storage_vector.storage_error);
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        storage_publish_fault_report_result(0u, "", STORAGE_MANAGER_ERROR_RESOURCE_BUSY);
        return false;
    }

    fatfs_port_status_t status = fatfs_port_make_directory("/reports");
    if (status == FATFS_PORT_STATUS_OK) {
        status = fatfs_port_make_directory("/reports/fault");
    }
    if (status != FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        storage_publish_fault_report_result(0u, "", STORAGE_MANAGER_ERROR_WRITE_FAILED);
        return false;
    }

    uint32_t max_sequence = 0u;
    status = fatfs_port_find_max_sequence("/reports/fault",
                                          "pulse_fault_",
                                          ".json",
                                          &max_sequence);
    if (status != FATFS_PORT_STATUS_OK || max_sequence >= 999999u) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_SEQUENCE;
        storage_publish_fault_report_result(0u, "", STORAGE_MANAGER_ERROR_SEQUENCE);
        return false;
    }

    const uint32_t sequence = max_sequence + 1u;
    char final_path[96];
    char tmp_path[96];
    (void)snprintf(final_path,
                   sizeof(final_path),
                   "/reports/fault/pulse_fault_%06lu.json",
                   (unsigned long)sequence);
    (void)snprintf(tmp_path, sizeof(tmp_path), "/reports/fault/pulse_fault.tmp");

    char payload[STORAGE_MANAGER_FAULT_REPORT_MAX_BYTES];
    const int written = snprintf(payload,
                                 sizeof(payload),
                                 "{\n"
                                 "  \"magic\": \"RP2350_TRIG_FAULT_REPORT\",\n"
                                 "  \"schema\": 1,\n"
                                 "  \"sequence\": %lu,\n"
                                 "  \"build_id\": \"%s\",\n"
                                 "  \"uptime_ms\": %lu,\n"
                                 "  \"storage\": {\n"
                                 "    \"state\": \"%s\",\n"
                                 "    \"error\": %lu\n"
                                 "  },\n"
                                 "  \"manifest\": {\n"
                                 "    \"status\": \"%s\",\n"
                                 "    \"build_id\": \"%s\"\n"
                                 "  },\n"
                                 "  \"snapshot\": {\n"
                                 "    \"id\": %lu,\n"
                                 "    \"path\": \"%s\",\n"
                                 "    \"path_hash\": %lu,\n"
                                 "    \"error\": %lu\n"
                                 "  },\n"
                                 "  \"trace\": {\n"
                                 "    \"id\": %lu,\n"
                                 "    \"path\": \"%s\",\n"
                                 "    \"path_hash\": %lu,\n"
                                 "    \"event_count\": %lu,\n"
                                 "    \"error\": %lu\n"
                                 "  }\n"
                                 "}\n",
                                 (unsigned long)sequence,
                                 g_project_build_id,
                                 (unsigned long)storage_now_ms(),
                                 storage_manager_state_string(s_storage_vector.state),
                                 (unsigned long)s_storage_vector.storage_error,
                                 storage_manager_manifest_status_string(s_storage_vector.manifest_status),
                                 s_storage_vector.manifest_build_id,
                                 (unsigned long)s_storage_vector.last_snapshot_id,
                                 s_storage_vector.last_snapshot_path,
                                 (unsigned long)s_storage_vector.last_snapshot_path_hash,
                                 (unsigned long)s_storage_vector.last_snapshot_error,
                                 (unsigned long)s_storage_vector.last_trace_id,
                                 s_storage_vector.last_trace_path,
                                 (unsigned long)s_storage_vector.last_trace_path_hash,
                                 (unsigned long)s_storage_vector.last_trace_event_count,
                                 (unsigned long)s_storage_vector.last_trace_error);
    if (written <= 0 || (size_t)written >= sizeof(payload)) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        storage_publish_fault_report_result(0u, "", STORAGE_MANAGER_ERROR_WRITE_FAILED);
        return false;
    }

    status = fatfs_port_write_text_file_atomic(final_path, tmp_path, payload);
    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        const uint32_t error = status == FATFS_PORT_STATUS_RENAME_FAILED ?
                                   STORAGE_MANAGER_ERROR_RENAME_FAILED :
                                   STORAGE_MANAGER_ERROR_WRITE_FAILED;
        s_storage_vector.storage_error = error;
        storage_publish_fault_report_result(0u, final_path, error);
        return false;
    }

    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.fs_mounted = true;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    storage_publish_fault_report_result(sequence, final_path, STORAGE_MANAGER_ERROR_NONE);
    return true;
}

void storage_manager_service(uint32_t budget_us)
{
    const uint64_t start_us = storage_now_us();
    if (storage_job_is_active()) {
        storage_manager_service_job();
        return;
    }

    /* A local recorder or upload holds the bounded RAM transaction until it
     * explicitly commits. Do not start background filesystem work while its
     * producer is collecting or retaining that immutable evidence. */
    if (storage_write_buffer_is_active()) {
        return;
    }

    if (s_boot_snapshot_pending &&
        (int32_t)(storage_now_ms() - s_boot_snapshot_due_ms) >= 0) {
        if (s_boot_snapshot_step == 0u) {
            if (s_storage_vector.manifest_status == STORAGE_MANAGER_MANIFEST_OK) {
                s_boot_snapshot_step = 3u;
            } else if (!storage_manager_scan_manifest() &&
                       s_storage_vector.manifest_status == STORAGE_MANAGER_MANIFEST_NOT_FOUND) {
                s_boot_snapshot_step = 1u;
            } else {
                s_boot_snapshot_step = 3u;
            }
            return;
        }

        if (storage_budget_elapsed(start_us, budget_us)) {
            return;
        }

        if (s_boot_snapshot_step == 1u) {
            (void)storage_manager_initialize_system_pack();
            s_boot_snapshot_step = 2u;
            return;
        }

        if (storage_budget_elapsed(start_us, budget_us)) {
            return;
        }

        if (s_boot_snapshot_step == 2u) {
            (void)storage_manager_scan_manifest();
            s_boot_snapshot_step = 3u;
            return;
        }

        if (storage_budget_elapsed(start_us, budget_us)) {
            return;
        }

        s_boot_snapshot_pending = false;
        storage_manager_trace_event(1u, 1u, 1u, s_storage_vector.manifest_status, 0u);
        (void)storage_manager_write_snapshot("boot");
        return;
    }

    if (storage_budget_elapsed(start_us, budget_us)) {
        return;
    }

    storage_manager_service_runtime_log(start_us, budget_us);
    if (storage_budget_elapsed(start_us, budget_us)) {
        return;
    }

    storage_manager_service_job();
}

void storage_manager_get_vector(storage_manager_vector_t *vector)
{
    if (vector == NULL) {
        return;
    }
    *vector = s_storage_vector;
}

const char *storage_manager_state_string(storage_manager_state_t state)
{
    switch (state) {
    case STORAGE_MANAGER_STATE_UNINITIALIZED: return "UNINITIALIZED";
    case STORAGE_MANAGER_STATE_IDLE: return "IDLE";
    case STORAGE_MANAGER_STATE_CARD_READY: return "CARD_READY";
    case STORAGE_MANAGER_STATE_NO_CARD: return "NO_CARD";
    case STORAGE_MANAGER_STATE_NO_FILESYSTEM: return "NO_FS";
    case STORAGE_MANAGER_STATE_PATH_DENIED: return "PATH_DENIED";
    case STORAGE_MANAGER_STATE_PATH_ERROR: return "NO_PATH";
    case STORAGE_MANAGER_STATE_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}

const char *storage_manager_manifest_status_string(storage_manager_manifest_status_t status)
{
    switch (status) {
    case STORAGE_MANAGER_MANIFEST_UNKNOWN: return "UNKNOWN";
    case STORAGE_MANAGER_MANIFEST_OK: return "OK";
    case STORAGE_MANAGER_MANIFEST_NOT_FOUND: return "NOT_FOUND";
    case STORAGE_MANAGER_MANIFEST_INVALID: return "INVALID";
    case STORAGE_MANAGER_MANIFEST_SCHEMA_UNSUPPORTED: return "SCHEMA_UNSUPPORTED";
    case STORAGE_MANAGER_MANIFEST_PRODUCT_MISMATCH: return "PRODUCT_MISMATCH";
    case STORAGE_MANAGER_MANIFEST_HARDWARE_MISMATCH: return "HARDWARE_MISMATCH";
    case STORAGE_MANAGER_MANIFEST_REQUIRED_MISSING: return "REQUIRED_MISSING";
    case STORAGE_MANAGER_MANIFEST_IO_ERROR: return "IO_ERROR";
    case STORAGE_MANAGER_MANIFEST_PATH_DENIED: return "PATH_DENIED";
    default: return "UNKNOWN";
    }
}

const char *storage_manager_job_type_string(storage_manager_job_type_t type)
{
    switch (type) {
    case STORAGE_MANAGER_JOB_TYPE_NONE: return "NONE";
    case STORAGE_MANAGER_JOB_TYPE_FILE_INFO: return "FILE_INFO";
    case STORAGE_MANAGER_JOB_TYPE_FILE_READ: return "FILE_READ";
    case STORAGE_MANAGER_JOB_TYPE_FILE_WRITE: return "FILE_WRITE";
    case STORAGE_MANAGER_JOB_TYPE_FILE_DELETE: return "FILE_DELETE";
    case STORAGE_MANAGER_JOB_TYPE_FILE_RENAME: return "FILE_RENAME";
    case STORAGE_MANAGER_JOB_TYPE_DIRECTORY_CREATE: return "DIRECTORY_CREATE";
    case STORAGE_MANAGER_JOB_TYPE_DIRECTORY_DELETE: return "DIRECTORY_DELETE";
    case STORAGE_MANAGER_JOB_TYPE_DIRECTORY_RENAME: return "DIRECTORY_RENAME";
    case STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE: return "CATALOG_PAGE";
    case STORAGE_MANAGER_JOB_TYPE_SNAPSHOT_WRITE: return "SNAPSHOT_WRITE";
    case STORAGE_MANAGER_JOB_TYPE_MANIFEST_SCAN: return "MANIFEST_SCAN";
    case STORAGE_MANAGER_JOB_TYPE_FAULT_EVIDENCE: return "FAULT_EVIDENCE";
    case STORAGE_MANAGER_JOB_TYPE_SYSTEM_INIT: return "SYSTEM_INIT";
    default: return "UNKNOWN";
    }
}

const char *storage_manager_write_state_string(storage_manager_write_state_t state)
{
    switch (state) {
    case STORAGE_MANAGER_WRITE_STATE_IDLE: return "IDLE";
    case STORAGE_MANAGER_WRITE_STATE_RECEIVING: return "RECEIVING";
    case STORAGE_MANAGER_WRITE_STATE_READY: return "READY";
    case STORAGE_MANAGER_WRITE_STATE_QUEUED: return "QUEUED";
    case STORAGE_MANAGER_WRITE_STATE_WRITING: return "WRITING";
    case STORAGE_MANAGER_WRITE_STATE_DONE: return "DONE";
    case STORAGE_MANAGER_WRITE_STATE_FAILED: return "FAILED";
    case STORAGE_MANAGER_WRITE_STATE_ABORTED: return "ABORTED";
    default: return "UNKNOWN";
    }
}

const char *storage_manager_object_string(storage_manager_object_t object)
{
    switch (object) {
    case STORAGE_MANAGER_OBJECT_NONE: return "NONE";
    case STORAGE_MANAGER_OBJECT_REFMEM_PACKAGE: return "REFMEM_PACKAGE";
    default: return "UNKNOWN";
    }
}

const char *storage_manager_job_state_string(storage_manager_job_state_t state)
{
    switch (state) {
    case STORAGE_MANAGER_JOB_STATE_IDLE: return "IDLE";
    case STORAGE_MANAGER_JOB_STATE_QUEUED: return "QUEUED";
    case STORAGE_MANAGER_JOB_STATE_RUNNING: return "RUNNING";
    case STORAGE_MANAGER_JOB_STATE_DONE: return "DONE";
    case STORAGE_MANAGER_JOB_STATE_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}
