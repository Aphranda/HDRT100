#include "tdma_pio_spi_phys.h"

#include <string.h>

#include "board_config.h"
#include "hardware/gpio.h"
#include "tdma_pio_spi_phys_internal.h"

void tdma_pio_spi_phys_data_train_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->data_train_guard, 1u, __ATOMIC_RELEASE);
}

void tdma_pio_spi_phys_data_train_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->data_train_guard, 1u, __ATOMIC_RELEASE);
}

void tdma_pio_spi_phys_data_train_publish_error(
    tdma_pio_spi_phys_t *phys,
    uint32_t epoch,
    tdma_pio_spi_data_train_reject_t reason)
{
    tdma_pio_spi_phys_data_train_write_begin(phys);
    memset(&phys->data_train, 0, sizeof(phys->data_train));
    phys->data_train.version = TDMA_PIO_SPI_DATA_TRAIN_SNAPSHOT_VERSION;
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
    phys->data_train.flags = TDMA_PIO_SPI_DATA_TRAIN_FLAG_DIAGNOSTIC_ONLY;
    phys->data_train.reject_reason = (uint32_t)reason;
    phys->data_train.epoch = epoch;
    tdma_pio_spi_phys_data_train_write_end(phys);
}

void tdma_pio_spi_phys_data_train_set_drivers(uint32_t role)
{
    /* DATA0_OUT returns upstream; only the role-specific transceiver drives. */
    gpio_put(BOARD_UP_BISS_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER);
    gpio_put(BOARD_DN_BISS_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE);
    gpio_put(BOARD_TRIG_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR);
}
