#include "storage_manager_internal.h"


storage_manager_vector_t s_storage_vector;
bool s_boot_snapshot_pending;
uint32_t s_boot_snapshot_due_ms;
storage_trace_record_t s_trace_ring[STORAGE_MANAGER_TRACE_RING_COUNT];
uint32_t s_trace_next;
uint32_t s_trace_count;
storage_manager_job_t s_storage_job;
bool s_file_read_result_leased;
uint32_t s_next_job_id;
uint8_t s_boot_snapshot_step;
storage_manager_write_snapshot_t s_write_snapshot;
uint8_t s_write_buffer[STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES];
uint32_t s_next_write_txn_id;
uint32_t s_runtime_log_last_attempt_ms;
uint32_t s_runtime_log_next_sequence;
bool s_runtime_log_sequence_initialized;

static const storage_manager_write_contract_t s_write_contracts[] = {
    {
        .object = STORAGE_MANAGER_OBJECT_REFMEM_PACKAGE,
        .directory = "/refmem",
        .final_path = "/refmem/app_model.rmtp",
        .tmp_path = "/refmem/app_model.tmp",
        .max_bytes = STORAGE_MANAGER_OBJECT_WRITE_MAX_BYTES,
    },
};

static const char *const s_allowed_roots[] = {
    "/manifest.json",
    "/manifest.idx",
    "/refs",
    "/packs",
    "/profile",
    "/mission",
    "/cal",
    "/snapshots",
    "/traces",
    "/reports",
    "/logs",
    "/update",
    "/factory",
    "/refmem",
};

static const char *const s_system_pack_dirs[] = {
    "/profile",
    "/profile/profiles",
    "/mission",
    "/cal",
    "/snapshots",
    "/snapshots/boot",
    "/snapshots/arm",
    "/snapshots/fault",
    "/snapshots/run",
    "/traces",
    "/traces/run",
    "/traces/fault",
    "/reports",
    "/reports/run",
    "/reports/fault",
    "/reports/acceptance",
    "/logs",
    "/logs/run",
    STORAGE_MANAGER_RUNTIME_LOG_DIRECTORY,
    "/update",
    "/update/compat",
    "/factory",
    "/refmem",
};

uint32_t storage_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

uint64_t storage_now_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

bool storage_budget_elapsed(uint64_t start_us, uint32_t budget_us)
{
    if (budget_us == 0u) {
        return false;
    }
    return (storage_now_us() - start_us) >= budget_us;
}

uint32_t storage_hash_path(const char *path)
{
    uint32_t hash = 2166136261u;
    if (path == NULL) {
        return hash;
    }
    while (*path != '\0') {
        hash ^= (uint8_t)(*path);
        hash *= 16777619u;
        path++;
    }
    return hash;
}

const storage_manager_write_contract_t *storage_find_object_contract(storage_manager_object_t object)
{
    for (size_t i = 0u; i < sizeof(s_write_contracts) / sizeof(s_write_contracts[0]); i++) {
        if (s_write_contracts[i].object == object) {
            return &s_write_contracts[i];
        }
    }
    return NULL;
}

bool storage_manager_init(void)
{
    memset(&s_storage_vector, 0, sizeof(s_storage_vector));
    s_storage_vector.state = STORAGE_MANAGER_STATE_IDLE;
    s_storage_vector.fatfs_available = fatfs_port_is_available();
    s_storage_vector.card_status = SD_CARD_STATUS_NOT_INITIALIZED;
    s_storage_vector.manifest_status = STORAGE_MANAGER_MANIFEST_UNKNOWN;
    s_storage_vector.current_job_state = STORAGE_MANAGER_JOB_STATE_IDLE;
    s_storage_vector.current_job_type = STORAGE_MANAGER_JOB_TYPE_NONE;
    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_file_read_result_leased = false;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_IDLE;
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_NONE;
    memset(&s_write_snapshot, 0, sizeof(s_write_snapshot));
    memset(s_write_buffer, 0, sizeof(s_write_buffer));
    s_next_job_id = 1u;
    s_next_write_txn_id = 1u;
    s_runtime_log_last_attempt_ms = 0u;
    s_runtime_log_next_sequence = 1u;
    s_runtime_log_sequence_initialized = false;
    s_boot_snapshot_pending = true;
    s_boot_snapshot_due_ms = storage_now_ms() + STORAGE_MANAGER_BOOT_SNAPSHOT_DELAY_MS;
    s_boot_snapshot_step = 0u;
    return true;
}

