#include "event_bus.h"

#include <string.h>

#include "osal.h"

#define EVENT_BUS_OTA_QUEUE_LENGTH 8u

typedef struct {
    ota_event_t event;
    uint8_t data[OTA_EVENT_MAX_DATA_SIZE];
} event_bus_ota_message_t;

static event_bus_ota_message_t s_ota_queue[EVENT_BUS_OTA_QUEUE_LENGTH];
static uint32_t s_ota_queue_head;
static uint32_t s_ota_queue_tail;
static uint32_t s_ota_queue_count;

bool event_bus_init(void)
{
    memset(s_ota_queue, 0, sizeof(s_ota_queue));
    s_ota_queue_head = 0u;
    s_ota_queue_tail = 0u;
    s_ota_queue_count = 0u;
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

        memcpy(message->data, event->payload.data.data, event->payload.data.length);
        message->event.payload.data.data = message->data;
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
