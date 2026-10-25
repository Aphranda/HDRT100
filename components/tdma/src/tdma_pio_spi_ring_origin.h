#ifndef TDMA_PIO_SPI_RING_ORIGIN_H
#define TDMA_PIO_SPI_RING_ORIGIN_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_pio_spi_ring_adapter.h"

bool tdma_pio_spi_ring_adapter_set_phys_origin(tdma_pio_spi_ring_adapter_t *adapter, const tdma_pio_spi_ring_origin_ops_t *ops);
bool tdma_pio_spi_ring_adapter_start_origin(tdma_pio_spi_ring_adapter_t *adapter, uint32_t rearm_budget_ticks, uint32_t abort_poll_count);
void tdma_pio_spi_ring_adapter_schedule_evidence(tdma_origin_schedule_evidence_t *out);
bool tdma_pio_spi_ring_adapter_start_origin_impl(tdma_pio_spi_ring_adapter_t *adapter, uint32_t rearm_budget_ticks, uint32_t abort_poll_count);
void tdma_pio_spi_ring_origin_accept(tdma_pio_spi_ring_adapter_t *adapter, const tdma_origin_observation_t *observation);
void tdma_pio_spi_ring_origin_correlate_feedback( tdma_pio_spi_ring_adapter_t *adapter, const tdma_transport_frame_view_t *view, uint32_t returned_generation);
void tdma_pio_spi_ring_origin_invalidate_time(tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_origin_pair_valid(tdma_pio_spi_ring_adapter_t *adapter, const tdma_transport_frame_view_t *view, const tdma_origin_observation_t *observation, const tdma_rx_prepare_t *prepared);
bool tdma_pio_spi_ring_origin_prepare_service(tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_origin_seed_valid( const tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_origin_service(tdma_pio_spi_ring_adapter_t *adapter, bool *rx_ok);

#endif /* TDMA_PIO_SPI_RING_ORIGIN_H */
