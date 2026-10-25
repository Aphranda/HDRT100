#include "tdma_pio_spi_phys.h"
#include "tdma_service_timing.h"
#include "tdma_pio_spi_phys_programs.h"
#include "tdma_pio_spi_phys_internal.h"
#include "tdma_pio_spi_origin_workspace.h"
#include "tdma_origin_build_job.h"

#include <string.h>

#include "board_config.h"
#include "project_config.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "drv_watchdog.h"
#include "pico/bit_ops.h"
#include "pico/time.h"
#include "tdma_flight_overlay.h"
#include "tdma_pio_spi.pio.h"
#include "tdma_origin.pio.h"
#include "tdma_process_image_layout.h"
#include "tdma_pio_spi_phys_timing.h"
#include "tdma_rx_sequence.h"
#include "vdc_timestamp_clock.h"


/* The paired event observer replaces the legacy follower TX-latch SM in the
 * process-follower persona. Convert its exact, sequence-bound PIO counter into
 * the same TIMER1 nanosecond domain used by the ordinary latch. The event
 * record remains diagnostic on its own; only this physical owner bridge can
 * attach it to the already CRC-validated transport sequence. */
static bool tdma_pio_spi_phys_take_event_tx_edge_ex(
    tdma_pio_spi_phys_t *phys,
    uint32_t expected_sequence,
    uint32_t expected_identity_crc32,
    tdma_ring_local_tx_edge_evidence_t *evidence)
{
#if PROJECT_TDMA_EVENT_OBSERVER && defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    tdma_pio_spi_event_live_snapshot_t live;
    tdma_pio_spi_event_exact_t exact;
    if (phys == NULL || evidence == NULL || expected_sequence == 0u ||
        expected_identity_crc32 == 0u ||
        !tdma_pio_spi_phys_event_get_live_snapshot(phys, &live) ||
        (live.flags & (TDMA_EVENT_LIVE_ACTIVE |
                       TDMA_EVENT_LIVE_ANCHOR_VALID)) !=
            (TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID) ||
        live.arm_epoch == 0u || live.record.epoch == 0u || live.tick_hz == 0u ||
        !vdc_timestamp_clock_is_current(live.tick_hz)) {
        return false;
    }
    const tdma_event_exact_result_t exact_result =
        tdma_pio_spi_phys_event_copy_history_exact(
            phys, live.arm_epoch, live.record.epoch, expected_sequence,
            &exact);
    if (exact_result != TDMA_EVENT_EXACT_OK ||
        exact.record.sequence != expected_sequence ||
        exact.record.tx_elapsed_cycles == 0u ||
        UINT64_MAX - live.timer1_enable_after < exact.record.tx_elapsed_cycles) {
        return false;
    }
    const uint64_t tx_ticks = live.timer1_enable_after +
        exact.record.tx_elapsed_cycles;
    const uint64_t tx_ns = vdc_timestamp_clock_ticks_to_ns(tx_ticks);
    if (tx_ns == 0u) {
        return false;
    }
    phys->flight_tx_edge_capture_generation++;
    evidence->timestamp_ns = tx_ns;
    evidence->sequence = expected_sequence;
    evidence->identity_crc32 = expected_identity_crc32;
    evidence->capture_generation = phys->flight_tx_edge_capture_generation;
    evidence->flags = TDMA_RING_LOCAL_TX_EDGE_FLAG_TIMESTAMP_VALID |
                      TDMA_RING_LOCAL_TX_EDGE_FLAG_SEQUENCE_BOUND |
                      TDMA_RING_LOCAL_TX_EDGE_FLAG_IDENTITY_BOUND;
    return true;
#else
    (void)phys;
    (void)expected_sequence;
    (void)expected_identity_crc32;
    (void)evidence;
    return false;
#endif
}

