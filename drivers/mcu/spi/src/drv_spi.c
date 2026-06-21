#include "drv_spi.h"

#include "hardware/gpio.h"

bool drv_spi_init(const drv_spi_config_t *config)
{
    if (config == NULL || config->instance == NULL) {
        return false;
    }

    gpio_put(config->cs_pin, 1);
    gpio_init(config->cs_pin);
    gpio_set_dir(config->cs_pin, GPIO_OUT);

    spi_init(config->instance, config->baud_hz);
    spi_set_format(config->instance, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(config->sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->mosi_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->miso_pin, GPIO_FUNC_SPI);
    gpio_pull_up(config->sck_pin);
    gpio_pull_up(config->mosi_pin);

    return true;
}
