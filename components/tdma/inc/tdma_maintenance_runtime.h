#ifndef TDMA_MAINTENANCE_RUNTIME_H
#define TDMA_MAINTENANCE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_pio_spi_ring_adapter.h"

#define TDMA_MAINTENANCE_RUNTIME_MAX TDMA_LONG_FRAME_MAX

bool tdma_maintenance_runtime_init(tdma_pio_spi_ring_adapter_t *adapter,
                                   uint32_t local_slot);
bool tdma_maintenance_runtime_ready(void);
/* Local node slot recorded at init; used by the OTA carrier to build the
 * peer target mask without exposing the ring profile. */
bool tdma_maintenance_runtime_get_local_slot(uint32_t *slot);
bool tdma_maintenance_runtime_submit(const uint8_t *frame,
                                     size_t frame_size,
                                     uint8_t stream_id,
                                     uint8_t generation,
                                     uint8_t target_mask);
bool tdma_maintenance_runtime_take(uint8_t *frame,
                                   size_t frame_capacity,
                                   size_t *frame_size,
                                   uint8_t *stream_id,
                                   uint8_t *generation);

/* N3.2.4 frame-level ACK: the receiver reports the completed maintenance
 * frame's (stream_id, generation); only a matching ACK releases the TX slot.
 * A NACK or a missing ACK retries the whole frame with a new generation. */
bool tdma_maintenance_runtime_ack(uint8_t stream_id,
                                  uint8_t generation,
                                  uint8_t source_slot,
                                  bool ok);

/* Test/debug only: drop the next N maintenance chunks (the stream index still
 * advances) to exercise retry.  Zero on INIT. */
void tdma_maintenance_runtime_drop_chunks(uint32_t count);

void tdma_maintenance_runtime_get_counters(uint32_t *mailbox_calls,
                                           uint32_t *chunks,
                                           uint32_t *frames,
                                           uint32_t *rejects);

/* N3.2.1 read-only queue/stream diagnostics.  `queue_depth` is sampled at call
 * time; the other counters are monotonic.  `rx_chunk_rejects` counts chunks
 * rejected by the stream reassembler (bad header or a chunk that belongs to a
 * different frame while one is still incomplete). */
typedef struct {
    uint32_t queue_depth;
    uint32_t enqueued;
    uint32_t dequeued;
    uint32_t queue_full;
    uint32_t mailbox_calls;
    uint32_t chunks;
    uint32_t frames;
    uint32_t rejects;
    uint32_t rx_chunk_rejects;
    uint32_t rx_restarts;
    uint32_t rx_duplicate_chunks;
    uint32_t tx_awaiting_ack;
    uint32_t tx_ack_ok;
    uint32_t tx_retries;
    uint32_t tx_fail;
    uint32_t tx_dropped_chunks;
} tdma_maintenance_runtime_diag_t;

void tdma_maintenance_runtime_get_diag(tdma_maintenance_runtime_diag_t *diag);

#endif /* TDMA_MAINTENANCE_RUNTIME_H */
