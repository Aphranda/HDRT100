#ifndef TDMA_LOAD_SEGMENT_H
#define TDMA_LOAD_SEGMENT_H

#include <stdbool.h>
#include <stdint.h>

/* Pure TDMA-owner model for segmented load instances: the OTA/CONFIG/
 * diagnostic work that rides a seat without owning the ring cycle.
 *
 * It answers one bounded question per cycle: how many segments of the active
 * transfer may enter, and what happens to the ones that cannot. It never opens
 * a window, never submits a frame, never claims delivery, and performs no
 * provider/PIO/FIFO/SCPI call, lock or allocation. One owner serializes every
 * call and copies snapshots through its existing guarded handoff.
 *
 * TX admission, RX reassembly facts and reverse (ACK/diagnostic) traffic are
 * counted separately: a fast sender never implies receive headroom. */
typedef enum {
    TDMA_LOAD_SEGMENT_CONFIG = 0u,
    TDMA_LOAD_SEGMENT_OTA,
    TDMA_LOAD_SEGMENT_DIAGNOSTIC,
    TDMA_LOAD_SEGMENT_CLASS_COUNT, /* No fitted class: measured, not fitted. */
} tdma_load_segment_class_t;

typedef enum {
    TDMA_LOAD_SEGMENT_OK = 0,
    TDMA_LOAD_SEGMENT_BAD_ARGUMENT,
    TDMA_LOAD_SEGMENT_NOT_CONFIGURED,
    TDMA_LOAD_SEGMENT_STALE_IDENTITY,
    TDMA_LOAD_SEGMENT_NO_TRANSFER,
    TDMA_LOAD_SEGMENT_NO_CYCLE,
    TDMA_LOAD_SEGMENT_DUPLICATE_SEGMENT,
    TDMA_LOAD_SEGMENT_UNKNOWN_SEGMENT,
    TDMA_LOAD_SEGMENT_QUEUE_FULL,
    TDMA_LOAD_SEGMENT_QUOTA_EXHAUSTED,
    TDMA_LOAD_SEGMENT_DEADLINE_EXPIRED,
    TDMA_LOAD_SEGMENT_OVERFLOW_DROP,
    TDMA_LOAD_SEGMENT_ABORTED,
    TDMA_LOAD_SEGMENT_COMPLETE,
} tdma_load_segment_result_t;

/* What happens to a segment that cannot enter the current cycle. BACKPRESSURE
 * keeps it queued and reports the refusal to the sender; the drop policies
 * decide which side loses data, and every loss is counted. */
typedef enum {
    TDMA_LOAD_OVERFLOW_BACKPRESSURE = 0u,
    TDMA_LOAD_OVERFLOW_DROP_NEW,
    TDMA_LOAD_OVERFLOW_DROP_OLDEST,
    TDMA_LOAD_OVERFLOW_ABORT_TRANSFER,
} tdma_load_overflow_policy_t;

typedef struct {
    uint32_t class_id;          /* tdma_load_segment_class_t */
    uint32_t segment_bytes;     /* Payload bytes per segment; nonzero. */
    uint32_t frames_per_cycle;  /* Admission quota per cycle; nonzero. */
    uint32_t queue_depth;       /* Bounded in-flight window; nonzero. */
    uint32_t deadline_us;       /* Per-transfer deadline; nonzero. */
    uint32_t overflow_policy;   /* tdma_load_overflow_policy_t */
} tdma_load_segment_policy_t;

typedef struct {
    /* Same-generation identity: a transfer opened under one configuration or
     * profile generation is never resumed under another. */
    uint32_t transfer_id;       /* Nonzero local immutable token. */
    uint32_t total_segments;    /* Nonzero. */
    uint32_t total_bytes;
    uint32_t config_generation;
    uint32_t profile_generation;
} tdma_load_segment_transfer_t;

