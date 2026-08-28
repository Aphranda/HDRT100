#include "tdma_pio_spi_phys.h"

#include <string.h>

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "tdma_flight_overlay.h"
#include "tdma_pio_spi.pio.h"
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
_Static_assert(TDMA_PIO_SPI_RX_RING_WORDS >=
                   3u * TDMA_PIO_SPI_RX_DMA_WORD_MAX,
               "TDMA SPI RX ring must hold three maximum short packets");
_Static_assert(TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES >=
                   TDMA_PIO_SPI_RX_DMA_WORD_MAX,
               "TRN-03B TX capture must hold one maximum short packet");

static bool s_tdma_pio_spi_sms_claimed;
static tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
static uint s_tdma_pio_spi_tx_offset;
static uint s_tdma_pio_spi_rx_offset;
static uint s_tdma_pio_spi_clk_forward_offset;
static uint s_tdma_pio_spi_marker_forward_offset;
static uint s_tdma_pio_spi_clk_burst_offset;
static uint s_tdma_pio_spi_clk_capture_offset;
static uint s_tdma_pio_spi_clk_coded_tx_offset;
static uint s_tdma_pio_spi_clk_oversample_offset;
static uint s_tdma_pio_spi_marker_origin_offset;
static uint s_tdma_pio_spi_marker_capture_offset;
static uint s_tdma_pio_spi_data_train_source_offset;
static uint s_tdma_pio_spi_data_train_sink_offset;
static uint s_tdma_pio_spi_sck_train_trigger_offset;
static uint s_tdma_pio_spi_sck_train_source_offset;
static uint s_tdma_pio_spi_sck_train_sink_offset;
static uint s_tdma_pio_spi_cal_tx_offset;
static uint s_tdma_pio_spi_cal_capture_offset;
static uint s_tdma_pio_spi_p3_initiator_offset;
static uint s_tdma_pio_spi_p3_responder_offset;
static uint s_tdma_pio_spi_p3_capture_offset;
static uint s_tdma_pio_spi_p3_responder_capture_offset;
static uint s_tdma_pio_spi_flight_origin_clock_offset;
static uint s_tdma_pio_spi_flight_origin_data_offset;
static uint s_tdma_pio_spi_flight_data_follower_offset;
static uint s_tdma_pio_spi_flight_process_follower_offset;
static uint s_tdma_pio_spi_flight_control_forward_offset;
static uint s_tdma_pio_spi_flight_clock_latch_offset;
static uint s_tdma_pio_spi_flight_origin_rtt_offset;
static uint32_t s_tdma_pio_spi_cal_ring[TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS]
    __attribute__((aligned(4)));
/* Calibration personas are mutually exclusive: the TDMA owner restores the
 * normal DATA/CS persona before accepting another request.  Keep one maximum
 * sized TX/RX workspace per direction instead of reserving coded, marker and
 * data-training arrays concurrently.  The named members preserve the
 * persona-specific bounds and call sites while the union makes the lifetime
 * contract explicit in the static RAM layout. */
typedef union {
    uint32_t coded[TDMA_PIO_SPI_CODED_BUFFER_WORDS];
    uint32_t marker[TDMA_PIO_SPI_MARKER_BUFFER_WORDS];
    uint32_t data_train[TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS];
} tdma_pio_spi_cal_tx_workspace_t;

typedef union {
    uint32_t coded[TDMA_PIO_SPI_CODED_BUFFER_WORDS];
    uint32_t marker[TDMA_PIO_SPI_MARKER_BUFFER_WORDS];
    uint32_t data_train[TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS];
} tdma_pio_spi_cal_rx_workspace_t;

static tdma_pio_spi_cal_tx_workspace_t s_tdma_pio_spi_cal_tx_workspace
    __attribute__((aligned(4)));
static tdma_pio_spi_cal_rx_workspace_t s_tdma_pio_spi_cal_rx_workspace
    __attribute__((aligned(4)));
#define s_tdma_pio_spi_coded_tx (s_tdma_pio_spi_cal_tx_workspace.coded)
#define s_tdma_pio_spi_marker_tx (s_tdma_pio_spi_cal_tx_workspace.marker)
#define s_tdma_pio_spi_data_train_tx \
    (s_tdma_pio_spi_cal_tx_workspace.data_train)
#define s_tdma_pio_spi_coded_rx (s_tdma_pio_spi_cal_rx_workspace.coded)
#define s_tdma_pio_spi_marker_rx (s_tdma_pio_spi_cal_rx_workspace.marker)
#define s_tdma_pio_spi_data_train_rx \
    (s_tdma_pio_spi_cal_rx_workspace.data_train)
/* CS-style local launch: high idle followed by one low edge. */
static uint32_t s_tdma_pio_spi_sck_train_inject_word = 0u;
static void tdma_pio_spi_phys_cal_decode(tdma_pio_spi_phys_t *phys);
static int s_tdma_pio_spi_tx_dma_channel = -1;
static int s_tdma_pio_spi_rx_dma_channel = -1;
static uint32_t s_tdma_pio_spi_rx_ring[TDMA_PIO_SPI_RX_RING_WORDS]
    __attribute__((aligned(TDMA_PIO_SPI_RX_RING_WORDS * sizeof(uint32_t))));
static uint32_t s_tdma_pio_spi_flight_tx_words[
    TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS] __attribute__((aligned(4)));
/* Two resident scripts allow core1 to prepare the next process-image
 * overlay while the PIO/DMA engine is still draining the current one.  The
 * active buffer is never mutated until its DMA transfer has completed. */
static uint32_t s_tdma_pio_spi_flight_overlay_script[2u][
    TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS] __attribute__((aligned(4)));
static uint32_t s_tdma_pio_spi_tx_last_frame[
    TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES]
    __attribute__((aligned(4)));
static volatile uint32_t s_tdma_pio_spi_tx_history_produced;
static volatile uint32_t s_tdma_pio_spi_tx_history_guard;
static volatile uint32_t s_tdma_pio_spi_tx_last_frame_bytes;
static volatile uint32_t s_tdma_pio_spi_tx_complete_frame_count;
static uint64_t s_tdma_pio_spi_rx_scan_produced;
static tdma_rx_sequence_tracker_t s_tdma_pio_spi_rx_sequence;
/* Assembled frame (magic-aligned) copied out of the continuous DMA ring. */
static uint32_t s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX];

static void tdma_pio_spi_phys_reset_normal_capture(void);

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
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM)) {
        return false;
    }
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
    s_tdma_pio_spi_sms_claimed = true;
    return true;
}

static bool tdma_pio_spi_phys_load_flight_clock_latch_program(void)
{
    if (!pio_can_add_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_clock_latch_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_clock_latch_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_flight_clock_latch_program);
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

static bool tdma_pio_spi_phys_load_marker_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_forward_program)) return false;
    s_tdma_pio_spi_marker_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_origin_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_forward_program,
                           s_tdma_pio_spi_marker_forward_offset);
        return false;
    }
    s_tdma_pio_spi_marker_origin_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_origin_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_forward_program,
                           s_tdma_pio_spi_marker_forward_offset);
        return false;
    }
    s_tdma_pio_spi_marker_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_data_train_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_origin_program)) return false;
    s_tdma_pio_spi_marker_origin_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_origin_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_data_train_source_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        return false;
    }
    s_tdma_pio_spi_data_train_source_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_data_train_source_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_data_train_sink_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_data_train_source_program,
                           s_tdma_pio_spi_data_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        return false;
    }
    s_tdma_pio_spi_data_train_sink_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_data_train_sink_program);
    return true;
}

static bool tdma_pio_spi_phys_load_sck_train_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_trigger_program)) {
        return false;
    }
    s_tdma_pio_spi_sck_train_trigger_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_trigger_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_source_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_trigger_program,
                           s_tdma_pio_spi_sck_train_trigger_offset);
        return false;
    }
    s_tdma_pio_spi_sck_train_source_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_source_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_sink_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_source_program,
                           s_tdma_pio_spi_sck_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_trigger_program,
                           s_tdma_pio_spi_sck_train_trigger_offset);
        return false;
    }
    s_tdma_pio_spi_sck_train_sink_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_sink_program);
    return true;
}

static bool tdma_pio_spi_phys_load_flight_origin_programs(void)
{
    if (!pio_can_add_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_clock_rx_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_origin_clock_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_flight_origin_clock_rx_program);
    if (!pio_can_add_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_data_tx_program)) {
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    s_tdma_pio_spi_flight_origin_data_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_flight_origin_data_tx_program);
    if (!pio_can_add_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_rtt_program)) {
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_data_tx_program,
            s_tdma_pio_spi_flight_origin_data_offset);
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    s_tdma_pio_spi_flight_origin_rtt_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_flight_origin_rtt_program);
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program()) {
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_rtt_program,
            s_tdma_pio_spi_flight_origin_rtt_offset);
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_data_tx_program,
            s_tdma_pio_spi_flight_origin_data_offset);
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    return true;
}

static bool tdma_pio_spi_phys_load_flight_follower_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_flight_control_forward_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_control_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO,
        &tdma_pio_spi_flight_control_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_flight_data_follower_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        return false;
    }
    s_tdma_pio_spi_flight_data_follower_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO,
        &tdma_pio_spi_flight_data_follower_program);
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program()) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_flight_data_follower_program,
                           s_tdma_pio_spi_flight_data_follower_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        return false;
    }
    return true;
}

static bool tdma_pio_spi_phys_load_flight_process_follower_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_flight_control_forward_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_control_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO,
        &tdma_pio_spi_flight_control_forward_program);
    if (!pio_can_add_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_process_follower_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        return false;
    }
    s_tdma_pio_spi_flight_process_follower_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO,
        &tdma_pio_spi_flight_process_follower_program);
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program()) {
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_process_follower_program,
            s_tdma_pio_spi_flight_process_follower_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        return false;
    }
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

static bool tdma_pio_spi_phys_load_p3_reference_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_initiator_program)) return false;
    s_tdma_pio_spi_p3_initiator_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_initiator_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_responder_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        return false;
    }
    s_tdma_pio_spi_p3_responder_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_responder_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        return false;
    }
    s_tdma_pio_spi_p3_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_capture_program);
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
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE:
        return tdma_pio_spi_phys_load_p3_reference_programs();
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
    case TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER:
        return tdma_pio_spi_phys_load_marker_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN:
        return tdma_pio_spi_phys_load_data_train_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN:
        return tdma_pio_spi_phys_load_sck_train_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN:
        return tdma_pio_spi_phys_load_flight_origin_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER:
        return tdma_pio_spi_phys_load_flight_follower_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER:
        return tdma_pio_spi_phys_load_flight_process_follower_programs();
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
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_p3_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
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
    case TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_capture_program,
                           s_tdma_pio_spi_marker_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_forward_program,
                           s_tdma_pio_spi_marker_forward_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_data_train_sink_program,
                           s_tdma_pio_spi_data_train_sink_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_data_train_source_program,
                           s_tdma_pio_spi_data_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_sink_program,
                           s_tdma_pio_spi_sck_train_sink_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_source_program,
                           s_tdma_pio_spi_sck_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_trigger_program,
                           s_tdma_pio_spi_sck_train_trigger_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN:
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_clock_latch_program,
            s_tdma_pio_spi_flight_clock_latch_offset);
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_rtt_program,
            s_tdma_pio_spi_flight_origin_rtt_offset);
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_data_tx_program,
            s_tdma_pio_spi_flight_origin_data_offset);
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER:
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_clock_latch_program,
            s_tdma_pio_spi_flight_clock_latch_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_flight_data_follower_program,
                           s_tdma_pio_spi_flight_data_follower_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER:
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_clock_latch_program,
            s_tdma_pio_spi_flight_clock_latch_offset);
        pio_remove_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_flight_process_follower_program,
            s_tdma_pio_spi_flight_process_follower_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
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
        persona > TDMA_PIO_SPI_PROGRAM_PERSONA_MAX ||
        !tdma_pio_spi_phys_ensure_sms_claimed()) {
        return false;
    }
    const uint32_t sm_mask = (1u << BOARD_TDMA_SPI_MASTER_SM) |
                             (1u << BOARD_TDMA_SPI_SLAVE_SM) |
                             (1u << BOARD_TDMA_SPI_CAPTURE_SM);
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
    phys->snapshot.flight_marker_offset_sample_count =
        phys->flight_marker_offset_sample_count;
    phys->snapshot.flight_sck_offset_sample_count =
        phys->flight_sck_offset_sample_count;
    phys->snapshot.flight_data_offset_sample_count =
        phys->flight_data_offset_sample_count;
    phys->snapshot.flight_marker_phase_delay_cycles =
        phys->flight_marker_phase_delay_cycles;
    phys->snapshot.flight_sck_phase_delay_cycles =
        phys->flight_sck_phase_delay_cycles;
    phys->snapshot.flight_data_phase_delay_cycles =
        phys->flight_data_phase_delay_cycles;
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
static void __attribute__((unused)) tdma_pio_spi_phys_configure(
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

static uint32_t tdma_pio_spi_phys_flight_tail_bytes(
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

static void tdma_pio_spi_phys_prepare_sm_pair(tdma_pio_spi_phys_t *phys)
{
    uint32_t sm_mask = (1u << phys->tx_sm) | (1u << phys->rx_sm);
    const bool has_rtt_sm = phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN;
    if (has_rtt_sm) {
        sm_mask |= 1u << BOARD_TDMA_SPI_RTT_SM;
    }
    pio_set_sm_mask_enabled(BOARD_TDMA_SPI_PIO, sm_mask, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    const uint32_t capture_sm =
        s_tdma_pio_spi_program_persona ==
                TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN
            ? phys->tx_sm
            : phys->rx_sm;
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, capture_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    if (has_rtt_sm) {
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
        pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
    }
}

static void tdma_pio_spi_phys_enable_sm_pair(tdma_pio_spi_phys_t *phys)
{
    uint32_t sm_mask = (1u << phys->tx_sm) | (1u << phys->rx_sm);
    if (phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN) {
        sm_mask |= 1u << BOARD_TDMA_SPI_RTT_SM;
    }
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO, sm_mask);
}

static uint32_t tdma_pio_spi_phys_txstall_mask(uint32_t sm)
{
    return 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
}

static bool tdma_pio_spi_phys_clock_latch_rearm(
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

    const uint sm = BOARD_TDMA_SPI_CAPTURE_SM;
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, sm);
    pio_sm_put_blocking(BOARD_TDMA_SPI_PIO, sm, UINT32_MAX);
    pio_sm_exec(BOARD_TDMA_SPI_PIO, sm, pio_encode_pull(false, true));
    pio_sm_exec(BOARD_TDMA_SPI_PIO, sm, pio_encode_mov(pio_x, pio_osr));
    pio_sm_exec(BOARD_TDMA_SPI_PIO,
                sm,
                pio_encode_jmp(s_tdma_pio_spi_flight_clock_latch_offset));
    phys->flight_clock_latch_epoch_ns = vdc_timestamp_clock_now_ns();
    phys->flight_clock_latch_resolution_ns = resolution_ns;
    phys->snapshot.clock_latch_resolution_ns = resolution_ns;
    phys->flight_clock_latch_armed = true;
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, sm, true);
    return true;
}

