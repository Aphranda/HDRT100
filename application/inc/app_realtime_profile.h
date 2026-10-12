#ifndef APP_REALTIME_PROFILE_H
#define APP_REALTIME_PROFILE_H

#include <stdbool.h>
#include <stdint.h>
#include "app.h"

#define APP_REALTIME_PROFILE_TABLE_VERSION 1u

/* Whole core1 phase table plus the cycle length it fills. The cycle is derived,
 * not independent: it is the end of the last phase, so a table can never
 * describe a cycle that its own windows do not cover. */
typedef struct {
    uint32_t version;
    uint32_t cycle_cycles;
    app_realtime_phase_contract_t phase[APP_REALTIME_PHASE_COUNT];
} app_realtime_profile_table_t;

/* A configured cycle still has to be measurable by the core-local free-running
 * counter the dispatcher reads. Mirrors M33_SYST_RVR_RELOAD_BITS. */
#define APP_REALTIME_PROFILE_CYCLE_MAX_CYCLES 0xffffffu

/* The dispatcher reaches a phase boundary with finite entry jitter, so even a
 * configured DPLL window needs late-entry headroom: a zero-headroom table
 * starved the real dispatcher unless it landed exactly on the start cycle.
 * Mirrors PROJECT_CORE1_DPLL_ENTRY_MARGIN_CYCLES. */
#define APP_REALTIME_PROFILE_DPLL_HEADROOM_CYCLES 1000u

/* Admission results. Rules are checked in enum order and the first violation
 * is reported, so a table with several faults always names the same one. */
typedef enum {
    APP_REALTIME_PROFILE_REJECT_OK = 0,
    APP_REALTIME_PROFILE_REJECT_NULL,
    APP_REALTIME_PROFILE_REJECT_VERSION,
    APP_REALTIME_PROFILE_REJECT_ORDER,
    APP_REALTIME_PROFILE_REJECT_EMPTY_PHASE,
    APP_REALTIME_PROFILE_REJECT_WCET,
    /* The tail guard carries no load, so a non-zero guard WCET would be a
     * budget claim the dispatcher never executes. */
    APP_REALTIME_PROFILE_REJECT_GUARD_WCET,
    APP_REALTIME_PROFILE_REJECT_CYCLE_RANGE,
    APP_REALTIME_PROFILE_REJECT_CYCLE_TAIL,
    APP_REALTIME_PROFILE_REJECT_TDMA_FLOOR,
    APP_REALTIME_PROFILE_REJECT_DPLL_HEADROOM,
    /* Not admission rules: a table that passes every rule above can still fail
     * because its durable record or its publication boundary was unavailable. */
    APP_REALTIME_PROFILE_REJECT_PERSIST,
    APP_REALTIME_PROFILE_REJECT_PUBLISH,
} app_realtime_profile_reject_t;

typedef struct {
    app_realtime_profile_reject_t code;
    /* Phase that violated the rule, or APP_REALTIME_PHASE_COUNT when the rule
     * is table-wide. */
    uint32_t phase_id;
} app_realtime_profile_rejection_t;

#define APP_REALTIME_PROFILE_REJECTION_INIT \
    { APP_REALTIME_PROFILE_REJECT_OK, APP_REALTIME_PHASE_COUNT }

const char *app_realtime_profile_reject_name(app_realtime_profile_reject_t reject);

/* Pure table lookup against the table in force. This does not authorize a
 * lifecycle change or a frame.
 *
 * While an override is installed it owns the cycle length: only that exact
 * cycle is admitted, because the compiled catalog scales the TDMA window and an
 * override must not be silently re-scaled by a period request. The catalog
 * profiles become reachable again once the override is cleared. */
bool app_realtime_profile_supported(uint32_t cycle_cycles);
bool app_realtime_profile_phase(uint32_t cycle_cycles,
    app_realtime_phase_id_t phase_id, app_realtime_phase_contract_t *phase);
/* Caller owns the schedule publication boundary. Retains cumulative timing
 * evidence; profile_generation identifies the installed table. */
bool app_realtime_profile_install(app_realtime_schedule_snapshot_t *schedule,
    uint32_t cycle_cycles, uint32_t generation);

/* Compiled four-board table at PROJECT_CORE1_CYCLE_CYCLES. Always available,
 * even while an override is installed, so a rejected configuration always has
 * a known-good fallback. */
bool app_realtime_profile_table_default(app_realtime_profile_table_t *table);
/* Table in force right now: the override when one is installed, otherwise the
 * compiled default. */
bool app_realtime_profile_table_effective(app_realtime_profile_table_t *table);
bool app_realtime_profile_table_validate(const app_realtime_profile_table_t *table,
    app_realtime_profile_rejection_t *rejection);

bool app_realtime_profile_override_active(void);
/* Replaces the whole table in one step. The table is validated first; a
 * rejected table leaves the table in force untouched. */
bool app_realtime_profile_override_install(const app_realtime_profile_table_t *table,
    app_realtime_profile_rejection_t *rejection);
/* Drops the override; the compiled default becomes effective again. */
bool app_realtime_profile_override_clear(void);

/* Staging: a configuration editor writes one phase at a time and only APPLY
 * makes the result effective, so a half-written table can never be dispatched.
 * BEGIN reloads the table in force. */
bool app_realtime_profile_stage_begin(void);
/* Seed an editor from the complete running snapshot (including catalog period
 * scaling), without changing the configured or running profile. */
bool app_realtime_profile_stage_from_schedule(
    const app_realtime_schedule_snapshot_t *schedule);
bool app_realtime_profile_stage_phase(uint32_t phase_id, uint32_t start_cycle,
    uint32_t end_cycle, uint32_t wcet_cycles);
/* Cycle length of the staged table: moves the end of the last phase and leaves
 * every earlier window unchanged. */
bool app_realtime_profile_stage_cycle(uint32_t cycle_cycles);
bool app_realtime_profile_stage_read(app_realtime_profile_table_t *table);
bool app_realtime_profile_stage_dirty(void);
bool app_realtime_profile_stage_validate(app_realtime_profile_rejection_t *rejection);
bool app_realtime_profile_stage_apply(app_realtime_profile_rejection_t *rejection);
bool app_realtime_profile_stage_discard(void);

#endif
