#ifndef REFMEM_PIO_SPI_ADAPTER_H
#define REFMEM_PIO_SPI_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_sync_frame.h"
#include "refmem_transport_adapter.h"

typedef struct {
    uint32_t adapter_id;
    uint32_t state;
    uint32_t capability_mask;
    uint32_t max_payload_size;
    uint32_t preferred_mtu;
    uint32_t latency_class_us;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t tx_reject_count;
    uint32_t rx_empty_count;
    uint32_t bad_frame_count;
    uint32_t drop_count;
    uint32_t timeout_count;
    uint32_t last_error;
    uint32_t last_tx_size;
    uint32_t last_rx_size;
    uint32_t last_rx_timestamp;
    uint32_t rx_pending;
} refmem_pio_spi_adapter_snapshot_t;

typedef struct {
    refmem_transport_caps_t caps;
    refmem_pio_spi_adapter_snapshot_t snapshot;
    uint8_t rx_frame[REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX];
    size_t rx_frame_size;
    bool initialized;
} refmem_pio_spi_adapter_t;

bool refmem_pio_spi_adapter_init(refmem_pio_spi_adapter_t *adapter,
                                 uint16_t max_payload_size,
                                 uint16_t preferred_mtu,
                                 uint32_t latency_class_us);
void refmem_pio_spi_adapter_reset_counters(refmem_pio_spi_adapter_t *adapter);
bool refmem_pio_spi_adapter_get_caps(const refmem_pio_spi_adapter_t *adapter,
                                     refmem_transport_caps_t *caps);
bool refmem_pio_spi_adapter_get_snapshot(
    const refmem_pio_spi_adapter_t *adapter,
    refmem_pio_spi_adapter_snapshot_t *snapshot);
bool refmem_pio_spi_adapter_send(refmem_pio_spi_adapter_t *adapter,
                                 const uint8_t *frame,
                                 size_t frame_size);
bool refmem_pio_spi_adapter_inject_rx_frame(refmem_pio_spi_adapter_t *adapter,
                                            const uint8_t *frame,
                                            size_t frame_size,
                                            uint32_t timestamp);
bool refmem_pio_spi_adapter_poll(refmem_pio_spi_adapter_t *adapter,
                                 uint8_t *frame,
                                 size_t frame_capacity,
                                 size_t *frame_size);

#endif
