#include "tdma_pio_spi_phys.h"

#include <string.h>

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "tdma_pio_spi.pio.h"
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
_Static_assert(TDMA_PIO_SPI_RX_RING_WORDS >=
                   3u * TDMA_PIO_SPI_RX_DMA_WORD_MAX,
               "TDMA SPI RX ring must hold three maximum short packets");

static bool s_tdma_pio_spi_sms_claimed;
static tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
static uint s_tdma_pio_spi_tx_offset;
static uint s_tdma_pio_spi_rx_offset;
static uint s_tdma_pio_spi_clk_forward_offset;
static uint s_tdma_pio_spi_clk_burst_offset;
static uint s_tdma_pio_spi_clk_capture_offset;
static uint s_tdma_pio_spi_clk_coded_tx_offset;
static uint s_tdma_pio_spi_clk_oversample_offset;
static uint s_tdma_pio_spi_cal_tx_offset;
static uint s_tdma_pio_spi_cal_capture_offset;
static uint s_tdma_pio_spi_p3_initiator_offset;
static uint s_tdma_pio_spi_p3_responder_offset;
static uint s_tdma_pio_spi_p3_capture_offset;
static uint s_tdma_pio_spi_p3_responder_capture_offset;
static uint32_t s_tdma_pio_spi_cal_ring[TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS]
    __attribute__((aligned(4)));
static uint32_t s_tdma_pio_spi_coded_tx[TDMA_PIO_SPI_CODED_BUFFER_WORDS]
    __attribute__((aligned(4)));
static uint32_t s_tdma_pio_spi_coded_rx[TDMA_PIO_SPI_CODED_BUFFER_WORDS]
    __attribute__((aligned(4)));
static void tdma_pio_spi_phys_cal_decode(tdma_pio_spi_phys_t *phys);
static int s_tdma_pio_spi_tx_dma_channel = -1;
static int s_tdma_pio_spi_rx_dma_channel = -1;
static uint32_t s_tdma_pio_spi_rx_ring[TDMA_PIO_SPI_RX_RING_WORDS]
    __attribute__((aligned(TDMA_PIO_SPI_RX_RING_WORDS * sizeof(uint32_t))));
static uint32_t s_tdma_pio_spi_rx_scan_produced;
static uint32_t s_tdma_pio_spi_rx_produced_seq;
static uint32_t s_tdma_pio_spi_rx_last_write_index;
static bool s_tdma_pio_spi_rx_write_index_valid;
/* Assembled frame (magic-aligned) copied out of the continuous DMA ring. */
static uint32_t s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX];

static void tdma_pio_spi_phys_clk_train_write_begin(
    tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->clk_train_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_clk_train_write_end(
    tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->clk_train_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_clk_train_reset(tdma_pio_spi_phys_t *phys)
{
    const uint32_t request_seq = phys->clk_train.request_seq;
    tdma_pio_spi_phys_clk_train_write_begin(phys);
    memset(&phys->clk_train, 0, sizeof(phys->clk_train));
    phys->clk_train.version = TDMA_PIO_SPI_CLK_TRAIN_SNAPSHOT_VERSION;
    phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_IDLE;
    phys->clk_train.result = TDMA_PIO_SPI_CLK_TRAIN_RESULT_NONE;
    phys->clk_train.role = phys->role;
    phys->clk_train.request_seq = request_seq;
    phys->clk_train.baud_hz = phys->baud_hz;
    phys->clk_train.tx_sck_pin = phys->tx_sck_pin;
    phys->clk_train.rx_sck_pin = phys->rx_sck_pin;
    phys->clk_train.timestamp_resolution_ns =
        vdc_timestamp_clock_resolution_ns();
    phys->clk_train.timestamp_flags =
        TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    phys->clk_train_return_deadline_ns = 0ull;
    tdma_pio_spi_phys_clk_train_write_end(phys);
}

static void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *phys,
                                        uint32_t error)
{
    if (phys == NULL) {
        return;
    }
    phys->snapshot.last_error = error;
    if (error == TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET ||
        error == TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE) {
        phys->snapshot.rx_bad_count++;
    }
}

static bool tdma_pio_spi_phys_ensure_sms_claimed(void)
{
    if (s_tdma_pio_spi_sms_claimed) return true;
    if (pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM)) {
        return false;
    }
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    s_tdma_pio_spi_sms_claimed = true;
    return true;
}

static bool tdma_pio_spi_phys_load_normal_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_tx_byte_program)) return false;
    s_tdma_pio_spi_tx_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_tx_byte_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_rx_byte_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_tx_byte_program,
                           s_tdma_pio_spi_tx_offset);
        return false;
    }
    s_tdma_pio_spi_rx_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_rx_byte_program);
    return true;
}

static bool tdma_pio_spi_phys_load_coarse_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_forward_program)) return false;
    s_tdma_pio_spi_clk_forward_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_clk_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_burst_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        return false;
    }
    s_tdma_pio_spi_clk_burst_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_clk_burst_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_burst_program,
                           s_tdma_pio_spi_clk_burst_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        return false;
    }
    s_tdma_pio_spi_clk_capture_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_clk_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_cal_loopback_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_tx_program)) return false;
    s_tdma_pio_spi_cal_tx_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_tx_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_tx_program,
                           s_tdma_pio_spi_cal_tx_offset);
        return false;
    }
    s_tdma_pio_spi_cal_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_coded_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_forward_program)) return false;
    s_tdma_pio_spi_clk_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_coded_tx_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        return false;
    }
    s_tdma_pio_spi_clk_coded_tx_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_coded_tx_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_oversample_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_coded_tx_program,
                           s_tdma_pio_spi_clk_coded_tx_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        return false;
    }
    s_tdma_pio_spi_clk_oversample_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_oversample_program);
    return true;
}

static bool tdma_pio_spi_phys_load_p3_initiator_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_initiator_program)) return false;
    s_tdma_pio_spi_p3_initiator_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_initiator_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        return false;
    }
    s_tdma_pio_spi_p3_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_p3_responder_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_responder_program)) return false;
    s_tdma_pio_spi_p3_responder_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_responder_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_responder_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        return false;
    }
    s_tdma_pio_spi_p3_responder_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_responder_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_programs(
    tdma_pio_spi_program_persona_t persona)
{
    switch (persona) {
    case TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL:
        return tdma_pio_spi_phys_load_normal_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE:
        return tdma_pio_spi_phys_load_coarse_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK:
        return tdma_pio_spi_phys_load_cal_loopback_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED:
        return tdma_pio_spi_phys_load_coded_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR:
        return tdma_pio_spi_phys_load_p3_initiator_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER:
        return tdma_pio_spi_phys_load_p3_responder_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR:
        return tdma_pio_spi_phys_load_p3_initiator_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER:
        return tdma_pio_spi_phys_load_p3_responder_programs();
    default:
        return false;
    }
}

static void tdma_pio_spi_phys_unload_programs(void)
{
    switch (s_tdma_pio_spi_program_persona) {
    case TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_rx_byte_program,
                           s_tdma_pio_spi_rx_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_tx_byte_program,
                           s_tdma_pio_spi_tx_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_capture_program,
                           s_tdma_pio_spi_clk_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_burst_program,
                           s_tdma_pio_spi_clk_burst_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_cal_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_tx_program,
                           s_tdma_pio_spi_cal_tx_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_oversample_program,
                           s_tdma_pio_spi_clk_oversample_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_coded_tx_program,
                           s_tdma_pio_spi_clk_coded_tx_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_p3_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_capture_program,
                           s_tdma_pio_spi_p3_responder_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_p3_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_capture_program,
                           s_tdma_pio_spi_p3_responder_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        break;
    default:
        break;
    }
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
}