void storage_copy_field(char *destination, size_t destination_size, const char *source)
{
    if (destination == NULL || destination_size == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)snprintf(destination, destination_size, "%s", source);
}

void storage_publish_job_result(void)
{
    s_storage_vector.current_job_id = s_storage_job.result.id;
    s_storage_vector.current_job_type = s_storage_job.result.type;
    s_storage_vector.current_job_state = s_storage_job.result.state;
    s_storage_vector.job_path_hash = s_storage_job.result.path_hash;
    s_storage_vector.job_total_bytes = s_storage_job.result.size;
    s_storage_vector.job_done_bytes =
        s_storage_job.result.state == STORAGE_MANAGER_JOB_STATE_DONE ?
            s_storage_job.result.size :
            0u;
    s_storage_vector.job_error = s_storage_job.result.error;
    storage_copy_field(s_storage_vector.job_path,
                       sizeof(s_storage_vector.job_path),
                           s_storage_job.result.path);
}

void storage_job_complete_manifest_scan(bool ok)
{
    s_storage_job.result.state = ok ? STORAGE_MANAGER_JOB_STATE_DONE :
                                      STORAGE_MANAGER_JOB_STATE_FAILED;
    s_storage_job.result.error = ok ? STORAGE_MANAGER_ERROR_NONE :
                                      (uint32_t)s_storage_vector.manifest_status;
    s_storage_job.result.size = s_storage_vector.manifest_required_count;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       "/manifest.idx");
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
}

void storage_job_complete_fault_evidence(void)
{
    const bool snapshot_ok = s_storage_job.step_ok0;
    const bool trace_ok = s_storage_job.step_ok1;
    const bool report_ok = s_storage_job.step_ok2;
    const bool ok = snapshot_ok && trace_ok && report_ok;

    s_storage_job.result.state = ok ? STORAGE_MANAGER_JOB_STATE_DONE :
                                      STORAGE_MANAGER_JOB_STATE_FAILED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    if (!snapshot_ok && s_storage_vector.last_snapshot_error != STORAGE_MANAGER_ERROR_NONE) {
        s_storage_job.result.error = s_storage_vector.last_snapshot_error;
    } else if (!trace_ok && s_storage_vector.last_trace_error != STORAGE_MANAGER_ERROR_NONE) {
        s_storage_job.result.error = s_storage_vector.last_trace_error;
    } else if (!report_ok && s_storage_vector.last_fault_report_error != STORAGE_MANAGER_ERROR_NONE) {
        s_storage_job.result.error = s_storage_vector.last_fault_report_error;
    } else if (!ok) {
        s_storage_job.result.error = s_storage_vector.storage_error;
    }

    s_storage_job.result.size = s_storage_vector.last_fault_report_id;
    s_storage_job.result.is_dir = false;
    if (s_storage_vector.last_fault_report_path[0] != '\0') {
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           s_storage_vector.last_fault_report_path);
        s_storage_job.result.path_hash = s_storage_vector.last_fault_report_path_hash;
    } else if (s_storage_vector.last_trace_path[0] != '\0') {
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           s_storage_vector.last_trace_path);
        s_storage_job.result.path_hash = s_storage_vector.last_trace_path_hash;
    } else {
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           s_storage_vector.last_snapshot_path);
        s_storage_job.result.path_hash = s_storage_vector.last_snapshot_path_hash;
    }
    storage_publish_job_result();
}

bool storage_job_is_active(void)
{
    return s_storage_job.result.state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
           s_storage_job.result.state == STORAGE_MANAGER_JOB_STATE_RUNNING ||
           s_file_read_result_leased;
}

bool storage_write_buffer_is_active(void)
{
    return s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_RECEIVING ||
           s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_READY ||
           s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_QUEUED ||
           s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_WRITING;
}

bool storage_error_is_retryable(uint32_t error)
{
    return error == STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
}

bool storage_acquire_sd_resource(void)
{
    /* Product hardware gives the TF card a dedicated SPI1 bus.  The SD
     * resource therefore owns SPI1 implicitly and must not contend with the
     * LCD's independent SPI0 resource. */
    if (!resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_SD,
                                        "StorageAO")) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }
    board_prepare_sd_spi();
    return true;
}

void storage_release_sd_resource(void)
{
    resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_SD,
                                   "StorageAO");
}

static bool storage_is_control_char(char c)
{
    return ((unsigned char)c) < 0x20u || c == 0x7fu;
}