static bool tdma_pio_spi_phys_clock_latch_read_and_rearm(
    tdma_pio_spi_phys_t *phys,
    uint64_t *timestamp_ns)
{
    if (timestamp_ns != NULL) {
        *timestamp_ns = 0ull;
    }
    if (phys == NULL || timestamp_ns == NULL ||
        !phys->flight_clock_latch_armed ||
        pio_sm_is_rx_fifo_empty(BOARD_TDMA_SPI_PIO,
                                BOARD_TDMA_SPI_CAPTURE_SM)) {
        if (phys != NULL) {
            phys->snapshot.clock_latch_miss_count++;
        }
        return false;
    }

    const uint32_t remaining = pio_sm_get(
        BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
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

static bool tdma_pio_spi_phys_restore_clock_latch(
    tdma_pio_spi_phys_t *phys,
    bool rearm)
{
    if (phys == NULL) return false;
    const uint sm = BOARD_TDMA_SPI_CAPTURE_SM;
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, sm);
    for (uint32_t index = 0u; index < 4u; index++) {
        BOARD_TDMA_SPI_PIO->instr_mem[
            s_tdma_pio_spi_flight_clock_latch_offset + index] =
                phys->flight_sck_waveform_saved_instructions[index];
    }
    tdma_pio_spi_flight_clock_latch_program_init(
        BOARD_TDMA_SPI_PIO, sm,
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
static bool tdma_pio_spi_phys_capture_restore_step(
    tdma_pio_spi_phys_t *phys, bool *complete)
{
    if (phys == NULL || complete == NULL) return false;
    *complete = false;
    const uint sm = BOARD_TDMA_SPI_CAPTURE_SM;
    const uint offset = s_tdma_pio_spi_flight_clock_latch_offset;
    switch (phys->flight_normal_capture_restore_stage) {
    case 0u:
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, sm, false);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, sm);
        pio_sm_restart(BOARD_TDMA_SPI_PIO, sm);
        phys->flight_normal_capture_restore_stage = 1u;
        return true;
    case 1u:
    case 2u:
    case 3u:
    case 4u: {
        const uint32_t index =
            phys->flight_normal_capture_restore_stage - 1u;
        BOARD_TDMA_SPI_PIO->instr_mem[offset + index] =
            phys->flight_sck_waveform_saved_instructions[index];
        phys->flight_normal_capture_restore_stage++;
        return true;
    }
    case 5u:
        tdma_pio_spi_flight_clock_latch_program_init(
            BOARD_TDMA_SPI_PIO, sm, offset,
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
    const uint sm = BOARD_TDMA_SPI_CAPTURE_SM;
    const uint offset = s_tdma_pio_spi_flight_clock_latch_offset;
    if (phys->flight_sck_waveform_capture_state ==
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_REQUESTED) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, sm, false);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, sm);
        pio_sm_restart(BOARD_TDMA_SPI_PIO, sm);
        for (uint32_t index = 0u; index < 4u; index++) {
            phys->flight_sck_waveform_saved_instructions[index] =
                BOARD_TDMA_SPI_PIO->instr_mem[offset + index];
        }

        /* The product persona already occupies all 32 PIO instructions.
         * Reuse only the capture SM's four-instruction latch region for this
         * bounded job. Persona patch and SM configuration are deliberately
         * separate core1 beats so neither can exceed the calibration phase
         * WCET or borrow a later load's budget. */
        BOARD_TDMA_SPI_PIO->instr_mem[offset + 0u] =
            (uint16_t)pio_encode_wait_gpio(false, phys->rx_csn_pin);
        BOARD_TDMA_SPI_PIO->instr_mem[offset + 1u] =
            (uint16_t)pio_encode_in(pio_pins, 1u);
        BOARD_TDMA_SPI_PIO->instr_mem[offset + 2u] =
            (uint16_t)pio_encode_nop();
        BOARD_TDMA_SPI_PIO->instr_mem[offset + 3u] =
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
        pio_sm_init(BOARD_TDMA_SPI_PIO, sm, offset, &config);
        phys->flight_sck_waveform_capture_deadline_us =
            tdma_pio_spi_phys_now_us() +
            TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_TIMEOUT_US;
        phys->flight_sck_waveform_capture_state =
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED;
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, sm, true);
        return phys->flight_sck_waveform_capture_state;
    }
    if (phys->flight_sck_waveform_capture_state !=
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED) {
        return phys->flight_sck_waveform_capture_state;
    }
    if (pio_sm_get_rx_fifo_level(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM) >=
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

static void tdma_pio_spi_phys_flight_origin_recover(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return;
    }
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    const uint32_t sm_mask = (1u << phys->tx_sm) | (1u << phys->rx_sm) |
                             (1u << BOARD_TDMA_SPI_RTT_SM);
    pio_set_sm_mask_enabled(BOARD_TDMA_SPI_PIO, sm_mask, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
    /* Recovery preserves the flight contract: core1 never drives a control
     * edge. Restore {CS=1,SCK=0} through the origin control SM before it is
     * re-enabled at its blocking PULL. */
    pio_sm_set_pins_with_mask64(
        BOARD_TDMA_SPI_PIO,
        phys->rx_sm,
        1ull << phys->tx_csn_pin,
        (1ull << phys->tx_sck_pin) | (1ull << phys->tx_csn_pin));
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 1u);
    BOARD_TDMA_SPI_PIO->fdebug =
        tdma_pio_spi_phys_txstall_mask(phys->rx_sm);
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO, sm_mask);
    phys->snapshot.origin_recovery_count++;
}

static bool tdma_pio_spi_phys_configure_flight(
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

    if (phys->role == TDMA_PIO_SPI_ROLE_MASTER) {
        /* rx_sm owns the generated forward clock and returned-DATA RX FIFO;
         * tx_sm owns the reverse DATA stream supplied by TX DMA. */
        phys->rx_sm = BOARD_TDMA_SPI_MASTER_SM;
        phys->tx_sm = BOARD_TDMA_SPI_SLAVE_SM;
        tdma_pio_spi_flight_origin_clock_rx_program_init(
            BOARD_TDMA_SPI_PIO,
            phys->rx_sm,
            s_tdma_pio_spi_flight_origin_clock_offset,
            phys->rx_pin,
            phys->tx_sck_pin,
            phys->tx_csn_pin,
            phys->baud_hz);
        tdma_pio_spi_flight_origin_data_tx_program_init(
            BOARD_TDMA_SPI_PIO,
            phys->tx_sm,
            s_tdma_pio_spi_flight_origin_data_offset,
            phys->tx_pin,
            phys->rx_pin,
            phys->rx_csn_pin,
            phys->rx_sck_pin,
            phys->flight_sck_phase_delay_cycles,
            phys->flight_data_phase_delay_cycles);
        tdma_pio_spi_flight_origin_rtt_program_init(
            BOARD_TDMA_SPI_PIO,
            BOARD_TDMA_SPI_RTT_SM,
            s_tdma_pio_spi_flight_origin_rtt_offset,
            phys->tx_csn_pin,
            phys->rx_csn_pin);
        if (!tdma_pio_spi_phys_ensure_tx_dma()) {
            return false;
        }
    } else {
        /* rx_sm performs DATA capture/reverse forwarding. tx_sm independently
         * regenerates the complete forward MARK/SCK control pair. */
        phys->rx_sm = BOARD_TDMA_SPI_MASTER_SM;
        phys->tx_sm = BOARD_TDMA_SPI_SLAVE_SM;
        if (phys->process_image_enabled) {
            tdma_pio_spi_flight_process_follower_program_init(
                BOARD_TDMA_SPI_PIO,
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
                BOARD_TDMA_SPI_PIO,
                phys->rx_sm,
                s_tdma_pio_spi_flight_data_follower_offset,
                phys->rx_pin,
                phys->tx_pin,
                phys->rx_sck_pin,
                phys->flight_data_phase_delay_cycles);
        }
        tdma_pio_spi_flight_control_forward_program_init(
            BOARD_TDMA_SPI_PIO,
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
        BOARD_TDMA_SPI_PIO,
        BOARD_TDMA_SPI_CAPTURE_SM,
        s_tdma_pio_spi_flight_clock_latch_offset,
        phys->role == TDMA_PIO_SPI_ROLE_MASTER
            ? phys->tx_csn_pin
            : phys->rx_csn_pin);
    tdma_pio_spi_phys_prepare_sm_pair(phys);
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
        phys->process_image_enabled) {
        /* Initialize the elastic tail outside the wire loop, leaving the PIO
         * instruction budget to the independent control and DATA paths. */
        pio_sm_exec(BOARD_TDMA_SPI_PIO, phys->rx_sm,
                    pio_encode_set(pio_y, 0u));
    }
    return true;
}

static bool tdma_pio_spi_phys_start_overlay_script(
    tdma_pio_spi_phys_t *phys,
    uint32_t *script,
    uint32_t script_words,
    uint32_t buffer_index)
{
    if (phys == NULL || script == NULL || script_words == 0u ||
        script_words > TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS ||
        buffer_index >= 2u || s_tdma_pio_spi_tx_dma_channel < 0) {
        if (phys != NULL) {
            phys->snapshot.overlay_last_error =
                TDMA_PIO_SPI_OVERLAY_ERROR_DMA_START_INVALID;
        }
        return false;
    }
    /* A busy DMA channel is normal while the previous frame drains.  The
     * caller keeps the prepared script in the other resident buffer and
     * retries from core1 service; it never waits here. */
    if (dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
        phys->snapshot.overlay_last_error =
            TDMA_PIO_SPI_OVERLAY_ERROR_DMA_BUSY_TIMEOUT;
        phys->snapshot.overlay_tx_dma_remaining =
            dma_hw->ch[s_tdma_pio_spi_tx_dma_channel].transfer_count;
        phys->snapshot.overlay_tx_dma_busy = 1u;
        phys->snapshot.overlay_tx_fifo_level_at_fail =
            pio_sm_get_tx_fifo_level(BOARD_TDMA_SPI_PIO, phys->rx_sm);
        phys->snapshot.overlay_prepare_wait_us = 0u;
        return false;
    }
    dma_channel_config dma_cfg = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_tx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(
        &dma_cfg,
        pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, true));
    dma_channel_configure(
        (uint)s_tdma_pio_spi_tx_dma_channel,
        &dma_cfg,
        &BOARD_TDMA_SPI_PIO->txf[phys->rx_sm],
        script,
        script_words,
        true);
    phys->flight_overlay_active_buffer = buffer_index;
    phys->flight_overlay_pending = false;
    phys->flight_overlay_pending_words = 0u;
    phys->snapshot.overlay_tx_dma_remaining = script_words;
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
    uint32_t buffer_index,
    uint32_t script_words)
{
    if (phys == NULL || buffer_index >= 2u || script_words == 0u ||
        script_words > TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS) {
        return false;
    }
    /* There is only one future frame at a time.  Never overwrite a script
     * that is already waiting for the DMA channel: doing so would make the
     * PIO command stream depend on parser/service jitter. */
    if (phys->flight_overlay_pending) {
        return false;
    }
    if (dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
        phys->flight_overlay_pending_buffer = buffer_index;
        phys->flight_overlay_pending_words = script_words;
        phys->flight_overlay_pending = true;
        phys->snapshot.overlay_last_error =
            TDMA_PIO_SPI_OVERLAY_ERROR_DMA_BUSY_TIMEOUT;
        phys->snapshot.overlay_tx_dma_busy = 1u;
        phys->snapshot.overlay_tx_dma_remaining =
            dma_hw->ch[s_tdma_pio_spi_tx_dma_channel].transfer_count;
        return true;
    }
    return tdma_pio_spi_phys_start_overlay_script(
        phys,
        s_tdma_pio_spi_flight_overlay_script[buffer_index],
        script_words,
        buffer_index);
}

static void tdma_pio_spi_phys_service_overlay_pending(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || !phys->flight_overlay_pending ||
        s_tdma_pio_spi_tx_dma_channel < 0 ||
        dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
        return;
    }
    const uint32_t buffer_index = phys->flight_overlay_pending_buffer;
    const uint32_t script_words = phys->flight_overlay_pending_words;
    if (tdma_pio_spi_phys_start_overlay_script(
            phys,
            s_tdma_pio_spi_flight_overlay_script[buffer_index],
            script_words,
            buffer_index)) {
        phys->flight_overlay_pending = false;
        phys->flight_overlay_pending_words = 0u;
    }
}

