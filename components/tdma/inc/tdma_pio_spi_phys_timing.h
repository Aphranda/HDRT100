#ifndef TDMA_PIO_SPI_PHYS_TIMING_H
#define TDMA_PIO_SPI_PHYS_TIMING_H

#include <stddef.h>
#include <stdint.h>

/* Pure timing helpers shared by the resident PHY and its diagnostic paths.
 * They deliberately do not access tdma_pio_spi_phys_t so timing arithmetic
 * remains independent of PIO/FIFO ownership. */
uint64_t tdma_pio_spi_phys_now_us(void);

/* One latch countdown step is two system-clock cycles. Round to nearest ns
 * exactly as before: 2e9 + UINT32_MAX/2 < UINT32_MAX, so the numerator and
 * divisor fit the hardware's 32-bit division at every input frequency. */
static inline uint32_t tdma_pio_spi_phys_latch_resolution_ns(uint32_t clock_hz)
{
    return clock_hz == 0u ? 0u : (2000000000u + clock_hz / 2u) / clock_hz;
}

uint32_t tdma_pio_spi_phys_frame_tail_us(uint32_t baud_hz,
                                         size_t packet_size,
                                         size_t packet_header_size);

uint64_t tdma_pio_spi_phys_wire_time_ns(uint32_t baud_hz,
                                        size_t packet_size,
                                        size_t packet_header_size);

/* A launched frame is only declared stuck after its nominal wire time plus
 * two Core1 service periods (the poll interval), with a 1 ms floor. This
 * keeps long low-rate frames from false-timeout while keeping recovery
 * bounded. */
static inline uint64_t tdma_pio_spi_phys_tx_completion_margin_ns(
    uint32_t cycle_period_ns)
{
    const uint64_t twice = (uint64_t)cycle_period_ns * 2ull;
    return twice > 1000000ull ? twice : 1000000ull;
}

uint32_t tdma_pio_spi_phys_txstall_mask(uint32_t sm);

#endif
