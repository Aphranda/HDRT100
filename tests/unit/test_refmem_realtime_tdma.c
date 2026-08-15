#include "refmem_realtime_tdma.h"

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    bool tx_ok;
    bool rx_ok;
    uint32_t tx_calls;
    uint32_t rx_calls;
} fake_ops_context_t;

static bool fake_transmit(void *context,
                          const uint8_t *frame,
                          size_t frame_size,
                          refmem_spi_physical_role_t role,
                          uint32_t baud_hz,
                          uint32_t deadline_us,
                          refmem_realtime_tdma_exec_status_t *status)
{
    (void)frame;
    (void)baud_hz;
    (void)deadline_us;
    fake_ops_context_t *fake = (fake_ops_context_t *)context;
    fake->tx_calls++;
    status->frame_size = frame_size;
    status->error = 0u;
    status->result = fake->tx_ok && role == REFMEM_SPI_PHYSICAL_ROLE_MASTER
        ? REFMEM_REALTIME_TDMA_EXEC_TX_OK
        : REFMEM_REALTIME_TDMA_EXEC_ERROR;
    return status->result == REFMEM_REALTIME_TDMA_EXEC_TX_OK;
}

static bool fake_receive(void *context,
                         uint8_t *frame,
                         size_t frame_capacity,
                         refmem_spi_physical_role_t role,
                         uint32_t baud_hz,
                         uint32_t deadline_us,
                         refmem_realtime_tdma_exec_status_t *status)
{
    (void)frame;
    (void)frame_capacity;
    (void)baud_hz;
    (void)deadline_us;
    fake_ops_context_t *fake = (fake_ops_context_t *)context;
    fake->rx_calls++;
    if (frame_capacity >= 4u) {
        frame[0] = 0x11u;
        frame[1] = 0x22u;
        frame[2] = 0x33u;
        frame[3] = 0x44u;
    }
    status->frame_size = 4u;
    status->error = fake->rx_ok ? 0u : 3u;
    status->result = fake->rx_ok && role == REFMEM_SPI_PHYSICAL_ROLE_SLAVE
        ? REFMEM_REALTIME_TDMA_EXEC_RX_OK
        : REFMEM_REALTIME_TDMA_EXEC_TIMEOUT;
    return status->result == REFMEM_REALTIME_TDMA_EXEC_RX_OK;
}

static const refmem_realtime_tdma_ops_t s_fake_ops = {
    .transmit = fake_transmit,
    .receive = fake_receive,
};

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static int test_init_snapshot(void)
{
    int failed = 0;
    refmem_realtime_tdma_service_t service;
    refmem_realtime_tdma_snapshot_t snapshot;

    failed += expect_bool("init", refmem_realtime_tdma_init(&service), true);
    failed += expect_bool("snapshot", refmem_realtime_tdma_get_snapshot(&service, &snapshot), true);
    failed += expect_u32("state", snapshot.state, REFMEM_REALTIME_TDMA_STATE_IDLE);
    failed += expect_u32("owner", snapshot.owner_core, 1u);
    failed += expect_u32("intent seq", snapshot.intent_seq, 0u);
    return failed;
}

static int test_tx_intent_completes_on_core1_service(void)
{
    int failed = 0;
    refmem_realtime_tdma_service_t service;
    refmem_realtime_tdma_snapshot_t snapshot;
    fake_ops_context_t fake = {.tx_ok = true, .rx_ok = true};
    const uint8_t frame[] = {0x52u, 0x4Du, 0x01u, 0x00u};
    const refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = 7u,
        .window_index = 3u,
        .deadline_us = 25u,
        .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
        .baud_hz = 25000000u,
        .frame = frame,
        .frame_size = sizeof(frame),
    };

    (void)refmem_realtime_tdma_init(&service);
    (void)refmem_realtime_tdma_bind_ops(&service, &s_fake_ops, &fake);
    failed += expect_bool("submit tx", refmem_realtime_tdma_submit_tx(&service, &config), true);
    (void)refmem_realtime_tdma_get_snapshot(&service, &snapshot);
    failed += expect_u32("pending", snapshot.state, REFMEM_REALTIME_TDMA_STATE_PENDING);
    failed += expect_u32("accepted", snapshot.last_result, REFMEM_REALTIME_TDMA_RESULT_ACCEPTED);
    failed += expect_u32("epoch", snapshot.window_epoch, 7u);
    failed += expect_u32("baud", snapshot.baud_hz, 25000000u);

    refmem_realtime_tdma_core1_service(&service);
    (void)refmem_realtime_tdma_get_snapshot(&service, &snapshot);
    failed += expect_u32("done", snapshot.state, REFMEM_REALTIME_TDMA_STATE_DONE);
    failed += expect_u32("completed seq", snapshot.completed_seq, snapshot.intent_seq);
    failed += expect_u32("ready count", snapshot.ready_count, 1u);
    failed += expect_u32("ready result", snapshot.last_result, REFMEM_REALTIME_TDMA_RESULT_FRAME_READY);
    failed += expect_u32("tx calls", fake.tx_calls, 1u);
    return failed;
}

