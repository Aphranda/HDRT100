#ifndef TDMA_PIO_SPI_PHYS_H
#define TDMA_PIO_SPI_PHYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_ring_runtime.h"
#include "tdma_transport_frame.h"

/* TDMA PIO SPI resident physical layer (bring-up, half-duplex ring).
 *
 * P0.5-3 topology (ring + half duplex, one RX leg + one TX leg per board):
 * every board carries two independent PIO SMs:
 *   - TX leg (SPI master, downlink): drives frame-sync/CS + SCK + data out
 *     toward the next board in the ring (C_n -> C_{n+1}). One TX direction
 *     only.
 *   - RX leg (SPI slave, uplink): follows the frame-sync/CS and SCK driven by
 *     the previous board and samples the data coming from it (C_{n-1} -> C_n).
 *     One RX direction only.
 * The reference board (slot == reference_slot) originates one IDLE_BEACON per
 * TDMA cycle on its TX leg; every follower receives on its RX leg, advances
 * the hop and re-emits on its TX leg, so the frame travels once around the
 * ring (C1 -> C2 -> C3 -> ... -> C1) and the reference receives its own
 * frame back on the RX leg.
 *
 * Product-board wiring:
 *   Cn downlink: CS=26, TX=29, SCK=25 -> Cn+1 uplink: CS=27, RX=24, SCK=28.
 * The TX-side CS is a point-to-point frame-sync signal, not a bus chip select.
 *
 * This layer is byte/transport level only: it carries the 4-byte packet
 * header (magic + length) plus the TdmaTransportFrame body and never parses
 * VDC/RefMem inner frames.
 */

#define TDMA_PIO_SPI_PACKET_MAGIC0 0x54u
#define TDMA_PIO_SPI_PACKET_MAGIC1 0x44u
#define TDMA_PIO_SPI_PACKET_HEADER_SIZE 4u
#define TDMA_PIO_SPI_RX_DMA_WORD_MAX \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX)
#define TDMA_PIO_SPI_RX_STABLE_US 1000u

/* Continuous RX capture ring (EtherCAT-style): the DMA stays armed for the
 * entire session and wraps its write address in SRAM. The CPU only scans
 * completed words for the outer magic/length header, so there is no
 * per-frame abort, FIFO clear, or DMA reconfiguration gap. */
#define TDMA_PIO_SPI_RX_RING_WORDS 1024u
#define TDMA_PIO_SPI_RX_RING_LOG2 12u
#define TDMA_PIO_SPI_TX_DMA_CHANNEL \
    TDMA_PROFILE_DEFAULT_TX_DMA_CHANNEL_ID
#define TDMA_PIO_SPI_RX_DMA_CHANNEL \
    TDMA_PROFILE_DEFAULT_RX_DMA_CHANNEL_ID
#define TDMA_PIO_SPI_TRAIN_CLOCK_DEFAULT_CYCLES 4096u
#define TDMA_PIO_SPI_TRAIN_CLOCK_MAX_CYCLES 65536u
#define TDMA_PIO_SPI_TRAIN_RETURN_TIMEOUT_NS 100000000ull
#define TDMA_PIO_SPI_CODED_BUFFER_WORDS 256u
#define TDMA_PIO_SPI_CODED_SNAPSHOT_VERSION 1u
#define TDMA_PIO_SPI_MARKER_BUFFER_WORDS 512u
#define TDMA_PIO_SPI_MARKER_SAMPLES_PER_WORD 16u
#define TDMA_PIO_SPI_MARKER_SNAPSHOT_VERSION 1u
#define TDMA_PIO_SPI_MARKER_RETURN_GUARD_SAMPLES 256u
#define TDMA_PIO_SPI_MARKER_MIN_OFFSET_SAMPLES (-10)
#define TDMA_PIO_SPI_MARKER_MAX_OFFSET_SAMPLES 10
#define TDMA_PIO_SPI_MARKER_FORWARD_DELAY_CYCLES 1u
#define TDMA_PIO_SPI_MARKER_MAX_CAPTURE_DELAY_CYCLES 31u
#define TDMA_PIO_SPI_MARKER_TIMEOUT_NS 3000000000ull
#define TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS 256u
#define TDMA_PIO_SPI_DATA_TRAIN_SNAPSHOT_VERSION 1u
#define TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES 32u
#define TDMA_PIO_SPI_DATA_TRAIN_MAX_DELAY_CYCLES 1000000u
#define TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS 3000000000ull
#define TDMA_PIO_SPI_CLK_TRAIN_SNAPSHOT_VERSION 1u
#define TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS 256u
#define TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_HZ 50000000u
#define TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_WORDS 128u
#define TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_PIO_DMA (1u << 0u)
#define TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_DIAGNOSTIC_ONLY (1u << 1u)
#define TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_SYNC_MATCH (1u << 2u)
#define TDMA_PIO_SPI_FRAME_WORDS \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE)

