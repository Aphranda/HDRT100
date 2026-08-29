#include "tdma_pio_spi_phys_internal.h"

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "tdma_pio_spi.pio.h"
#include "tdma_state_machine_resources.h"

/* Flight and maintenance setup is kept out of the transport owner. These
 * helpers only configure resident PIO state; FIFO/DMA service remains in the
 * transport module. */
void tdma_pio_spi_phys_rx_prepare(tdma_pio_spi_phys_t *phys)
{
    /* The SM must keep running across frame boundaries. Resetting it here
     * would make the next capture depend on the CPU/service phase. */
    pio_sm_clear_fifos(tdma_pio_spi_phys_capture_pio(phys),
                       tdma_pio_spi_phys_capture_sm(phys));
}

/* Product-board SPI persona. Pin direction and PIO ownership are frozen in
 * board_config.h; CS remains the point-to-point frame-sync signal. */
void __attribute__((unused)) tdma_pio_spi_phys_configure(
    tdma_pio_spi_phys_t *phys)
{
    phys->tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    phys->tx_pin = BOARD_TDMA_SPI_DOWNLINK_TX_PIN;
    phys->tx_sck_pin = BOARD_TDMA_SPI_DOWNLINK_SCK_PIN;
    phys->tx_csn_pin = BOARD_TDMA_SPI_DOWNLINK_CSN_PIN;
    phys->rx_sm = BOARD_TDMA_SPI_SLAVE_SM;
    phys->rx_pin = BOARD_TDMA_SPI_UPLINK_RX_PIN;
    phys->rx_sck_pin = BOARD_TDMA_SPI_UPLINK_SCK_PIN;
    phys->rx_csn_pin = BOARD_TDMA_SPI_UPLINK_CSN_PIN;

    tdma_pio_spi_tx_byte_program_init(BOARD_TDMA_SPI_PIO,
                                      phys->tx_sm,
                                      s_tdma_pio_spi_tx_offset,
                                      phys->tx_pin,
                                      phys->tx_sck_pin,
                                      phys->baud_hz);
    tdma_pio_spi_rx_byte_program_init(BOARD_TDMA_SPI_PIO,
                                      phys->rx_sm,
                                      s_tdma_pio_spi_rx_offset,
                                      phys->rx_pin,
                                      phys->rx_csn_pin,
                                      phys->rx_sck_pin);
    gpio_init(phys->tx_csn_pin);
    gpio_set_dir(phys->tx_csn_pin, GPIO_OUT);
    gpio_put(phys->tx_csn_pin, true);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, true);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, true);
}

uint32_t tdma_pio_spi_phys_flight_tail_bytes(
    const tdma_ring_runtime_config_t *config)
{
    if (config == NULL || config->node_count < 2u || config->baud_hz == 0u) {
        return 0u;
    }
    const uint64_t loop_bits =
        ((uint64_t)config->loop_delay_ns * config->baud_hz +
         999999999ull) /
        1000000000ull;
    const uint32_t loop_bytes = (uint32_t)((loop_bits + 7ull) / 8ull);
    /* One elastic byte per follower, plus two guard bytes for the returned
     * CS/SCK phase and the final DATA propagation. */
    return (config->node_count - 1u) + loop_bytes + 2u;
}

void tdma_pio_spi_phys_prepare_sm_pair(tdma_pio_spi_phys_t *phys)
{
    const PIO control_pio = tdma_pio_spi_phys_control_pio(phys);
    const PIO data_pio = tdma_pio_spi_phys_data_pio(phys);
    const uint control_sm = tdma_pio_spi_phys_control_sm(phys);
    const uint data_sm = tdma_pio_spi_phys_data_sm(phys);
    const PIO capture_pio = tdma_pio_spi_phys_capture_pio(phys);
    const uint capture_sm = tdma_pio_spi_phys_capture_sm(phys);
    const bool has_rtt_sm = phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN;
    const uint latch_sm = phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? phys->flight_resources.tx_data_in_forward_sm
        : phys->flight_resources.rx_evidence_in_sm;
    pio_set_sm_mask_enabled(control_pio, 1u << control_sm, false);
    pio_set_sm_mask_enabled(data_pio, 1u << data_sm, false);
    pio_sm_clear_fifos(control_pio, control_sm);
    pio_sm_clear_fifos(data_pio, data_sm);
    pio_sm_clear_fifos(capture_pio, capture_sm);
    pio_sm_restart(control_pio, control_sm);
    pio_sm_restart(data_pio, data_sm);
    pio_sm_restart(capture_pio, capture_sm);
    if (has_rtt_sm) {
        pio_sm_clear_fifos(control_pio,
                           phys->flight_resources.tx_sync_out_sm);
        pio_sm_restart(control_pio,
                       phys->flight_resources.tx_sync_out_sm);
    }
    pio_sm_clear_fifos(tdma_pio_spi_phys_evidence_pio(phys), latch_sm);
    pio_sm_restart(tdma_pio_spi_phys_evidence_pio(phys), latch_sm);
}

