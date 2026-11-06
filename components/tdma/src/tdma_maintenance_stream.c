#include "tdma_maintenance_stream.h"

#include <string.h>

#include "algo_checksum.h"
#include "algo_wire.h"

static void put_u16(uint8_t *dst, uint16_t value)
{
    algo_le16_store(dst, value);
}

static uint16_t get_u16(const uint8_t *src)
{
    return algo_le16_load(src);
}

static void put_u32(uint8_t *dst, uint32_t value)
{
    algo_le32_store(dst, value);
}

static uint32_t get_u32(const uint8_t *src)
{
    return algo_le32_load(src);
}

uint32_t tdma_maintenance_stream_crc32(const uint8_t *data, size_t size)
{
    if (data == NULL) {
        return 0u;
    }
    return algo_crc32_iso_hdlc_compute(data, size);
}

bool tdma_maintenance_stream_tx_begin(tdma_maintenance_stream_tx_t *tx,
                                      const uint8_t *frame,
                                      size_t frame_size,
                                      uint8_t stream_id,
                                      uint8_t generation)
{
    if (tx == NULL || frame == NULL || frame_size == 0u ||
        frame_size > TDMA_MAINTENANCE_STREAM_MAX) {
        return false;
    }
    memset(tx, 0, sizeof(*tx));
    tx->frame = frame;
    tx->frame_size = frame_size;
    tx->frame_crc32 = tdma_maintenance_stream_crc32(frame, frame_size);
    tx->stream_id = stream_id;
    tx->generation = generation;
    tx->chunk_count = (uint8_t)((frame_size + TDMA_MAINTENANCE_CHUNK_DATA_SIZE - 1u) /
                                TDMA_MAINTENANCE_CHUNK_DATA_SIZE);
    return true;
}

bool tdma_maintenance_stream_next_chunk(tdma_maintenance_stream_tx_t *tx,
                                        uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE])
{
    if (tx == NULL || chunk == NULL || tx->frame == NULL ||
        tx->next_chunk >= tx->chunk_count) {
        return false;
    }
    memset(chunk, 0, TDMA_MAINTENANCE_CHUNK_SIZE);
    const size_t offset = (size_t)tx->next_chunk * TDMA_MAINTENANCE_CHUNK_DATA_SIZE;
    const size_t remaining = tx->frame_size - offset;
    const size_t copy = remaining < TDMA_MAINTENANCE_CHUNK_DATA_SIZE
        ? remaining : TDMA_MAINTENANCE_CHUNK_DATA_SIZE;
    put_u16(chunk + 0u, TDMA_MAINTENANCE_STREAM_MAGIC);
    chunk[2] = tx->stream_id;
    chunk[3] = tx->generation;
    chunk[4] = tx->next_chunk;
    chunk[5] = tx->chunk_count;
    put_u16(chunk + 6u, (uint16_t)tx->frame_size);
    put_u32(chunk + 8u, tx->frame_crc32);
    memcpy(chunk + TDMA_MAINTENANCE_CHUNK_HEADER_SIZE,
           tx->frame + offset, copy);
    tx->next_chunk++;
    return true;
}

bool tdma_maintenance_stream_rx_chunk(tdma_maintenance_stream_rx_t *rx,
                                      const uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE],
                                      bool *complete)
{
    if (rx == NULL || chunk == NULL || complete == NULL ||
        get_u16(chunk + 0u) != TDMA_MAINTENANCE_STREAM_MAGIC) {
        return false;
    }
    const uint8_t index = chunk[4];
    const uint8_t count = chunk[5];
    const uint16_t total = get_u16(chunk + 6u);
    const uint32_t crc = get_u32(chunk + 8u);
    if (count == 0u || index >= count ||
        count > TDMA_MAINTENANCE_CHUNK_MAX ||
        total == 0u || total > TDMA_MAINTENANCE_STREAM_MAX) {
        return false;
    }
    if (rx->chunk_count == 0u) {
        rx->stream_id = chunk[2];
        rx->generation = chunk[3];
        rx->chunk_count = count;
        rx->frame_size = total;
        rx->frame_crc32 = crc;
    } else if (rx->chunk_count != count || rx->frame_size != total ||
               rx->frame_crc32 != crc ||
               rx->stream_id != chunk[2] || rx->generation != chunk[3]) {
        /* A lost chunk means the old frame can never complete.  Abandon it
         * and start the new frame from this header; otherwise every later
         * frame would be rejected and one loss would wedge the receiver. */
        const uint32_t duplicate = rx->duplicate_count;
        const uint32_t restart = rx->restart_count;
        memset(rx, 0, sizeof(*rx));
        rx->duplicate_count = duplicate;
        rx->restart_count = restart + 1u;
        rx->stream_id = chunk[2];
        rx->generation = chunk[3];
        rx->chunk_count = count;
        rx->frame_size = total;
        rx->frame_crc32 = crc;
    }
    const size_t offset = (size_t)index * TDMA_MAINTENANCE_CHUNK_DATA_SIZE;
    if (offset >= rx->frame_size) {
        return false;
    }
    size_t copy = rx->frame_size - offset;
    if (copy > TDMA_MAINTENANCE_CHUNK_DATA_SIZE) {
        copy = TDMA_MAINTENANCE_CHUNK_DATA_SIZE;
    }
    memcpy(rx->frame + offset,
           chunk + TDMA_MAINTENANCE_CHUNK_HEADER_SIZE, copy);
    const uint8_t chunk_bit = (uint8_t)(1u << index);
    if ((rx->received_mask & chunk_bit) != 0u) {
        rx->duplicate_count++;
    }
    rx->received_mask |= chunk_bit;
    const uint8_t full_mask = (uint8_t)((1u << rx->chunk_count) - 1u);
    *complete = (rx->received_mask & full_mask) == full_mask &&
        tdma_maintenance_stream_crc32(rx->frame, rx->frame_size) == rx->frame_crc32;
    return true;
}
