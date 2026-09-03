#include <assert.h>
#include <stdio.h>

#include "tdma_pio_spi_persona_fsm.h"

static void dispatch(tdma_pio_spi_persona_fsm_t *fsm,
                     tdma_pio_spi_persona_event_t event,
                     uint32_t persona)
{
    assert(tdma_pio_spi_persona_fsm_dispatch(fsm, event, persona));
}

int main(void)
{
    tdma_pio_spi_persona_fsm_t fsm;
    tdma_pio_spi_persona_fsm_init(&fsm, 0u);
    assert(fsm.state == TDMA_PIO_SPI_PERSONA_STATE_STOPPED);

    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_REQUEST, 11u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_VALID, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_QUIESCED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_UNLOADED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_LOADED, 0u);
    assert(fsm.state == TDMA_PIO_SPI_PERSONA_STATE_ACTIVE);
    assert(fsm.current_persona == 11u);

    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_REQUEST, 13u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_BUSY, 0u);
    assert(fsm.state == TDMA_PIO_SPI_PERSONA_STATE_ACTIVE);
    assert(fsm.current_persona == 11u);
    assert(fsm.last_error == TDMA_PIO_SPI_PERSONA_ERROR_RESOURCE_BUSY);
    const uint32_t busy_transition_seq = fsm.transition_seq;

    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_REQUEST, 13u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_VALID, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_QUIESCED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_UNLOADED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_LOADED, 0u);
    assert(fsm.state == TDMA_PIO_SPI_PERSONA_STATE_ACTIVE);
    assert(fsm.current_persona == 13u);
    assert(fsm.last_error == TDMA_PIO_SPI_PERSONA_ERROR_NONE);
    assert(fsm.transition_seq > busy_transition_seq);

    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_REQUEST, 17u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_VALID, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_QUIESCED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_UNLOADED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_LOAD_FAILED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_LOADED, 0u);
    assert(fsm.state == TDMA_PIO_SPI_PERSONA_STATE_ACTIVE);
    assert(fsm.current_persona == 13u);
    assert(fsm.target_persona == 13u);
    assert(fsm.last_error == TDMA_PIO_SPI_PERSONA_ERROR_LOAD_FAILED);

    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_REQUEST, 17u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_VALID, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_QUIESCED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_UNLOADED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_LOAD_FAILED, 0u);
    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_FAILED, 0u);
    assert(fsm.state == TDMA_PIO_SPI_PERSONA_STATE_FAULT);
    assert(fsm.current_persona == 0u);

    dispatch(&fsm, TDMA_PIO_SPI_PERSONA_EVENT_RESET, 0u);
    assert(fsm.state == TDMA_PIO_SPI_PERSONA_STATE_STOPPED);
    puts("TDMA persona FSM host tests passed");
    return 0;
}