typedef enum {
    TDMA_PIO_SPI_PHYS_ERROR_NONE = 0u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT = 1u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_ROLE = 2u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET = 3u,
    TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE = 4u,
    TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY = 5u,
    TDMA_PIO_SPI_PHYS_ERROR_RESOURCE_CONFLICT = 6u,
} tdma_pio_spi_phys_error_t;

typedef enum {
    TDMA_PIO_SPI_ROLE_MASTER = 0u,
    TDMA_PIO_SPI_ROLE_SLAVE = 1u,
} tdma_pio_spi_role_t;

typedef enum {
    TDMA_PIO_SPI_CLK_TRAIN_IDLE = 0u,
    TDMA_PIO_SPI_CLK_TRAIN_FORWARDING = 1u,
    TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING = 2u,
    TDMA_PIO_SPI_CLK_TRAIN_MASTER_COMPLETE = 3u,
    TDMA_PIO_SPI_CLK_TRAIN_ERROR = 4u,
} tdma_pio_spi_clk_train_state_t;

typedef enum {
    TDMA_PIO_SPI_CLK_TRAIN_RESULT_NONE = 0u,
    TDMA_PIO_SPI_CLK_TRAIN_RESULT_FORWARD_ARMED = 1u,
    TDMA_PIO_SPI_CLK_TRAIN_RESULT_RETURN_OVERLAP = 2u,
    TDMA_PIO_SPI_CLK_TRAIN_RESULT_NO_OVERLAP = 3u,
    TDMA_PIO_SPI_CLK_TRAIN_RESULT_REJECTED = 4u,
    TDMA_PIO_SPI_CLK_TRAIN_RESULT_RETURN_TIMEOUT = 5u,
} tdma_pio_spi_clk_train_result_t;

typedef enum {
    TDMA_PIO_SPI_PROGRAM_PERSONA_NONE = 0u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL = 1u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE = 2u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK = 3u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED = 4u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR = 5u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER = 6u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR = 7u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER = 8u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER = 9u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN = 10u,
} tdma_pio_spi_program_persona_t;

typedef enum {
    TDMA_PIO_SPI_DATA_TRAIN_IDLE = 0u,
    TDMA_PIO_SPI_DATA_TRAIN_ARMED = 1u,
    TDMA_PIO_SPI_DATA_TRAIN_RUNNING = 2u,
    TDMA_PIO_SPI_DATA_TRAIN_COMPLETE = 3u,
    TDMA_PIO_SPI_DATA_TRAIN_ERROR = 4u,
} tdma_pio_spi_data_train_state_t;

typedef enum {
    TDMA_PIO_SPI_DATA_TRAIN_ROLE_NONE = 0u,
    /* The initiator emits CS/marker downstream and captures the DATA return. */
    TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR = 1u,
    /* The responder waits for incoming CS and emits DATA upstream. */
    TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER = 2u,
} tdma_pio_spi_data_train_role_t;

