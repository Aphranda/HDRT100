#include "tdma_pio_spi_phys_internal.h"

#include "board_config.h"
#include "hardware/pio.h"

/*
 * Logical-to-physical routing for the maintenance and flight personas.
 *
 * The public physical object keeps tx_sm/rx_sm as stable logical names for
 * compatibility with maintenance callers.  Flight traffic uses the dedicated
 * TX/RX PIO blocks and maps control, data, evidence and capture endpoints by
 * role.  Keeping that mapping here makes persona-specific routing explicit
 * without adding work to the real-time FIFO path.
 */
bool tdma_pio_spi_phys_is_flight_persona(void)
{
    return s_tdma_pio_spi_program_persona ==
               TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
           s_tdma_pio_spi_program_persona ==
               TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER ||
           s_tdma_pio_spi_program_persona ==
               TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
}

PIO tdma_pio_spi_phys_tx_sm_pio(const tdma_pio_spi_phys_t *phys)
{
    return (tdma_pio_spi_phys_is_flight_persona() && phys != NULL &&
            phys->tx_sm_pio != NULL)
        ? phys->tx_sm_pio : BOARD_TDMA_SPI_PIO;
}

PIO tdma_pio_spi_phys_rx_sm_pio(const tdma_pio_spi_phys_t *phys)
{
    return (tdma_pio_spi_phys_is_flight_persona() && phys != NULL &&
            phys->rx_sm_pio != NULL)
        ? phys->rx_sm_pio : BOARD_TDMA_SPI_PIO;
}

PIO tdma_pio_spi_phys_evidence_pio(const tdma_pio_spi_phys_t *phys)
{
    return (tdma_pio_spi_phys_is_flight_persona() && phys != NULL &&
            phys->evidence_pio != NULL)
        ? phys->evidence_pio : BOARD_TDMA_SPI_PIO;
}

uint tdma_pio_spi_phys_latch_sm(const tdma_pio_spi_phys_t *phys)
{
    return (tdma_pio_spi_phys_is_flight_persona() && phys != NULL &&
            phys->flight_resources.tx_pio != NULL)
        ? (phys->role == TDMA_PIO_SPI_ROLE_MASTER
               ? phys->flight_resources.tx_data_in_forward_sm
               : phys->flight_resources.rx_evidence_in_sm)
        : BOARD_TDMA_SPI_CAPTURE_SM;
}

PIO tdma_pio_spi_phys_control_pio(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? tdma_pio_spi_phys_rx_sm_pio(phys)
        : tdma_pio_spi_phys_tx_sm_pio(phys);
}

uint tdma_pio_spi_phys_control_sm(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? phys->rx_sm : phys->tx_sm;
}

PIO tdma_pio_spi_phys_data_pio(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? tdma_pio_spi_phys_tx_sm_pio(phys)
        : tdma_pio_spi_phys_rx_sm_pio(phys);
}

uint tdma_pio_spi_phys_data_sm(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? phys->tx_sm : phys->rx_sm;
}

PIO tdma_pio_spi_phys_capture_pio(const tdma_pio_spi_phys_t *phys)
{
    /* Flight DATA SMs already auto-push each received byte. Reusing that FIFO
     * keeps DATA GPIO24 owned by one PIO block; a second capture SM would
     * steal the pin mux from the forwarding SM. */
    return tdma_pio_spi_phys_is_flight_persona() && phys != NULL
        ? tdma_pio_spi_phys_data_pio(phys)
        : (phys != NULL && phys->role == TDMA_PIO_SPI_ROLE_MASTER
               ? tdma_pio_spi_phys_tx_sm_pio(phys)
               : tdma_pio_spi_phys_rx_sm_pio(phys));
}

uint tdma_pio_spi_phys_capture_sm(const tdma_pio_spi_phys_t *phys)
{
    return tdma_pio_spi_phys_is_flight_persona() && phys != NULL &&
            phys->flight_resources.tx_pio != NULL
        ? tdma_pio_spi_phys_data_sm(phys)
        : (phys != NULL ? phys->rx_sm : BOARD_TDMA_SPI_SLAVE_SM);
}
