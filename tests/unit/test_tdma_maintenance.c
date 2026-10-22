#include "tdma_maintenance.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t frame[TDMA_LONG_FRAME_MAX];
    size_t frame_size;
    uint32_t generation;
    uint32_t sequence;
    uint32_t segment_mask;
    bool pending;
} loopback_t;

static bool loop_send(void *context,
                      const uint8_t *frame,
                      size_t frame_size,
                      uint32_t generation,
                      uint32_t sequence,
                      uint32_t segment_mask)
{
    loopback_t *loop = (loopback_t *)context;
    if (loop == NULL || frame_size == 0u || frame_size > sizeof(loop->frame)) {
        return false;
    }
    memcpy(loop->frame, frame, frame_size);
    loop->frame_size = frame_size;
    loop->generation = generation;
    loop->sequence = sequence;
    loop->segment_mask = segment_mask;
    loop->pending = true;
    return true;
}

static bool loop_poll(void *context,
                      uint8_t *frame,
                      size_t frame_capacity,
                      size_t *frame_size,
                      uint32_t *generation,
                      uint32_t *sequence,
                      uint32_t *segment_mask)
{
    loopback_t *loop = (loopback_t *)context;
    if (loop == NULL || !loop->pending || loop->frame_size > frame_capacity) {
        return false;
    }
    memcpy(frame, loop->frame, loop->frame_size);
    *frame_size = loop->frame_size;
    *generation = loop->generation;
    *sequence = loop->sequence;
    *segment_mask = loop->segment_mask;
    loop->pending = false;
    return true;
}

int main(void)
{
    tdma_long_frame_fifo_t tx;
    tdma_long_frame_fifo_t rx;
    tdma_long_frame_fifo_init(&tx);
    tdma_long_frame_fifo_init(&rx);

    loopback_t loop;
    memset(&loop, 0, sizeof(loop));
    const tdma_maintenance_transport_ops_t ops = {
        .send = loop_send,
        .poll = loop_poll,
    };
    const tdma_maintenance_config_t config = {
        .period_cycles = 2u,
        .window_cycles = 1u,
        .max_window_bytes = TDMA_LONG_FRAME_MAX,
    };
    tdma_maintenance_service_t service;
    assert(tdma_maintenance_init(&service, &tx, &rx, &config, &ops, &loop));

    uint8_t payload[80];
    for (size_t i = 0u; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(0x20u + i);
    }
    assert(tdma_maintenance_submit_tx(&service, payload, sizeof(payload),
                                      7u, 3u, 0x01u));

    /* cycle 1: window opens, frame leaves the independent TX FIFO. */
    tdma_maintenance_on_cycle(&service);
    tdma_maintenance_snapshot_t snapshot;
    tdma_maintenance_get_snapshot(&service, &snapshot);
    assert(snapshot.window_open);
    assert(snapshot.tx_sent_count == 1u);
    assert(snapshot.rx_published_count == 0u);

    /* cycle 2: window closed, receiver publishes the looped frame. */
    tdma_maintenance_on_cycle(&service);
    tdma_maintenance_get_snapshot(&service, &snapshot);
    assert(!snapshot.window_open);
    assert(snapshot.rx_published_count == 1u);

    tdma_long_frame_view_t view;
    assert(tdma_long_frame_acquire_rx(&rx, &view));
    assert(view.data_size == sizeof(payload));
    assert(view.generation == 7u && view.sequence == 3u);
    assert(memcmp(view.data, payload, sizeof(payload)) == 0);
    tdma_long_frame_release_rx(&rx, view.slot_index);

    printf("tdma_maintenance: window + independent FIFO transport OK\n");
    return 0;
}