static int test_rejects_overrun(void)
{
    int failed = 0;
    refmem_realtime_tdma_service_t service;
    refmem_realtime_tdma_snapshot_t snapshot;
    fake_ops_context_t fake = {.tx_ok = true, .rx_ok = true};
    const uint8_t frame[] = {0x01u};
    const refmem_realtime_tdma_intent_config_t config = {
        .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
        .frame = frame,
        .frame_size = sizeof(frame),
    };

    (void)refmem_realtime_tdma_init(&service);
    (void)refmem_realtime_tdma_bind_ops(&service, &s_fake_ops, &fake);
    failed += expect_bool("submit first", refmem_realtime_tdma_submit_tx(&service, &config), true);
    failed += expect_bool("submit busy", refmem_realtime_tdma_submit_tx(&service, &config), false);
    (void)refmem_realtime_tdma_get_snapshot(&service, &snapshot);
    failed += expect_u32("reject count", snapshot.reject_count, 1u);
    failed += expect_u32("still pending", snapshot.state, REFMEM_REALTIME_TDMA_STATE_PENDING);
    return failed;
}

static int test_rx_timeout_maps_to_result(void)
{
    int failed = 0;
    refmem_realtime_tdma_service_t service;
    refmem_realtime_tdma_snapshot_t snapshot;
    fake_ops_context_t fake = {.tx_ok = true, .rx_ok = false};
    const refmem_realtime_tdma_intent_config_t config = {
        .deadline_us = 1000u,
        .role = REFMEM_SPI_PHYSICAL_ROLE_SLAVE,
    };

    (void)refmem_realtime_tdma_init(&service);
    (void)refmem_realtime_tdma_bind_ops(&service, &s_fake_ops, &fake);
    failed += expect_bool("submit rx", refmem_realtime_tdma_submit_rx(&service, &config), true);
    refmem_realtime_tdma_core1_service(&service);
    (void)refmem_realtime_tdma_get_snapshot(&service, &snapshot);
    failed += expect_u32("rx calls", fake.rx_calls, 1u);
    failed += expect_u32("timeout count", snapshot.timeout_count, 1u);
    failed += expect_u32("timeout result", snapshot.last_result, REFMEM_REALTIME_TDMA_RESULT_TIMEOUT);
    failed += expect_u32("timeout state", snapshot.state, REFMEM_REALTIME_TDMA_STATE_ERROR);
    return failed;
}

static int test_rx_result_frame_is_readable(void)
{
    int failed = 0;
    refmem_realtime_tdma_service_t service;
    refmem_realtime_tdma_snapshot_t snapshot;
    fake_ops_context_t fake = {.tx_ok = true, .rx_ok = true};
    uint8_t frame[8];
    size_t frame_size = 0u;
    const refmem_realtime_tdma_intent_config_t config = {
        .deadline_us = 1000u,
        .role = REFMEM_SPI_PHYSICAL_ROLE_SLAVE,
    };

    (void)refmem_realtime_tdma_init(&service);
    (void)refmem_realtime_tdma_bind_ops(&service, &s_fake_ops, &fake);
    failed += expect_bool("submit rx ok", refmem_realtime_tdma_submit_rx(&service, &config), true);
    refmem_realtime_tdma_core1_service(&service);
    (void)refmem_realtime_tdma_get_snapshot(&service, &snapshot);
    failed += expect_u32("rx ok size", snapshot.frame_size, 4u);
    failed += expect_bool("get rx frame",
                          refmem_realtime_tdma_get_result_frame(&service,
                                                                frame,
                                                                sizeof(frame),
                                                                &frame_size),
                          true);
    failed += expect_u32("rx frame size", (uint32_t)frame_size, 4u);
    failed += expect_u32("rx frame byte0", frame[0], 0x11u);
    failed += expect_u32("rx frame byte3", frame[3], 0x44u);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_init_snapshot();
    failed += test_tx_intent_completes_on_core1_service();
    failed += test_rejects_overrun();
    failed += test_rx_timeout_maps_to_result();
    failed += test_rx_result_frame_is_readable();
    if (failed != 0) {
        (void)printf("refmem_realtime_tdma tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("refmem_realtime_tdma tests passed\n");
    return 0;
}
