#include "tdma_pio_spi_phys_internal.h"

#include "board_config.h"
#include "hardware/dma.h"
#include "pico/time.h"

void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return;
    }
    phys->snapshot.armed = phys->armed ? 1u : 0u;
    phys->snapshot.role = phys->role;
    phys->snapshot.baud_hz = phys->baud_hz;
    phys->snapshot.tx_sck_pin = phys->tx_sck_pin;
    phys->snapshot.tx_csn_pin = phys->tx_csn_pin;
    phys->snapshot.tx_pin = phys->tx_pin;
    phys->snapshot.rx_sck_pin = phys->rx_sck_pin;
    phys->snapshot.rx_csn_pin = phys->rx_csn_pin;
    phys->snapshot.rx_pin = phys->rx_pin;
    phys->snapshot.program_persona =
        (uint32_t)s_tdma_pio_spi_program_persona;
    phys->snapshot.flight_marker_offset_sample_count =
        phys->flight_marker_offset_sample_count;
    phys->snapshot.flight_sck_offset_sample_count =
        phys->flight_sck_offset_sample_count;
    phys->snapshot.flight_data_offset_sample_count =
        phys->flight_data_offset_sample_count;
    phys->snapshot.flight_marker_phase_delay_cycles =
        phys->flight_marker_phase_delay_cycles;
    phys->snapshot.flight_sck_phase_delay_cycles =
        phys->flight_sck_phase_delay_cycles;
    phys->snapshot.flight_data_phase_delay_cycles =
        phys->flight_data_phase_delay_cycles;
}

uint64_t tdma_pio_spi_phys_now_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

uint32_t tdma_pio_spi_phys_frame_tail_us(
    const tdma_pio_spi_phys_t *phys, size_t packet_size)
{
    const uint32_t baud_hz =
        (phys != NULL && phys->baud_hz != 0u) ? phys->baud_hz : 1000000u;
    const size_t packet_words = packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    /* Keep CS active while the joined TX FIFO and current OSR drain. */
    const size_t tail_words = packet_words < 10u ? packet_words : 10u;
    const uint64_t bit_us =
        ((uint64_t)tail_words * 8ull * 1000000ull + baud_hz - 1ull) /
        baud_hz;
    return (uint32_t)bit_us + 10u;
}

uint64_t tdma_pio_spi_phys_wire_time_ns(
    const tdma_pio_spi_phys_t *phys, size_t packet_size)
{
    const uint32_t baud_hz =
        (phys != NULL && phys->baud_hz != 0u) ? phys->baud_hz : 1000000u;
    const uint64_t bits =
        (uint64_t)(packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE) * 8ull;
    return (bits * 1000000000ull + baud_hz - 1ull) / baud_hz;
}

bool tdma_pio_spi_phys_ensure_rx_dma(void)
{
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        return true;
    }
    if (dma_channel_is_claimed(TDMA_PIO_SPI_RX_DMA_CHANNEL)) {
        return false;
    }
    dma_channel_claim(TDMA_PIO_SPI_RX_DMA_CHANNEL);
    s_tdma_pio_spi_rx_dma_channel = (int)TDMA_PIO_SPI_RX_DMA_CHANNEL;
    return true;
}

bool tdma_pio_spi_phys_ensure_tx_dma(void)
{
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        return true;
    }
    if (dma_channel_is_claimed(TDMA_PIO_SPI_TX_DMA_CHANNEL)) {
        return false;
    }
    dma_channel_claim(TDMA_PIO_SPI_TX_DMA_CHANNEL);
    s_tdma_pio_spi_tx_dma_channel = (int)TDMA_PIO_SPI_TX_DMA_CHANNEL;
    return true;
}
