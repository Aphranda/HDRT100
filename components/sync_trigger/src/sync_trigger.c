#include "sync_trigger.h"

#include <string.h>

#include "osal.h"
#include "resource_arbiter.h"
#include "sync_io.h"
#include "trigger_fb.h"

#define SYNC_TRIGGER_QUEUE_LENGTH  16u

typedef struct {
    trigger_vector_t vector;
    trig_event_t     queue[SYNC_TRIGGER_QUEUE_LENGTH];
    uint32_t         queue_head;
    uint32_t         queue_tail;
    uint32_t         queue_count;
} sync_trigger_ao_t;

static sync_trigger_ao_t s_ao;

/* ── 事件队列 ── */

static bool ao_enqueue(const trig_event_t *event)
{
    if (s_ao.queue_count >= SYNC_TRIGGER_QUEUE_LENGTH) {
        return false;
    }

    s_ao.queue[s_ao.queue_tail] = *event;
    s_ao.queue_tail = (s_ao.queue_tail + 1u) % SYNC_TRIGGER_QUEUE_LENGTH;
    s_ao.queue_count++;
    return true;
}

static bool ao_dequeue(trig_event_t *event)
{
    if (s_ao.queue_count == 0u) {
        return false;
    }

    *event = s_ao.queue[s_ao.queue_head];
    s_ao.queue_head = (s_ao.queue_head + 1u) % SYNC_TRIGGER_QUEUE_LENGTH;
    s_ao.queue_count--;
    return true;
}

/* ── sync_io 状态同步 ── */

static void ao_refresh_from_io(void)
{
    sync_io_status_t status;
    sync_io_get_status(&status);

    s_ao.vector.io_initialized = status.initialized;
    s_ao.vector.capture_running = status.capture_running;
    s_ao.vector.sync_clock_running = status.sync_clock_running;
    s_ao.vector.dropped_capture_words = status.dropped_capture_words;

    resource_arbiter_publish_trigger_activity(status.capture_running,
                                              status.sync_clock_running);
}

/* ── 公共接口 ── */

bool sync_trigger_init(void)
{
    memset(&s_ao, 0, sizeof(s_ao));
    trigger_fb_init(&s_ao.vector);
    ao_refresh_from_io();
    return true;
}

bool sync_trigger_post_event(const sync_trigger_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    trig_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = (trig_event_type_t)event->type;
    e.payload.value = event->value;

    return ao_enqueue(&e);
}

bool sync_trigger_post(const trig_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    return ao_enqueue(event);
}

void sync_trigger_service(void)
{
    trig_event_t event;

    if (!ao_dequeue(&event)) {
        /* 无事件时仍同步 ARM 态 PIO 状态 */
        if (s_ao.vector.state == TRIG_STATE_SEQ_ARMED) {
            trig_event_t svc_event;
            memset(&svc_event, 0, sizeof(svc_event));
            svc_event.type = TRIG_EVENT_DMA_ROLLOVER;
            trigger_fb_execute(&s_ao.vector, &svc_event);
        }
        ao_refresh_from_io();
        return;
    }

    trigger_fb_execute(&s_ao.vector, &event);
    ao_refresh_from_io();
}

void sync_trigger_get_summary(sync_trigger_summary_t *summary)
{
    if (summary == NULL) {
        return;
    }

    osal_critical_enter();
    *summary = s_ao.vector;
    osal_critical_exit();
}

void sync_trigger_get_vector(trigger_vector_t *vector)
{
    if (vector == NULL) {
        return;
    }

    osal_critical_enter();
    *vector = s_ao.vector;
    osal_critical_exit();
}
