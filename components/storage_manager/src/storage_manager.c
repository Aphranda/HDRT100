#include "storage_manager.h"

#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "board.h"
#include "fatfs_port.h"
#include "osal.h"
#include "ota_crc32.h"
#include "portable_log_port.h"
#include "project_config.h"
#include "resource_arbiter.h"
#include "pico/time.h"

#define STORAGE_MANAGER_SD_INIT_BAUD_HZ 400000u
#define STORAGE_MANAGER_SD_RUN_BAUD_HZ 12500000u
#define STORAGE_MANAGER_ERROR_NONE 0u
#define STORAGE_MANAGER_ERROR_RESOURCE_BUSY 1u
#define STORAGE_MANAGER_ERROR_CARD 2u
#define STORAGE_MANAGER_ERROR_NO_FS 3u
#define STORAGE_MANAGER_ERROR_PATH 4u
#define STORAGE_MANAGER_ERROR_PATH_DENIED 5u
#define STORAGE_MANAGER_ERROR_WRITE_FAILED 6u
#define STORAGE_MANAGER_ERROR_RENAME_FAILED 7u
#define STORAGE_MANAGER_ERROR_SEQUENCE 8u
#define STORAGE_MANAGER_MAX_PATH_LEN 95u
#define STORAGE_MANAGER_TMP_SUFFIX ".tmp"
#define STORAGE_MANAGER_MANIFEST_MAX_BYTES 1024u
#define STORAGE_MANAGER_MANIFEST_MAX_LINE 160u
#define STORAGE_MANAGER_SNAPSHOT_MAX_BYTES 768u
#define STORAGE_MANAGER_FAULT_REPORT_MAX_BYTES 1024u
#define STORAGE_MANAGER_RUNTIME_LOG_CHUNK_MAX_BYTES 1024u
#define STORAGE_MANAGER_RUNTIME_LOG_FLUSH_THRESHOLD_BYTES 512u
#define STORAGE_MANAGER_RUNTIME_LOG_FLUSH_PERIOD_MS 5000u
#define STORAGE_MANAGER_FILE_READ_MAX_BYTES 512u
#define STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES 8192u
#define STORAGE_MANAGER_CATALOG_PAGE_MAX_BYTES 384u
#define STORAGE_MANAGER_RAW_CLEAR_MAX_SECTORS 64u
#define STORAGE_MANAGER_BOOT_SNAPSHOT_DELAY_MS 500u
#define STORAGE_MANAGER_TRACE_RING_COUNT 64u
#define STORAGE_MANAGER_TRACE_MAGIC 0x43525452u
#define STORAGE_MANAGER_TRACE_SCHEMA 1u
#define STORAGE_MANAGER_TRACE_TICK_HZ 1000u
#define STORAGE_MANAGER_PRODUCT_ID "RP2350_TRIG"
#define STORAGE_MANAGER_HARDWARE_ID "rp2350_trig"
#define STORAGE_MANAGER_BOOTSTRAP_OTA_TEXT "RP2350_TRIG placeholder OTA package. Replace with a release package before offline OTA.\n"

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint16_t event_id;
    uint8_t domain;
    uint8_t severity;
    uint32_t arg0;
    uint32_t arg1;
} storage_trace_record_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t schema;
    uint16_t header_len;
    uint32_t sequence;
    uint32_t event_count;
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t tick_hz;
    uint32_t flags;
    uint32_t crc32;
} storage_trace_header_t;

typedef struct {
    storage_manager_job_result_t result;
    char argument[16];
    uint32_t offset;
    uint32_t length;
    uint32_t limit;
    uint8_t step;
    bool step_ok0;
    bool step_ok1;
    bool step_ok2;
    storage_manager_file_read_t read_info;
    storage_manager_catalog_page_t catalog_page;
    uint8_t read_buffer[STORAGE_MANAGER_FILE_READ_MAX_BYTES];
    char catalog_buffer[STORAGE_MANAGER_CATALOG_PAGE_MAX_BYTES];
    char target_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
} storage_manager_job_t;

