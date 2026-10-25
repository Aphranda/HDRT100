#ifndef TDMA_PIO_SPI_RING_RX_PREPARE_H
#define TDMA_PIO_SPI_RING_RX_PREPARE_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_pio_spi_ring_adapter.h"

bool tdma_pio_spi_ring_rx_accept(tdma_pio_spi_ring_adapter_t *adapter, tdma_rx_prepare_t *job);
bool tdma_pio_spi_ring_rx_request(tdma_pio_spi_ring_adapter_t *adapter, tdma_rx_prepare_t *job, size_t packet_size, uint64_t timestamp, const tdma_origin_observation_t *observation);

#endif /* TDMA_PIO_SPI_RING_RX_PREPARE_H */
