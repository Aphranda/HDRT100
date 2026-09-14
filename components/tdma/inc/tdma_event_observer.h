#ifndef TDMA_EVENT_OBSERVER_H
#define TDMA_EVENT_OBSERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Core1-owned diagnostic consumer. No shared writer, allocation or hardware I/O.
 * RX/TX words are IN X,32 then IN Y,32 from the SAME SM. Sequence words are
 * wire little-endian bytes shifted left by PIO and are byte-swapped here.
 * This interface deliberately supplies NO formal timestamp/DPLL admission. */
#define TDMA_EVENT_STREAMS 3u
#define TDMA_EVENT_FIFO_WORDS 8u
#define TDMA_EVENT_MAX_RECORDS 4u
#define TDMA_EVENT_ALL_EMPTY_MASK 7u

typedef enum {
    TDMA_EVENT_STOPPED = 0,
    TDMA_EVENT_ACTIVE,
    TDMA_EVENT_INVALID
} tdma_event_state_t;

typedef enum {
    TDMA_EVENT_OK = 0,
    TDMA_EVENT_BAD_ARGUMENT,
    TDMA_EVENT_BAD_EPOCH,
    TDMA_EVENT_PRE_FAULT,
    TDMA_EVENT_POST_FAULT,
    TDMA_EVENT_SERVICE_AGE,
    TDMA_EVENT_STAGING_OVERFLOW,
    TDMA_EVENT_JOIN_TIMEOUT,
    TDMA_EVENT_ORDINAL,
    TDMA_EVENT_SEQUENCE,
    TDMA_EVENT_LIFT_NO_CANDIDATE,
    TDMA_EVENT_LIFT_AMBIGUOUS,
    TDMA_EVENT_TIME_OVERFLOW,
    TDMA_EVENT_ABSOLUTE_TIME,
    TDMA_EVENT_FRAME_SPACING,
    TDMA_EVENT_PAIR_SKEW
} tdma_event_reason_t;

/* Adapter maps sticky stall/overflow, sequence bad-PC, disabled observers,
 * changed clock/config, or dirty initial state to these diagnostic bits. */
enum {
    TDMA_EVENT_FAULT_STALL = 1u << 0,
    TDMA_EVENT_FAULT_OVERFLOW = 1u << 1,
    TDMA_EVENT_FAULT_SEQUENCE = 1u << 2,
    TDMA_EVENT_FAULT_DISABLED = 1u << 3,
    TDMA_EVENT_FAULT_CONFIG = 1u << 4,
    TDMA_EVENT_FAULT_DIRTY_START = 1u << 5
};

typedef struct {
    uint64_t lo;
    uint64_t hi;
} tdma_event_interval_t;

typedef struct {
    uint32_t pio_hz;
    uint64_t epoch_limit_cycles;
    uint64_t join_timeout_cycles;
    uint64_t min_frame_cycles;
    uint64_t min_tx_delay_cycles;
    uint64_t max_tx_delay_cycles;
} tdma_event_config_t;

typedef struct {
    uint32_t epoch;
    /* One conservative absolute cycle bracket encompassing ALL FIFO reads.
     * empty_mask bits assert FIFO empty at its bounded drain's end. */
    tdma_event_interval_t observed;
    uint32_t pre_faults;
    uint32_t post_faults;
    uint32_t words[TDMA_EVENT_STREAMS][TDMA_EVENT_FIFO_WORDS];
    uint8_t count[TDMA_EVENT_STREAMS];
    uint8_t empty_mask;
} tdma_event_batch_t;

typedef struct {
    uint32_t epoch;
    uint32_t ordinal;
    uint32_t sequence;
    uint32_t raw_rx;
    uint32_t raw_tx;
    uint64_t rx_elapsed_cycles;
    uint64_t tx_elapsed_cycles;
    tdma_event_interval_t start_bounds;
    /* Always true: first wire value only establishes a diagnostic baseline;
     * neither first physical event nor complete packet identity is proved. */
    bool diagnostic_only;
    bool physical_first_unproved;
    bool identity_unproved;
    bool timestamp_valid;
    bool dpll_eligible;
} tdma_event_record_t;

typedef struct {
    uint32_t word;
    tdma_event_interval_t age;
} tdma_event_word_t;

typedef struct {
    tdma_event_state_t state;
    tdma_event_reason_t reason;
    uint32_t epoch;
    uint32_t fault_bits;
    tdma_event_config_t config;
    tdma_event_interval_t start;
    tdma_event_interval_t anchor_bounds;
    tdma_event_interval_t last_observed;
    uint64_t last_empty[TDMA_EVENT_STREAMS];
    tdma_event_word_t pending[TDMA_EVENT_STREAMS][TDMA_EVENT_FIFO_WORDS];
    uint8_t pending_count[TDMA_EVENT_STREAMS];
    tdma_event_word_t previous[2];
    uint64_t elapsed[2];
    uint64_t joined;
    uint32_t first_sequence;
    uint32_t last_sequence;
    uint32_t reads_last;
} tdma_event_observer_t;

void tdma_event_observer_init(tdma_event_observer_t *observer);
/* Caller proves common restart, X=Y=UINT32_MAX, clean FIFOs and pinned clock.
 * Only STOPPED may start. Epoch must strictly increase and cannot wrap. */
bool tdma_event_observer_start(tdma_event_observer_t *observer,
                               const tdma_event_config_t *config,
                               uint32_t epoch, tdma_event_interval_t start,
                               uint32_t initial_faults);
/* Retires pending data before adapter disables/clears observer SMs. */
void tdma_event_observer_stop(tdma_event_observer_t *observer);
/* At most 24 input words and 4 diagnostic outputs; no unbounded work. A
 * failure invalidates the entire batch and epoch. Ignore all out[] when the
 * return is zero; after INVALID all earlier epoch records are also ineligible.
 * Single Core1 owner must not call stop/start concurrently with feed. */
size_t tdma_event_observer_feed(tdma_event_observer_t *observer,
                                const tdma_event_batch_t *batch,
                                tdma_event_record_t out[TDMA_EVENT_MAX_RECORDS]);

/* Unique lift: 2*((previous-current) mod 2^32) + overhead + (current>previous)
 * + q*(2*2^32+1), q>=0. overhead is 1 at common seed or 5 for adjacent pairs.
 * Rejects invalid ranges, absent/ambiguous lifts and overflow. */
tdma_event_reason_t tdma_event_observer_lift(uint32_t previous, uint32_t current,
                                           uint64_t lower, uint64_t upper,
                                           uint32_t overhead, uint64_t *elapsed);

#endif