/* Flight resource claim/release is owned by tdma_pio_spi_phys_persona.c. */

void tdma_pio_spi_phys_enable_sm_pair(tdma_pio_spi_phys_t *phys)
{
    const bool has_rtt_sm = phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN;
    pio_enable_sm_mask_in_sync(tdma_pio_spi_phys_control_pio(phys),
                               1u << tdma_pio_spi_phys_control_sm(phys));
    pio_enable_sm_mask_in_sync(tdma_pio_spi_phys_data_pio(phys),
                               1u << tdma_pio_spi_phys_data_sm(phys));
    pio_enable_sm_mask_in_sync(tdma_pio_spi_phys_capture_pio(phys),
                               1u << tdma_pio_spi_phys_capture_sm(phys));
    pio_enable_sm_mask_in_sync(tdma_pio_spi_phys_evidence_pio(phys),
                               1u << (phys->role == TDMA_PIO_SPI_ROLE_MASTER
                                          ? phys->flight_resources.tx_data_in_forward_sm
                                          : phys->flight_resources.rx_evidence_in_sm));
    if (has_rtt_sm) {
        pio_enable_sm_mask_in_sync(tdma_pio_spi_phys_control_pio(phys),
                                   1u << phys->flight_resources.tx_sync_out_sm);
    }
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
    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint rtt_sm = phys->flight_resources.tx_sync_out_sm;
    const uint latch_sm = tdma_pio_spi_phys_latch_sm(phys);
    pio_sm_set_enabled(control_pio, tdma_pio_spi_phys_control_sm(phys), false);
    pio_sm_set_enabled(data_pio, tdma_pio_spi_phys_data_sm(phys), false);
    pio_sm_set_enabled(evidence_pio, rtt_sm, false);
    pio_sm_clear_fifos(control_pio, tdma_pio_spi_phys_control_sm(phys));
    pio_sm_clear_fifos(data_pio, tdma_pio_spi_phys_data_sm(phys));
    pio_sm_clear_fifos(evidence_pio, rtt_sm);
    pio_sm_clear_fifos(evidence_pio, latch_sm);
    pio_sm_restart(control_pio, tdma_pio_spi_phys_control_sm(phys));
    pio_sm_restart(data_pio, tdma_pio_spi_phys_data_sm(phys));
    pio_sm_restart(evidence_pio, rtt_sm);
    /* Recovery preserves the flight contract: core1 never drives a control
     * edge. Restore {CS=1,SCK=0} through the origin control SM before it is
     * re-enabled at its blocking PULL. */
    pio_sm_set_pins_with_mask64(
        control_pio,
        tdma_pio_spi_phys_control_sm(phys),
        1ull << phys->tx_csn_pin,
        (1ull << phys->tx_sck_pin) | (1ull << phys->tx_csn_pin));
    pio_interrupt_clear(control_pio, 1u);
    control_pio->fdebug =
        tdma_pio_spi_phys_txstall_mask(tdma_pio_spi_phys_control_sm(phys));
    pio_enable_sm_mask_in_sync(control_pio,
                               1u << tdma_pio_spi_phys_control_sm(phys));
    pio_enable_sm_mask_in_sync(data_pio,
                               1u << tdma_pio_spi_phys_data_sm(phys));
    pio_enable_sm_mask_in_sync(evidence_pio, 1u << rtt_sm);
    /* Recovery must restore the independent edge-latch SM as well; leaving
     * it disabled would turn the next valid frame into a false timestamp
     * miss and silently remove hardware evidence from the DPLL path. */
    (void)tdma_pio_spi_phys_clock_latch_rearm(phys);
    phys->snapshot.origin_recovery_count++;
}

