#ifndef TRIGGER_GRID_PREVIEW_H
#define TRIGGER_GRID_PREVIEW_H

#include "trigger_time_grid.h"
#include "vdc_dpll_manager.h"

/* Core0 STOP-only staging and read-only planning. This configuration is not
 * TriggerAO active state, PREPARED/READY, a wire publication, or ARM authority.
 * epoch identifies the explicitly selected common experiment time domain;
 * local clock epochs and sessions remain separately bound board identities. */
enum {
    TRIGGER_GRID_PREVIEW_BUSY = 32u,
    TRIGGER_GRID_PREVIEW_NOT_STOPPED,
    TRIGGER_GRID_PREVIEW_UNCONFIGURED,
    TRIGGER_GRID_PREVIEW_MODEL,
    TRIGGER_GRID_PREVIEW_CHANGED,
    TRIGGER_GRID_PREVIEW_GENERATION,
    TRIGGER_GRID_PREVIEW_CLOCK,
    TRIGGER_GRID_PREVIEW_ANCHOR = 39u,
};

typedef struct {
    trigger_time_grid_t grid;
    uint32_t configured, last_generation;
    uint32_t session, ring_config, role_generation, clock_epoch, clock_run;
    uint32_t local_slot, schedule_crc;
} trigger_grid_preview_snapshot_t;

typedef struct {
    trigger_grid_preview_snapshot_t configuration;
    vdc_dpll_manager_committed_model_t model;
    trigger_time_grid_plan_t plan;
    uint64_t now_ticks, checked_at_ticks, minimum_lead_ticks, maximum_ahead_ticks;
    uint32_t tick_hz;
} trigger_grid_preview_plan_t;

typedef struct {
    uint32_t valid, event_id, sample_seq, model_token, session, generation;
    uint64_t anchor_vdc_ns;
    uint32_t schedule_crc, timestamp_source, resolution_ns, timestamp_flags;
} trigger_grid_anchor_snapshot_t;

/* A generation cannot be reinterpreted or resurrected after CLEAR. An exact
 * same-generation retry is idempotent only with the same local binding. */
uint32_t trigger_grid_preview_configure(const trigger_time_grid_t *grid);
uint32_t trigger_grid_preview_clear(void);
uint32_t trigger_grid_preview_snapshot(trigger_grid_preview_snapshot_t *out);
/* Read-only cached copy for the unified-control T2 configure path: TDMA is
 * running, so the STOP-gated snapshot above is unavailable, but the staged
 * grid must still be readable by the command handler. */
uint32_t trigger_grid_preview_snapshot_cached(trigger_grid_preview_snapshot_t *out);
/* Success proves arithmetic and a stable local snapshot only. It never
 * reserves an output, changes a model, publishes READY or issues a pulse.
 * All rejected calls preserve *out. A later executor must revalidate. */
uint32_t trigger_grid_preview_plan(uint32_t epoch, uint32_t generation,
    uint64_t ordinal, uint64_t minimum_lead_ns, uint64_t maximum_ahead_ns,
    trigger_grid_preview_plan_t *out);
/* Bind the configured grid's A to a real accepted sync observer event. The
 * event identity and model/session are rechecked by anchored planning. */
uint32_t trigger_grid_preview_bind_anchor(uint32_t event_id,
    uint64_t anchor_vdc_ns);
uint32_t trigger_grid_preview_anchor_snapshot(
    trigger_grid_anchor_snapshot_t *out);
uint32_t trigger_grid_preview_plan_anchored(uint32_t epoch, uint32_t generation,
    uint64_t ordinal, uint64_t minimum_lead_ns, uint64_t maximum_ahead_ns,
    trigger_grid_preview_plan_t *out);

#endif
