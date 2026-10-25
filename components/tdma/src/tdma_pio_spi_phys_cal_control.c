#include "tdma_pio_spi_phys.h"
#include "tdma_service_timing.h"
#include "tdma_pio_spi_phys_programs.h"
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

#define TDMA_PIO_SPI_DEFAULT_BAUD_HZ 1000000u

/* Fixed frame window: 4-byte packet header + 32-byte TdmaTransportFrame idle
 * beacon. Every ring frame is exactly this long. */
#define TDMA_PIO_SPI_FIXED_RX_WORDS \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE)

/* Bounded TX FIFO wait: core1 must stay predictable (HAOFV). If the downlink
 * SM stops consuming (e.g. the config plane disarms it while core1 is mid
 * frame), put_blocking would hang core1 forever and break the flash lockout
 * protocol. Instead we wait at most this long per word and fail the frame. */
#define TDMA_PIO_SPI_TX_PUT_TIMEOUT_US 500u

/* CS is now the point-to-point frame-sync. After the fixed frame DMA completes,
 * the sender can still hold CS low while the final shifted byte drains. Wait
 * for CS idle before clearing FIFO/re-arming DMA, otherwise the RX SM can treat
 * the low tail as the beginning of the next capture window. */
#define TDMA_PIO_SPI_RX_CSN_IDLE_WAIT_US 500u

/* The flight process image makes a short ring packet almost 300 words. Keep
 * three complete maximum packets so a poll that observes an incomplete tail
 * can safely wait through the next core1 service without DMA overwrite. */

#include "tdma_pio_spi_phys_internal.h"

void tdma_pio_spi_phys_cal_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->cal_loopback_guard, 1u, __ATOMIC_RELEASE);
}
void tdma_pio_spi_phys_cal_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->cal_loopback_guard, 1u, __ATOMIC_RELEASE);
}

void tdma_pio_spi_phys_cal_reject(tdma_pio_spi_phys_t *phys,
                                         uint32_t epoch,
                                         uint32_t reason)
{
    tdma_pio_spi_phys_cal_write_begin(phys);
    memset(&phys->cal_loopback, 0, sizeof(phys->cal_loopback));
    phys->cal_loopback.complete = 1u;
    phys->cal_loopback.reject_reason = reason;
    phys->cal_loopback.epoch = epoch;
    tdma_pio_spi_phys_cal_write_end(phys);
}

bool tdma_pio_spi_phys_cal_loopback_start(tdma_pio_spi_phys_t *phys,
                                          uint32_t sample_hz,
                                          uint32_t sample_words,
                                          uint32_t epoch)
{
    const bool persona_supported =
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE ||
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL ||
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN ||
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER ||
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER ||
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE;
    if (phys == NULL || sample_words == 0u ||
        sample_words > TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS ||
        !persona_supported ||
        phys->cal_loopback_start_pending ||
        phys->cal_loopback_transition !=
            TDMA_PIO_SPI_CAL_TRANSITION_IDLE ||
        phys->cal_loopback.armed != 0u ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING) {
        return false;
    }
    if (!tdma_pio_spi_phys_leave_flight_for_maintenance(phys)) return false;
    phys->cal_loopback_sample_hz = sample_hz == 0u
        ? TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_HZ : sample_hz;
    phys->cal_loopback_sample_words = sample_words;
    phys->cal_loopback_epoch = epoch;
    if (!phys->armed) {
        phys->role = TDMA_PIO_SPI_ROLE_MASTER;
        phys->baud_hz = BOARD_TDMA_SPI_BAUD_HZ;
        phys->tx_sm = BOARD_TDMA_SPI_MASTER_SM;
        phys->rx_sm = BOARD_TDMA_SPI_SLAVE_SM;
        phys->tx_sck_pin = BOARD_TDMA_SPI_DOWNLINK_SCK_PIN;
        phys->tx_csn_pin = BOARD_TDMA_SPI_DOWNLINK_CSN_PIN;
        phys->tx_pin = BOARD_TDMA_SPI_DOWNLINK_TX_PIN;
        phys->rx_sck_pin = BOARD_TDMA_SPI_UPLINK_SCK_PIN;
        phys->rx_csn_pin = BOARD_TDMA_SPI_UPLINK_CSN_PIN;
        phys->rx_pin = BOARD_TDMA_SPI_UPLINK_RX_PIN;
        phys->armed = true;
        tdma_pio_spi_phys_set_line_drivers(true);
    }
    phys->cal_loopback_start_pending = true;
    phys->cal_loopback_stop_pending = false;
    phys->cal_loopback_program_step = 0u;
    phys->cal_loopback_program_count = 0u;
    phys->cal_loopback_transition =
        TDMA_PIO_SPI_CAL_TRANSITION_START_UNLOAD;
    return true;
}

void tdma_pio_spi_phys_cal_loopback_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys != NULL) {
        if (phys->cal_loopback_transition ==
                TDMA_PIO_SPI_CAL_TRANSITION_IDLE &&
            phys->cal_loopback.armed == 0u &&
            s_tdma_pio_spi_program_persona !=
                TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE) {
            phys->cal_loopback_start_pending = false;
            phys->cal_loopback_stop_pending = false;
            return;
        }
        phys->cal_loopback_start_pending = false;
        phys->cal_loopback_stop_pending = true;
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_STOP_FREEZE;
    }
}