typedef enum {
    TDMA_PIO_SPI_DATA_TRAIN_REJECT_NONE = 0u,
    TDMA_PIO_SPI_DATA_TRAIN_REJECT_BAD_ARGUMENT = 1u,
    TDMA_PIO_SPI_DATA_TRAIN_REJECT_RESOURCE = 2u,
    TDMA_PIO_SPI_DATA_TRAIN_REJECT_DMA = 3u,
    TDMA_PIO_SPI_DATA_TRAIN_REJECT_CAPTURE_SHORT = 4u,
    TDMA_PIO_SPI_DATA_TRAIN_REJECT_PIO_STALL = 5u,
    TDMA_PIO_SPI_DATA_TRAIN_REJECT_TIMEOUT = 6u,
} tdma_pio_spi_data_train_reject_t;

#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_MARKER (1u << 1u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_DATA (1u << 2u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_MARKER_DMA_COMPLETE (1u << 3u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_DATA_DMA_COMPLETE (1u << 4u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_SOURCE_IRQ (1u << 5u)

typedef struct {
    uint32_t role;
    const uint32_t *marker_words;
    uint32_t marker_word_count;
    const uint32_t *data_words;
    uint32_t data_word_count;
    uint32_t data_sample_count;
    uint32_t capture_sample_count;
    uint32_t marker_to_data_delay_cycles;
    uint32_t source_phase_delay_cycles;
    uint32_t phase_delay_cycles;
    uint32_t epoch;
} tdma_pio_spi_data_train_request_t;

typedef struct {
    uint32_t version;
    uint32_t state;
    uint32_t role;
    uint32_t flags;
    uint32_t reject_reason;
    uint32_t epoch;
    uint32_t marker_word_count;
    uint32_t data_word_count;
    uint32_t data_sample_count;
    uint32_t capture_word_count;
    uint32_t capture_sample_count;
    uint32_t marker_to_data_delay_cycles;
    uint32_t source_phase_delay_cycles;
    uint32_t phase_delay_cycles;
    uint32_t marker_dma_remaining;
    uint32_t data_dma_remaining;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t timeout_count;
    uint64_t marker_capture_tick;
    uint64_t data_capture_tick;
} tdma_pio_spi_data_train_snapshot_t;

typedef enum {
    TDMA_PIO_SPI_MARKER_IDLE = 0u,
    TDMA_PIO_SPI_MARKER_ARMED = 1u,
    TDMA_PIO_SPI_MARKER_RUNNING = 2u,
    TDMA_PIO_SPI_MARKER_COMPLETE = 3u,
    TDMA_PIO_SPI_MARKER_ERROR = 4u,
} tdma_pio_spi_marker_state_t;

typedef enum {
    TDMA_PIO_SPI_MARKER_ROLE_NONE = 0u,
    TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR = 1u,
    TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER = 2u,
} tdma_pio_spi_marker_role_t;

typedef enum {
    TDMA_PIO_SPI_MARKER_REJECT_NONE = 0u,
    TDMA_PIO_SPI_MARKER_REJECT_BAD_ARGUMENT = 1u,
    TDMA_PIO_SPI_MARKER_REJECT_RESOURCE = 2u,
    TDMA_PIO_SPI_MARKER_REJECT_DMA = 3u,
    TDMA_PIO_SPI_MARKER_REJECT_CAPTURE_SHORT = 4u,
    TDMA_PIO_SPI_MARKER_REJECT_PIO_STALL = 5u,
    TDMA_PIO_SPI_MARKER_REJECT_TIMEOUT = 6u,
    TDMA_PIO_SPI_MARKER_REJECT_EDGE_MISSING = 7u,
} tdma_pio_spi_marker_reject_t;

