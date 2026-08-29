#include "tdma_pio_spi_phys.h"

#include "hardware/pio.h"
#include "tdma_pio_spi_phys_internal.h"
#include "vdc_timestamp_clock.h"

/* CS-style local launch: high idle followed by one low edge.  The trigger
 * word is kept with the SCK persona so the SCK module owns its launch token. */
static const uint32_t s_tdma_pio_spi_sck_train_inject_word = 0u;

uint32_t tdma_pio_spi_phys_sck_train_inject_word(void)
{
    return s_tdma_pio_spi_sck_train_inject_word;
}

bool tdma_pio_spi_phys_sck_train_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_data_train_request_t *request)
{
    return request != NULL &&
           (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
            request->role ==
                TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION) &&
           tdma_pio_spi_phys_data_train_arm(phys, request);
}

bool tdma_pio_spi_phys_sck_train_inject(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        phys->data_train.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
        phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_ARMED) {
        return false;
    }
    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_RUNNING;
    phys->data_train.marker_capture_tick = 1ull;
    phys->data_train.data_capture_tick =
        1ull + phys->data_train.marker_to_data_delay_cycles +
        phys->data_train.phase_delay_cycles -
        phys->data_train.source_phase_delay_cycles;
    phys->data_train_deadline_ns = vdc_timestamp_clock_now_ns() +
                                   TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS;
    tdma_pio_spi_phys_data_train_write_end(phys);
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm,
               s_tdma_pio_spi_sck_train_inject_word);
    return true;
}

void tdma_pio_spi_phys_sck_train_stop(tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_data_train_stop(phys);
}

void tdma_pio_spi_phys_sck_train_service(tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_data_train_service(phys);
}
