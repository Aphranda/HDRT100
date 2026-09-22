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

/* A generation cannot be reinterpreted or resurrected after CLEAR. An exact
 * same-generation retry is idempotent only with the same local binding. */
uint32_t trigger_grid_preview_configure(const trigger_time_grid_t *grid);
uint32_t trigger_grid_preview_clear(void);
uint32_t trigger_grid_preview_snapshot(trigger_grid_preview_snapshot_t *out);
/* Success proves arithmetic and a stable local snapshot only. It never
 * reserves an output, changes a model, publishes READY or issues a pulse.
 * All rejected calls preserve *out. A later executor must revalidate. */
uint32_t trigger_grid_preview_plan(uint32_t epoch, uint32_t generation,
    uint64_t ordinal, uint64_t minimum_lead_ns, uint64_t maximum_ahead_ns,
    trigger_grid_preview_plan_t *out);

#endif