typedef struct {
    storage_manager_object_t object;
    const char *directory;
    const char *final_path;
    const char *tmp_path;
    uint32_t max_bytes;
} storage_manager_write_contract_t;

static storage_manager_vector_t s_storage_vector;
static bool s_boot_snapshot_pending;
static uint32_t s_boot_snapshot_due_ms;
static storage_trace_record_t s_trace_ring[STORAGE_MANAGER_TRACE_RING_COUNT];
static uint32_t s_trace_next;
static uint32_t s_trace_count;
static storage_manager_job_t s_storage_job;
static uint32_t s_next_job_id;
static uint8_t s_boot_snapshot_step;
static storage_manager_write_snapshot_t s_write_snapshot;
static uint8_t s_write_buffer[STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES];
static uint32_t s_next_write_txn_id;
static uint32_t s_runtime_log_last_attempt_ms;

static const storage_manager_write_contract_t s_write_contracts[] = {
    {
        .object = STORAGE_MANAGER_OBJECT_REFMEM_PACKAGE,
        .directory = "/refmem",
        .final_path = "/refmem/app_model.rmtp",
        .tmp_path = "/refmem/app_model.tmp",
        .max_bytes = STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES,
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
    "/update",
    "/update/compat",
    "/factory",
    "/refmem",
};

static uint32_t storage_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static uint64_t storage_now_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

static bool storage_budget_elapsed(uint64_t start_us, uint32_t budget_us)
{
    if (budget_us == 0u) {
        return false;
    }
    return (storage_now_us() - start_us) >= budget_us;
}

static uint32_t storage_hash_path(const char *path)
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

static const storage_manager_write_contract_t *storage_find_object_contract(storage_manager_object_t object)
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
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_IDLE;
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_NONE;
    memset(&s_write_snapshot, 0, sizeof(s_write_snapshot));
    memset(s_write_buffer, 0, sizeof(s_write_buffer));
    s_next_job_id = 1u;
    s_next_write_txn_id = 1u;
    s_runtime_log_last_attempt_ms = 0u;
    s_boot_snapshot_pending = true;
    s_boot_snapshot_due_ms = storage_now_ms() + STORAGE_MANAGER_BOOT_SNAPSHOT_DELAY_MS;
    s_boot_snapshot_step = 0u;
    return true;
}

static void storage_copy_field(char *destination, size_t destination_size, const char *source)
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

static void storage_publish_job_result(void)
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

static void storage_job_complete_manifest_scan(bool ok)
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

static void storage_job_complete_fault_evidence(void)
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

static bool storage_job_is_active(void)
{
    return s_storage_job.result.state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
           s_storage_job.result.state == STORAGE_MANAGER_JOB_STATE_RUNNING;
}

static bool storage_error_is_retryable(uint32_t error)
{
    return error == STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
}

static bool storage_acquire_sd_resource(void)
{
    if (!resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                        RESOURCE_ARBITER_RESOURCE_SD,
                                        "StorageAO")) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }
    board_prepare_sd_spi();
    return true;
}

