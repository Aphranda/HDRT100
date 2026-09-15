#ifndef TDMA_ORIGIN_RELEASE_H
#define TDMA_ORIGIN_RELEASE_H

#include <stdint.h>

typedef enum {
    TDMA_ORIGIN_RELEASE_NONE = 0,
    TDMA_ORIGIN_RELEASE_PREPARING,
    TDMA_ORIGIN_RELEASE_READY,
    TDMA_ORIGIN_RELEASED,
    TDMA_ORIGIN_RELEASE_CANCELLED,
    TDMA_ORIGIN_RELEASE_FAILED
} tdma_origin_release_state_t;

typedef enum {
    TDMA_ORIGIN_RELEASE_ATTEMPT_NONE = 0,
    TDMA_ORIGIN_RELEASE_ATTEMPT_ACCEPTED,
    TDMA_ORIGIN_RELEASE_ATTEMPT_ARGUMENT,
    TDMA_ORIGIN_RELEASE_ATTEMPT_NOT_READY,
    TDMA_ORIGIN_RELEASE_ATTEMPT_DUPLICATE,
    TDMA_ORIGIN_RELEASE_ATTEMPT_STALE,
    TDMA_ORIGIN_RELEASE_ATTEMPT_EXPIRED,
    TDMA_ORIGIN_RELEASE_ATTEMPT_EXHAUSTED
} tdma_origin_release_attempt_reason_t;

/* STOP/ACK diagnostic only. Core1 owns result; Core0 owns attempt. Neither
 * establishes an edge timestamp or gives a packet physical identity. */
typedef struct {
    uint32_t state, trial_epoch, config_seq, seed_sequence, seed_identity;
    uint32_t record_epoch, ready_checks, request_seq;
    uint32_t physical_reject, physical_observed, physical_expected;
    uint64_t ready_ticks, last_ready_ticks, release_ticks;
} tdma_origin_release_result_t;

typedef struct {
    uint32_t attempts, rejected, reason, trial_epoch, config_seq, request_seq;
} tdma_origin_release_attempt_t;

typedef struct {
    tdma_origin_release_result_t result;
    tdma_origin_release_attempt_t attempt;
} tdma_origin_release_snapshot_t;

#endif
