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

#define TDMA_PIO_SPI_DEFAULT_BAUD_HZ 1000000u

/* Fixed frame window: 4-byte packet header + 32-byte TdmaTransportFrame idle
 * beacon. Every ring frame is exactly this long. */
#define TDMA_PIO_SPI_FIXED_RX_WORDS \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE)

/* Bounded TX FIFO wait: core1 must stay predictable (HAOFV). If the downlink
 * SM stops consuming (e.g. the config plane disarms it while core1 is mid
 * frame), put_blocking would hang core1 forever and break the flash lockout
 * protocol. Instead we wait at most this long per word and fail the frame. */

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
_Static_assert(TDMA_RX_DMA_RELOAD_FRAMES % TDMA_PIO_SPI_RX_RING_WORDS == 0u,
               "RX hardware count reload must preserve the SRAM ring index");
_Static_assert(TDMA_RX_DMA_COUNT_MAX == DMA_CH0_TRANS_COUNT_COUNT_BITS,
               "RX count admission must match the hardware field width");
_Static_assert(TDMA_RX_OBSERVATION_SCAN_WORDS + TDMA_PIO_SPI_RX_DMA_WORD_MAX <
                   TDMA_PIO_SPI_RX_RING_WORDS,
               "RX scan window must leave space before the DMA writer");
_Static_assert(TDMA_RX_OBSERVATION_SCAN_WORDS >= TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS,
               "RX resynchronization must cover a full physical frame");
_Static_assert(TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES >=
                   TDMA_PIO_SPI_RX_DMA_WORD_MAX,
               "TRN-03B TX capture must hold one maximum short packet");

static bool s_tdma_pio_spi_sms_claimed;
bool s_tdma_pio_spi_flight_sms_claimed;
static bool s_tdma_pio_spi_maintenance_resources_claimed;
tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
uint s_tdma_pio_spi_tx_offset;
uint s_tdma_pio_spi_rx_offset;
uint s_tdma_pio_spi_clk_forward_offset;
uint s_tdma_pio_spi_marker_forward_offset;
uint s_tdma_pio_spi_clk_burst_offset;
uint s_tdma_pio_spi_clk_capture_offset;
uint s_tdma_pio_spi_clk_coded_tx_offset;
uint s_tdma_pio_spi_clk_oversample_offset;
uint s_tdma_pio_spi_marker_origin_offset;
uint s_tdma_pio_spi_marker_capture_offset;
uint s_tdma_pio_spi_data_train_source_offset;
uint s_tdma_pio_spi_data_train_sink_offset;
uint s_tdma_pio_spi_sck_train_trigger_offset;
uint s_tdma_pio_spi_sck_train_source_offset;
uint s_tdma_pio_spi_sck_train_sink_offset;
static uint s_tdma_pio_spi_cal_tx_offset;
static uint s_tdma_pio_spi_cal_capture_offset;
uint s_tdma_pio_spi_p3_initiator_offset;
uint s_tdma_pio_spi_p3_responder_offset;
uint s_tdma_pio_spi_p3_capture_offset;
uint s_tdma_pio_spi_p3_responder_capture_offset;
uint s_tdma_pio_spi_flight_origin_clock_offset;
uint s_tdma_pio_spi_flight_origin_data_offset;
uint s_tdma_pio_spi_flight_origin_data_capture_offset;
uint s_tdma_pio_spi_flight_data_follower_offset;
uint s_tdma_pio_spi_flight_process_follower_offset;
uint s_tdma_pio_spi_flight_control_forward_offset;
static uint s_tdma_pio_spi_flight_clock_latch_offset;
static uint s_tdma_pio_spi_flight_rx_clock_latch_offset;
uint s_tdma_pio_spi_flight_origin_rtt_offset;
uint s_tdma_event_counter_offset;
uint s_tdma_event_sequence_offset;
uint32_t s_tdma_pio_spi_cal_ring[TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS]
    __attribute__((aligned(4)));
/* Calibration personas are mutually exclusive: the TDMA owner restores the
 * normal DATA/CS persona before accepting another request.  Keep one maximum
 * sized TX/RX workspace per direction instead of reserving coded, marker and
 * data-training arrays concurrently.  The named members preserve the
 * persona-specific bounds and call sites while the union makes the lifetime
 * contract explicit in the static RAM layout. */


tdma_pio_spi_cal_tx_workspace_t s_tdma_pio_spi_cal_tx_workspace
    __attribute__((aligned(4)));