bool tdma_pio_spi_phys_take_local_tx_edge_ex(
    void *context,
    uint32_t expected_sequence,
    uint32_t expected_identity_crc32,
    tdma_ring_local_tx_edge_evidence_t *evidence)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (evidence != NULL) {
        memset(evidence, 0, sizeof(*evidence));
    }
    if (phys != NULL && evidence != NULL &&
        tdma_pio_spi_phys_event_selected(phys)) {
        return tdma_pio_spi_phys_take_event_tx_edge_ex(
            phys, expected_sequence, expected_identity_crc32, evidence);
    }
    if (phys == NULL || evidence == NULL ||
        !tdma_pio_spi_phys_take_local_tx_edge(phys,
                                               &evidence->timestamp_ns)) {
        return false;
    }
    phys->flight_tx_edge_capture_generation++;
    evidence->sequence = expected_sequence;
    evidence->identity_crc32 = expected_identity_crc32;
    evidence->capture_generation = phys->flight_tx_edge_capture_generation;
    evidence->flags = TDMA_RING_LOCAL_TX_EDGE_FLAG_TIMESTAMP_VALID;
    if (expected_sequence != 0u) {
        evidence->flags |= TDMA_RING_LOCAL_TX_EDGE_FLAG_SEQUENCE_BOUND;
    }
    if (expected_identity_crc32 != 0u) {
        evidence->flags |= TDMA_RING_LOCAL_TX_EDGE_FLAG_IDENTITY_BOUND;
    }
    return true;
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
            tdma_pio_spi_phys_latch_offset(phys) + index] =
                phys->flight_sck_waveform_saved_instructions[index];
    }
    tdma_pio_spi_flight_clock_latch_program_init(
        evidence_pio, sm,
        tdma_pio_spi_phys_latch_offset(phys),
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
    const uint offset = tdma_pio_spi_phys_latch_offset(phys);
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
    const uint offset = tdma_pio_spi_phys_latch_offset(phys);
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
    if (pio_sm_get_rx_fifo_level(evidence_pio, sm) >=
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

void tdma_pio_spi_phys_flight_origin_recover(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return;
    }
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    const PIO control_pio = tdma_pio_spi_phys_control_pio(phys);
    const PIO data_pio = tdma_pio_spi_phys_data_pio(phys);
    const PIO capture_pio = tdma_pio_spi_phys_capture_pio(phys);
    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint control_sm = tdma_pio_spi_phys_control_sm(phys);
    const uint data_sm = tdma_pio_spi_phys_data_sm(phys);
    const uint capture_sm = tdma_pio_spi_phys_capture_sm(phys);
    const uint latch_sm = tdma_pio_spi_phys_latch_sm(phys);
    const uint rtt_sm = tdma_pio_spi_phys_rtt_sm(phys);
    pio_sm_set_enabled(control_pio, control_sm, false);
    pio_sm_set_enabled(data_pio, data_sm, false);
    pio_sm_set_enabled(capture_pio, capture_sm, false);
    pio_sm_set_enabled(evidence_pio, latch_sm, false);
    pio_sm_set_enabled(evidence_pio, rtt_sm, false);
    pio_sm_clear_fifos(control_pio, control_sm);
    pio_sm_clear_fifos(data_pio, data_sm);
    pio_sm_clear_fifos(capture_pio, capture_sm);
    pio_sm_clear_fifos(evidence_pio, latch_sm);
    pio_sm_clear_fifos(evidence_pio, rtt_sm);
    pio_sm_restart(control_pio, control_sm);
    pio_sm_restart(data_pio, data_sm);
    pio_sm_restart(capture_pio, capture_sm);
    pio_sm_restart(evidence_pio, latch_sm);
    pio_sm_restart(evidence_pio, rtt_sm);
    pio_sm_exec(control_pio, control_sm,
                pio_encode_jmp(s_tdma_pio_spi_flight_origin_clock_offset));
    pio_sm_exec(data_pio, data_sm,
                pio_encode_jmp(s_tdma_pio_spi_flight_origin_data_offset));
    pio_sm_exec(
        capture_pio, capture_sm,
        pio_encode_jmp(s_tdma_pio_spi_flight_origin_data_capture_offset));
    pio_sm_exec(evidence_pio, rtt_sm,
                pio_encode_jmp(s_tdma_pio_spi_flight_origin_rtt_offset));
    /* Recovery preserves the flight contract: core1 never drives a control
     * edge. Restore {CS=1,SCK=0} through the origin control SM before it is
     * re-enabled at its blocking PULL. */
    pio_sm_set_pins_with_mask64(
        control_pio,
        control_sm,
        1ull << phys->tx_csn_pin,
        (1ull << phys->tx_sck_pin) | (1ull << phys->tx_csn_pin));
    pio_interrupt_clear(control_pio, 1u);
    control_pio->fdebug = tdma_pio_spi_phys_txstall_mask(control_sm);
    pio_sm_set_enabled(data_pio, data_sm, true);
    pio_sm_set_enabled(capture_pio, capture_sm, true);
    pio_sm_set_enabled(evidence_pio, rtt_sm, true);
    (void)tdma_pio_spi_phys_clock_latch_rearm(phys);
    pio_sm_set_enabled(control_pio, control_sm, true);
    phys->snapshot.origin_recovery_count++;
}

bool tdma_pio_spi_phys_configure_flight(
    tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config)
{
    if (phys == NULL || config == NULL) {
        return false;
    }
#if PROJECT_TDMA_EVENT_OBSERVER && defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (s_tdma_event_dma_active) {
        tdma_pio_spi_phys_event_stop(phys);
        if (s_tdma_event_dma_active) return false;
    }
#endif
    phys->flight_resources = tdma_state_machine_resource_contract();
    if (!tdma_state_machine_rx_endpoint_contract_valid(
            &phys->flight_resources.rx_endpoints)) {
        return false;
    }
    phys->tx_pin = phys->flight_resources.rx_data_out_pin;
    phys->tx_sck_pin = phys->flight_resources.tx_clk_out_pin;
    phys->tx_csn_pin = phys->flight_resources.tx_sync_out_pin;
    phys->rx_pin = phys->flight_resources.tx_data_in_pin;
    phys->rx_sck_pin = phys->flight_resources.rx_clk_in_pin;
    phys->rx_csn_pin = phys->flight_resources.rx_sync_in_pin;

    if (phys->role == TDMA_PIO_SPI_ROLE_MASTER) {
        /* Origin control and returned-DATA capture live on TX PIO; reverse
         * DATA output is isolated on RX PIO. */
        phys->rx_sm = phys->flight_resources.tx_data_capture_sm;
        phys->tx_sm = phys->flight_resources.rx_endpoints.data_output.sm;
        tdma_pio_spi_flight_origin_clock_rx_program_init(
            phys->flight_resources.tx_pio,
            phys->flight_resources.tx_control_out_sm,
            s_tdma_pio_spi_flight_origin_clock_offset,
            phys->tx_sck_pin,
            phys->tx_csn_pin,
            phys->baud_hz);
        tdma_pio_spi_flight_origin_data_tx_program_init(
            phys->flight_resources.rx_endpoints.data_output.pio,
            phys->flight_resources.rx_endpoints.data_output.sm,
            s_tdma_pio_spi_flight_origin_data_offset,
            phys->tx_pin,
            phys->rx_csn_pin,
            phys->rx_sck_pin,
            phys->flight_sck_phase_delay_cycles,
            phys->flight_data_phase_delay_cycles);
        tdma_pio_spi_flight_origin_data_capture_program_init(
            phys->flight_resources.tx_pio,
            phys->flight_resources.tx_data_capture_sm,
            s_tdma_pio_spi_flight_origin_data_capture_offset,
            phys->rx_pin,
            phys->rx_csn_pin,
            phys->rx_sck_pin,
            phys->flight_origin_capture_phase_delay_cycles);
        tdma_pio_spi_flight_origin_rtt_program_init(
            phys->flight_resources.tx_pio,
            phys->flight_resources.tx_rtt_evidence_sm,
            s_tdma_pio_spi_flight_origin_rtt_offset,
            phys->tx_csn_pin,
            phys->rx_csn_pin);
        if (!tdma_pio_spi_phys_ensure_tx_dma()) {
            return false;
        }
    } else {
        /* rx_sm performs DATA capture/reverse forwarding. tx_sm independently
         * regenerates the complete forward MARK/SCK control pair. */
        phys->rx_sm = phys->flight_resources.rx_endpoints.data_unload.sm;
        phys->tx_sm = phys->flight_resources.tx_control_out_sm;
        if (phys->process_image_enabled) {
            tdma_pio_spi_flight_process_follower_program_init(
                phys->flight_resources.rx_endpoints.data_unload.pio,
                phys->rx_sm,
                s_tdma_pio_spi_flight_process_follower_offset,
                phys->rx_pin,
                phys->tx_pin,
                phys->rx_csn_pin,
                phys->rx_sck_pin,
                phys->flight_sck_phase_delay_cycles,
                phys->flight_data_phase_delay_cycles);
            if (!tdma_pio_spi_phys_ensure_tx_dma()) {
                return false;
            }
        } else {
            tdma_pio_spi_flight_data_follower_program_init(
                phys->flight_resources.rx_endpoints.data_unload.pio,
                phys->rx_sm,
                s_tdma_pio_spi_flight_data_follower_offset,
                phys->rx_pin,
                phys->tx_pin,
                phys->rx_sck_pin,
                phys->flight_data_phase_delay_cycles);
        }
        tdma_pio_spi_flight_control_forward_program_init(
            phys->flight_resources.tx_pio,
            phys->tx_sm,
            s_tdma_pio_spi_flight_control_forward_offset,
            phys->rx_csn_pin,
            phys->rx_sck_pin,
            phys->tx_sck_pin,
            phys->tx_csn_pin,
            phys->flight_marker_phase_delay_cycles,
            phys->flight_sck_phase_delay_cycles);
    }
    tdma_pio_spi_flight_clock_latch_program_init(
        tdma_pio_spi_phys_evidence_pio(phys),
        tdma_pio_spi_phys_latch_sm(phys),
        tdma_pio_spi_phys_latch_offset(phys),
        phys->role == TDMA_PIO_SPI_ROLE_MASTER
            ? phys->tx_csn_pin
            : phys->rx_csn_pin);
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE && !tdma_pio_spi_phys_event_selected(phys)) {
        tdma_pio_spi_flight_clock_latch_program_init(
            tdma_pio_spi_phys_tx_latch_pio(phys),
            tdma_pio_spi_phys_tx_latch_sm(phys),
        tdma_pio_spi_phys_latch_offset(phys),
            phys->tx_csn_pin);
    }
    tdma_pio_spi_phys_prepare_sm_pair(phys);
    tdma_pio_spi_phys_event_prepare(phys, config);
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
        phys->process_image_enabled) {
        /* Initialize the elastic tail outside the wire loop, leaving the PIO
         * instruction budget to the independent control and DATA paths. */
        pio_sm_exec(tdma_pio_spi_phys_data_pio(phys),
                    tdma_pio_spi_phys_data_sm(phys),
                    pio_encode_mov(pio_isr, pio_null));
    }
    return true;
}