static bool storage_path_is_allowed_root(const char *path)
{
    if (strcmp(path, "/") == 0) {
        return true;
    }

    for (size_t i = 0u; i < sizeof(s_allowed_roots) / sizeof(s_allowed_roots[0]); i++) {
        const char *root = s_allowed_roots[i];
        const size_t root_len = strlen(root);
        if (strncmp(path, root, root_len) != 0) {
            continue;
        }
        if (path[root_len] == '\0' || path[root_len] == '/') {
            return true;
        }
    }
    return false;
}

bool storage_normalize_path(const char *input, char *output, size_t output_size)
{
    if (input == NULL || output == NULL || output_size == 0u) {
        return false;
    }

    size_t input_len = strlen(input);
    while (input_len > 0u && (input[input_len - 1u] == '\r' || input[input_len - 1u] == '\n' ||
                              input[input_len - 1u] == ' ' || input[input_len - 1u] == '\t')) {
        input_len--;
    }

    if (input_len == 0u) {
        if (output_size < 2u) {
            return false;
        }
        (void)snprintf(output, output_size, "/");
        return true;
    }

    if (input_len >= output_size || input_len > STORAGE_MANAGER_MAX_PATH_LEN) {
        return false;
    }

    if (input[0] != '/') {
        return false;
    }

    bool previous_slash = false;
    size_t used = 0u;
    for (size_t i = 0u; i < input_len; i++) {
        const char c = input[i];
        if (storage_is_control_char(c) || c == '\\' || c == ':') {
            return false;
        }
        if (c == '/') {
            if (previous_slash) {
                continue;
            }
            previous_slash = true;
        } else {
            previous_slash = false;
        }
        if (used + 1u >= output_size) {
            return false;
        }
        output[used++] = c;
    }

    while (used > 1u && output[used - 1u] == '/') {
        used--;
    }
    output[used] = '\0';

    const size_t normalized_len = strlen(output);
    const bool ends_with_parent = normalized_len >= 3u &&
                                  strcmp(output + normalized_len - 3u, "/..") == 0;
    if (strstr(output, "/../") != NULL ||
        strstr(output, "/./") != NULL ||
        strcmp(output, "/..") == 0 ||
        strcmp(output, "/.") == 0 ||
        ends_with_parent) {
        return false;
    }

    return storage_path_is_allowed_root(output);
}

bool storage_make_write_tmp_path(const char *final_path, char *tmp_path, size_t tmp_path_size)
{
    if (final_path == NULL || tmp_path == NULL || tmp_path_size == 0u) {
        return false;
    }

    const char *slash = strrchr(final_path, '/');
    const size_t dir_len = slash == NULL ? 0u : (size_t)(slash - final_path);
    if (dir_len == 0u) {
        return snprintf(tmp_path, tmp_path_size, "/write.tmp") > 0;
    }
    if (dir_len + strlen("/write.tmp") + 1u > tmp_path_size) {
        return false;
    }
    memcpy(tmp_path, final_path, dir_len);
    tmp_path[dir_len] = '\0';
    (void)snprintf(tmp_path + dir_len, tmp_path_size - dir_len, "/write.tmp");
    return true;
}

bool storage_parse_u32(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }

    uint32_t parsed = 0u;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        parsed = (parsed * 10u) + (uint32_t)(*cursor - '0');
    }
    *value = parsed;
    return true;
}

static bool storage_parse_required_field(const char *entry,
                                         const char *name,
                                         char *value,
                                         size_t value_size)
{
    if (entry == NULL || name == NULL || value == NULL || value_size == 0u) {
        return false;
    }

    const size_t name_len = strlen(name);
    const char *cursor = entry;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, ',');
        const size_t token_len = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (token_len > name_len && strncmp(cursor, name, name_len) == 0 && cursor[name_len] == '=') {
            const size_t copy_len = token_len - name_len - 1u;
            if (copy_len >= value_size) {
                return false;
            }
            memcpy(value, cursor + name_len + 1u, copy_len);
            value[copy_len] = '\0';
            return true;
        }
        if (end == NULL) {
            break;
        }
        cursor = end + 1u;
    }
    return false;
}

void storage_reset_manifest_summary(storage_manager_manifest_status_t status)
{
    s_storage_vector.manifest_status = status;
    s_storage_vector.manifest_schema = 0u;
    s_storage_vector.manifest_required_count = 0u;
    s_storage_vector.manifest_missing_count = 0u;
    s_storage_vector.manifest_product_id[0] = '\0';
    s_storage_vector.manifest_hardware_id[0] = '\0';
    s_storage_vector.manifest_build_id[0] = '\0';
}

bool storage_snapshot_kind_is_valid(const char *kind)
{
    return kind != NULL &&
           (strcmp(kind, "boot") == 0 ||
            strcmp(kind, "arm") == 0 ||
            strcmp(kind, "fault") == 0 ||
            strcmp(kind, "run") == 0);
}