tdma_pio_spi_cal_rx_workspace_t s_tdma_pio_spi_cal_rx_workspace
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
uint32_t s_tdma_pio_spi_sck_train_inject_word = 0u;
bool tdma_pio_spi_phys_cal_decode_step(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_set_line_drivers(bool enabled);
int s_tdma_pio_spi_tx_dma_channel = -1;
int s_tdma_pio_spi_rx_dma_channel = -1;
int s_tdma_pio_spi_command_dma_channel = -1;
int s_tdma_pio_spi_executor_dma_channel = -1;
tdma_pio_spi_program_manager_t s_tdma_pio_spi_program_manager = {
    .sms_claimed = &s_tdma_pio_spi_sms_claimed,
    .flight_sms_claimed = &s_tdma_pio_spi_flight_sms_claimed,
    .maintenance_resources_claimed =
        &s_tdma_pio_spi_maintenance_resources_claimed,
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
    .flight_origin_data_capture_offset =
        &s_tdma_pio_spi_flight_origin_data_capture_offset,
    .flight_data_follower_offset = &s_tdma_pio_spi_flight_data_follower_offset,
    .flight_process_follower_offset = &s_tdma_pio_spi_flight_process_follower_offset,
    .flight_control_forward_offset = &s_tdma_pio_spi_flight_control_forward_offset,
    .flight_clock_latch_offset = &s_tdma_pio_spi_flight_clock_latch_offset,
    .flight_rx_clock_latch_offset = &s_tdma_pio_spi_flight_rx_clock_latch_offset,
    .flight_origin_rtt_offset = &s_tdma_pio_spi_flight_origin_rtt_offset,
    .event_counter_offset = &s_tdma_event_counter_offset,
    .event_sequence_offset = &s_tdma_event_sequence_offset,
    .tx_dma_channel = &s_tdma_pio_spi_tx_dma_channel,
    .rx_dma_channel = &s_tdma_pio_spi_rx_dma_channel,
    .command_dma_channel = &s_tdma_pio_spi_command_dma_channel,
    .executor_dma_channel = &s_tdma_pio_spi_executor_dma_channel,
};
/* The ring needs 4 KiB alignment for DMA wrap; inside .bss that alignment
 * padded up to 4 KiB whenever .data grew. The DMA ring section is NOLOAD, so
 * tdma_pio_spi_phys_workspace_reset() restores the zeroed boot state. */
tdma_pio_spi_workspace_t s_tdma_pio_spi_workspace
    __attribute__((section(".sync_io_dma_ring")))
    __attribute__((aligned(TDMA_PIO_SPI_RX_RING_WORDS * sizeof(uint32_t))));
/* The cancellation handshake must outlive every persona in the union. */
tdma_origin_build_job_t s_tdma_origin_build_job;
/* Two resident plans: only selection of the successor retires the old pool.
 * A BUSY sample or physical frame IRQ does not authorize pool reuse. */
/* SRAM: PASS traffic must not depend on XIP cache latency. */
uint32_t s_tdma_pio_spi_flight_live_word = TDMA_FLIGHT_OVERLAY_LIVE_WORD;
_Static_assert(tdma_pio_spi_flight_process_follower_offset_final_bit == 16u,
               "Update terminal token admission for the installed PIO catalog");
_Static_assert(sizeof(tdma_pio_spi_flight_process_follower_program_instructions) /
                   sizeof(uint16_t) == 28u, "Process PIO instruction budget");
_Static_assert(sizeof(tdma_flight_overlay_dma_run_t) == 16u &&
               offsetof(dma_channel_hw_t, al3_ctrl) == 0x30u &&
               offsetof(dma_channel_hw_t, al3_read_addr_trig) == 0x3cu,
               "Command descriptors must match the RP2350 AL3 register alias");
uint64_t s_tdma_pio_spi_rx_scan_produced;
tdma_rx_dma_counter_t s_tdma_pio_spi_rx_sequence;
/* Core1-only boot-local diagnostic IDs. Neither STOP nor a new phys object
 * resets them. Exhaustion disables provenance, never healthy RX transport. */
uint64_t s_tdma_pio_spi_rx_arm_epoch;
uint64_t s_tdma_pio_spi_rx_capture_id;
bool s_tdma_pio_spi_rx_arm_valid;
/* Core1-private normalized bytes, never a DMA target. The live DMA ring
 * remains word-wide; only this assembled observation is compacted. */
uint8_t s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX]
    __attribute__((aligned(4)));

bool tdma_pio_spi_phys_is_flight_persona(void)
{
    return s_tdma_pio_spi_program_persona ==
               TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
           s_tdma_pio_spi_program_persona ==
               TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN ||
           s_tdma_pio_spi_program_persona ==
               TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER ||
           s_tdma_pio_spi_program_persona ==
               TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
}

