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
