#include "ota_fb.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "drv_flash.h"
#include "drv_watchdog.h"
#include "ota_ao_private.h"
#include "ota_crc32.h"
#include "ota_error.h"
#include "ota_image.h"
#include "ota_metadata.h"
#include "ota_partition.h"
#include "ota_vector.h"
#include "sync_io.h"

static void ota_fb_set_state(struct ota_ao_context *context, ota_state_t state)
{
    context->vector.state = (uint32_t)state;
    context->vector.sequence++;
}

static void ota_fb_set_error(struct ota_ao_context *context, ota_error_t error)
{
    context->vector.error_code = (uint32_t)error;
    context->vector.last_result = (uint32_t)OTA_RESULT_FAILED;
    ota_fb_set_state(context, OTA_STATE_FAILED);
}

static void ota_fb_update_progress(struct ota_ao_context *context)
{
    if (context->vector.expected_size == 0u) {
        context->vector.progress_permille = 0u;
        return;
    }

    uint64_t progress = (uint64_t)context->vector.received_size * 1000u;
    progress /= context->vector.expected_size;
    if (progress > 1000u) {
        progress = 1000u;
    }
    context->vector.progress_permille = (uint32_t)progress;
}

static uint32_t ota_fb_align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static bool ota_fb_trigger_is_idle(void)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    return !status.capture_running && !status.sync_clock_running;
}

static void ota_fb_handle_begin(struct ota_ao_context *context, const ota_event_t *event)
{
    if (context->vector.state != (uint32_t)OTA_STATE_IDLE &&
        context->vector.state != (uint32_t)OTA_STATE_ABORTED &&
        context->vector.state != (uint32_t)OTA_STATE_FAILED &&
        context->vector.state != (uint32_t)OTA_STATE_COMMITTED) {
        ota_fb_set_error(context, OTA_ERR_INVALID_STATE);
        return;
    }

    if (event->payload.begin.size == 0u ||
        event->payload.begin.size > OTA_DEFAULT_TARGET_SLOT_SIZE ||
        event->payload.begin.size > OTA_APP_SIZE_FAIL_THRESHOLD) {
        ota_fb_set_error(context, OTA_ERR_IMAGE_TOO_LARGE);
        return;
    }

    if (!ota_fb_trigger_is_idle()) {
        ota_fb_set_error(context, OTA_ERR_BUSY);
        return;
    }

    context->target_slot = OTA_SLOT_B;
    context->target_offset = OTA_DEFAULT_TARGET_SLOT_OFFSET;
    context->target_size = ota_fb_align_up_u32(event->payload.begin.size, DRV_FLASH_SECTOR_SIZE);
    context->erase_offset = 0u;

    context->vector.target_slot = (uint32_t)context->target_slot;
    context->vector.expected_size = event->payload.begin.size;
    context->vector.received_size = 0u;
    context->vector.programmed_size = 0u;
    context->vector.crc32_expected = event->payload.begin.crc32;
    context->vector.crc32_running = 0u;
    context->vector.image_version = event->payload.begin.image_version;
    context->vector.error_code = (uint32_t)OTA_ERR_NONE;
    context->vector.last_result = (uint32_t)OTA_RESULT_ACCEPTED;
    ota_fb_update_progress(context);

    ota_fb_set_state(context, OTA_STATE_CHECK_PERMISSION);
}

static void ota_fb_handle_tick(struct ota_ao_context *context)
{
    switch ((ota_state_t)context->vector.state) {
    case OTA_STATE_CHECK_PERMISSION:
        ota_fb_set_state(context, OTA_STATE_ERASE_SLOT);
        break;

    case OTA_STATE_ERASE_SLOT: {
        const uint32_t remaining = context->target_size - context->erase_offset;
        const uint32_t erase_size = (remaining > DRV_FLASH_SECTOR_SIZE) ? DRV_FLASH_SECTOR_SIZE : remaining;
        if (erase_size == 0u) {
            ota_fb_set_state(context, OTA_STATE_RECEIVING);
            break;
        }
        if (!drv_flash_erase(context->target_offset + context->erase_offset, erase_size)) {
            ota_fb_set_error(context, OTA_ERR_FLASH_ERASE);
            break;
        }
        context->erase_offset += erase_size;
        context->vector.programmed_size = context->erase_offset;
        ota_fb_update_progress(context);
        if (context->erase_offset >= context->target_size) {
            context->vector.programmed_size = 0u;
            ota_fb_set_state(context, OTA_STATE_RECEIVING);
        }
        break;
    }

    default:
        break;
    }
}