#define TDMA_PIO_SPI_MARKER_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define TDMA_PIO_SPI_MARKER_FLAG_TX_DMA_COMPLETE (1u << 1u)
#define TDMA_PIO_SPI_MARKER_FLAG_RX_DMA_COMPLETE (1u << 2u)
#define TDMA_PIO_SPI_MARKER_FLAG_HARDWARE_CAPTURE (1u << 3u)
#define TDMA_PIO_SPI_MARKER_FLAG_INPUT_EDGE (1u << 4u)
#define TDMA_PIO_SPI_MARKER_FLAG_OUTPUT_EDGE (1u << 5u)
#define TDMA_PIO_SPI_MARKER_FLAG_RETURN_EDGE (1u << 6u)

typedef struct {
    uint32_t role;
    const uint32_t *tx_words;
    uint32_t tx_word_count;
    uint32_t marker_sample_count;
    uint32_t capture_sample_count;
    uint32_t epoch;
    int32_t offset_sample_count;
    uint32_t capture_phase_delay_cycles;
} tdma_pio_spi_marker_request_t;

typedef struct {
    uint32_t version;
    uint32_t state;
    uint32_t role;
    uint32_t flags;
    uint32_t reject_reason;
    uint32_t epoch;
    uint32_t tx_word_count;
    uint32_t marker_sample_count;
    uint32_t capture_word_count;
    uint32_t capture_sample_count;
    uint32_t tx_dma_remaining;
    uint32_t rx_dma_remaining;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t timeout_count;
    uint64_t marker_capture_tick;
    uint64_t marker_forward_tick;
    uint64_t marker_return_tick;
} tdma_pio_spi_marker_snapshot_t;

typedef enum {
    TDMA_PIO_SPI_P3_IDLE = 0u,
    TDMA_PIO_SPI_P3_ARMED = 1u,
    TDMA_PIO_SPI_P3_COMPLETE = 2u,
    TDMA_PIO_SPI_P3_ERROR = 3u,
} tdma_pio_spi_p3_state_t;

typedef enum {
    TDMA_PIO_SPI_P3_ROLE_NONE = 0u,
    TDMA_PIO_SPI_P3_ROLE_INITIATOR = 1u,
    TDMA_PIO_SPI_P3_ROLE_RESPONDER = 2u,
} tdma_pio_spi_p3_role_t;

/* P3 measures a selected forward line against the DATA return line.  The
 * CLK/CS names are physical net labels only; the unselected line is used as
 * the sync marker for that trial. */
typedef enum {
    TDMA_PIO_SPI_P3_GROUP_CLK_DATA = 0u,
    TDMA_PIO_SPI_P3_GROUP_CS_DATA = 1u,
} tdma_pio_spi_p3_signal_group_t;

#define TDMA_PIO_SPI_P3_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define TDMA_PIO_SPI_P3_FLAG_HARDWARE_LATCHED (1u << 1u)
#define TDMA_PIO_SPI_P3_FLAG_DMA_COMPLETE (1u << 2u)
#define TDMA_PIO_SPI_P3_FLAG_SYNC_MATCH (1u << 3u)

typedef struct {
    uint32_t role;
    uint32_t baud_hz;
    uint32_t pulse_count;
    uint32_t capture_words;
    uint32_t epoch;
    uint32_t signal_group;
} tdma_pio_spi_p3_request_t;

typedef struct {
    uint32_t state;
    uint32_t role;
    uint32_t signal_group;
    uint32_t flags;
    uint32_t reject_reason;
    uint32_t baud_hz;
    uint32_t epoch;
    uint32_t sample_period_ns;
    uint32_t pulse_count;
    uint32_t requested_words;
    uint32_t produced_words;
    uint32_t edge_mask;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t clock_high_ns;
    uint32_t clock_low_ns;
    uint32_t data_high_ns;
    /* ABI-compatible names retained for SCPI/tool parsing.  Semantics are
     * t1=forward TX, t2=forward RX, t3=return(DATA) TX, t4=return RX. */
    uint64_t t1_clk_tx;
    uint64_t t2_clk_rx;
    uint64_t t3_data_tx;
    uint64_t t4_data_rx;
} tdma_pio_spi_p3_snapshot_t;