bool tdma_pio_spi_phys_configure_flight(
    tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config)
{
    if (phys == NULL || config == NULL) {
        return false;
    }
    phys->tx_pin = BOARD_TDMA_SPI_DOWNLINK_TX_PIN;
    phys->tx_sck_pin = BOARD_TDMA_SPI_DOWNLINK_SCK_PIN;
    phys->tx_csn_pin = BOARD_TDMA_SPI_DOWNLINK_CSN_PIN;
    phys->rx_pin = BOARD_TDMA_SPI_UPLINK_RX_PIN;
    phys->rx_sck_pin = BOARD_TDMA_SPI_UPLINK_SCK_PIN;
    phys->rx_csn_pin = BOARD_TDMA_SPI_UPLINK_CSN_PIN;
    phys->flight_resources = tdma_state_machine_resource_contract();

    if (phys->role == TDMA_PIO_SPI_ROLE_MASTER) {
        /* TX PIO owns generated CLK/SYNC and the independent returned-DATA
         * capture SM. RX PIO owns the reverse DATA output and observes
         * incoming CLK/SYNC through the data persona. */
        phys->rx_sm_pio = phys->flight_resources.tx_pio;
        phys->tx_sm_pio = phys->flight_resources.rx_pio;
        phys->evidence_pio = phys->flight_resources.tx_pio;
        phys->rx_sm = phys->flight_resources.tx_clk_out_sm;
        phys->tx_sm = phys->flight_resources.rx_data_out_sm;
        tdma_pio_spi_flight_origin_clock_rx_program_init(
            phys->rx_sm_pio,
            phys->rx_sm,
            s_tdma_pio_spi_flight_origin_clock_offset,
            phys->flight_resources.tx_data_in_pin,
            phys->tx_sck_pin,
            phys->tx_csn_pin,
            phys->baud_hz);
        tdma_pio_spi_flight_origin_data_tx_program_init(
            phys->tx_sm_pio,
            phys->tx_sm,
            s_tdma_pio_spi_flight_origin_data_offset,
            phys->flight_resources.rx_data_out_pin,
            phys->flight_resources.tx_data_in_pin,
            phys->rx_csn_pin,
            phys->rx_sck_pin,
            phys->flight_sck_phase_delay_cycles,
            phys->flight_data_phase_delay_cycles);
        tdma_pio_spi_flight_origin_rtt_program_init(
            phys->evidence_pio,
            phys->flight_resources.tx_sync_out_sm,
            s_tdma_pio_spi_flight_origin_rtt_offset,
            phys->tx_csn_pin,
            phys->rx_csn_pin);
        if (!tdma_pio_spi_phys_ensure_tx_dma()) {
            return false;
        }
    } else {
        /* TX PIO regenerates the complete forward CLK/SYNC pair. RX PIO
         * receives reverse DATA and drives the upstream DATA output. */
        phys->tx_sm_pio = phys->flight_resources.tx_pio;
        phys->rx_sm_pio = phys->flight_resources.rx_pio;
        phys->evidence_pio = phys->flight_resources.rx_pio;
        phys->tx_sm = phys->flight_resources.tx_clk_out_sm;
        phys->rx_sm = phys->flight_resources.rx_data_out_sm;
        if (phys->process_image_enabled) {
            tdma_pio_spi_flight_process_follower_program_init(
                phys->rx_sm_pio,
                phys->rx_sm,
                s_tdma_pio_spi_flight_process_follower_offset,
                phys->flight_resources.tx_data_in_pin,
                phys->flight_resources.rx_data_out_pin,
                phys->rx_csn_pin,
                phys->rx_sck_pin,
                phys->flight_sck_phase_delay_cycles,
                phys->flight_data_phase_delay_cycles);
            if (!tdma_pio_spi_phys_ensure_tx_dma()) {
                return false;
            }
        } else {
            tdma_pio_spi_flight_data_follower_program_init(
                phys->rx_sm_pio,
                phys->rx_sm,
                s_tdma_pio_spi_flight_data_follower_offset,
                phys->flight_resources.tx_data_in_pin,
                phys->flight_resources.rx_data_out_pin,
                phys->rx_sck_pin,
                phys->flight_data_phase_delay_cycles);
        }
        tdma_pio_spi_flight_control_forward_program_init(
            phys->tx_sm_pio,
            phys->tx_sm,
            s_tdma_pio_spi_flight_control_forward_offset,
            phys->rx_csn_pin,
            phys->rx_sck_pin,
            phys->tx_sck_pin,
            phys->tx_csn_pin,
            phys->flight_marker_phase_delay_cycles,
            phys->flight_sck_phase_delay_cycles);
    }
    tdma_pio_spi_flight_data_capture_program_init(
        tdma_pio_spi_phys_capture_pio(phys),
        tdma_pio_spi_phys_capture_sm(phys),
        s_tdma_pio_spi_flight_data_capture_offset,
        phys->flight_resources.tx_data_in_pin,
        phys->flight_resources.rx_sync_in_pin,
        phys->flight_resources.rx_clk_in_pin);
    /* The latch follows the edge local to each persona: TX PIO for origin,
     * RX PIO for follower.  It uses the dedicated evidence SM rather than a
     * business FIFO. */
    const PIO latch_pio = phys->evidence_pio;
    const uint latch_sm = phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? phys->flight_resources.tx_data_in_forward_sm
        : phys->flight_resources.rx_evidence_in_sm;
    tdma_pio_spi_flight_clock_latch_program_init(
        latch_pio,
        latch_sm,
        s_tdma_pio_spi_flight_clock_latch_offset,
        phys->role == TDMA_PIO_SPI_ROLE_MASTER
            ? phys->tx_csn_pin
            : phys->rx_csn_pin);
    tdma_pio_spi_phys_prepare_sm_pair(phys);
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
        phys->process_image_enabled) {
        /* Initialize the elastic tail outside the wire loop, leaving the PIO
         * instruction budget to the independent control and DATA paths. */
        pio_sm_exec(phys->rx_sm_pio, phys->rx_sm,
                    pio_encode_set(pio_y, 0u));
    }
    return true;
}