static bool tdma_pio_spi_phys_prepare_pass_overlay(
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
    uint32_t *script = s_tdma_pio_spi_flight_overlay_script[buffer_index];
    memset(script,
           0,
           (phys->flight_physical_byte_count + 1u) *
               sizeof(script[0]));
    script[phys->flight_physical_byte_count] = TDMA_FLIGHT_OVERLAY_SCRIPT_END;
    return tdma_pio_spi_phys_queue_overlay_script(
        phys, buffer_index, phys->flight_physical_byte_count + 1u);
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
    const bool boundary_observed =
        pio_interrupt_get(BOARD_TDMA_SPI_PIO, 3u);
    if (boundary_observed) {
        /* IRQ3 is raised only after the fixed physical byte count and CS
         * rising edge.  RX DMA publication can become visible a bounded
         * number of core1 service passes later than this edge.  Record an
         * explicit grace state instead of committing PASS immediately:
         * otherwise the late parser result is coalesced behind PASS and this
         * Node's mailbox is absent from an otherwise transport-valid process
         * image. */
        pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 3u);
        phys->snapshot.overlay_frame_boundary_count++;
        phys->flight_overlay_boundary_pending = true;
        phys->flight_overlay_grace_remaining =
            TDMA_PIO_SPI_OVERLAY_GRACE_SERVICE_PASSES;
    }
    if (!phys->flight_overlay_boundary_pending) {
        return true;
    }
    if (phys->flight_overlay_next_prepared) {
        phys->flight_overlay_next_prepared = false;
        phys->flight_overlay_boundary_pending = false;
        phys->flight_overlay_grace_remaining = 0u;
        return true;
    }
    /* A prepared script may still be waiting for the DMA channel.  It is
     * already the successor for this boundary; do not replace it with PASS
     * merely because the bounded service pass observed the channel busy. */
    if (phys->flight_overlay_pending) {
        phys->flight_overlay_next_prepared = false;
        phys->flight_overlay_boundary_pending = false;
        phys->flight_overlay_grace_remaining = 0u;
        return true;
    }
    if (phys->flight_overlay_grace_remaining != 0u) {
        phys->flight_overlay_grace_remaining--;
        return true;
    }
    /* No complete parser result arrived within the fixed grace.  PASS is a
     * bounded recovery action for an actually missing/bad frame, not the
     * normal response to RX-DMA publication latency. */
    if (!tdma_pio_spi_phys_prepare_pass_overlay(phys)) {
        phys->snapshot.overlay_prepare_fail_count++;
        return false;
    }
    phys->flight_overlay_pass_committed = true;
    phys->flight_overlay_boundary_pending = false;
    phys->flight_overlay_grace_remaining = 0u;
    phys->snapshot.overlay_pass_recovery_count++;
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
    uint32_t data_phase_delay_cycles)
{
    if (phys == NULL || phys->armed || marker_phase_delay_cycles == 0u ||
        sck_phase_delay_cycles == 0u || data_phase_delay_cycles == 0u ||
        marker_phase_delay_cycles > 31u ||
        sck_phase_delay_cycles > 31u ||
        data_phase_delay_cycles > 31u) {
        return false;
    }
    phys->flight_marker_offset_sample_count = marker_offset_sample_count;
    phys->flight_sck_offset_sample_count = sck_offset_sample_count;
    phys->flight_data_offset_sample_count = data_offset_sample_count;
    phys->flight_marker_phase_delay_cycles = marker_phase_delay_cycles;
    phys->flight_sck_phase_delay_cycles = sck_phase_delay_cycles;
    phys->flight_data_phase_delay_cycles = data_phase_delay_cycles;
    return true;
}

bool tdma_pio_spi_phys_prepare_process_overlay(
    void *context,
    const uint8_t *incoming_packet,
    const uint8_t *processed_packet,
    size_t packet_size)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->armed || !phys->process_image_enabled ||
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
    /* A boundary recovery may already have committed PASS for the upcoming
     * frame. During the idle-high inter-frame gap its DMA remains busy because
     * the process SM is waiting for SCK, so a late decode of the preceding
     * frame cannot replace that script. Coalesce that stale result instead of
     * blocking core1 for the generic DMA timeout or reporting a wire failure.
     * Once CS is active, the committed PASS is draining and the normal bounded
     * wait below may prepare the following frame's overlay. */
    if (phys->flight_overlay_pass_committed &&
        s_tdma_pio_spi_tx_dma_channel >= 0 &&
        dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel) &&
        gpio_get(phys->rx_csn_pin)) {
        phys->snapshot.overlay_late_coalesce_count++;
        return true;
    }
    if (phys->flight_overlay_pass_committed &&
        (s_tdma_pio_spi_tx_dma_channel < 0 ||
         !dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel))) {
        phys->flight_overlay_pass_committed = false;
    }
    /* Keep at most one successor script.  The PIO stream is strictly
     * serial, so a second parser result before the pending script is armed is
     * stale with respect to the next boundary and must not overwrite it. */
    if (phys->flight_overlay_pending) {
        phys->snapshot.overlay_late_coalesce_count++;
        return true;
    }
    if (s_tdma_pio_spi_tx_dma_channel < 0) {
        phys->snapshot.overlay_prepare_fail_count++;
        phys->snapshot.overlay_last_error =
            TDMA_PIO_SPI_OVERLAY_ERROR_DMA_START_INVALID;
        return false;
    }
    const uint32_t buffer_index =
        tdma_pio_spi_phys_overlay_free_buffer(phys);
    uint32_t *script = s_tdma_pio_spi_flight_overlay_script[buffer_index];
    phys->snapshot.overlay_alignment_byte_shift =
        phys->flight_alignment_byte_shift;
    phys->snapshot.overlay_alignment_bit_shift =
        phys->flight_alignment_bit_shift;
    tdma_flight_overlay_result_t overlay;
    if (!tdma_flight_overlay_build(
            incoming_packet,
            processed_packet,
            packet_size,
            TDMA_PIO_SPI_PACKET_HEADER_SIZE,
            phys->flight_alignment_byte_shift,
            phys->flight_alignment_bit_shift,
            phys->flight_physical_byte_count,
            script,
            TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS,
            &overlay)) {
        phys->snapshot.overlay_last_error =
            TDMA_PIO_SPI_OVERLAY_ERROR_BUILD_FAILED;
        phys->snapshot.overlay_tx_dma_remaining =
            s_tdma_pio_spi_tx_dma_channel >= 0
                ? dma_hw->ch[s_tdma_pio_spi_tx_dma_channel].transfer_count
                : 0u;
        phys->snapshot.overlay_tx_dma_busy =
            s_tdma_pio_spi_tx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)
                ? 1u
                : 0u;
        phys->snapshot.overlay_tx_fifo_level_at_fail =
            pio_sm_get_tx_fifo_level(BOARD_TDMA_SPI_PIO, phys->rx_sm);
        phys->snapshot.overlay_prepare_wait_us = 0u;
        phys->snapshot.overlay_prepare_fail_count++;
        return false;
    }
    if (!tdma_pio_spi_phys_queue_overlay_script(
            phys, buffer_index, phys->flight_physical_byte_count + 1u)) {
        phys->snapshot.overlay_prepare_fail_count++;
        return false;
    }
    phys->flight_overlay_next_prepared = true;
    phys->flight_overlay_pass_committed = false;
    phys->snapshot.overlay_prepare_count++;
    phys->snapshot.overlay_replacement_byte_count +=
        overlay.replacement_byte_count;
    phys->snapshot.overlay_alignment_byte_shift =
        overlay.alignment_byte_shift;
    phys->snapshot.overlay_alignment_bit_shift = overlay.alignment_bit_shift;
    phys->snapshot.overlay_physical_byte_count =
        overlay.physical_byte_count;
    return true;
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