typedef enum {
    TDMA_PIO_SPI_CODED_IDLE = 0u,
    TDMA_PIO_SPI_CODED_FORWARDING = 1u,
    TDMA_PIO_SPI_CODED_RUNNING = 2u,
    TDMA_PIO_SPI_CODED_COMPLETE = 3u,
    TDMA_PIO_SPI_CODED_ERROR = 4u,
} tdma_pio_spi_coded_state_t;

typedef enum {
    TDMA_PIO_SPI_CODED_REJECT_NONE = 0u,
    TDMA_PIO_SPI_CODED_REJECT_BAD_ARGUMENT = 1u,
    TDMA_PIO_SPI_CODED_REJECT_RESOURCE = 2u,
    TDMA_PIO_SPI_CODED_REJECT_DMA = 3u,
    TDMA_PIO_SPI_CODED_REJECT_CAPTURE_SHORT = 4u,
    TDMA_PIO_SPI_CODED_REJECT_PIO_STALL = 5u,
} tdma_pio_spi_coded_reject_t;

#define TDMA_PIO_SPI_CODED_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define TDMA_PIO_SPI_CODED_FLAG_TX_DMA_COMPLETE  (1u << 1u)
#define TDMA_PIO_SPI_CODED_FLAG_RX_DMA_COMPLETE  (1u << 2u)
#define TDMA_PIO_SPI_CODED_FLAG_FORWARD_ONLY     (1u << 3u)

typedef struct {
    const uint32_t *tx_words;
    uint32_t tx_word_count;
    uint32_t tx_sample_count;
    uint32_t capture_sample_count;
    uint32_t timing_field_tx_origin_sample;
    uint32_t epoch;
} tdma_pio_spi_coded_request_t;

typedef struct {
    uint32_t version;
    uint32_t state;
    uint32_t role;
    uint32_t flags;
    uint32_t reject_reason;
    uint32_t epoch;
    uint32_t tx_dma_channel;
    uint32_t rx_dma_channel;
    uint32_t tx_word_count;
    uint32_t tx_sample_count;
    uint32_t capture_word_count;
    uint32_t capture_sample_count;
    uint32_t timing_field_tx_origin_sample;
    uint32_t tx_dma_remaining;
    uint32_t rx_dma_remaining;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint64_t capture_origin_tick;
} tdma_pio_spi_coded_snapshot_t;

typedef struct {
    uint32_t version;
    uint32_t state;
    uint32_t result;
    uint32_t role;
    uint32_t request_seq;
    uint32_t service_count;
    uint32_t baud_hz;
    uint32_t requested_cycles;
    uint32_t return_seen;
    uint32_t return_before_tx_done;
    uint32_t tx_sck_pin;
    uint32_t rx_sck_pin;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint64_t tx_start_timestamp_ns;
    uint64_t tx_done_observed_timestamp_ns;
    uint64_t return_observed_timestamp_ns;
    uint64_t burst_duration_ns;
} tdma_pio_spi_clk_train_snapshot_t;

typedef struct {
    uint32_t armed;
    uint32_t complete;
    uint32_t sample_hz;
    uint32_t sample_period_ns;
    uint32_t requested_words;
    uint32_t produced_words;
    uint32_t edge_mask;
    uint32_t flags;
    uint32_t reject_reason;
    uint32_t epoch;
    uint64_t t1_clk_tx;
    uint64_t t2_clk_rx;
    uint64_t t3_data_tx;
    uint64_t t4_data_rx;
} tdma_pio_spi_cal_loopback_snapshot_t;

