#ifndef FLASH_TRANSACTION_FB_H
#define FLASH_TRANSACTION_FB_H

#include "flash_transaction.h"

#define FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE \
    (FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES * 2u)

typedef struct {
    bool (*policy_allows)(uint32_t requester);
    uint32_t (*policy_check)(uint32_t requester, uint32_t *temperature_flags);
    bool (*acquire_flash)(void);
    void (*release_flash)(void);
    bool (*park_core1)(void);
    bool (*release_core1)(void);
    bool (*erase)(uint32_t absolute_offset, uint32_t length);
    bool (*program)(uint32_t absolute_offset, const uint8_t *data,
                    uint32_t length);
    bool (*verify_erased)(uint32_t absolute_offset, uint32_t length);
    bool (*verify_programmed)(uint32_t absolute_offset, const uint8_t *data,
                              uint32_t length);
    void (*get_lockout)(uint32_t *request_seq, uint32_t *ack_seq,
                        uint32_t *timeout_count);
    uint32_t (*now_ms)(void);
    void (*step_hook)(void *context, uint32_t state);
    void *step_hook_context;
} flash_transaction_platform_t;

typedef struct {
    flash_transaction_platform_t platform;
    flash_transaction_request_t request;
    uint8_t owned_payload[FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE];
    flash_transaction_vector_t vector;
    uint32_t active_app_partition_id;
    uint32_t absolute_offset;
    uint32_t next_job_id;
    uint32_t terminal_state;
    uint32_t last_terminal_job_id;
    bool last_terminal_valid;
    bool occupied;
    bool resource_acquired;
    bool core1_parked;
    bool payload_owned;
    bool provider_reset_pending;
    const flash_transaction_buffer_lease_t *buffer_lease;
    bool provider_retained;
    const flash_transaction_completion_lease_t *completion_lease;
    bool completion_retained;
    bool completion_terminal_published;
    bool completion_journal_failed;
} flash_transaction_fb_t;

void flash_transaction_fb_init(flash_transaction_fb_t *context,
                               const flash_transaction_platform_t *platform);
bool flash_transaction_fb_set_active_app_partition(
    flash_transaction_fb_t *context, uint32_t partition_id);
bool flash_transaction_fb_resolve_range(uint32_t absolute_offset,
                                        uint32_t length,
                                        uint32_t *partition_id,
                                        uint32_t *relative_offset);
bool flash_transaction_fb_submit(flash_transaction_fb_t *context,
                                 const flash_transaction_request_t *request);
void flash_transaction_fb_service(flash_transaction_fb_t *context);
bool flash_transaction_fb_request_abort(flash_transaction_fb_t *context,
                                        uint32_t job_id);
bool flash_transaction_fb_notify_provider_reset(
    flash_transaction_fb_t *context, uint32_t provider_generation);
bool flash_transaction_fb_get_vector(const flash_transaction_fb_t *context,
                                     flash_transaction_vector_t *vector);

#endif
