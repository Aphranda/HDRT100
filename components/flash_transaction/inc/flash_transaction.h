#ifndef FLASH_TRANSACTION_H
#define FLASH_TRANSACTION_H

#include <stdbool.h>
#include <stdint.h>

#include "flash_deployment_map.h"

typedef enum {
    FLASH_TRANSACTION_REQUESTER_NONE = 0,
    FLASH_TRANSACTION_REQUESTER_OTA_IMAGE = 1,
    FLASH_TRANSACTION_REQUESTER_OTA_METADATA = 2,
    FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG = 3,
    FLASH_TRANSACTION_REQUESTER_VALIDATION = 4,
} flash_transaction_requester_t;

typedef enum {
    FLASH_TRANSACTION_OPERATION_NONE = 0,
    FLASH_TRANSACTION_OPERATION_ERASE = 1,
    FLASH_TRANSACTION_OPERATION_PROGRAM = 2,
} flash_transaction_operation_t;

typedef enum {
    FLASH_TRANSACTION_STATE_IDLE = 0,
    FLASH_TRANSACTION_STATE_VALIDATE,
    FLASH_TRANSACTION_STATE_QUIESCE,
    FLASH_TRANSACTION_STATE_ACQUIRE,
    FLASH_TRANSACTION_STATE_PARK_CORE1,
    FLASH_TRANSACTION_STATE_ERASE_PROGRAM,
    FLASH_TRANSACTION_STATE_VERIFY,
    FLASH_TRANSACTION_STATE_COMMIT,
    FLASH_TRANSACTION_STATE_RELEASE,
    FLASH_TRANSACTION_STATE_COMPLETE,
    FLASH_TRANSACTION_STATE_FAILED,
    FLASH_TRANSACTION_STATE_ABORTED,
} flash_transaction_state_t;

typedef enum {
    FLASH_TRANSACTION_COMPLETION_NONE = 0,
    FLASH_TRANSACTION_COMPLETION_ACCEPTED,
    FLASH_TRANSACTION_COMPLETION_PROGRAMMED,
    FLASH_TRANSACTION_COMPLETION_VERIFIED,
    FLASH_TRANSACTION_COMPLETION_COMMITTED,
} flash_transaction_completion_level_t;

typedef enum {
    FLASH_TRANSACTION_RESULT_NONE = 0,
    FLASH_TRANSACTION_RESULT_COMMITTED,
    FLASH_TRANSACTION_RESULT_FAILED,
    FLASH_TRANSACTION_RESULT_ABORTED,
} flash_transaction_result_t;

typedef enum {
    FLASH_TRANSACTION_ERROR_NONE = 0,
    FLASH_TRANSACTION_ERROR_BUSY,
    FLASH_TRANSACTION_ERROR_BAD_ARGUMENT,
    FLASH_TRANSACTION_ERROR_PERMISSION,
    FLASH_TRANSACTION_ERROR_RANGE,
    FLASH_TRANSACTION_ERROR_ALIGNMENT,
    FLASH_TRANSACTION_ERROR_ACTIVE_UNKNOWN,
    FLASH_TRANSACTION_ERROR_ACTIVE_PARTITION,
    FLASH_TRANSACTION_ERROR_PROVIDER,
    FLASH_TRANSACTION_ERROR_POLICY,
    FLASH_TRANSACTION_ERROR_RESOURCE,
    FLASH_TRANSACTION_ERROR_RAW_OPERATION,
    FLASH_TRANSACTION_ERROR_VERIFY,
    FLASH_TRANSACTION_ERROR_ABORTED,
    FLASH_TRANSACTION_ERROR_THERMAL,
    FLASH_TRANSACTION_ERROR_DIAGNOSTICS_FAULT,
    FLASH_TRANSACTION_ERROR_TRIGGER_ACTIVE,
    FLASH_TRANSACTION_ERROR_MODE,
    FLASH_TRANSACTION_ERROR_PARK,
    FLASH_TRANSACTION_ERROR_RELEASE,
    FLASH_TRANSACTION_ERROR_CALIBRATION_ACTIVE,
    FLASH_TRANSACTION_ERROR_TDMA_TRAINING_ACTIVE,
    FLASH_TRANSACTION_ERROR_COMPLETION,
} flash_transaction_error_t;

