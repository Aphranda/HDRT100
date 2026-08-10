#ifndef SYNC_TRIGGER_H
#define SYNC_TRIGGER_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_vector.h"

/* ── 旧类型别名（兼容 sync_trigger_summary / sync_trigger_event）── */

typedef trigger_vector_t sync_trigger_summary_t;

typedef enum {
    SYNC_TRIGGER_EVENT_RESET               = TRIG_EVENT_RESET,
    SYNC_TRIGGER_EVENT_SET_TRIGGER_WIDTH   = TRIG_EVENT_SET_TRIGGER_WIDTH,
    SYNC_TRIGGER_EVENT_FIRE_TRIGGER        = TRIG_EVENT_FIRE_TRIGGER,
    SYNC_TRIGGER_EVENT_SET_PULSE_WIDTH     = TRIG_EVENT_SET_PULSE_WIDTH,
    SYNC_TRIGGER_EVENT_FIRE_PULSE          = TRIG_EVENT_FIRE_PULSE,
    SYNC_TRIGGER_EVENT_SET_RJ45_TRIGGER_WIDTH = TRIG_EVENT_SET_RJ45_TRIGGER_WIDTH,
    SYNC_TRIGGER_EVENT_FIRE_RJ45_TRIGGER      = TRIG_EVENT_FIRE_RJ45_TRIGGER,
    SYNC_TRIGGER_EVENT_SET_MARKER_WIDTH    = TRIG_EVENT_SET_MARKER_WIDTH, /* deprecated RJ45 compat */
    SYNC_TRIGGER_EVENT_FIRE_MARKER         = TRIG_EVENT_FIRE_MARKER,      /* deprecated RJ45 compat */
    SYNC_TRIGGER_EVENT_SET_SAMPLE_RATE     = TRIG_EVENT_SET_SAMPLE_RATE,
    SYNC_TRIGGER_EVENT_SET_SAMPLE_STATE    = TRIG_EVENT_SET_SAMPLE_STATE,
    SYNC_TRIGGER_EVENT_SET_CLOCK_FREQ      = TRIG_EVENT_SET_CLOCK_FREQ,
    SYNC_TRIGGER_EVENT_SET_CLOCK_STATE     = TRIG_EVENT_SET_CLOCK_STATE,
} sync_trigger_event_type_t;

typedef struct {
    sync_trigger_event_type_t type;
    uint32_t value;
} sync_trigger_event_t;

/* ── 公共接口 ── */

bool sync_trigger_init(void);

/* 旧版兼容：只收发 value 型事件 */
bool sync_trigger_post_event(const sync_trigger_event_t *event);

/* 新版：收发完整 trig_event_t（支持 seq_config 载荷） */
bool sync_trigger_post(const trig_event_t *event);

void sync_trigger_service(void);

/* 快照查询 */
void sync_trigger_get_summary(sync_trigger_summary_t *summary);
void sync_trigger_get_vector(trigger_vector_t *vector);
void sync_trigger_get_debug(uint32_t *stage,
                            uint32_t *event_type,
                            uint32_t *state,
                            uint32_t *error_code);

#endif
