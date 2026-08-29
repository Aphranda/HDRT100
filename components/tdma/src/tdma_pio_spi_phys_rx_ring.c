#include "tdma_pio_spi_phys_internal.h"

#include "hardware/dma.h"
#include "tdma_rx_sequence.h"

extern uint32_t s_tdma_pio_spi_rx_ring[TDMA_PIO_SPI_RX_RING_WORDS];
extern tdma_rx_sequence_tracker_t s_tdma_pio_spi_rx_sequence;

uint32_t tdma_pio_spi_phys_rx_write_index(void)
{
    const uintptr_t ring_base = (uintptr_t)s_tdma_pio_spi_rx_ring;
    const uintptr_t write_addr =
        (uintptr_t)dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].write_addr;
    return (uint32_t)(((write_addr - ring_base) &
                       ((TDMA_PIO_SPI_RX_RING_WORDS * sizeof(uint32_t)) -
                        1u)) /
                      sizeof(uint32_t));
}

uint64_t tdma_pio_spi_phys_rx_produced_words(
    const tdma_pio_spi_phys_t *phys)
{
    const uint32_t write_index = tdma_pio_spi_phys_rx_write_index();
    const bool fixed_frames = phys != NULL && phys->process_image_enabled &&
        phys->flight_physical_byte_count != 0u;
    const uint32_t complete_frames = !fixed_frames
        ? 0u
        : (phys->role == TDMA_PIO_SPI_ROLE_MASTER
               ? phys->snapshot.tx_count
               : phys->snapshot.overlay_frame_boundary_count);
    const uint32_t frame_words = fixed_frames
        ? phys->flight_physical_byte_count : 0u;
    uint64_t produced = s_tdma_pio_spi_rx_sequence.produced_words;
    if (!tdma_rx_sequence_observe(&s_tdma_pio_spi_rx_sequence,
                                  write_index,
                                  complete_frames,
                                  frame_words,
                                  &produced)) {
        return s_tdma_pio_spi_rx_sequence.produced_words;
    }
    return produced;
}

uint32_t tdma_pio_spi_phys_rx_ring_word(uint64_t produced)
{
    return s_tdma_pio_spi_rx_ring[produced &
                                  (TDMA_PIO_SPI_RX_RING_WORDS - 1u)];
}

uint8_t tdma_pio_spi_phys_rx_ring_byte(uint64_t produced)
{
    return (uint8_t)(tdma_pio_spi_phys_rx_ring_word(produced) & 0xFFu);
}

uint8_t tdma_pio_spi_phys_rx_ring_aligned_byte(uint64_t produced,
                                                uint32_t bit_shift)
{
    const uint8_t first = tdma_pio_spi_phys_rx_ring_byte(produced);
    if (bit_shift == 0u) {
        return first;
    }
    const uint8_t second = tdma_pio_spi_phys_rx_ring_byte(produced + 1u);
    return (uint8_t)(((uint32_t)first << bit_shift) |
                     ((uint32_t)second >> (8u - bit_shift)));
}