typedef struct {
    const uint8_t *data;
    uint32_t length;
    uint32_t generation;
    void *context;
    bool (*retain)(void *context);
    void (*release)(void *context);
} flash_transaction_buffer_lease_t;

typedef enum {
    FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED = 1,
    FLASH_TRANSACTION_JOURNAL_EVENT_PROGRAMMED,
    FLASH_TRANSACTION_JOURNAL_EVENT_VERIFIED,
    FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED,
    FLASH_TRANSACTION_JOURNAL_EVENT_FAILED,
    FLASH_TRANSACTION_JOURNAL_EVENT_ABORTED,
} flash_transaction_journal_event_t;

typedef struct {
    uint32_t job_id;
    uint32_t transaction_generation;
    uint32_t provider_generation;
    uint32_t store_generation;
    uint32_t event;
    uint32_t result;
    uint32_t error;
    uint32_t processed_bytes;
    uint32_t verified_bytes;
} flash_transaction_journal_record_t;

typedef struct {
    void *context;
    bool (*retain)(void *context);
    void (*release)(void *context);
    bool (*append)(void *context,
                   const flash_transaction_journal_record_t *record);
} flash_transaction_completion_lease_t;

typedef struct {
    uint32_t job_id;
    uint32_t requester;
    uint32_t partition_id;
    uint32_t operation;
    uint32_t relative_offset;
    uint32_t length;
    const uint8_t *data;
    uint32_t provider_generation;
    uint32_t store_generation;
    const flash_transaction_buffer_lease_t *buffer_lease;
    const flash_transaction_completion_lease_t *completion_lease;
} flash_transaction_request_t;

typedef struct {
    uint32_t job_id;
    uint32_t level;
    uint32_t result;
    uint32_t error;
    uint32_t processed_bytes;
    uint32_t verified_bytes;
    uint32_t transaction_generation;
} flash_transaction_completion_t;

typedef struct {
    uint32_t guard;
    uint32_t state;
    uint32_t job_id;
    uint32_t requester;
    uint32_t partition_id;
    uint32_t operation;
    uint32_t requested_bytes;
    uint32_t processed_bytes;
    uint32_t verified_bytes;
    uint32_t map_version;
    uint32_t provider_generation;
    uint32_t store_generation;
    uint32_t transaction_generation;
    uint32_t completion_level;
    uint32_t last_result;
    uint32_t last_error;
    uint32_t retry_count;
    uint32_t abort_pending;
    uint32_t lockout_request_seq;
    uint32_t lockout_ack_seq;
    uint32_t lockout_timeout_count;
    uint32_t erase_count_delta;
    uint32_t program_count_delta;
    uint32_t verify_failure_count;
    uint32_t temperature_flags;
    uint32_t policy_gate_reason;
    uint32_t started_timestamp_ms;
    uint32_t completed_timestamp_ms;
} flash_transaction_vector_t;

bool flash_transaction_ao_init(void);
/* Process-lifetime completion journal lease owned by FlashTransactionAO. */
bool flash_transaction_ao_set_completion_lease(
    const flash_transaction_completion_lease_t *lease);
const flash_transaction_completion_lease_t *
flash_transaction_ao_get_completion_lease(void);
bool flash_transaction_ao_set_active_app_partition(uint32_t partition_id);
bool flash_transaction_ao_resolve_range(uint32_t absolute_offset,
                                        uint32_t length,
                                        uint32_t *partition_id,
                                        uint32_t *relative_offset);
bool flash_transaction_ao_submit(const flash_transaction_request_t *request);
void flash_transaction_ao_service(void);
bool flash_transaction_ao_request_abort(uint32_t job_id);
bool flash_transaction_ao_notify_provider_reset(uint32_t provider_generation);
bool flash_transaction_ao_execute(const flash_transaction_request_t *request,
                                  flash_transaction_completion_t *completion);
bool flash_transaction_ao_get_vector(flash_transaction_vector_t *vector);

#endif