bool tdma_pio_spi_phys_select_program_persona(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona)
{
    if (phys == NULL || persona <= TDMA_PIO_SPI_PROGRAM_PERSONA_NONE ||
        persona > TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER ||
        !tdma_pio_spi_phys_ensure_sms_claimed()) {
        return false;
    }
    const uint32_t sm_mask = (1u << BOARD_TDMA_SPI_MASTER_SM) |
                             (1u << BOARD_TDMA_SPI_SLAVE_SM);
    if ((BOARD_TDMA_SPI_PIO->ctrl & sm_mask) != 0u ||
        (s_tdma_pio_spi_tx_dma_channel >= 0 &&
         dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) ||
        (s_tdma_pio_spi_rx_dma_channel >= 0 &&
         dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel))) {
        phys->snapshot.program_switch_fail_count++;
        return false;
    }
    if (s_tdma_pio_spi_program_persona == persona) {
        phys->snapshot.program_persona = (uint32_t)persona;
        return true;
    }
    const tdma_pio_spi_program_persona_t previous =
        s_tdma_pio_spi_program_persona;
    tdma_pio_spi_phys_unload_programs();
    if (!tdma_pio_spi_phys_load_programs(persona)) {
        phys->snapshot.program_switch_fail_count++;
        if (previous != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE &&
            tdma_pio_spi_phys_load_programs(previous)) {
            s_tdma_pio_spi_program_persona = previous;
        }
        phys->snapshot.program_persona =
            (uint32_t)s_tdma_pio_spi_program_persona;
        return false;
    }
    s_tdma_pio_spi_program_persona = persona;
    phys->snapshot.program_persona = (uint32_t)persona;
    phys->snapshot.program_switch_count++;
    return true;
}

static void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys)
{
    phys->snapshot.armed = phys->armed ? 1u : 0u;
    phys->snapshot.role = phys->role;
    phys->snapshot.baud_hz = phys->baud_hz;
    phys->snapshot.tx_sck_pin = phys->tx_sck_pin;
    phys->snapshot.tx_csn_pin = phys->tx_csn_pin;
    phys->snapshot.tx_pin = phys->tx_pin;
    phys->snapshot.rx_sck_pin = phys->rx_sck_pin;
    phys->snapshot.rx_csn_pin = phys->rx_csn_pin;
    phys->snapshot.rx_pin = phys->rx_pin;
    phys->snapshot.program_persona =
        (uint32_t)s_tdma_pio_spi_program_persona;
}

static uint64_t tdma_pio_spi_phys_now_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

static uint32_t tdma_pio_spi_phys_frame_tail_us(const tdma_pio_spi_phys_t *phys,
                                                size_t packet_size)
{
    const uint32_t baud_hz =
        (phys != NULL && phys->baud_hz != 0u) ? phys->baud_hz : 1000000u;
    const size_t packet_words = packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    /* After the last pio_sm_put(), up to the joined TX FIFO depth plus the
     * current OSR can still be on the wire. Keep CS active for that tail. */
    const size_t tail_words = packet_words < 10u ? packet_words : 10u;
    const uint64_t bit_us =
        ((uint64_t)tail_words * 8ull * 1000000ull + baud_hz - 1ull) /
        baud_hz;
    return (uint32_t)bit_us + 10u;
}

static uint64_t tdma_pio_spi_phys_wire_time_ns(const tdma_pio_spi_phys_t *phys,
                                               size_t packet_size)
{
    const uint32_t baud_hz =
        (phys != NULL && phys->baud_hz != 0u) ? phys->baud_hz : 1000000u;
    const uint64_t bits =
        (uint64_t)(packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE) * 8ull;
    return (bits * 1000000000ull + baud_hz - 1ull) / baud_hz;
}

static bool tdma_pio_spi_phys_ensure_rx_dma(void)
{
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        return true;
    }
    if (dma_channel_is_claimed(TDMA_PIO_SPI_RX_DMA_CHANNEL)) {
        return false;
    }
    dma_channel_claim(TDMA_PIO_SPI_RX_DMA_CHANNEL);
    s_tdma_pio_spi_rx_dma_channel = (int)TDMA_PIO_SPI_RX_DMA_CHANNEL;
    return true;
}

static bool tdma_pio_spi_phys_ensure_tx_dma(void)
{
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        return true;
    }
    if (dma_channel_is_claimed(TDMA_PIO_SPI_TX_DMA_CHANNEL)) {
        return false;
    }
    dma_channel_claim(TDMA_PIO_SPI_TX_DMA_CHANNEL);
    s_tdma_pio_spi_tx_dma_channel = (int)TDMA_PIO_SPI_TX_DMA_CHANNEL;
    return true;
}

static void tdma_pio_spi_phys_rx_prepare(tdma_pio_spi_phys_t *phys)
{
    /* The SM must keep running across frame boundaries. Resetting it here
     * would make the next capture depend on the CPU/service phase. */
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
}

/* Product-board SPI persona. Pin direction and PIO ownership are frozen in
 * board_config.h; CS remains the point-to-point frame-sync signal. */
static void tdma_pio_spi_phys_configure(tdma_pio_spi_phys_t *phys)
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

static void tdma_pio_spi_phys_set_line_drivers(bool enabled)
{
    /* DATA0, CLK1 and TRIG/CS have independent ISO1452 drivers. */
    gpio_put(BOARD_UP_BISS_DE_PIN, enabled);
    gpio_put(BOARD_DN_BISS_DE_PIN, enabled);
    gpio_put(BOARD_TRIG_DE_PIN, enabled);
}

static void tdma_pio_spi_phys_cal_cleanup(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return;
    }
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
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

static bool tdma_pio_spi_phys_rx_arm(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || !tdma_pio_spi_phys_ensure_rx_dma()) {
        return false;
    }
    dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    tdma_pio_spi_phys_rx_prepare(phys);
    memset(s_tdma_pio_spi_rx_ring, 0, sizeof(s_tdma_pio_spi_rx_ring));
    s_tdma_pio_spi_rx_scan_produced = 0u;
    s_tdma_pio_spi_rx_produced_seq = 0u;
    s_tdma_pio_spi_rx_last_write_index = 0u;
    s_tdma_pio_spi_rx_write_index_valid = false;
    dma_channel_config dma_cfg =
        dma_channel_get_default_config((uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_ring(&dma_cfg, true, TDMA_PIO_SPI_RX_RING_LOG2);
    channel_config_set_dreq(&dma_cfg,
                            pio_get_dreq(BOARD_TDMA_SPI_PIO,
                                         phys->rx_sm,
                                         false));
    dma_channel_configure(
        (uint)s_tdma_pio_spi_rx_dma_channel,
        &dma_cfg,
        s_tdma_pio_spi_rx_ring,
        &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
        UINT32_MAX,
        false);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    phys->rx_capture_active = true;
    return true;
}

static uint32_t tdma_pio_spi_phys_rx_write_index(void)
{
    const uintptr_t ring_base = (uintptr_t)s_tdma_pio_spi_rx_ring;
    const uintptr_t write_addr =
        (uintptr_t)dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].write_addr;
    return (uint32_t)(((write_addr - ring_base) &
                       ((TDMA_PIO_SPI_RX_RING_WORDS * sizeof(uint32_t)) -
                        1u)) /
                      sizeof(uint32_t));
}

