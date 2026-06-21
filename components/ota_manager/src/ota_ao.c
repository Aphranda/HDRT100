#include "ota_ao.h"

#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "ota_ao_private.h"
#include "ota_error.h"
#include "ota_fb.h"
#include "ota_metadata.h"
#include "ota_partition.h"

#define OTA_AO_QUEUE_LENGTH 4u

typedef struct {
    ota_event_t event;
    uint8_t data[OTA_EVENT_MAX_DATA_SIZE];
} ota_queued_event_t;

static struct ota_ao_context s_ota_context;
static ota_queued_event_t s_queue[OTA_AO_QUEUE_LENGTH];
static uint32_t s_queue_head;
static uint32_t s_queue_tail;
static uint32_t s_queue_count;

static bool ota_ao_queue_push(const ota_event_t *event)
{
    if (s_queue_count >= OTA_AO_QUEUE_LENGTH) {
        return false;
    }

    ota_queued_event_t *queued = &s_queue[s_queue_tail];
    queued->event = *event;

    if (event->type == OTA_EVENT_DATA_BLOCK) {
        if (event->payload.data.data == NULL ||
            event->payload.data.length == 0u ||
            event->payload.data.length > OTA_EVENT_MAX_DATA_SIZE) {
            return false;
        }

        memcpy(queued->data, event->payload.data.data, event->payload.data.length);
        queued->event.payload.data.data = queued->data;
    }

    s_queue_tail = (s_queue_tail + 1u) % OTA_AO_QUEUE_LENGTH;
    s_queue_count++;
    return true;
}

static bool ota_ao_queue_pop(ota_event_t *event)
{
    if (s_queue_count == 0u) {
        return false;
    }

    *event = s_queue[s_queue_head].event;
    s_queue_head = (s_queue_head + 1u) % OTA_AO_QUEUE_LENGTH;
    s_queue_count--;
    return true;
}

const char *ota_state_to_string(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_IDLE:
        return "IDLE";
    case OTA_STATE_CHECK_PERMISSION:
        return "CHECK_PERMISSION";
    case OTA_STATE_ERASE_SLOT:
        return "ERASE_SLOT";
    case OTA_STATE_RECEIVING:
        return "RECEIVING";
    case OTA_STATE_VERIFYING:
        return "VERIFYING";
    case OTA_STATE_MARK_PENDING:
        return "MARK_PENDING";
    case OTA_STATE_READY_TO_REBOOT:
        return "READY_TO_REBOOT";
    case OTA_STATE_PENDING_CONFIRM:
        return "PENDING_CONFIRM";
    case OTA_STATE_COMMITTED:
        return "COMMITTED";
    case OTA_STATE_FAILED:
        return "FAILED";
    case OTA_STATE_ABORTED:
        return "ABORTED";
    default:
        return "UNKNOWN";
    }
}

const char *ota_error_to_string(uint32_t error_code)
{
    switch ((ota_error_t)error_code) {
    case OTA_ERR_NONE:
        return "NONE";
    case OTA_ERR_BUSY:
        return "BUSY";
    case OTA_ERR_INVALID_STATE:
        return "INVALID_STATE";
    case OTA_ERR_IMAGE_TOO_LARGE:
        return "IMAGE_TOO_LARGE";
    case OTA_ERR_BAD_HEADER:
        return "BAD_HEADER";
    case OTA_ERR_BOARD_MISMATCH:
        return "BOARD_MISMATCH";
    case OTA_ERR_VERSION_REJECTED:
        return "VERSION_REJECTED";
    case OTA_ERR_FLASH_ERASE:
        return "FLASH_ERASE";
    case OTA_ERR_FLASH_PROGRAM:
        return "FLASH_PROGRAM";
    case OTA_ERR_READBACK:
        return "READBACK";
    case OTA_ERR_CRC:
        return "CRC";
    case OTA_ERR_VECTOR:
        return "VECTOR";
    case OTA_ERR_METADATA:
        return "METADATA";
    case OTA_ERR_ABORTED:
        return "ABORTED";
    case OTA_ERR_BOOT_ROLLBACK:
        return "BOOT_ROLLBACK";
    case OTA_ERR_QUEUE_FULL:
        return "QUEUE_FULL";
    case OTA_ERR_BAD_ARGUMENT:
        return "BAD_ARGUMENT";
    default:
        return "UNKNOWN";
    }
}

bool ota_ao_init(void)
{
    memset(&s_ota_context, 0, sizeof(s_ota_context));
    memset(s_queue, 0, sizeof(s_queue));
    s_queue_head = 0u;
    s_queue_tail = 0u;
    s_queue_count = 0u;

    s_ota_context.vector.timestamp_ms = board_uptime_ms();
    s_ota_context.vector.state = (uint32_t)OTA_STATE_IDLE;
    s_ota_context.vector.target_slot = (uint32_t)OTA_SLOT_B;
    s_ota_context.vector.error_code = (uint32_t)OTA_ERR_NONE;

    ota_metadata_t metadata;
    if (ota_metadata_load(&metadata)) {
        s_ota_context.vector.target_slot = metadata.pending_slot != (uint32_t)OTA_SLOT_NONE ?
                                               metadata.pending_slot :
                                               metadata.active_slot;
        s_ota_context.vector.expected_size = metadata.slot_b_size;
        s_ota_context.vector.crc32_expected = metadata.slot_b_crc32;
        s_ota_context.vector.boot_flags_summary = metadata.last_boot_result;
    }

    s_ota_context.target_slot = OTA_SLOT_B;
    s_ota_context.target_offset = OTA_DEFAULT_TARGET_SLOT_OFFSET;
    s_ota_context.target_size = OTA_DEFAULT_TARGET_SLOT_SIZE;

    LOG_INFO("ota", "OTA AO initialized");
    return true;
}

bool ota_ao_post_event(const ota_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    if (!ota_ao_queue_push(event)) {
        s_ota_context.vector.error_code = (uint32_t)OTA_ERR_QUEUE_FULL;
        return false;
    }

    return true;
}

void ota_ao_service(uint32_t budget_us)
{
    (void)budget_us;

    s_ota_context.vector.timestamp_ms = board_uptime_ms();

    ota_event_t event;
    if (ota_ao_queue_pop(&event)) {
        ota_fb_execute(&s_ota_context, &event);
        return;
    }

    const ota_event_t tick = {
        .type = OTA_EVENT_TICK,
    };
    ota_fb_execute(&s_ota_context, &tick);
}

void ota_ao_get_vector(ota_vector_t *vector)
{
    if (vector == NULL) {
        return;
    }

    *vector = s_ota_context.vector;
}

bool ota_ao_get_metadata(ota_metadata_t *metadata)
{
    return ota_metadata_load(metadata);
}