static void storage_release_sd_resource(void)
{
    resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                   RESOURCE_ARBITER_RESOURCE_SD,
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

static bool storage_normalize_path(const char *input, char *output, size_t output_size)
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

static bool storage_make_write_tmp_path(const char *final_path, char *tmp_path, size_t tmp_path_size)
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

static bool storage_parse_u32(const char *text, uint32_t *value)
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

static void storage_reset_manifest_summary(storage_manager_manifest_status_t status)
{
    s_storage_vector.manifest_status = status;
    s_storage_vector.manifest_schema = 0u;
    s_storage_vector.manifest_required_count = 0u;
    s_storage_vector.manifest_missing_count = 0u;
    s_storage_vector.manifest_product_id[0] = '\0';
    s_storage_vector.manifest_hardware_id[0] = '\0';
    s_storage_vector.manifest_build_id[0] = '\0';
}

static bool storage_snapshot_kind_is_valid(const char *kind)
{
    return kind != NULL &&
           (strcmp(kind, "boot") == 0 ||
            strcmp(kind, "arm") == 0 ||
            strcmp(kind, "fault") == 0 ||
            strcmp(kind, "run") == 0);
}

static void storage_publish_snapshot_result(const char *kind,
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

static void storage_publish_trace_result(const char *kind,
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

static void storage_publish_runtime_log_result(uint32_t sequence,
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

static void storage_publish_runtime_log_queue_status(void)
{
    portable_log_port_status_t status;
    portable_log_port_get_status(&status);
    s_storage_vector.log_pending_bytes = status.persistent_queue_bytes;
    s_storage_vector.log_dropped_count = status.persistent_queue_dropped_count;
    s_storage_vector.log_dropped_bytes = status.persistent_queue_dropped_bytes;
}

static void storage_publish_fault_report_result(uint32_t sequence,
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

static bool storage_trace_kind_is_valid(const char *kind)
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

static bool storage_manifest_check_required(const char *required_entry)
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
                       "default.ota_package=/update/RP2350_TRIG_UPDATE.pkg\n"
                       "required=/profile/active.json,type=profile,crc32=%08lX\n"
                       "required=/mission/recipe.json,type=mission,crc32=%08lX\n"
                       "required=/cal/board_cal.json,type=calibration,crc32=%08lX\n"
                       "required=/update/RP2350_TRIG_UPDATE.pkg,type=ota_package,crc32=%08lX\n",
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
                       "    \"ota_default\": \"/update/RP2350_TRIG_UPDATE.pkg\"\n"
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
        status = storage_write_text_if_missing("/update/RP2350_TRIG_UPDATE.pkg",
                                               "/update/RP2350_TRIG_UPDATE.tmp",
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

bool storage_manager_raw_clear_prefix(uint32_t sector_count,
                                      uint32_t *cleared_count,
                                      sd_card_status_t *raw_status)
{
    if (cleared_count != NULL) {
        *cleared_count = 0u;
    }
    if (raw_status != NULL) {
        *raw_status = SD_CARD_STATUS_BAD_RESPONSE;
    }
    if (sector_count == 0u || sector_count > STORAGE_MANAGER_RAW_CLEAR_MAX_SECTORS) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
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

    s_storage_vector.card_status = status;
    s_storage_vector.card_present = info.present;
    s_storage_vector.card_type = info.type;
    s_storage_vector.high_capacity = info.high_capacity;
    s_storage_vector.block_count = info.block_count;
    s_storage_vector.capacity_kib = info.capacity_kib;
    s_storage_vector.probe_count++;
    s_storage_vector.last_probe_ms = storage_now_ms();

    if (status == SD_CARD_STATUS_OK && info.present) {
        uint8_t zero_sector[512];
        memset(zero_sector, 0, sizeof(zero_sector));
        for (uint32_t sector = 0u; sector < sector_count; sector++) {
            status = sd_card_write_blocks(sector, 1u, zero_sector);
            if (status != SD_CARD_STATUS_OK) {
                break;
            }
            if (cleared_count != NULL) {
                *cleared_count = sector + 1u;
            }
        }
    }

    storage_release_sd_resource();

    if (raw_status != NULL) {
        *raw_status = status;
    }
    s_storage_vector.fs_mounted = false;
    s_storage_vector.state = status == SD_CARD_STATUS_OK ?
                                 STORAGE_MANAGER_STATE_NO_FILESYSTEM :
                                 STORAGE_MANAGER_STATE_FAILED;
    s_storage_vector.storage_error = status == SD_CARD_STATUS_OK ?
                                         STORAGE_MANAGER_ERROR_NONE :
                                         STORAGE_MANAGER_ERROR_CARD;
    storage_reset_manifest_summary(STORAGE_MANAGER_MANIFEST_UNKNOWN);
    return status == SD_CARD_STATUS_OK;
}

bool storage_manager_raw_read_sector(uint32_t sector,
                                     uint8_t *buffer,
                                     size_t buffer_size,
                                     sd_card_status_t *raw_status)
{
    if (raw_status != NULL) {
        *raw_status = SD_CARD_STATUS_BAD_RESPONSE;
    }
    if (buffer == NULL || buffer_size < 512u) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
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
    }

    s_storage_vector.card_status = status;
    s_storage_vector.card_present = info.present;
    s_storage_vector.card_type = info.type;
    s_storage_vector.high_capacity = info.high_capacity;
    s_storage_vector.block_count = info.block_count;
    s_storage_vector.capacity_kib = info.capacity_kib;
    s_storage_vector.probe_count++;
    s_storage_vector.last_probe_ms = storage_now_ms();

    if (status == SD_CARD_STATUS_OK && info.present) {
        status = sd_card_read_blocks(sector, 1u, buffer);
    }

    storage_release_sd_resource();

    if (raw_status != NULL) {
        *raw_status = status;
    }
    s_storage_vector.storage_error = status == SD_CARD_STATUS_OK ?
                                         STORAGE_MANAGER_ERROR_NONE :
                                         STORAGE_MANAGER_ERROR_CARD;
    return status == SD_CARD_STATUS_OK;
}

bool storage_manager_format_volume(fatfs_port_status_t *format_status)
{
    if (format_status != NULL) {
        *format_status = FATFS_PORT_STATUS_FORMAT_FAILED;
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
    sd_card_status_t card_status = SD_CARD_STATUS_NOT_INITIALIZED;
    if (sd_card_init(&config)) {
        card_status = sd_card_probe(&info);
    } else {
        memset(&info, 0, sizeof(info));
    }

    s_storage_vector.card_status = card_status;
    s_storage_vector.card_present = info.present;
    s_storage_vector.card_type = info.type;
    s_storage_vector.high_capacity = info.high_capacity;
    s_storage_vector.block_count = info.block_count;
    s_storage_vector.capacity_kib = info.capacity_kib;
    s_storage_vector.fatfs_available = fatfs_port_is_available();
    s_storage_vector.probe_count++;
    s_storage_vector.last_probe_ms = storage_now_ms();

    fatfs_port_status_t status = FATFS_PORT_STATUS_FORMAT_FAILED;
    if (card_status == SD_CARD_STATUS_OK && info.present && s_storage_vector.fatfs_available) {
        status = fatfs_port_format_volume();
    }

    storage_release_sd_resource();

    if (format_status != NULL) {
        *format_status = status;
    }

    s_storage_vector.fs_mounted = status == FATFS_PORT_STATUS_OK;
    if (card_status != SD_CARD_STATUS_OK || !info.present) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_FAILED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_CARD;
    } else if (status == FATFS_PORT_STATUS_OK) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    } else {
        s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
    }
    storage_reset_manifest_summary(STORAGE_MANAGER_MANIFEST_UNKNOWN);
    return status == FATFS_PORT_STATUS_OK;
}

static bool storage_post_object_path_job(storage_manager_object_t object,
                                         storage_manager_job_type_t type,
                                         uint32_t offset,
                                         uint32_t length,
                                         uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    const storage_manager_write_contract_t *contract = storage_find_object_contract(object);
    if (contract == NULL) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = type;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    s_storage_job.offset = offset;
    s_storage_job.length = length > STORAGE_MANAGER_FILE_READ_MAX_BYTES ?
                               STORAGE_MANAGER_FILE_READ_MAX_BYTES :
                               length;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       contract->final_path);
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_file_delete_job(const char *path, uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        strcmp(normalized_path, "/") == 0) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FILE_DELETE;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

static bool storage_manager_post_rename_job(const char *old_path,
                                            const char *new_path,
                                            storage_manager_job_type_t type,
                                            bool is_dir,
                                            uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    char normalized_old_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    char normalized_new_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(old_path, normalized_old_path, sizeof(normalized_old_path)) ||
        !storage_normalize_path(new_path, normalized_new_path, sizeof(normalized_new_path)) ||
        strcmp(normalized_old_path, "/") == 0 ||
        strcmp(normalized_new_path, "/") == 0) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = type;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = is_dir;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_old_path);
    storage_copy_field(s_storage_job.target_path,
                       sizeof(s_storage_job.target_path),
                       normalized_new_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_old_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_file_rename_job(const char *old_path,
                                          const char *new_path,
                                          uint32_t *job_id)
{
    return storage_manager_post_rename_job(old_path,
                                           new_path,
                                           STORAGE_MANAGER_JOB_TYPE_FILE_RENAME,
                                           false,
                                           job_id);
}

static bool storage_manager_post_directory_job(const char *path,
                                               storage_manager_job_type_t type,
                                               uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        strcmp(normalized_path, "/") == 0) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = type;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = true;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_directory_create_job(const char *path, uint32_t *job_id)
{
    return storage_manager_post_directory_job(path,
                                              STORAGE_MANAGER_JOB_TYPE_DIRECTORY_CREATE,
                                              job_id);
}

bool storage_manager_post_directory_delete_job(const char *path, uint32_t *job_id)
{
    return storage_manager_post_directory_job(path,
                                              STORAGE_MANAGER_JOB_TYPE_DIRECTORY_DELETE,
                                              job_id);
}

bool storage_manager_post_directory_rename_job(const char *old_path,
                                               const char *new_path,
                                               uint32_t *job_id)
{
    return storage_manager_post_rename_job(old_path,
                                           new_path,
                                           STORAGE_MANAGER_JOB_TYPE_DIRECTORY_RENAME,
                                           true,
                                           job_id);
}

bool storage_manager_post_object_info_job(storage_manager_object_t object, uint32_t *job_id)
{
    return storage_post_object_path_job(object,
                                        STORAGE_MANAGER_JOB_TYPE_FILE_INFO,
                                        0u,
                                        0u,
                                        job_id);
}

bool storage_manager_post_object_read_job(storage_manager_object_t object,
                                          uint32_t offset,
                                          uint32_t length,
                                          uint32_t *job_id)
{
    if (length == 0u) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }
    return storage_post_object_path_job(object,
                                        STORAGE_MANAGER_JOB_TYPE_FILE_READ,
                                        offset,
                                        length,
                                        job_id);
}

bool storage_manager_post_object_delete_job(storage_manager_object_t object, uint32_t *job_id)
{
    return storage_post_object_path_job(object,
                                        STORAGE_MANAGER_JOB_TYPE_FILE_DELETE,
                                        0u,
                                        0u,
                                        job_id);
}

bool storage_manager_begin_file_write(const char *path,
                                      uint32_t expected_size,
                                      uint32_t expected_crc32,
                                      uint32_t *txn_id)
{
    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        strcmp(normalized_path, "/") == 0 ||
        expected_size == 0u ||
        expected_size > STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }
    if (s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_RECEIVING ||
        s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_READY ||
        s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_QUEUED ||
        s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_WRITING) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    char tmp_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_make_write_tmp_path(normalized_path, tmp_path, sizeof(tmp_path))) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }

    memset(&s_write_snapshot, 0, sizeof(s_write_snapshot));
    memset(s_write_buffer, 0, sizeof(s_write_buffer));
    s_write_snapshot.txn_id = s_next_write_txn_id++;
    if (s_next_write_txn_id == 0u) {
        s_next_write_txn_id = 1u;
    }
    s_write_snapshot.object = STORAGE_MANAGER_OBJECT_NONE;
    s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_RECEIVING;
    s_write_snapshot.expected_size = expected_size;
    s_write_snapshot.expected_crc32 = expected_crc32;
    s_write_snapshot.path_hash = storage_hash_path(normalized_path);
    storage_copy_field(s_write_snapshot.path,
                       sizeof(s_write_snapshot.path),
                       normalized_path);
    storage_copy_field(s_write_snapshot.tmp_path,
                       sizeof(s_write_snapshot.tmp_path),
                       tmp_path);
    if (txn_id != NULL) {
        *txn_id = s_write_snapshot.txn_id;
    }
    return true;
}