void storage_publish_snapshot_result(const char *kind,
                                            uint32_t sequence,
                                            const char *path,
                                            uint32_t error)
{
    s_storage_vector.last_snapshot_id = sequence;
    s_storage_vector.last_snapshot_error = error;
    s_storage_vector.last_snapshot_path_hash = storage_hash_path(path);
    storage_copy_field(s_storage_vector.last_snapshot_kind,
                       sizeof(s_storage_vector.last_snapshot_kind),
                       kind);
    storage_copy_field(s_storage_vector.last_snapshot_path,
                       sizeof(s_storage_vector.last_snapshot_path),
                       path);
}

void storage_publish_trace_result(const char *kind,
                                         uint32_t sequence,
                                         const char *path,
                                         uint32_t event_count,
                                         uint32_t error)
{
    s_storage_vector.last_trace_id = sequence;
    s_storage_vector.last_trace_error = error;
    s_storage_vector.last_trace_event_count = event_count;
    s_storage_vector.last_trace_path_hash = storage_hash_path(path);
    storage_copy_field(s_storage_vector.last_trace_kind,
                       sizeof(s_storage_vector.last_trace_kind),
                       kind);
    storage_copy_field(s_storage_vector.last_trace_path,
                       sizeof(s_storage_vector.last_trace_path),
                       path);
}

void storage_publish_runtime_log_result(uint32_t sequence,
                                               const char *path,
                                               uint32_t byte_count,
                                               uint32_t error)
{
    s_storage_vector.log_attempt_error = error;
    if (error == STORAGE_MANAGER_ERROR_NONE) {
        s_storage_vector.last_log_id = sequence;
        s_storage_vector.last_log_error = STORAGE_MANAGER_ERROR_NONE;
        s_storage_vector.last_log_bytes = byte_count;
        s_storage_vector.last_log_path_hash = storage_hash_path(path);
        s_storage_vector.log_segment_count++;
        s_storage_vector.log_flushed_bytes += byte_count;
        storage_copy_field(s_storage_vector.last_log_path,
                           sizeof(s_storage_vector.last_log_path),
                           path);
    }
}

void storage_publish_runtime_log_queue_status(void)
{
    portable_log_port_status_t status;
    portable_log_port_get_status(&status);
    s_storage_vector.log_pending_bytes = status.persistent_queue_bytes;
    s_storage_vector.log_dropped_count = status.persistent_queue_dropped_count;
    s_storage_vector.log_dropped_bytes = status.persistent_queue_dropped_bytes;
}

void storage_publish_fault_report_result(uint32_t sequence,
                                                const char *path,
                                                uint32_t error)
{
    s_storage_vector.last_fault_report_id = sequence;
    s_storage_vector.last_fault_report_error = error;
    s_storage_vector.last_fault_report_path_hash = storage_hash_path(path);
    s_storage_vector.last_fault_snapshot_id = s_storage_vector.last_snapshot_id;
    s_storage_vector.last_fault_trace_id = s_storage_vector.last_trace_id;
    storage_copy_field(s_storage_vector.last_fault_report_path,
                       sizeof(s_storage_vector.last_fault_report_path),
                       path);
}

bool storage_trace_kind_is_valid(const char *kind)
{
    return kind != NULL && (strcmp(kind, "fault") == 0 || strcmp(kind, "run") == 0);
}

void storage_manager_trace_event(uint8_t domain,
                                 uint16_t event_id,
                                 uint8_t severity,
                                 uint32_t arg0,
                                 uint32_t arg1)
{
    osal_critical_enter();
    storage_trace_record_t *record = &s_trace_ring[s_trace_next];
    record->timestamp_ms = storage_now_ms();
    record->event_id = event_id;
    record->domain = domain;
    record->severity = severity;
    record->arg0 = arg0;
    record->arg1 = arg1;

    s_trace_next = (s_trace_next + 1u) % STORAGE_MANAGER_TRACE_RING_COUNT;
    if (s_trace_count < STORAGE_MANAGER_TRACE_RING_COUNT) {
        s_trace_count++;
    }
    osal_critical_exit();
}

