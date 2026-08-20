#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_flight_fifo.h"

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr,
            "FAIL %s: got %u expected %u\n",
            name,
            actual ? 1u : 0u,
            expected ? 1u : 0u);
    return 1;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr,
            "FAIL %s: got %lu expected %lu\n",
            name,
            (unsigned long)actual,
            (unsigned long)expected);
    return 1;
}

static int expect_mem(const char *name,
                      const uint8_t *actual,
                      const uint8_t *expected,
                      size_t size)
{
    if (actual != NULL && expected != NULL &&
        memcmp(actual, expected, size) == 0) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: memory mismatch\n", name);
    return 1;
}

int main(void)
{
    int failed = 0;
    tdma_flight_fifo_t fifo;
    tdma_flight_tx_view_t tx_view;
    tdma_flight_rx_view_t rx_view;
    tdma_flight_fifo_snapshot_t snapshot;
    const uint8_t tx0[] = {1u, 2u, 3u, 4u};
    const uint8_t tx1[] = {5u, 6u, 7u, 8u};
    const uint8_t tx2[] = {9u, 10u, 11u, 12u};
    const uint8_t rx0[] = {21u, 22u, 23u};
    const uint8_t rx1[] = {31u, 32u, 33u};

    failed += expect_bool("init", tdma_flight_fifo_init(&fifo), true);
    failed += expect_bool("no tx before publish",
                          tdma_flight_fifo_core1_acquire_tx(&fifo, &tx_view),
                          false);

    failed += expect_bool("publish tx0",
                          tdma_flight_fifo_core0_publish_tx(&fifo,
                                                            tx0,
                                                            sizeof(tx0),
                                                            1u,
                                                            100u,
                                                            0x1u),
                          true);
    failed += expect_bool("acquire tx0",
                          tdma_flight_fifo_core1_acquire_tx(&fifo, &tx_view),
                          true);
    failed += expect_u32("tx0 generation", tx_view.generation, 1u);
    failed += expect_u32("tx0 sequence", tx_view.sequence, 100u);
    failed += expect_bool("tx0 not reused", tx_view.reused_previous, false);
    failed += expect_mem("tx0 payload", tx_view.data, tx0, sizeof(tx0));

    failed += expect_bool("reuse tx0",
                          tdma_flight_fifo_core1_acquire_tx(&fifo, &tx_view),
                          true);
    failed += expect_bool("tx0 reused", tx_view.reused_previous, true);
    failed += expect_u32("reuse generation", tx_view.generation, 1u);

    failed += expect_bool("publish tx1 while tx0 active",
                          tdma_flight_fifo_core0_publish_tx(&fifo,
                                                            tx1,
                                                            sizeof(tx1),
                                                            2u,
                                                            101u,
                                                            0x2u),
                          true);
    failed += expect_bool("reject tx2 no inactive slot",
                          tdma_flight_fifo_core0_publish_tx(&fifo,
                                                            tx2,
                                                            sizeof(tx2),
                                                            3u,
                                                            102u,
                                                            0x4u),
                          false);
    failed += expect_bool("boundary switches to tx1",
                          tdma_flight_fifo_core1_acquire_tx(&fifo, &tx_view),
                          true);
    failed += expect_u32("tx1 generation", tx_view.generation, 2u);
    failed += expect_u32("tx1 sequence", tx_view.sequence, 101u);
    failed += expect_mem("tx1 payload", tx_view.data, tx1, sizeof(tx1));
    tdma_flight_fifo_core1_release_tx(&fifo);
    failed += expect_bool("no tx after release",
                          tdma_flight_fifo_core1_acquire_tx(&fifo, &tx_view),
                          false);

    failed += expect_bool("publish rx0",
                          tdma_flight_fifo_core1_publish_rx(&fifo,
                                                            rx0,
                                                            sizeof(rx0),
                                                            10u,
                                                            200u,
                                                            0x10u,
                                                            123456ull,
                                                            0xA5u),
                          true);
    failed += expect_bool("publish rx1",
                          tdma_flight_fifo_core1_publish_rx(&fifo,
                                                            rx1,
                                                            sizeof(rx1),
                                                            11u,
                                                            201u,
                                                            0x20u,
                                                            123556ull,
                                                            0x5Au),
                          true);
    failed += expect_bool("acquire rx0",
                          tdma_flight_fifo_core0_acquire_rx(&fifo, &rx_view),
                          true);
    failed += expect_u32("rx0 generation", rx_view.generation, 10u);
    failed += expect_u32("rx0 sequence", rx_view.sequence, 200u);
    failed += expect_u32("rx0 quality", rx_view.quality_flags, 0xA5u);
    failed += expect_mem("rx0 payload", rx_view.data, rx0, sizeof(rx0));
    failed += expect_bool("release rx0",
                          tdma_flight_fifo_core0_release_rx(&fifo,
                                                            rx_view.slot_index),
                          true);
    failed += expect_bool("acquire rx1",
                          tdma_flight_fifo_core0_acquire_rx(&fifo, &rx_view),
                          true);
    failed += expect_u32("rx1 generation", rx_view.generation, 11u);
    failed += expect_mem("rx1 payload", rx_view.data, rx1, sizeof(rx1));
    failed += expect_bool("release rx1",
                          tdma_flight_fifo_core0_release_rx(&fifo,
                                                            rx_view.slot_index),
                          true);
    failed += expect_bool("no rx after release",
                          tdma_flight_fifo_core0_acquire_rx(&fifo, &rx_view),
                          false);

    failed += expect_bool("snapshot",
                          tdma_flight_fifo_get_snapshot(&fifo, &snapshot),
                          true);
    failed += expect_u32("tx publish count", snapshot.tx_publish_count, 2u);
    failed += expect_u32("tx reject count",
                         snapshot.tx_publish_reject_count,
                         1u);
    failed += expect_u32("tx stale count",
                         snapshot.tx_image_stale_count,
                         1u);
    failed += expect_u32("rx publish count", snapshot.rx_publish_count, 2u);
    failed += expect_u32("rx release count", snapshot.rx_release_count, 2u);
    failed += expect_u32("rx queued empty", snapshot.rx_queued_count, 0u);

    failed += expect_bool("publish tx corrupt candidate",
                          tdma_flight_fifo_core0_publish_tx(&fifo,
                                                            tx2,
                                                            sizeof(tx2),
                                                            3u,
                                                            102u,
                                                            0x4u),
                          true);
    fifo.tx_ring[fifo.tx_tail % TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT].sequence++;
    failed += expect_bool("reject corrupt tx descriptor without wedging",
                          tdma_flight_fifo_core1_acquire_tx(&fifo, &tx_view),
                          false);
    failed += expect_bool("publish tx after corrupt descriptor",
                          tdma_flight_fifo_core0_publish_tx(&fifo,
                                                            tx0,
                                                            sizeof(tx0),
                                                            4u,
                                                            103u,
                                                            0x1u),
                          true);
    failed += expect_bool("acquire tx after corrupt descriptor",
                          tdma_flight_fifo_core1_acquire_tx(&fifo, &tx_view),
                          true);
    failed += expect_u32("tx recovery sequence", tx_view.sequence, 103u);
    tdma_flight_fifo_core1_release_tx(&fifo);

    failed += expect_bool("publish rx corrupt candidate",
                          tdma_flight_fifo_core1_publish_rx(&fifo,
                                                            rx0,
                                                            sizeof(rx0),
                                                            12u,
                                                            202u,
                                                            0x10u,
                                                            123656ull,
                                                            0u),
                          true);
    fifo.rx_ring[fifo.rx_tail % TDMA_FLIGHT_RX_FRAME_SLOT_COUNT].sequence++;
    failed += expect_bool("reject corrupt rx descriptor without wedging",
                          tdma_flight_fifo_core0_acquire_rx(&fifo, &rx_view),
                          false);
    failed += expect_bool("publish rx after corrupt descriptor",
                          tdma_flight_fifo_core1_publish_rx(&fifo,
                                                            rx1,
                                                            sizeof(rx1),
                                                            13u,
                                                            203u,
                                                            0x20u,
                                                            123756ull,
                                                            0u),
                          true);
    failed += expect_bool("acquire rx after corrupt descriptor",
                          tdma_flight_fifo_core0_acquire_rx(&fifo, &rx_view),
                          true);
    failed += expect_u32("rx recovery sequence", rx_view.sequence, 203u);
    failed += expect_bool("release rx recovery",
                          tdma_flight_fifo_core0_release_rx(
                              &fifo, rx_view.slot_index),
                          true);

    if (failed != 0) {
        return 1;
    }
    puts("tdma_flight_fifo tests passed");
    return 0;
}
