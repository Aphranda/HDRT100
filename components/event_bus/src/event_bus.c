#include "event_bus.h"

#include <string.h>

#include "osal.h"

typedef struct {
    ota_event_t event;
    uint8_t data[OTA_EVENT_INLINE_DATA_SIZE];
} event_bus_ota_message_t;

static event_bus_ota_message_t s_ota_queue[EVENT_BUS_OTA_QUEUE_LENGTH];
#if OTA_EVENT_MAX_DATA_SIZE > OTA_EVENT_INLINE_DATA_SIZE
static uint8_t s_ota_fast_blocks[EVENT_BUS_OTA_FAST_BLOCK_DEPTH]
                                [OTA_EVENT_MAX_DATA_SIZE];
static uint32_t s_ota_fast_block_mask;
#endif
static uint32_t s_ota_queue_head;
static uint32_t s_ota_queue_tail;
static uint32_t s_ota_queue_count;

bool event_bus_init(void)
{
    memset(s_ota_queue, 0, sizeof(s_ota_queue));
    s_ota_queue_head = 0u;
    s_ota_queue_tail = 0u;
    s_ota_queue_count = 0u;
#if OTA_EVENT_MAX_DATA_SIZE > OTA_EVENT_INLINE_DATA_SIZE
    s_ota_fast_block_mask = 0u;
#endif
    return true;
}

bool event_bus_post_ota_event(const ota_event_t *event)
{
    bool posted = false;

    if (event == NULL) {
        return false;
    }

    osal_critical_enter();
    if (s_ota_queue_count >= EVENT_BUS_OTA_QUEUE_LENGTH) {
        goto exit;
    }

    event_bus_ota_message_t *message = &s_ota_queue[s_ota_queue_tail];
    message->event = *event;

    if (event->type == OTA_EVENT_DATA_BLOCK) {
        if (event->payload.data.data == NULL ||
            event->payload.data.length == 0u ||
            event->payload.data.length > OTA_EVENT_MAX_DATA_SIZE) {
            goto exit;
        }

        if (event->payload.data.length <= OTA_EVENT_INLINE_DATA_SIZE) {
            memcpy(message->data, event->payload.data.data,
                   event->payload.data.length);
            message->event.payload.data.data = message->data;
        } else {
#if OTA_EVENT_MAX_DATA_SIZE > OTA_EVENT_INLINE_DATA_SIZE
            uint32_t fast_slot = EVENT_BUS_OTA_FAST_BLOCK_DEPTH;
            for (uint32_t index = 0u;
                 index < EVENT_BUS_OTA_FAST_BLOCK_DEPTH; ++index) {
                if ((s_ota_fast_block_mask & (1u << index)) == 0u) {
                    fast_slot = index;
                    break;
                }
            }
            if (fast_slot >= EVENT_BUS_OTA_FAST_BLOCK_DEPTH) {
                goto exit;
            }
            memcpy(s_ota_fast_blocks[fast_slot], event->payload.data.data,
                   event->payload.data.length);
            message->event.payload.data.data = s_ota_fast_blocks[fast_slot];
            s_ota_fast_block_mask |= 1u << fast_slot;
#else
            goto exit;
#endif
        }
    }

    s_ota_queue_tail = (s_ota_queue_tail + 1u) % EVENT_BUS_OTA_QUEUE_LENGTH;
    s_ota_queue_count++;
    posted = true;

exit:
    osal_critical_exit();
    return posted;
}

bool event_bus_try_recv_ota_event(ota_event_t *event)
{
    bool received = false;

    if (event == NULL) {
        return false;
    }

    osal_critical_enter();
    if (s_ota_queue_count == 0u) {
        goto exit;
    }

    *event = s_ota_queue[s_ota_queue_head].event;
    s_ota_queue_head = (s_ota_queue_head + 1u) % EVENT_BUS_OTA_QUEUE_LENGTH;
    s_ota_queue_count--;
    received = true;

exit:
    osal_critical_exit();
    return received;
}

void event_bus_complete_ota_event(const ota_event_t *event)
{
#if OTA_EVENT_MAX_DATA_SIZE > OTA_EVENT_INLINE_DATA_SIZE
    if (event == NULL || event->type != OTA_EVENT_DATA_BLOCK ||
        event->payload.data.length <= OTA_EVENT_INLINE_DATA_SIZE) {
        return;
    }

    osal_critical_enter();
    for (uint32_t index = 0u;
         index < EVENT_BUS_OTA_FAST_BLOCK_DEPTH; ++index) {
        if (event->payload.data.data == s_ota_fast_blocks[index]) {
            s_ota_fast_block_mask &= ~(1u << index);
            break;
        }
    }
    osal_critical_exit();
#else
    (void)event;
#endif
}
