#ifndef TDMA_PIO_SPI_RING_ADAPTER_API_H
#define TDMA_PIO_SPI_RING_ADAPTER_API_H

#include "../src/tdma_pio_spi_ring_adapter_internal.h"

/* N5.0.4l: cross-unit helpers of the ring adapter split. */
extern const tdma_ring_adapter_ops_t s_tdma_pio_spi_ring_adapter_ops;
void tdma_pio_spi_ring_adapter_note_origin_return( tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_burst_exhausted( const tdma_pio_spi_ring_adapter_t *adapter);
uint32_t tdma_pio_spi_ring_get_u32(const uint8_t *src);
void tdma_pio_spi_ring_adapter_bind_reference_local_tx_edge( tdma_pio_spi_ring_adapter_t *adapter, uint32_t sequence, uint32_t identity_crc32, uint64_t timestamp_ns);
bool tdma_pio_spi_ring_adapter_build_dpll_observation( tdma_pio_spi_ring_adapter_t *adapter, uint8_t *payload, size_t payload_size);
bool tdma_pio_spi_ring_adapter_correlate_dpll_observation( tdma_pio_spi_ring_adapter_t *adapter, const tdma_transport_frame_view_t *view);
void tdma_pio_spi_ring_adapter_sample_local_tx_edge( tdma_pio_spi_ring_adapter_t *adapter, bool paired_with_rx);
void tdma_pio_spi_ring_adapter_reset_bad_packet_diagnostic( tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_queue_push( tdma_pio_spi_ring_adapter_t *adapter, const uint8_t *packet, size_t packet_size, uint64_t timestamp_ns);
bool tdma_pio_spi_ring_adapter_queue_pop( tdma_pio_spi_ring_adapter_t *adapter, uint8_t *packet, size_t packet_capacity, size_t *packet_size, uint64_t *timestamp_ns);
uint32_t tdma_pio_spi_ring_adapter_last_error( const void *context);
uint32_t tdma_pio_spi_ring_adapter_expected_owner_mask( const tdma_pio_spi_ring_adapter_t *adapter, const tdma_ring_runtime_config_t *config);
bool tdma_pio_spi_ring_adapter_resident_hop_position( const tdma_pio_spi_ring_adapter_t *adapter, uint32_t *ingress_hop, uint32_t *egress_hop);
bool tdma_pio_spi_ring_adapter_start( void *context, const tdma_ring_runtime_config_t *config);
bool tdma_pio_spi_ring_adapter_stop(void *context);
void tdma_pio_spi_ring_adapter_commit_resident_completion( tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_launch_reference( tdma_pio_spi_ring_adapter_t *adapter, const uint8_t *packet, size_t packet_size, const tdma_transport_frame_view_t *view, bool clock_evidence, bool bootstrap_retry, bool *launched);
bool tdma_pio_spi_ring_adapter_tx_resident_cycle_from( tdma_pio_spi_ring_adapter_t *adapter, const uint8_t *source_packet, size_t source_packet_size, bool data_rx_timed_out, bool *launched);
bool tdma_pio_spi_ring_adapter_tx_next_resident_cycle( tdma_pio_spi_ring_adapter_t *adapter, bool *launched);
bool tdma_pio_spi_ring_adapter_tx_stale_resident_cycle( tdma_pio_spi_ring_adapter_t *adapter, bool *launched);
bool tdma_pio_spi_ring_adapter_tx_bootstrap_retry( tdma_pio_spi_ring_adapter_t *adapter, bool *launched);
bool tdma_pio_spi_ring_adapter_tx_beacon( tdma_pio_spi_ring_adapter_t *adapter, bool *launched);
bool tdma_pio_spi_ring_adapter_tx_forward( tdma_pio_spi_ring_adapter_t *adapter);
__attribute__((noinline)) bool tdma_pio_spi_ring_adapter_rx_capture( tdma_pio_spi_ring_adapter_t *adapter, uint8_t *packet, size_t packet_capacity);
__attribute__((noinline)) bool tdma_pio_spi_ring_adapter_rx_legacy( tdma_pio_spi_ring_adapter_t *adapter);
__attribute__((noinline)) bool tdma_pio_spi_ring_adapter_prepare_overlay_legacy( tdma_pio_spi_ring_adapter_t *adapter);

#endif /* TDMA_PIO_SPI_RING_ADAPTER_API_H */
