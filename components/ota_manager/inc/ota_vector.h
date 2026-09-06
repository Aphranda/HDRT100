#ifndef OTA_VECTOR_H
#define OTA_VECTOR_H

#include <stdint.h>

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECK_PERMISSION,
    OTA_STATE_ERASE_SLOT,
    OTA_STATE_RECEIVING,
    OTA_STATE_VERIFYING,
    OTA_STATE_MARK_PENDING,
    OTA_STATE_READY_TO_REBOOT,
    OTA_STATE_PENDING_CONFIRM,
    OTA_STATE_COMMITTED,
    OTA_STATE_FAILED,
    OTA_STATE_ABORTED,
} ota_state_t;

typedef enum {
    OTA_RESULT_NONE = 0,
    OTA_RESULT_ACCEPTED,
    OTA_RESULT_IMAGE_STAGED,
    OTA_RESULT_ABORTED,
    OTA_RESULT_FAILED,
    OTA_RESULT_COMMITTED,
} ota_result_t;

/* Retained breadcrumbs for diagnosing resets during END finalization. */
typedef enum {
    OTA_TRACE_PHASE_NONE = 0u,
    OTA_TRACE_PHASE_END_BEGIN = 1u,
    OTA_TRACE_PHASE_END_ACCEPTED = 2u,
    OTA_TRACE_PHASE_VERIFY_BEGIN = 3u,
    OTA_TRACE_PHASE_VERIFY_DONE = 4u,
    OTA_TRACE_PHASE_VERIFY_FAILED = 5u,
    OTA_TRACE_PHASE_MANIFEST_BEGIN = 6u,
    OTA_TRACE_PHASE_MANIFEST_DONE = 7u,
    OTA_TRACE_PHASE_MANIFEST_FAILED = 8u,
    OTA_TRACE_PHASE_MARK_PENDING_BEGIN = 9u,
    OTA_TRACE_PHASE_MARK_PENDING_DONE = 10u,
    OTA_TRACE_PHASE_MARK_PENDING_FAILED = 11u,
    OTA_TRACE_PHASE_READY_TO_REBOOT = 12u,
    OTA_TRACE_PHASE_MARK_PENDING_LOAD = 13u,
    OTA_TRACE_PHASE_MARK_PENDING_BCB_INIT = 14u,
    OTA_TRACE_PHASE_MARK_PENDING_BCB_SELECT = 15u,
    OTA_TRACE_PHASE_MARK_PENDING_TXN_BEGIN = 16u,
    OTA_TRACE_PHASE_MARK_PENDING_TXN_STEP = 17u,
    OTA_TRACE_PHASE_MARK_PENDING_TXN_DONE = 18u,
    OTA_TRACE_PHASE_MARK_PENDING_TXN_FAILED = 19u,
    OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_INIT = 28u,
    OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_SELECT = 29u,
    OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_SELECT_DONE = 30u,
    OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_COPY = 31u,
    OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_VALID = 32u,
    OTA_TRACE_PHASE_MARK_PENDING_MUTATE_DONE = 33u,
    OTA_TRACE_PHASE_MARK_PENDING_STORE_INIT_DONE = 34u,
} ota_trace_phase_t;

typedef struct {
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t state;
    uint32_t target_slot;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t programmed_size;
    uint32_t crc32_expected;
    uint32_t crc32_running;
    uint32_t image_version;
    uint32_t progress_permille;
    uint32_t boot_flags_summary;
    uint32_t error_code;
    uint32_t last_event;
    uint32_t last_result;
} ota_vector_t;

const char *ota_state_to_string(ota_state_t state);
const char *ota_error_to_string(uint32_t error_code);
const char *ota_result_to_string(ota_result_t result);

#endif
