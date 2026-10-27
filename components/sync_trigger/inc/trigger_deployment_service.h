#ifndef TRIGGER_DEPLOYMENT_SERVICE_H
#define TRIGGER_DEPLOYMENT_SERVICE_H

#include "trigger_deployment.h"
#include "trigger_grid_preview.h"

/* STOP-only business routing configuration. ACTIVE means installed mapping,
 * not an armed role, acquired IO, device readiness or distributed commit. */
enum {
    TRIGGER_DEPLOYMENT_SERVICE_BUSY = 32u,
    TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE,
    TRIGGER_DEPLOYMENT_SERVICE_UNCONFIGURED,
    TRIGGER_DEPLOYMENT_SERVICE_CHANGED,
    TRIGGER_DEPLOYMENT_SERVICE_DIGEST,
    TRIGGER_DEPLOYMENT_SERVICE_NO_STAGE,
    TRIGGER_DEPLOYMENT_SERVICE_GRID_BASE = 64u,
};

typedef struct {
    trigger_deployment_t table;
    uint64_t local_uid;
    uint32_t valid, crc, local_role, ring_config, schedule_crc, local_slot;
} trigger_deployment_binding_t;

typedef struct {
    trigger_deployment_binding_t active;
    trigger_deployment_t staged;
    uint32_t staged_valid, staged_crc, last_generation;
} trigger_deployment_snapshot_t;

typedef struct {
    trigger_deployment_binding_t deployment;
    trigger_deployment_row_t route;
    uint32_t role;
    trigger_grid_preview_plan_t clock;
} trigger_deployment_plan_t;

uint32_t trigger_deployment_stage(const trigger_deployment_t *table, uint32_t expected_crc);
uint32_t trigger_deployment_activate(uint32_t generation, uint32_t expected_crc);
uint32_t trigger_deployment_clear(void);
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *out);
/* Read-only cached copy for the unified-control T2 configure path while TDMA
 * is running; the STOP-gated snapshot above remains the local SCPI gate. */
uint32_t trigger_deployment_snapshot_cached(trigger_deployment_snapshot_t *out);
/* Local destination qualification plus the existing exact-q clock planner.
 * No transport, READY, output claim or device completion is implied. */
uint32_t trigger_deployment_plan(uint32_t deployment_generation, uint32_t role,
    uint32_t grid_epoch, uint32_t grid_generation, uint64_t ordinal,
    uint64_t minimum_lead_ns, uint64_t maximum_ahead_ns, trigger_deployment_plan_t *out);

#endif