PIO tdma_pio_spi_phys_control_pio(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && tdma_pio_spi_phys_is_flight_persona()
        ? phys->flight_resources.tx_pio : BOARD_TDMA_SPI_PIO;
}

uint tdma_pio_spi_phys_control_sm(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && tdma_pio_spi_phys_is_flight_persona()
        ? phys->flight_resources.tx_control_out_sm
        : (phys != NULL ? phys->tx_sm : BOARD_TDMA_SPI_MASTER_SM);
}

PIO tdma_pio_spi_phys_data_pio(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && tdma_pio_spi_phys_is_flight_persona()
        ? phys->flight_resources.rx_endpoints.data_output.pio
        : BOARD_TDMA_SPI_PIO;
}

uint tdma_pio_spi_phys_data_sm(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && tdma_pio_spi_phys_is_flight_persona()
        ? phys->flight_resources.rx_endpoints.data_output.sm
        : (phys != NULL ? phys->tx_sm : BOARD_TDMA_SPI_MASTER_SM);
}

PIO tdma_pio_spi_phys_capture_pio(const tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || !tdma_pio_spi_phys_is_flight_persona()) {
        return BOARD_TDMA_SPI_PIO;
    }
    return phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? phys->flight_resources.tx_pio
        : phys->flight_resources.rx_endpoints.data_unload.pio;
}

uint tdma_pio_spi_phys_capture_sm(const tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || !tdma_pio_spi_phys_is_flight_persona()) {
        return phys != NULL ? phys->rx_sm : BOARD_TDMA_SPI_SLAVE_SM;
    }
    return phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? phys->flight_resources.tx_data_capture_sm
        : phys->flight_resources.rx_endpoints.data_unload.sm;
}

PIO tdma_pio_spi_phys_evidence_pio(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && tdma_pio_spi_phys_is_flight_persona() &&
            phys->role == TDMA_PIO_SPI_ROLE_MASTER
        ? phys->flight_resources.tx_pio
        : (phys != NULL
               ? phys->flight_resources.rx_endpoints.clock_evidence.pio
               : BOARD_TDMA_RX_PIO);
}

uint tdma_pio_spi_phys_latch_sm(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && tdma_pio_spi_phys_is_flight_persona()
        ? (phys->role == TDMA_PIO_SPI_ROLE_MASTER
               ? phys->flight_resources.tx_clock_latch_sm
               : phys->flight_resources.rx_endpoints.clock_evidence.sm)
        : BOARD_TDMA_SPI_CAPTURE_SM;
}

uint tdma_pio_spi_phys_latch_offset(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && phys->role == TDMA_PIO_SPI_ROLE_SLAVE
        ? s_tdma_pio_spi_flight_rx_clock_latch_offset
        : s_tdma_pio_spi_flight_clock_latch_offset;
}

PIO tdma_pio_spi_phys_tx_latch_pio(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL ? phys->flight_resources.tx_pio : BOARD_TDMA_TX_PIO;
}

uint tdma_pio_spi_phys_tx_latch_sm(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL ? phys->flight_resources.tx_clock_latch_sm
                        : BOARD_TDMA_TX_CLOCK_LATCH_SM;
}

uint tdma_pio_spi_phys_rtt_sm(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL
        ? phys->flight_resources.tx_rtt_evidence_sm
        : BOARD_TDMA_TX_RTT_EVIDENCE_SM;
}

void tdma_pio_spi_phys_clk_train_write_begin(
    tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->clk_train_guard, 1u, __ATOMIC_RELEASE);
}

void tdma_pio_spi_phys_clk_train_write_end(
    tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->clk_train_guard, 1u, __ATOMIC_RELEASE);
}

void tdma_pio_spi_phys_clk_train_reset(tdma_pio_spi_phys_t *phys)
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

void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *phys,
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

bool tdma_pio_spi_phys_arm_reject(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_phys_error_t error)
{
    tdma_geometry_arm_failed();
    tdma_pio_spi_phys_set_error(phys, (uint32_t)error);
    return false;
}

void tdma_pio_spi_phys_publish_arm_error(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_phys_error_t error)
{
    tdma_geometry_arm_failed();
    tdma_pio_spi_phys_set_error(phys, (uint32_t)error);
}

uint32_t tdma_pio_spi_phys_last_error(const void *context)
{
    const tdma_pio_spi_phys_t *phys =
        (const tdma_pio_spi_phys_t *)context;
    return phys != NULL ? phys->snapshot.last_error
                        : TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT;
}