static uint32_t tdma_pio_spi_phys_rx_produced_words(void)
{
    const uint32_t write_index = tdma_pio_spi_phys_rx_write_index();
    if (!s_tdma_pio_spi_rx_write_index_valid) {
        s_tdma_pio_spi_rx_last_write_index = write_index;
        s_tdma_pio_spi_rx_write_index_valid = true;
        return s_tdma_pio_spi_rx_produced_seq;
    }
    if (write_index != s_tdma_pio_spi_rx_last_write_index) {
        const uint32_t last = s_tdma_pio_spi_rx_last_write_index;
        const uint32_t delta =
            write_index >= last
                ? write_index - last
                : (TDMA_PIO_SPI_RX_RING_WORDS - last) + write_index;
        s_tdma_pio_spi_rx_produced_seq += delta;
        s_tdma_pio_spi_rx_last_write_index = write_index;
    }
    return s_tdma_pio_spi_rx_produced_seq;
}

static uint32_t tdma_pio_spi_phys_rx_ring_word(uint32_t produced)
{
    return s_tdma_pio_spi_rx_ring[produced &
                                  (TDMA_PIO_SPI_RX_RING_WORDS - 1u)];
}

static uint8_t tdma_pio_spi_phys_rx_ring_byte(uint32_t produced)
{
    return (uint8_t)(tdma_pio_spi_phys_rx_ring_word(produced) & 0xFFu);
}

static bool tdma_pio_spi_phys_transport_header_matches(uint32_t packet_start,
                                                       uint16_t frame_size)
{
    if (frame_size < TDMA_TRANSPORT_FRAME_HEADER_SIZE) {
        return false;
    }
    const uint16_t transport_magic =
        (uint16_t)tdma_pio_spi_phys_rx_ring_byte(packet_start) |
        ((uint16_t)tdma_pio_spi_phys_rx_ring_byte(packet_start + 1u) << 8u);
    const uint16_t transport_size =
        (uint16_t)tdma_pio_spi_phys_rx_ring_byte(packet_start + 4u) |
        ((uint16_t)tdma_pio_spi_phys_rx_ring_byte(packet_start + 5u) << 8u);
    const uint8_t frame_class =
        tdma_pio_spi_phys_rx_ring_byte(packet_start + 3u);

    return transport_magic == TDMA_TRANSPORT_FRAME_MAGIC &&
           tdma_pio_spi_phys_rx_ring_byte(packet_start + 2u) ==
               TDMA_TRANSPORT_FRAME_VERSION &&
           (frame_class == TDMA_TRANSPORT_FRAME_CLASS_SHORT ||
            frame_class == TDMA_TRANSPORT_FRAME_CLASS_LONG) &&
           transport_size == frame_size &&
           tdma_pio_spi_phys_rx_ring_byte(packet_start + 6u) ==
               TDMA_TRANSPORT_FRAME_HEADER_SIZE;
}

static bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys,
                                            size_t max_words,
                                            size_t *received_words)
{
    if (received_words != NULL) {
        *received_words = 0u;
    }
    if (phys == NULL || received_words == NULL ||
        max_words == 0u || max_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX ||
        !phys->rx_capture_active) {
        return false;
    }
    const uint32_t produced = tdma_pio_spi_phys_rx_produced_words();
    phys->snapshot.rx_dma_produced_words = produced;
    phys->snapshot.rx_scan_produced_words = s_tdma_pio_spi_rx_scan_produced;
    phys->snapshot.rx_dma_write_index = tdma_pio_spi_phys_rx_write_index();
    phys->snapshot.rx_dma_channel = (uint32_t)s_tdma_pio_spi_rx_dma_channel;
    if (produced <= s_tdma_pio_spi_rx_scan_produced) {
        return false;
    }

    if (produced - s_tdma_pio_spi_rx_scan_produced >
        TDMA_PIO_SPI_RX_RING_WORDS) {
        phys->snapshot.rx_ring_overrun_count++;
        s_tdma_pio_spi_rx_scan_produced =
            produced - TDMA_PIO_SPI_RX_RING_WORDS;
    }

    uint32_t candidate = s_tdma_pio_spi_rx_scan_produced;
    while (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE <= produced) {
        if ((tdma_pio_spi_phys_rx_ring_word(candidate) &
             0xFFu) == TDMA_PIO_SPI_PACKET_MAGIC0 &&
            (tdma_pio_spi_phys_rx_ring_word(candidate + 1u) &
             0xFFu) == TDMA_PIO_SPI_PACKET_MAGIC1) {
            const uint16_t frame_size =
                (uint16_t)(tdma_pio_spi_phys_rx_ring_word(candidate + 2u) &
                           0xFFu) |
                (uint16_t)((tdma_pio_spi_phys_rx_ring_word(candidate + 3u) &
                            0xFFu)
                           << 8u);
            const uint32_t total_words =
                TDMA_PIO_SPI_PACKET_HEADER_SIZE + frame_size;
            if (frame_size == 0u || total_words > max_words) {
                candidate++;
                continue;
            }
            if (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE +
                    TDMA_TRANSPORT_FRAME_HEADER_SIZE >
                produced) {
                s_tdma_pio_spi_rx_scan_produced = candidate;
                return false;
            }
            if (!tdma_pio_spi_phys_transport_header_matches(
                    candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE,
                    frame_size)) {
                candidate++;
                continue;
            }
            if (candidate + total_words > produced) {
                /* Header is valid, but the last bytes are still on the wire. */
                s_tdma_pio_spi_rx_scan_produced = candidate;
                return false;
            }
            for (uint32_t i = 0u; i < total_words; i++) {
                s_tdma_pio_spi_rx_frame[i] =
                    tdma_pio_spi_phys_rx_ring_word(candidate + i);
            }
            s_tdma_pio_spi_rx_scan_produced = candidate + total_words;
            phys->snapshot.rx_magic_at_zero++;
            *received_words = total_words;
            return true;
        }
        candidate++;
    }
    /* Keep one trailing word so a split magic header can be completed by the
     * next DMA sample. */
    const uint32_t bad_start = s_tdma_pio_spi_rx_scan_produced;
    const uint32_t bad_available = produced - bad_start;
    phys->snapshot.last_bad_header0 =
        bad_available > 0u ? tdma_pio_spi_phys_rx_ring_word(bad_start) : 0u;
    phys->snapshot.last_bad_header1 =
        bad_available > 1u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 1u) : 0u;
    phys->snapshot.last_bad_header2 =
        bad_available > 2u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 2u) : 0u;
    phys->snapshot.last_bad_header3 =
        bad_available > 3u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 3u) : 0u;
    phys->snapshot.last_bad_words = bad_available;
    phys->snapshot.rx_magic_fail_count++;
    s_tdma_pio_spi_rx_scan_produced = produced > 0u ? produced - 1u : 0u;
    return false;
}