static uint32_t tdma_pio_spi_phys_overlay_final_pc(void)
{
    return s_tdma_pio_spi_flight_process_follower_offset +
           tdma_pio_spi_flight_process_follower_offset_final_bit;
}

static bool tdma_pio_spi_phys_overlay_dma_busy(tdma_pio_spi_phys_t *phys)
{
    /* Both channels may briefly be idle between control blocks. Recurrence
     * owns its pools until bounded STOP, independently of a BUSY sample. */
    return phys->flight_overlay_dma_active;
}

static void tdma_pio_spi_phys_service_overlay_pending(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || !phys->flight_overlay_dma_active) {
        return;
    }
    const uint32_t selected = phys->flight_overlay_selected_generation;
    __dmb();
    phys->snapshot.overlay_selected_generation = selected;
    if (phys->flight_overlay_pending &&
        selected == phys->flight_overlay_published_generation) {
        phys->flight_overlay_active_buffer = phys->flight_overlay_pending_buffer;
        phys->flight_overlay_pending = false;
        phys->snapshot.overlay_selection_pending = 0u;
    }
}

static tdma_flight_overlay_binding_t tdma_pio_spi_phys_overlay_binding(
    const tdma_pio_spi_phys_t *phys)
{
    const uint output = (uint)s_tdma_pio_spi_tx_dma_channel;
    const uint loader = (uint)s_tdma_pio_spi_command_dma_channel;
    dma_channel_config cfg = dma_channel_get_default_config(output);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_high_priority(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_chain_to(&cfg, loader);
    channel_config_set_dreq(&cfg, pio_get_dreq(tdma_pio_spi_phys_data_pio(phys),
                                             tdma_pio_spi_phys_data_sm(phys), true));
    tdma_flight_overlay_binding_t binding = {
        .pass_control = channel_config_get_ctrl_value(&cfg),
        .tx_fifo_address = (uint32_t)(uintptr_t)
            &tdma_pio_spi_phys_data_pio(phys)->txf[tdma_pio_spi_phys_data_sm(phys)],
        .live_word_address = (uint32_t)(uintptr_t)&s_tdma_pio_spi_flight_live_word,
        .selected_generation_address = (uint32_t)(uintptr_t)&phys->flight_overlay_selected_generation,
        .loader_trigger_address = (uint32_t)(uintptr_t)&dma_hw->ch[loader].al3_read_addr_trig,
        .next_address_address = (uint32_t)(uintptr_t)&phys->flight_overlay_next_address,
    };
    channel_config_set_read_increment(&cfg, true);
    binding.token_control = channel_config_get_ctrl_value(&cfg);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_dreq(&cfg, DREQ_FORCE);
    binding.selection_control = channel_config_get_ctrl_value(&cfg);
    channel_config_set_chain_to(&cfg, output);
    binding.restart_control = channel_config_get_ctrl_value(&cfg);
    return binding;
}

static bool tdma_pio_spi_phys_start_overlay_script(
    tdma_pio_spi_phys_t *phys, uint32_t buffer_index, bool worker_bound)
{
    if (phys == NULL || !phys->flight_resource_claimed ||
        s_tdma_pio_spi_command_dma_channel < 0 ||
        buffer_index >= 2u || s_tdma_pio_spi_tx_dma_channel < 0) {
        if (phys != NULL) {
            phys->snapshot.overlay_last_error =
                TDMA_PIO_SPI_OVERLAY_ERROR_DMA_START_INVALID;
        }
        return false;
    }
    tdma_pio_spi_phys_service_overlay_pending(phys);
    if (phys->flight_overlay_pending ||
        (phys->flight_overlay_dma_active &&
         buffer_index == phys->flight_overlay_active_buffer)) {
        phys->snapshot.overlay_last_error =
            TDMA_PIO_SPI_OVERLAY_ERROR_DMA_BUSY_TIMEOUT;
        phys->snapshot.overlay_tx_dma_remaining =
            dma_hw->ch[s_tdma_pio_spi_tx_dma_channel].transfer_count;
        phys->snapshot.overlay_tx_dma_busy = 1u;
        phys->snapshot.overlay_tx_fifo_level_at_fail =
            pio_sm_get_tx_fifo_level(tdma_pio_spi_phys_data_pio(phys),
                                     tdma_pio_spi_phys_data_sm(phys));
        phys->snapshot.overlay_prepare_wait_us = 0u;
        return false;
    }
    tdma_flight_overlay_plan_t *plan =
        &s_tdma_pio_spi_flight_overlay_plan[buffer_index];
    if (plan->command_word_count != phys->flight_physical_byte_count *
            TDMA_FLIGHT_OVERLAY_COMMAND_WORDS_PER_BYTE ||
        plan->generation != 0u ||
        plan->run_count <= (worker_bound ? TDMA_FLIGHT_OVERLAY_CONTROL_RUNS : 0u) ||
        plan->run_count > (worker_bound ? TDMA_FLIGHT_OVERLAY_BOUND_RUN_MAX : TDMA_FLIGHT_OVERLAY_RUN_MAX) ||
        plan->token_word_count > TDMA_FLIGHT_OVERLAY_TOKEN_WORD_MAX ||
        (!worker_bound && tdma_overlay_prepare_state(phys->overlay_preparation) !=
            TDMA_OVERLAY_PREPARE_IDLE)) {
        phys->snapshot.overlay_last_error = TDMA_PIO_SPI_OVERLAY_ERROR_BUILD_FAILED;
        return false;
    }
    const uint output = (uint)s_tdma_pio_spi_tx_dma_channel;
    const uint loader = (uint)s_tdma_pio_spi_command_dma_channel;
    const tdma_state_machine_command_dma_contract_t command =
        tdma_state_machine_command_dma_contract();
    if (command.control_descriptor_count != TDMA_FLIGHT_OVERLAY_CONTROL_RUNS) {
        phys->snapshot.overlay_last_error = TDMA_PIO_SPI_OVERLAY_ERROR_BUILD_FAILED;
        return false;
    }
    if (!worker_bound) {
        const tdma_flight_overlay_binding_t binding = tdma_pio_spi_phys_overlay_binding(phys);
        if (!tdma_flight_overlay_bind_plan(plan, tdma_pio_spi_phys_overlay_final_pc(), &binding)) {
            phys->snapshot.overlay_last_error = TDMA_PIO_SPI_OVERLAY_ERROR_BUILD_FAILED;
            return false;
        }
        /* ARM's pass plan freezes the admitted register words and addresses.
         * No worker exists here; later grants lease only inactive SRAM. The
         * template remains unchanged until STOP drains the job and hardware. */
        if (phys->overlay_preparation != NULL) phys->overlay_preparation->binding = binding;
    }
    uint32_t generation = phys->flight_overlay_published_generation + 1u;
    if (generation == 0u) generation = 1u;
    plan->generation = generation;

    /* Publish only one successor. DMA selection may lead physical CS by FIFO
     * prefetch; it proves memory retirement, never SENT or wire completion. */
    phys->flight_overlay_pending_buffer = buffer_index;
    phys->flight_overlay_published_generation = generation;
    phys->flight_overlay_pending = true;
    phys->snapshot.overlay_published_generation = generation;
    phys->snapshot.overlay_selection_pending = 1u;
    __dmb();
    phys->flight_overlay_next_address = (uint32_t)(uintptr_t)plan->run;
    __dmb();

    /* ARM starts the loader once. Every later publication changes only the
     * protected SRAM pointer; hardware chooses it at its next plan boundary. */
    if (!phys->flight_overlay_dma_active) {
        dma_channel_config loader_cfg = dma_channel_get_default_config(loader);
        channel_config_set_transfer_data_size(&loader_cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&loader_cfg, true);
        channel_config_set_write_increment(&loader_cfg, true);
        channel_config_set_ring(&loader_cfg, true, command.descriptor_write_ring_log2);
        channel_config_set_high_priority(&loader_cfg, true);
        channel_config_set_chain_to(&loader_cfg, loader);
        phys->flight_overlay_dma_active = true;
        __dmb();
        dma_channel_configure(loader, &loader_cfg, &dma_hw->ch[output].al3_ctrl,
                              plan->run, command.descriptor_words, true);
    }
    phys->snapshot.overlay_tx_dma_remaining = plan->command_word_count;
    phys->snapshot.overlay_tx_dma_busy = 1u;
    phys->snapshot.overlay_prepare_wait_us = 0u;
    phys->snapshot.overlay_last_error = TDMA_PIO_SPI_OVERLAY_ERROR_NONE;
    return true;
}

static uint32_t tdma_pio_spi_phys_overlay_free_buffer(
    const tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return 0u;
    }
    return phys->flight_overlay_active_buffer ^ 1u;
}

