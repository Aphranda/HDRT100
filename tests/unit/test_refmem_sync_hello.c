#include "refmem_pio_spi_adapter.h"
#include "refmem_sync_hello.h"

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

static int test_hello_payload_and_adapter_poll(void)
{
    int failed = 0;
    refmem_pio_spi_adapter_t adapter;
    refmem_transport_caps_t caps;
    refmem_board_capability_entry_t board;
    refmem_sync_hello_config_t config;
    refmem_sync_hello_payload_t payload;
    refmem_sync_frame_header_t header;
    const uint8_t *decoded_payload = NULL;
    uint16_t decoded_payload_size = 0u;
    uint8_t frame[128];
    uint8_t polled[128];
    size_t frame_size = 0u;
    size_t polled_size = 0u;

    (void)memset(&board, 0, sizeof(board));
    board.capability_mask =
        REFMEM_APP_CAP_BOARD | REFMEM_APP_CAP_REFMEM | REFMEM_APP_CAP_VDC |
        REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;
    board.io_constraint_mask = REFMEM_APP_IO_PIO_SPI_SYNC;
    board.ip_core_mask = REFMEM_APP_IP_PIO_SPI_SYNC_DELTA;

    config.build_id_crc32 = 0x11223344u;
    config.layout_version = 1u;
    config.application_crc32 = 0x55667788u;
    config.config_crc32 = 0x99AABBCCu;
    config.source_slot = 0u;
    config.target_mask = 0x02u;
    config.epoch_id = 10u;
    config.run_id = 20u;
    config.seq32 = 30u;
    config.compact_time = 40u;

    failed += expect_bool("adapter init",
                          refmem_pio_spi_adapter_init(&adapter, 128u, 128u, 50u),
                          true);
    failed += expect_bool("adapter caps",
                          refmem_pio_spi_adapter_get_caps(&adapter, &caps),
                          true);
    failed += expect_bool("hello payload",
                          refmem_sync_hello_payload_from_board(&config,
                                                               &board,
                                                               &caps,
                                                               &payload),
                          true);
    failed += expect_u32("hello adapter id", payload.adapter_id, REFMEM_TRANSPORT_ADAPTER_ID_PIO_SPI);
    failed += expect_u32("hello adapter caps", payload.adapter_caps, caps.capability_mask);
    failed += expect_u32("hello io", payload.io_constraint_mask, REFMEM_APP_IO_PIO_SPI_SYNC);
    failed += expect_u32("hello ip", payload.ip_core_mask, REFMEM_APP_IP_PIO_SPI_SYNC_DELTA);

    failed += expect_bool("hello encode",
                          refmem_sync_hello_encode_frame(&config,
                                                         &payload,
                                                         frame,
                                                         sizeof(frame),
                                                         &frame_size),
                          true);
    failed += expect_bool("adapter inject hello",
                          refmem_pio_spi_adapter_inject_rx_frame(&adapter,
                                                                 frame,
                                                                 frame_size,
                                                                 123u),
                          true);
    failed += expect_bool("adapter poll hello",
                          refmem_pio_spi_adapter_poll(&adapter,
                                                      polled,
                                                      sizeof(polled),
                                                      &polled_size),
                          true);
    failed += expect_u32("poll hello size", (uint32_t)polled_size, (uint32_t)frame_size);
    failed += expect_u32("poll hello bytes",
                         memcmp(frame, polled, frame_size) == 0 ? 1u : 0u,
                         1u);
    failed += expect_u32("hello validate",
                         refmem_sync_frame_validate(polled,
                                                    polled_size,
                                                    &header,
                                                    &decoded_payload,
                                                    &decoded_payload_size),
                         REFMEM_SYNC_FRAME_OK);
    failed += expect_u32("hello type", header.frame_type, REFMEM_SYNC_FRAME_HELLO);
    failed += expect_u32("hello source", header.source_slot, config.source_slot);
    failed += expect_u32("hello payload size", decoded_payload_size, sizeof(payload));
    failed += expect_u32("hello payload bytes",
                         memcmp(decoded_payload, &payload, sizeof(payload)) == 0 ? 1u : 0u,
                         1u);
    return failed;
}

int main(void)
{
    const int failed = test_hello_payload_and_adapter_poll();

    if (failed != 0) {
        (void)printf("refmem_sync_hello tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_sync_hello tests passed\n");
    return 0;
}
