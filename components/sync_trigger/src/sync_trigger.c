#include "sync_trigger.h"

#include <string.h>

#include "osal.h"
#include "resource_arbiter.h"
#include "sync_io.h"

#define SYNC_TRIGGER_DEFAULT_PULSE_US    10u
#define SYNC_TRIGGER_DEFAULT_CAPTURE_HZ  1000000u
#define SYNC_TRIGGER_DEFAULT_CLOCK_HZ    1000000u
#define SYNC_TRIGGER_QUEUE_LENGTH        16u

typedef struct {
    sync_trigger_summary_t summary;
    sync_trigger_event_t queue[SYNC_TRIGGER_QUEUE_LENGTH];
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t queue_count;
} sync_trigger_context_t;

static sync_trigger_context_t s_sync_trigger;

static void sync_trigger_apply_defaults_locked(void)
{
    memset(&s_sync_trigger, 0, sizeof(s_sync_trigger));
    s_sync_trigger.summary.initialized = true;
    s_sync_trigger.summary.trigger_width_us = SYNC_TRIGGER_DEFAULT_PULSE_US;
    s_sync_trigger.summary.pulse_width_us = SYNC_TRIGGER_DEFAULT_PULSE_US;
    s_sync_trigger.summary.marker_width_us = SYNC_TRIGGER_DEFAULT_PULSE_US;
    s_sync_trigger.summary.capture_sample_hz = SYNC_TRIGGER_DEFAULT_CAPTURE_HZ;
    s_sync_trigger.summary.sync_clock_hz = SYNC_TRIGGER_DEFAULT_CLOCK_HZ;
}

static void sync_trigger_refresh_from_io(void)
{
    sync_io_status_t status;
    sync_io_get_status(&status);

    osal_critical_enter();
    s_sync_trigger.summary.io_initialized = status.initialized;
    s_sync_trigger.summary.capture_running = status.capture_running;
    s_sync_trigger.summary.sync_clock_running = status.sync_clock_running;
    s_sync_trigger.summary.dropped_capture_words = status.dropped_capture_words;
    osal_critical_exit();

    resource_arbiter_publish_trigger_activity(status.capture_running,
                                              status.sync_clock_running);
}

bool sync_trigger_init(void)
{
    osal_critical_enter();
    sync_trigger_apply_defaults_locked();
    osal_critical_exit();
    sync_trigger_refresh_from_io();
    return true;
}

bool sync_trigger_post_event(const sync_trigger_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    osal_critical_enter();
    if (!s_sync_trigger.summary.initialized) {
        sync_trigger_apply_defaults_locked();
    }

    if (s_sync_trigger.queue_count >= SYNC_TRIGGER_QUEUE_LENGTH) {
        osal_critical_exit();
        return false;
    }

    s_sync_trigger.queue[s_sync_trigger.queue_tail] = *event;
    s_sync_trigger.queue_tail = (s_sync_trigger.queue_tail + 1u) % SYNC_TRIGGER_QUEUE_LENGTH;
    s_sync_trigger.queue_count++;
    osal_critical_exit();
    return true;
}

static bool sync_trigger_pop_event(sync_trigger_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    osal_critical_enter();
    if (s_sync_trigger.queue_count == 0u) {
        osal_critical_exit();
        return false;
    }

    *event = s_sync_trigger.queue[s_sync_trigger.queue_head];
    s_sync_trigger.queue_head = (s_sync_trigger.queue_head + 1u) % SYNC_TRIGGER_QUEUE_LENGTH;
    s_sync_trigger.queue_count--;
    osal_critical_exit();
    return true;
}

static void sync_trigger_update_u32_field(uint32_t *field, uint32_t value)
{
    if (field == NULL || value == 0u) {
        return;
    }

    osal_critical_enter();
    *field = value;
    osal_critical_exit();
}

static uint32_t sync_trigger_get_u32_field(const uint32_t *field)
{
    uint32_t value = 0u;

    if (field == NULL) {
        return 0u;
    }

    osal_critical_enter();
    value = *field;
    osal_critical_exit();
    return value;
}

static bool sync_trigger_get_clock_enabled(void)
{
    bool enabled;

    osal_critical_enter();
    enabled = s_sync_trigger.summary.sync_clock_enabled;
    osal_critical_exit();
    return enabled;
}

static void sync_trigger_set_clock_enabled(bool enabled)
{
    osal_critical_enter();
    s_sync_trigger.summary.sync_clock_enabled = enabled;
    osal_critical_exit();
}