static bool tdma_pio_spi_phys_queue_overlay_script(
    tdma_pio_spi_phys_t *phys,
    uint32_t buffer_index)
{
    if (phys == NULL || buffer_index >= 2u) {
        return false;
    }
    return tdma_pio_spi_phys_start_overlay_script(phys, buffer_index, false);
}

bool tdma_pio_spi_phys_process_overlay_ready(void *context)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->armed || !phys->flight_overlay_dma_active ||
        !phys->process_image_enabled || phys->role != TDMA_PIO_SPI_ROLE_SLAVE ||
        s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)
        return false;
    tdma_pio_spi_phys_service_overlay_pending(phys);
    return !phys->flight_overlay_pending &&
        phys->flight_overlay_alignment_samples >=
            TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES;
}

bool tdma_pio_spi_phys_grant_overlay(void *context, tdma_overlay_prepare_t *job)
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL || job == NULL || phys->overlay_preparation != job ||
        tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_IDLE ||
        !phys->process_image_enabled || phys->role != TDMA_PIO_SPI_ROLE_SLAVE ||
        s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER ||
        !tdma_pio_spi_phys_process_overlay_ready(phys)) return false;
    job->buffer_index = tdma_pio_spi_phys_overlay_free_buffer(phys);
    job->plan = &s_tdma_pio_spi_flight_overlay_plan[job->buffer_index];
    job->config = (tdma_flight_overlay_config_t){
        .packet_size = TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size,
        .outer_header_size = TDMA_PIO_SPI_PACKET_HEADER_SIZE,
        .alignment_byte_shift = phys->flight_alignment_byte_shift,
        .alignment_bit_shift = phys->flight_alignment_bit_shift,
        .physical_byte_count = phys->flight_physical_byte_count,
        .local_slot_id = phys->flight_local_slot_id,
        .header_write_mask = tdma_transport_frame_resident_overlay_header_mask(),
        .final_bit_pc = tdma_pio_spi_phys_overlay_final_pc(),
    };
    return true;
}