typedef struct {
    uint32_t armed;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_bad_count;
    uint32_t tx_busy_count;
    uint32_t last_error;
    uint32_t last_tx_size;
    uint32_t last_rx_size;
    uint64_t last_rx_timestamp_ns;
    uint32_t tx_sck_pin;
    uint32_t tx_csn_pin;
    uint32_t tx_pin;
    uint32_t rx_sck_pin;
    uint32_t rx_csn_pin;
    uint32_t rx_pin;
    /* RX capture diagnostics (bring-up): how often the DMA capture produced
     * a partial frame, how often the rx_byte SM stalled on a full RX FIFO,
     * and how often the bounded TX put timed out. */
    uint32_t rx_partial_count;
    uint32_t rx_stall_count;
    uint32_t tx_timeout_count;
    /* Last rejected frame header words and received word count, to identify
     * byte-boundary drift (a shifted magic instead of a corrupted payload). */
    uint32_t last_bad_header0;
    uint32_t last_bad_header1;
    uint32_t last_bad_header2;
    uint32_t last_bad_header3;
    uint32_t last_bad_words;
    /* Capture path diagnostics: how often the service saw the DMA still
     * busy (frame incomplete) vs completed with a shifted magic. */
    uint32_t rx_busy_count;
    uint32_t rx_magic_fail_count;
    /* Partial words visible in the DMA buffer while busy (to identify the
     * byte-boundary drift pattern). */
    uint32_t rx_busy_word0;
    uint32_t rx_busy_word1;
    uint32_t rx_busy_word2;
    uint32_t rx_busy_word3;
    uint32_t rx_busy_moved; /* words moved so far while busy. */
    /* Magic alignment distribution of completed captures: 0 = frame header
     * at buffer start (aligned), 1..35 = header shifted (capture started
     * mid-frame), plus magic_fail_count for header-not-found. */
    uint32_t rx_magic_at_zero;
    uint32_t rx_magic_at_shift;
    uint32_t rx_ring_overrun_count;
    uint32_t rx_dma_produced_words;
    uint32_t rx_scan_produced_words;
    uint32_t rx_dma_write_index;
    uint32_t rx_dma_channel;
    uint32_t tx_edge_count;
    uint32_t rx_edge_count;
    uint64_t last_tx_edge_timestamp_ns;
    uint64_t last_tx_done_timestamp_ns;
    uint64_t last_rx_edge_timestamp_ns;
    uint64_t last_rx_extract_timestamp_ns;
    uint32_t program_persona;
    uint32_t program_switch_count;
    uint32_t program_switch_fail_count;
} tdma_pio_spi_phys_snapshot_t;

typedef struct {
    bool armed;
    uint32_t role;
    uint32_t baud_hz;
    /* Downlink TX leg (SPI master, drives CS + SCK + data toward next board). */
    uint32_t tx_sm;
    uint32_t tx_sck_pin;
    uint32_t tx_csn_pin;
    uint32_t tx_pin;
    /* Uplink RX leg (SPI slave, follows previous board's CS + SCK). */
    uint32_t rx_sm;
    uint32_t rx_sck_pin;
    uint32_t rx_csn_pin;
    uint32_t rx_pin;
    tdma_pio_spi_phys_snapshot_t snapshot;
    bool rx_capture_active;
    size_t rx_capture_max_words;
    uint32_t rx_capture_last_remaining;
    uint64_t rx_capture_last_change_us;
    volatile uint32_t clk_train_guard;
    tdma_pio_spi_clk_train_snapshot_t clk_train;
    uint64_t clk_train_return_deadline_ns;
    volatile uint32_t cal_loopback_guard;
    tdma_pio_spi_cal_loopback_snapshot_t cal_loopback;
    volatile bool cal_loopback_start_pending;
    volatile bool cal_loopback_stop_pending;
    uint32_t cal_loopback_sample_hz;
    uint32_t cal_loopback_sample_words;
    uint32_t cal_loopback_epoch;
    uint32_t cal_loopback_tx_sm;
    uint32_t cal_loopback_capture_sm;
    volatile uint32_t coded_guard;
    tdma_pio_spi_coded_snapshot_t coded;
    volatile uint32_t p3_guard;
    tdma_pio_spi_p3_snapshot_t p3;
    volatile uint32_t marker_guard;
    tdma_pio_spi_marker_snapshot_t marker;
    uint64_t marker_deadline_ns;
    volatile uint32_t data_train_guard;
    tdma_pio_spi_data_train_snapshot_t data_train;
    uint64_t data_train_deadline_ns;
} tdma_pio_spi_phys_t;