bool tdma_pio_spi_phys_tx_retryable(const void *context)
{
    const tdma_pio_spi_phys_t *phys =
        (const tdma_pio_spi_phys_t *)context;
    return phys != NULL &&
           phys->snapshot.last_error == TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY;
}

void tdma_pio_spi_phys_origin_record_invalidate(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    const uint32_t guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&phys->flight_origin_record_guard, guard + 1u, __ATOMIC_RELEASE);
    __dmb();
    __atomic_store_n(&phys->flight_origin_record_frozen, false, __ATOMIC_RELAXED);
    __atomic_store_n(&phys->flight_origin_first_readable_epoch, 0u, __ATOMIC_RELAXED);
    phys->flight_origin_live.active = 0u;
    __atomic_store_n(&phys->flight_origin_record_guard, guard + 2u, __ATOMIC_RELEASE);
}

void tdma_rx_first_window_admit(const tdma_ring_runtime_config_t *config);
void tdma_rx_first_window_arm_failed(void);

void tdma_pio_spi_phys_workspace_reset(void)
{
    memset(&s_tdma_pio_spi_workspace, 0, sizeof(s_tdma_pio_spi_workspace));
}

void tdma_pio_spi_phys_service_observer(tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_event_service(phys);
}

bool tdma_pio_spi_phys_get_event_snapshot(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_event_snapshot_t *snapshot)
{
    return tdma_pio_spi_phys_event_copy(phys, snapshot);
}

bool tdma_pio_spi_phys_get_event_sequence_evidence(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_event_sequence_evidence_t *out)
{
#if PROJECT_TDMA_EVENT_OBSERVER
    return tdma_pio_spi_phys_event_sequence_copy(phys, out);
#else
    if (out != NULL) *out = (tdma_pio_spi_event_sequence_evidence_t){0};
    return out != NULL;
#endif
}

bool tdma_pio_spi_phys_select_program_persona(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona)
{
    tdma_geometry_persona(persona);
    if (persona != s_tdma_pio_spi_program_persona) {
        tdma_priority_stop();
        tdma_pio_spi_phys_event_stop(phys);
#if PROJECT_TDMA_EVENT_OBSERVER && defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
        if (s_tdma_event_dma_active) return false;
#endif
    }
    tdma_pio_spi_phys_origin_record_invalidate(phys);
    const bool selected = tdma_pio_spi_programs_select(
        &s_tdma_pio_spi_program_manager, phys, persona);
    /* The manager may have frozen an old origin while stopping its tree.
     * A requested persona starts a new memory lifetime after it returns. */
    tdma_pio_spi_phys_origin_record_invalidate(phys);
    return selected;
}

/* Compatibility entry point for calibration service includes.  The owner
 * context remains explicit inside the migrated program manager. */
bool tdma_pio_spi_phys_ensure_sms_claimed(void)
{
    return tdma_pio_spi_programs_ensure_sms_claimed(
        &s_tdma_pio_spi_program_manager);
}

void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys)
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
    phys->snapshot.flight_origin_capture_phase_delay_cycles =
        phys->flight_origin_capture_phase_delay_cycles;
}

bool tdma_pio_spi_phys_ensure_rx_dma(void)
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

bool tdma_pio_spi_phys_ensure_tx_dma(void)
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
    tdma_pio_spi_phys_event_stop(phys);
