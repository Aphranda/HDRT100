#include "tdma_long_frame.h"
#include "refmem_cmd_codec.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    tdma_long_frame_fifo_t fifo;
    tdma_long_frame_fifo_init(&fifo);

    uint8_t payload[REFMEM_CMD_WIRE_SIZE] = {0};
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(0x40u + i);
    }

    assert(tdma_long_frame_publish_tx(&fifo, payload, sizeof(payload), 1u, 7u, 1u));
    tdma_long_frame_view_t view;
    assert(tdma_long_frame_acquire_tx(&fifo, &view));
    assert(view.data_size == sizeof(payload));
    assert(view.generation == 1u && view.sequence == 7u);
    assert(memcmp(view.data, payload, sizeof(payload)) == 0);
    tdma_long_frame_release_tx(&fifo, view.slot_index);

    assert(tdma_long_frame_publish_tx(&fifo, payload, sizeof(payload), 1u, 8u, 1u));
    assert(!tdma_long_frame_publish_tx(&fifo, payload, sizeof(payload), 1u, 9u, 1u));

    for (uint32_t i = 0u; i < 1u; i++) {
        assert(tdma_long_frame_publish_rx(&fifo, payload, sizeof(payload),
                                          2u, 100u + i, 1u));
    }
    assert(!tdma_long_frame_publish_rx(&fifo, payload, sizeof(payload),
                                       2u, 200u, 1u));

    uint32_t seen = 0u;
    while (tdma_long_frame_acquire_rx(&fifo, &view)) {
        seen++;
        tdma_long_frame_release_rx(&fifo, view.slot_index);
    }
    assert(seen == 1u);

    tdma_long_frame_snapshot_t snapshot;
    tdma_long_frame_get_snapshot(&fifo, &snapshot);
    assert(snapshot.tx_publish_count == 2u);
    assert(snapshot.tx_reject_count == 1u);
    assert(snapshot.rx_publish_count == 1u);
    assert(snapshot.rx_reject_count == 1u);
    assert(snapshot.rx_acquire_count == 1u);

    printf("tdma_long_frame: independent FIFO contract OK\n");
    return 0;
}
