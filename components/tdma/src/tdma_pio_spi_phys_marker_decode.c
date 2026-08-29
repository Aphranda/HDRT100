#include "tdma_pio_spi_phys.h"

#include "tdma_pio_spi_phys_internal.h"

static uint32_t tdma_pio_spi_phys_marker_sample(uint32_t word,
                                                uint32_t sample_index)
{
    return (word >> ((sample_index & 15u) * 2u)) & 0x3u;
}

void tdma_pio_spi_phys_marker_decode_edges(tdma_pio_spi_phys_t *phys)
{
    uint64_t first_tx = 0u;
    uint64_t first_rx = 0u;
    uint32_t previous = 0x3u;
    const uint32_t samples = phys->marker.capture_sample_count;
    uint32_t *capture = tdma_pio_spi_phys_marker_rx_buffer();
    for (uint32_t index = 0u; index < samples; index++) {
        const uint32_t sample =
            tdma_pio_spi_phys_marker_sample(capture[index / 16u], index);
        const uint32_t falling = previous & ~sample;
        if (first_tx == 0u && (falling & 0x1u) != 0u) {
            first_tx = (uint64_t)index + 1ull;
        }
        if (first_rx == 0u && (falling & 0x2u) != 0u) {
            first_rx = (uint64_t)index + 1ull;
        }
        previous = sample;
        if (first_tx != 0u && first_rx != 0u) break;
    }

    if (phys->marker.role == TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER) {
        /* WAIT 0 latches the input edge; the following instruction is the
         * first DMA sample, so the local accepted edge is tick one. */
        phys->marker.marker_capture_tick = 1ull;
        phys->marker.marker_forward_tick =
            first_tx == 0u ? 0ull : first_tx + 1ull;
        phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_INPUT_EDGE;
        if (first_tx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_OUTPUT_EDGE;
        }
    } else {
        /* The origin capture preamble consumes two local ticks before the
         * returned upstream edge is exposed as the ring measurement. */
        phys->marker.marker_forward_tick = first_tx != 0u ? 1ull : 0ull;
        phys->marker.marker_return_tick =
            first_rx == 0u ? 0ull : first_rx + 2ull;
        phys->marker.marker_capture_tick = phys->marker.marker_return_tick;
        if (first_tx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_OUTPUT_EDGE;
        }
        if (first_rx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_INPUT_EDGE |
                                  TDMA_PIO_SPI_MARKER_FLAG_RETURN_EDGE;
        }
    }
}
