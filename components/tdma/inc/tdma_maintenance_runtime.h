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
bool tdma_maintenance_runtime_submit(const uint8_t *frame,
                                     size_t frame_size,
                                     uint8_t stream_id,
                                     uint8_t generation);
bool tdma_maintenance_runtime_take(uint8_t *frame,
                                   size_t frame_capacity,
                                   size_t *frame_size,
                                   uint8_t *stream_id,
                                   uint8_t *generation);
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
} tdma_maintenance_runtime_diag_t;

void tdma_maintenance_runtime_get_diag(tdma_maintenance_runtime_diag_t *diag);

#endif /* TDMA_MAINTENANCE_RUNTIME_H */