bool storage_manifest_check_required(const char *required_entry)
{
    char raw_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_parse_required_field(required_entry, "required", raw_path, sizeof(raw_path))) {
        return false;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(raw_path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.manifest_missing_count++;
        return false;
    }

    fatfs_port_file_info_t info;
    if (fatfs_port_file_info(normalized_path, &info) != FATFS_PORT_STATUS_OK || info.is_dir) {
        s_storage_vector.manifest_missing_count++;
        return false;
    }

    char size_text[16];
    uint32_t expected_size = 0u;
    if (storage_parse_required_field(required_entry, "size", size_text, sizeof(size_text)) &&
        storage_parse_u32(size_text, &expected_size) &&
        expected_size != info.size) {
        s_storage_vector.manifest_missing_count++;
        return false;
    }

    return true;
}

static fatfs_port_status_t storage_write_text_checked(const char *final_path,
                                                      const char *tmp_path,
                                                      const char *text)
{
    const fatfs_port_status_t status = fatfs_port_write_text_file_atomic(final_path, tmp_path, text);
    if (status != FATFS_PORT_STATUS_OK) {
        s_storage_vector.storage_error = status == FATFS_PORT_STATUS_RENAME_FAILED ?
                                             STORAGE_MANAGER_ERROR_RENAME_FAILED :
                                             STORAGE_MANAGER_ERROR_WRITE_FAILED;
    }
    return status;
}

static fatfs_port_status_t storage_write_text_if_missing(const char *final_path,
                                                         const char *tmp_path,
                                                         const char *text)
{
    fatfs_port_file_info_t info;
    const fatfs_port_status_t info_status = fatfs_port_file_info(final_path, &info);
    if (info_status == FATFS_PORT_STATUS_OK && !info.is_dir) {
        return FATFS_PORT_STATUS_OK;
    }
    if (info_status != FATFS_PORT_STATUS_OK &&
        info_status != FATFS_PORT_STATUS_PATH_NOT_FOUND) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        return info_status;
    }
    return storage_write_text_checked(final_path, tmp_path, text);
}

static fatfs_port_status_t storage_make_system_pack_dirs(void)
{
    for (size_t i = 0u; i < sizeof(s_system_pack_dirs) / sizeof(s_system_pack_dirs[0]); i++) {
        const fatfs_port_status_t status = fatfs_port_make_directory(s_system_pack_dirs[i]);
        if (status != FATFS_PORT_STATUS_OK) {
            return status;
        }
    }
    return FATFS_PORT_STATUS_OK;
}

static uint32_t storage_crc32_text(const char *text)
{
    return ota_crc32_compute((const uint8_t *)text, strlen(text));
}