bool tdma_pio_spi_phys_arm(void *context,
                           const tdma_ring_runtime_config_t *config)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || config == NULL || config->enabled == 0u ||
        config->node_count < 2u ||
        config->local_slot_id >= config->node_count ||
        config->tx_dma_channel_id != TDMA_PIO_SPI_TX_DMA_CHANNEL ||
        config->rx_dma_channel_id != TDMA_PIO_SPI_RX_DMA_CHANNEL) {
        return false;
    }
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL)) {
        return false;
    }

    phys->role = (config->local_slot_id == config->reference_slot_id)
                     ? TDMA_PIO_SPI_ROLE_MASTER
                     : TDMA_PIO_SPI_ROLE_SLAVE;
    phys->baud_hz = config->baud_hz;
    tdma_pio_spi_phys_configure(phys);
    tdma_pio_spi_phys_set_line_drivers(true);
    if (!tdma_pio_spi_phys_rx_arm(phys)) {
        tdma_pio_spi_phys_set_line_drivers(false);
        return false;
    }

    phys->armed = true;
    phys->snapshot.tx_count = 0u;
    phys->snapshot.rx_count = 0u;
    phys->snapshot.rx_bad_count = 0u;
    phys->snapshot.tx_busy_count = 0u;
    phys->snapshot.rx_partial_count = 0u;
    phys->snapshot.rx_stall_count = 0u;
    phys->snapshot.tx_timeout_count = 0u;
    phys->snapshot.rx_busy_count = 0u;
    phys->snapshot.rx_magic_fail_count = 0u;
    phys->snapshot.rx_magic_at_zero = 0u;
    phys->snapshot.rx_magic_at_shift = 0u;
    phys->snapshot.rx_ring_overrun_count = 0u;
    phys->snapshot.rx_dma_produced_words = 0u;
    phys->snapshot.rx_scan_produced_words = 0u;
    phys->snapshot.rx_dma_write_index = 0u;
    phys->snapshot.rx_dma_channel = TDMA_PIO_SPI_RX_DMA_CHANNEL;
    phys->snapshot.tx_edge_count = 0u;
    phys->snapshot.rx_edge_count = 0u;
    phys->snapshot.last_tx_edge_timestamp_ns = 0ull;
    phys->snapshot.last_tx_done_timestamp_ns = 0ull;
    phys->snapshot.last_rx_edge_timestamp_ns = 0ull;
    phys->snapshot.last_rx_extract_timestamp_ns = 0ull;
    phys->snapshot.rx_busy_word0 = 0u;
    phys->snapshot.rx_busy_word1 = 0u;
    phys->snapshot.rx_busy_word2 = 0u;
    phys->snapshot.rx_busy_word3 = 0u;
    phys->snapshot.rx_busy_moved = 0u;
    phys->snapshot.last_bad_header0 = 0u;
    phys->snapshot.last_bad_header1 = 0u;
    phys->snapshot.last_bad_header2 = 0u;
    phys->snapshot.last_bad_header3 = 0u;
    phys->snapshot.last_bad_words = 0u;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_clk_train_reset(phys);
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

void tdma_pio_spi_phys_disarm(void *context)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->armed) {
        return;
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    tdma_pio_spi_phys_set_line_drivers(false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    gpio_set_function(phys->tx_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->tx_csn_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->tx_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_csn_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_pin, GPIO_FUNC_SIO);
    gpio_set_dir(phys->tx_sck_pin, GPIO_IN);
    gpio_set_dir(phys->tx_csn_pin, GPIO_IN);
    gpio_set_dir(phys->tx_pin, GPIO_IN);
    gpio_set_dir(phys->rx_sck_pin, GPIO_IN);
    gpio_set_dir(phys->rx_csn_pin, GPIO_IN);
    gpio_set_dir(phys->rx_pin, GPIO_IN);
    phys->armed = false;
    phys->rx_capture_active = false;
    tdma_pio_spi_phys_clk_train_reset(phys);
    tdma_pio_spi_phys_fill_static_snapshot(phys);
}

static bool tdma_pio_spi_phys_tx_put(tdma_pio_spi_phys_t *phys,
                                     uint32_t word)
{
    const uint64_t deadline_us =
        tdma_pio_spi_phys_now_us() + TDMA_PIO_SPI_TX_PUT_TIMEOUT_US;
    while (pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        if (tdma_pio_spi_phys_now_us() >= deadline_us) {
            phys->snapshot.tx_timeout_count++;
            return false; /* SM stopped: do not hang core1. */
        }
    }
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm, word);
    return true;
}

bool tdma_pio_spi_phys_train_clock(void *context, uint32_t cycles)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->armed || cycles == 0u ||
        cycles > TDMA_PIO_SPI_TRAIN_CLOCK_MAX_CYCLES) {
        if (phys != NULL) {
            tdma_pio_spi_phys_set_error(phys,
                                        TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
            tdma_pio_spi_phys_clk_train_write_begin(phys);
            phys->clk_train.request_seq++;
            phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_ERROR;
            phys->clk_train.result = TDMA_PIO_SPI_CLK_TRAIN_RESULT_REJECTED;
            phys->clk_train.requested_cycles = cycles;
            tdma_pio_spi_phys_clk_train_write_end(phys);
        }
        return false;
    }

    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    phys->rx_capture_active = false;
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE)) {
        tdma_pio_spi_phys_set_error(
            phys, TDMA_PIO_SPI_PHYS_ERROR_RESOURCE_CONFLICT);
        tdma_pio_spi_phys_clk_train_write_begin(phys);
        phys->clk_train.request_seq++;
        phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_ERROR;
        phys->clk_train.result = TDMA_PIO_SPI_CLK_TRAIN_RESULT_REJECTED;
        phys->clk_train.requested_cycles = cycles;
        tdma_pio_spi_phys_clk_train_write_end(phys);
        return false;
    }
    for (uint32_t irq = 0u; irq < 4u; irq++) {
        pio_interrupt_clear(BOARD_TDMA_SPI_PIO, irq);
    }
    gpio_put(phys->tx_csn_pin, true);

    const uint32_t request_seq = phys->clk_train.request_seq + 1u;
    const uint64_t burst_duration_ns =
        ((uint64_t)cycles * 1000000000ull + phys->baud_hz - 1ull) /
        phys->baud_hz;
    tdma_pio_spi_phys_clk_train_write_begin(phys);
    memset(&phys->clk_train, 0, sizeof(phys->clk_train));
    phys->clk_train.version = TDMA_PIO_SPI_CLK_TRAIN_SNAPSHOT_VERSION;
    phys->clk_train.role = phys->role;
    phys->clk_train.request_seq = request_seq;
    phys->clk_train.baud_hz = phys->baud_hz;
    phys->clk_train.requested_cycles = cycles;
    phys->clk_train.tx_sck_pin = phys->tx_sck_pin;
    phys->clk_train.rx_sck_pin = phys->rx_sck_pin;
    phys->clk_train.timestamp_resolution_ns =
        vdc_timestamp_clock_resolution_ns();
    phys->clk_train.timestamp_flags =
        TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    phys->clk_train.burst_duration_ns = burst_duration_ns;

    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        tdma_pio_spi_clk_forward_program_init(
            BOARD_TDMA_SPI_PIO,
            phys->tx_sm,
            s_tdma_pio_spi_clk_forward_offset,
            phys->rx_sck_pin,
            phys->tx_sck_pin);
        phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_FORWARDING;
        phys->clk_train.result =
            TDMA_PIO_SPI_CLK_TRAIN_RESULT_FORWARD_ARMED;
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, true);
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
        tdma_pio_spi_phys_clk_train_write_end(phys);
        return true;
    }

    tdma_pio_spi_clk_burst_program_init(BOARD_TDMA_SPI_PIO,
                                        phys->tx_sm,
                                        s_tdma_pio_spi_clk_burst_offset,
                                        phys->tx_sck_pin,
                                        phys->baud_hz);
    tdma_pio_spi_clk_capture_program_init(
        BOARD_TDMA_SPI_PIO,
        phys->rx_sm,
        s_tdma_pio_spi_clk_capture_offset,
        phys->rx_sck_pin);
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm, cycles - 1u);
    const uint64_t tx_start_timestamp_ns = vdc_timestamp_clock_now_ns();
    phys->clk_train.tx_start_timestamp_ns = tx_start_timestamp_ns;
    phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING;
    phys->clk_train.result = TDMA_PIO_SPI_CLK_TRAIN_RESULT_NONE;
    phys->clk_train_return_deadline_ns =
        tdma_pio_spi_phys_now_us() * 1000ull + burst_duration_ns +
        TDMA_PIO_SPI_TRAIN_RETURN_TIMEOUT_NS;
    pio_enable_sm_mask_in_sync(
        BOARD_TDMA_SPI_PIO,
        (1u << phys->tx_sm) | (1u << phys->rx_sm));
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_clk_train_write_end(phys);
    return true;
}