static bool tdma_pio_spi_phys_rx_arm(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || !tdma_pio_spi_phys_ensure_rx_dma()) {
        return false;
    }
    dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    tdma_pio_spi_phys_rx_prepare(phys);
    memset(s_tdma_pio_spi_rx_ring, 0, sizeof(s_tdma_pio_spi_rx_ring));
    s_tdma_pio_spi_rx_scan_produced = 0u;
    if (!tdma_rx_sequence_reset(&s_tdma_pio_spi_rx_sequence,
                                TDMA_PIO_SPI_RX_RING_WORDS,
                                0u,
                                0u)) {
        return false;
    }
    dma_channel_config dma_cfg =
        dma_channel_get_default_config((uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_ring(&dma_cfg, true, TDMA_PIO_SPI_RX_RING_LOG2);
    const uint32_t capture_sm =
        s_tdma_pio_spi_program_persona ==
                TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN
            ? phys->tx_sm
            : phys->rx_sm;
    channel_config_set_dreq(&dma_cfg,
                            pio_get_dreq(BOARD_TDMA_SPI_PIO,
                                         capture_sm,
                                         false));
    dma_channel_configure(
        (uint)s_tdma_pio_spi_rx_dma_channel,
        &dma_cfg,
        s_tdma_pio_spi_rx_ring,
        &BOARD_TDMA_SPI_PIO->rxf[capture_sm],
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

static uint64_t tdma_pio_spi_phys_rx_produced_words(
    const tdma_pio_spi_phys_t *phys)
{
    const uint32_t write_index = tdma_pio_spi_phys_rx_write_index();
    const bool fixed_frames = phys != NULL && phys->process_image_enabled &&
        phys->flight_physical_byte_count != 0u;
    const uint32_t complete_frames = !fixed_frames
        ? 0u
        : (phys->role == TDMA_PIO_SPI_ROLE_MASTER
               ? phys->snapshot.tx_count
               : phys->snapshot.overlay_frame_boundary_count);
    const uint32_t frame_words = fixed_frames
        ? phys->flight_physical_byte_count : 0u;
    uint64_t produced = s_tdma_pio_spi_rx_sequence.produced_words;
    if (!tdma_rx_sequence_observe(&s_tdma_pio_spi_rx_sequence,
                                  write_index,
                                  complete_frames,
                                  frame_words,
                                  &produced)) {
        return s_tdma_pio_spi_rx_sequence.produced_words;
    }
    return produced;
}

static uint32_t tdma_pio_spi_phys_rx_ring_word(uint64_t produced)
{
    return s_tdma_pio_spi_rx_ring[produced &
                                  (TDMA_PIO_SPI_RX_RING_WORDS - 1u)];
}

static uint8_t tdma_pio_spi_phys_rx_ring_byte(uint64_t produced)
{
    return (uint8_t)(tdma_pio_spi_phys_rx_ring_word(produced) & 0xFFu);
}

static uint8_t tdma_pio_spi_phys_rx_ring_aligned_byte(uint64_t produced,
                                                      uint32_t bit_shift)
{
    const uint8_t first = tdma_pio_spi_phys_rx_ring_byte(produced);
    if (bit_shift == 0u) {
        return first;
    }
    const uint8_t second = tdma_pio_spi_phys_rx_ring_byte(produced + 1u);
    return (uint8_t)(((uint32_t)first << bit_shift) |
                     ((uint32_t)second >> (8u - bit_shift)));
}

static bool tdma_pio_spi_phys_transport_header_matches(
    uint64_t packet_start,
    uint32_t bit_shift,
    uint16_t frame_size)
{
    if (frame_size < TDMA_TRANSPORT_FRAME_HEADER_SIZE) {
        return false;
    }
    const uint16_t transport_magic =
        (uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
            packet_start, bit_shift) |
        ((uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
             packet_start + 1u, bit_shift) << 8u);
    const uint16_t transport_size =
        (uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
            packet_start + 4u, bit_shift) |
        ((uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
             packet_start + 5u, bit_shift) << 8u);
    const uint8_t frame_class =
        tdma_pio_spi_phys_rx_ring_aligned_byte(
            packet_start + 3u, bit_shift);

    return transport_magic == TDMA_TRANSPORT_FRAME_MAGIC &&
           tdma_pio_spi_phys_rx_ring_aligned_byte(
               packet_start + 2u, bit_shift) ==
               TDMA_TRANSPORT_FRAME_VERSION &&
           (frame_class == TDMA_TRANSPORT_FRAME_CLASS_SHORT ||
            frame_class == TDMA_TRANSPORT_FRAME_CLASS_LONG) &&
           transport_size == frame_size &&
           tdma_pio_spi_phys_rx_ring_aligned_byte(
               packet_start + 6u, bit_shift) ==
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
    const uint64_t produced = tdma_pio_spi_phys_rx_produced_words(phys);
    phys->snapshot.rx_dma_produced_words = (uint32_t)produced;
    phys->snapshot.rx_scan_produced_words =
        (uint32_t)s_tdma_pio_spi_rx_scan_produced;
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

    uint64_t candidate = s_tdma_pio_spi_rx_scan_produced;
    while (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE <= produced) {
        for (uint32_t bit_shift = 0u; bit_shift < 8u; bit_shift++) {
            const uint32_t alignment_extra = bit_shift == 0u ? 0u : 1u;
            if (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE +
                    alignment_extra > produced ||
                tdma_pio_spi_phys_rx_ring_aligned_byte(
                    candidate, bit_shift) != TDMA_PIO_SPI_PACKET_MAGIC0 ||
                tdma_pio_spi_phys_rx_ring_aligned_byte(
                    candidate + 1u, bit_shift) !=
                    TDMA_PIO_SPI_PACKET_MAGIC1) {
                continue;
            }
            const uint16_t frame_size =
                (uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
                    candidate + 2u, bit_shift) |
                ((uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
                     candidate + 3u, bit_shift) << 8u);
            const uint32_t total_words =
                TDMA_PIO_SPI_PACKET_HEADER_SIZE + frame_size;
            if (frame_size == 0u || total_words > max_words) {
                continue;
            }
            if (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE +
                    TDMA_TRANSPORT_FRAME_HEADER_SIZE + alignment_extra >
                produced) {
                s_tdma_pio_spi_rx_scan_produced = candidate;
                return false;
            }
            if (!tdma_pio_spi_phys_transport_header_matches(
                    candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE,
                    bit_shift,
                    frame_size)) {
                continue;
            }
            if (candidate + total_words + alignment_extra > produced) {
                /* Header is valid, but the last bytes are still on the wire. */
                s_tdma_pio_spi_rx_scan_produced = candidate;
                return false;
            }
            for (uint32_t i = 0u; i < total_words; i++) {
                s_tdma_pio_spi_rx_frame[i] = (uint32_t)
                    tdma_pio_spi_phys_rx_ring_aligned_byte(
                        candidate + i, bit_shift);
            }
            /* A shifted aligned byte uses raw[i] and raw[i+1].  The final
             * raw word is therefore also the first word for the next aligned
             * byte/frame; consuming alignment_extra here skips every next
             * frame when the origin capture has no idle raw byte between
             * frames. */
            s_tdma_pio_spi_rx_scan_produced = candidate + total_words;
            if (phys->process_image_enabled &&
                phys->flight_physical_byte_count != 0u) {
                phys->flight_alignment_byte_shift =
                    (uint32_t)(candidate %
                               phys->flight_physical_byte_count);
                phys->flight_alignment_bit_shift = bit_shift;
            }
            if (bit_shift == 0u) {
                phys->snapshot.rx_magic_at_zero++;
            } else {
                phys->snapshot.rx_magic_at_shift++;
            }
            *received_words = total_words;
            return true;
        }
        candidate++;
    }
    /* A shifted four-byte outer header consumes one additional raw word.
     * Preserve that full prefix across service passes: retaining only the
     * two-byte magic can discard a valid candidate when DMA publication is
     * observed after four raw words but before the fifth word arrives. */
    const uint64_t retain_words =
        TDMA_PIO_SPI_PACKET_HEADER_SIZE + 1u;
    const uint64_t bad_start = s_tdma_pio_spi_rx_scan_produced;
    const uint64_t bad_available = produced - bad_start;
    phys->snapshot.last_bad_header0 =
        bad_available > 0u ? tdma_pio_spi_phys_rx_ring_word(bad_start) : 0u;
    phys->snapshot.last_bad_header1 =
        bad_available > 1u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 1u) : 0u;
    phys->snapshot.last_bad_header2 =
        bad_available > 2u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 2u) : 0u;
    phys->snapshot.last_bad_header3 =
        bad_available > 3u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 3u) : 0u;
    phys->snapshot.last_bad_words = (uint32_t)bad_available;
    phys->snapshot.rx_magic_fail_count++;
    s_tdma_pio_spi_rx_scan_produced =
        produced > retain_words ? produced - retain_words : 0u;
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
    phys->role = (config->local_slot_id == config->reference_slot_id)
                     ? TDMA_PIO_SPI_ROLE_MASTER
                     : TDMA_PIO_SPI_ROLE_SLAVE;
    phys->baud_hz = config->baud_hz;
    const uint32_t clk_sys_hz = clock_get_hz(clk_sys);
    const uint32_t period_cycles = phys->baud_hz == 0u
        ? 0u : clk_sys_hz / phys->baud_hz;
    const uint32_t half_period_cycles = phys->baud_hz == 0u
        ? 0u : clk_sys_hz / (2u * phys->baud_hz);
    if (period_cycles == 0u || half_period_cycles == 0u ||
        (phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
         phys->flight_sck_phase_delay_cycles +
              TDMA_PIO_SPI_FLIGHT_SCK_REARM_CYCLES > half_period_cycles) ||
        phys->flight_data_phase_delay_cycles +
            TDMA_PIO_SPI_FLIGHT_DATA_REARM_CYCLES > period_cycles) {
        return false;
    }
    phys->node_count = config->node_count;
    phys->flight_tail_bytes = tdma_pio_spi_phys_flight_tail_bytes(config);
    if (phys->flight_tail_bytes > TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES) {
        return false;
    }
    const tdma_pio_spi_program_persona_t flight_persona =
        phys->role == TDMA_PIO_SPI_ROLE_MASTER
            ? TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN
            : (phys->process_image_enabled
                   ? TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER
                   : TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER);
    const uint32_t flight_payload_size = phys->flight_payload_size != 0u
        ? phys->flight_payload_size : TDMA_TRANSPORT_SHORT_PAYLOAD_MAX;
    phys->flight_physical_byte_count =
        TDMA_PIO_SPI_PACKET_HEADER_SIZE +
        TDMA_TRANSPORT_FRAME_HEADER_SIZE +
        flight_payload_size +
        phys->flight_tail_bytes;
    if (phys->process_image_enabled) {
        if (phys->flight_physical_byte_count + 1u >
            TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS) {
            return false;
        }
    }
    phys->flight_alignment_byte_shift = 0u;
    phys->flight_alignment_bit_shift = 0u;
    phys->flight_overlay_next_prepared = false;
    phys->flight_overlay_pass_committed = false;
    phys->flight_overlay_boundary_pending = false;
    phys->flight_overlay_grace_remaining = 0u;
    phys->flight_overlay_pending = false;
    phys->flight_overlay_active_buffer = 0u;
    phys->flight_overlay_pending_buffer = 0u;
    phys->flight_overlay_pending_words = 0u;
    phys->flight_tx_pending = false;
    phys->flight_tx_completion_pending = false;
    phys->flight_tx_completion_timestamp_ns = 0ull;
    phys->flight_tx_packet_size = 0u;
    phys->flight_tx_wire_bytes = 0u;
    phys->flight_tx_launch_timestamp_ns = 0ull;
    phys->flight_tx_deadline_ns = 0ull;
    phys->flight_sck_waveform_capture_state =
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE;
    phys->flight_sck_waveform_capture_deadline_us = 0ull;
    phys->flight_normal_capture_copy_stage = 0u;
    phys->flight_normal_capture_sck_cursor = 0u;
    phys->flight_normal_capture_restore_stage = 0u;
    phys->flight_normal_capture_rx_produced = 0u;
    phys->flight_normal_capture_rx_start = 0u;
    phys->flight_normal_capture_rx_count = 0u;
    phys->flight_normal_capture_rx_cursor = 0u;
    if (!tdma_pio_spi_phys_select_program_persona(phys, flight_persona) ||
        !tdma_pio_spi_phys_configure_flight(phys, config)) {
        return false;
    }
    tdma_pio_spi_phys_set_line_drivers(true);
    if (!tdma_pio_spi_phys_rx_arm(phys)) {
        tdma_pio_spi_phys_set_line_drivers(false);
        return false;
    }
    /* rx_arm clears both FIFOs of the capture/forward SM. Prime the process
     * command DMA only after that reset so its initial PASS script survives. */
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
        phys->process_image_enabled &&
        !tdma_pio_spi_phys_prepare_pass_overlay(phys)) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        tdma_pio_spi_phys_set_line_drivers(false);
        return false;
    }
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        const uint32_t control_bits = phys->flight_physical_byte_count * 8u;
        if (control_bits == 0u) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
            tdma_pio_spi_phys_set_line_drivers(false);
            return false;
        }
        pio_sm_put_blocking(BOARD_TDMA_SPI_PIO,
                            phys->tx_sm,
                            control_bits - 1u);
        pio_sm_exec(BOARD_TDMA_SPI_PIO,
                    phys->tx_sm,
                    pio_encode_pull(false, true));
    }
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 3u);
    if (!tdma_pio_spi_phys_clock_latch_rearm(phys)) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        tdma_pio_spi_phys_set_line_drivers(false);
        return false;
    }
    tdma_pio_spi_phys_enable_sm_pair(phys);

    tdma_pio_spi_phys_reset_normal_capture();
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
    phys->snapshot.clock_latch_count = 0u;
    phys->snapshot.clock_latch_miss_count = 0u;
    phys->snapshot.origin_done_irq_count = 0u;
    phys->snapshot.origin_done_txstall_count = 0u;
    phys->snapshot.origin_clock_timeout_count = 0u;
    phys->snapshot.origin_data_timeout_count = 0u;
    phys->snapshot.origin_recovery_count = 0u;
    phys->snapshot.overlay_prepare_count = 0u;
    phys->snapshot.overlay_prepare_fail_count = 0u;
    phys->snapshot.overlay_replacement_byte_count = 0u;
    phys->snapshot.overlay_alignment_byte_shift = 0u;
    phys->snapshot.overlay_alignment_bit_shift = 0u;
    phys->snapshot.overlay_physical_byte_count =
        phys->flight_physical_byte_count;
    phys->snapshot.overlay_last_error = TDMA_PIO_SPI_OVERLAY_ERROR_NONE;
    phys->snapshot.overlay_tx_dma_remaining = 0u;
    phys->snapshot.overlay_tx_dma_busy = 0u;
    phys->snapshot.overlay_tx_fifo_level_at_fail = 0u;
    phys->snapshot.overlay_prepare_wait_us = 0u;
    phys->snapshot.overlay_program_offset =
        phys->role == TDMA_PIO_SPI_ROLE_SLAVE && phys->process_image_enabled
            ? s_tdma_pio_spi_flight_process_follower_offset
            : 0u;
    phys->snapshot.overlay_tx_dma_read_index = 0u;
    phys->snapshot.overlay_tx_dma_ctrl = 0u;
    phys->snapshot.overlay_sm_shiftctrl = 0u;
    phys->snapshot.overlay_sm_execctrl = 0u;
    phys->snapshot.overlay_sm_pc_at_fail = 0u;
    phys->snapshot.overlay_pio_ctrl_at_fail = 0u;
    phys->snapshot.overlay_pio_fstat_at_fail = 0u;
    phys->snapshot.overlay_pio_fdebug_at_fail = 0u;
    phys->snapshot.overlay_frame_boundary_count = 0u;
    phys->snapshot.overlay_pass_recovery_count = 0u;
    phys->snapshot.overlay_late_coalesce_count = 0u;
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
    if (phys == NULL) {
        return;
    }
    /* Process-image followers keep the overlay TX DMA blocked on the PIO TX
     * FIFO between frames.  A failed ARM can also leave a DMA or SM active
     * before phys->armed is published.  STOP is the common idempotent
     * rollback for both states, so hardware cleanup must not be conditional
     * on the software armed flag. */
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    if (phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_PATCHED ||
        phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED ||
        phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY) {
        (void)tdma_pio_spi_phys_restore_clock_latch(phys, false);
    }
    tdma_pio_spi_phys_set_line_drivers(false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
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
    phys->flight_clock_latch_armed = false;
    phys->flight_sck_waveform_capture_state =
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE;
    phys->flight_sck_waveform_capture_deadline_us = 0ull;
    phys->rx_capture_active = false;
    phys->flight_overlay_pending = false;
    phys->flight_overlay_pending_words = 0u;
    phys->flight_overlay_next_prepared = false;
    phys->flight_overlay_pass_committed = false;
    phys->flight_overlay_boundary_pending = false;
    phys->flight_overlay_grace_remaining = 0u;
    phys->flight_tx_pending = false;
    phys->flight_tx_completion_pending = false;
    phys->flight_tx_packet_size = 0u;
    phys->flight_tx_wire_bytes = 0u;
    phys->flight_tx_deadline_ns = 0ull;
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

    /* Clock training replaces the complete resident flight persona.  The
     * product persona may have both the process-overlay TX DMA and the
     * independent CS clock-latch SM active, while the reference also owns
     * the RTT SM.  Quiesce every TDMA-owned execution resource before asking
     * the PIO allocator to remove and replace the program set. */
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    phys->rx_capture_active = false;
    phys->flight_clock_latch_armed = false;
    const uint32_t owned_sm_mask =
        (1u << BOARD_TDMA_SPI_MASTER_SM) |
        (1u << BOARD_TDMA_SPI_SLAVE_SM) |
        (1u << BOARD_TDMA_SPI_CAPTURE_SM) |
        (1u << BOARD_TDMA_SPI_RTT_SM);
    pio_set_sm_mask_enabled(BOARD_TDMA_SPI_PIO, owned_sm_mask, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
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
        phys->cal_loopback.armed != 0u ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING) {
        return false;
    }
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE)) {
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
        if (phys->cal_loopback_stop_pending &&
            s_tdma_pio_spi_program_persona ==
                TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE) {
            (void)tdma_pio_spi_phys_select_program_persona(
                phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
        }
        phys->cal_loopback_start_pending = false;
        phys->cal_loopback_stop_pending = false;
        return;
    }
    if (phys->cal_loopback.armed != 0u &&
        s_tdma_pio_spi_rx_dma_channel >= 0 &&
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count == 0u) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->cal_loopback_tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
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
        (void)tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    }
    if (!phys->cal_loopback_start_pending || phys->cal_loopback.armed != 0u) return;
    const uint tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    const uint responder_sm = BOARD_TDMA_SPI_SLAVE_SM;
    const uint capture_sm = BOARD_TDMA_SPI_CAPTURE_SM;
    if (!tdma_pio_spi_phys_ensure_rx_dma()) {
        phys->cal_loopback_start_pending = false;
        tdma_pio_spi_phys_cal_reject(phys, phys->cal_loopback_epoch, 1u);
        tdma_pio_spi_phys_cal_cleanup(phys);
        (void)tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
        return;
    }
    tdma_pio_spi_p3_initiator_program_init(
        BOARD_TDMA_SPI_PIO, tx_sm, s_tdma_pio_spi_p3_initiator_offset,
        BOARD_TDMA_SPI_DOWNLINK_CSN_PIN,
        BOARD_TDMA_SPI_DOWNLINK_SCK_PIN,
        BOARD_TDMA_SPI_BAUD_HZ);
    tdma_pio_spi_p3_responder_program_init(
        BOARD_TDMA_SPI_PIO, responder_sm,
        s_tdma_pio_spi_p3_responder_offset,
        BOARD_TDMA_SPI_UPLINK_CSN_PIN,
        BOARD_TDMA_SPI_UPLINK_SCK_PIN,
        BOARD_TDMA_SPI_DOWNLINK_TX_PIN,
        BOARD_TDMA_SPI_BAUD_HZ);
    tdma_pio_spi_cal_loopback_capture_program_init(
        BOARD_TDMA_SPI_PIO, capture_sm, s_tdma_pio_spi_p3_capture_offset,
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
    phys->cal_loopback.flags = TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_PIO_DMA;
    phys->cal_loopback.epoch = phys->cal_loopback_epoch;
    phys->cal_loopback_tx_sm = tx_sm;
    phys->cal_loopback_capture_sm = capture_sm;
    phys->cal_loopback_start_pending = false;
    tdma_pio_spi_phys_cal_write_end(phys);
    pio_sm_put(BOARD_TDMA_SPI_PIO, tx_sm, 15u);
    pio_sm_put(BOARD_TDMA_SPI_PIO, responder_sm, 15u);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                               (1u << tx_sm) | (1u << responder_sm) |
                               (1u << capture_sm));
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

static void tdma_pio_spi_phys_prepare_maintenance_mapping(
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
}

