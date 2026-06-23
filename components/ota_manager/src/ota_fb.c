#include "ota_fb.h"

#include "drv_watchdog.h"
#include "ota_ao_private.h"
#include "ota_error.h"
#include "ota_metadata.h"
#include "ota_package.h"
#include "ota_partition.h"
#include "portable_ota_port.h"
#include "sync_io.h"

static void ota_fb_bump_sequence(struct ota_ao_context *context)
{
    context->vector.sequence++;
}

static void ota_fb_set_state(struct ota_ao_context *context, ota_state_t state)
{
    context->vector.state = (uint32_t)state;
    ota_fb_bump_sequence(context);
}

static void ota_fb_set_error(struct ota_ao_context *context, ota_error_t error)
{
    context->vector.error_code = (uint32_t)error;
    context->vector.last_result = (uint32_t)OTA_RESULT_FAILED;
    ota_fb_set_state(context, OTA_STATE_FAILED);
}

static bool ota_fb_trigger_is_idle(void)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    return !status.capture_running && !status.sync_clock_running;
}

static bool ota_fb_state_accepts_begin(const struct ota_ao_context *context)
{
    return context->vector.state == (uint32_t)OTA_STATE_IDLE ||
           context->vector.state == (uint32_t)OTA_STATE_ABORTED ||
           context->vector.state == (uint32_t)OTA_STATE_FAILED ||
           context->vector.state == (uint32_t)OTA_STATE_COMMITTED;
}

static void ota_fb_sync_portable_status(struct ota_ao_context *context, bool ok)
{
    (void)ok;
    ota_fb_bump_sequence(context);
}

static void ota_fb_handle_begin(struct ota_ao_context *context, const ota_event_t *event)
{
    if (!ota_fb_state_accepts_begin(context)) {
        ota_fb_set_error(context, OTA_ERR_INVALID_STATE);
        return;
    }

    const bool package_mode = (event->payload.begin.flags & OTA_BEGIN_FLAG_PACKAGE) != 0u;
    const uint32_t max_size = package_mode ?
                                  (OTA_PACKAGE_HEADER_SIZE + OTA_SLOT_A_SIZE + OTA_SLOT_B_SIZE) :
                                  OTA_APP_SIZE_FAIL_THRESHOLD;

    if (event->payload.begin.size == 0u ||
        event->payload.begin.size > max_size) {
        ota_fb_set_error(context, OTA_ERR_IMAGE_TOO_LARGE);
        return;
    }

    if (package_mode && event->payload.begin.size <= OTA_PACKAGE_HEADER_SIZE) {
        ota_fb_set_error(context, OTA_ERR_BAD_HEADER);
        return;
    }

    if (!ota_fb_trigger_is_idle()) {
        ota_fb_set_error(context, OTA_ERR_BUSY);
        return;
    }

    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        ota_fb_set_error(context, OTA_ERR_METADATA);
        return;
    }

    context->vector.image_version = event->payload.begin.image_version;
    context->vector.boot_flags_summary = metadata.last_boot_result;
    const bool ok = portable_ota_port_core_begin(&metadata,
                                                 event->payload.begin.size,
                                                 event->payload.begin.crc32,
                                                 package_mode,
                                                 &context->vector);
    ota_fb_sync_portable_status(context, ok);
}

static void ota_fb_handle_tick(struct ota_ao_context *context)
{
    if (context->vector.state != (uint32_t)OTA_STATE_CHECK_PERMISSION &&
        context->vector.state != (uint32_t)OTA_STATE_ERASE_SLOT) {
        return;
    }

    const bool ok = portable_ota_port_core_service(0u, &context->vector);
    ota_fb_sync_portable_status(context, ok);
}

static void ota_fb_handle_data(struct ota_ao_context *context, const ota_event_t *event)
{
    if (event->payload.data.data == NULL ||
        event->payload.data.length == 0u ||
        event->payload.data.length > OTA_EVENT_MAX_DATA_SIZE) {
        ota_fb_set_error(context, OTA_ERR_BAD_ARGUMENT);
        return;
    }

    const bool ok = portable_ota_port_core_write(event->payload.data.data,
                                                 event->payload.data.length,
                                                 &context->vector);
    ota_fb_sync_portable_status(context, ok);
}

static void ota_fb_handle_end(struct ota_ao_context *context)
{
    const bool ok = portable_ota_port_core_end(&context->vector);
    ota_fb_sync_portable_status(context, ok);
}

static void ota_fb_handle_abort(struct ota_ao_context *context)
{
    const bool ok = portable_ota_port_core_abort(&context->vector);
    ota_fb_sync_portable_status(context, ok);
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
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        ota_fb_set_error(context, OTA_ERR_METADATA);
        return;
    }

    if (!portable_ota_port_metadata_can_confirm_active(&metadata)) {
        ota_fb_set_error(context, OTA_ERR_INVALID_STATE);
        return;
    }

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
