#include "ota_ao.h"

#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "event_bus.h"
#include "ota_ao_private.h"
#include "ota_error.h"
#include "ota_fb.h"
#include "ota_metadata.h"
#include "ota_partition.h"
#include "portable_ota_port.h"
#include "resource_arbiter.h"

static struct ota_ao_context s_ota_context;
static ota_vector_t s_vector_snapshot[2];
static ota_metadata_t s_metadata_snapshot[2];
static uint32_t s_vector_active_index;
static uint32_t s_metadata_active_index;
static bool s_metadata_snapshot_valid;

void ota_ao_publish_vector(const ota_ao_context_t *context)
{
    if (context == NULL) {
        return;
    }
    const uint32_t next =
        (__atomic_load_n(&s_vector_active_index, __ATOMIC_RELAXED) + 1u) & 1u;
    s_vector_snapshot[next] = context->vector;
    __atomic_store_n(&s_vector_active_index, next, __ATOMIC_RELEASE);
}

void ota_ao_publish_metadata(ota_ao_context_t *context,
                             const ota_metadata_t *metadata)
{
    if (context == NULL || metadata == NULL) {
        return;
    }
    const uint32_t next =
        (__atomic_load_n(&s_metadata_active_index, __ATOMIC_RELAXED) + 1u) & 1u;
    s_metadata_snapshot[next] = *metadata;
    context->metadata_snapshot = *metadata;
    context->metadata_snapshot_valid = true;
    s_metadata_snapshot_valid = true;
    __atomic_store_n(&s_metadata_active_index, next, __ATOMIC_RELEASE);
}

void ota_ao_publish_metadata_snapshot(const ota_metadata_t *metadata)
{
    ota_ao_publish_metadata(&s_ota_context, metadata);
}