/* Called by the ring adapter start() once the active ring config is known.
 * Both legs (downlink TX master + uplink RX slave) are armed together. */
bool tdma_pio_spi_phys_arm(void *context,
                           const tdma_ring_runtime_config_t *config);
void tdma_pio_spi_phys_disarm(void *context);
/* Submit first-stage SPI CLK training on the TDMA owner/core1 path. A forward
 * node enters RX-CLK -> TX-CLK regeneration. The reference node starts an
 * autonomous CLK burst and return-edge overlap detector. */
bool tdma_pio_spi_phys_train_clock(void *context, uint32_t cycles);
void tdma_pio_spi_phys_train_clock_service(void *context, uint64_t now_ns);
bool tdma_pio_spi_phys_get_clk_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_clk_train_snapshot_t *snapshot);
bool tdma_pio_spi_phys_cal_loopback_start(tdma_pio_spi_phys_t *phys,
                                          uint32_t sample_hz,
                                          uint32_t sample_words,
                                          uint32_t epoch);
void tdma_pio_spi_phys_cal_loopback_stop(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_cal_loopback_service(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_get_cal_loopback_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_cal_loopback_snapshot_t *snapshot);
/* CLOCK_CODED is a raw-evidence transport owned by TDMA/core1.  It does not
 * know marker fields or correlation policy; Calibration supplies packed raw
 * samples and consumes the completed capture after the guarded snapshot says
 * both fixed DMA windows completed. */
bool tdma_pio_spi_phys_coded_start(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_coded_request_t *request);
void tdma_pio_spi_phys_coded_stop(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_coded_service(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_get_coded_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_coded_snapshot_t *snapshot);
bool tdma_pio_spi_phys_copy_coded_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count);
bool tdma_pio_spi_phys_marker_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_marker_request_t *request);
bool tdma_pio_spi_phys_marker_inject(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_marker_stop(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_marker_service(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_get_marker_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_marker_snapshot_t *snapshot);
bool tdma_pio_spi_phys_copy_marker_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count);
bool tdma_pio_spi_phys_data_train_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_data_train_request_t *request);
bool tdma_pio_spi_phys_data_train_inject(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_data_train_stop(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_data_train_service(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_get_data_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_data_train_snapshot_t *snapshot);
bool tdma_pio_spi_phys_copy_data_train_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count);
bool tdma_pio_spi_phys_p3_start(
    tdma_pio_spi_phys_t *phys, const tdma_pio_spi_p3_request_t *request);
void tdma_pio_spi_phys_p3_stop(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_p3_service(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_get_p3_snapshot(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_p3_snapshot_t *snapshot);
/* Core1 TDMA-owner-only persona switch. The caller must first stop both SMs
 * and DMA. Programs are removed/loaded as a set; no core0/SCPI caller may
 * write PIO instruction memory directly. */
bool tdma_pio_spi_phys_select_program_persona(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona);
bool tdma_pio_spi_phys_get_snapshot(const tdma_pio_spi_phys_t *phys,
                                    tdma_pio_spi_phys_snapshot_t *snapshot);

/* phys_tx: push one complete packet (header + TdmaTransportFrame) into the TX
 * leg FIFO. The downlink SM shifts it out on the TX pin toward the next board
 * while driving SCK. */
bool tdma_pio_spi_phys_tx(void *context,
                          const uint8_t *packet,
                          size_t packet_size,
                          uint64_t *tx_timestamp_ns);
/* phys_rx: pull one complete packet received on the uplink RX leg from the
 * previous board. Non-blocking: it arms the RX DMA on the first call and
 * returns the frame only once the fixed-length capture has arrived. */
bool tdma_pio_spi_phys_rx(void *context,
                          uint8_t *packet,
                          size_t packet_capacity,
                          size_t *packet_size,
                          uint64_t *rx_timestamp_ns);

#endif