static void tdma_pio_spi_phys_reset_normal_capture(void)
{
    (void)__atomic_add_fetch(&s_tdma_pio_spi_tx_history_guard,
                             1u, __ATOMIC_ACQ_REL);
    memset(s_tdma_pio_spi_tx_last_frame, 0,
           sizeof(s_tdma_pio_spi_tx_last_frame));
    s_tdma_pio_spi_tx_history_produced = 0u;
    s_tdma_pio_spi_tx_last_frame_bytes = 0u;
    s_tdma_pio_spi_tx_complete_frame_count = 0u;
    (void)__atomic_add_fetch(&s_tdma_pio_spi_tx_history_guard,
                             1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_record_complete_tx_frame(
    const uint8_t *header,
    const uint8_t *packet,
    size_t packet_size)
{
    const uint32_t frame_bytes =
        (uint32_t)packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    if (header == NULL || packet == NULL ||
        frame_bytes > TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES) {
        return;
    }
    (void)__atomic_add_fetch(&s_tdma_pio_spi_tx_history_guard,
                             1u, __ATOMIC_ACQ_REL);
    for (uint32_t index = 0u;
         index < TDMA_PIO_SPI_PACKET_HEADER_SIZE; index++) {
        s_tdma_pio_spi_tx_last_frame[index] = header[index];
    }
    for (uint32_t index = 0u; index < (uint32_t)packet_size; index++) {
        s_tdma_pio_spi_tx_last_frame[
            TDMA_PIO_SPI_PACKET_HEADER_SIZE + index] = packet[index];
    }
    s_tdma_pio_spi_tx_last_frame_bytes = frame_bytes;
    s_tdma_pio_spi_tx_history_produced += frame_bytes;
    s_tdma_pio_spi_tx_complete_frame_count++;
    (void)__atomic_add_fetch(&s_tdma_pio_spi_tx_history_guard,
                             1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_prepare_maintenance_pins(
    tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_prepare_maintenance_mapping(phys);
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
        phys->coded.state == TDMA_PIO_SPI_CODED_FORWARDING ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING) {
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
        s_tdma_pio_spi_clk_coded_tx_offset, phys->tx_sck_pin, false);
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

static void tdma_pio_spi_phys_marker_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->marker_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_marker_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->marker_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_marker_publish_error(
    tdma_pio_spi_phys_t *phys, uint32_t epoch,
    tdma_pio_spi_marker_reject_t reason)
{
    tdma_pio_spi_phys_marker_write_begin(phys);
    memset(&phys->marker, 0, sizeof(phys->marker));
    phys->marker.version = TDMA_PIO_SPI_MARKER_SNAPSHOT_VERSION;
    phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
    phys->marker.flags = TDMA_PIO_SPI_MARKER_FLAG_DIAGNOSTIC_ONLY;
    phys->marker.reject_reason = (uint32_t)reason;
    phys->marker.epoch = epoch;
    tdma_pio_spi_phys_marker_write_end(phys);
}

static uint32_t tdma_pio_spi_phys_marker_sample(uint32_t word,
                                                uint32_t sample_index)
{
    return (word >> ((sample_index & 15u) * 2u)) & 0x3u;
}

static void tdma_pio_spi_phys_marker_decode_edges(
    tdma_pio_spi_phys_t *phys)
{
    uint64_t first_tx = 0u;
    uint64_t first_rx = 0u;
    uint32_t previous = 0x3u;
    const uint32_t samples = phys->marker.capture_sample_count;
    for (uint32_t index = 0u; index < samples; index++) {
        const uint32_t sample = tdma_pio_spi_phys_marker_sample(
            s_tdma_pio_spi_marker_rx[index / 16u], index);
        const uint32_t falling = previous & ~sample;
        if (first_tx == 0u && (falling & 0x1u) != 0u) {
            first_tx = (uint64_t)index + 1ull;
        }
        if (first_rx == 0u && (falling & 0x2u) != 0u) {
            first_rx = (uint64_t)index + 1ull;
        }
        previous = sample;
        if (first_tx != 0u && first_rx != 0u) break;
    }

    if (phys->marker.role == TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER) {
        /* The capture program's WAIT 0 GPIO is the hardware input latch.  DMA
         * starts on the following instruction, hence local tick 1 denotes
         * the accepted marker edge and sampled output ticks start at 2. */
        phys->marker.marker_capture_tick = 1ull;
        phys->marker.marker_forward_tick =
            first_tx == 0u ? 0ull : first_tx + 1ull;
        phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_INPUT_EDGE;
        if (first_tx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_OUTPUT_EDGE;
        }
    } else {
        /* Reference TX and capture SMs start from one hardware sync mask.
         * Its two-NOP capture preamble is accounted for by the local origin
         * tick; the later upstream edge is the full-ring return. */
        phys->marker.marker_forward_tick = first_tx != 0u ? 1ull : 0ull;
        phys->marker.marker_return_tick =
            first_rx == 0u ? 0ull : first_rx + 2ull;
        phys->marker.marker_capture_tick = phys->marker.marker_return_tick;
        if (first_tx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_OUTPUT_EDGE;
        }
        if (first_rx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_INPUT_EDGE |
                                  TDMA_PIO_SPI_MARKER_FLAG_RETURN_EDGE;
        }
    }
}

bool tdma_pio_spi_phys_marker_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_marker_request_t *request)
{
    if (phys == NULL || request == NULL ||
        (request->role != TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR &&
         request->role != TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER) ||
        request->marker_sample_count == 0u ||
        request->offset_sample_count < TDMA_PIO_SPI_MARKER_MIN_OFFSET_SAMPLES ||
        request->offset_sample_count > TDMA_PIO_SPI_MARKER_MAX_OFFSET_SAMPLES ||
        request->capture_phase_delay_cycles >
            TDMA_PIO_SPI_MARKER_MAX_CAPTURE_DELAY_CYCLES ||
        request->capture_sample_count < request->marker_sample_count ||
        request->capture_sample_count >
            TDMA_PIO_SPI_MARKER_BUFFER_WORDS *
                TDMA_PIO_SPI_MARKER_SAMPLES_PER_WORD ||
        (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR &&
         (request->tx_words == NULL || request->tx_word_count == 0u ||
          request->tx_word_count > TDMA_PIO_SPI_MARKER_BUFFER_WORDS))) {
        if (phys != NULL) {
            tdma_pio_spi_phys_marker_publish_error(
                phys, request == NULL ? 0u : request->epoch,
                TDMA_PIO_SPI_MARKER_REJECT_BAD_ARGUMENT);
        }
        return false;
    }
    if (phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING ||
        phys->coded.state == TDMA_PIO_SPI_CODED_RUNNING ||
        phys->coded.state == TDMA_PIO_SPI_CODED_FORWARDING ||
        phys->p3.state == TDMA_PIO_SPI_P3_ARMED ||
        phys->cal_loopback.armed != 0u) {
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
    /* Hardware-pad guard: hold the downstream marker pin high throughout
     * program unload/load and GPIO mux changes.  This is the ARM invariant;
     * only marker injection or physical upstream forwarding may create a
     * falling edge after the override is released. */
    gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_HIGH);
    /* Keep persona switching invisible on an already armed physical link.
     * Dropping DE here creates a real falling edge through ISO1452 and can
     * release a downstream follower before the origin is injected.  The
     * maintenance-only path has no active link, so it may still use the
     * traditional disable/restore guard. */
    const bool drivers_were_enabled = phys->armed;
    if (!drivers_were_enabled) {
        gpio_put(BOARD_TRIG_DE_PIN, false);
    }
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER) ||
        !tdma_pio_spi_phys_ensure_rx_dma() ||
        (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR &&
         !tdma_pio_spi_phys_ensure_tx_dma())) {
        if (!drivers_were_enabled) {
            gpio_put(BOARD_TRIG_DE_PIN, true);
        }
        gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
        tdma_pio_spi_phys_marker_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_MARKER_REJECT_RESOURCE);
        return false;
    }
    const bool enable_drivers_after_setup = !phys->armed;
    if (enable_drivers_after_setup) {
        /* Do not expose the SIO input/default-low state to the downstream
         * trigger isolator.  The marker initializers first establish the
         * high-idle PIO latch and output direction; only then may the line
         * drivers be enabled. */
        tdma_pio_spi_phys_prepare_maintenance_mapping(phys);
    }

    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    if (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR) {
        tdma_pio_spi_marker_origin_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_marker_origin_offset, phys->tx_csn_pin);
    } else {
        tdma_pio_spi_marker_forward_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_marker_forward_offset,
            phys->rx_csn_pin, phys->tx_csn_pin,
            TDMA_PIO_SPI_MARKER_FORWARD_DELAY_CYCLES);
    }
    tdma_pio_spi_marker_capture_program_init(
        BOARD_TDMA_SPI_PIO, phys->rx_sm,
        s_tdma_pio_spi_marker_capture_offset,
        phys->tx_csn_pin, phys->rx_csn_pin,
        request->capture_phase_delay_cycles);

    const uint32_t capture_words =
        (request->capture_sample_count + 15u) / 16u;
    memset(s_tdma_pio_spi_marker_rx, 0,
           capture_words * sizeof(s_tdma_pio_spi_marker_rx[0]));
    if (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR) {
        memcpy(s_tdma_pio_spi_marker_tx, request->tx_words,
               request->tx_word_count * sizeof(request->tx_words[0]));
    }

    dma_channel_config rx_dc = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_dc, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_dc, false);
    channel_config_set_write_increment(&rx_dc, true);
    channel_config_set_dreq(
        &rx_dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, false));
    dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &rx_dc,
                          s_tdma_pio_spi_marker_rx,
                          &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                          capture_words, false);

    if (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR) {
        dma_channel_config tx_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_tx_dma_channel);
        channel_config_set_transfer_data_size(&tx_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&tx_dc, true);
        channel_config_set_write_increment(&tx_dc, false);
        channel_config_set_dreq(
            &tx_dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->tx_sm, true));
        dma_channel_configure((uint)s_tdma_pio_spi_tx_dma_channel, &tx_dc,
                              &BOARD_TDMA_SPI_PIO->txf[phys->tx_sm],
                              s_tdma_pio_spi_marker_tx,
                              request->tx_word_count, false);
    }

    tdma_pio_spi_phys_marker_write_begin(phys);
    memset(&phys->marker, 0, sizeof(phys->marker));
    phys->marker.version = TDMA_PIO_SPI_MARKER_SNAPSHOT_VERSION;
    phys->marker.state = TDMA_PIO_SPI_MARKER_ARMED;
    phys->marker.role = request->role;
    phys->marker.flags = TDMA_PIO_SPI_MARKER_FLAG_DIAGNOSTIC_ONLY |
                         TDMA_PIO_SPI_MARKER_FLAG_HARDWARE_CAPTURE;
    phys->marker.epoch = request->epoch;
    phys->marker.tx_word_count = request->tx_word_count;
    phys->marker.marker_sample_count = request->marker_sample_count;
    phys->marker.capture_word_count = capture_words;
    phys->marker.capture_sample_count = request->capture_sample_count;
    phys->marker.tx_dma_remaining = request->tx_word_count;
    phys->marker.rx_dma_remaining = capture_words;
    phys->marker_deadline_ns = 0u;
    tdma_pio_spi_phys_marker_write_end(phys);

    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    gpio_put(BOARD_TRIG_DE_PIN, true);
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                               (1u << phys->tx_sm) | (1u << phys->rx_sm));
    gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
    return true;
}

bool tdma_pio_spi_phys_marker_inject(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        phys->marker.state != TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.role != TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR ||
        s_tdma_pio_spi_tx_dma_channel < 0 ||
        dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
        return false;
    }

    tdma_pio_spi_phys_marker_write_begin(phys);
    phys->marker.state = TDMA_PIO_SPI_MARKER_RUNNING;
    phys->marker_deadline_ns = vdc_timestamp_clock_now_ns() +
                               TDMA_PIO_SPI_MARKER_TIMEOUT_NS;
    tdma_pio_spi_phys_marker_write_end(phys);

    /* The first DMA word releases marker_origin's pull block.  The physical
     * falling edge and all subsequent samples remain entirely in PIO. */
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_tx_dma_channel);
    return true;
}

void tdma_pio_spi_phys_marker_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    tdma_pio_spi_phys_cal_cleanup(phys);
    tdma_pio_spi_phys_marker_write_begin(phys);
    phys->marker.state = TDMA_PIO_SPI_MARKER_IDLE;
    phys->marker.reject_reason = TDMA_PIO_SPI_MARKER_REJECT_NONE;
    phys->marker.tx_dma_remaining = 0u;
    phys->marker.rx_dma_remaining = 0u;
    tdma_pio_spi_phys_marker_write_end(phys);
    (void)tdma_pio_spi_phys_select_program_persona(
        phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

void tdma_pio_spi_phys_marker_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        (phys->marker.state != TDMA_PIO_SPI_MARKER_ARMED &&
         phys->marker.state != TDMA_PIO_SPI_MARKER_RUNNING) ||
        s_tdma_pio_spi_rx_dma_channel < 0) {
        return;
    }
    const uint32_t rx_remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
    const uint32_t tx_remaining =
        phys->marker.role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR &&
                s_tdma_pio_spi_tx_dma_channel >= 0
            ? dma_hw->ch[(uint)s_tdma_pio_spi_tx_dma_channel].transfer_count
            : 0u;
    const bool stalled =
        pio_sm_is_rx_fifo_full(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    const bool timed_out = phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING &&
        rx_remaining != 0u &&
        vdc_timestamp_clock_now_ns() >= phys->marker_deadline_ns;

    tdma_pio_spi_phys_marker_write_begin(phys);
    phys->marker.rx_dma_remaining = rx_remaining;
    phys->marker.tx_dma_remaining = tx_remaining;
    if (phys->marker.role == TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER &&
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED &&
        rx_remaining < phys->marker.capture_word_count) {
        phys->marker.state = TDMA_PIO_SPI_MARKER_RUNNING;
        phys->marker_deadline_ns = vdc_timestamp_clock_now_ns() +
                                   TDMA_PIO_SPI_MARKER_TIMEOUT_NS;
    }
    if (tx_remaining == 0u) {
        phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_TX_DMA_COMPLETE;
    }
    if (stalled && rx_remaining != 0u) {
        phys->marker.pio_stall_count++;
        phys->marker.dma_overrun_count++;
    }
    if (timed_out) {
        phys->marker.timeout_count++;
        phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
        phys->marker.reject_reason = TDMA_PIO_SPI_MARKER_REJECT_TIMEOUT;
    } else if (rx_remaining == 0u) {
        phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_RX_DMA_COMPLETE;
        tdma_pio_spi_phys_marker_decode_edges(phys);
        if (tx_remaining != 0u) {
            phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
            phys->marker.reject_reason =
                TDMA_PIO_SPI_MARKER_REJECT_CAPTURE_SHORT;
        } else if (phys->marker.pio_stall_count != 0u) {
            phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
            phys->marker.reject_reason =
                TDMA_PIO_SPI_MARKER_REJECT_PIO_STALL;
        } else if (phys->marker.marker_capture_tick == 0u ||
                   phys->marker.marker_forward_tick == 0u) {
            phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
            phys->marker.reject_reason =
                TDMA_PIO_SPI_MARKER_REJECT_EDGE_MISSING;
        } else {
            phys->marker.state = TDMA_PIO_SPI_MARKER_COMPLETE;
            phys->marker.reject_reason = TDMA_PIO_SPI_MARKER_REJECT_NONE;
        }
    }
    const bool finished = phys->marker.state == TDMA_PIO_SPI_MARKER_COMPLETE ||
                          phys->marker.state == TDMA_PIO_SPI_MARKER_ERROR;
    tdma_pio_spi_phys_marker_write_end(phys);
    if (finished) {
        if (s_tdma_pio_spi_tx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
        }
        if (s_tdma_pio_spi_rx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        }
        tdma_pio_spi_phys_cal_cleanup(phys);
        (void)tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    }
}

