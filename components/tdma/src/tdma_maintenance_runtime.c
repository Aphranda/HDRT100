#include "tdma_maintenance_runtime.h"

#include <string.h>

#include "tdma_maintenance_stream.h"
#include "tdma_process_image_layout.h"
#include "tdma_spsc_index.h"

static tdma_pio_spi_ring_adapter_t *s_adapter;
static tdma_maintenance_stream_tx_t s_tx;
static tdma_maintenance_stream_rx_t s_rx;
static uint32_t s_local_slot;
static uint32_t s_mailbox_seq;
static volatile uint32_t s_mailbox_calls;
static volatile uint32_t s_chunks;
static volatile uint32_t s_frames;
static volatile uint32_t s_rejects;

/* Small TX queue so a multi-frame control message (e.g. a fragmented REFMEM
 * command) can be queued before the ring starts; the adapter's maintenance
 * mailbox drains it chunk by chunk whenever the adapter asks for one. */
#define TDMA_MAINTENANCE_RUNTIME_TX_QUEUE 8u
static uint8_t s_tx_queue[TDMA_MAINTENANCE_RUNTIME_TX_QUEUE]
                          [TDMA_MAINTENANCE_STREAM_MAX];
static uint16_t s_tx_queue_size[TDMA_MAINTENANCE_RUNTIME_TX_QUEUE];
static uint8_t s_tx_queue_stream_id[TDMA_MAINTENANCE_RUNTIME_TX_QUEUE];
static uint8_t s_tx_queue_generation[TDMA_MAINTENANCE_RUNTIME_TX_QUEUE];
static uint32_t s_tx_queue_head;
static uint32_t s_tx_queue_tail;
static volatile uint32_t s_tx_queued;
static volatile uint32_t s_tx_queue_full;
static volatile uint32_t s_tx_dequeued;
static volatile uint32_t s_rx_chunk_rejects;
static volatile uint32_t s_rx_restarts;
static volatile uint32_t s_rx_duplicate_chunks;

static bool maintenance_mailbox(void *context,
                                uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE])
{
    (void)context;
    s_mailbox_calls++;
    if (s_tx.frame != NULL && s_tx.next_chunk >= s_tx.chunk_count) {
        /* The frame has been fully handed to the wire.  Release its slot only
         * now: the stream keeps a pointer into the queue slot while draining,
         * so advancing head earlier would let the producer overwrite a frame
         * that is still being transmitted. */
        s_tx.frame = NULL;
        const uint32_t head = s_tx_queue_head;
        __atomic_store_n(&s_tx_queue_head,
                         tdma_spsc_next(head, TDMA_MAINTENANCE_RUNTIME_TX_QUEUE),
                         __ATOMIC_RELEASE);
        s_tx_dequeued++;
    }
    if (s_tx.frame == NULL) {
        const uint32_t head = s_tx_queue_head;
        const uint32_t tail =
            __atomic_load_n(&s_tx_queue_tail, __ATOMIC_ACQUIRE);
        if (tdma_spsc_empty(head, tail)) {
            return false;
        }
        if (!tdma_maintenance_stream_tx_begin(
                &s_tx, s_tx_queue[head], s_tx_queue_size[head],
                s_tx_queue_stream_id[head], s_tx_queue_generation[head])) {
            return false;
        }
    }
    uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE];
    if (!tdma_maintenance_stream_next_chunk(&s_tx, chunk)) {
        return false;
    }
    s_chunks++;
    memset(mailbox, 0, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    mailbox[0] = (uint8_t)TDMA_FLIGHT_MAILBOX_MAGIC;
    mailbox[1] = (uint8_t)(TDMA_FLIGHT_MAILBOX_MAGIC >> 8u);
    mailbox[2] = TDMA_FLIGHT_MAILBOX_VERSION;
    mailbox[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
    mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] = (uint8_t)s_local_slot;
    mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 0x0Fu;
    mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET] = (uint8_t)s_mailbox_seq;
    mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET + 1u] =
        (uint8_t)(s_mailbox_seq >> 8u);
    s_mailbox_seq++;
    memcpy(mailbox + TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE,
           chunk, sizeof(chunk));
    const uint16_t crc =
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET);
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
    return true;
}

bool tdma_maintenance_runtime_init(tdma_pio_spi_ring_adapter_t *adapter,
                                   uint32_t local_slot)
{
    if (adapter == NULL || local_slot >= TDMA_RING_NODE_MAX) {
        return false;
    }
    s_adapter = adapter;
    s_local_slot = local_slot;
    memset(&s_tx, 0, sizeof(s_tx));
    memset(&s_rx, 0, sizeof(s_rx));
    s_tx_queue_head = 0u;
    s_tx_queue_tail = 0u;
    tdma_pio_spi_ring_adapter_set_maintenance_mailbox(
        adapter, maintenance_mailbox, NULL);
    return true;
}

