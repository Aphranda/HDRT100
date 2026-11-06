#include "tdma_long_frame.h"

#include <string.h>

#include "tdma_spsc_index.h"

enum {
    TDMA_LONG_FRAME_OWNER_FREE = 0u,
    TDMA_LONG_FRAME_OWNER_TX_READY = 1u,
    TDMA_LONG_FRAME_OWNER_RX_READY = 2u,
};

static uint32_t next_index(uint32_t index, uint32_t count)
{
    return tdma_spsc_next(index, count);
}

void tdma_long_frame_fifo_init(tdma_long_frame_fifo_t *fifo)
{
    if (fifo == NULL) {
        return;
    }
    memset(fifo, 0, sizeof(*fifo));
    for (uint32_t i = 0u; i < TDMA_LONG_FRAME_TX_SLOTS; i++) {
        fifo->tx[i].owner = TDMA_LONG_FRAME_OWNER_FREE;
    }
    for (uint32_t i = 0u; i < TDMA_LONG_FRAME_RX_SLOTS; i++) {
        fifo->rx[i].owner = TDMA_LONG_FRAME_OWNER_FREE;
    }
}

static bool publish(tdma_long_frame_slot_t *slots,
                    uint32_t slot_count,
                    volatile uint32_t *head,
                    volatile uint32_t *tail,
                    volatile uint32_t *published,
                    volatile uint32_t *rejected,
                    uint32_t slot_owner,
                    const uint8_t *data,
                    size_t data_size,
                    uint32_t generation,
                    uint32_t sequence,
                    uint32_t segment_mask)
{
    if (slots == NULL || head == NULL || tail == NULL || published == NULL ||
        rejected == NULL || data == NULL || data_size == 0u ||
        data_size > TDMA_LONG_FRAME_MAX || generation == 0u) {
        if (rejected != NULL) {
            (*rejected)++;
        }
        return false;
    }

    const uint32_t next = next_index(*head, slot_count);
    if (next == *tail) {
        (*rejected)++;
        return false;
    }
    tdma_long_frame_slot_t *slot = &slots[*head];
    slot->generation = generation;
    slot->sequence = sequence;
    slot->segment_mask = segment_mask;
    slot->data_size = (uint32_t)data_size;
    memcpy(slot->data, data, data_size);
    slot->owner = slot_owner;
    *head = next;
    (*published)++;
    return true;
}

bool tdma_long_frame_publish_tx(tdma_long_frame_fifo_t *fifo,
                                const uint8_t *data,
                                size_t data_size,
                                uint32_t generation,
                                uint32_t sequence,
                                uint32_t segment_mask)
{
    if (fifo == NULL) {
        return false;
    }
    return publish(fifo->tx, TDMA_LONG_FRAME_TX_SLOTS,
                   &fifo->tx_head, &fifo->tx_tail,
                   &fifo->tx_publish_count, &fifo->tx_reject_count,
                   TDMA_LONG_FRAME_OWNER_TX_READY,
                   data, data_size, generation, sequence, segment_mask);
}

bool tdma_long_frame_publish_rx(tdma_long_frame_fifo_t *fifo,
                                const uint8_t *data,
                                size_t data_size,
                                uint32_t generation,
                                uint32_t sequence,
                                uint32_t segment_mask)
{
    if (fifo == NULL) {
        return false;
    }
    return publish(fifo->rx, TDMA_LONG_FRAME_RX_SLOTS,
                   &fifo->rx_head, &fifo->rx_tail,
                   &fifo->rx_publish_count, &fifo->rx_reject_count,
                   TDMA_LONG_FRAME_OWNER_RX_READY,
                   data, data_size, generation, sequence, segment_mask);
}

static bool acquire(tdma_long_frame_slot_t *slots,
                    uint32_t slot_count,
                    volatile uint32_t *head,
                    volatile uint32_t *tail,
                    volatile uint32_t *acquired,
                    tdma_long_frame_view_t *view)
{
    if (slots == NULL || head == NULL || tail == NULL || view == NULL ||
        *tail == *head) {
        return false;
    }
    tdma_long_frame_slot_t *slot = &slots[*tail];
    if (slot->owner != TDMA_LONG_FRAME_OWNER_TX_READY &&
        slot->owner != TDMA_LONG_FRAME_OWNER_RX_READY) {
        return false;
    }
    view->generation = slot->generation;
    view->sequence = slot->sequence;
    view->segment_mask = slot->segment_mask;
    view->data_size = slot->data_size;
    view->data = slot->data;
    view->slot_index = *tail;
    slot->owner = TDMA_LONG_FRAME_OWNER_FREE;
    *tail = next_index(*tail, slot_count);
    if (acquired != NULL) {
        (*acquired)++;
    }
    return true;
}

bool tdma_long_frame_acquire_tx(tdma_long_frame_fifo_t *fifo,
                                tdma_long_frame_view_t *view)
{
    if (fifo == NULL) {
        return false;
    }
    return acquire(fifo->tx, TDMA_LONG_FRAME_TX_SLOTS,
                   &fifo->tx_head, &fifo->tx_tail,
                   NULL, view);
}

void tdma_long_frame_release_tx(tdma_long_frame_fifo_t *fifo,
                                uint32_t slot_index)
{
    (void)fifo;
    (void)slot_index;
}

bool tdma_long_frame_acquire_rx(tdma_long_frame_fifo_t *fifo,
                                tdma_long_frame_view_t *view)
{
    if (fifo == NULL) {
        return false;
    }
    return acquire(fifo->rx, TDMA_LONG_FRAME_RX_SLOTS,
                   &fifo->rx_head, &fifo->rx_tail,
                   &fifo->rx_acquire_count, view);
}

void tdma_long_frame_release_rx(tdma_long_frame_fifo_t *fifo,
                                uint32_t slot_index)
{
    if (fifo == NULL || slot_index >= TDMA_LONG_FRAME_RX_SLOTS) {
        return;
    }
    fifo->rx[slot_index].owner = TDMA_LONG_FRAME_OWNER_FREE;
    fifo->rx_release_count++;
}

void tdma_long_frame_get_snapshot(const tdma_long_frame_fifo_t *fifo,
                                  tdma_long_frame_snapshot_t *snapshot)
{
    if (fifo == NULL || snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = TDMA_LONG_FRAME_VERSION;
    snapshot->tx_publish_count = fifo->tx_publish_count;
    snapshot->tx_reject_count = fifo->tx_reject_count;
    snapshot->rx_publish_count = fifo->rx_publish_count;
    snapshot->rx_reject_count = fifo->rx_reject_count;
    snapshot->rx_acquire_count = fifo->rx_acquire_count;
    snapshot->rx_release_count = fifo->rx_release_count;
    snapshot->rx_queued_count = tdma_spsc_depth(fifo->rx_tail, fifo->rx_head,
                                                TDMA_LONG_FRAME_RX_SLOTS);
    snapshot->max_frame_size = TDMA_LONG_FRAME_MAX;
}
