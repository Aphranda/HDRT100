#ifndef TDMA_RX_EVENT_CANDIDATE_H
#define TDMA_RX_EVENT_CANDIDATE_H

#include <stdint.h>

/* A completed diagnostic query, never a retained packet/timestamp lease.
 * Header validation is a READY caller precondition. Event sequence and time
 * remain independently observed; matching them does not prove wire identity. */
typedef enum {
    TDMA_RX_EVENT_NONE = 0,
    TDMA_RX_EVENT_MATCHED,
    TDMA_RX_EVENT_OBSERVER_DISABLED,
    TDMA_RX_EVENT_BAD_ARGUMENT,
    TDMA_RX_EVENT_NO_CAPTURE,
    TDMA_RX_EVENT_UNSUPPORTED_PERSONA,
    TDMA_RX_EVENT_ARM_STALE,
    TDMA_RX_EVENT_OBSERVATION_STALE,
    TDMA_RX_EVENT_OBSERVER_UNAVAILABLE,
    TDMA_RX_EVENT_PIN_MISSING,
    TDMA_RX_EVENT_PIN_STALE,
    TDMA_RX_EVENT_HISTORY_UNAVAILABLE,
    TDMA_RX_EVENT_BAD_ENVELOPE,
    TDMA_RX_EVENT_NOT_FOUND,
    TDMA_RX_EVENT_AMBIGUOUS,
    TDMA_RX_EVENT_HISTORY_REJECTED,
    TDMA_RX_EVENT_CAPTURE_STALE
} tdma_rx_event_reason_t;

enum {
    TDMA_RX_EVENT_DIAGNOSTIC_ONLY = 1u << 0,
    TDMA_RX_EVENT_HISTORICAL = 1u << 1,
    TDMA_RX_EVENT_HEADER_VALIDATED = 1u << 2,
    TDMA_RX_EVENT_MAILBOX_UNKNOWN = 1u << 3,
    TDMA_RX_EVENT_IDENTITY_UNPROVED = 1u << 4,
    TDMA_RX_EVENT_PHYSICAL_FIRST_UNPROVED = 1u << 5,
    TDMA_RX_EVENT_MATCH_PRESENT = 1u << 6,
    TDMA_RX_EVENT_RETIRED = 1u << 7,
    /* Reserved negative assertions: this diagnostic path never sets these. */
    TDMA_RX_EVENT_TIMESTAMP_VALID = 1u << 8,
    TDMA_RX_EVENT_DPLL_ELIGIBLE = 1u << 9
};

typedef struct {
    /* Saturating boot-lifetime counters survive prepare/STOP/new ARM. */
    uint32_t query_count, matched_count, unavailable_count;
    uint32_t stale_count, missing_count, ambiguous_count;
    /* Everything below describes one query; no pointers or packet payload. */
    uint64_t capture_id, arm_epoch, observation_epoch, dma_candidate;
    uint64_t observer_arm_epoch, station_age_ns, history_accepted;
    uint64_t rx_elapsed_cycles, tx_elapsed_cycles;
    uint64_t start_lo_cycles, start_hi_cycles;
    uint32_t capture_observer_epoch, query_observer_epoch;
    uint32_t packet_sequence, event_sequence, event_ordinal;
    uint32_t reason, flags, history_reason, history_query_reason;
    uint32_t history_active, history_count, history_oldest_ordinal, history_evicted;
    uint32_t observer_state, pio_hz;
} tdma_rx_event_candidate_snapshot_t;

_Static_assert(sizeof(tdma_rx_event_candidate_snapshot_t) == 176u,
               "RX event diagnostic snapshot must remain bounded");

#endif