bool storage_manager_initialize_system_pack(void)
{
    if (!storage_manager_probe()) {
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    fatfs_port_file_info_t existing_manifest;
    fatfs_port_status_t status = fatfs_port_file_info("/manifest.idx", &existing_manifest);
    if (status == FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        return true;
    }
    if (status != FATFS_PORT_STATUS_PATH_NOT_FOUND) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        return false;
    }

    status = storage_make_system_pack_dirs();
    if (status != FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    char profile[384];
    int written = snprintf(profile,
                           sizeof(profile),
                           "{\n"
                           "  \"magic\": \"RP2350_TRIG_PROFILE\",\n"
                           "  \"schema\": 1,\n"
                           "  \"product_id\": \"%s\",\n"
                           "  \"hardware_id\": \"%s\",\n"
                           "  \"build_id\": \"%s\",\n"
                           "  \"source\": \"device-bootstrap\",\n"
                           "  \"name\": \"default\",\n"
                           "  \"trigger\": {\"mode\": \"IDLE\", \"armed\": false}\n"
                           "}\n",
                           STORAGE_MANAGER_PRODUCT_ID,
                           STORAGE_MANAGER_HARDWARE_ID,
                           g_project_build_id);
    if (written <= 0 || (size_t)written >= sizeof(profile)) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    char mission[320];
    written = snprintf(mission,
                       sizeof(mission),
                       "{\n"
                       "  \"magic\": \"RP2350_TRIG_MISSION\",\n"
                       "  \"schema\": 1,\n"
                       "  \"product_id\": \"%s\",\n"
                       "  \"hardware_id\": \"%s\",\n"
                       "  \"build_id\": \"%s\",\n"
                       "  \"source\": \"device-bootstrap\",\n"
                       "  \"name\": \"default\",\n"
                       "  \"steps\": []\n"
                       "}\n",
                       STORAGE_MANAGER_PRODUCT_ID,
                       STORAGE_MANAGER_HARDWARE_ID,
                       g_project_build_id);
    if (written <= 0 || (size_t)written >= sizeof(mission)) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    char node_map[384];
    written = snprintf(node_map,
                       sizeof(node_map),
                       "{\n"
                       "  \"magic\": \"RP2350_TRIG_NODE_MAP\",\n"
                       "  \"schema\": 1,\n"
                       "  \"product_id\": \"%s\",\n"
                       "  \"hardware_id\": \"%s\",\n"
                       "  \"build_id\": \"%s\",\n"
                       "  \"source\": \"device-bootstrap\",\n"
                       "  \"nodes\": {\"A0\": \"unassigned\", \"A1\": \"unassigned\", \"A2\": \"unassigned\", \"A3\": \"unassigned\"}\n"
                       "}\n",
                       STORAGE_MANAGER_PRODUCT_ID,
                       STORAGE_MANAGER_HARDWARE_ID,
                       g_project_build_id);
    if (written <= 0 || (size_t)written >= sizeof(node_map)) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    char calibration[384];
    written = snprintf(calibration,
                       sizeof(calibration),
                       "{\n"
                       "  \"magic\": \"RP2350_TRIG_CAL\",\n"
                       "  \"schema\": 1,\n"
                       "  \"product_id\": \"%s\",\n"
                       "  \"hardware_id\": \"%s\",\n"
                       "  \"build_id\": \"%s\",\n"
                       "  \"source\": \"device-bootstrap\",\n"
                       "  \"board\": {\"timebase_ppm\": 0, \"trigger_delay_ns\": 0}\n"
                       "}\n",
                       STORAGE_MANAGER_PRODUCT_ID,
                       STORAGE_MANAGER_HARDWARE_ID,
                       g_project_build_id);
    if (written <= 0 || (size_t)written >= sizeof(calibration)) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    char manifest_idx[960];
    written = snprintf(manifest_idx,
                       sizeof(manifest_idx),
                       "magic=RP2350_TRIG_SD\n"
                       "schema=1\n"
                       "product_id=%s\n"
                       "hardware_id=%s\n"
                       "pack_version=0.1.0\n"
                       "min_firmware=0.1.0\n"
                       "build_id=%s\n"
                       "default.profile=/profile/active.json\n"
                       "default.mission=/mission/recipe.json\n"
                       "default.calibration=/cal/board_cal.json\n"
                       "default.ota_package=/update/DHRT100_UPDATE.pkg\n"
                       "required=/profile/active.json,type=profile,crc32=%08lX\n"
                       "required=/mission/recipe.json,type=mission,crc32=%08lX\n"
                       "required=/cal/board_cal.json,type=calibration,crc32=%08lX\n"
                       "required=/update/DHRT100_UPDATE.pkg,type=ota_package,crc32=%08lX\n",
                       STORAGE_MANAGER_PRODUCT_ID,
                       STORAGE_MANAGER_HARDWARE_ID,
                       g_project_build_id,
                       (unsigned long)storage_crc32_text(profile),
                       (unsigned long)storage_crc32_text(mission),
                       (unsigned long)storage_crc32_text(calibration),
                       (unsigned long)storage_crc32_text(STORAGE_MANAGER_BOOTSTRAP_OTA_TEXT));
    if (written <= 0 || (size_t)written >= sizeof(manifest_idx)) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    char manifest_json[768];
    written = snprintf(manifest_json,
                       sizeof(manifest_json),
                       "{\n"
                       "  \"schema_version\": 1,\n"
                       "  \"source\": \"device-bootstrap\",\n"
                       "  \"product_id\": \"%s\",\n"
                       "  \"hardware_id\": \"%s\",\n"
                       "  \"build_id\": \"%s\",\n"
                       "  \"defaults\": {\n"
                       "    \"profile\": \"/profile/active.json\",\n"
                       "    \"mission\": \"/mission/recipe.json\",\n"
                       "    \"node_map\": \"/mission/node_map.json\",\n"
                       "    \"calibration\": \"/cal/board_cal.json\",\n"
                       "    \"ota_default\": \"/update/DHRT100_UPDATE.pkg\"\n"
                       "  },\n"
                       "  \"note\": \"OTA package is a placeholder until replaced by PC tooling.\"\n"
                       "}\n",
                       STORAGE_MANAGER_PRODUCT_ID,
                       STORAGE_MANAGER_HARDWARE_ID,
                       g_project_build_id);
    if (written <= 0 || (size_t)written >= sizeof(manifest_json)) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    status = storage_write_text_if_missing("/profile/active.json", "/profile/active.tmp", profile);
    if (status == FATFS_PORT_STATUS_OK) {
        status = storage_write_text_if_missing("/mission/recipe.json", "/mission/recipe.tmp", mission);
    }
    if (status == FATFS_PORT_STATUS_OK) {
        status = storage_write_text_if_missing("/mission/node_map.json", "/mission/node_map.tmp", node_map);
    }
    if (status == FATFS_PORT_STATUS_OK) {
        status = storage_write_text_if_missing("/cal/board_cal.json", "/cal/board_cal.tmp", calibration);
    }
    if (status == FATFS_PORT_STATUS_OK) {
        status = storage_write_text_if_missing("/update/DHRT100_UPDATE.pkg",
                                               "/update/DHRT100_UPDATE.tmp",
                                               STORAGE_MANAGER_BOOTSTRAP_OTA_TEXT);
    }
    if (status == FATFS_PORT_STATUS_OK) {
        status = storage_write_text_if_missing("/manifest.json", "/manifest.tmp", manifest_json);
    }
    if (status == FATFS_PORT_STATUS_OK) {
        status = storage_write_text_checked("/manifest.idx", "/manifest.idx.tmp", manifest_idx);
    }

    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        return false;
    }

    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.fs_mounted = true;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    storage_reset_manifest_summary(STORAGE_MANAGER_MANIFEST_UNKNOWN);
    return true;
}