bool tdma_pio_spi_phys_commit_overlay(void *context, tdma_overlay_prepare_t *job)
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL || job == NULL || phys->overlay_preparation != job ||
        tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_READY ||
        job->request_epoch != job->epoch ||
        !phys->process_image_enabled || phys->role != TDMA_PIO_SPI_ROLE_SLAVE ||
        s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER ||
        !tdma_pio_spi_phys_process_overlay_ready(phys) ||
        job->buffer_index != tdma_pio_spi_phys_overlay_free_buffer(phys) ||
        job->plan != &s_tdma_pio_spi_flight_overlay_plan[job->buffer_index] ||
        job->config.alignment_byte_shift != phys->flight_alignment_byte_shift ||
        job->config.alignment_bit_shift != phys->flight_alignment_bit_shift ||
        job->config.physical_byte_count != phys->flight_physical_byte_count ||
        job->config.packet_size != TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size ||
        job->config.local_slot_id != phys->flight_local_slot_id ||
        job->config.final_bit_pc != tdma_pio_spi_phys_overlay_final_pc()) return false;
    /* Core0 validated tokens and bound every descriptor using the template
     * frozen at ARM, then released READY. Resources stay owned until STOP
     * drains the lease. Only generation and publication remain on Core1. */
    if (!tdma_pio_spi_phys_start_overlay_script(phys, job->buffer_index, true)) return false;
    if (!phys->flight_overlay_alignment_locked) tdma_geometry_trained(phys);
    phys->flight_overlay_alignment_locked = true;
    phys->snapshot.overlay_prepare_count++;
    phys->snapshot.overlay_replacement_byte_count += job->plan->replacement_byte_count;
    phys->snapshot.overlay_alignment_byte_shift = job->config.alignment_byte_shift;
    phys->snapshot.overlay_alignment_bit_shift = job->config.alignment_bit_shift;
    phys->snapshot.overlay_physical_byte_count = job->config.physical_byte_count;
    return true;
}

