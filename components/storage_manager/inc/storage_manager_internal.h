#ifndef STORAGE_MANAGER_INTERNAL_H
#define STORAGE_MANAGER_INTERNAL_H

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
#define STORAGE_MANAGER_RUNTIME_LOG_SLOT_COUNT 128u
#define STORAGE_MANAGER_RUNTIME_LOG_DIRECTORY "/logs/runtime"
#define STORAGE_MANAGER_RUNTIME_LOG_CURSOR_PATH "/logs/runtime/cursor.idx"
#define STORAGE_MANAGER_RUNTIME_LOG_CURSOR_TMP_PATH "/logs/runtime/cursor.tmp"
#define STORAGE_MANAGER_OBJECT_WRITE_MAX_BYTES 8192u
#define STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES \
    STORAGE_MANAGER_FILE_WRITE_MAX_BYTES
#define STORAGE_MANAGER_CATALOG_PAGE_MAX_BYTES 384u
#define STORAGE_MANAGER_RAW_CLEAR_MAX_SECTORS 64u
#define STORAGE_MANAGER_BOOT_SNAPSHOT_DELAY_MS 500u
#define STORAGE_MANAGER_TRACE_RING_COUNT 64u
#define STORAGE_MANAGER_TRACE_MAGIC 0x43525452u
#define STORAGE_MANAGER_TRACE_SCHEMA 1u
#define STORAGE_MANAGER_TRACE_TICK_HZ 1000u
#define STORAGE_MANAGER_PRODUCT_ID PROJECT_PRODUCT_ID
#define STORAGE_MANAGER_HARDWARE_ID PROJECT_HARDWARE_ID
#define STORAGE_MANAGER_BOOTSTRAP_OTA_TEXT "DHRT100 placeholder OTA package. Replace with a release package before offline OTA.\n"

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

/* N5.0.4g: single definitions live in storage_manager.c. */
extern storage_manager_vector_t s_storage_vector;
extern bool s_boot_snapshot_pending;
extern uint32_t s_boot_snapshot_due_ms;
extern storage_trace_record_t s_trace_ring[STORAGE_MANAGER_TRACE_RING_COUNT];
extern uint32_t s_trace_next;
extern uint32_t s_trace_count;
extern storage_manager_job_t s_storage_job;
extern bool s_file_read_result_leased;
extern uint32_t s_next_job_id;
extern uint8_t s_boot_snapshot_step;
extern storage_manager_write_snapshot_t s_write_snapshot;
extern uint8_t s_write_buffer[STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES];
extern uint32_t s_next_write_txn_id;
extern uint32_t s_runtime_log_last_attempt_ms;
extern uint32_t s_runtime_log_next_sequence;
extern bool s_runtime_log_sequence_initialized;
uint32_t storage_now_ms(void);
uint64_t storage_now_us(void);
bool storage_budget_elapsed(uint64_t start_us, uint32_t budget_us);
uint32_t storage_hash_path(const char *path);
const storage_manager_write_contract_t *storage_find_object_contract(storage_manager_object_t object);
void storage_copy_field(char *destination, size_t destination_size, const char *source);
void storage_publish_job_result(void);
void storage_job_complete_manifest_scan(bool ok);
void storage_job_complete_fault_evidence(void);
bool storage_job_is_active(void);
bool storage_write_buffer_is_active(void);
bool storage_error_is_retryable(uint32_t error);
bool storage_acquire_sd_resource(void);
void storage_release_sd_resource(void);
bool storage_normalize_path(const char *input, char *output, size_t output_size);
bool storage_make_write_tmp_path(const char *final_path, char *tmp_path, size_t tmp_path_size);
bool storage_parse_u32(const char *text, uint32_t *value);
void storage_reset_manifest_summary(storage_manager_manifest_status_t status);
bool storage_snapshot_kind_is_valid(const char *kind);
void storage_publish_snapshot_result(const char *kind, uint32_t sequence, const char *path, uint32_t error);
void storage_publish_trace_result(const char *kind, uint32_t sequence, const char *path, uint32_t event_count, uint32_t error);
void storage_publish_runtime_log_result(uint32_t sequence, const char *path, uint32_t byte_count, uint32_t error);
void storage_publish_runtime_log_queue_status(void);
void storage_publish_fault_report_result(uint32_t sequence, const char *path, uint32_t error);
bool storage_trace_kind_is_valid(const char *kind);
bool storage_manifest_check_required(const char *required_entry);

#endif /* STORAGE_MANAGER_INTERNAL_H */

