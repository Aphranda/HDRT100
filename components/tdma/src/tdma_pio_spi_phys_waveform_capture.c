#include "tdma_pio_spi_phys_internal.h"

#include <stdint.h>

#include "hardware/clocks.h"
#include "tdma_pio_spi.pio.h"
#include "vdc_timestamp_clock.h"

/* The resident flight latch is also the bounded SCK waveform capture
 * endpoint. Capture temporarily patches its four instructions, then restores
 * the resident latch persona before returning to the flight path. */
bool tdma_pio_spi_phys_clock_latch_rearm(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        (s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)) {
        return false;
    }
    const uint32_t clk_hz = clock_get_hz(clk_sys);
    if (clk_hz == 0u) {
        return false;
    }
    const uint32_t resolution_ns = (uint32_t)(
        (2000000000ull + clk_hz / 2u) / clk_hz);
    if (resolution_ns == 0u) {
        return false;
    }

    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint sm = tdma_pio_spi_phys_latch_sm(phys);
    pio_sm_set_enabled(evidence_pio, sm, false);
    pio_sm_clear_fifos(evidence_pio, sm);
    pio_sm_restart(evidence_pio, sm);
    pio_sm_put_blocking(evidence_pio, sm, UINT32_MAX);
    pio_sm_exec(evidence_pio, sm, pio_encode_pull(false, true));
    pio_sm_exec(evidence_pio, sm, pio_encode_mov(pio_x, pio_osr));
    pio_sm_exec(evidence_pio,
                sm,
                pio_encode_jmp(s_tdma_pio_spi_flight_clock_latch_offset));
    phys->flight_clock_latch_epoch_ns = vdc_timestamp_clock_now_ns();
    phys->flight_clock_latch_resolution_ns = resolution_ns;
    phys->snapshot.clock_latch_resolution_ns = resolution_ns;
    phys->flight_clock_latch_armed = true;
    pio_sm_set_enabled(evidence_pio, sm, true);
    return true;
}

bool tdma_pio_spi_phys_clock_latch_read_and_rearm(
    tdma_pio_spi_phys_t *phys,
    uint64_t *timestamp_ns)
{
    /* The latch is the common local-RX edge timestamp for reference and
     * follower personas; extraction must not introduce software jitter. */
    if (timestamp_ns != NULL) {
        *timestamp_ns = 0ull;
    }
    if (phys == NULL || timestamp_ns == NULL ||
        !phys->flight_clock_latch_armed ||
        pio_sm_is_rx_fifo_empty(tdma_pio_spi_phys_evidence_pio(phys),
                                tdma_pio_spi_phys_latch_sm(phys))) {
        if (phys != NULL) {
            phys->snapshot.clock_latch_miss_count++;
        }
        return false;
    }

    const uint32_t remaining = pio_sm_get(
        tdma_pio_spi_phys_evidence_pio(phys),
        tdma_pio_spi_phys_latch_sm(phys));
    const uint64_t elapsed_count = (uint64_t)UINT32_MAX - remaining;
    const uint64_t elapsed_ns = elapsed_count *
        (uint64_t)phys->flight_clock_latch_resolution_ns;
    if (UINT64_MAX - phys->flight_clock_latch_epoch_ns < elapsed_ns) {
        phys->snapshot.clock_latch_miss_count++;
        (void)tdma_pio_spi_phys_clock_latch_rearm(phys);
        return false;
    }
    *timestamp_ns = phys->flight_clock_latch_epoch_ns + elapsed_ns;
    phys->snapshot.clock_latch_count++;
    return tdma_pio_spi_phys_clock_latch_rearm(phys);
}

bool tdma_pio_spi_phys_restore_clock_latch(
    tdma_pio_spi_phys_t *phys,
    bool rearm)
{
    if (phys == NULL) return false;
    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint sm = tdma_pio_spi_phys_latch_sm(phys);
    pio_sm_set_enabled(evidence_pio, sm, false);
    pio_sm_clear_fifos(evidence_pio, sm);
    pio_sm_restart(evidence_pio, sm);
    for (uint32_t index = 0u; index < 4u; index++) {
        evidence_pio->instr_mem[
            s_tdma_pio_spi_flight_clock_latch_offset + index] =
                phys->flight_sck_waveform_saved_instructions[index];
    }
    tdma_pio_spi_flight_clock_latch_program_init(
        evidence_pio, sm,
        s_tdma_pio_spi_flight_clock_latch_offset,
        phys->role == TDMA_PIO_SPI_ROLE_MASTER
            ? phys->tx_csn_pin
            : phys->rx_csn_pin);
    phys->flight_sck_waveform_capture_deadline_us = 0ull;
    phys->flight_sck_waveform_capture_state =
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE;
    phys->flight_clock_latch_armed = false;
    return !rearm || tdma_pio_spi_phys_clock_latch_rearm(phys);
}

/* Capture-only asynchronous restore. Reinstalling the resident latch persona
 * touches PIO control registers and four instruction words; doing it as one
 * call can exceed the calibration phase WCET. */
