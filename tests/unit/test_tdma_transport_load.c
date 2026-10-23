#include "tdma_transport_load.h"

#include <stdio.h>
#include <string.h>

static int expect(const char *name, int actual, int expected)
{
    if (actual != expected) {
        printf("%s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    tdma_transport_load_t load;
    const tdma_transport_load_descriptor_t descriptor = {
        .load_id = 7u,
        .node_id = 0u,
        .payload_class = 2u,
        .seat_class = 3u,
        .window_period_cycles = 8u,
        .window_bytes = 4u,
        .max_frame_size = 56u,
        .required_mask = 0x0Fu,
    };
    uint8_t payload[56];
    uint8_t out[56];
    uint32_t out_size = 0u;
    int rc = 0;

    memset(payload, 0xA5, sizeof(payload));
    rc |= expect("init", tdma_transport_load_init(&load, &descriptor), 1);
    rc |= expect("empty", tdma_transport_load_pending(&load), 0);
    rc |= expect("submit", tdma_transport_load_submit(&load, payload, sizeof(payload)), 1);
    rc |= expect("pending", tdma_transport_load_pending(&load), 1);
    rc |= expect("window open", tdma_transport_load_window_open(&load), 1);
    rc |= expect("take", tdma_transport_load_take(&load, out, sizeof(out), &out_size), 1);
    rc |= expect("size", out_size == sizeof(payload), 1);
    rc |= expect("bytes", memcmp(out, payload, sizeof(payload)) == 0, 1);
    rc |= expect("empty after take", tdma_transport_load_pending(&load), 0);

    for (uint32_t i = 0u; i < descriptor.window_bytes; i++) {
        tdma_transport_load_advance(&load);
    }
    rc |= expect("window closed", tdma_transport_load_window_open(&load), 0);

    if (rc == 0) {
        printf("tdma_transport_load tests passed\n");
    }
    return rc;
}