static ota_slot_t ota_ao_target_slot_from_metadata(const ota_metadata_t *metadata)
{
    if (metadata == NULL ||
        metadata->boot_mode != (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
        return OTA_SLOT_B;
    }

    if (metadata->active_slot == (uint32_t)OTA_SLOT_A) {
        return OTA_SLOT_B;
    }

    if (metadata->active_slot == (uint32_t)OTA_SLOT_B) {
        return OTA_SLOT_A;
    }

    return OTA_SLOT_B;
}

const char *ota_state_to_string(ota_state_t state)
{
    return portable_ota_port_state_to_string(state);
}

const char *ota_error_to_string(uint32_t error_code)
{
    return portable_ota_port_error_to_string(error_code);
}

const char *ota_result_to_string(ota_result_t result)
{
    return portable_ota_port_result_to_string(result);
}

bool ota_ao_init(void)
{
    memset(&s_ota_context, 0, sizeof(s_ota_context));
    memset(s_vector_snapshot, 0, sizeof(s_vector_snapshot));
    memset(s_metadata_snapshot, 0, sizeof(s_metadata_snapshot));
    s_vector_active_index = 0u;
    s_metadata_active_index = 0u;
    s_metadata_snapshot_valid = false;

    s_ota_context.vector.timestamp_ms = board_uptime_ms();
    s_ota_context.vector.state = (uint32_t)OTA_STATE_IDLE;
    s_ota_context.vector.target_slot = (uint32_t)OTA_SLOT_B;
    s_ota_context.vector.error_code = (uint32_t)OTA_ERR_NONE;
    s_ota_context.target_slot = OTA_SLOT_B;

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    /* Completion durability must be established even when BCB metadata is
     * absent/corrupt and the stream session cannot be opened yet. */
    if (!portable_ota_port_durable_init()) {
        return false;
    }
#endif

    ota_metadata_t metadata;
    if (ota_metadata_load(&metadata)) {
        ota_ao_publish_metadata(&s_ota_context, &metadata);
        s_ota_context.target_slot = ota_ao_target_slot_from_metadata(&metadata);
        s_ota_context.vector.target_slot = metadata.pending_slot != (uint32_t)OTA_SLOT_NONE ?
                                               metadata.pending_slot :
                                               (uint32_t)s_ota_context.target_slot;
        s_ota_context.vector.expected_size = metadata.slot_b_size;
        s_ota_context.vector.crc32_expected = metadata.slot_b_crc32;
        s_ota_context.vector.boot_flags_summary = metadata.last_boot_result;
        const bool stream_initialized =
            portable_ota_port_stream_init(&metadata);
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
        if (!stream_initialized) {
            return false;
        }
#else
        (void)stream_initialized;
#endif
    }

    s_ota_context.target_offset = ota_partition_slot_offset(s_ota_context.target_slot);
    s_ota_context.target_size = ota_partition_slot_size(s_ota_context.target_slot);
    s_ota_context.target_run_offset = OTA_DEFAULT_APP_RUN_OFFSET;
    ota_ao_publish_vector(&s_ota_context);

    LOG_INFO("ota", "OTA AO initialized");
    return true;
}

bool ota_ao_post_event(const ota_event_t *event)
{
    if (!event_bus_post_ota_event(event)) {
        s_ota_context.vector.error_code = (uint32_t)OTA_ERR_QUEUE_FULL;
        ota_ao_publish_vector(&s_ota_context);
        return false;
    }

    return true;
}

void ota_ao_service(uint32_t budget_us)
{
    if (budget_us == 0u) {
        return;
    }

    s_ota_context.vector.timestamp_ms = board_uptime_ms();
    ota_ao_publish_vector(&s_ota_context);

    /* Service bounded permission/erase work before consuming DATA.  SCPI
     * producers can queue a DATA block while the inactive slot is still
     * erasing; consuming it early converts normal back-pressure into
     * INVALID_STATE and eventually QUEUE_FULL. */
    const ota_event_t tick = {
        .type = OTA_EVENT_TICK,
    };
    if (s_ota_context.vector.state == (uint32_t)OTA_STATE_CHECK_PERMISSION ||
        s_ota_context.vector.state == (uint32_t)OTA_STATE_ERASE_SLOT) {
        ota_fb_execute(&s_ota_context, &tick);
        return;
    }

    ota_event_t event;
    if (event_bus_try_recv_ota_event(&event)) {
        ota_fb_execute(&s_ota_context, &event);
        event_bus_complete_ota_event(&event);
        return;
    }

    /* Stream ingress shares the same bounded AO service cadence as the legacy
     * event path.  Only one path advances a session per tick; the stream port
     * itself owns the FlashTransaction intent and readback boundary. */
    if (portable_ota_port_stream_is_active()) {
        pota_stream_ingress_status_t status;
        if (portable_ota_port_stream_get_status(&status) &&
            (status.state == POTA_STREAM_STATE_OPEN ||
             status.state == POTA_STREAM_STATE_RECEIVING ||
             status.state == POTA_STREAM_STATE_ENDING)) {
            const pota_stream_ingress_result_t stream_result =
                portable_ota_port_stream_service(budget_us);
            /* END is intentionally serviced after CLOSE.  If a bounded
             * FlashTransaction step fails asynchronously, release the
             * maintenance lease here; otherwise the board would remain
             * permanently admitted to OTA with no recoverable session. */
            if (stream_result != POTA_STREAM_INGRESS_OK) {
                pota_stream_ingress_status_t failed_status;
                if (portable_ota_port_stream_get_status(&failed_status) &&
                    (failed_status.state == POTA_STREAM_STATE_FAILED ||
                     failed_status.state == POTA_STREAM_STATE_ABORTED)) {
                    resource_arbiter_release_ota_admission();
                }
            }
        }
        return;
    }

    ota_fb_execute(&s_ota_context, &tick);
}

void ota_ao_get_vector(ota_vector_t *vector)
{
    if (vector == NULL) {
        return;
    }

    const uint32_t active =
        __atomic_load_n(&s_vector_active_index, __ATOMIC_ACQUIRE) & 1u;
    *vector = s_vector_snapshot[active];
}

bool ota_ao_get_metadata_snapshot(ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }
    if (!s_metadata_snapshot_valid) {
        return false;
    }
    const uint32_t active =
        __atomic_load_n(&s_metadata_active_index, __ATOMIC_ACQUIRE) & 1u;
    *metadata = s_metadata_snapshot[active];
    return true;
}

bool ota_ao_is_active(void)
{
    const uint32_t state = s_ota_context.vector.state;
    return state >= (uint32_t)OTA_STATE_CHECK_PERMISSION &&
           state <= (uint32_t)OTA_STATE_READY_TO_REBOOT;
}
