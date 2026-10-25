#ifndef HOST_SHIM_HARDWARE_SPI_H
#define HOST_SHIM_HARDWARE_SPI_H

/* Host stand-in for the Pico SDK SPI header: the type aliases the board
 * headers need. Hardware access is replaced at link time. */

#include <stddef.h>
#include <stdint.h>

typedef struct spi_inst { uint32_t reserved; } spi_inst_t;

extern spi_inst_t *spi0;
extern spi_inst_t *spi1;

void spi_init(spi_inst_t *spi, uint32_t baudrate);
void spi_deinit(spi_inst_t *spi);
int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len);
int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx_data, uint8_t *dst, size_t len);
int spi_write_read_blocking(spi_inst_t *spi, const uint8_t *src, uint8_t *dst, size_t len);

#endif /* HOST_SHIM_HARDWARE_SPI_H */
