#ifndef DRV_SPI_H
#define DRV_SPI_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"

typedef struct {
    spi_inst_t *instance;
    uint32_t sck_pin;
    uint32_t mosi_pin;
    uint32_t miso_pin;
    uint32_t cs_pin;
    uint32_t baud_hz;
} drv_spi_config_t;

bool drv_spi_init(const drv_spi_config_t *config);

#endif
