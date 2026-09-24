#ifndef TRIGGER_TIME_CURSOR_H
#define TRIGGER_TIME_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TRIGGER_TIME_CURSOR_OK = 0u,
    TRIGGER_TIME_CURSOR_UNCONFIGURED = 1u,
    TRIGGER_TIME_CURSOR_NO_SYNC = 2u,
    TRIGGER_TIME_CURSOR_SESSION = 3u,
    TRIGGER_TIME_CURSOR_SAMPLE_GAP = 4u,
    TRIGGER_TIME_CURSOR_OVERFLOW = 5u,
} trigger_time_cursor_error_t;

typedef struct {
    uint32_t configured, valid;
    uint32_t grid_epoch, grid_generation, subdivisions;
    uint32_t session, sample_seq, last_event_id;
    uint32_t model_token, dco_update_seq;
    uint32_t missed_sync_count;
    uint64_t pulse_index, action_cursor;
    uint32_t fine_slice_index;
    trigger_time_cursor_error_t last_error;
} trigger_time_cursor_snapshot_t;

bool trigger_time_cursor_configure(uint32_t grid_epoch, uint32_t grid_generation,
    uint32_t subdivisions);
void trigger_time_cursor_clear(void);
void trigger_time_cursor_service(void);
bool trigger_time_cursor_snapshot(trigger_time_cursor_snapshot_t *out);

#endif
