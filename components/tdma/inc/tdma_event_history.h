#ifndef TDMA_EVENT_HISTORY_H
#define TDMA_EVENT_HISTORY_H

#include "tdma_event_observer.h"

#define TDMA_EVENT_HISTORY_CAPACITY 16u

/* One Core1-owned diagnostic history, shared by all compiled node counts.
 * The physical owner exports a bounded guarded read-only window; readers
 * acquire neither ownership nor a retained capture lease. No expected
 * physical identity is accepted by this API. */
typedef union {
    struct {
        uint64_t rx_elapsed_cycles;
        uint64_t tx_elapsed_cycles;
        uint32_t raw_rx;
        uint32_t raw_tx;
        uint32_t sequence;
        uint32_t ordinal;
    };
    uint32_t words[8]; /* Runtime publication/copy uses atomic words. */
} tdma_event_history_record_t;

typedef enum {
    TDMA_EVENT_HISTORY_OK = 0,
    TDMA_EVENT_HISTORY_RETIRED,
    TDMA_EVENT_HISTORY_INACTIVE,
    TDMA_EVENT_HISTORY_BAD_STATE,
    TDMA_EVENT_HISTORY_BAD_ARGUMENT,
    TDMA_EVENT_HISTORY_BAD_EPOCH,
    TDMA_EVENT_HISTORY_BAD_COUNT,
    TDMA_EVENT_HISTORY_INCOMPLETE_BATCH,
    TDMA_EVENT_HISTORY_BAD_FLAGS,
    TDMA_EVENT_HISTORY_BAD_ORDINAL,
    TDMA_EVENT_HISTORY_BAD_SEQUENCE,
    TDMA_EVENT_HISTORY_BAD_TIME,
    TDMA_EVENT_HISTORY_BAD_START_BOUNDS,
    TDMA_EVENT_HISTORY_NOT_FOUND,
    TDMA_EVENT_HISTORY_AMBIGUOUS
} tdma_event_history_reason_t;

typedef struct {
    tdma_event_history_record_t records[TDMA_EVENT_HISTORY_CAPACITY];
    /* Immutable conservative start bracket for this epoch. Per-event observer
     * anchors may narrow; lookup returns this initial bracket, not an exact
     * event anchor or a qualified physical timestamp. */
    tdma_event_interval_t initial_start;
    uint64_t accepted;
    uint32_t epoch;
    uint32_t pio_hz;
    uint32_t next;
    uint32_t count;
    uint32_t evicted;
    uint32_t oldest_ordinal;
    tdma_event_history_reason_t reason;
    tdma_event_history_reason_t query_reason;
    bool active;
    /* Occupies existing tail padding. Core1 alone writes; never reset by
     * runtime start/retire. Shared readers bound time to exclude guard ABA. */
    uint32_t publication_guard;
} tdma_event_history_t;

/* Cold initialization only; no concurrent reader may exist. */
void tdma_event_history_init(tdma_event_history_t *history);
/* Clears ALL available records. Preserves highest started epoch and prior
 * rejection reason. STOP, prepare, or observer INVALID must call this. */
void tdma_event_history_retire(tdma_event_history_t *history);
bool tdma_event_history_start(tdma_event_history_t *history, uint32_t epoch,
                              uint32_t pio_hz, tdma_event_interval_t initial_start);
/* Call once for the complete batch only AFTER the owner's final fault check.
 * source_joined is observer.joined, independent of count: a truncated/dropped
 * batch must not become a complete publication. At most MAX_RECORDS inputs;
 * validate every input before committing any. Any rejection retires history.
 * Sequence may wrap uint32; ordinal may NOT wrap. Capacity eviction is normal
 * diagnostic loss, not proof that this depth covers every configuration. */
bool tdma_event_history_append(tdma_event_history_t *history,
                               const tdma_event_record_t *records, size_t count,
                               uint64_t source_joined);
/* Searches at most CAPACITY entries. Every failure zeroes out; success always
 * returns diagnostic_only/physical_first_unproved/identity_unproved, with
 * timestamp_valid and dpll_eligible false. Wrong epoch/missing data do not
 * invalidate healthy current history. Ambiguity retires it. Caller must not
 * retain a copied result as live evidence after STOP, INVALID or a new epoch. */
bool tdma_event_history_lookup(tdma_event_history_t *history, uint32_t epoch,
                               uint32_t sequence, tdma_event_record_t *out);

#endif