static void sync_trigger_handle_reset(void)
{
    sync_io_stop_clock();
    sync_io_stop_capture();

    osal_critical_enter();
    sync_trigger_apply_defaults_locked();
    osal_critical_exit();
    sync_trigger_refresh_from_io();
}

static void sync_trigger_handle_sample_rate(uint32_t sample_hz)
{
    if (sample_hz == 0u) {
        return;
    }

    sync_trigger_update_u32_field(&s_sync_trigger.summary.capture_sample_hz, sample_hz);
    (void)sync_io_start_capture(sample_hz);
}

static void sync_trigger_handle_sample_state(bool enable)
{
    if (enable) {
        const uint32_t sample_hz = sync_trigger_get_u32_field(&s_sync_trigger.summary.capture_sample_hz);
        (void)sync_io_start_capture(sample_hz);
        return;
    }

    sync_io_stop_capture();
}

static void sync_trigger_handle_clock_freq(uint32_t frequency_hz)
{
    if (frequency_hz == 0u) {
        return;
    }

    sync_trigger_update_u32_field(&s_sync_trigger.summary.sync_clock_hz, frequency_hz);
    if (sync_trigger_get_clock_enabled()) {
        (void)sync_io_start_clock(frequency_hz);
    }
}

static void sync_trigger_handle_clock_state(bool enable)
{
    sync_trigger_set_clock_enabled(enable);
    if (enable) {
        const uint32_t frequency_hz = sync_trigger_get_u32_field(&s_sync_trigger.summary.sync_clock_hz);
        (void)sync_io_start_clock(frequency_hz);
        return;
    }

    sync_io_stop_clock();
}

static void sync_trigger_execute(const sync_trigger_event_t *event)
{
    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case SYNC_TRIGGER_EVENT_RESET:
        sync_trigger_handle_reset();
        break;
    case SYNC_TRIGGER_EVENT_SET_TRIGGER_WIDTH:
        sync_trigger_update_u32_field(&s_sync_trigger.summary.trigger_width_us, event->value);
        break;
    case SYNC_TRIGGER_EVENT_FIRE_TRIGGER:
        (void)sync_io_fire_pulse_us(sync_trigger_get_u32_field(&s_sync_trigger.summary.trigger_width_us));
        break;
    case SYNC_TRIGGER_EVENT_SET_PULSE_WIDTH:
        sync_trigger_update_u32_field(&s_sync_trigger.summary.pulse_width_us, event->value);
        break;
    case SYNC_TRIGGER_EVENT_FIRE_PULSE:
        (void)sync_io_fire_pulse_out_us(sync_trigger_get_u32_field(&s_sync_trigger.summary.pulse_width_us));
        break;
    case SYNC_TRIGGER_EVENT_SET_MARKER_WIDTH:
        sync_trigger_update_u32_field(&s_sync_trigger.summary.marker_width_us, event->value);
        break;
    case SYNC_TRIGGER_EVENT_FIRE_MARKER:
        (void)sync_io_fire_marker_us(sync_trigger_get_u32_field(&s_sync_trigger.summary.marker_width_us));
        break;
    case SYNC_TRIGGER_EVENT_SET_SAMPLE_RATE:
        sync_trigger_handle_sample_rate(event->value);
        break;
    case SYNC_TRIGGER_EVENT_SET_SAMPLE_STATE:
        sync_trigger_handle_sample_state(event->value != 0u);
        break;
    case SYNC_TRIGGER_EVENT_SET_CLOCK_FREQ:
        sync_trigger_handle_clock_freq(event->value);
        break;
    case SYNC_TRIGGER_EVENT_SET_CLOCK_STATE:
        sync_trigger_handle_clock_state(event->value != 0u);
        break;
    default:
        break;
    }
}

void sync_trigger_service(void)
{
    sync_trigger_event_t event;

    if (!sync_trigger_pop_event(&event)) {
        if (s_sync_trigger.summary.initialized) {
            sync_trigger_refresh_from_io();
        }
        return;
    }

    sync_trigger_execute(&event);
    sync_trigger_refresh_from_io();
}

void sync_trigger_get_summary(sync_trigger_summary_t *summary)
{
    if (summary == NULL) {
        return;
    }

    osal_critical_enter();
    *summary = s_sync_trigger.summary;
    osal_critical_exit();
}