void tdma_pio_spi_phys_train_clock_service(void *context, uint64_t now_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->armed) {
        return;
    }

    const uint32_t state = phys->clk_train.state;
    if (state != TDMA_PIO_SPI_CLK_TRAIN_FORWARDING &&
        state != TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING) {
        return;
    }

    tdma_pio_spi_phys_clk_train_write_begin(phys);
    phys->clk_train.service_count++;
    if (state == TDMA_PIO_SPI_CLK_TRAIN_FORWARDING) {
        tdma_pio_spi_phys_clk_train_write_end(phys);
        return;
    }

    const bool return_seen =
        pio_interrupt_get(BOARD_TDMA_SPI_PIO, 2u);
    const bool tx_done = pio_interrupt_get(BOARD_TDMA_SPI_PIO, 1u);
    if (tx_done && phys->clk_train.tx_done_observed_timestamp_ns == 0ull) {
        phys->clk_train.tx_done_observed_timestamp_ns = now_ns;
    }
    if (return_seen) {
        phys->clk_train.return_seen = 1u;
        phys->clk_train.return_before_tx_done = tx_done ? 0u : 1u;
        phys->clk_train.return_observed_timestamp_ns = now_ns;
        phys->clk_train.result = tx_done
                                     ? TDMA_PIO_SPI_CLK_TRAIN_RESULT_NO_OVERLAP
                                     : TDMA_PIO_SPI_CLK_TRAIN_RESULT_RETURN_OVERLAP;
        phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_MASTER_COMPLETE;
    } else if (tx_done && now_ns >= phys->clk_train_return_deadline_ns) {
        phys->clk_train.result =
            TDMA_PIO_SPI_CLK_TRAIN_RESULT_RETURN_TIMEOUT;
        phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_ERROR;
    }

    if (phys->clk_train.state != TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
    }
    tdma_pio_spi_phys_clk_train_write_end(phys);
}

static void tdma_pio_spi_phys_cal_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->cal_loopback_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_cal_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->cal_loopback_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_cal_reject(tdma_pio_spi_phys_t *phys,
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
    if (phys == NULL || sample_words == 0u ||
        sample_words > TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS ||
        phys->cal_loopback_start_pending ||
        phys->cal_loopback.armed != 0u) {
        return false;
    }
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK)) {
        tdma_pio_spi_phys_cal_reject(phys, epoch, 1u);
        return false;
    }
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
    return true;
}

void tdma_pio_spi_phys_cal_loopback_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys != NULL) {
        phys->cal_loopback_stop_pending = true;
    }
}

void tdma_pio_spi_phys_cal_loopback_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (!phys->armed && !phys->cal_loopback_start_pending) {
        phys->cal_loopback_start_pending = false;
        phys->cal_loopback_stop_pending = false;
        return;
    }
    if (phys->cal_loopback.armed != 0u &&
        s_tdma_pio_spi_rx_dma_channel >= 0 &&
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count == 0u) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->cal_loopback_tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->cal_loopback_capture_sm, false);
        tdma_pio_spi_phys_cal_write_begin(phys);
        phys->cal_loopback.produced_words = phys->cal_loopback.requested_words;
        phys->cal_loopback.armed = 0u;
        phys->cal_loopback.complete = 1u;
        tdma_pio_spi_phys_cal_decode(phys);
        tdma_pio_spi_phys_cal_write_end(phys);
        tdma_pio_spi_phys_cal_cleanup(phys);
    }
    if (phys->cal_loopback_stop_pending) {
        if (s_tdma_pio_spi_rx_dma_channel >= 0) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        }
        if (phys->cal_loopback.armed != 0u) {
            pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->cal_loopback_tx_sm, false);
            pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->cal_loopback_capture_sm, false);
        }
        tdma_pio_spi_phys_cal_write_begin(phys);
        phys->cal_loopback.armed = 0u;
        phys->cal_loopback_stop_pending = false;
        tdma_pio_spi_phys_cal_cleanup(phys);
        tdma_pio_spi_phys_cal_write_end(phys);
    }
    if (!phys->cal_loopback_start_pending || phys->cal_loopback.armed != 0u) return;
    const uint tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    const uint capture_sm = BOARD_TDMA_SPI_SLAVE_SM;
    if (!tdma_pio_spi_phys_ensure_rx_dma()) {
        phys->cal_loopback_start_pending = false;
        tdma_pio_spi_phys_cal_reject(phys, phys->cal_loopback_epoch, 1u);
        tdma_pio_spi_phys_cal_cleanup(phys);
        return;
    }
    tdma_pio_spi_cal_loopback_tx_program_init(
        BOARD_TDMA_SPI_PIO, tx_sm, s_tdma_pio_spi_cal_tx_offset);
    tdma_pio_spi_cal_loopback_capture_program_init(
        BOARD_TDMA_SPI_PIO, capture_sm, s_tdma_pio_spi_cal_capture_offset,
        phys->cal_loopback_sample_hz);
    memset(s_tdma_pio_spi_cal_ring, 0, sizeof(s_tdma_pio_spi_cal_ring));
    dma_channel_config dc = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, capture_sm, false));
    dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &dc,
                          s_tdma_pio_spi_cal_ring,
                          &BOARD_TDMA_SPI_PIO->rxf[capture_sm],
                          phys->cal_loopback_sample_words, false);
    tdma_pio_spi_phys_cal_write_begin(phys);
    memset(&phys->cal_loopback, 0, sizeof(phys->cal_loopback));
    phys->cal_loopback.armed = 1u;
    phys->cal_loopback.sample_hz = phys->cal_loopback_sample_hz;
    phys->cal_loopback.sample_period_ns =
        1000000000u / phys->cal_loopback_sample_hz;
    phys->cal_loopback.requested_words = phys->cal_loopback_sample_words;
    phys->cal_loopback.flags = TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_PIO_DMA |
                               TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_DIAGNOSTIC_ONLY;
    phys->cal_loopback.epoch = phys->cal_loopback_epoch;
    phys->cal_loopback_tx_sm = tx_sm;
    phys->cal_loopback_capture_sm = capture_sm;
    phys->cal_loopback_start_pending = false;
    tdma_pio_spi_phys_cal_write_end(phys);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                               (1u << tx_sm) | (1u << capture_sm));
}

static uint32_t tdma_pio_spi_cal_sample_byte(uint32_t word, uint32_t index)
{
    return (word >> (index * 8u)) & 0xFFu;
}