bool tdma_maintenance_runtime_ready(void)
{
    return s_adapter != NULL;
}

bool tdma_maintenance_runtime_submit(const uint8_t *frame,
                                     size_t frame_size,
                                     uint8_t stream_id,
                                     uint8_t generation)
{
    if (!tdma_maintenance_runtime_ready()) {
        s_rejects++;
        return false;
    }
    if (frame == NULL || frame_size == 0u ||
        frame_size > TDMA_MAINTENANCE_STREAM_MAX) {
        s_rejects++;
        return false;
    }
    const uint32_t head =
        __atomic_load_n(&s_tx_queue_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = s_tx_queue_tail;
    if (tdma_spsc_full(head, tail, TDMA_MAINTENANCE_RUNTIME_TX_QUEUE)) {
        s_rejects++;
        s_tx_queue_full++;
        return false;
    }
    memcpy(s_tx_queue[tail], frame, frame_size);
    s_tx_queue_size[tail] = (uint16_t)frame_size;
    s_tx_queue_stream_id[tail] = stream_id;
    s_tx_queue_generation[tail] = generation;
    __atomic_store_n(&s_tx_queue_tail,
                     tdma_spsc_next(tail, TDMA_MAINTENANCE_RUNTIME_TX_QUEUE),
                     __ATOMIC_RELEASE);
    s_tx_queued++;
    return true;
}

bool tdma_maintenance_runtime_take(uint8_t *frame,
                                   size_t frame_capacity,
                                   size_t *frame_size,
                                   uint8_t *stream_id,
                                   uint8_t *generation)
{
    if (s_adapter == NULL || frame == NULL || frame_size == NULL) {
        return false;
    }
    uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE];
    uint32_t source_slot = 0u;
    while (tdma_pio_spi_ring_adapter_take_maintenance_chunk(
               s_adapter, chunk, &source_slot)) {
        bool complete = false;
        const uint32_t restart_before = s_rx.restart_count;
        const uint32_t duplicate_before = s_rx.duplicate_count;
        const bool accepted = tdma_maintenance_stream_rx_chunk(&s_rx, chunk,
                                                               &complete);
        s_rx_restarts += s_rx.restart_count - restart_before;
        s_rx_duplicate_chunks += s_rx.duplicate_count - duplicate_before;
        if (!accepted) {
            s_rejects++;
            s_rx_chunk_rejects++;
            continue;
        }
        if (complete) {
            if (s_rx.frame_size > frame_capacity) {
                s_rejects++;
                memset(&s_rx, 0, sizeof(s_rx));
                return false;
            }
            memcpy(frame, s_rx.frame, s_rx.frame_size);
            *frame_size = s_rx.frame_size;
            if (stream_id != NULL) {
                *stream_id = s_rx.stream_id;
            }
            if (generation != NULL) {
                *generation = s_rx.generation;
            }
            s_frames++;
            const uint32_t duplicate = s_rx.duplicate_count;
            const uint32_t restart = s_rx.restart_count;
            memset(&s_rx, 0, sizeof(s_rx));
            s_rx.duplicate_count = duplicate;
            s_rx.restart_count = restart;
            return true;
        }
    }
    return false;
}

void tdma_maintenance_runtime_get_counters(uint32_t *mailbox_calls,
                                           uint32_t *chunks,
                                           uint32_t *frames,
                                           uint32_t *rejects)
{
    if (mailbox_calls != NULL) {
        *mailbox_calls = s_mailbox_calls;
    }
    if (chunks != NULL) {
        *chunks = s_chunks;
    }
    if (frames != NULL) {
        *frames = s_frames;
    }
    if (rejects != NULL) {
        *rejects = s_rejects;
    }
}

void tdma_maintenance_runtime_get_diag(tdma_maintenance_runtime_diag_t *diag)
{
    if (diag == NULL) {
        return;
    }
    memset(diag, 0, sizeof(*diag));
    const uint32_t head =
        __atomic_load_n(&s_tx_queue_head, __ATOMIC_ACQUIRE);
    const uint32_t tail =
        __atomic_load_n(&s_tx_queue_tail, __ATOMIC_ACQUIRE);
    diag->queue_depth =
        tdma_spsc_depth(head, tail, TDMA_MAINTENANCE_RUNTIME_TX_QUEUE);
    diag->enqueued = s_tx_queued;
    diag->dequeued = s_tx_dequeued;
    diag->queue_full = s_tx_queue_full;
    diag->mailbox_calls = s_mailbox_calls;
    diag->chunks = s_chunks;
    diag->frames = s_frames;
    diag->rejects = s_rejects;
    diag->rx_chunk_rejects = s_rx_chunk_rejects;
    diag->rx_restarts = s_rx_restarts;
    diag->rx_duplicate_chunks = s_rx_duplicate_chunks;
}
