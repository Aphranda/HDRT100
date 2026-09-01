#ifndef TDMA_PIO_SPI_PERSONA_FSM_H
#define TDMA_PIO_SPI_PERSONA_FSM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TDMA_PIO_SPI_PERSONA_STATE_STOPPED = 0u,
    TDMA_PIO_SPI_PERSONA_STATE_VALIDATING = 1u,
    TDMA_PIO_SPI_PERSONA_STATE_QUIESCING = 2u,
    TDMA_PIO_SPI_PERSONA_STATE_UNLOADING = 3u,
    TDMA_PIO_SPI_PERSONA_STATE_LOADING = 4u,
    TDMA_PIO_SPI_PERSONA_STATE_ACTIVE = 5u,
    TDMA_PIO_SPI_PERSONA_STATE_ROLLING_BACK = 6u,
    TDMA_PIO_SPI_PERSONA_STATE_FAULT = 7u,
} tdma_pio_spi_persona_state_t;

typedef enum {
    TDMA_PIO_SPI_PERSONA_EVENT_REQUEST = 0u,
    TDMA_PIO_SPI_PERSONA_EVENT_VALID = 1u,
    TDMA_PIO_SPI_PERSONA_EVENT_INVALID = 2u,
    TDMA_PIO_SPI_PERSONA_EVENT_BUSY = 3u,
    TDMA_PIO_SPI_PERSONA_EVENT_RETAIN = 4u,
    TDMA_PIO_SPI_PERSONA_EVENT_QUIESCED = 5u,
    TDMA_PIO_SPI_PERSONA_EVENT_UNLOADED = 6u,
    TDMA_PIO_SPI_PERSONA_EVENT_LOADED = 7u,
    TDMA_PIO_SPI_PERSONA_EVENT_LOAD_FAILED = 8u,
    TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_LOADED = 9u,
    TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_FAILED = 10u,
    TDMA_PIO_SPI_PERSONA_EVENT_RESET = 11u,
} tdma_pio_spi_persona_event_t;

typedef enum {
    TDMA_PIO_SPI_PERSONA_ERROR_NONE = 0u,
    TDMA_PIO_SPI_PERSONA_ERROR_INVALID_REQUEST = 1u,
    TDMA_PIO_SPI_PERSONA_ERROR_RESOURCE_BUSY = 2u,
    TDMA_PIO_SPI_PERSONA_ERROR_LOAD_FAILED = 3u,
    TDMA_PIO_SPI_PERSONA_ERROR_ROLLBACK_FAILED = 4u,
    TDMA_PIO_SPI_PERSONA_ERROR_INVALID_TRANSITION = 5u,
} tdma_pio_spi_persona_error_t;

typedef struct {
    uint32_t state;
    uint32_t current_persona;
    uint32_t target_persona;
    uint32_t previous_persona;
    uint32_t transition_seq;
    uint32_t last_error;
} tdma_pio_spi_persona_fsm_t;

void tdma_pio_spi_persona_fsm_init(tdma_pio_spi_persona_fsm_t *fsm,
                                   uint32_t current_persona);
bool tdma_pio_spi_persona_fsm_dispatch(tdma_pio_spi_persona_fsm_t *fsm,
                                      tdma_pio_spi_persona_event_t event,
                                      uint32_t requested_persona);

#endif