static void tdma_pio_spi_phys_cal_decode(tdma_pio_spi_phys_t *phys)
{
    uint32_t previous = 0u;
    bool have_previous = false;
    uint32_t found = 0u;
    uint64_t times[4] = {0u, 0u, 0u, 0u};
    uint32_t sync_edges = 0u;
    const uint32_t period = phys->cal_loopback.sample_period_ns;
    for (uint32_t w = 0u;
         w < phys->cal_loopback.requested_words && found != 0x0Fu; w++) {
        for (uint32_t i = 0u; i < 4u && found != 0x0Fu; i++) {
            const uint32_t sample = tdma_pio_spi_cal_sample_byte(
                s_tdma_pio_spi_cal_ring[w], i);
            if (!have_previous) {
                previous = sample;
                have_previous = true;
                continue;
            }
            const uint32_t rising = sample & ~previous;
            const uint64_t t = ((uint64_t)w * 4ull + i) * period;
            if ((rising & (1u << 2u)) != 0u) sync_edges |= 1u;
            if ((rising & (1u << 3u)) != 0u) sync_edges |= 2u;
            if ((rising & (1u << 1u)) != 0u && (found & 1u) == 0u) {
                times[0] = t; found |= 1u;
            }
            if ((rising & (1u << 4u)) != 0u && (found & 2u) == 0u) {
                times[1] = t; found |= 2u;
            }
            if ((rising & (1u << 5u)) != 0u && (found & 4u) == 0u) {
                times[2] = t; found |= 4u;
            }
            if ((rising & (1u << 0u)) != 0u && (found & 8u) == 0u) {
                times[3] = t; found |= 8u;
            }
            previous = sample;
        }
    }
    phys->cal_loopback.edge_mask = found;
    phys->cal_loopback.t1_clk_tx = times[0];
    phys->cal_loopback.t2_clk_rx = times[1];
    phys->cal_loopback.t3_data_tx = times[2];
    phys->cal_loopback.t4_data_rx = times[3];
    if (sync_edges == 3u) {
        phys->cal_loopback.flags |=
            TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_SYNC_MATCH;
    }
}

bool tdma_pio_spi_phys_get_cal_loopback_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_cal_loopback_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(&phys->cal_loopback_guard,
                                               __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->cal_loopback;
        const uint32_t end = __atomic_load_n(&phys->cal_loopback_guard,
                                             __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

static void tdma_pio_spi_phys_coded_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->coded_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_coded_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->coded_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_coded_publish_error(
    tdma_pio_spi_phys_t *phys,
    uint32_t epoch,
    tdma_pio_spi_coded_reject_t reason)
{
    tdma_pio_spi_phys_coded_write_begin(phys);
    memset(&phys->coded, 0, sizeof(phys->coded));
    phys->coded.version = TDMA_PIO_SPI_CODED_SNAPSHOT_VERSION;
    phys->coded.state = TDMA_PIO_SPI_CODED_ERROR;
    phys->coded.role = phys->role;
    phys->coded.flags = TDMA_PIO_SPI_CODED_FLAG_DIAGNOSTIC_ONLY;
    phys->coded.reject_reason = (uint32_t)reason;
    phys->coded.epoch = epoch;
    phys->coded.tx_dma_channel = TDMA_PIO_SPI_TX_DMA_CHANNEL;
    phys->coded.rx_dma_channel = TDMA_PIO_SPI_RX_DMA_CHANNEL;
    tdma_pio_spi_phys_coded_write_end(phys);
}

static void tdma_pio_spi_phys_prepare_maintenance_pins(
    tdma_pio_spi_phys_t *phys)
{
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

bool tdma_pio_spi_phys_coded_start(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_coded_request_t *request)
{
    if (phys == NULL || request == NULL) {
        if (phys != NULL) {
            tdma_pio_spi_phys_coded_publish_error(
                phys, 0u, TDMA_PIO_SPI_CODED_REJECT_BAD_ARGUMENT);
        }
        return false;
    }
    /* A rejected overlapping request must not overwrite the guarded facts of
     * the transfer that still owns both DMA channels. */
    if (phys->coded.state == TDMA_PIO_SPI_CODED_RUNNING ||
        phys->coded.state == TDMA_PIO_SPI_CODED_FORWARDING) {
        return false;
    }
    if (phys->role != TDMA_PIO_SPI_ROLE_MASTER &&
        phys->role != TDMA_PIO_SPI_ROLE_SLAVE) {
        tdma_pio_spi_phys_coded_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_CODED_REJECT_BAD_ARGUMENT);
        return false;
    }

    const uint32_t capture_words =
        (request->capture_sample_count + 31u) / 32u;
    if (phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        (request->tx_words == NULL || request->tx_word_count == 0u ||
         request->tx_word_count > TDMA_PIO_SPI_CODED_BUFFER_WORDS ||
         request->tx_sample_count == 0u ||
         request->tx_sample_count > request->tx_word_count * 32u ||
         request->capture_sample_count < request->tx_sample_count ||
         capture_words == 0u ||
         capture_words > TDMA_PIO_SPI_CODED_BUFFER_WORDS)) {
        tdma_pio_spi_phys_coded_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_CODED_REJECT_BAD_ARGUMENT);
        return false;
    }

    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    phys->rx_capture_active = false;
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED)) {
        tdma_pio_spi_phys_coded_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_CODED_REJECT_RESOURCE);
        return false;
    }

    if (!phys->armed) {
        tdma_pio_spi_phys_prepare_maintenance_pins(phys);
    }
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);

    tdma_pio_spi_phys_coded_write_begin(phys);
    memset(&phys->coded, 0, sizeof(phys->coded));
    phys->coded.version = TDMA_PIO_SPI_CODED_SNAPSHOT_VERSION;
    phys->coded.role = phys->role;
    phys->coded.flags = TDMA_PIO_SPI_CODED_FLAG_DIAGNOSTIC_ONLY;
    phys->coded.epoch = request->epoch;
    phys->coded.tx_dma_channel = TDMA_PIO_SPI_TX_DMA_CHANNEL;
    phys->coded.rx_dma_channel = TDMA_PIO_SPI_RX_DMA_CHANNEL;
    phys->coded.tx_word_count = request->tx_word_count;
    phys->coded.tx_sample_count = request->tx_sample_count;
    phys->coded.capture_word_count = capture_words;
    phys->coded.capture_sample_count = request->capture_sample_count;
    phys->coded.timing_field_tx_origin_sample =
        request->timing_field_tx_origin_sample;

    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        tdma_pio_spi_clk_forward_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_clk_forward_offset,
            phys->rx_sck_pin, phys->tx_sck_pin);
        phys->coded.state = TDMA_PIO_SPI_CODED_FORWARDING;
        phys->coded.flags |= TDMA_PIO_SPI_CODED_FLAG_FORWARD_ONLY;
        tdma_pio_spi_phys_coded_write_end(phys);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, true);
        return true;
    }
    tdma_pio_spi_phys_coded_write_end(phys);

    if (!tdma_pio_spi_phys_ensure_tx_dma() ||
        !tdma_pio_spi_phys_ensure_rx_dma()) {
        tdma_pio_spi_phys_coded_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_CODED_REJECT_RESOURCE);
        tdma_pio_spi_phys_cal_cleanup(phys);
        return false;
    }

    memcpy(s_tdma_pio_spi_coded_tx, request->tx_words,
           request->tx_word_count * sizeof(request->tx_words[0]));
    memset(s_tdma_pio_spi_coded_rx, 0,
           capture_words * sizeof(s_tdma_pio_spi_coded_rx[0]));
    tdma_pio_spi_clk_coded_tx_program_init(
        BOARD_TDMA_SPI_PIO, phys->tx_sm,
        s_tdma_pio_spi_clk_coded_tx_offset, phys->tx_sck_pin);
    tdma_pio_spi_clk_oversample_program_init(
        BOARD_TDMA_SPI_PIO, phys->rx_sm,
        s_tdma_pio_spi_clk_oversample_offset, phys->rx_sck_pin);

    dma_channel_config tx_dc = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_tx_dma_channel);
    channel_config_set_transfer_data_size(&tx_dc, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_dc, true);
    channel_config_set_write_increment(&tx_dc, false);
    channel_config_set_dreq(
        &tx_dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->tx_sm, true));
    dma_channel_configure((uint)s_tdma_pio_spi_tx_dma_channel, &tx_dc,
                          &BOARD_TDMA_SPI_PIO->txf[phys->tx_sm],
                          s_tdma_pio_spi_coded_tx,
                          request->tx_word_count, false);

    dma_channel_config rx_dc = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_dc, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_dc, false);
    channel_config_set_write_increment(&rx_dc, true);
    channel_config_set_dreq(
        &rx_dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, false));
    dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &rx_dc,
                          s_tdma_pio_spi_coded_rx,
                          &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                          capture_words, false);

    tdma_pio_spi_phys_coded_write_begin(phys);
    phys->coded.state = TDMA_PIO_SPI_CODED_RUNNING;
    phys->coded.capture_origin_tick = vdc_timestamp_clock_now_ns();
    phys->coded.tx_dma_remaining = request->tx_word_count;
    phys->coded.rx_dma_remaining = capture_words;
    tdma_pio_spi_phys_coded_write_end(phys);

    dma_start_channel_mask((1u << (uint)s_tdma_pio_spi_tx_dma_channel) |
                           (1u << (uint)s_tdma_pio_spi_rx_dma_channel));
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                               (1u << phys->tx_sm) | (1u << phys->rx_sm));
    return true;
}

