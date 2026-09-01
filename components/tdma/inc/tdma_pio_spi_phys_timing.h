#ifndef TDMA_PIO_SPI_PHYS_TIMING_H
#define TDMA_PIO_SPI_PHYS_TIMING_H

#include <stddef.h>
#include <stdint.h>

/* Pure timing helpers shared by the resident PHY and its diagnostic paths.
 * They deliberately do not access tdma_pio_spi_phys_t so timing arithmetic
 * remains independent of PIO/FIFO ownership. */
uint64_t tdma_pio_spi_phys_now_us(void);

uint32_t tdma_pio_spi_phys_frame_tail_us(uint32_t baud_hz,
                                         size_t packet_size,
                                         size_t packet_header_size);

uint64_t tdma_pio_spi_phys_wire_time_ns(uint32_t baud_hz,
                                        size_t packet_size,
                                        size_t packet_header_size);

uint32_t tdma_pio_spi_phys_txstall_mask(uint32_t sm);

#endif