bool tdma_pio_spi_phys_get_marker_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_marker_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->marker_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->marker;
        const uint32_t end =
            __atomic_load_n(&phys->marker_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool tdma_pio_spi_phys_copy_marker_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    tdma_pio_spi_marker_snapshot_t snapshot;
    if (capture_word_count != NULL) *capture_word_count = 0u;
    if (phys == NULL || capture_words == NULL || capture_word_count == NULL ||
        !tdma_pio_spi_phys_get_marker_snapshot(phys, &snapshot) ||
        (snapshot.state != TDMA_PIO_SPI_MARKER_COMPLETE &&
         snapshot.state != TDMA_PIO_SPI_MARKER_ERROR) ||
        (snapshot.flags & TDMA_PIO_SPI_MARKER_FLAG_RX_DMA_COMPLETE) == 0u ||
        snapshot.capture_word_count > capture_word_capacity) {
        return false;
    }
    memcpy(capture_words, s_tdma_pio_spi_marker_rx,
           snapshot.capture_word_count * sizeof(capture_words[0]));
    *capture_word_count = snapshot.capture_word_count;
    return true;
}

static void tdma_pio_spi_phys_data_train_write_begin(
    tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->data_train_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_data_train_write_end(
    tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->data_train_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_data_train_publish_error(
    tdma_pio_spi_phys_t *phys, uint32_t epoch,
    tdma_pio_spi_data_train_reject_t reason)
{
    tdma_pio_spi_phys_data_train_write_begin(phys);
    memset(&phys->data_train, 0, sizeof(phys->data_train));
    phys->data_train.version = TDMA_PIO_SPI_DATA_TRAIN_SNAPSHOT_VERSION;
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
    phys->data_train.flags =
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_DIAGNOSTIC_ONLY;
    phys->data_train.reject_reason = (uint32_t)reason;
    phys->data_train.epoch = epoch;
    tdma_pio_spi_phys_data_train_write_end(phys);
}

static void tdma_pio_spi_phys_data_train_set_drivers(uint32_t role)
{
    /* DATA0_OUT returns toward the link initiator through the upstream
     * transceiver. The initiator drives only the downstream TRIG marker. */
    gpio_put(BOARD_UP_BISS_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER);
    gpio_put(BOARD_DN_BISS_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE);
    gpio_put(BOARD_TRIG_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR);
}

bool tdma_pio_spi_phys_data_train_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_data_train_request_t *request)
{
    if (phys == NULL || request == NULL ||
        (request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER &&
         request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE &&
         request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION) ||
        request->marker_to_data_delay_cycles == 0u ||
        request->marker_to_data_delay_cycles >
            TDMA_PIO_SPI_DATA_TRAIN_MAX_DELAY_CYCLES ||
        request->source_phase_delay_cycles == 0u ||
        request->source_phase_delay_cycles >
            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES ||
        request->phase_delay_cycles == 0u ||
        request->phase_delay_cycles >
            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES ||
        (request->diagnostic_fault_flags &
         ~TDMA_PIO_SPI_DATA_TRAIN_FAULT_ALL) != 0u ||
        (request->diagnostic_fault_flags != 0u &&
         request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR) ||
        (request->diagnostic_fault_flags != 0u &&
         (request->diagnostic_fault_flags &
          (request->diagnostic_fault_flags - 1u)) != 0u) ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         request->phase_delay_cycles < request->source_phase_delay_cycles) ||
        request->data_sample_count == 0u ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         (request->marker_words == NULL ||
          request->marker_word_count == 0u ||
          request->marker_word_count > TDMA_PIO_SPI_MARKER_BUFFER_WORDS ||
          request->capture_sample_count < request->data_sample_count ||
           request->capture_sample_count == 0u ||
           request->capture_sample_count >
               TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS * 32u ||
           (((request->diagnostic_fault_flags &
              TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u) &&
            request->capture_sample_count <= 32u))) ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER &&
         (request->data_words == NULL || request->data_word_count == 0u ||
          request->data_word_count > TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS ||
          request->data_sample_count > request->data_word_count * 32u)) ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE &&
         (request->data_words == NULL || request->data_word_count == 0u ||
          request->data_word_count > TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS ||
          request->data_sample_count > request->data_word_count * 32u)) ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION &&
         (request->capture_sample_count < request->data_sample_count ||
          request->capture_sample_count == 0u ||
          request->capture_sample_count >
              TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS * 32u))) {
        if (phys != NULL) {
            tdma_pio_spi_phys_data_train_publish_error(
                phys, request == NULL ? 0u : request->epoch,
                TDMA_PIO_SPI_DATA_TRAIN_REJECT_BAD_ARGUMENT);
        }
        return false;
    }
    if (phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_ARMED ||
        phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_RUNNING ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING ||
        phys->coded.state == TDMA_PIO_SPI_CODED_RUNNING ||
        phys->coded.state == TDMA_PIO_SPI_CODED_FORWARDING ||
        phys->p3.state == TDMA_PIO_SPI_P3_ARMED ||
        phys->cal_loopback.armed != 0u) {
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
    tdma_pio_spi_phys_prepare_maintenance_mapping(phys);
    if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER) {
        gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_HIGH);
        gpio_set_outover(phys->tx_pin, GPIO_OVERRIDE_LOW);
    } else if (request->role ==
               TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE) {
        gpio_set_outover(phys->tx_sck_pin, GPIO_OVERRIDE_LOW);
    }
    tdma_pio_spi_phys_data_train_set_drivers(
        TDMA_PIO_SPI_DATA_TRAIN_ROLE_NONE);
    if (!tdma_pio_spi_phys_select_program_persona(
            phys,
            (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
             request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION)
                ? TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN
                : TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN) ||
        !tdma_pio_spi_phys_ensure_rx_dma() ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         !tdma_pio_spi_phys_ensure_tx_dma())) {
        gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
        gpio_set_outover(phys->tx_pin, GPIO_OVERRIDE_NORMAL);
        gpio_set_outover(phys->tx_sck_pin, GPIO_OVERRIDE_NORMAL);
        tdma_pio_spi_phys_data_train_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_DATA_TRAIN_REJECT_RESOURCE);
        return false;
    }

    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 4u);
    BOARD_TDMA_SPI_PIO->fdebug =
        1u << (PIO_FDEBUG_TXSTALL_LSB + phys->rx_sm);

    const uint32_t capture_words =
        (request->capture_sample_count + 31u) / 32u;
    const bool inject_rx_dma_pause =
        (request->diagnostic_fault_flags &
         TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_PAUSE) != 0u;
    const bool inject_rx_dma_short =
        (request->diagnostic_fault_flags &
         TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u;
    const uint32_t capture_dma_words =
        inject_rx_dma_short ? capture_words - 1u : capture_words;
    if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR) {
        memcpy(s_tdma_pio_spi_marker_tx, request->marker_words,
               request->marker_word_count * sizeof(request->marker_words[0]));
        tdma_pio_spi_marker_origin_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_marker_origin_offset, phys->tx_csn_pin);
        tdma_pio_spi_data_train_sink_program_init(
            BOARD_TDMA_SPI_PIO, phys->rx_sm,
            s_tdma_pio_spi_data_train_sink_offset,
            phys->tx_csn_pin, phys->rx_pin, request->phase_delay_cycles);
        pio_sm_put(BOARD_TDMA_SPI_PIO, phys->rx_sm,
                   request->marker_to_data_delay_cycles - 1u);

        dma_channel_config marker_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_tx_dma_channel);
        channel_config_set_transfer_data_size(&marker_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&marker_dc, true);
        channel_config_set_write_increment(&marker_dc, false);
        channel_config_set_dreq(
            &marker_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->tx_sm, true));
        dma_channel_configure((uint)s_tdma_pio_spi_tx_dma_channel,
                              &marker_dc,
                              &BOARD_TDMA_SPI_PIO->txf[phys->tx_sm],
                              s_tdma_pio_spi_marker_tx,
                              request->marker_word_count, false);

        memset(s_tdma_pio_spi_data_train_rx, 0,
               capture_words * sizeof(s_tdma_pio_spi_data_train_rx[0]));
        dma_channel_config capture_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&capture_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&capture_dc, false);
        channel_config_set_write_increment(&capture_dc, true);
        channel_config_set_dreq(
            &capture_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, false));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel,
                               &capture_dc, s_tdma_pio_spi_data_train_rx,
                               &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                               capture_dma_words, false);
    } else if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER) {
        memcpy(s_tdma_pio_spi_data_train_tx, request->data_words,
               request->data_word_count * sizeof(request->data_words[0]));
        tdma_pio_spi_data_train_source_program_init(
            BOARD_TDMA_SPI_PIO, phys->rx_sm,
            s_tdma_pio_spi_data_train_source_offset,
            phys->rx_csn_pin, phys->tx_pin, request->phase_delay_cycles);
        pio_sm_put(BOARD_TDMA_SPI_PIO, phys->rx_sm,
                   request->marker_to_data_delay_cycles - 1u);

        dma_channel_config data_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&data_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&data_dc, true);
        channel_config_set_write_increment(&data_dc, false);
        channel_config_set_dreq(
            &data_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, true));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &data_dc,
                              &BOARD_TDMA_SPI_PIO->txf[phys->rx_sm],
                              s_tdma_pio_spi_data_train_tx,
                              request->data_word_count, false);
    } else if (request->role ==
               TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE) {
        memcpy(s_tdma_pio_spi_data_train_tx, request->data_words,
               request->data_word_count * sizeof(request->data_words[0]));
        tdma_pio_spi_sck_train_trigger_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_sck_train_trigger_offset, phys->tx_csn_pin);
        tdma_pio_spi_sck_train_source_program_init(
            BOARD_TDMA_SPI_PIO, phys->rx_sm,
            s_tdma_pio_spi_sck_train_source_offset, phys->tx_csn_pin,
            phys->tx_sck_pin, request->phase_delay_cycles);
        pio_sm_put(BOARD_TDMA_SPI_PIO, phys->rx_sm,
                   request->marker_to_data_delay_cycles - 1u);

        dma_channel_config sck_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&sck_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&sck_dc, true);
        channel_config_set_write_increment(&sck_dc, false);
        channel_config_set_dreq(
            &sck_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, true));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &sck_dc,
                              &BOARD_TDMA_SPI_PIO->txf[phys->rx_sm],
                              s_tdma_pio_spi_data_train_tx,
                              request->data_word_count, false);
    } else {
        memset(s_tdma_pio_spi_data_train_rx, 0,
               capture_words * sizeof(s_tdma_pio_spi_data_train_rx[0]));
        tdma_pio_spi_sck_train_sink_program_init(
            BOARD_TDMA_SPI_PIO, phys->rx_sm,
            s_tdma_pio_spi_sck_train_sink_offset, phys->rx_sck_pin,
            request->phase_delay_cycles);

        dma_channel_config capture_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&capture_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&capture_dc, false);
        channel_config_set_write_increment(&capture_dc, true);
        channel_config_set_dreq(
            &capture_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, false));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel,
                              &capture_dc, s_tdma_pio_spi_data_train_rx,
                              &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                              capture_words, false);
    }

    tdma_pio_spi_phys_data_train_write_begin(phys);
    memset(&phys->data_train, 0, sizeof(phys->data_train));
    phys->data_train.version = TDMA_PIO_SPI_DATA_TRAIN_SNAPSHOT_VERSION;
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ARMED;
    phys->data_train.role = request->role;
    phys->data_train.flags =
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_DIAGNOSTIC_ONLY |
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_ORIGIN |
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_DATA;
    phys->data_train.epoch = request->epoch;
    phys->data_train.marker_word_count =
        request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE
            ? 1u
            : request->marker_word_count;
    phys->data_train.data_word_count = request->data_word_count;
    phys->data_train.data_sample_count = request->data_sample_count;
    phys->data_train.capture_word_count = capture_words;
    phys->data_train.capture_sample_count = request->capture_sample_count;
    phys->data_train.marker_to_data_delay_cycles =
        request->marker_to_data_delay_cycles;
    phys->data_train.source_phase_delay_cycles =
        request->source_phase_delay_cycles;
    phys->data_train.phase_delay_cycles = request->phase_delay_cycles;
    phys->data_train.marker_dma_remaining =
        request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE
            ? 0u
            : request->marker_word_count;
    phys->data_train.data_dma_remaining =
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER ||
         request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE)
            ? request->data_word_count : capture_dma_words;
    phys->data_train.diagnostic_fault_flags =
        request->diagnostic_fault_flags;
    /* A destination may be armed several seconds before the source while
     * four USB CDC endpoints are configured sequentially.  Like the TRN-01
     * follower, it starts its transfer timeout only after RX DMA proves that
     * the physical marker gate opened and DATA capture actually began. */
    phys->data_train_deadline_ns = 0u;
    tdma_pio_spi_phys_data_train_write_end(phys);

    if (!inject_rx_dma_pause) {
        dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    }
    tdma_pio_spi_phys_data_train_set_drivers(request->role);
    if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR ||
        request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE) {
        pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                                   (1u << phys->tx_sm) |
                                   (1u << phys->rx_sm));
        gpio_set_outover(
            request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE
                ? phys->tx_sck_pin
                : phys->tx_csn_pin,
            GPIO_OVERRIDE_NORMAL);
        if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE) {
            gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
        }
    } else {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, true);
        gpio_set_outover(phys->tx_pin, GPIO_OVERRIDE_NORMAL);
        gpio_set_outover(phys->tx_sck_pin, GPIO_OVERRIDE_NORMAL);
        gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
    }
    return true;
}

bool tdma_pio_spi_phys_data_train_inject(tdma_pio_spi_phys_t *phys)
{
    const bool sck_source = phys != NULL &&
        phys->data_train.role ==
            TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE;
    if (phys == NULL ||
        phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_ARMED ||
        (phys->data_train.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         !sck_source) ||
        (!sck_source &&
         (s_tdma_pio_spi_tx_dma_channel < 0 ||
          dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)))) {
        return false;
    }
    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_RUNNING;
    phys->data_train.marker_capture_tick = 1ull;
    phys->data_train.data_capture_tick =
        1ull + phys->data_train.marker_to_data_delay_cycles +
        phys->data_train.phase_delay_cycles -
        phys->data_train.source_phase_delay_cycles;
    phys->data_train_deadline_ns = vdc_timestamp_clock_now_ns() +
                                   TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS;
    tdma_pio_spi_phys_data_train_write_end(phys);
    if (sck_source) {
        pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm,
                   s_tdma_pio_spi_sck_train_inject_word);
    } else {
        dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_tx_dma_channel);
    }
    return true;
}