bool storage_manager_probe(void)
{
    if (s_storage_vector.state == STORAGE_MANAGER_STATE_UNINITIALIZED) {
        return false;
    }
    if (s_storage_vector.state == STORAGE_MANAGER_STATE_CARD_READY &&
        s_storage_vector.card_present &&
        s_storage_vector.fs_mounted &&
        s_storage_vector.card_status == SD_CARD_STATUS_OK) {
        if (s_storage_vector.storage_error == STORAGE_MANAGER_ERROR_RESOURCE_BUSY) {
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
        }
        return true;
    }

    if (!storage_acquire_sd_resource()) {
        return false;
    }

    const sd_card_config_t config = {
        .spi = BOARD_SD_SPI_PORT,
        .sck_pin = BOARD_SD_SPI_CLK_PIN,
        .mosi_pin = BOARD_SD_SPI_MOSI_PIN,
        .miso_pin = BOARD_SD_SPI_MISO_PIN,
        .cs_pin = BOARD_SD_SPI_CS_PIN,
        .init_baud_hz = STORAGE_MANAGER_SD_INIT_BAUD_HZ,
        .run_baud_hz = STORAGE_MANAGER_SD_RUN_BAUD_HZ,
    };

    sd_card_info_t info;
    sd_card_status_t status = SD_CARD_STATUS_NOT_INITIALIZED;
    if (sd_card_init(&config)) {
        status = sd_card_probe(&info);
    } else {
        memset(&info, 0, sizeof(info));
        status = SD_CARD_STATUS_NOT_INITIALIZED;
    }
    s_storage_vector.probe_count++;
    s_storage_vector.last_probe_ms = storage_now_ms();
    s_storage_vector.card_status = status;
    s_storage_vector.card_present = info.present;
    s_storage_vector.card_type = info.type;
    s_storage_vector.high_capacity = info.high_capacity;
    s_storage_vector.block_count = info.block_count;
    s_storage_vector.capacity_kib = info.capacity_kib;
    s_storage_vector.fatfs_available = fatfs_port_is_available();
    s_storage_vector.fs_mounted = false;

    if (status != SD_CARD_STATUS_OK) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_NO_CARD;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_CARD;
        storage_release_sd_resource();
        return false;
    }

    if (!s_storage_vector.fatfs_available) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        storage_release_sd_resource();
        return true;
    }

    const fatfs_port_status_t mount_status = fatfs_port_mount();
    storage_release_sd_resource();
    s_storage_vector.fs_mounted = mount_status == FATFS_PORT_STATUS_OK;
    s_storage_vector.state = s_storage_vector.fs_mounted ?
                                 STORAGE_MANAGER_STATE_CARD_READY :
                                 STORAGE_MANAGER_STATE_NO_FILESYSTEM;
    s_storage_vector.storage_error = s_storage_vector.fs_mounted ?
                                         STORAGE_MANAGER_ERROR_NONE :
                                         STORAGE_MANAGER_ERROR_NO_FS;
    return s_storage_vector.fs_mounted;
}

bool storage_manager_catalog(const char *path, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0u) {
        return false;
    }
    buffer[0] = '\0';

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        (void)snprintf(buffer, buffer_size, "PATH_DENIED");
        return false;
    }

    if (!storage_manager_probe()) {
        (void)snprintf(buffer, buffer_size, "%s", storage_manager_state_string(s_storage_vector.state));
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        (void)snprintf(buffer, buffer_size, "RESOURCE_BUSY");
        return false;
    }

    const fatfs_port_status_t status = fatfs_port_catalog(normalized_path, buffer, buffer_size);
    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        if (status == FATFS_PORT_STATUS_PATH_NOT_FOUND) {
            s_storage_vector.fs_mounted = true;
            s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_ERROR;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        } else {
            s_storage_vector.fs_mounted = false;
            s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        }
        return false;
    }

    s_storage_vector.fs_mounted = true;
    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    return true;
}

