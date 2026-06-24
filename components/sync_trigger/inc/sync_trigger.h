#ifndef SYNC_TRIGGER_H
#define SYNC_TRIGGER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SYNC_TRIGGER_EVENT_RESET = 0,
    SYNC_TRIGGER_EVENT_SET_TRIGGER_WIDTH,
    SYNC_TRIGGER_EVENT_FIRE_TRIGGER,
    SYNC_TRIGGER_EVENT_SET_PULSE_WIDTH,
    SYNC_TRIGGER_EVENT_FIRE_PULSE,
    SYNC_TRIGGER_EVENT_SET_MARKER_WIDTH,
    SYNC_TRIGGER_EVENT_FIRE_MARKER,
    SYNC_TRIGGER_EVENT_SET_SAMPLE_RATE,
    SYNC_TRIGGER_EVENT_SET_SAMPLE_STATE,
    SYNC_TRIGGER_EVENT_SET_CLOCK_FREQ,
    SYNC_TRIGGER_EVENT_SET_CLOCK_STATE,
} sync_trigger_event_type_t;

typedef struct {
    sync_trigger_event_type_t type;
    uint32_t value;
} sync_trigger_event_t;

typedef struct {
    bool initialized;
    bool io_initialized;
    bool capture_running;
    bool sync_clock_running;
    bool sync_clock_enabled;
    uint32_t trigger_width_us;
    uint32_t pulse_width_us;
    uint32_t marker_width_us;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t dropped_capture_words;
} sync_trigger_summary_t;

bool sync_trigger_init(void);
bool sync_trigger_post_event(const sync_trigger_event_t *event);
void sync_trigger_service(void);
void sync_trigger_get_summary(sync_trigger_summary_t *summary);

#endif
