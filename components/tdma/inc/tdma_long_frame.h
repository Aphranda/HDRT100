#ifndef TDMA_LONG_FRAME_H
#define TDMA_LONG_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Independent maintenance long-frame FIFO.
 *
 * This FIFO deliberately shares no storage, counters or ownership flags with
 * tdma_flight_fifo (the T2/process-image flight path).  A producer may publish
 * a bounded maintenance frame here without touching T2 slots or counters.
 *
 * Transport scheduling is supplied by the caller; this module only owns the
 * queue contract so a future ring maintenance window can consume it without
 * changing the T2 path.
 */

#define TDMA_LONG_FRAME_VERSION 1u
#define TDMA_LONG_FRAME_MAX 96u
#define TDMA_LONG_FRAME_TX_SLOTS 2u
#define TDMA_LONG_FRAME_RX_SLOTS 2u

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    uint32_t segment_mask;
    uint32_t data_size;
    const uint8_t *data;
    uint32_t slot_index;
} tdma_long_frame_view_t;

typedef struct {
    volatile uint32_t owner;
    volatile uint32_t generation;
    volatile uint32_t sequence;
    volatile uint32_t segment_mask;
    volatile uint32_t data_size;
    uint8_t data[TDMA_LONG_FRAME_MAX];
} tdma_long_frame_slot_t;

typedef struct {
    tdma_long_frame_slot_t tx[TDMA_LONG_FRAME_TX_SLOTS];
    tdma_long_frame_slot_t rx[TDMA_LONG_FRAME_RX_SLOTS];
    volatile uint32_t tx_head;
    volatile uint32_t tx_tail;
    volatile uint32_t rx_head;
    volatile uint32_t rx_tail;
    volatile uint32_t tx_publish_count;
    volatile uint32_t tx_reject_count;
    volatile uint32_t rx_publish_count;
    volatile uint32_t rx_reject_count;
    volatile uint32_t rx_acquire_count;
    volatile uint32_t rx_release_count;
} tdma_long_frame_fifo_t;

typedef struct {
    uint32_t version;
    uint32_t tx_publish_count;
    uint32_t tx_reject_count;
    uint32_t rx_publish_count;
    uint32_t rx_reject_count;
    uint32_t rx_acquire_count;
    uint32_t rx_release_count;
    uint32_t rx_queued_count;
    uint32_t max_frame_size;
} tdma_long_frame_snapshot_t;

void tdma_long_frame_fifo_init(tdma_long_frame_fifo_t *fifo);

bool tdma_long_frame_publish_tx(tdma_long_frame_fifo_t *fifo,
                                const uint8_t *data,
                                size_t data_size,
                                uint32_t generation,
                                uint32_t sequence,
                                uint32_t segment_mask);

/* Core1/transport side: publish a received maintenance frame. */
bool tdma_long_frame_publish_rx(tdma_long_frame_fifo_t *fifo,
                                const uint8_t *data,
                                size_t data_size,
                                uint32_t generation,
                                uint32_t sequence,
                                uint32_t segment_mask);

bool tdma_long_frame_acquire_tx(tdma_long_frame_fifo_t *fifo,
                                tdma_long_frame_view_t *view);
void tdma_long_frame_release_tx(tdma_long_frame_fifo_t *fifo,
                                uint32_t slot_index);

bool tdma_long_frame_acquire_rx(tdma_long_frame_fifo_t *fifo,
                                tdma_long_frame_view_t *view);
void tdma_long_frame_release_rx(tdma_long_frame_fifo_t *fifo,
                                uint32_t slot_index);

void tdma_long_frame_get_snapshot(const tdma_long_frame_fifo_t *fifo,
                                  tdma_long_frame_snapshot_t *snapshot);

#endif /* TDMA_LONG_FRAME_H */