#if PROJECT_TDMA_EVENT_OBSERVER && defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (s_tdma_event_dma_active) return;
#endif
    const PIO control_pio = tdma_pio_spi_phys_control_pio(phys);
    const PIO data_pio = tdma_pio_spi_phys_data_pio(phys);
    const PIO capture_pio = tdma_pio_spi_phys_capture_pio(phys);
    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint control_sm = tdma_pio_spi_phys_control_sm(phys);
    const uint data_sm = tdma_pio_spi_phys_data_sm(phys);
    const uint capture_sm = tdma_pio_spi_phys_capture_sm(phys);
    const uint latch_sm = tdma_pio_spi_phys_latch_sm(phys);
    const bool has_rtt_sm = phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN;
    /* A follower shares RX PIO with the legacy maintenance persona and does
     * not use every flight SM (notably the reserved control/evidence and the
     * origin RTT SM).  Clear the complete flight SM banks at the persona
     * boundary; disabling only role-selected SMs leaves a stale enabled bit
     * and makes the next flight->maintenance selection report BUSY. */
    if (tdma_pio_spi_phys_is_flight_persona()) {
        for (uint sm = 0u; sm < 4u; ++sm) {
            pio_sm_set_enabled(phys->flight_resources.tx_pio, sm, false);
            pio_sm_clear_fifos(phys->flight_resources.tx_pio, sm);
            pio_sm_restart(phys->flight_resources.tx_pio, sm);
            pio_sm_set_enabled(phys->flight_resources.rx_pio, sm, false);
            pio_sm_clear_fifos(phys->flight_resources.rx_pio, sm);
            pio_sm_restart(phys->flight_resources.rx_pio, sm);
        }
    } else {
        pio_sm_set_enabled(control_pio, control_sm, false);
        pio_sm_set_enabled(data_pio, data_sm, false);
        pio_sm_set_enabled(capture_pio, capture_sm, false);
        pio_sm_set_enabled(evidence_pio, latch_sm, false);
        pio_sm_clear_fifos(control_pio, control_sm);
        pio_sm_clear_fifos(data_pio, data_sm);
        pio_sm_clear_fifos(capture_pio, capture_sm);
        pio_sm_clear_fifos(evidence_pio, latch_sm);
        pio_sm_restart(control_pio, control_sm);
        pio_sm_restart(data_pio, data_sm);
        pio_sm_restart(capture_pio, capture_sm);
        pio_sm_restart(evidence_pio, latch_sm);
    }
    if (has_rtt_sm) {
        const uint rtt_sm = tdma_pio_spi_phys_rtt_sm(phys);
        pio_sm_clear_fifos(evidence_pio, rtt_sm);
        pio_sm_restart(evidence_pio, rtt_sm);
    }
}

/* Disabling an SM does not retire pending DMA FIFO accesses. Keep FIFO/PC
 * reset separate: a failed abort must retain both the pool and FIFO state. */
void tdma_pio_spi_phys_pause_sm_pair(tdma_pio_spi_phys_t *phys)
{
    if (tdma_pio_spi_phys_is_flight_persona()) {
        for (uint sm = 0u; sm < 4u; ++sm) {
            pio_sm_set_enabled(phys->flight_resources.tx_pio, sm, false);
            pio_sm_set_enabled(phys->flight_resources.rx_pio, sm, false);
        }
    } else {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM, false);
    }
}

void tdma_pio_spi_phys_disable_dma_mask(uint32_t mask)
{
    for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
        if ((mask & (1u << channel)) != 0u) {
            hw_clear_bits(&dma_hw->ch[channel].ctrl_trig,
                          DMA_CH0_CTRL_TRIG_EN_BITS);
        }
    }
}

bool tdma_pio_spi_phys_stop_dma_level(uint32_t mask, uint64_t deadline)
{
    if (mask == 0u) return true;
    /* Re-clear EN after upstream quiescence: an in-flight AL3 CTRL write
     * can undo the initial disable. RP2350-E5 also requires EN clear before
     * aborting a channel with downstream triggers still in flight. */
    tdma_pio_spi_phys_disable_dma_mask(mask);
    dma_hw->abort = mask;
    for (;;) {
        uint32_t pending = dma_hw->abort & mask;
        for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
            if ((mask & (1u << channel)) != 0u &&
                dma_channel_is_busy(channel)) pending |= 1u << channel;
        }
        if (pending == 0u) return true;
        if (tdma_pio_spi_phys_now_us() >= deadline) return false;
    }
}

/* Owner-supplied masks name dependency levels, not completion samples.
 * The follower has no executor level; origin integration can insert its
 * executor between the loader and both finite DATA children. Every level
 * shares one deadline, and failure authorizes no pool/FIFO/program reuse. */
bool tdma_pio_spi_phys_stop_dma_chain(uint32_t loader_mask,
                                            uint32_t executor_mask,
                                            uint32_t children_mask,
                                            uint64_t deadline)
{
    const uint32_t all = loader_mask | executor_mask | children_mask;
    tdma_pio_spi_phys_disable_dma_mask(all);
    if (!tdma_pio_spi_phys_stop_dma_level(loader_mask, deadline) ||
        !tdma_pio_spi_phys_stop_dma_level(executor_mask, deadline) ||
        !tdma_pio_spi_phys_stop_dma_level(children_mask, deadline)) {
        tdma_pio_spi_phys_disable_dma_mask(all);
        return false;
    }
    __dmb();
    return true;
}