bool storage_manager_write_file_chunk(uint32_t txn_id,
                                      uint32_t offset,
                                      const uint8_t *data,
                                      size_t data_size)
{
    if (data == NULL ||
        data_size == 0u ||
        s_write_snapshot.txn_id != txn_id ||
        s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_RECEIVING ||
        offset != s_write_snapshot.received_size ||
        offset + data_size > s_write_snapshot.expected_size ||
        offset + data_size > sizeof(s_write_buffer)) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_SEQUENCE;
        return false;
    }

    memcpy(s_write_buffer + offset, data, data_size);
    s_write_snapshot.received_size = offset + (uint32_t)data_size;
    if (s_write_snapshot.received_size == s_write_snapshot.expected_size) {
        s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_READY;
    }
    return true;
}

bool storage_manager_commit_file_write(uint32_t txn_id, uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }
    if (s_write_snapshot.txn_id != txn_id ||
        s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_READY ||
        s_write_snapshot.received_size != s_write_snapshot.expected_size) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_SEQUENCE;
        return false;
    }

    s_write_snapshot.actual_crc32 =
        ota_crc32_compute(s_write_buffer, s_write_snapshot.received_size);
    if (s_write_snapshot.actual_crc32 != s_write_snapshot.expected_crc32) {
        s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_FAILED;
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FILE_WRITE;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.size = s_write_snapshot.expected_size;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       s_write_snapshot.path);
    s_storage_job.result.path_hash = s_write_snapshot.path_hash;
    s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_QUEUED;
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_abort_file_write(uint32_t txn_id)
{
    if (s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_IDLE &&
        s_write_snapshot.txn_id != txn_id) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_SEQUENCE;
        return false;
    }
    memset(s_write_buffer, 0, sizeof(s_write_buffer));
    s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_ABORTED;
    s_write_snapshot.error = STORAGE_MANAGER_ERROR_NONE;
    return true;
}