void tdma_pio_spi_phys_data_train_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 4u);
    tdma_pio_spi_phys_cal_cleanup(phys);
    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_IDLE;
    phys->data_train.reject_reason = TDMA_PIO_SPI_DATA_TRAIN_REJECT_NONE;
    phys->data_train.marker_dma_remaining = 0u;
    phys->data_train.data_dma_remaining = 0u;
    tdma_pio_spi_phys_data_train_write_end(phys);
    (void)tdma_pio_spi_phys_select_program_persona(
        phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

void tdma_pio_spi_phys_data_train_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        (phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_ARMED &&
         phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_RUNNING) ||
        s_tdma_pio_spi_rx_dma_channel < 0) {
        return;
    }
    const bool initiator =
        phys->data_train.role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR ||
        phys->data_train.role ==
            TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION;
    const bool injects_dma_origin =
        phys->data_train.role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR;
    const uint32_t marker_remaining =
        injects_dma_origin && s_tdma_pio_spi_tx_dma_channel >= 0
            ? dma_hw->ch[(uint)s_tdma_pio_spi_tx_dma_channel].transfer_count
            : 0u;
    const uint32_t hardware_data_remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
    const uint32_t responder_stall_mask =
        1u << (PIO_FDEBUG_TXSTALL_LSB + phys->rx_sm);
    const bool responder_done = !initiator &&
        (BOARD_TDMA_SPI_PIO->fdebug & responder_stall_mask) != 0u;
    /* A full responder TX FIFO is expected DMA backpressure while the SM waits
     * for CS. Only an undrained initiator RX FIFO represents capture loss. */
    const bool stalled = initiator &&
        pio_sm_is_rx_fifo_full(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    const bool inject_rx_dma_short =
        (phys->data_train.diagnostic_fault_flags &
         TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u;
    /* RX_DMA_PAUSE deliberately leaves the channel untriggered.  On the
     * target DMA implementation an untriggered channel may report a zero
     * hardware transfer_count even though the configured capture has not
     * consumed a single word.  Treating that value as completion races the
     * real PIO FIFO-full evidence and turns the requested PIO_STALL fault
     * into a false COMPLETE/correlation result.  Keep the logical remainder
     * at the requested capture size until the FIFO reports backpressure. */
    const bool inject_rx_dma_pause =
        (phys->data_train.diagnostic_fault_flags &
         TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_PAUSE) != 0u;
    const uint32_t data_remaining = inject_rx_dma_pause
                                        ? phys->data_train.capture_word_count
                                        : hardware_data_remaining;
    const uint32_t armed_capture_words =
        inject_rx_dma_short && phys->data_train.capture_word_count != 0u
            ? phys->data_train.capture_word_count - 1u
            : phys->data_train.capture_word_count;
    const bool capture_started = initiator &&
        data_remaining < armed_capture_words;
    const bool timed_out = phys->data_train_deadline_ns != 0u &&
        vdc_timestamp_clock_now_ns() >= phys->data_train_deadline_ns &&
        ((initiator && data_remaining != 0u) ||
         (!initiator && !responder_done));

    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.marker_dma_remaining = marker_remaining;
    phys->data_train.data_dma_remaining = data_remaining;
    if (initiator && capture_started &&
        phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_ARMED) {
        phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_RUNNING;
        phys->data_train_deadline_ns = vdc_timestamp_clock_now_ns() +
                                       TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS;
    }
    if (initiator && capture_started) {
        phys->data_train.marker_capture_tick = 1ull;
        phys->data_train.data_capture_tick = 1ull +
            (phys->data_train.role ==
                     TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION
                 ? phys->data_train.phase_delay_cycles
                 : phys->data_train.marker_to_data_delay_cycles +
                       phys->data_train.phase_delay_cycles -
                       phys->data_train.source_phase_delay_cycles);
    }
    if (marker_remaining == 0u) {
        phys->data_train.flags |=
            TDMA_PIO_SPI_DATA_TRAIN_FLAG_MARKER_DMA_COMPLETE;
    }
    if (data_remaining == 0u) {
        phys->data_train.flags |=
            TDMA_PIO_SPI_DATA_TRAIN_FLAG_DATA_DMA_COMPLETE;
    }
    if (responder_done) {
        phys->data_train.flags |= TDMA_PIO_SPI_DATA_TRAIN_FLAG_SOURCE_IRQ;
    }
    if (inject_rx_dma_short && data_remaining == 0u) {
        /* A deliberately short RX transfer is a DMA boundary fault even if
         * the PIO FIFO happens to drain before it becomes full. */
        if (phys->data_train.dma_overrun_count == 0u) {
            phys->data_train.dma_overrun_count++;
        }
    } else if (stalled && data_remaining != 0u) {
        /* A paused RX DMA is only classified as PIO_STALL after the real PIO
         * RX FIFO reports backpressure.  The DMA count is derivative evidence
         * and must not replace the physical root cause. */
        phys->data_train.pio_stall_count++;
        phys->data_train.dma_overrun_count++;
    }
    if (inject_rx_dma_short &&
        phys->data_train.dma_overrun_count != 0u) {
        phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
        phys->data_train.reject_reason = TDMA_PIO_SPI_DATA_TRAIN_REJECT_DMA;
    } else if (stalled && data_remaining != 0u &&
               (phys->data_train.diagnostic_fault_flags &
                TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_PAUSE) != 0u) {
        /* Do not let the watchdog turn an observed FIFO blockage into a
         * generic timeout.  The injected pause is diagnostic-only; the
         * physical FIFO full indication is the required root-cause evidence. */
        phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
        phys->data_train.reject_reason = TDMA_PIO_SPI_DATA_TRAIN_REJECT_PIO_STALL;
    } else if (timed_out) {
        phys->data_train.timeout_count++;
        phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
        phys->data_train.reject_reason =
            TDMA_PIO_SPI_DATA_TRAIN_REJECT_TIMEOUT;
    } else if ((initiator && !inject_rx_dma_short && marker_remaining == 0u &&
                data_remaining == 0u) ||
               (!initiator && responder_done && data_remaining == 0u)) {
        if (phys->data_train.pio_stall_count != 0u) {
            phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
            phys->data_train.reject_reason =
                TDMA_PIO_SPI_DATA_TRAIN_REJECT_PIO_STALL;
        } else {
            phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_COMPLETE;
            phys->data_train.reject_reason =
                TDMA_PIO_SPI_DATA_TRAIN_REJECT_NONE;
        }
    }
    const bool finished =
        phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE ||
        phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_ERROR;
    tdma_pio_spi_phys_data_train_write_end(phys);
    if (finished) {
        if (s_tdma_pio_spi_tx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
        }
        if (s_tdma_pio_spi_rx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        }
        pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 4u);
        tdma_pio_spi_phys_cal_cleanup(phys);
        (void)tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    }
}

bool tdma_pio_spi_phys_get_data_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_data_train_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->data_train_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->data_train;
        const uint32_t end =
            __atomic_load_n(&phys->data_train_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool tdma_pio_spi_phys_copy_data_train_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    tdma_pio_spi_data_train_snapshot_t snapshot;
    if (capture_word_count != NULL) *capture_word_count = 0u;
    if (phys == NULL || capture_words == NULL || capture_word_count == NULL ||
        !tdma_pio_spi_phys_get_data_train_snapshot(phys, &snapshot) ||
        (snapshot.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         snapshot.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION) ||
        (snapshot.state != TDMA_PIO_SPI_DATA_TRAIN_COMPLETE &&
         !(snapshot.state == TDMA_PIO_SPI_DATA_TRAIN_ERROR &&
           snapshot.reject_reason == TDMA_PIO_SPI_DATA_TRAIN_REJECT_DMA &&
           (snapshot.diagnostic_fault_flags &
            TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u &&
           snapshot.dma_overrun_count != 0u))) {
        return false;
    }
    const size_t available_words =
        snapshot.state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE
            ? snapshot.capture_word_count
            : snapshot.capture_word_count - 1u;
    if (available_words == 0u || available_words > capture_word_capacity) {
        return false;
    }
    memcpy(capture_words, s_tdma_pio_spi_data_train_rx,
           available_words * sizeof(capture_words[0]));
    *capture_word_count = available_words;
    return true;
}

bool tdma_pio_spi_phys_sck_train_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_data_train_request_t *request)
{
    return request != NULL &&
           (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
            request->role ==
                TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION) &&
           tdma_pio_spi_phys_data_train_arm(phys, request);
}

bool tdma_pio_spi_phys_sck_train_inject(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        phys->data_train.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
        phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_ARMED) {
        return false;
    }
    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_RUNNING;
    phys->data_train.marker_capture_tick = 1ull;
    phys->data_train.data_capture_tick =
        1ull + phys->data_train.marker_to_data_delay_cycles +
        phys->data_train.phase_delay_cycles -
        phys->data_train.source_phase_delay_cycles;
    phys->data_train_deadline_ns = vdc_timestamp_clock_now_ns() +
                                   TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS;
    tdma_pio_spi_phys_data_train_write_end(phys);
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm,
               s_tdma_pio_spi_sck_train_inject_word);
    return true;
}

void tdma_pio_spi_phys_sck_train_stop(tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_data_train_stop(phys);
}

void tdma_pio_spi_phys_sck_train_service(tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_data_train_service(phys);
}

bool tdma_pio_spi_phys_get_sck_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_data_train_snapshot_t *snapshot)
{
    return tdma_pio_spi_phys_get_data_train_snapshot(phys, snapshot) &&
           (snapshot->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
            snapshot->role ==
                TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION);
}

bool tdma_pio_spi_phys_copy_sck_train_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    return tdma_pio_spi_phys_copy_data_train_capture(
        phys, capture_words, capture_word_capacity, capture_word_count);
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
    uint64_t data_high_sum = 0u;
    uint32_t data_high_count = 0u;
    bool have_clock_rise = false;
    bool have_clock_fall = false;
    bool have_data_rise = false;
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
            if ((rising & data_mask) != 0u) {
                data_rise = timestamp;
                have_data_rise = true;
            }
            if ((falling & data_mask) != 0u && have_data_rise) {
                data_high_sum += timestamp - data_rise;
                data_high_count++;
                have_data_rise = false;
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
    if (data_high_count != 0u) {
        phys->p3.data_high_ns = (uint32_t)(
            (data_high_sum + data_high_count / 2u) / data_high_count);
    }
    phys->p3.data_pulse_count = data_high_count;
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

static bool tdma_pio_spi_phys_flight_origin_tx(
    tdma_pio_spi_phys_t *phys,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t *tx_timestamp_ns)
{
    if (phys == NULL || packet == NULL || packet_size == 0u ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        phys->role != TDMA_PIO_SPI_ROLE_MASTER ||
        s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        s_tdma_pio_spi_tx_dma_channel < 0) {
        return false;
    }
    /* A burst is accepted at launch and completed by
     * tdma_pio_spi_phys_service_tx().  Never wait here for the wire, DMA or
     * PIO TXSTALL: doing so would let a line delay consume the TDMA phase. */
    if (phys->flight_tx_pending ||
        dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
        phys->snapshot.tx_busy_count++;
        return false;
    }
    if (!pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        /* A failed prior burst must not leave the cyclic path permanently
         * TX_BUSY.  No successful call returns while this FIFO is occupied. */
        tdma_pio_spi_phys_flight_origin_recover(phys);
    }

    const uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE] = {
        TDMA_PIO_SPI_PACKET_MAGIC0,
        TDMA_PIO_SPI_PACKET_MAGIC1,
        (uint8_t)(packet_size & 0xFFu),
        (uint8_t)(packet_size >> 8u),
    };
    const uint32_t wire_bytes =
        (uint32_t)packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    const uint32_t clock_bytes = wire_bytes + phys->flight_tail_bytes;
    if (clock_bytes == 0u ||
        clock_bytes != phys->flight_physical_byte_count ||
        clock_bytes > TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS) {
        tdma_pio_spi_phys_set_error(phys,
                                    TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        return false;
    }
    for (uint32_t index = 0u; index < clock_bytes; index++) {
        const uint8_t value = index >= wire_bytes
            ? 0u
            : (index < TDMA_PIO_SPI_PACKET_HEADER_SIZE
                   ? header[index]
                   : packet[index - TDMA_PIO_SPI_PACKET_HEADER_SIZE]);
        s_tdma_pio_spi_flight_tx_words[index] = ((uint32_t)value) << 24u;
    }

    dma_channel_config dma_cfg = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_tx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(
        &dma_cfg,
        pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->tx_sm, true));
    dma_channel_configure(
        (uint)s_tdma_pio_spi_tx_dma_channel,
        &dma_cfg,
        &BOARD_TDMA_SPI_PIO->txf[phys->tx_sm],
        s_tdma_pio_spi_flight_tx_words,
        clock_bytes,
        false);

    const uint32_t clock_bits = clock_bytes * 8u;
    const uint32_t clock_txstall_mask =
        tdma_pio_spi_phys_txstall_mask(phys->rx_sm);
    if (pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO, phys->tx_sm) ||
        pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO,
                               BOARD_TDMA_SPI_RTT_SM) ||
        pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO, phys->rx_sm)) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
        phys->snapshot.tx_busy_count++;
        return false;
    }
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 1u);
    /* The DATA SM consumes one frame control word before the DMA payload.
     * This keeps CS in the outer PIO loop and bytes in the inner loop.  The
     * FIFO was checked above, so the non-blocking put is deterministic. */
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm, clock_bytes - 1u);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_tx_dma_channel);
    pio_sm_put(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM, UINT32_MAX);
    const uint64_t tx_edge_diagnostic_ns = vdc_timestamp_clock_now_ns();
    /* This FIFO word is the sole launch event. The control PIO consumes it,
     * lowers CS, generates every SCK edge, then restores idle CS atomically. */
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->rx_sm, clock_bits - 1u);
    /* The clock SM was parked at its blocking PULL before the word above.
     * Clear that old sticky TXSTALL only after releasing the PULL. */
    BOARD_TDMA_SPI_PIO->fdebug = clock_txstall_mask;
    const uint64_t now_ns = tx_edge_diagnostic_ns;
    const uint64_t wire_ns = phys->baud_hz == 0u
        ? 0ull
        : ((uint64_t)clock_bits * 1000000000ull + phys->baud_hz - 1ull) /
              phys->baud_hz;
    phys->flight_tx_pending = true;
    phys->flight_tx_completion_pending = false;
    phys->flight_tx_completion_timestamp_ns = 0ull;
    phys->flight_tx_packet_size = (uint32_t)packet_size;
    phys->flight_tx_wire_bytes = wire_bytes;
    phys->flight_tx_launch_timestamp_ns = now_ns;
    phys->flight_tx_deadline_ns =
        UINT64_MAX - now_ns < wire_ns + 1000000ull
            ? UINT64_MAX
            : now_ns + wire_ns + 1000000ull;
    if (tx_timestamp_ns != NULL) {
        /* Completion owns the hardware latch timestamp.  Zero is explicit:
         * the adapter must not promote a software launch time to DPLL
         * evidence. */
        *tx_timestamp_ns = 0ull;
    }
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    return true;
}