bool tdma_pio_spi_phys_prepare_pass_overlay(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || phys->flight_physical_byte_count == 0u ||
        phys->flight_physical_byte_count + 1u >
            TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS) {
        return false;
    }
    if (s_tdma_pio_spi_tx_dma_channel < 0 || phys->flight_overlay_pending) {
        return false;
    }
    const uint32_t buffer_index =
        tdma_pio_spi_phys_overlay_free_buffer(phys);
    if (!tdma_flight_overlay_build_pass_plan(phys->flight_physical_byte_count,
            tdma_pio_spi_phys_overlay_final_pc(),
            &s_tdma_pio_spi_flight_overlay_plan[buffer_index])) return false;
    return tdma_pio_spi_phys_queue_overlay_script(phys, buffer_index);
}

bool tdma_pio_spi_phys_service_process_overlay_boundary(void *context)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->armed || !phys->process_image_enabled ||
        phys->role != TDMA_PIO_SPI_ROLE_SLAVE ||
        s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER) {
        return false;
    }
    tdma_pio_spi_phys_service_overlay_pending(phys);
    tdma_priority_boundary_service(phys);
    return true;
}

bool tdma_pio_spi_phys_set_process_image_mode(
    tdma_pio_spi_phys_t *phys,
    bool enabled,
    uint32_t payload_size)
{
    if (phys == NULL || phys->armed ||
        payload_size > TDMA_TRANSPORT_SHORT_PAYLOAD_MAX ||
        (enabled && payload_size == 0u)) {
        return false;
    }
    phys->process_image_enabled = enabled;
    phys->flight_payload_size = payload_size;
    return true;
}

bool tdma_pio_spi_phys_set_flight_payload_size(
    tdma_pio_spi_phys_t *phys,
    uint32_t payload_size)
{
    if (phys == NULL || phys->armed ||
        payload_size > TDMA_TRANSPORT_SHORT_PAYLOAD_MAX) {
        return false;
    }
    phys->flight_payload_size = payload_size;
    return true;
}

bool tdma_pio_spi_phys_set_flight_offsets(
    tdma_pio_spi_phys_t *phys,
    int32_t marker_offset_sample_count,
    int32_t sck_offset_sample_count,
    int32_t data_offset_sample_count,
    uint32_t marker_phase_delay_cycles,
    uint32_t sck_phase_delay_cycles,
    uint32_t data_phase_delay_cycles,
    uint32_t origin_capture_phase_delay_cycles)
{
    if (phys == NULL || phys->armed || marker_phase_delay_cycles == 0u ||
        sck_phase_delay_cycles == 0u || data_phase_delay_cycles == 0u ||
        marker_phase_delay_cycles > 31u ||
        sck_phase_delay_cycles > 31u ||
        data_phase_delay_cycles > 31u || origin_capture_phase_delay_cycles == 0u ||
        origin_capture_phase_delay_cycles > 31u) {
        return false;
    }
    phys->flight_marker_offset_sample_count = marker_offset_sample_count;
    phys->flight_sck_offset_sample_count = sck_offset_sample_count;
    phys->flight_data_offset_sample_count = data_offset_sample_count;
    phys->flight_marker_phase_delay_cycles = marker_phase_delay_cycles;
    phys->flight_sck_phase_delay_cycles = sck_phase_delay_cycles;
    phys->flight_data_phase_delay_cycles = data_phase_delay_cycles;
    phys->flight_origin_capture_phase_delay_cycles = origin_capture_phase_delay_cycles;
    return true;
}

