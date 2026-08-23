#ifndef POTA_CORE_H
#define POTA_CORE_H

#include "pota_package.h"
#include "pota_platform.h"

typedef struct {
    pota_platform_t platform;
    pota_status_t status;
    bool package_mode;
    bool package_header_received;
    pota_slot_t target_slot;
    uint32_t target_offset;
    uint32_t target_run_offset;
    uint32_t target_erase_size;
    uint32_t target_erase_offset;
    uint32_t selected_image_offset;
    uint32_t selected_image_size;
    uint32_t selected_image_crc32;
    uint32_t selected_security_counter;
    uint32_t selected_image_crc32_running;
    uint32_t selected_image_received_size;
    bool raw_resume_active;
    bool raw_resume_erasing_tail;
    uint32_t raw_resume_durable_offset;
    uint32_t raw_resume_durable_crc32;
    uint32_t raw_resume_scan_offset;
    uint32_t raw_resume_scan_crc32;
} pota_context_t;

typedef struct {
    uint32_t size;
    uint32_t crc32;
    bool package_mode;
} pota_begin_t;

typedef struct {
    const uint8_t *data;
    uint32_t size;
} pota_write_t;

bool pota_init(pota_context_t *context, const pota_platform_t *platform);
pota_error_t pota_begin(pota_context_t *context, const pota_begin_t *begin);
pota_error_t pota_resume_raw(pota_context_t *context,
                             const pota_begin_t *begin,
                             uint32_t durable_offset,
                             uint32_t durable_crc32);
pota_error_t pota_service(pota_context_t *context, uint32_t budget_us);
pota_error_t pota_write(pota_context_t *context, const uint8_t *data, uint32_t size);
pota_error_t pota_end(pota_context_t *context);
pota_error_t pota_abort(pota_context_t *context);
pota_error_t pota_commit(pota_context_t *context);
void pota_get_status(const pota_context_t *context, pota_status_t *status);

void pota_core_set_failed(pota_context_t *context, pota_error_t error);
pota_error_t pota_core_begin_action(pota_context_t *context, const void *argument);
pota_error_t pota_core_service_action(pota_context_t *context, const void *argument);
pota_error_t pota_core_write_action(pota_context_t *context, const void *argument);
pota_error_t pota_core_end_action(pota_context_t *context, const void *argument);
pota_error_t pota_core_abort_action(pota_context_t *context, const void *argument);
pota_error_t pota_core_commit_action(pota_context_t *context, const void *argument);

#endif