typedef struct {
    /* TX admission facts for one class. */
    uint32_t admitted_segments;
    uint32_t confirmed_segments;
    uint32_t dropped_segments;
    uint32_t refused_segments;
    uint32_t out_of_order_count;
    uint32_t duplicate_count;
    uint32_t deadline_miss_count;
    uint32_t current_depth;
    uint32_t queue_high_watermark;
    uint32_t cycle_segments;
    uint32_t cycle_bytes;
    /* RX reassembly and reverse flow: separate budgets, never derived. */
    uint32_t reassembled_segments;
    uint32_t reassembly_gap_count;
    uint32_t reverse_frames;
    uint32_t reverse_bytes;
    uint32_t reverse_refused_frames;
} tdma_load_segment_class_stats_t;

typedef struct {
    uint32_t configured;
    uint32_t class_id;
    uint32_t config_generation;
    uint32_t profile_generation;
    uint32_t segment_bytes;
    uint32_t frames_per_cycle;
    uint32_t queue_depth;
    uint32_t deadline_us;
    uint32_t overflow_policy;
    /* Active transfer state. */
    uint32_t active;
    uint32_t transfer_id;
    uint32_t total_segments;
    uint32_t next_segment;      /* Next segment index expected from the sender. */
    uint32_t confirmed_segments;
    uint32_t current_depth;
    uint64_t started_ns;
    uint64_t deadline_ns;
    uint32_t deadline_missed;
    uint32_t cycle_open;
    uint32_t cycle_segments;
    uint32_t cycle_bytes;
    uint32_t aborted;
    tdma_load_segment_result_t last_result;
    tdma_load_segment_class_stats_t stats;
} tdma_load_segment_snapshot_t;

typedef struct {
    tdma_load_segment_policy_t policy;
    tdma_load_segment_snapshot_t snapshot;
    /* Bounded admission window: segment index -> accepted cycle. */
    uint32_t pending_cycle[TDMA_LOAD_SEGMENT_CLASS_COUNT];
    bool pending_valid[TDMA_LOAD_SEGMENT_CLASS_COUNT];
} tdma_load_segment_t;

bool tdma_load_segment_init(tdma_load_segment_t *model);
/* Configure the policy for one class. Configuration must be stopped: an active
 * transfer refuses a generation change instead of silently re-splitting. */
tdma_load_segment_result_t tdma_load_segment_configure(
    tdma_load_segment_t *model, const tdma_load_segment_policy_t *policy);
/* Open one transfer. The deadline starts at the first admission opportunity,
 * not at this call, so a sender that opens early is not punished. */
tdma_load_segment_result_t tdma_load_segment_open(
    tdma_load_segment_t *model, const tdma_load_segment_transfer_t *transfer,
    uint64_t now_ns);
/* Begin one TDMA cycle: resets the per-cycle quota and byte account. */
tdma_load_segment_result_t tdma_load_segment_cycle_begin(
    tdma_load_segment_t *model, uint64_t now_ns);
/* Offer the next segment. Admission is decided by the cycle quota, the bounded
 * queue depth and the transfer deadline, in that order; the configured overflow
 * policy decides what a refusal does. */
tdma_load_segment_result_t tdma_load_segment_offer(
    tdma_load_segment_t *model, uint32_t segment_index, uint32_t segment_bytes,
    uint64_t now_ns);
/* Report a confirmed fact from the physical owner: the segment left the queue
 * because it was delivered (not merely dispatched) or reassembled on RX. */
tdma_load_segment_result_t tdma_load_segment_confirm(
    tdma_load_segment_t *model, uint32_t segment_index, uint64_t now_ns);
/* Reassembly facts from the receiving side of the same transfer. */
tdma_load_segment_result_t tdma_load_segment_reassembled(
    tdma_load_segment_t *model, uint32_t segment_index);
/* Reverse flow (ACK/diagnostic) is admitted independently of TX quota. */
tdma_load_segment_result_t tdma_load_segment_reverse(
    tdma_load_segment_t *model, uint32_t frames, uint32_t bytes);
tdma_load_segment_result_t tdma_load_segment_abort(
    tdma_load_segment_t *model, uint64_t now_ns);
tdma_load_segment_result_t tdma_load_segment_expire(
    tdma_load_segment_t *model, uint64_t now_ns);
void tdma_load_segment_snapshot(const tdma_load_segment_t *model,
                                tdma_load_segment_snapshot_t *snapshot);

#endif
