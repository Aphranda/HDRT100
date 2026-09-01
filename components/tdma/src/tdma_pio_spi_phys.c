#include "tdma_pio_spi_phys.h"
#include "tdma_pio_spi_phys_programs.h"

#include <string.h>

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "tdma_flight_overlay.h"
#include "tdma_pio_spi.pio.h"
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
static bool tdma_pio_spi_phys_cal_decode_step(tdma_pio_spi_phys_t *phys);
static int s_tdma_pio_spi_tx_dma_channel = -1;
static int s_tdma_pio_spi_rx_dma_channel = -1;
static tdma_pio_spi_program_manager_t s_tdma_pio_spi_program_manager = {
    .sms_claimed = &s_tdma_pio_spi_sms_claimed,
    .program_persona = &s_tdma_pio_spi_program_persona,
    .tx_offset = &s_tdma_pio_spi_tx_offset,
    .rx_offset = &s_tdma_pio_spi_rx_offset,
    .clk_forward_offset = &s_tdma_pio_spi_clk_forward_offset,
    .marker_forward_offset = &s_tdma_pio_spi_marker_forward_offset,
    .clk_burst_offset = &s_tdma_pio_spi_clk_burst_offset,
    .clk_capture_offset = &s_tdma_pio_spi_clk_capture_offset,
    .clk_coded_tx_offset = &s_tdma_pio_spi_clk_coded_tx_offset,
    .clk_oversample_offset = &s_tdma_pio_spi_clk_oversample_offset,
    .marker_origin_offset = &s_tdma_pio_spi_marker_origin_offset,
    .marker_capture_offset = &s_tdma_pio_spi_marker_capture_offset,
    .data_train_source_offset = &s_tdma_pio_spi_data_train_source_offset,
    .data_train_sink_offset = &s_tdma_pio_spi_data_train_sink_offset,
    .sck_train_trigger_offset = &s_tdma_pio_spi_sck_train_trigger_offset,
    .sck_train_source_offset = &s_tdma_pio_spi_sck_train_source_offset,
    .sck_train_sink_offset = &s_tdma_pio_spi_sck_train_sink_offset,
    .cal_tx_offset = &s_tdma_pio_spi_cal_tx_offset,
    .cal_capture_offset = &s_tdma_pio_spi_cal_capture_offset,
    .p3_initiator_offset = &s_tdma_pio_spi_p3_initiator_offset,
    .p3_responder_offset = &s_tdma_pio_spi_p3_responder_offset,
    .p3_capture_offset = &s_tdma_pio_spi_p3_capture_offset,
    .p3_responder_capture_offset = &s_tdma_pio_spi_p3_responder_capture_offset,
    .flight_origin_clock_offset = &s_tdma_pio_spi_flight_origin_clock_offset,
    .flight_origin_data_offset = &s_tdma_pio_spi_flight_origin_data_offset,
    .flight_data_follower_offset = &s_tdma_pio_spi_flight_data_follower_offset,
    .flight_process_follower_offset = &s_tdma_pio_spi_flight_process_follower_offset,
    .flight_control_forward_offset = &s_tdma_pio_spi_flight_control_forward_offset,
    .flight_clock_latch_offset = &s_tdma_pio_spi_flight_clock_latch_offset,
    .flight_origin_rtt_offset = &s_tdma_pio_spi_flight_origin_rtt_offset,
    .tx_dma_channel = &s_tdma_pio_spi_tx_dma_channel,
    .rx_dma_channel = &s_tdma_pio_spi_rx_dma_channel,
};
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

bool tdma_pio_spi_phys_select_program_persona(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona)
{
    return tdma_pio_spi_programs_select(
        &s_tdma_pio_spi_program_manager, phys, persona);
}

/* Compatibility entry point for calibration service includes.  The owner
 * context remains explicit inside the migrated program manager. */
static bool tdma_pio_spi_phys_ensure_sms_claimed(void)
{
    return tdma_pio_spi_programs_ensure_sms_claimed(
        &s_tdma_pio_spi_program_manager);
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

#include "tdma_pio_spi_phys_cal_control.inc"
#include "tdma_pio_spi_phys_cal_service.inc"
#include "tdma_pio_spi_phys_coded.inc"
#include "tdma_pio_spi_phys_marker_restored.inc"
#include "tdma_pio_spi_phys_data_train_restored.inc"
#include "tdma_pio_spi_phys_p3.inc"
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

#include "tdma_pio_spi_phys_flight_io.inc"
