#include "tdma_maintenance_stream.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t frame[80];
    for (size_t i = 0u; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)(0x80u + i);
    }

    tdma_maintenance_stream_tx_t tx;
    assert(tdma_maintenance_stream_tx_begin(&tx, frame, sizeof(frame), 7u, 1u));
    assert(tx.chunk_count ==
           (sizeof(frame) + TDMA_MAINTENANCE_CHUNK_DATA_SIZE - 1u) /
               TDMA_MAINTENANCE_CHUNK_DATA_SIZE);

    tdma_maintenance_stream_rx_t rx;
    memset(&rx, 0, sizeof(rx));
    uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE];
    bool complete = false;
    uint32_t chunks = 0u;
    while (tdma_maintenance_stream_next_chunk(&tx, chunk)) {
        chunks++;
        assert(tdma_maintenance_stream_rx_chunk(&rx, chunk, &complete));
    }
    assert(chunks == tx.chunk_count);
    assert(complete);
    assert(rx.frame_size == sizeof(frame));
    assert(memcmp(rx.frame, frame, sizeof(frame)) == 0);

    uint8_t refmem_cmd[56];
    for (size_t i = 0u; i < sizeof(refmem_cmd); i++) {
        refmem_cmd[i] = (uint8_t)(0x44u + i);
    }
    assert(tdma_maintenance_stream_tx_begin(&tx, refmem_cmd,
                                             sizeof(refmem_cmd), 0x52u, 2u));
    memset(&rx, 0, sizeof(rx));
    complete = false;
    chunks = 0u;
    while (tdma_maintenance_stream_next_chunk(&tx, chunk)) {
        chunks++;
        assert(tdma_maintenance_stream_rx_chunk(&rx, chunk, &complete));
    }
    assert(chunks == tx.chunk_count);
    assert(complete);
    assert(rx.frame_size == sizeof(refmem_cmd));
    assert(memcmp(rx.frame, refmem_cmd, sizeof(refmem_cmd)) == 0);

    /* A new header while the old frame is incomplete must restart the
     * reassembler instead of rejecting every later frame. */
    uint8_t frame_a[40];
    uint8_t frame_b[24];
    for (size_t i = 0u; i < sizeof(frame_a); i++) {
        frame_a[i] = (uint8_t)(0x10u + i);
    }
    for (size_t i = 0u; i < sizeof(frame_b); i++) {
        frame_b[i] = (uint8_t)(0x60u + i);
    }
    memset(&rx, 0, sizeof(rx));
    assert(tdma_maintenance_stream_tx_begin(&tx, frame_a, sizeof(frame_a),
                                             1u, 1u));
    assert(tdma_maintenance_stream_next_chunk(&tx, chunk));
    complete = false;
    assert(tdma_maintenance_stream_rx_chunk(&rx, chunk, &complete));
    assert(!complete);

    assert(tdma_maintenance_stream_tx_begin(&tx, frame_b, sizeof(frame_b),
                                             2u, 2u));
    chunks = 0u;
    while (tdma_maintenance_stream_next_chunk(&tx, chunk)) {
        chunks++;
        assert(tdma_maintenance_stream_rx_chunk(&rx, chunk, &complete));
    }
    assert(chunks == tx.chunk_count);
    assert(complete);
    assert(rx.restart_count == 1u);
    assert(rx.frame_size == sizeof(frame_b));
    assert(memcmp(rx.frame, frame_b, sizeof(frame_b)) == 0);

    /* A repeated chunk of the completed frame is counted, not copied twice. */
    const uint32_t duplicates_before = rx.duplicate_count;
    assert(tdma_maintenance_stream_rx_chunk(&rx, chunk, &complete));
    assert(rx.duplicate_count == duplicates_before + 1u);

    printf("tdma_maintenance_stream: 80B + 56B + restart/duplicate OK\n");
    return 0;
}