static void ota_fb_handle_data(struct ota_ao_context *context, const ota_event_t *event)
{
    if (context->vector.state != (uint32_t)OTA_STATE_RECEIVING) {
        ota_fb_set_error(context, OTA_ERR_INVALID_STATE);
        return;
    }

    if (event->payload.data.data == NULL || event->payload.data.length == 0u ||
        event->payload.data.length > OTA_EVENT_MAX_DATA_SIZE) {
        ota_fb_set_error(context, OTA_ERR_BAD_ARGUMENT);
        return;
    }

    if ((context->vector.received_size + event->payload.data.length) > context->vector.expected_size) {
        ota_fb_set_error(context, OTA_ERR_IMAGE_TOO_LARGE);
        return;
    }

    const bool is_final_block =
        (context->vector.received_size + event->payload.data.length) == context->vector.expected_size;
    if (!is_final_block && (event->payload.data.length % DRV_FLASH_PAGE_SIZE) != 0u) {
        ota_fb_set_error(context, OTA_ERR_BAD_ARGUMENT);
        return;
    }

    const uint32_t write_offset = context->target_offset + context->vector.received_size;
    uint8_t page_buffer[OTA_EVENT_MAX_DATA_SIZE + DRV_FLASH_PAGE_SIZE];
    const uint32_t program_length =
        (event->payload.data.length + DRV_FLASH_PAGE_SIZE - 1u) & ~(DRV_FLASH_PAGE_SIZE - 1u);

    if (program_length > sizeof(page_buffer)) {
        ota_fb_set_error(context, OTA_ERR_BAD_ARGUMENT);
        return;
    }

    memset(page_buffer, 0xFF, program_length);
    memcpy(page_buffer, event->payload.data.data, event->payload.data.length);

    if (!drv_flash_program(write_offset, page_buffer, program_length)) {
        ota_fb_set_error(context, OTA_ERR_FLASH_PROGRAM);
        return;
    }

    context->vector.crc32_running =
        ota_crc32_update(context->vector.crc32_running, event->payload.data.data, event->payload.data.length);
    context->vector.received_size += event->payload.data.length;
    context->vector.programmed_size = context->vector.received_size;
    context->vector.last_result = (uint32_t)OTA_RESULT_ACCEPTED;
    ota_fb_update_progress(context);
}

static void ota_fb_handle_end(struct ota_ao_context *context)
{
    if (context->vector.state != (uint32_t)OTA_STATE_RECEIVING) {
        ota_fb_set_error(context, OTA_ERR_INVALID_STATE);
        return;
    }

    if (context->vector.received_size != context->vector.expected_size) {
        ota_fb_set_error(context, OTA_ERR_READBACK);
        return;
    }

    ota_fb_set_state(context, OTA_STATE_VERIFYING);

    if (context->vector.crc32_running != context->vector.crc32_expected) {
        ota_fb_set_error(context, OTA_ERR_CRC);
        return;
    }

    if (!ota_image_validate_app_vector(context->target_offset,
                                       context->vector.expected_size,
                                       OTA_DEFAULT_APP_RUN_OFFSET)) {
        ota_fb_set_error(context, OTA_ERR_VECTOR);
        return;
    }

    ota_fb_set_state(context, OTA_STATE_MARK_PENDING);

    if (!ota_metadata_mark_pending(context->target_slot,
                                   context->vector.expected_size,
                                   context->vector.crc32_expected)) {
        ota_fb_set_error(context, OTA_ERR_METADATA);
        return;
    }

    context->vector.last_result = (uint32_t)OTA_RESULT_IMAGE_STAGED;
    ota_fb_set_state(context, OTA_STATE_READY_TO_REBOOT);
}

static void ota_fb_handle_abort(struct ota_ao_context *context)
{
    context->vector.error_code = (uint32_t)OTA_ERR_ABORTED;
    context->vector.last_result = (uint32_t)OTA_RESULT_ABORTED;
    ota_fb_set_state(context, OTA_STATE_ABORTED);
}

static void ota_fb_handle_boot(struct ota_ao_context *context)
{
    if (context->vector.state != (uint32_t)OTA_STATE_READY_TO_REBOOT) {
        ota_fb_set_error(context, OTA_ERR_INVALID_STATE);
        return;
    }

    drv_watchdog_reboot(50u);
}

static void ota_fb_handle_commit(struct ota_ao_context *context)
{
    if (!ota_metadata_confirm_active()) {
        ota_fb_set_error(context, OTA_ERR_METADATA);
        return;
    }

    context->vector.last_result = (uint32_t)OTA_RESULT_COMMITTED;
    ota_fb_set_state(context, OTA_STATE_COMMITTED);
}

void ota_fb_execute(ota_ao_context_t *context, const ota_event_t *event)
{
    if (context == NULL || event == NULL) {
        return;
    }

    context->vector.last_event = (uint32_t)event->type;

    switch (event->type) {
    case OTA_EVENT_BEGIN:
        ota_fb_handle_begin(context, event);
        break;
    case OTA_EVENT_TICK:
        ota_fb_handle_tick(context);
        break;
    case OTA_EVENT_DATA_BLOCK:
        ota_fb_handle_data(context, event);
        break;
    case OTA_EVENT_END:
        ota_fb_handle_end(context);
        break;
    case OTA_EVENT_ABORT:
        ota_fb_handle_abort(context);
        break;
    case OTA_EVENT_BOOT:
        ota_fb_handle_boot(context);
        break;
    case OTA_EVENT_COMMIT:
        ota_fb_handle_commit(context);
        break;
    default:
        break;
    }
}
