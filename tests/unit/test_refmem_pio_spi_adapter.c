#include "refmem_pio_spi_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

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

static bool make_hello_frame(uint8_t *frame, size_t frame_capacity, size_t *frame_size)
{
    refmem_sync_hello_payload_t hello;
    refmem_sync_frame_header_t header;

    (void)memset(&hello, 0, sizeof(hello));
    hello.layout_version = 1u;
    hello.adapter_id = REFMEM_TRANSPORT_ADAPTER_ID_PIO_SPI;
    hello.max_payload_size = REFMEM_SYNC_FRAME_PAYLOAD_MAX;
    hello.preferred_mtu = 128u;

    if (!refmem_sync_frame_header_init(&header,
                                       REFMEM_SYNC_FRAME_HELLO,
                                       0u,
                                       0u,
                                       0x02u,
                                       1u,
                                       2u,
                                       3u,
                                       0u,
                                       4u,
                                       &hello,
                                       sizeof(hello))) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    &hello,
                                    sizeof(hello),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static int test_caps_and_snapshot(void)
{
    int failed = 0;
    refmem_pio_spi_adapter_t adapter;
    refmem_transport_caps_t caps;
    refmem_pio_spi_adapter_snapshot_t snapshot;

    failed += expect_bool("adapter init",
                          refmem_pio_spi_adapter_init(&adapter, 128u, 128u, 50u),
                          true);
    failed += expect_bool("adapter get caps",
                          refmem_pio_spi_adapter_get_caps(&adapter, &caps),
                          true);
    failed += expect_u32("caps adapter id", caps.adapter_id, REFMEM_TRANSPORT_ADAPTER_ID_PIO_SPI);
    failed += expect_u32("caps max payload", caps.max_payload_size, 128u);
    failed += expect_bool("caps counters bit",
                          (caps.capability_mask & REFMEM_TRANSPORT_CAP_COUNTERS) != 0u,
                          true);

    failed += expect_bool("adapter get snapshot",
                          refmem_pio_spi_adapter_get_snapshot(&adapter, &snapshot),
                          true);
    failed += expect_u32("snapshot state", snapshot.state, REFMEM_TRANSPORT_STATE_IDLE);
    failed += expect_u32("snapshot latency", snapshot.latency_class_us, 50u);
    return failed;
}

static int test_send_stub_counters(void)
{
    int failed = 0;
    refmem_pio_spi_adapter_t adapter;
    refmem_pio_spi_adapter_snapshot_t snapshot;
    uint8_t frame[128];
    size_t frame_size = 0u;

    (void)refmem_pio_spi_adapter_init(&adapter, 128u, 128u, 50u);
    failed += expect_bool("make hello frame", make_hello_frame(frame, sizeof(frame), &frame_size), true);
    failed += expect_bool("send frame", refmem_pio_spi_adapter_send(&adapter, frame, frame_size), true);
    (void)refmem_pio_spi_adapter_get_snapshot(&adapter, &snapshot);
    failed += expect_u32("tx count", snapshot.tx_count, 1u);
    failed += expect_u32("tx last size", snapshot.last_tx_size, (uint32_t)frame_size);
    failed += expect_u32("tx last error", snapshot.last_error, REFMEM_TRANSPORT_ERROR_NONE);

    frame[0] = 0u;
    failed += expect_bool("send bad frame", refmem_pio_spi_adapter_send(&adapter, frame, frame_size), false);
    (void)refmem_pio_spi_adapter_get_snapshot(&adapter, &snapshot);
    failed += expect_u32("tx reject count", snapshot.tx_reject_count, 1u);
    failed += expect_u32("bad frame count", snapshot.bad_frame_count, 1u);
    failed += expect_u32("bad frame error", snapshot.last_error, REFMEM_TRANSPORT_ERROR_BAD_FRAME);
    return failed;
}

static int test_poll_stub_and_reset(void)
{
    int failed = 0;
    refmem_pio_spi_adapter_t adapter;
    refmem_pio_spi_adapter_snapshot_t snapshot;
    uint8_t frame[32];
    size_t frame_size = 1u;

    (void)refmem_pio_spi_adapter_init(&adapter, 64u, 64u, 100u);
    failed += expect_bool("poll empty",
                          refmem_pio_spi_adapter_poll(&adapter,
                                                      frame,
                                                      sizeof(frame),
                                                      &frame_size),
                          false);
    (void)refmem_pio_spi_adapter_get_snapshot(&adapter, &snapshot);
    failed += expect_u32("poll size zero", (uint32_t)frame_size, 0u);
    failed += expect_u32("rx empty count", snapshot.rx_empty_count, 1u);
    failed += expect_u32("poll no frame error", snapshot.last_error, REFMEM_TRANSPORT_ERROR_NO_RX_FRAME);

    refmem_pio_spi_adapter_reset_counters(&adapter);
    (void)refmem_pio_spi_adapter_get_snapshot(&adapter, &snapshot);
    failed += expect_u32("reset rx empty", snapshot.rx_empty_count, 0u);
    failed += expect_u32("reset state", snapshot.state, REFMEM_TRANSPORT_STATE_IDLE);
    failed += expect_u32("reset max payload", snapshot.max_payload_size, 64u);
    return failed;
}

static int test_inject_and_poll_hello_frame(void)
{
    int failed = 0;
    refmem_pio_spi_adapter_t adapter;
    refmem_pio_spi_adapter_snapshot_t snapshot;
    uint8_t frame[128];
    uint8_t polled[128];
    size_t frame_size = 0u;
    size_t polled_size = 0u;

    (void)refmem_pio_spi_adapter_init(&adapter, 128u, 128u, 50u);
    failed += expect_bool("make rx hello frame",
                          make_hello_frame(frame, sizeof(frame), &frame_size),
                          true);
    failed += expect_bool("inject rx hello",
                          refmem_pio_spi_adapter_inject_rx_frame(&adapter,
                                                                 frame,
                                                                 frame_size,
                                                                 1234u),
                          true);
    (void)refmem_pio_spi_adapter_get_snapshot(&adapter, &snapshot);
    failed += expect_u32("rx pending after inject", snapshot.rx_pending, 1u);
    failed += expect_u32("last rx timestamp", snapshot.last_rx_timestamp, 1234u);

    failed += expect_bool("poll rx hello",
                          refmem_pio_spi_adapter_poll(&adapter,
                                                      polled,
                                                      sizeof(polled),
                                                      &polled_size),
                          true);
    failed += expect_u32("polled size", (uint32_t)polled_size, (uint32_t)frame_size);
    failed += expect_u32("polled data match", memcmp(frame, polled, frame_size) == 0 ? 1u : 0u, 1u);
    (void)refmem_pio_spi_adapter_get_snapshot(&adapter, &snapshot);
    failed += expect_u32("rx count", snapshot.rx_count, 1u);
    failed += expect_u32("rx pending after poll", snapshot.rx_pending, 0u);
    failed += expect_u32("poll last error", snapshot.last_error, REFMEM_TRANSPORT_ERROR_NONE);
    return failed;
}

static int test_inject_rejects_bad_payload_crc(void)
{
    int failed = 0;
    refmem_pio_spi_adapter_t adapter;
    refmem_pio_spi_adapter_snapshot_t snapshot;
    uint8_t frame[128];
    size_t frame_size = 0u;

    (void)refmem_pio_spi_adapter_init(&adapter, 128u, 128u, 50u);
    failed += expect_bool("make bad crc hello frame",
                          make_hello_frame(frame, sizeof(frame), &frame_size),
                          true);
    frame[REFMEM_SYNC_FRAME_HEADER_SIZE] ^= 0x01u;
    failed += expect_bool("inject bad crc",
                          refmem_pio_spi_adapter_inject_rx_frame(&adapter,
                                                                 frame,
                                                                 frame_size,
                                                                 5678u),
                          false);
    (void)refmem_pio_spi_adapter_get_snapshot(&adapter, &snapshot);
    failed += expect_u32("bad crc count", snapshot.bad_frame_count, 1u);
    failed += expect_u32("bad crc pending", snapshot.rx_pending, 0u);
    failed += expect_u32("bad crc error", snapshot.last_error, REFMEM_TRANSPORT_ERROR_BAD_FRAME);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_caps_and_snapshot();
    failed += test_send_stub_counters();
    failed += test_poll_stub_and_reset();
    failed += test_inject_and_poll_hello_frame();
    failed += test_inject_rejects_bad_payload_crc();

    if (failed != 0) {
        (void)printf("refmem_pio_spi_adapter tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_pio_spi_adapter tests passed\n");
    return 0;
}