bool tdma_pio_spi_phys_stop_command_dma(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return false;
    tdma_priority_stop();
    /* Cancel the live sample even if a later hardware STOP fails. Historical
     * evidence stays readable; no copied record grants a running lease. */
    if (phys->flight_origin_live.active != 0u) {
        const uint32_t live_guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_RELAXED);
        __atomic_store_n(&phys->flight_origin_record_guard, live_guard + 1u, __ATOMIC_RELEASE);
        __dmb();
        phys->flight_origin_live.active = 0u;
        __atomic_store_n(&phys->flight_origin_record_guard, live_guard + 2u, __ATOMIC_RELEASE);
    }
    const bool builder_retired = tdma_origin_build_job_cancel(&s_tdma_origin_build_job);
    const bool completed_origin = phys->flight_origin_workspace_owned &&
        phys->flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_COMPLETE;
    const uint32_t loader_mask = s_tdma_pio_spi_command_dma_channel < 0 ? 0u :
        1u << (uint)s_tdma_pio_spi_command_dma_channel;
    const uint32_t executor_mask = s_tdma_pio_spi_executor_dma_channel < 0 ? 0u :
        1u << (uint)s_tdma_pio_spi_executor_dma_channel;
    const uint32_t children_mask =
        (s_tdma_pio_spi_tx_dma_channel < 0 ? 0u :
            1u << (uint)s_tdma_pio_spi_tx_dma_channel) |
        (s_tdma_pio_spi_rx_dma_channel < 0 ? 0u :
            1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    const uint64_t deadline = tdma_pio_spi_phys_now_us() +
                              TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US;
    tdma_pio_spi_phys_pause_sm_pair(phys);
    if (!tdma_pio_spi_phys_stop_dma_chain(loader_mask, executor_mask, children_mask,
                                         deadline)) goto failed;
    if (!builder_retired) goto failed;
    tdma_pio_spi_phys_pause_sm_pair(phys);
    __dmb();
    if (completed_origin) {
        const uint32_t guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_RELAXED);
        __atomic_store_n(&phys->flight_origin_record_guard, guard + 1u, __ATOMIC_RELEASE);
        __dmb();
        __atomic_store_n(&phys->flight_origin_record_published_version,
            s_tdma_pio_spi_workspace.origin.state.record_published_version, __ATOMIC_RELAXED);
        __atomic_store_n(&phys->flight_origin_record_fault,
            s_tdma_pio_spi_workspace.origin.state.fault, __ATOMIC_RELAXED);
        __atomic_store_n(&phys->flight_origin_record_frozen, true, __ATOMIC_RELAXED);
        __atomic_store_n(&phys->flight_origin_record_guard, guard + 2u, __ATOMIC_RELEASE);
    }
    phys->flight_overlay_dma_active = false;
    phys->rx_capture_active = false;
    phys->flight_origin_workspace_owned = false;
    phys->flight_origin_rx_observation_ready = false;
    return true;
failed:
    tdma_pio_spi_phys_set_line_drivers(false);
    tdma_pio_spi_phys_pause_sm_pair(phys);
    /* A pending write can still reach a child after failure. Leave memory,
     * FIFOs and IRQs intact and retain the ownership latch until STOP retry. */
    phys->armed = false;
    phys->flight_overlay_dma_active = true;
    phys->snapshot.armed = 0u;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY;
    return false;
}

void tdma_pio_spi_phys_release_flight_resources(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return;
    }
    tdma_pio_spi_phys_event_stop(phys);
#if PROJECT_TDMA_EVENT_OBSERVER && defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (s_tdma_event_dma_active) return;
#endif
    if (!tdma_pio_spi_phys_stop_command_dma(phys)) return;
    /* Release the persona that is actually selected.  Hard-coding ORIGIN
     * leaves follower/process-follower programs resident and makes the next
     * maintenance/coarse transition fail with a false PIO ownership clash. */
    tdma_pio_spi_programs_release_resources(
        &s_tdma_pio_spi_program_manager,
        phys,
        s_tdma_pio_spi_program_persona);
}

