#ifndef TRIGGER_TIME_CURSOR_H
#define TRIGGER_TIME_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_time_grid.h"

typedef enum {
    TRIGGER_TIME_CURSOR_OK = 0u,
    TRIGGER_TIME_CURSOR_UNCONFIGURED = 1u,
    TRIGGER_TIME_CURSOR_NO_SYNC = 2u,
    TRIGGER_TIME_CURSOR_SESSION = 3u,
    TRIGGER_TIME_CURSOR_SAMPLE_GAP = 4u,
    TRIGGER_TIME_CURSOR_OVERFLOW = 5u,
    TRIGGER_TIME_CURSOR_NO_LOCK = 6u,
    TRIGGER_TIME_CURSOR_IDENTITY = 7u,
    TRIGGER_TIME_CURSOR_MODEL_TIME = 8u,
    TRIGGER_TIME_CURSOR_GRID_PHASE = 9u,
    TRIGGER_TIME_CURSOR_BACKWARD = 10u,
} trigger_time_cursor_error_t;

typedef struct {
    /* valid means current local DCO/grid mapping, not a phase-lock or
     * physical output precision grant. No diagnostic GPIO is required. */
    uint32_t configured, valid;
    uint32_t grid_epoch, grid_generation, period_ns, subdivisions;
    uint64_t anchor_vdc_ns;
    /* sample_seq counts locally elapsed coarse periods (first sample = 1),
     * not received VDC observations. Legacy RX identity fields remain zero. */
    uint32_t session, sample_seq, last_event_id;
    uint32_t carrier_sequence, identity_crc32, ring_config_seq;
    uint32_t model_token, dco_update_seq;
    /* Coarse periods not individually observed by this local poller; this
     * is not wire loss. A scheduler period may span multiple coarse cells. */
    uint32_t missed_sync_count;
    /* pulse_index is coarse ordinal + 1; action_cursor includes fine q. */
    uint64_t pulse_index, action_cursor;
    uint64_t event_local_ns, event_vdc_ns;
    uint32_t fine_slice_index;
    trigger_time_cursor_error_t last_error;
} trigger_time_cursor_snapshot_t;

bool trigger_time_cursor_configure(const trigger_time_grid_t *grid);
void trigger_time_cursor_clear(void);
void trigger_time_cursor_service(void);
bool trigger_time_cursor_snapshot(trigger_time_cursor_snapshot_t *out);

#endif
