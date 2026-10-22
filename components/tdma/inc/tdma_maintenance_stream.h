#ifndef TDMA_MAINTENANCE_STREAM_H
#define TDMA_MAINTENANCE_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Fragmentation for the maintenance payload.
 *
 * The ring origin mailbox is TDMA_FLIGHT_SHORT_SLOT_SIZE (32 B), so a bounded
 * maintenance frame is carried as an ordered stream of short chunks.  The
 * stream layer is independent of T2 and of the transport queue.
 */

#define TDMA_MAINTENANCE_CHUNK_SIZE 22u
#define TDMA_MAINTENANCE_CHUNK_HEADER_SIZE 12u
#define TDMA_MAINTENANCE_CHUNK_DATA_SIZE \
    (TDMA_MAINTENANCE_CHUNK_SIZE - TDMA_MAINTENANCE_CHUNK_HEADER_SIZE)
#define TDMA_MAINTENANCE_STREAM_MAX (TDMA_MAINTENANCE_CHUNK_DATA_SIZE * 8u)
#define TDMA_MAINTENANCE_CHUNK_MAX \
    ((TDMA_MAINTENANCE_STREAM_MAX + TDMA_MAINTENANCE_CHUNK_DATA_SIZE - 1u) / \
     TDMA_MAINTENANCE_CHUNK_DATA_SIZE)
#define TDMA_MAINTENANCE_STREAM_MAGIC 0x4D53u

typedef struct {
    const uint8_t *frame;
    size_t frame_size;
    uint32_t frame_crc32;
    uint8_t stream_id;
    uint8_t generation;
    uint8_t chunk_count;
    uint8_t next_chunk;
} tdma_maintenance_stream_tx_t;

typedef struct {
    uint8_t frame[TDMA_MAINTENANCE_STREAM_MAX];
    size_t frame_size;
    uint32_t frame_crc32;
    uint8_t stream_id;
    uint8_t generation;
    uint8_t chunk_count;
    uint8_t received_mask;
} tdma_maintenance_stream_rx_t;

uint32_t tdma_maintenance_stream_crc32(const uint8_t *data, size_t size);

bool tdma_maintenance_stream_tx_begin(tdma_maintenance_stream_tx_t *tx,
                                      const uint8_t *frame,
                                      size_t frame_size,
                                      uint8_t stream_id,
                                      uint8_t generation);

/* Returns false when the stream is complete or the output buffer is small. */
bool tdma_maintenance_stream_next_chunk(tdma_maintenance_stream_tx_t *tx,
                                        uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE]);

/* Returns true when this chunk was accepted and the whole frame is complete. */
bool tdma_maintenance_stream_rx_chunk(tdma_maintenance_stream_rx_t *rx,
                                      const uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE],
                                      bool *complete);

#endif /* TDMA_MAINTENANCE_STREAM_H */