void tdma_pio_spi_phys_enable_sm_pair(tdma_pio_spi_phys_t *phys)
{
    pio_sm_set_enabled(tdma_pio_spi_phys_control_pio(phys),
                       tdma_pio_spi_phys_control_sm(phys), true);
    pio_sm_set_enabled(tdma_pio_spi_phys_data_pio(phys),
                       tdma_pio_spi_phys_data_sm(phys), true);
    pio_sm_set_enabled(tdma_pio_spi_phys_capture_pio(phys),
                       tdma_pio_spi_phys_capture_sm(phys), true);
    pio_sm_set_enabled(tdma_pio_spi_phys_evidence_pio(phys),
                       tdma_pio_spi_phys_latch_sm(phys), true);
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE && !tdma_pio_spi_phys_event_selected(phys)) {
        pio_sm_set_enabled(tdma_pio_spi_phys_tx_latch_pio(phys),
                           tdma_pio_spi_phys_tx_latch_sm(phys), true);
    }
    if (phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN) {
        pio_sm_set_enabled(tdma_pio_spi_phys_evidence_pio(phys),
                           tdma_pio_spi_phys_rtt_sm(phys), true);
    }
}

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
    const uint32_t resolution_ns = tdma_pio_spi_phys_latch_resolution_ns(clk_hz);
    if (resolution_ns == 0u) {
        return false;
    }

    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint sm = tdma_pio_spi_phys_latch_sm(phys);
    pio_sm_set_enabled(evidence_pio, sm, false);
    pio_sm_clear_fifos(evidence_pio, sm);
    pio_sm_restart(evidence_pio, sm);
    /* The clock-latch program consumes X only. Seed it directly while the
     * SM is disabled; no FIFO transfer or blocking PULL is needed. */
    pio_sm_exec(evidence_pio, sm, pio_encode_mov_not(pio_x, pio_null));
    pio_sm_exec(evidence_pio,
                sm,
                pio_encode_jmp(tdma_pio_spi_phys_latch_offset(phys)));
    phys->flight_clock_latch_epoch_ns = vdc_timestamp_clock_now_ns();
    phys->flight_clock_latch_resolution_ns = resolution_ns;
    phys->snapshot.clock_latch_resolution_ns = resolution_ns;
    phys->flight_clock_latch_armed = true;
    pio_sm_set_enabled(evidence_pio, sm, true);
    return true;
}

bool tdma_pio_spi_phys_tx_clock_latch_rearm(
    tdma_pio_spi_phys_t *phys)
{
    if (tdma_pio_spi_phys_event_selected(phys)) return true;
    if (phys == NULL || phys->role != TDMA_PIO_SPI_ROLE_SLAVE ||
        !tdma_pio_spi_phys_is_flight_persona()) {
        return false;
    }
    const uint32_t clk_hz = clock_get_hz(clk_sys);
    if (clk_hz == 0u) {
        return false;
    }
    const uint32_t resolution_ns = tdma_pio_spi_phys_latch_resolution_ns(clk_hz);
    if (resolution_ns == 0u) {
        return false;
    }
    const PIO pio = tdma_pio_spi_phys_tx_latch_pio(phys);
    const uint sm = tdma_pio_spi_phys_tx_latch_sm(phys);
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    /* Same X-only clock-latch program as RX, on the owner's TX endpoint. */
    pio_sm_exec(pio, sm, pio_encode_mov_not(pio_x, pio_null));
    pio_sm_exec(pio, sm,
                pio_encode_jmp(s_tdma_pio_spi_flight_clock_latch_offset));
    phys->flight_tx_clock_latch_epoch_ns = vdc_timestamp_clock_now_ns();
    phys->flight_tx_clock_latch_resolution_ns = resolution_ns;
    phys->flight_tx_clock_latch_armed = true;
    pio_sm_set_enabled(pio, sm, true);
    return true;
}

static bool tdma_pio_spi_phys_capture_owns_clock_latch(
    const tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return false;
    }
    switch (phys->flight_sck_waveform_capture_state) {
    case TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_REQUESTED:
    case TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_PATCHED:
    case TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED:
    case TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY:
        return true;
    default:
        return false;
    }
}

