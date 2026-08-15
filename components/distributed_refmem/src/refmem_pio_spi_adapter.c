#include "refmem_pio_spi_adapter.h"

#include <string.h>

static void refmem_pio_spi_adapter_set_error(refmem_pio_spi_adapter_t *adapter,
                                             uint32_t error)
{
    if (adapter == NULL) {
        return;
    }
    adapter->snapshot.last_error = error;
    if (error != REFMEM_TRANSPORT_ERROR_NONE) {
        adapter->snapshot.state = REFMEM_TRANSPORT_STATE_ERROR;
    }
}

bool refmem_pio_spi_adapter_init(refmem_pio_spi_adapter_t *adapter,
                                 uint16_t max_payload_size,
                                 uint16_t preferred_mtu,
                                 uint32_t latency_class_us)
{
    if (adapter == NULL ||
        max_payload_size == 0u ||
        max_payload_size > REFMEM_SYNC_FRAME_PAYLOAD_MAX ||
        preferred_mtu < REFMEM_SYNC_FRAME_HEADER_SIZE ||
        preferred_mtu > (uint16_t)(REFMEM_SYNC_FRAME_HEADER_SIZE + max_payload_size)) {
        return false;
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->caps.adapter_id = REFMEM_TRANSPORT_ADAPTER_ID_PIO_SPI;
    adapter->caps.capability_mask = REFMEM_TRANSPORT_CAP_FRAME_CRC |
                                    REFMEM_TRANSPORT_CAP_HALF_DUPLEX |
                                    REFMEM_TRANSPORT_CAP_RX_TIMESTAMP_OPTIONAL |
                                    REFMEM_TRANSPORT_CAP_COUNTERS;
    adapter->caps.max_payload_size = max_payload_size;
    adapter->caps.preferred_mtu = preferred_mtu;
    adapter->caps.latency_class_us = latency_class_us;
    adapter->caps.flags = 0u;

    adapter->snapshot.adapter_id = adapter->caps.adapter_id;
    adapter->snapshot.state = REFMEM_TRANSPORT_STATE_IDLE;
    adapter->snapshot.capability_mask = adapter->caps.capability_mask;
    adapter->snapshot.max_payload_size = adapter->caps.max_payload_size;
    adapter->snapshot.preferred_mtu = adapter->caps.preferred_mtu;
    adapter->snapshot.latency_class_us = adapter->caps.latency_class_us;
    adapter->snapshot.last_error = REFMEM_TRANSPORT_ERROR_NONE;
    adapter->initialized = true;
    return true;
}

void refmem_pio_spi_adapter_reset_counters(refmem_pio_spi_adapter_t *adapter)
{
    refmem_transport_caps_t caps;
    bool initialized;

    if (adapter == NULL) {
        return;
    }

    caps = adapter->caps;
    initialized = adapter->initialized;
    memset(&adapter->snapshot, 0, sizeof(adapter->snapshot));
    adapter->caps = caps;
    adapter->initialized = initialized;
    adapter->snapshot.adapter_id = caps.adapter_id;
    adapter->snapshot.state = initialized ? REFMEM_TRANSPORT_STATE_IDLE
                                          : REFMEM_TRANSPORT_STATE_UNINIT;
    adapter->snapshot.capability_mask = caps.capability_mask;
    adapter->snapshot.max_payload_size = caps.max_payload_size;
    adapter->snapshot.preferred_mtu = caps.preferred_mtu;
    adapter->snapshot.latency_class_us = caps.latency_class_us;
    adapter->rx_frame_size = 0u;
}

bool refmem_pio_spi_adapter_get_caps(const refmem_pio_spi_adapter_t *adapter,
                                     refmem_transport_caps_t *caps)
{
    if (adapter == NULL || caps == NULL || !adapter->initialized) {
        return false;
    }
    *caps = adapter->caps;
    return true;
}

bool refmem_pio_spi_adapter_get_snapshot(
    const refmem_pio_spi_adapter_t *adapter,
    refmem_pio_spi_adapter_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = adapter->snapshot;
    return true;
}

bool refmem_pio_spi_adapter_send(refmem_pio_spi_adapter_t *adapter,
                                 const uint8_t *frame,
                                 size_t frame_size)
{
    refmem_sync_frame_header_t header;
    refmem_sync_frame_result_t frame_result;
    const uint8_t *payload;
    uint16_t payload_size;

    if (adapter == NULL || frame == NULL || !adapter->initialized) {
        if (adapter != NULL) {
            adapter->snapshot.tx_reject_count++;
            refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_BAD_ARGUMENT);
        }
        return false;
    }

    frame_result = refmem_sync_frame_validate(frame,
                                              frame_size,
                                              &header,
                                              &payload,
                                              &payload_size);
    if (frame_result != REFMEM_SYNC_FRAME_OK) {
        adapter->snapshot.tx_reject_count++;
        adapter->snapshot.bad_frame_count++;
        refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_BAD_FRAME);
        return false;
    }
    if (header.payload_size > adapter->caps.max_payload_size) {
        adapter->snapshot.tx_reject_count++;
        adapter->snapshot.drop_count++;
        refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_PAYLOAD_TOO_LARGE);
        return false;
    }

    adapter->snapshot.tx_reject_count++;
    adapter->snapshot.last_tx_size = (uint32_t)frame_size;
    refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_TX_UNBOUND);
    return false;
}