void tdma_pio_spi_phys_coded_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    tdma_pio_spi_phys_cal_cleanup(phys);
    tdma_pio_spi_phys_coded_write_begin(phys);
    phys->coded.state = TDMA_PIO_SPI_CODED_IDLE;
    phys->coded.reject_reason = TDMA_PIO_SPI_CODED_REJECT_NONE;
    phys->coded.tx_dma_remaining = 0u;
    phys->coded.rx_dma_remaining = 0u;
    tdma_pio_spi_phys_coded_write_end(phys);
    (void)tdma_pio_spi_phys_select_program_persona(
        phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

void tdma_pio_spi_phys_coded_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || phys->coded.state != TDMA_PIO_SPI_CODED_RUNNING ||
        s_tdma_pio_spi_tx_dma_channel < 0 ||
        s_tdma_pio_spi_rx_dma_channel < 0) {
        return;
    }
    const uint32_t tx_remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_tx_dma_channel].transfer_count;
    const uint32_t rx_remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
    const bool rx_stalled =
        pio_sm_is_rx_fifo_full(BOARD_TDMA_SPI_PIO, phys->rx_sm);

    tdma_pio_spi_phys_coded_write_begin(phys);
    phys->coded.tx_dma_remaining = tx_remaining;
    phys->coded.rx_dma_remaining = rx_remaining;
    if (tx_remaining == 0u) {
        phys->coded.flags |= TDMA_PIO_SPI_CODED_FLAG_TX_DMA_COMPLETE;
    }
    if (rx_stalled && rx_remaining != 0u) {
        phys->coded.pio_stall_count++;
        phys->coded.dma_overrun_count++;
    }
    if (rx_remaining == 0u) {
        phys->coded.flags |= TDMA_PIO_SPI_CODED_FLAG_RX_DMA_COMPLETE;
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
        if (tx_remaining != 0u) {
            phys->coded.state = TDMA_PIO_SPI_CODED_ERROR;
            phys->coded.reject_reason =
                TDMA_PIO_SPI_CODED_REJECT_CAPTURE_SHORT;
        } else if (phys->coded.pio_stall_count != 0u) {
            phys->coded.state = TDMA_PIO_SPI_CODED_ERROR;
            phys->coded.reject_reason = TDMA_PIO_SPI_CODED_REJECT_PIO_STALL;
        } else {
            phys->coded.state = TDMA_PIO_SPI_CODED_COMPLETE;
            phys->coded.reject_reason = TDMA_PIO_SPI_CODED_REJECT_NONE;
        }
    }
    const bool finished =
        phys->coded.state == TDMA_PIO_SPI_CODED_COMPLETE ||
        phys->coded.state == TDMA_PIO_SPI_CODED_ERROR;
    tdma_pio_spi_phys_coded_write_end(phys);
    if (finished) {
        tdma_pio_spi_phys_cal_cleanup(phys);
        (void)tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    }
}

bool tdma_pio_spi_phys_get_coded_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_coded_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->coded_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->coded;
        const uint32_t end =
            __atomic_load_n(&phys->coded_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool tdma_pio_spi_phys_copy_coded_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    tdma_pio_spi_coded_snapshot_t snapshot;
    if (capture_word_count != NULL) *capture_word_count = 0u;
    if (phys == NULL || capture_words == NULL || capture_word_count == NULL ||
        !tdma_pio_spi_phys_get_coded_snapshot(phys, &snapshot) ||
        snapshot.state != TDMA_PIO_SPI_CODED_COMPLETE ||
        snapshot.capture_word_count > capture_word_capacity) {
        return false;
    }
    memcpy(capture_words, s_tdma_pio_spi_coded_rx,
           snapshot.capture_word_count * sizeof(capture_words[0]));
    *capture_word_count = snapshot.capture_word_count;
    return true;
}

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

static void tdma_pio_spi_phys_p3_decode(tdma_pio_spi_phys_t *phys)
{
    uint32_t previous = 0u;
    bool have_previous = false;
    uint32_t found = 0u;
    uint64_t times[4] = {0u, 0u, 0u, 0u};
    uint64_t clock_rise = 0u;
    uint64_t clock_fall = 0u;
    uint64_t clock_high_sum = 0u;
    uint64_t clock_low_sum = 0u;
    uint32_t clock_high_count = 0u;
    uint32_t clock_low_count = 0u;
    uint64_t data_rise = 0u;
    uint64_t data_fall = 0u;
    bool have_clock_rise = false;
    bool have_clock_fall = false;
    bool have_data_rise = false;
    bool have_data_fall = false;
    /* signal_group selects the physical line used for the forward leg.
     * The return leg is always DATA for the current P3 wiring.  The third
     * line is only a sync marker and is intentionally absent from t1..t4. */
    const bool forward_is_cs = phys->p3.signal_group ==
        TDMA_PIO_SPI_P3_GROUP_CS_DATA;
    const uint32_t forward_mask = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR
            ? (1u << (forward_is_cs ? 2u : 1u))
            : (1u << (forward_is_cs ? 3u : 4u));
    const uint32_t data_mask = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR ? (1u << 0u) : (1u << 5u);
    const uint32_t period = phys->p3.sample_period_ns;
    for (uint32_t w = 0u; w < phys->p3.requested_words; w++) {
        for (uint32_t i = 0u; i < 4u; i++) {
            const uint32_t sample = tdma_pio_spi_cal_sample_byte(
                s_tdma_pio_spi_cal_ring[w], i);
            if (!have_previous) {
                previous = sample;
                have_previous = true;
                continue;
            }
            const uint32_t rising = sample & ~previous;
            const uint32_t falling = previous & ~sample;
            const uint64_t timestamp =
                ((uint64_t)w * 4ull + i) * period;
            if ((rising & forward_mask) != 0u) {
                if (!have_clock_rise) {
                    clock_rise = timestamp;
                    have_clock_rise = true;
                }
                if (have_clock_fall) {
                    clock_low_sum += timestamp - clock_fall;
                    clock_low_count++;
                    have_clock_fall = false;
                }
                clock_rise = timestamp;
            }
            if ((falling & forward_mask) != 0u && have_clock_rise) {
                clock_fall = timestamp;
                have_clock_fall = true;
                clock_high_sum += timestamp - clock_rise;
                clock_high_count++;
            }
            if ((rising & data_mask) != 0u && !have_data_rise) {
                data_rise = timestamp;
                have_data_rise = true;
            }
            if ((falling & data_mask) != 0u && have_data_rise &&
                !have_data_fall) {
                data_fall = timestamp;
                have_data_fall = true;
            }
            if (phys->p3.role == TDMA_PIO_SPI_P3_ROLE_INITIATOR) {
                const uint32_t tx_mask = forward_mask;
                if ((rising & tx_mask) != 0u && (found & 1u) == 0u) {
                    times[0] = timestamp;
                    found |= 1u;
                }
                if ((rising & (1u << 0u)) != 0u && (found & 8u) == 0u) {
                    times[3] = timestamp;
                    found |= 8u;
                }
            } else {
                const uint32_t rx_mask = forward_mask;
                if ((rising & rx_mask) != 0u && (found & 2u) == 0u) {
                    times[1] = timestamp;
                    found |= 2u;
                }
                if ((rising & (1u << 5u)) != 0u && (found & 4u) == 0u) {
                    times[2] = timestamp;
                    found |= 4u;
                }
            }
            previous = sample;
        }
    }
    phys->p3.edge_mask = found;
    /* ABI-compatible field names; semantically these are forward/return
     * edges selected by signal_group, not fixed CLK/DATA functions. */
    phys->p3.t1_clk_tx = times[0];
    phys->p3.t2_clk_rx = times[1];
    phys->p3.t3_data_tx = times[2];
    phys->p3.t4_data_rx = times[3];
    if (clock_high_count != 0u) {
        phys->p3.clock_high_ns = (uint32_t)(
            (clock_high_sum + clock_high_count / 2u) / clock_high_count);
    }
    if (clock_low_count != 0u) {
        phys->p3.clock_low_ns = (uint32_t)(
            (clock_low_sum + clock_low_count / 2u) / clock_low_count);
    }
    if (have_data_rise && have_data_fall) {
        phys->p3.data_high_ns = (uint32_t)(data_fall - data_rise);
    }
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
        phys->p3.state == TDMA_PIO_SPI_P3_ARMED) {
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

bool tdma_pio_spi_phys_get_clk_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_clk_train_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) {
        return false;
    }
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t guard_begin =
            __atomic_load_n(&phys->clk_train_guard, __ATOMIC_ACQUIRE);
        if ((guard_begin & 1u) != 0u) {
            continue;
        }
        *snapshot = phys->clk_train;
        const uint32_t guard_end =
            __atomic_load_n(&phys->clk_train_guard, __ATOMIC_ACQUIRE);
        if (guard_begin == guard_end && (guard_end & 1u) == 0u) {
            return true;
        }
    }
    return false;
}