void tdma_pio_spi_phys_service_tx(void *context, uint64_t now_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->flight_tx_pending ||
        s_tdma_pio_spi_tx_dma_channel < 0 ||
        s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN) {
        return;
    }

    const uint32_t stall_mask = tdma_pio_spi_phys_txstall_mask(phys->rx_sm);
    const bool clock_done =
        (BOARD_TDMA_SPI_PIO->fdebug & stall_mask) != 0u;
    const bool data_done =
        !dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel);
    if (!clock_done || !data_done) {
        if (now_ns >= phys->flight_tx_deadline_ns) {
            phys->snapshot.tx_timeout_count++;
            if (!clock_done) {
                phys->snapshot.origin_clock_timeout_count++;
            }
            if (!data_done) {
                phys->snapshot.origin_data_timeout_count++;
            }
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            tdma_pio_spi_phys_flight_origin_recover(phys);
            phys->flight_tx_pending = false;
            phys->flight_tx_completion_pending = false;
        }
        return;
    }

    phys->snapshot.origin_done_txstall_count++;
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 1u);
    BOARD_TDMA_SPI_PIO->fdebug = stall_mask;
    uint64_t tx_edge_timestamp_ns = 0ull;
    (void)tdma_pio_spi_phys_clock_latch_read_and_rearm(
        phys, &tx_edge_timestamp_ns);

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE];
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    const uint32_t packet_size = phys->flight_tx_packet_size;
    const uint32_t wire_bytes = phys->flight_tx_wire_bytes;
    if (packet_size == 0u || wire_bytes != packet_size +
            TDMA_PIO_SPI_PACKET_HEADER_SIZE ||
        wire_bytes > TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        phys->flight_tx_pending = false;
        phys->flight_tx_completion_pending = false;
        return;
    }
    for (uint32_t index = 0u; index < wire_bytes; index++) {
        const uint8_t value = (uint8_t)(
            s_tdma_pio_spi_flight_tx_words[index] >> 24u);
        if (index < TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
            header[index] = value;
        } else {
            packet[index - TDMA_PIO_SPI_PACKET_HEADER_SIZE] = value;
        }
    }
    tdma_pio_spi_phys_record_complete_tx_frame(header, packet, packet_size);
    phys->snapshot.tx_count++;
    phys->snapshot.tx_edge_count++;
    phys->snapshot.last_tx_size = packet_size;
    phys->snapshot.last_tx_edge_timestamp_ns =
        tx_edge_timestamp_ns != 0ull
            ? tx_edge_timestamp_ns
            : phys->flight_tx_launch_timestamp_ns;
    phys->snapshot.last_tx_done_timestamp_ns = vdc_timestamp_clock_now_ns();
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    phys->flight_tx_pending = false;
    phys->flight_tx_completion_pending = true;
    phys->flight_tx_completion_timestamp_ns = tx_edge_timestamp_ns;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
}

bool tdma_pio_spi_phys_take_tx_completion(void *context,
                                          uint64_t *tx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = 0ull;
    }
    if (phys == NULL || !phys->flight_tx_completion_pending) {
        return false;
    }
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = phys->flight_tx_completion_timestamp_ns;
    }
    phys->flight_tx_completion_pending = false;
    return true;
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
    if (s_tdma_pio_spi_program_persona ==
        TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN) {
        return tdma_pio_spi_phys_flight_origin_tx(
            phys, packet, packet_size, tx_timestamp_ns);
    }
    if (s_tdma_pio_spi_program_persona !=
        TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_ROLE);
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
    tdma_pio_spi_phys_record_complete_tx_frame(header, packet, packet_size);

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
    uint64_t hardware_rx_edge_timestamp_ns = 0ull;
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        (void)tdma_pio_spi_phys_clock_latch_read_and_rearm(
            phys, &hardware_rx_edge_timestamp_ns);
    }
    *rx_timestamp_ns = phys->role == TDMA_PIO_SPI_ROLE_SLAVE
        ? hardware_rx_edge_timestamp_ns
        : rx_edge_timestamp_ns;

    phys->snapshot.rx_count++;
    phys->snapshot.last_rx_size = frame_size;
    phys->snapshot.last_rx_timestamp_ns = *rx_timestamp_ns;
    phys->snapshot.rx_edge_count++;
    phys->snapshot.last_rx_edge_timestamp_ns =
        hardware_rx_edge_timestamp_ns != 0ull
            ? hardware_rx_edge_timestamp_ns
            : rx_edge_timestamp_ns;
    phys->snapshot.last_rx_extract_timestamp_ns = rx_extract_timestamp_ns;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

bool tdma_pio_spi_phys_feedback_round_trip(
    void *context,
    uint32_t *round_trip_ns,
    uint32_t *resolution_ns,
    uint32_t *flags)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (round_trip_ns != NULL) {
        *round_trip_ns = 0u;
    }
    if (resolution_ns != NULL) {
        *resolution_ns = 0u;
    }
    if (flags != NULL) {
        *flags = TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    }
    if (phys == NULL || round_trip_ns == NULL || resolution_ns == NULL ||
        flags == NULL || !phys->armed ||
        phys->role != TDMA_PIO_SPI_ROLE_MASTER ||
        s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        pio_sm_is_rx_fifo_empty(BOARD_TDMA_SPI_PIO,
                                BOARD_TDMA_SPI_RTT_SM)) {
        return false;
    }

    /* A rejected transport frame can leave an older duration in this FIFO.
     * Wire ordering is strict, so the newest completed CS interval belongs
     * to the newest complete frame returned by phys_rx. */
    uint32_t remaining = 0u;
    do {
        remaining = pio_sm_get(BOARD_TDMA_SPI_PIO,
                               BOARD_TDMA_SPI_RTT_SM);
    } while (!pio_sm_is_rx_fifo_empty(BOARD_TDMA_SPI_PIO,
                                      BOARD_TDMA_SPI_RTT_SM));

    const uint32_t clk_hz = clock_get_hz(clk_sys);
    if (clk_hz == 0u) {
        return false;
    }
    const uint32_t edge_resolution_ns = (uint32_t)(
        (2000000000ull + clk_hz / 2u) / clk_hz);
    const uint64_t elapsed_counts = (uint64_t)UINT32_MAX - remaining;
    const uint64_t duration_ns =
        elapsed_counts * (uint64_t)edge_resolution_ns;
    if (elapsed_counts == 0ull || edge_resolution_ns == 0u ||
        duration_ns == 0ull || duration_ns > UINT32_MAX) {
        return false;
    }
    *round_trip_ns = (uint32_t)duration_ns;
    *resolution_ns = edge_resolution_ns;
    *flags = TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    return true;
}

bool tdma_pio_spi_phys_get_snapshot(const tdma_pio_spi_phys_t *phys,
                                    tdma_pio_spi_phys_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = phys->snapshot;
    snapshot->pio_irq_flags = BOARD_TDMA_SPI_PIO->irq;
    snapshot->pio_fdebug = BOARD_TDMA_SPI_PIO->fdebug;
    snapshot->tx_sm_pc = pio_sm_get_pc(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    snapshot->rx_sm_pc = pio_sm_get_pc(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    snapshot->tx_sm_tx_fifo_level =
        pio_sm_get_tx_fifo_level(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    snapshot->tx_sm_rx_fifo_level =
        pio_sm_get_rx_fifo_level(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    snapshot->rx_sm_tx_fifo_level =
        pio_sm_get_tx_fifo_level(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    snapshot->rx_sm_rx_fifo_level =
        pio_sm_get_rx_fifo_level(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    snapshot->gpio_input_levels =
        (gpio_get(phys->tx_sck_pin) ? (1u << 0u) : 0u) |
        (gpio_get(phys->tx_csn_pin) ? (1u << 1u) : 0u) |
        (gpio_get(phys->tx_pin) ? (1u << 2u) : 0u) |
        (gpio_get(phys->rx_sck_pin) ? (1u << 3u) : 0u) |
        (gpio_get(phys->rx_csn_pin) ? (1u << 4u) : 0u) |
        (gpio_get(phys->rx_pin) ? (1u << 5u) : 0u);
    return true;
}

tdma_pio_spi_normal_capture_copy_result_t
tdma_pio_spi_phys_copy_normal_capture(
    tdma_pio_spi_phys_t *phys,
    uint32_t *rx_bytes,
    size_t rx_capacity,
    uint32_t *tx_bytes,
    size_t tx_capacity,
    tdma_pio_spi_normal_capture_snapshot_t *snapshot)
{
    if (phys == NULL || rx_bytes == NULL || tx_bytes == NULL ||
        snapshot == NULL || rx_capacity == 0u || tx_capacity == 0u ||
        rx_capacity > TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES ||
        tx_capacity > TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES ||
        !phys->armed || !phys->rx_capture_active ||
        (s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)) {
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
    }

    /* Stage 0 owns only the SCK evidence FIFO. Clock-latch restoration and
     * raw RX history use later fixed-size beats so the request never borrows
     * time from another realtime phase. */
    if (phys->flight_normal_capture_copy_stage == 0u) {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->version = TDMA_PIO_SPI_NORMAL_CAPTURE_VERSION;
        snapshot->baud_hz = phys->baud_hz;
        snapshot->bit_period_ns = phys->baud_hz == 0u
            ? 0u
            : (uint32_t)((1000000000ull + phys->baud_hz / 2u) /
                         phys->baud_hz);
        if (s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        if (tdma_pio_spi_phys_service_ring_waveform_capture(phys) !=
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        phys->flight_normal_capture_sck_cursor = 0u;
        phys->flight_normal_capture_copy_stage = 1u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    /* Drain one SCK evidence word per scheduled beat. */
    if (phys->flight_normal_capture_copy_stage == 1u) {
        pio_sm_set_enabled(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM, false);
        const uint32_t sck_word_count = pio_sm_get_rx_fifo_level(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
        if (sck_word_count != TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS ||
            phys->flight_normal_capture_sck_cursor >= sck_word_count) {
            (void)tdma_pio_spi_phys_restore_clock_latch(phys, true);
            phys->flight_sck_waveform_capture_state =
                TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED;
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        const uint32_t index = phys->flight_normal_capture_sck_cursor++;
        snapshot->sck_words[index] = pio_sm_get(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
        if (phys->flight_normal_capture_sck_cursor < sck_word_count) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
        }
        snapshot->sck_word_count = sck_word_count;
        snapshot->sck_sample_count =
            sck_word_count * TDMA_PIO_SPI_FLIGHT_SCK_SAMPLES_PER_WORD;
        snapshot->sck_sample_period_ns =
            TDMA_PIO_SPI_FLIGHT_SCK_SAMPLE_PERIOD_NS;
        phys->flight_normal_capture_copy_stage = 2u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    /* Restore the resident clock-latch program in its own beat. */
    if (phys->flight_normal_capture_copy_stage == 2u) {
        bool restore_complete = false;
        if (!tdma_pio_spi_phys_capture_restore_step(
                phys, &restore_complete)) {
            phys->flight_sck_waveform_capture_state =
                TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED;
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        if (!restore_complete) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
        }
        phys->flight_normal_capture_copy_stage = 3u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    /* Rearm separately, then freeze the raw RX history boundary retained by
     * all subsequent fixed-size chunk copies. */
    if (phys->flight_normal_capture_copy_stage == 3u) {
        if (!tdma_pio_spi_phys_clock_latch_rearm(phys)) {
            phys->flight_sck_waveform_capture_state =
                TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED;
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        phys->flight_normal_capture_rx_produced =
            tdma_pio_spi_phys_rx_produced_words(phys);
        phys->flight_normal_capture_rx_count =
            phys->flight_normal_capture_rx_produced < rx_capacity
            ? phys->flight_normal_capture_rx_produced
            : (uint32_t)rx_capacity;
        phys->flight_normal_capture_rx_start =
            phys->flight_normal_capture_rx_produced -
            phys->flight_normal_capture_rx_count;
        phys->flight_normal_capture_rx_cursor = 0u;
        snapshot->rx_byte_count = phys->flight_normal_capture_rx_count;
        snapshot->rx_produced_bytes =
            phys->flight_normal_capture_rx_produced;
        phys->flight_normal_capture_copy_stage = 4u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    if (phys->flight_normal_capture_copy_stage == 4u) {
        const uint32_t remaining = phys->flight_normal_capture_rx_count -
            phys->flight_normal_capture_rx_cursor;
        const uint32_t count =
            remaining < TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_CHUNK_BYTES
            ? remaining : TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_CHUNK_BYTES;
        for (uint32_t index = 0u; index < count; index++) {
            const uint32_t cursor =
                phys->flight_normal_capture_rx_cursor + index;
            rx_bytes[cursor] = tdma_pio_spi_phys_rx_ring_byte(
                phys->flight_normal_capture_rx_start + cursor);
        }
        phys->flight_normal_capture_rx_cursor += count;
        if (phys->flight_normal_capture_rx_cursor <
            phys->flight_normal_capture_rx_count) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
        }
        phys->flight_normal_capture_copy_stage = 5u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    if (phys->flight_normal_capture_copy_stage != 5u) {
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
    }

    /* Flight evidence is the physical RX loop stream. The origin's software
     * TX history is a continuously rewritten single buffer, not a waveform;
     * seqlock-copying it here can consume an unbounded number of retries and
     * violate the calibration beat. Offline reference selection therefore
     * uses the adjacent Node's immutable physical RX capture. */
    (void)tx_bytes;
    snapshot->tx_byte_count = 0u;
    snapshot->tx_produced_bytes = 0u;
    snapshot->tx_complete_frame_count = 0u;
    phys->flight_normal_capture_copy_stage = 5u;
    return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_READY;
}
