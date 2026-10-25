#ifndef TDMA_PIO_SPI_RING_ADAPTER_INTERNAL_H
#define TDMA_PIO_SPI_RING_ADAPTER_INTERNAL_H

#include "tdma_pio_spi_ring_adapter.h"

/* Helpers shared between the ring-adapter unit and its origin / RX-prepare
 * units. They were file static while the fragments were textually
 * included; they stay module internal. */
uint64_t tdma_pio_spi_ring_adapter_loop_ns(uint64_t ticks);
uint32_t tdma_pio_spi_ring_adapter_loop_tick_hz(void);
void tdma_pio_spi_ring_adapter_reset_loop_time( tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_resident_process_image( const tdma_pio_spi_ring_adapter_t *adapter);
void tdma_pio_spi_ring_adapter_set_error( tdma_pio_spi_ring_adapter_t *adapter, uint32_t error);
void tdma_pio_spi_ring_adapter_snapshot_write_begin( tdma_pio_spi_ring_adapter_t *adapter);
void tdma_pio_spi_ring_adapter_snapshot_write_end( tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_product_payload(const tdma_pio_spi_ring_adapter_t *adapter, size_t payload_size);
bool tdma_pio_spi_ring_adapter_process_rx_impl( tdma_pio_spi_ring_adapter_t *adapter, const uint8_t *packet, size_t packet_size, uint64_t rx_timestamp_ns, const tdma_origin_observation_t *origin_observation, const tdma_rx_prepare_t *prepared);
/* Defined here, called by the origin unit's service path. */
bool tdma_pio_spi_ring_adapter_rx_once(tdma_pio_spi_ring_adapter_t *adapter);

#endif /* TDMA_PIO_SPI_RING_ADAPTER_INTERNAL_H */
