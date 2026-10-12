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

/* Which check latched the sticky binding fault. Diagnostic only. */
typedef enum {
    TRIGGER_TIME_CURSOR_FAULT_NONE = 0u,
    TRIGGER_TIME_CURSOR_FAULT_RING = 1u,        /* ring flags/config/schedule */
    TRIGGER_TIME_CURSOR_FAULT_MODEL = 2u,       /* model identity on a new token */
    TRIGGER_TIME_CURSOR_FAULT_RING_MODEL = 3u,  /* model slot/schedule vs ring */
    TRIGGER_TIME_CURSOR_FAULT_BACKWARD = 4u,
    TRIGGER_TIME_CURSOR_FAULT_OVERFLOW = 5u,
} trigger_time_cursor_fault_site_t;

/* Inputs of the latching check that differ from the bound identity (or are
 * themselves invalid), computed by Core1 when the fault latches. */
typedef enum {
    TRIGGER_TIME_CURSOR_DIFF_RING_PENDING = 1u << 0,     /* config != applied */
    TRIGGER_TIME_CURSOR_DIFF_RING_CONFIG = 1u << 1,      /* config != bound */
    TRIGGER_TIME_CURSOR_DIFF_RING_SCHEDULE_ZERO = 1u << 2,
    TRIGGER_TIME_CURSOR_DIFF_ROLE = 1u << 3,
    TRIGGER_TIME_CURSOR_DIFF_CLOCK_EPOCH = 1u << 4,
    TRIGGER_TIME_CURSOR_DIFF_CLOCK_RUN = 1u << 5,
    TRIGGER_TIME_CURSOR_DIFF_SLOT = 1u << 6,
    TRIGGER_TIME_CURSOR_DIFF_SCHEDULE = 1u << 7,
    TRIGGER_TIME_CURSOR_DIFF_PERIOD = 1u << 8,           /* model vs grid */
    TRIGGER_TIME_CURSOR_DIFF_RING_SLOT = 1u << 9,        /* model vs ring */
    TRIGGER_TIME_CURSOR_DIFF_RING_SCHEDULE = 1u << 10,   /* model vs ring */
} trigger_time_cursor_fault_diff_t;

/* First sticky fault of the current binding, written once by Core1 when it
 * latches; four words because the shared SRAM budget has no more slack.
 * The bound ring config/model token are the cursor snapshot's own fields.
 * flags: [3:0] site, [7:4] error, [8] ring enabled, [9] adapter started,
 * [10] data enabled, [11] training dirty (ring bits only when the check
 * read the ring),
 * [31:16] trigger_time_cursor_fault_diff_t. ring_config/model_token are the
 * live values of the failing poll (zero when not read by that check). */
typedef struct {
    uint32_t flags, sample_seq, ring_config, model_token;
} trigger_time_cursor_fault_t;

bool trigger_time_cursor_configure(const trigger_time_grid_t *grid);
void trigger_time_cursor_clear(void);
void trigger_time_cursor_service(void);
bool trigger_time_cursor_snapshot(trigger_time_cursor_snapshot_t *out);
bool trigger_time_cursor_fault_snapshot(trigger_time_cursor_fault_t *out);

#endif