bool tdma_pio_spi_phys_capture_restore_step(
    tdma_pio_spi_phys_t *phys, bool *complete)
{
    if (phys == NULL || complete == NULL) return false;
    *complete = false;
    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint sm = tdma_pio_spi_phys_latch_sm(phys);
    const uint offset = s_tdma_pio_spi_flight_clock_latch_offset;
    switch (phys->flight_normal_capture_restore_stage) {
    case 0u:
        pio_sm_set_enabled(evidence_pio, sm, false);
        pio_sm_clear_fifos(evidence_pio, sm);
        pio_sm_restart(evidence_pio, sm);
        phys->flight_normal_capture_restore_stage = 1u;
        return true;
    case 1u:
    case 2u:
    case 3u:
    case 4u: {
        const uint32_t index =
            phys->flight_normal_capture_restore_stage - 1u;
        evidence_pio->instr_mem[offset + index] =
            phys->flight_sck_waveform_saved_instructions[index];
        phys->flight_normal_capture_restore_stage++;
        return true;
    }
    case 5u:
        tdma_pio_spi_flight_clock_latch_program_init(
            evidence_pio, sm, offset,
            phys->role == TDMA_PIO_SPI_ROLE_MASTER
                ? phys->tx_csn_pin : phys->rx_csn_pin);
        phys->flight_sck_waveform_capture_deadline_us = 0ull;
        phys->flight_sck_waveform_capture_state =
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE;
        phys->flight_clock_latch_armed = false;
        phys->flight_normal_capture_restore_stage = 6u;
        *complete = true;
        return true;
    default:
        return false;
    }
}

bool tdma_pio_spi_phys_begin_ring_waveform_capture(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || !phys->armed || !phys->rx_capture_active ||
        (s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)) {
        return false;
    }
    if (phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_PATCHED ||
        phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED ||
        phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY) {
        if (!tdma_pio_spi_phys_restore_clock_latch(phys, false)) {
            return false;
        }
    }
    phys->flight_sck_waveform_capture_state =
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_REQUESTED;
    phys->flight_normal_capture_copy_stage = 0u;
    phys->flight_normal_capture_sck_cursor = 0u;
    phys->flight_normal_capture_restore_stage = 0u;
    phys->flight_normal_capture_rx_produced = 0u;
    phys->flight_normal_capture_rx_start = 0u;
    phys->flight_normal_capture_rx_count = 0u;
    phys->flight_normal_capture_rx_cursor = 0u;
    return true;
}

tdma_pio_spi_ring_waveform_capture_state_t
tdma_pio_spi_phys_service_ring_waveform_capture(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED;
    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint sm = tdma_pio_spi_phys_latch_sm(phys);
    const uint offset = s_tdma_pio_spi_flight_clock_latch_offset;
    if (phys->flight_sck_waveform_capture_state ==
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_REQUESTED) {
        pio_sm_set_enabled(evidence_pio, sm, false);
        pio_sm_clear_fifos(evidence_pio, sm);
        pio_sm_restart(evidence_pio, sm);
        for (uint32_t index = 0u; index < 4u; index++) {
            phys->flight_sck_waveform_saved_instructions[index] =
                evidence_pio->instr_mem[offset + index];
        }

        /* The product persona already occupies all 32 PIO instructions.
         * Reuse only the capture SM's four-instruction latch region for this
         * bounded job. Persona patch and SM configuration are deliberately
         * separate core1 beats so neither can exceed the calibration phase
         * WCET or borrow a later load's budget. */
        evidence_pio->instr_mem[offset + 0u] =
            (uint16_t)pio_encode_wait_gpio(false, phys->rx_csn_pin);
        evidence_pio->instr_mem[offset + 1u] =
            (uint16_t)pio_encode_in(pio_pins, 1u);
        evidence_pio->instr_mem[offset + 2u] =
            (uint16_t)pio_encode_nop();
        evidence_pio->instr_mem[offset + 3u] =
            (uint16_t)pio_encode_nop();
        phys->flight_clock_latch_armed = false;
        phys->flight_sck_waveform_capture_state =
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_PATCHED;
        return phys->flight_sck_waveform_capture_state;
    }
    if (phys->flight_sck_waveform_capture_state ==
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_PATCHED) {
        pio_sm_config config = pio_get_default_sm_config();
        /* Execute WAIT once, then hardware-wrap the single 4 ns IN. */
        sm_config_set_wrap(&config, offset + 1u, offset + 1u);
        sm_config_set_in_pins(&config, phys->rx_sck_pin);
        sm_config_set_in_shift(&config, true, true, 32u);
        sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
        sm_config_set_clkdiv(&config, 1.0f);
        sm_config_set_jmp_pin(&config, phys->rx_sck_pin);
        pio_sm_init(evidence_pio, sm, offset, &config);
        phys->flight_sck_waveform_capture_deadline_us =
            tdma_pio_spi_phys_now_us() +
            TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_TIMEOUT_US;
        phys->flight_sck_waveform_capture_state =
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED;
        pio_sm_set_enabled(evidence_pio, sm, true);
        return phys->flight_sck_waveform_capture_state;
    }
    if (phys->flight_sck_waveform_capture_state !=
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED) {
        return phys->flight_sck_waveform_capture_state;
    }
    if (pio_sm_get_rx_fifo_level(
            evidence_pio, sm) >=
        TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS) {
        phys->flight_sck_waveform_capture_state =
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY;
        return phys->flight_sck_waveform_capture_state;
    }
    if (tdma_pio_spi_phys_now_us() >=
        phys->flight_sck_waveform_capture_deadline_us) {
        (void)tdma_pio_spi_phys_restore_clock_latch(phys, true);
        phys->flight_sck_waveform_capture_state =
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED;
    }
    return phys->flight_sck_waveform_capture_state;
}

