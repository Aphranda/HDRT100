#include "tdma_pio_spi_phys_internal.h"

#include <string.h>

#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "tdma_pio_spi.pio.h"

/* P3 is a bounded diagnostic transaction. The PIO programs and DMA transfer
 * run independently; this module only owns their lifecycle and post-capture
 * state publication. */

static void tdma_pio_spi_phys_p3_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->p3_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_p3_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->p3_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_p3_set_drivers(uint32_t role)
{
    gpio_put(BOARD_UP_BISS_DE_PIN,
             role == TDMA_PIO_SPI_P3_ROLE_RESPONDER);
    gpio_put(BOARD_DN_BISS_DE_PIN,
             role == TDMA_PIO_SPI_P3_ROLE_INITIATOR);
    gpio_put(BOARD_TRIG_DE_PIN,
             role == TDMA_PIO_SPI_P3_ROLE_INITIATOR);
}

static uint32_t tdma_pio_spi_phys_p3_half_period_ns(uint32_t baud_hz)
{
    const uint64_t sys_hz = clock_get_hz(clk_sys);
    const uint64_t divider_fixed =
        (sys_hz * 256ull + (uint64_t)baud_hz * 2ull) /
        ((uint64_t)baud_hz * 4ull);
    return (uint32_t)((2ull * divider_fixed * 1000000000ull +
                       sys_hz * 128ull) / (sys_hz * 256ull));
}

bool tdma_pio_spi_phys_p3_start(
    tdma_pio_spi_phys_t *phys, const tdma_pio_spi_p3_request_t *request)
{
    if (phys == NULL || request == NULL ||
        (request->role != TDMA_PIO_SPI_P3_ROLE_INITIATOR &&
         request->role != TDMA_PIO_SPI_P3_ROLE_RESPONDER) ||
        request->signal_group > TDMA_PIO_SPI_P3_GROUP_CS_DATA ||
        (request->baud_hz != 10000000u &&
         request->baud_hz != 25000000u &&
         request->baud_hz != 30000000u) ||
        request->pulse_count < 4u || request->pulse_count > 1024u ||
        request->capture_words == 0u ||
        request->capture_words > TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS ||
        phys->p3.state == TDMA_PIO_SPI_P3_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING) {
        return false;
    }
    const tdma_pio_spi_program_persona_t persona =
        request->role == TDMA_PIO_SPI_P3_ROLE_INITIATOR
            ? (request->signal_group == TDMA_PIO_SPI_P3_GROUP_CS_DATA
                   ? TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR
                   : TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR)
            : (request->signal_group == TDMA_PIO_SPI_P3_GROUP_CS_DATA
                   ? TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER
                   : TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER);
    if (!tdma_pio_spi_phys_select_program_persona(phys, persona) ||
        !tdma_pio_spi_phys_ensure_rx_dma()) {
        return false;
    }
    const uint tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    const uint capture_sm = BOARD_TDMA_SPI_SLAVE_SM;
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, tx_sm, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, capture_sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, capture_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, capture_sm);
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 1u);

    if (request->role == TDMA_PIO_SPI_P3_ROLE_INITIATOR) {
        const bool forward_is_cs = request->signal_group ==
            TDMA_PIO_SPI_P3_GROUP_CS_DATA;
        const uint32_t sync_tx_pin = forward_is_cs
            ? BOARD_TDMA_SPI_DOWNLINK_SCK_PIN
            : BOARD_TDMA_SPI_DOWNLINK_CSN_PIN;
        const uint32_t forward_tx_pin = forward_is_cs
            ? BOARD_TDMA_SPI_DOWNLINK_CSN_PIN
            : BOARD_TDMA_SPI_DOWNLINK_SCK_PIN;
        tdma_pio_spi_p3_initiator_program_init(
            BOARD_TDMA_SPI_PIO, tx_sm, s_tdma_pio_spi_p3_initiator_offset,
            sync_tx_pin, forward_tx_pin, request->baud_hz);
        tdma_pio_spi_cal_loopback_capture_program_init(
            BOARD_TDMA_SPI_PIO, capture_sm,
            s_tdma_pio_spi_p3_capture_offset, 250000000u);
        pio_sm_put(BOARD_TDMA_SPI_PIO, tx_sm,
                   request->pulse_count - 1u);
    } else {
        const bool forward_is_cs = request->signal_group ==
            TDMA_PIO_SPI_P3_GROUP_CS_DATA;
        const uint32_t sync_rx_pin = forward_is_cs
            ? BOARD_TDMA_SPI_UPLINK_SCK_PIN
            : BOARD_TDMA_SPI_UPLINK_CSN_PIN;
        const uint32_t forward_rx_pin = forward_is_cs
            ? BOARD_TDMA_SPI_UPLINK_CSN_PIN
            : BOARD_TDMA_SPI_UPLINK_SCK_PIN;
        tdma_pio_spi_p3_responder_program_init(
            BOARD_TDMA_SPI_PIO, tx_sm, s_tdma_pio_spi_p3_responder_offset,
            sync_rx_pin, forward_rx_pin,
            BOARD_TDMA_SPI_DOWNLINK_TX_PIN, request->baud_hz);
        tdma_pio_spi_p3_responder_capture_program_init(
            BOARD_TDMA_SPI_PIO, capture_sm,
            s_tdma_pio_spi_p3_responder_capture_offset,
            sync_rx_pin);
        pio_sm_put(BOARD_TDMA_SPI_PIO, tx_sm,
                   request->pulse_count - 1u);
    }

    memset(s_tdma_pio_spi_cal_ring, 0, sizeof(s_tdma_pio_spi_cal_ring));
    dma_channel_config dc = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(
        &dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, capture_sm, false));
    dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &dc,
                          s_tdma_pio_spi_cal_ring,
                          &BOARD_TDMA_SPI_PIO->rxf[capture_sm],
                          request->capture_words, false);

    const uint32_t half_ns =
        tdma_pio_spi_phys_p3_half_period_ns(request->baud_hz);
    tdma_pio_spi_phys_p3_write_begin(phys);
    memset(&phys->p3, 0, sizeof(phys->p3));
    phys->p3.state = TDMA_PIO_SPI_P3_ARMED;
    phys->p3.role = request->role;
    phys->p3.signal_group = request->signal_group;
    phys->p3.flags = TDMA_PIO_SPI_P3_FLAG_DIAGNOSTIC_ONLY;
    phys->p3.baud_hz = request->baud_hz;
    phys->p3.epoch = request->epoch;
    phys->p3.sample_period_ns = 4u;
    phys->p3.pulse_count = request->pulse_count;
    phys->p3.requested_words = request->capture_words;
    phys->p3.clock_high_ns = half_ns;
    phys->p3.clock_low_ns = half_ns;
    phys->p3.data_high_ns =
        tdma_pio_spi_p3_data_high_cycles(request->baud_hz) * 4u;
    tdma_pio_spi_phys_p3_write_end(phys);
    phys->armed = true;
    phys->tx_sm = tx_sm;
    phys->rx_sm = capture_sm;
    tdma_pio_spi_phys_p3_set_drivers(request->role);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                               (1u << tx_sm) | (1u << capture_sm));
    return true;
}