bool tdma_pio_spi_phys_tx(void *context,
                          const uint8_t *packet,
                          size_t packet_size,
                          uint64_t *tx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || packet == NULL || packet_size == 0u ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX || !phys->armed) {
        if (phys != NULL) {
            tdma_pio_spi_phys_set_error(phys,
                                        TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        }
        return false;
    }
    if (!pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
        phys->snapshot.tx_busy_count++;
        return false;
    }

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE] = {
        TDMA_PIO_SPI_PACKET_MAGIC0,
        TDMA_PIO_SPI_PACKET_MAGIC1,
        (uint8_t)(packet_size & 0xFFu),
        (uint8_t)(packet_size >> 8u),
    };
    const uint64_t tx_edge_timestamp_ns = vdc_timestamp_clock_now_ns();
    gpio_put(phys->tx_csn_pin, false);
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_PACKET_HEADER_SIZE; i++) {
        if (!tdma_pio_spi_phys_tx_put(phys, ((uint32_t)header[i]) << 24u)) {
            gpio_put(phys->tx_csn_pin, true);
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            phys->snapshot.tx_busy_count++;
            return false;
        }
    }
    for (size_t i = 0u; i < packet_size; i++) {
        if (!tdma_pio_spi_phys_tx_put(phys, ((uint32_t)packet[i]) << 24u)) {
            gpio_put(phys->tx_csn_pin, true);
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            phys->snapshot.tx_busy_count++;
            return false;
        }
    }
    busy_wait_us_32(tdma_pio_spi_phys_frame_tail_us(phys, packet_size));
    gpio_put(phys->tx_csn_pin, true);
    const uint64_t tx_done_timestamp_ns = vdc_timestamp_clock_now_ns();

    phys->snapshot.tx_count++;
    phys->snapshot.tx_edge_count++;
    phys->snapshot.last_tx_size = (uint32_t)packet_size;
    phys->snapshot.last_tx_edge_timestamp_ns = tx_edge_timestamp_ns;
    phys->snapshot.last_tx_done_timestamp_ns = tx_done_timestamp_ns;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = tx_edge_timestamp_ns;
    }
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

bool tdma_pio_spi_phys_rx(void *context,
                          uint8_t *packet,
                          size_t packet_capacity,
                          size_t *packet_size,
                          uint64_t *rx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (packet_size != NULL) {
        *packet_size = 0u;
    }
    if (phys == NULL || packet == NULL || packet_capacity == 0u ||
        packet_size == NULL || rx_timestamp_ns == NULL || !phys->armed) {
        if (phys != NULL) {
            tdma_pio_spi_phys_set_error(phys,
                                        TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        }
        return false;
    }

    size_t received_words = 0u;
    if (!tdma_pio_spi_phys_capture_words(phys,
                                         TDMA_PIO_SPI_RX_DMA_WORD_MAX,
                                         &received_words)) {
        return false;
    }
    if (received_words < TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE];
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_PACKET_HEADER_SIZE; i++) {
        header[i] = (uint8_t)(s_tdma_pio_spi_rx_frame[i] & 0xFFu);
    }
    const uint16_t frame_size =
        (uint16_t)header[2] | ((uint16_t)header[3] << 8u);
    if (header[0] != TDMA_PIO_SPI_PACKET_MAGIC0 ||
        header[1] != TDMA_PIO_SPI_PACKET_MAGIC1 ||
        frame_size == 0u) {
        phys->snapshot.last_bad_header0 = s_tdma_pio_spi_rx_frame[0];
        phys->snapshot.last_bad_header1 = s_tdma_pio_spi_rx_frame[1];
        phys->snapshot.last_bad_header2 = s_tdma_pio_spi_rx_frame[2];
        phys->snapshot.last_bad_header3 = s_tdma_pio_spi_rx_frame[3];
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }
    if ((size_t)frame_size > packet_capacity) {
        phys->snapshot.last_bad_header0 =
            header[2] | ((uint32_t)header[3] << 8u);
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys,
                                    TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE);
        return false;
    }
    if (received_words <
        (size_t)frame_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }

    for (uint16_t i = 0u; i < frame_size; i++) {
        packet[i] = (uint8_t)(
            s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_PACKET_HEADER_SIZE + i] &
            0xFFu);
    }
    *packet_size = frame_size;
    const uint64_t rx_extract_timestamp_ns = vdc_timestamp_clock_now_ns();
    const uint64_t wire_time_ns =
        tdma_pio_spi_phys_wire_time_ns(phys, frame_size);
    const uint64_t rx_edge_timestamp_ns =
        rx_extract_timestamp_ns > wire_time_ns
            ? rx_extract_timestamp_ns - wire_time_ns
            : rx_extract_timestamp_ns;
    *rx_timestamp_ns = rx_edge_timestamp_ns;

    phys->snapshot.rx_count++;
    phys->snapshot.last_rx_size = frame_size;
    phys->snapshot.last_rx_timestamp_ns = *rx_timestamp_ns;
    phys->snapshot.rx_edge_count++;
    phys->snapshot.last_rx_edge_timestamp_ns = rx_edge_timestamp_ns;
    phys->snapshot.last_rx_extract_timestamp_ns = rx_extract_timestamp_ns;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

bool tdma_pio_spi_phys_get_snapshot(const tdma_pio_spi_phys_t *phys,
                                    tdma_pio_spi_phys_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = phys->snapshot;
    return true;
}
