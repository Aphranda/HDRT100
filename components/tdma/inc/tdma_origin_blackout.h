#ifndef TDMA_ORIGIN_BLACKOUT_H
#define TDMA_ORIGIN_BLACKOUT_H

#include <stdbool.h>
#include <stdint.h>

/* Finite diagnostic omission only, not a physical cadence or WCET grant. */
#define TDMA_ORIGIN_BLACKOUT_SCHEMA 1u
#define TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS 64u
#define TDMA_ORIGIN_BLACKOUT_SKIP_CALLS 4u
#define TDMA_ORIGIN_BLACKOUT_MAX_INTERVAL_US 10000u

typedef enum {
    TDMA_ORIGIN_BLACKOUT_DISABLED = 0u,
    TDMA_ORIGIN_BLACKOUT_WAITING,
    TDMA_ORIGIN_BLACKOUT_ACTIVE,
    TDMA_ORIGIN_BLACKOUT_COMPLETE,
    TDMA_ORIGIN_BLACKOUT_CANCELLED,
    TDMA_ORIGIN_BLACKOUT_INVALID,
    TDMA_ORIGIN_BLACKOUT_DEADLINE,
} tdma_origin_blackout_state_t;

typedef struct {
    uint32_t schema, state, trial_epoch, config_seq, record_epoch, clock_hz;
    uint32_t wait_calls, skipped_calls, before_version, after_version;
    uint64_t begin_ticks, end_ticks;
} tdma_origin_blackout_snapshot_t;

typedef struct {
    uint32_t guard;
    tdma_origin_blackout_snapshot_t snapshot;
} tdma_origin_blackout_t;

typedef struct {
    bool authorized, ready;
    uint32_t record_epoch, record_version, clock_hz;
    uint64_t now_ticks;
} tdma_origin_blackout_input_t;

typedef enum {
    TDMA_ORIGIN_BLACKOUT_SERVICE = 0u,
    TDMA_ORIGIN_BLACKOUT_SKIP,
    TDMA_ORIGIN_BLACKOUT_STOP,
} tdma_origin_blackout_action_t;

/* Sole Core1 owner writes; terminal snapshots are retained across STOP until
 * a new admitted trial. COMPLETE means the omission protocol completed, not
 * that records, physical cadence, timestamps or product gates passed. */
void tdma_origin_blackout_reset(tdma_origin_blackout_t *trial, bool enabled,
    uint32_t epoch, uint32_t config_seq);
tdma_origin_blackout_action_t tdma_origin_blackout_step(tdma_origin_blackout_t *trial,
    const tdma_origin_blackout_input_t *input);
/* One seqlock attempt. No live query/record copying during the omission. */
bool tdma_origin_blackout_get(const tdma_origin_blackout_t *trial,
    tdma_origin_blackout_snapshot_t *out);

#endif