bool tdma_pio_spi_phys_prepare_process_overlay(
    void *context,
    const uint8_t *incoming_packet,
    const uint8_t *processed_packet,
    size_t packet_size,
    const uint32_t *force_replace_payload_bitmap,
    size_t force_replace_payload_bitmap_words)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL ||
        tdma_overlay_prepare_state(phys->overlay_preparation) != TDMA_OVERLAY_PREPARE_IDLE ||
        !phys->armed || !phys->process_image_enabled ||
        phys->role != TDMA_PIO_SPI_ROLE_SLAVE ||
        s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER) {
        if (phys != NULL) {
            phys->snapshot.overlay_last_error =
                TDMA_PIO_SPI_OVERLAY_ERROR_BAD_STATE;
            phys->snapshot.overlay_prepare_fail_count++;
        }
        return false;
    }
    /* The owner preflights readiness before acquiring TX. A pending plan
     * must never be overwritten or reported as an accepted publication. */
    if (!tdma_pio_spi_phys_process_overlay_ready(phys)) {
        phys->snapshot.overlay_late_coalesce_count++;
        return false;
    }
    if (s_tdma_pio_spi_tx_dma_channel < 0) {
        phys->snapshot.overlay_prepare_fail_count++;
        phys->snapshot.overlay_last_error =
            TDMA_PIO_SPI_OVERLAY_ERROR_DMA_START_INVALID;
        return false;
    }
    const uint32_t buffer_index =
        tdma_pio_spi_phys_overlay_free_buffer(phys);
    tdma_flight_overlay_plan_t *plan = &s_tdma_pio_spi_flight_overlay_plan[buffer_index];
    phys->snapshot.overlay_alignment_byte_shift =
        phys->flight_alignment_byte_shift;
    phys->snapshot.overlay_alignment_bit_shift =
        phys->flight_alignment_bit_shift;
    const tdma_flight_overlay_config_t config = {
        .packet_size = TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size,
        .outer_header_size = TDMA_PIO_SPI_PACKET_HEADER_SIZE,
        .alignment_byte_shift = phys->flight_alignment_byte_shift,
        .alignment_bit_shift = phys->flight_alignment_bit_shift,
        .physical_byte_count = phys->flight_physical_byte_count,
        .local_slot_id = phys->flight_local_slot_id,
        .header_write_mask = tdma_transport_frame_resident_overlay_header_mask(),
        .final_bit_pc = tdma_pio_spi_phys_overlay_final_pc(),
    };
    if (!tdma_flight_overlay_build_plan(incoming_packet, processed_packet, packet_size,
            force_replace_payload_bitmap, force_replace_payload_bitmap_words, &config, plan)) {
        phys->snapshot.overlay_last_error =
            TDMA_PIO_SPI_OVERLAY_ERROR_BUILD_FAILED;
        phys->snapshot.overlay_tx_dma_remaining =
            s_tdma_pio_spi_tx_dma_channel >= 0
                ? dma_hw->ch[s_tdma_pio_spi_tx_dma_channel].transfer_count
                : 0u;
        phys->snapshot.overlay_tx_dma_busy =
            s_tdma_pio_spi_tx_dma_channel >= 0 &&
            tdma_pio_spi_phys_overlay_dma_busy(phys)
                ? 1u
                : 0u;
        phys->snapshot.overlay_tx_fifo_level_at_fail =
            pio_sm_get_tx_fifo_level(tdma_pio_spi_phys_data_pio(phys),
                                     tdma_pio_spi_phys_data_sm(phys));
        phys->snapshot.overlay_prepare_wait_us = 0u;
        phys->snapshot.overlay_prepare_fail_count++;
        return false;
    }
    if (!tdma_pio_spi_phys_queue_overlay_script(
            phys, buffer_index)) {
        phys->snapshot.overlay_prepare_fail_count++;
        return false;
    }
    if (!phys->flight_overlay_alignment_locked) tdma_geometry_trained(phys);
    phys->flight_overlay_alignment_locked = true;
    phys->snapshot.overlay_prepare_count++;
    phys->snapshot.overlay_replacement_byte_count +=
        plan->replacement_byte_count;
    phys->snapshot.overlay_alignment_byte_shift =
        phys->flight_alignment_byte_shift;
    phys->snapshot.overlay_alignment_bit_shift = phys->flight_alignment_bit_shift;
    phys->snapshot.overlay_physical_byte_count =
        phys->flight_physical_byte_count;
    return true;
}

void tdma_pio_spi_phys_set_line_drivers(bool enabled)
{
    /* DATA0, CLK1 and TRIG/CS have independent ISO1452 drivers. */
    gpio_put(BOARD_UP_BISS_DE_PIN, enabled);
    gpio_put(BOARD_DN_BISS_DE_PIN, enabled);
    gpio_put(BOARD_TRIG_DE_PIN, enabled);
}

