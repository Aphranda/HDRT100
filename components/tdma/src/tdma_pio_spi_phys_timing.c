#include "tdma_pio_spi_phys_timing.h"

#include "hardware/pio.h"
#include "pico/time.h"

uint64_t tdma_pio_spi_phys_now_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

uint32_t tdma_pio_spi_phys_frame_tail_us(uint32_t baud_hz,
                                         size_t packet_size,
                                         size_t packet_header_size)
{
    if (baud_hz == 0u) {
        baud_hz = 1000000u;
    }
    const size_t packet_words = packet_size + packet_header_size;
    /* The joined TX FIFO and current OSR can still be on the wire after the
     * last CPU-side put. Keep the frame-sync line active for that tail. */
    const size_t tail_words = packet_words < 10u ? packet_words : 10u;
    const uint64_t bit_us =
        ((uint64_t)tail_words * 8ull * 1000000ull + baud_hz - 1ull) /
        baud_hz;
    return (uint32_t)bit_us + 10u;
}

uint64_t tdma_pio_spi_phys_wire_time_ns(uint32_t baud_hz,
                                        size_t packet_size,
                                        size_t packet_header_size)
{
    if (baud_hz == 0u) {
        baud_hz = 1000000u;
    }
    const uint64_t bits =
        (uint64_t)(packet_size + packet_header_size) * 8ull;
    return (bits * 1000000000ull + baud_hz - 1ull) / baud_hz;
}

uint32_t tdma_pio_spi_phys_txstall_mask(uint32_t sm)
{
    return 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
}
