#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fatfs_port.h"
#include "sd_card.h"

#define STORAGE_MANAGER_FILE_READ_MAX_BYTES 4096u
#define STORAGE_MANAGER_FILE_WRITE_MAX_BYTES 16384u

typedef enum {
    STORAGE_MANAGER_STATE_UNINITIALIZED = 0,
    STORAGE_MANAGER_STATE_IDLE,
    STORAGE_MANAGER_STATE_CARD_READY,
    STORAGE_MANAGER_STATE_NO_CARD,
    STORAGE_MANAGER_STATE_NO_FILESYSTEM,
    STORAGE_MANAGER_STATE_PATH_DENIED,
    STORAGE_MANAGER_STATE_PATH_ERROR,
    STORAGE_MANAGER_STATE_FAILED,
} storage_manager_state_t;

typedef enum {
    STORAGE_MANAGER_MANIFEST_UNKNOWN = 0,
    STORAGE_MANAGER_MANIFEST_OK,
    STORAGE_MANAGER_MANIFEST_NOT_FOUND,
    STORAGE_MANAGER_MANIFEST_INVALID,
    STORAGE_MANAGER_MANIFEST_SCHEMA_UNSUPPORTED,
    STORAGE_MANAGER_MANIFEST_PRODUCT_MISMATCH,
    STORAGE_MANAGER_MANIFEST_HARDWARE_MISMATCH,
    STORAGE_MANAGER_MANIFEST_REQUIRED_MISSING,
    STORAGE_MANAGER_MANIFEST_IO_ERROR,
    STORAGE_MANAGER_MANIFEST_PATH_DENIED,
} storage_manager_manifest_status_t;

typedef enum {
    STORAGE_MANAGER_JOB_TYPE_NONE = 0,
    STORAGE_MANAGER_JOB_TYPE_FILE_INFO,
    STORAGE_MANAGER_JOB_TYPE_FILE_READ,
    STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE,
    STORAGE_MANAGER_JOB_TYPE_SNAPSHOT_WRITE,
    STORAGE_MANAGER_JOB_TYPE_MANIFEST_SCAN,
    STORAGE_MANAGER_JOB_TYPE_FAULT_EVIDENCE,
    STORAGE_MANAGER_JOB_TYPE_SYSTEM_INIT,
    STORAGE_MANAGER_JOB_TYPE_FILE_WRITE,
    STORAGE_MANAGER_JOB_TYPE_FILE_DELETE,
    STORAGE_MANAGER_JOB_TYPE_FILE_RENAME,
    STORAGE_MANAGER_JOB_TYPE_DIRECTORY_CREATE,
    STORAGE_MANAGER_JOB_TYPE_DIRECTORY_DELETE,
    STORAGE_MANAGER_JOB_TYPE_DIRECTORY_RENAME,
} storage_manager_job_type_t;

typedef enum {
    STORAGE_MANAGER_JOB_STATE_IDLE = 0,
    STORAGE_MANAGER_JOB_STATE_QUEUED,
    STORAGE_MANAGER_JOB_STATE_RUNNING,
    STORAGE_MANAGER_JOB_STATE_DONE,
    STORAGE_MANAGER_JOB_STATE_FAILED,
} storage_manager_job_state_t;

typedef struct {
    storage_manager_state_t state;
    bool card_present;
    bool fs_mounted;
    bool fatfs_available;
    sd_card_type_t card_type;
    bool high_capacity;
    uint32_t block_count;
    uint32_t capacity_kib;
    uint32_t probe_count;
    uint32_t last_probe_ms;
    sd_card_status_t card_status;
    uint32_t storage_error;
    storage_manager_manifest_status_t manifest_status;
    uint32_t manifest_schema;
    uint32_t manifest_required_count;
    uint32_t manifest_missing_count;
    char manifest_product_id[32];
    char manifest_hardware_id[32];
    char manifest_build_id[32];
    uint32_t last_snapshot_id;
    uint32_t last_snapshot_path_hash;
    uint32_t last_snapshot_error;
    char last_snapshot_kind[16];
    char last_snapshot_path[96];
    uint32_t last_trace_id;
    uint32_t last_trace_path_hash;
    uint32_t last_trace_error;
    uint32_t last_trace_event_count;
    char last_trace_kind[16];
    char last_trace_path[96];
    uint32_t last_log_id;
    uint32_t last_log_path_hash;
    uint32_t last_log_error;
    uint32_t last_log_bytes;
    uint32_t log_segment_count;
    uint32_t log_flushed_bytes;
    uint32_t log_pending_bytes;
    uint32_t log_dropped_count;
    uint32_t log_dropped_bytes;
    uint32_t log_attempt_error;
    char last_log_path[96];
    uint32_t last_fault_report_id;
    uint32_t last_fault_report_path_hash;
    uint32_t last_fault_report_error;
    uint32_t last_fault_snapshot_id;
    uint32_t last_fault_trace_id;
    char last_fault_report_path[96];
    uint32_t current_job_id;
    storage_manager_job_type_t current_job_type;
    storage_manager_job_state_t current_job_state;
    uint32_t job_path_hash;
    uint32_t job_total_bytes;
    uint32_t job_done_bytes;
    uint32_t job_error;
    char job_path[96];
} storage_manager_vector_t;