bool tdma_pio_spi_phys_clock_latch_read_and_rearm(
    tdma_pio_spi_phys_t *phys,
    uint64_t *timestamp_ns)
{
    /* The normal origin also uses this helper for TX completion. Only the
     * slave's RX extraction belongs to the RX_LATCH timing parent. */
    const bool rx_probe = phys != NULL && phys->role == TDMA_PIO_SPI_ROLE_SLAVE;
    const uint64_t read_start = rx_probe ? tdma_service_timing_now() : 0ull;
    if (timestamp_ns != NULL) {
        *timestamp_ns = 0ull;
    }
    /* The capture lifecycle owns this SM from the first request until the
     * immutable SCK FIFO has been copied and the resident latch restored.
     * Treating capture data as a normal edge latch would consume its FIFO
     * word and reinstall the clock-latch persona before capture completes. */
    if (phys == NULL || timestamp_ns == NULL) {
        if (rx_probe) tdma_service_timing_record(TDMA_TIMING_RX_LATCH_READ, read_start);
        return false;
    }
    const PIO evidence_pio = tdma_pio_spi_phys_evidence_pio(phys);
    const uint latch_sm = tdma_pio_spi_phys_latch_sm(phys);
    if (
        tdma_pio_spi_phys_capture_owns_clock_latch(phys) ||
        !phys->flight_clock_latch_armed ||
        pio_sm_is_rx_fifo_empty(evidence_pio, latch_sm)) {
        phys->snapshot.clock_latch_miss_count++;
        if (rx_probe) tdma_service_timing_record(TDMA_TIMING_RX_LATCH_READ, read_start);
        return false;
    }

    const uint32_t remaining = pio_sm_get(evidence_pio, latch_sm);
    const uint64_t elapsed_count = (uint64_t)UINT32_MAX - remaining;
    const uint64_t elapsed_ns = elapsed_count *
        (uint64_t)phys->flight_clock_latch_resolution_ns;
    if (rx_probe) tdma_service_timing_record(TDMA_TIMING_RX_LATCH_READ, read_start);
    if (UINT64_MAX - phys->flight_clock_latch_epoch_ns < elapsed_ns) {
        phys->snapshot.clock_latch_miss_count++;
        const uint64_t rearm_start = rx_probe ? tdma_service_timing_now() : 0ull;
        (void)tdma_pio_spi_phys_clock_latch_rearm(phys);
        if (rx_probe) tdma_service_timing_record(TDMA_TIMING_RX_LATCH_REARM, rearm_start);
        return false;
    }
    *timestamp_ns = phys->flight_clock_latch_epoch_ns + elapsed_ns;
    phys->snapshot.clock_latch_count++;
    const uint64_t rearm_start = rx_probe ? tdma_service_timing_now() : 0ull;
    const bool rearmed = tdma_pio_spi_phys_clock_latch_rearm(phys);
    if (rx_probe) tdma_service_timing_record(TDMA_TIMING_RX_LATCH_REARM, rearm_start);
    return rearmed;
}

static bool tdma_pio_spi_phys_tx_clock_latch_read_and_rearm(
    tdma_pio_spi_phys_t *phys,
    uint64_t *timestamp_ns)
{
    const uint64_t read_start = tdma_service_timing_now();
    if (timestamp_ns != NULL) {
        *timestamp_ns = 0ull;
    }
    if (tdma_pio_spi_phys_event_selected(phys)) {
        /* Paired observer FIFO words cannot be consumed by the legacy latch.
         * Its raw diagnostics are not yet eligible timestamp evidence. */
        tdma_service_timing_record(TDMA_TIMING_TX_LATCH_READ, read_start);
        return false;
    }
    if (phys == NULL || timestamp_ns == NULL ||
        phys->role != TDMA_PIO_SPI_ROLE_SLAVE ||
        !phys->flight_tx_clock_latch_armed) {
        tdma_service_timing_record(TDMA_TIMING_TX_LATCH_READ, read_start);
        return false;
    }
    const PIO pio = tdma_pio_spi_phys_tx_latch_pio(phys);
    const uint sm = tdma_pio_spi_phys_tx_latch_sm(phys);
    if (pio_sm_is_rx_fifo_empty(pio, sm)) {
        phys->snapshot.clock_latch_miss_count++;
        tdma_service_timing_record(TDMA_TIMING_TX_LATCH_READ, read_start);
        return false;
    }
    const uint32_t remaining = pio_sm_get(pio, sm);
    const uint64_t elapsed_count = (uint64_t)UINT32_MAX - remaining;
    const uint64_t elapsed_ns = elapsed_count *
        (uint64_t)phys->flight_tx_clock_latch_resolution_ns;
    tdma_service_timing_record(TDMA_TIMING_TX_LATCH_READ, read_start);
    if (UINT64_MAX - phys->flight_tx_clock_latch_epoch_ns < elapsed_ns) {
        phys->snapshot.clock_latch_miss_count++;
        const uint64_t rearm_start = tdma_service_timing_now();
        (void)tdma_pio_spi_phys_tx_clock_latch_rearm(phys);
        tdma_service_timing_record(TDMA_TIMING_TX_LATCH_REARM, rearm_start);
        return false;
    }
    *timestamp_ns = phys->flight_tx_clock_latch_epoch_ns + elapsed_ns;
    phys->snapshot.clock_latch_count++;
    const uint64_t rearm_start = tdma_service_timing_now();
    const bool rearmed = tdma_pio_spi_phys_tx_clock_latch_rearm(phys);
    tdma_service_timing_record(TDMA_TIMING_TX_LATCH_REARM, rearm_start);
    return rearmed;
}

bool tdma_pio_spi_phys_take_local_tx_edge(void *context,
                                          uint64_t *tx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = 0ull;
    }
    if (phys == NULL || tx_timestamp_ns == NULL || !phys->armed ||
        phys->role != TDMA_PIO_SPI_ROLE_SLAVE) {
        return false;
    }
    return tdma_pio_spi_phys_tx_clock_latch_read_and_rearm(
        phys, tx_timestamp_ns);
}