void tdma_pio_spi_phys_p3_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    tdma_pio_spi_phys_cal_cleanup(phys);
    tdma_pio_spi_phys_p3_write_begin(phys);
    phys->p3.state = TDMA_PIO_SPI_P3_IDLE;
    tdma_pio_spi_phys_p3_write_end(phys);
    (void)tdma_pio_spi_phys_select_program_persona(
        phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

void tdma_pio_spi_phys_p3_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || phys->p3.state != TDMA_PIO_SPI_P3_ARMED ||
        s_tdma_pio_spi_rx_dma_channel < 0) return;
    const uint32_t remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
    if (remaining != 0u) return;
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
    tdma_pio_spi_phys_p3_write_begin(phys);
    phys->p3.produced_words = phys->p3.requested_words;
    phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_DMA_COMPLETE |
                      TDMA_PIO_SPI_P3_FLAG_HARDWARE_LATCHED |
                      TDMA_PIO_SPI_P3_FLAG_SYNC_MATCH;
    tdma_pio_spi_phys_p3_decode(phys);
    const uint32_t expected = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR
            ? 0x09u
            : 0x06u;
    if ((phys->p3.edge_mask & expected) == expected) {
        phys->p3.state = TDMA_PIO_SPI_P3_COMPLETE;
    } else {
        phys->p3.state = TDMA_PIO_SPI_P3_ERROR;
        phys->p3.reject_reason = 1u;
    }
    tdma_pio_spi_phys_p3_write_end(phys);
    tdma_pio_spi_phys_cal_cleanup(phys);
    (void)tdma_pio_spi_phys_select_program_persona(
        phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

bool tdma_pio_spi_phys_get_p3_snapshot(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_p3_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->p3;
        const uint32_t end =
            __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}