typedef struct {
    uint32_t size;
    bool is_dir;
    uint32_t path_hash;
    char path[96];
} storage_manager_file_info_t;

typedef struct {
    uint32_t returned_count;
    uint32_t next_offset;
    bool complete;
    bool truncated;
    uint32_t path_hash;
    char path[96];
} storage_manager_catalog_page_t;

typedef struct {
    uint32_t offset;
    uint32_t requested;
    uint32_t returned;
    uint32_t file_size;
    bool eof;
    uint32_t path_hash;
    char path[96];
} storage_manager_file_read_t;

typedef struct {
    uint32_t id;
    storage_manager_job_type_t type;
    storage_manager_job_state_t state;
    uint32_t path_hash;
    uint32_t error;
    uint32_t size;
    bool is_dir;
    char path[96];
} storage_manager_job_result_t;

typedef enum {
    STORAGE_MANAGER_OBJECT_NONE = 0,
    STORAGE_MANAGER_OBJECT_REFMEM_PACKAGE = 1,
} storage_manager_object_t;

typedef enum {
    STORAGE_MANAGER_WRITE_STATE_IDLE = 0,
    STORAGE_MANAGER_WRITE_STATE_RECEIVING,
    STORAGE_MANAGER_WRITE_STATE_READY,
    STORAGE_MANAGER_WRITE_STATE_QUEUED,
    STORAGE_MANAGER_WRITE_STATE_WRITING,
    STORAGE_MANAGER_WRITE_STATE_DONE,
    STORAGE_MANAGER_WRITE_STATE_FAILED,
    STORAGE_MANAGER_WRITE_STATE_ABORTED,
} storage_manager_write_state_t;

typedef struct {
    uint32_t txn_id;
    storage_manager_object_t object;
    storage_manager_write_state_t state;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t expected_crc32;
    uint32_t actual_crc32;
    uint32_t path_hash;
    uint32_t error;
    bool direct_write;
    char path[96];
    char tmp_path[96];
} storage_manager_write_snapshot_t;

bool storage_manager_init(void);
bool storage_manager_probe(void);
bool storage_manager_catalog(const char *path, char *buffer, size_t buffer_size);
bool storage_manager_catalog_page(const char *path,
                                  uint32_t offset,
                                  uint32_t limit,
                                  char *buffer,
                                  size_t buffer_size,
                                  storage_manager_catalog_page_t *page);
bool storage_manager_file_info(const char *path, storage_manager_file_info_t *info);
bool storage_manager_read_file_range(const char *path,
                                     uint32_t offset,
                                     uint8_t *buffer,
                                     size_t buffer_size,
                                     storage_manager_file_read_t *read_info);
bool storage_manager_scan_manifest(void);
bool storage_manager_initialize_system_pack(void);
bool storage_manager_post_object_info_job(storage_manager_object_t object, uint32_t *job_id);
bool storage_manager_post_object_read_job(storage_manager_object_t object,
                                          uint32_t offset,
                                          uint32_t length,
                                          uint32_t *job_id);
bool storage_manager_post_object_delete_job(storage_manager_object_t object, uint32_t *job_id);
bool storage_manager_post_file_delete_job(const char *path, uint32_t *job_id);
bool storage_manager_post_file_rename_job(const char *old_path,
                                          const char *new_path,
                                          uint32_t *job_id);