bool storage_manager_catalog_page(const char *path,
                                  uint32_t offset,
                                  uint32_t limit,
                                  char *buffer,
                                  size_t buffer_size,
                                  storage_manager_catalog_page_t *page)
{
    if (buffer == NULL || buffer_size == 0u || page == NULL || limit == 0u) {
        return false;
    }
    buffer[0] = '\0';
    memset(page, 0, sizeof(*page));

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        (void)snprintf(buffer, buffer_size, "PATH_DENIED");
        return false;
    }

    if (!storage_manager_probe()) {
        (void)snprintf(buffer, buffer_size, "%s", storage_manager_state_string(s_storage_vector.state));
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        (void)snprintf(buffer, buffer_size, "RESOURCE_BUSY");
        return false;
    }

    fatfs_port_catalog_page_t fat_page;
    const fatfs_port_status_t status = fatfs_port_catalog_page(normalized_path,
                                                              offset,
                                                              limit,
                                                              buffer,
                                                              buffer_size,
                                                              &fat_page);
    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        if (status == FATFS_PORT_STATUS_PATH_NOT_FOUND) {
            s_storage_vector.fs_mounted = true;
            s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_ERROR;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        } else {
            s_storage_vector.fs_mounted = false;
            s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        }
        return false;
    }

    page->returned_count = fat_page.returned_count;
    page->next_offset = fat_page.next_offset;
    page->complete = fat_page.complete;
    page->truncated = fat_page.truncated;
    page->path_hash = storage_hash_path(normalized_path);
    storage_copy_field(page->path, sizeof(page->path), normalized_path);
    s_storage_vector.fs_mounted = true;
    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    return true;
}

bool storage_manager_file_info(const char *path, storage_manager_file_info_t *info)
{
    if (info == NULL) {
        return false;
    }
    memset(info, 0, sizeof(*info));

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        return false;
    }

    if (!storage_manager_probe()) {
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        return false;
    }

    fatfs_port_file_info_t file_info;
    const fatfs_port_status_t status = fatfs_port_file_info(normalized_path, &file_info);
    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        if (status == FATFS_PORT_STATUS_PATH_NOT_FOUND) {
            s_storage_vector.fs_mounted = true;
            s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_ERROR;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        } else {
            s_storage_vector.fs_mounted = false;
            s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        }
        return false;
    }

    info->size = file_info.size;
    info->is_dir = file_info.is_dir;
    info->path_hash = storage_hash_path(normalized_path);
    storage_copy_field(info->path, sizeof(info->path), normalized_path);
    s_storage_vector.fs_mounted = true;
    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    return true;
}

bool storage_manager_read_file_range(const char *path,
                                     uint32_t offset,
                                     uint8_t *buffer,
                                     size_t buffer_size,
                                     storage_manager_file_read_t *read_info)
{
    if (buffer == NULL || buffer_size == 0u || read_info == NULL) {
        return false;
    }
    memset(read_info, 0, sizeof(*read_info));

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        return false;
    }

    if (!storage_manager_probe()) {
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        return false;
    }

    size_t bytes_read = 0u;
    uint32_t file_size = 0u;
    const fatfs_port_status_t status = fatfs_port_read_binary_range(normalized_path,
                                                                    offset,
                                                                    buffer,
                                                                    buffer_size,
                                                                    &bytes_read,
                                                                    &file_size);
    storage_release_sd_resource();

    if (status != FATFS_PORT_STATUS_OK) {
        if (status == FATFS_PORT_STATUS_PATH_NOT_FOUND) {
            s_storage_vector.fs_mounted = true;
            s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_ERROR;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        } else {
            s_storage_vector.fs_mounted = false;
            s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        }
        return false;
    }

    read_info->offset = offset;
    read_info->requested = (uint32_t)buffer_size;
    read_info->returned = (uint32_t)bytes_read;
    read_info->file_size = file_size;
    read_info->eof = offset + (uint32_t)bytes_read >= file_size;
    read_info->path_hash = storage_hash_path(normalized_path);
    storage_copy_field(read_info->path, sizeof(read_info->path), normalized_path);
    s_storage_vector.fs_mounted = true;
    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    return true;
}