bool refmem_pio_spi_adapter_inject_rx_frame(refmem_pio_spi_adapter_t *adapter,
                                            const uint8_t *frame,
                                            size_t frame_size,
                                            uint32_t timestamp)
{
    refmem_sync_frame_header_t header;
    refmem_sync_frame_result_t frame_result;
    const uint8_t *payload;
    uint16_t payload_size;

    if (adapter == NULL || frame == NULL || !adapter->initialized) {
        if (adapter != NULL) {
            adapter->snapshot.bad_frame_count++;
            refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_BAD_ARGUMENT);
        }
        return false;
    }

    if (adapter->rx_frame_size != 0u) {
        adapter->snapshot.drop_count++;
        refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_RX_BUSY);
        return false;
    }

    frame_result = refmem_sync_frame_validate(frame,
                                              frame_size,
                                              &header,
                                              &payload,
                                              &payload_size);
    if (frame_result != REFMEM_SYNC_FRAME_OK) {
        adapter->snapshot.bad_frame_count++;
        refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_BAD_FRAME);
        return false;
    }
    if (header.payload_size > adapter->caps.max_payload_size ||
        frame_size > sizeof(adapter->rx_frame)) {
        adapter->snapshot.drop_count++;
        refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_PAYLOAD_TOO_LARGE);
        return false;
    }

    memcpy(adapter->rx_frame, frame, frame_size);
    adapter->rx_frame_size = frame_size;
    adapter->snapshot.last_rx_size = (uint32_t)frame_size;
    adapter->snapshot.last_rx_timestamp = timestamp;
    adapter->snapshot.rx_pending = 1u;
    adapter->snapshot.last_error = REFMEM_TRANSPORT_ERROR_NONE;
    adapter->snapshot.state = REFMEM_TRANSPORT_STATE_IDLE;
    return true;
}

bool refmem_pio_spi_adapter_poll(refmem_pio_spi_adapter_t *adapter,
                                 uint8_t *frame,
                                 size_t frame_capacity,
                                 size_t *frame_size)
{
    if (adapter == NULL || !adapter->initialized ||
        frame == NULL || frame_size == NULL || frame_capacity == 0u) {
        if (adapter != NULL) {
            adapter->snapshot.rx_empty_count++;
            refmem_pio_spi_adapter_set_error(adapter, REFMEM_TRANSPORT_ERROR_BAD_ARGUMENT);
        }
        return false;
    }

    if (adapter->rx_frame_size != 0u) {
        if (frame_capacity < adapter->rx_frame_size) {
            adapter->snapshot.drop_count++;
            refmem_pio_spi_adapter_set_error(adapter,
                                             REFMEM_TRANSPORT_ERROR_PAYLOAD_TOO_LARGE);
            return false;
        }

        memcpy(frame, adapter->rx_frame, adapter->rx_frame_size);
        *frame_size = adapter->rx_frame_size;
        adapter->rx_frame_size = 0u;
        adapter->snapshot.rx_count++;
        adapter->snapshot.rx_pending = 0u;
        adapter->snapshot.last_error = REFMEM_TRANSPORT_ERROR_NONE;
        adapter->snapshot.state = REFMEM_TRANSPORT_STATE_IDLE;
        return true;
    }

    *frame_size = 0u;
    adapter->snapshot.rx_empty_count++;
    adapter->snapshot.last_error = REFMEM_TRANSPORT_ERROR_NO_RX_FRAME;
    adapter->snapshot.state = REFMEM_TRANSPORT_STATE_IDLE;
    return false;
}