bool storage_manager_begin_object_write(storage_manager_object_t object,
                                        uint32_t expected_size,
                                        uint32_t expected_crc32,
                                        uint32_t *txn_id)
{
    const storage_manager_write_contract_t *contract = storage_find_object_contract(object);
    if (contract == NULL) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }
    const bool ok = storage_manager_begin_file_write(contract->final_path,
                                                    expected_size,
                                                    expected_crc32,
                                                    txn_id);
    if (ok) {
        s_write_snapshot.object = object;
        storage_copy_field(s_write_snapshot.tmp_path,
                           sizeof(s_write_snapshot.tmp_path),
                           contract->tmp_path);
    }
    return ok;
}

bool storage_manager_write_object_chunk(uint32_t txn_id,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        size_t data_size)
{
    return storage_manager_write_file_chunk(txn_id, offset, data, data_size);
}

bool storage_manager_commit_object_write(uint32_t txn_id, uint32_t *job_id)
{
    return storage_manager_commit_file_write(txn_id, job_id);
}

bool storage_manager_abort_object_write(uint32_t txn_id)
{
    return storage_manager_abort_file_write(txn_id);
}

void storage_manager_get_write_snapshot(storage_manager_write_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    *snapshot = s_write_snapshot;
}

bool storage_manager_post_file_info_job(const char *path, uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FILE_INFO;

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_file_read_job(const char *path,
                                        uint32_t offset,
                                        uint32_t length,
                                        uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FILE_READ;
    s_storage_job.offset = offset;
    s_storage_job.length = length > STORAGE_MANAGER_FILE_READ_MAX_BYTES ?
                               STORAGE_MANAGER_FILE_READ_MAX_BYTES :
                               length;

    if (s_storage_job.length == 0u) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_get_file_read_job_result(uint32_t job_id,
                                              storage_manager_file_read_t *read_info,
                                              uint8_t *buffer,
                                              size_t buffer_size)
{
    if (read_info == NULL ||
        s_storage_job.result.id != job_id ||
        s_storage_job.result.type != STORAGE_MANAGER_JOB_TYPE_FILE_READ ||
        s_storage_job.result.state != STORAGE_MANAGER_JOB_STATE_DONE) {
        return false;
    }

    *read_info = s_storage_job.read_info;
    if (buffer != NULL && buffer_size > 0u && s_storage_job.read_info.returned > 0u) {
        const size_t copy_size = s_storage_job.read_info.returned < buffer_size ?
                                     (size_t)s_storage_job.read_info.returned :
                                     buffer_size;
        memcpy(buffer, s_storage_job.read_buffer, copy_size);
    }
    return true;
}

bool storage_manager_post_catalog_page_job(const char *path,
                                           uint32_t offset,
                                           uint32_t limit,
                                           uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE;
    s_storage_job.offset = offset;
    s_storage_job.limit = limit;

    if (limit == 0u) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_copy_field(s_storage_job.catalog_buffer,
                           sizeof(s_storage_job.catalog_buffer),
                           "PATH_DENIED");
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = true;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_get_catalog_page_job_result(uint32_t job_id,
                                                 storage_manager_catalog_page_t *page,
                                                 char *buffer,
                                                 size_t buffer_size)
{
    if (page == NULL ||
        s_storage_job.result.id != job_id ||
        s_storage_job.result.type != STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE ||
        s_storage_job.result.state != STORAGE_MANAGER_JOB_STATE_DONE) {
        return false;
    }

    *page = s_storage_job.catalog_page;
    storage_copy_field(buffer, buffer_size, s_storage_job.catalog_buffer);
    return true;
}

bool storage_manager_post_snapshot_job(const char *kind, uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    const char *snapshot_kind = (kind == NULL || kind[0] == '\0') ? "boot" : kind;
    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_SNAPSHOT_WRITE;

    if (!storage_snapshot_kind_is_valid(snapshot_kind)) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH;
        storage_copy_field(s_storage_job.argument,
                           sizeof(s_storage_job.argument),
                           snapshot_kind);
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           snapshot_kind);
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    storage_copy_field(s_storage_job.argument,
                       sizeof(s_storage_job.argument),
                       snapshot_kind);
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       snapshot_kind);
    s_storage_job.result.path_hash = storage_hash_path(snapshot_kind);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_manifest_scan_job(uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_MANIFEST_SCAN;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       "/manifest.idx");
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_system_init_job(uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_SYSTEM_INIT;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       "/manifest.idx");
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_fault_evidence_job(uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FAULT_EVIDENCE;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       "/reports/fault");
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

void storage_manager_get_job_result(storage_manager_job_result_t *result)
{
    if (result == NULL) {
        return;
    }
    *result = s_storage_job.result;
}

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
                                                        s_storage_job.read_buffer,
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
            fatfs_port_status_t status = fatfs_port_write_binary_file_atomic(s_write_snapshot.path,
                                                                             s_write_snapshot.tmp_path,
                                                                             s_write_buffer,
                                                                             s_write_snapshot.received_size);
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
        status = fatfs_port_make_directory("/logs/run");
    }
    if (status != FATFS_PORT_STATUS_OK) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        storage_publish_runtime_log_result(0u, "", 0u, STORAGE_MANAGER_ERROR_WRITE_FAILED);
        storage_publish_runtime_log_queue_status();
        return false;
    }

    uint32_t max_sequence = 0u;
    status = fatfs_port_find_max_sequence("/logs/run", "run_", ".log", &max_sequence);
    if (status != FATFS_PORT_STATUS_OK || max_sequence >= 999999u) {
        storage_release_sd_resource();
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_SEQUENCE;
        storage_publish_runtime_log_result(0u, "", 0u, STORAGE_MANAGER_ERROR_SEQUENCE);
        storage_publish_runtime_log_queue_status();
        return false;
    }

    const uint32_t sequence = max_sequence + 1u;
    char final_path[96];
    char tmp_path[96];
    (void)snprintf(final_path,
                   sizeof(final_path),
                   "/logs/run/run_%06lu.log",
                   (unsigned long)sequence);
    (void)snprintf(tmp_path, sizeof(tmp_path), "/logs/run/run.tmp");

    status = fatfs_port_write_text_file_atomic(final_path, tmp_path, payload);
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
