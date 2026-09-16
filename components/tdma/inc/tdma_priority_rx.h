#ifndef TDMA_PRIORITY_RX_H
#define TDMA_PRIORITY_RX_H

#include <stdbool.h>
#include <stdint.h>

#define TDMA_PRIORITY_RX_CAPACITY 4u
#define TDMA_PRIORITY_RX_HEADER_BYTES 32u
#define TDMA_PRIORITY_RX_MAILBOX_BYTES 32u

/* A retained transport record, not an edge timestamp or DPLL grant.
 * irq_entry_ticks is handler entry time; candidate_word is a DMA coordinate.
 * The sequence is the complete carrier sequence, not the mailbox event low word. */
typedef struct {
    uint64_t irq_entry_ticks, candidate_word;
    uint32_t epoch, sequence;
    uint8_t header[TDMA_PRIORITY_RX_HEADER_BYTES];
    uint8_t mailbox[TDMA_PRIORITY_RX_MAILBOX_BYTES];
} tdma_priority_rx_record_t;

typedef struct {
    uint32_t schema, active, epoch, irq_count, publish_count;
    uint32_t overwrite_count, duplicate_count, sequence_gap_count;
    uint32_t reject_count, last_reject, latest_sequence, latest_mailbox_seq16;
    uint32_t irq_last_cycles, irq_max_cycles;
    uint64_t irq_total_cycles, last_entry_ticks;
    uint32_t retained_mask, sequence[TDMA_PRIORITY_RX_CAPACITY];
} tdma_priority_rx_snapshot_t;

/* Core1 scheduler accounting only, read with the priority IRQ disabled.
 * Retained records and their cross-core snapshot keep their existing API. */
typedef struct {
    uint64_t cycles;
    uint32_t epoch, count, maximum, active;
} tdma_priority_rx_counters_t;

/* Instrumented IRQ-body wall time, in TIMER1 clk_sys cycles. Each maximum
 * may come from a different accepted capture; their sum is not a WCET.
 * 0: entry through copy, 1: copy through DMA/header recheck,
 * 2: validation, 3: publication through the existing body end timestamp. */
typedef struct {
    uint32_t samples;
    uint32_t max_cycles[4];
} tdma_priority_rx_timing_t;

typedef enum {
    TDMA_PRIORITY_RX_OK = 0,
    TDMA_PRIORITY_RX_INACTIVE,
    TDMA_PRIORITY_RX_GEOMETRY,
    TDMA_PRIORITY_RX_DMA,
    TDMA_PRIORITY_RX_INCOMPLETE,
    TDMA_PRIORITY_RX_OVERWRITTEN,
    TDMA_PRIORITY_RX_HEADER,
    TDMA_PRIORITY_RX_HEADER_CRC,
    TDMA_PRIORITY_RX_MAILBOX,
    TDMA_PRIORITY_RX_MAILBOX_CRC,
    TDMA_PRIORITY_RX_SEQUENCE,
    TDMA_PRIORITY_RX_EXHAUSTED,
    TDMA_PRIORITY_RX_CADENCE
} tdma_priority_rx_reason_t;

typedef struct {
    uint32_t packet_bytes, reference_slot, local_slot, node_count;
    uint32_t schedule_crc32, profile_crc32;
} tdma_priority_rx_binding_t;

/* Exactly one serialized producer: Core1 IRQ while active, Core1 owner while
 * the IRQ source is disabled. Readers never mutate these objects. */
typedef struct {
    uint32_t guard;
    tdma_priority_rx_snapshot_t status;
    uint32_t published[sizeof(tdma_priority_rx_snapshot_t) / 4u];
    uint32_t record[TDMA_PRIORITY_RX_CAPACITY][sizeof(tdma_priority_rx_record_t) / 4u];
} tdma_priority_rx_t;

bool tdma_priority_rx_start(tdma_priority_rx_t *lane);
bool tdma_priority_rx_rebase(tdma_priority_rx_t *lane);
void tdma_priority_rx_stop(tdma_priority_rx_t *lane);
void tdma_priority_rx_reject(tdma_priority_rx_t *lane, uint32_t reason);
void tdma_priority_rx_finish_irq(tdma_priority_rx_t *lane, uint64_t ticks, uint32_t cycles);
bool tdma_priority_rx_candidate(uint64_t produced, uint32_t stride, uint32_t frame_words,
    uint32_t byte_shift, uint32_t bit_shift, uint32_t ring_words, uint64_t *candidate);
uint32_t tdma_priority_rx_validate(const tdma_priority_rx_binding_t *binding,
    const tdma_priority_rx_record_t *record);
bool tdma_priority_rx_publish(tdma_priority_rx_t *lane, const tdma_priority_rx_record_t *record);
/* One bounded read attempt; callers must not suspend an attempt across 2^31
 * completed producer publications (one full uint32 guard circle). This API
 * does not guarantee consistency after an arbitrarily long reader pause.
 * Carrier wrap advances epoch atomically and retires earlier records while
 * preserving cumulative counters. Epoch exhaustion stops the lane until reboot. */
bool tdma_priority_rx_snapshot(const tdma_priority_rx_t *lane, tdma_priority_rx_snapshot_t *out);
/* Diagnostic exact copy remains readable after STOP while retained. */
bool tdma_priority_rx_copy(const tdma_priority_rx_t *lane, uint32_t epoch,
    uint32_t sequence, tdma_priority_rx_record_t *out);
/* One bounded exact copy that also requires active in the same guard window.
 * STOP revokes this read; all failures leave *out unchanged. No consumption. */
bool tdma_priority_rx_copy_live(const tdma_priority_rx_t *lane, uint32_t epoch,
    uint32_t sequence, tdma_priority_rx_record_t *out);

#endif
