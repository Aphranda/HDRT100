#ifndef TDMA_ORIGIN_STOP_CAUSE_H
#define TDMA_ORIGIN_STOP_CAUSE_H

#include <stdint.h>

/* Why the origin lifetime path stopped the ring: written by Core1 at the
 * revoke itself, first stop of each trial epoch, kept until the next trial. */
enum {
    TDMA_ORIGIN_STOP_UNAUTHORIZED = 1u << 0,   /* lifetime authorization lost */
    TDMA_ORIGIN_STOP_COMM_FAULT = 1u << 1,     /* adapter comm state machine FAULT */
};
/* Failed terms of tdma_runtime_owner_origin_authorized(), one bit each. */
enum {
    TDMA_ORIGIN_AUTH_FAULT_LATCHED = 1u << 0,  /* see fault_site */
    TDMA_ORIGIN_AUTH_NO_EPOCH = 1u << 1,
    TDMA_ORIGIN_AUTH_DISABLED = 1u << 2,
    TDMA_ORIGIN_AUTH_BURST_LIMIT = 1u << 3,
    TDMA_ORIGIN_AUTH_VERSION = 1u << 4,
    TDMA_ORIGIN_AUTH_EPOCH_SUPERSEDED = 1u << 5, /* Core0 revoked or replaced it */
    TDMA_ORIGIN_AUTH_MODEL_EPOCH = 1u << 6,
    TDMA_ORIGIN_AUTH_ADMISSION_CLASS = 1u << 7,
    TDMA_ORIGIN_AUTH_CAPABILITY = 1u << 8,
    TDMA_ORIGIN_AUTH_CONFIG_SEQ = 1u << 9,
    TDMA_ORIGIN_AUTH_RING_DISABLED = 1u << 10,
    TDMA_ORIGIN_AUTH_FOUNDATION = 1u << 11,
    TDMA_ORIGIN_AUTH_CALIBRATION = 1u << 12,
    TDMA_ORIGIN_AUTH_CLOCK = 1u << 13,
    TDMA_ORIGIN_AUTH_EXPIRED = 1u << 14,
};
/* First site that latched the owner-private trial fault. */
enum {
    TDMA_ORIGIN_FAULT_NONE = 0u, TDMA_ORIGIN_FAULT_ADMISSION = 1u,
    TDMA_ORIGIN_FAULT_BEGIN = 2u, TDMA_ORIGIN_FAULT_BUILD = 3u,
    TDMA_ORIGIN_FAULT_UNHEALTHY = 4u, TDMA_ORIGIN_FAULT_BLACKOUT = 5u,
};
/* BEGIN refusal details owned by the runtime owner (reason
 * TDMA_ORIGIN_REJECT_BEGIN); physical preconditions use 1..47. */
enum {
    TDMA_ORIGIN_BEGIN_OWNER_CONTEXT = 0x81u, TDMA_ORIGIN_BEGIN_OWNER_UNAUTHORIZED = 0x82u,
    TDMA_ORIGIN_BEGIN_OWNER_REARM = 0x83u, TDMA_ORIGIN_BEGIN_OWNER_ABORT_POLL = 0x84u,
    TDMA_ORIGIN_BEGIN_OWNER_CONFIG = 0x85u,
};
typedef struct {
    uint32_t stop_count;            /* lifetime STOPs since boot */
    uint32_t trial_epoch, origin_epoch;
    uint32_t cause, auth_mask, fault_site;
    uint32_t comm_state, comm_last_error;
    uint32_t trial_config_seq, ring_config_seq;
    uint32_t release_state;
    /* First BEGIN refusal of the trial, valid when fault_site == BEGIN. */
    uint32_t begin_reject, begin_observed, begin_expected;
    uint64_t stop_ticks;
} tdma_origin_stop_cause_t;

#endif
