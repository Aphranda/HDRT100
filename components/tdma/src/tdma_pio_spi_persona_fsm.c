#include "tdma_pio_spi_persona_fsm.h"

#include <string.h>

static void tdma_pio_spi_persona_fsm_transition(
    tdma_pio_spi_persona_fsm_t *fsm,
    tdma_pio_spi_persona_state_t state,
    tdma_pio_spi_persona_error_t error)
{
    fsm->state = (uint32_t)state;
    fsm->last_error = (uint32_t)error;
    fsm->transition_seq++;
}

void tdma_pio_spi_persona_fsm_init(tdma_pio_spi_persona_fsm_t *fsm,
                                   uint32_t current_persona)
{
    if (fsm == NULL) {
        return;
    }
    memset(fsm, 0, sizeof(*fsm));
    fsm->current_persona = current_persona;
    fsm->state = current_persona == 0u
        ? TDMA_PIO_SPI_PERSONA_STATE_STOPPED
        : TDMA_PIO_SPI_PERSONA_STATE_ACTIVE;
}

bool tdma_pio_spi_persona_fsm_dispatch(tdma_pio_spi_persona_fsm_t *fsm,
                                      tdma_pio_spi_persona_event_t event,
                                      uint32_t requested_persona)
{
    if (fsm == NULL) {
        return false;
    }

    switch ((tdma_pio_spi_persona_state_t)fsm->state) {
    case TDMA_PIO_SPI_PERSONA_STATE_STOPPED:
    case TDMA_PIO_SPI_PERSONA_STATE_ACTIVE:
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_RESET) {
            tdma_pio_spi_persona_fsm_init(fsm, 0u);
            return true;
        }
        if (event != TDMA_PIO_SPI_PERSONA_EVENT_REQUEST) {
            break;
        }
        fsm->previous_persona = fsm->current_persona;
        fsm->target_persona = requested_persona;
        tdma_pio_spi_persona_fsm_transition(
            fsm, TDMA_PIO_SPI_PERSONA_STATE_VALIDATING,
            TDMA_PIO_SPI_PERSONA_ERROR_NONE);
        return true;

    case TDMA_PIO_SPI_PERSONA_STATE_VALIDATING:
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_INVALID) {
            fsm->target_persona = fsm->current_persona;
            tdma_pio_spi_persona_fsm_transition(
                fsm,
                fsm->current_persona == 0u
                    ? TDMA_PIO_SPI_PERSONA_STATE_STOPPED
                    : TDMA_PIO_SPI_PERSONA_STATE_ACTIVE,
                TDMA_PIO_SPI_PERSONA_ERROR_INVALID_REQUEST);
            return true;
        }
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_BUSY) {
            fsm->target_persona = fsm->current_persona;
            tdma_pio_spi_persona_fsm_transition(
                fsm,
                fsm->current_persona == 0u
                    ? TDMA_PIO_SPI_PERSONA_STATE_STOPPED
                    : TDMA_PIO_SPI_PERSONA_STATE_ACTIVE,
                TDMA_PIO_SPI_PERSONA_ERROR_RESOURCE_BUSY);
            return true;
        }
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_RETAIN &&
            fsm->target_persona == fsm->current_persona) {
            tdma_pio_spi_persona_fsm_transition(
                fsm, TDMA_PIO_SPI_PERSONA_STATE_ACTIVE,
                TDMA_PIO_SPI_PERSONA_ERROR_NONE);
            return true;
        }
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_VALID) {
            tdma_pio_spi_persona_fsm_transition(
                fsm, TDMA_PIO_SPI_PERSONA_STATE_QUIESCING,
                TDMA_PIO_SPI_PERSONA_ERROR_NONE);
            return true;
        }
        break;

    case TDMA_PIO_SPI_PERSONA_STATE_QUIESCING:
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_QUIESCED) {
            tdma_pio_spi_persona_fsm_transition(
                fsm, TDMA_PIO_SPI_PERSONA_STATE_UNLOADING,
                TDMA_PIO_SPI_PERSONA_ERROR_NONE);
            return true;
        }
        break;

    case TDMA_PIO_SPI_PERSONA_STATE_UNLOADING:
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_UNLOADED) {
            tdma_pio_spi_persona_fsm_transition(
                fsm, TDMA_PIO_SPI_PERSONA_STATE_LOADING,
                TDMA_PIO_SPI_PERSONA_ERROR_NONE);
            return true;
        }
        break;

    case TDMA_PIO_SPI_PERSONA_STATE_LOADING:
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_LOADED) {
            fsm->current_persona = fsm->target_persona;
            tdma_pio_spi_persona_fsm_transition(
                fsm, TDMA_PIO_SPI_PERSONA_STATE_ACTIVE,
                TDMA_PIO_SPI_PERSONA_ERROR_NONE);
            return true;
        }
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_LOAD_FAILED) {
            tdma_pio_spi_persona_fsm_transition(
                fsm, TDMA_PIO_SPI_PERSONA_STATE_ROLLING_BACK,
                TDMA_PIO_SPI_PERSONA_ERROR_LOAD_FAILED);
            return true;
        }
        break;

    case TDMA_PIO_SPI_PERSONA_STATE_ROLLING_BACK:
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_LOADED) {
            fsm->current_persona = fsm->previous_persona;
            fsm->target_persona = fsm->previous_persona;
            tdma_pio_spi_persona_fsm_transition(
                fsm,
                fsm->current_persona == 0u
                    ? TDMA_PIO_SPI_PERSONA_STATE_STOPPED
                    : TDMA_PIO_SPI_PERSONA_STATE_ACTIVE,
                TDMA_PIO_SPI_PERSONA_ERROR_LOAD_FAILED);
            return true;
        }
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_FAILED) {
            fsm->current_persona = 0u;
            fsm->target_persona = 0u;
            tdma_pio_spi_persona_fsm_transition(
                fsm, TDMA_PIO_SPI_PERSONA_STATE_FAULT,
                TDMA_PIO_SPI_PERSONA_ERROR_ROLLBACK_FAILED);
            return true;
        }
        break;

    case TDMA_PIO_SPI_PERSONA_STATE_FAULT:
        if (event == TDMA_PIO_SPI_PERSONA_EVENT_RESET) {
            tdma_pio_spi_persona_fsm_init(fsm, 0u);
            return true;
        }
        break;

    default:
        break;
    }

    tdma_pio_spi_persona_fsm_transition(
        fsm, TDMA_PIO_SPI_PERSONA_STATE_FAULT,
        TDMA_PIO_SPI_PERSONA_ERROR_INVALID_TRANSITION);
    return false;
}