void tdma_pio_spi_phys_cal_cleanup(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return;
    }
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    tdma_pio_spi_phys_set_line_drivers(false);
    const uint32_t pins[] = {
        BOARD_TDMA_SPI_UPLINK_RX_PIN,
        BOARD_TDMA_SPI_DOWNLINK_SCK_PIN,
        BOARD_TDMA_SPI_DOWNLINK_CSN_PIN,
        BOARD_TDMA_SPI_UPLINK_CSN_PIN,
        BOARD_TDMA_SPI_UPLINK_SCK_PIN,
        BOARD_TDMA_SPI_DOWNLINK_TX_PIN,
    };
    for (uint32_t i = 0u; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_set_function(pins[i], GPIO_FUNC_SIO);
        gpio_set_dir(pins[i], GPIO_IN);
    }
    phys->armed = false;
    phys->rx_capture_active = false;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
}

bool tdma_pio_spi_phys_rx_arm(tdma_pio_spi_phys_t *phys)
{
    s_tdma_pio_spi_rx_arm_valid = false;
    if (phys == NULL || phys->flight_origin_workspace_owned) return false;
    if (!tdma_pio_spi_phys_ensure_rx_dma()) {
        return false;
    }
    dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    tdma_pio_spi_phys_rx_prepare(phys);
    memset(s_tdma_pio_spi_rx_ring, 0, sizeof(s_tdma_pio_spi_rx_ring));
    s_tdma_pio_spi_rx_scan_produced = 0u;
    if (!tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,
                                    phys->flight_physical_byte_count,
                                    vdc_timestamp_clock_read_ticks64())) {
        return false;
    }
    dma_channel_config dma_cfg =
        dma_channel_get_default_config((uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_ring(&dma_cfg, true, TDMA_PIO_SPI_RX_RING_LOG2);
    channel_config_set_irq_quiet(&dma_cfg, true);
    const PIO capture_pio = tdma_pio_spi_phys_capture_pio(phys);
    const uint capture_sm = tdma_pio_spi_phys_capture_sm(phys);
    channel_config_set_dreq(
        &dma_cfg, pio_get_dreq(capture_pio, capture_sm, false));
    dma_channel_configure(
        (uint)s_tdma_pio_spi_rx_dma_channel,
        &dma_cfg,
        s_tdma_pio_spi_rx_ring,
        &capture_pio->rxf[capture_sm],
        dma_encode_transfer_count_with_self_trigger(s_tdma_pio_spi_rx_sequence.reload_words),
        false);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    if (s_tdma_pio_spi_rx_arm_epoch != UINT64_MAX) {
        ++s_tdma_pio_spi_rx_arm_epoch;
        s_tdma_pio_spi_rx_arm_valid = true;
    }
    phys->rx_capture_active = true;
    return true;
}

uint32_t tdma_pio_spi_phys_rx_write_index(void)
{
    const uintptr_t ring_base = (uintptr_t)s_tdma_pio_spi_rx_ring;
    const uintptr_t write_addr =
        (uintptr_t)dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].write_addr;
    return (uint32_t)(((write_addr - ring_base) &
                       ((TDMA_PIO_SPI_RX_RING_WORDS * sizeof(uint32_t)) -
                        1u)) /
                      sizeof(uint32_t));
}

uint64_t tdma_pio_spi_phys_rx_produced_words(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || s_tdma_pio_spi_rx_dma_channel < 0) return 0u;
    const uint64_t before_ticks = vdc_timestamp_clock_read_ticks64();
    const uint32_t count = dma_hw->ch[s_tdma_pio_spi_rx_dma_channel].transfer_count;
    __dmb();
    const uint64_t after_ticks = vdc_timestamp_clock_read_ticks64();
    uint64_t produced;
    bool discontinuity;
    if ((count & DMA_CH0_TRANS_COUNT_MODE_BITS) !=
            (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB) ||
        !tdma_rx_dma_counter_observe(&s_tdma_pio_spi_rx_sequence,
            count & DMA_CH0_TRANS_COUNT_COUNT_BITS, before_ticks, after_ticks,
            &produced, &discontinuity)) return 0u;
    if (discontinuity) {
        /* An ambiguous counter epoch is observation loss, never a reason to
         * stop, rearm, or relocate the hardware wire slots. */
        phys->snapshot.rx_observation_drop_count++;
        tdma_service_timing_rx_drop(TDMA_RX_DROP_EPOCH, 0u);
        s_tdma_pio_spi_rx_scan_produced = produced;
        if (!phys->flight_overlay_alignment_locked)
            phys->flight_overlay_alignment_samples = 0u;
    }
    return produced;
}

uint32_t tdma_pio_spi_phys_rx_ring_word(uint64_t produced)
{
    /* The DMA writer is outside the C abstract machine. Read each completed
     * word at its declared width; callers still revalidate epoch/overwrite. */
    const volatile uint32_t *ring = s_tdma_pio_spi_rx_ring;
    return ring[produced & (TDMA_PIO_SPI_RX_RING_WORDS - 1u)];
}

uint8_t tdma_pio_spi_phys_rx_ring_reversed_byte(uint64_t produced)
{
    const uint32_t word = tdma_pio_spi_phys_rx_ring_word(produced);
#if defined(__ARM_ARCH) && __ARM_ARCH >= 7 && defined(__GNUC__)
    /* Pico's __rev is an external bit reverse, unlike ACLE's byte swap.
     * Keep the RBIT in the copy loop on the supported ARM target. */
    return (uint8_t)__builtin_arm_rbit(word);
#else
    return (uint8_t)__rev(word);
#endif
}