bool storage_manager_post_directory_create_job(const char *path, uint32_t *job_id);
bool storage_manager_post_directory_delete_job(const char *path, uint32_t *job_id);
bool storage_manager_post_directory_rename_job(const char *old_path,
                                               const char *new_path,
                                               uint32_t *job_id);
bool storage_manager_begin_file_write(const char *path,
                                      uint32_t expected_size,
                                      uint32_t expected_crc32,
                                      uint32_t *txn_id);
bool storage_manager_begin_evidence_write(const char *path,
                                          uint32_t expected_size,
                                          uint32_t expected_crc32,
                                          uint32_t *txn_id);
bool storage_manager_write_file_chunk(uint32_t txn_id,
                                      uint32_t offset,
                                      const uint8_t *data,
                                      size_t data_size);
bool storage_manager_commit_file_write(uint32_t txn_id, uint32_t *job_id);
bool storage_manager_abort_file_write(uint32_t txn_id);
bool storage_manager_begin_object_write(storage_manager_object_t object,
                                        uint32_t expected_size,
                                        uint32_t expected_crc32,
                                        uint32_t *txn_id);
bool storage_manager_write_object_chunk(uint32_t txn_id,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        size_t data_size);
bool storage_manager_commit_object_write(uint32_t txn_id, uint32_t *job_id);
bool storage_manager_abort_object_write(uint32_t txn_id);
void storage_manager_get_write_snapshot(storage_manager_write_snapshot_t *snapshot);
bool storage_manager_raw_clear_prefix(uint32_t sector_count,
                                      uint32_t *cleared_count,
                                      sd_card_status_t *raw_status);
bool storage_manager_raw_read_sector(uint32_t sector, uint8_t *buffer, size_t buffer_size, sd_card_status_t *raw_status);
bool storage_manager_format_volume(fatfs_port_status_t *format_status);
bool storage_manager_post_file_info_job(const char *path, uint32_t *job_id);
bool storage_manager_post_file_read_job(const char *path,
                                        uint32_t offset,
                                        uint32_t length,
                                        uint32_t *job_id);
bool storage_manager_get_file_read_job_result(uint32_t job_id,
                                              storage_manager_file_read_t *read_info,
                                              uint8_t *buffer,
                                              size_t buffer_size);
bool storage_manager_acquire_file_read_job_result(
    uint32_t job_id,
    storage_manager_file_read_t *read_info);
bool storage_manager_copy_file_read_job_result(
    uint32_t job_id,
    uint32_t result_offset,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *copied_size);
void storage_manager_release_file_read_job_result(uint32_t job_id);
bool storage_manager_post_catalog_page_job(const char *path,
                                           uint32_t offset,
                                           uint32_t limit,
                                           uint32_t *job_id);
bool storage_manager_get_catalog_page_job_result(uint32_t job_id,
                                                 storage_manager_catalog_page_t *page,
                                                 char *buffer,
                                                 size_t buffer_size);
bool storage_manager_post_snapshot_job(const char *kind, uint32_t *job_id);
bool storage_manager_post_manifest_scan_job(uint32_t *job_id);
bool storage_manager_post_fault_evidence_job(uint32_t *job_id);
bool storage_manager_post_system_init_job(uint32_t *job_id);
void storage_manager_get_job_result(storage_manager_job_result_t *result);
bool storage_manager_write_snapshot(const char *kind);
void storage_manager_trace_event(uint8_t domain,
                                 uint16_t event_id,
                                 uint8_t severity,
                                 uint32_t arg0,
                                 uint32_t arg1);
bool storage_manager_write_trace(const char *kind);
bool storage_manager_write_fault_report(void);
void storage_manager_service(uint32_t budget_us);
void storage_manager_get_vector(storage_manager_vector_t *vector);
const char *storage_manager_state_string(storage_manager_state_t state);
const char *storage_manager_manifest_status_string(storage_manager_manifest_status_t status);
const char *storage_manager_job_type_string(storage_manager_job_type_t type);
const char *storage_manager_job_state_string(storage_manager_job_state_t state);
const char *storage_manager_write_state_string(storage_manager_write_state_t state);
const char *storage_manager_object_string(storage_manager_object_t object);

#endif
