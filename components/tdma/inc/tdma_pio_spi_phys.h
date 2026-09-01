#ifndef TDMA_PIO_SPI_PHYS_H
#define TDMA_PIO_SPI_PHYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_ring_runtime.h"
#include "tdma_state_machine_resources.h"
#include "tdma_transport_frame.h"

/* TDMA PIO SPI resident physical layer.
 *
 * Product cyclic traffic uses the actual three-line direction: CS/SCK travel
 * from Node n to Node n+1 while DATA travels from Node n+1 back to Node n.
 * The reference produces a bounded CS/SCK burst and injects DATA when the
 * returned CS/SCK reaches it. Followers regenerate CS/SCK forward and DATA
 * backward in PIO, with a one-byte elastic stage; core service is not in the
 * wire forwarding path. The legacy NORMAL persona remains available for
 * calibration diagnostics and host/store-forward tests.
 *
 * Product-board wiring:
 *   forward: Cn CS=26/SCK=25 -> Cn+1 CS=27/SCK=28
 *   reverse: Cn+1 DATA=29 -> Cn DATA=24
 * CS is a point-to-point frame-sync signal, not a bus chip select.
 *
 * This layer is byte/transport level only: it carries the 4-byte packet
 * header (magic + length) plus the TdmaTransportFrame body and never parses
 * VDC/RefMem inner frames.
 */

#define TDMA_PIO_SPI_PACKET_MAGIC0 0x54u
#define TDMA_PIO_SPI_PACKET_MAGIC1 0x44u
#define TDMA_PIO_SPI_PACKET_HEADER_SIZE 4u
#define TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES 11u
#define TDMA_PIO_SPI_RX_DMA_WORD_MAX \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX)
#define TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS \
    (TDMA_PIO_SPI_RX_DMA_WORD_MAX + \
     TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT + 4u)
#define TDMA_PIO_SPI_OVERLAY_GRACE_SERVICE_PASSES 1u
#define TDMA_PIO_SPI_RX_STABLE_US 1000u

typedef enum {
    TDMA_PIO_SPI_OVERLAY_ERROR_NONE = 0,
    TDMA_PIO_SPI_OVERLAY_ERROR_BAD_STATE = 1,
    TDMA_PIO_SPI_OVERLAY_ERROR_BUILD_FAILED = 2,
    TDMA_PIO_SPI_OVERLAY_ERROR_DMA_BUSY_TIMEOUT = 3,
    TDMA_PIO_SPI_OVERLAY_ERROR_DMA_START_INVALID = 4,
} tdma_pio_spi_overlay_error_t;

/* Continuous RX capture ring (EtherCAT-style): the DMA stays armed for the
 * entire session and wraps its write address in SRAM. The CPU only scans
 * completed words for the outer magic/length header, so there is no
 * per-frame abort, FIFO clear, or DMA reconfiguration gap. */
#define TDMA_PIO_SPI_RX_RING_WORDS 1024u
#define TDMA_PIO_SPI_RX_RING_LOG2 12u
/* Bounded NORMAL-persona diagnostic evidence copied to SD by TRN-03B. RX is
 * the newest raw stream sampled on RX SCK rising edges. TX is the newest
 * complete frame accepted by the local TX FIFO, including its packet header.
 * The buffer is larger than a maximum short packet so TX keeps its boundary. */
#define TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES 512u
#define TDMA_PIO_SPI_NORMAL_CAPTURE_VERSION 3u
#define TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS 8u
#define TDMA_PIO_SPI_FLIGHT_SCK_SAMPLES_PER_WORD 32u
#define TDMA_PIO_SPI_FLIGHT_SCK_SAMPLE_PERIOD_NS 4u
#define TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_TIMEOUT_US 100000u
#define TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_CHUNK_BYTES 4u
/* Runtime/staging store the complete calibrated SCK target phase. The PIO
 * patch subtracts the two instructions before the output edge when encoding
 * the delay field. After that edge, two instructions are still required to
 * return to the opposite-edge WAIT; this is a separate half-period re-arm
 * budget. DATA has its own fixed replay budget. */
#define TDMA_PIO_SPI_FLIGHT_SCK_REARM_CYCLES 2u
#define TDMA_PIO_SPI_FLIGHT_DATA_REARM_CYCLES 5u
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
#define TDMA_PIO_SPI_DATA_TRAIN_SNAPSHOT_VERSION 2u
#define TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES 32u
#define TDMA_PIO_SPI_DATA_TRAIN_MAX_DELAY_CYCLES 1000000u
#define TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS 3000000000ull
#define TDMA_PIO_SPI_CLK_TRAIN_SNAPSHOT_VERSION 1u
#define TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS 256u
/* P3 endpoint-reference capture runs directly at clk_sys.  Keeping this at
 * the same 4 ns sample grid as per-link P3 prevents a coarser reference from
 * being promoted into the active path table. */
#define TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_HZ 250000000u
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
    TDMA_PIO_SPI_PHYS_ERROR_PHASE_ADMISSION = 7u,
    TDMA_PIO_SPI_PHYS_ERROR_TAIL_CAPACITY = 8u,
    TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY = 9u,
    TDMA_PIO_SPI_PHYS_ERROR_PERSONA_RESOURCE = 10u,
    TDMA_PIO_SPI_PHYS_ERROR_PROGRAM_LOAD = 11u,
    TDMA_PIO_SPI_PHYS_ERROR_FLIGHT_CONFIG = 12u,
    TDMA_PIO_SPI_PHYS_ERROR_RX_ARM = 13u,
    TDMA_PIO_SPI_PHYS_ERROR_OVERLAY_PREPARE = 14u,
    TDMA_PIO_SPI_PHYS_ERROR_CLOCK_LATCH = 15u,
    TDMA_PIO_SPI_PHYS_ERROR_OWNER_ARGUMENT = 16u,
    TDMA_PIO_SPI_PHYS_ERROR_OWNER_FLIGHT_MAP = 17u,
    TDMA_PIO_SPI_PHYS_ERROR_OWNER_TOPOLOGY_PHASE = 18u,
    TDMA_PIO_SPI_PHYS_ERROR_OWNER_CALIBRATION_STAGE = 19u,
    TDMA_PIO_SPI_PHYS_ERROR_OWNER_CALIBRATION_LINK = 20u,
    TDMA_PIO_SPI_PHYS_ERROR_OWNER_FLIGHT_OFFSET = 21u,
} tdma_pio_spi_phys_error_t;

typedef enum {
    TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE = 0u,
    TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_REQUESTED = 1u,
    TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_PATCHED = 2u,
    TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED = 3u,
    TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY = 4u,
    TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED = 5u,
} tdma_pio_spi_ring_waveform_capture_state_t;

typedef enum {
    TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED = 0u,
    TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING = 1u,
    TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_READY = 2u,
} tdma_pio_spi_normal_capture_copy_result_t;

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
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN = 11u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER = 12u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER = 13u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN = 14u,
    TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE = 15u,
} tdma_pio_spi_program_persona_t;

#define TDMA_PIO_SPI_PROGRAM_PERSONA_MAX \
    TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE

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
    /* Independent SCK source released by an internal PIO IRQ. */
    TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE = 3u,
    /* Independent destination whose capture origin is the RX SCK edge. */
    TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION = 4u,
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

typedef enum {
    TDMA_PIO_SPI_CAL_TRANSITION_IDLE = 0u,
    TDMA_PIO_SPI_CAL_TRANSITION_START_UNLOAD,
    TDMA_PIO_SPI_CAL_TRANSITION_START_LOAD,
    TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_TX,
    TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_RESPONDER,
    TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_CAPTURE,
    TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_DMA,
    TDMA_PIO_SPI_CAL_TRANSITION_START_ARM,
    TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_FREEZE,
    TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_DECODE,
    TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_CLEANUP,
    TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_PUBLISH,
    TDMA_PIO_SPI_CAL_TRANSITION_STOP_FREEZE,
    TDMA_PIO_SPI_CAL_TRANSITION_STOP_CLEANUP,
    TDMA_PIO_SPI_CAL_TRANSITION_STOP_UNLOAD,
    TDMA_PIO_SPI_CAL_TRANSITION_STOP_LOAD,
} tdma_pio_spi_cal_transition_t;

#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_ORIGIN (1u << 1u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_MARKER \
    TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_ORIGIN
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_DATA (1u << 2u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_MARKER_DMA_COMPLETE (1u << 3u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_DATA_DMA_COMPLETE (1u << 4u)
#define TDMA_PIO_SPI_DATA_TRAIN_FLAG_SOURCE_IRQ (1u << 5u)

#define TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_PAUSE (1u << 0u)
#define TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT (1u << 1u)
#define TDMA_PIO_SPI_DATA_TRAIN_FAULT_ALL \
    (TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_PAUSE | \
     TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT)

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
    uint32_t diagnostic_fault_flags;
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
    uint32_t diagnostic_fault_flags;
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
    TDMA_PIO_SPI_P3_TRANSITION_IDLE = 0u,
    TDMA_PIO_SPI_P3_TRANSITION_START_UNLOAD,
    TDMA_PIO_SPI_P3_TRANSITION_START_LOAD,
    TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_TX,
    TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_CAPTURE,
    TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_DMA,
    TDMA_PIO_SPI_P3_TRANSITION_START_ARM,
    TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_FREEZE,
    TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_DECODE,
    TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_CLEANUP,
    TDMA_PIO_SPI_P3_TRANSITION_RESTORE_UNLOAD,
    TDMA_PIO_SPI_P3_TRANSITION_RESTORE_LOAD,
    TDMA_PIO_SPI_P3_TRANSITION_PUBLISH,
    TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE,
    TDMA_PIO_SPI_P3_TRANSITION_STOP_CLEANUP,
} tdma_pio_spi_p3_transition_t;

typedef enum {
    TDMA_PIO_SPI_P3_REJECT_NONE = 0u,
    TDMA_PIO_SPI_P3_REJECT_EDGE_MISSING = 1u,
    TDMA_PIO_SPI_P3_REJECT_RESOURCE = 2u,
    TDMA_PIO_SPI_P3_REJECT_DMA = 3u,
} tdma_pio_spi_p3_reject_t;

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
    uint32_t data_pulse_count;
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
    uint32_t clock_latch_resolution_ns;
    uint32_t clock_latch_count;
    uint32_t clock_latch_miss_count;
    uint32_t program_persona;
    uint32_t program_switch_count;
    uint32_t program_switch_fail_count;
    uint32_t program_lifecycle_state;
    uint32_t program_target_persona;
    uint32_t program_previous_persona;
    uint32_t program_transition_seq;
    uint32_t program_lifecycle_error;
    int32_t flight_marker_offset_sample_count;
    int32_t flight_sck_offset_sample_count;
    int32_t flight_data_offset_sample_count;
    uint32_t flight_marker_phase_delay_cycles;
    uint32_t flight_sck_phase_delay_cycles;
    uint32_t flight_data_phase_delay_cycles;
    /* Live PIO diagnostics for TRN-03B flight bring-up.  tx_sm/rx_sm keep
     * their role-dependent meanings from tdma_pio_spi_phys_t; the raw PC,
     * FIFO, IRQ and pin values make a stopped clock/data pipeline observable
     * without changing the wire path. */
    uint32_t pio_irq_flags;
    uint32_t pio_fdebug;
    uint32_t tx_sm_pc;
    uint32_t rx_sm_pc;
    uint32_t tx_sm_tx_fifo_level;
    uint32_t tx_sm_rx_fifo_level;
    uint32_t rx_sm_tx_fifo_level;
    uint32_t rx_sm_rx_fifo_level;
    uint32_t gpio_input_levels;
    uint32_t origin_done_irq_count;
    uint32_t origin_done_txstall_count;
    uint32_t origin_clock_timeout_count;
    uint32_t origin_data_timeout_count;
    uint32_t origin_recovery_count;
    uint32_t overlay_prepare_count;
    uint32_t overlay_prepare_fail_count;
    uint32_t overlay_replacement_byte_count;
    uint32_t overlay_alignment_byte_shift;
    uint32_t overlay_alignment_bit_shift;
    uint32_t overlay_physical_byte_count;
    /* Last overlay rearm outcome. DMA fields are captured at the decision
     * point so a script-build failure can be distinguished from a previous
     * script that has not drained through the PIO TX FIFO. */
    uint32_t overlay_last_error;
    uint32_t overlay_tx_dma_remaining;
    uint32_t overlay_tx_dma_busy;
    uint32_t overlay_tx_fifo_level_at_fail;
    uint32_t overlay_prepare_wait_us;
    uint32_t overlay_program_offset;
    uint32_t overlay_tx_dma_read_index;
    uint32_t overlay_tx_dma_ctrl;
    uint32_t overlay_sm_shiftctrl;
    uint32_t overlay_sm_execctrl;
    uint32_t overlay_sm_pc_at_fail;
    uint32_t overlay_pio_ctrl_at_fail;
    uint32_t overlay_pio_fstat_at_fail;
    uint32_t overlay_pio_fdebug_at_fail;
    uint32_t overlay_frame_boundary_count;
    uint32_t overlay_pass_recovery_count;
    uint32_t overlay_late_coalesce_count;
} tdma_pio_spi_phys_snapshot_t;

typedef struct {
    uint32_t version;
    uint32_t baud_hz;
    uint32_t bit_period_ns;
    uint32_t rx_byte_count;
    uint32_t tx_byte_count;
    uint32_t rx_produced_bytes;
    uint32_t tx_produced_bytes;
    uint32_t tx_complete_frame_count;
    uint32_t sck_sample_period_ns;
    uint32_t sck_sample_count;
    uint32_t sck_word_count;
    uint32_t sck_words[TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS];
} tdma_pio_spi_normal_capture_snapshot_t;

typedef struct {
    bool armed;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t node_count;
    uint32_t flight_tail_bytes;
    bool process_image_enabled;
    uint32_t flight_payload_size;
    uint32_t flight_physical_byte_count;
    uint32_t flight_alignment_byte_shift;
    uint32_t flight_alignment_bit_shift;
    bool flight_overlay_next_prepared;
    bool flight_overlay_pass_committed;
    bool flight_overlay_boundary_pending;
    uint32_t flight_overlay_grace_remaining;
    bool flight_overlay_pending;
    uint32_t flight_overlay_active_buffer;
    uint32_t flight_overlay_pending_buffer;
    uint32_t flight_overlay_pending_words;
    /* Flight-origin TX is submitted by core1 and completed by a later
     * service pass.  The wire/PIO duration never blocks the TDMA phase. */
    bool flight_tx_pending;
    bool flight_tx_completion_pending;
    uint64_t flight_tx_completion_timestamp_ns;
    uint32_t flight_tx_packet_size;
    uint32_t flight_tx_wire_bytes;
    uint64_t flight_tx_launch_timestamp_ns;
    uint64_t flight_tx_deadline_ns;
    int32_t flight_marker_offset_sample_count;
    int32_t flight_sck_offset_sample_count;
    int32_t flight_data_offset_sample_count;
    uint32_t flight_marker_phase_delay_cycles;
    uint32_t flight_sck_phase_delay_cycles;
    uint32_t flight_data_phase_delay_cycles;
    uint64_t flight_clock_latch_epoch_ns;
    uint32_t flight_clock_latch_resolution_ns;
    bool flight_clock_latch_armed;
    tdma_pio_spi_ring_waveform_capture_state_t
        flight_sck_waveform_capture_state;
    uint64_t flight_sck_waveform_capture_deadline_us;
    uint16_t flight_sck_waveform_saved_instructions[4];
    uint32_t flight_normal_capture_copy_stage;
    uint32_t flight_normal_capture_sck_cursor;
    uint32_t flight_normal_capture_restore_stage;
    uint32_t flight_normal_capture_rx_produced;
    uint32_t flight_normal_capture_rx_start;
    uint32_t flight_normal_capture_rx_count;
    uint32_t flight_normal_capture_rx_cursor;
    /* Physical output pins. CS/SCK are forward; DATA is reverse. */
    uint32_t tx_sm;
    uint32_t tx_sck_pin;
    uint32_t tx_csn_pin;
    uint32_t tx_pin;
    /* Physical input pins. CS/SCK are from the previous Node; DATA is from
     * the next Node. In flight persona rx_sm names the SM feeding RX DMA. */
    uint32_t rx_sm;
    uint32_t rx_sck_pin;
    uint32_t rx_csn_pin;
    uint32_t rx_pin;
    /* The directional contract is admitted before a flight persona is
     * loaded. Legacy maintenance fields remain until that persona migrates. */
    tdma_state_machine_resource_contract_t flight_resources;
    bool flight_resource_claimed;
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
    volatile uint32_t cal_loopback_transition;
    uint32_t cal_loopback_program_step;
    uint32_t cal_loopback_program_count;
    uint32_t cal_loopback_decode_word;
    uint32_t cal_loopback_decode_previous;
    uint32_t cal_loopback_decode_found;
    uint32_t cal_loopback_decode_sync_edges;
    bool cal_loopback_decode_have_previous;
    uint64_t cal_loopback_decode_times[4];
    uint32_t cal_loopback_sample_hz;
    uint32_t cal_loopback_sample_words;
    uint32_t cal_loopback_epoch;
    uint32_t cal_loopback_tx_sm;
    uint32_t cal_loopback_capture_sm;
    volatile uint32_t coded_guard;
    tdma_pio_spi_coded_snapshot_t coded;
    volatile uint32_t p3_guard;
    tdma_pio_spi_p3_snapshot_t p3;
    volatile uint32_t p3_transition;
    uint32_t p3_target_persona;
    uint32_t p3_program_step;
    uint32_t p3_program_count;
    uint32_t p3_publish_state;
    uint32_t p3_decode_word;
    uint32_t p3_decode_previous;
    uint32_t p3_decode_found;
    bool p3_decode_have_previous;
    bool p3_decode_have_clock_rise;
    bool p3_decode_have_clock_fall;
    bool p3_decode_have_data_rise;
    uint64_t p3_decode_times[4];
    uint64_t p3_decode_clock_rise;
    uint64_t p3_decode_clock_fall;
    uint64_t p3_decode_clock_high_sum;
    uint64_t p3_decode_clock_low_sum;
    uint32_t p3_decode_clock_high_count;
    uint32_t p3_decode_clock_low_count;
    uint64_t p3_decode_data_rise;
    uint64_t p3_decode_data_high_sum;
    uint32_t p3_decode_data_high_count;
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
void tdma_pio_spi_phys_publish_arm_error(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_phys_error_t error);
uint32_t tdma_pio_spi_phys_last_error(const void *context);
void tdma_pio_spi_phys_disarm(void *context);
bool tdma_pio_spi_phys_set_process_image_mode(
    tdma_pio_spi_phys_t *phys,
    bool enabled,
    uint32_t payload_size);
/* Freeze the transport payload length used by the resident flight persona.
 * The TDMA ProcessImage map is the single source of truth for this value;
 * the physical layer must use the same length as the adapter when calculating
 * its fixed CS/SCK burst.  A zero value restores the legacy maximum-short
 * fallback used by standalone topology diagnostics. */
bool tdma_pio_spi_phys_set_flight_payload_size(
    tdma_pio_spi_phys_t *phys,
    uint32_t payload_size);
bool tdma_pio_spi_phys_set_flight_offsets(
    tdma_pio_spi_phys_t *phys,
    int32_t marker_offset_sample_count,
    int32_t sck_offset_sample_count,
    int32_t data_offset_sample_count,
    uint32_t marker_phase_delay_cycles,
    uint32_t sck_phase_delay_cycles,
    uint32_t data_phase_delay_cycles);
bool tdma_pio_spi_phys_prepare_process_overlay(
    void *context,
    const uint8_t *incoming_packet,
    const uint8_t *processed_packet,
    size_t packet_size);
/* Core1-only frame-boundary service.  A failed raw-frame decode must remain
 * visible in RX evidence, but it must not strand the process follower at its
 * next blocking PULL.  This queues exactly one PASS script when IRQ3 proves
 * that a frame ended without a prepared successor. */
bool tdma_pio_spi_phys_service_process_overlay_boundary(void *context);
/* Poll completion of a previously submitted flight-origin burst.  This is
 * deliberately separate from the TX submit callback so core1 can account
 * the hardware launch and completion in distinct bounded passes. */
void tdma_pio_spi_phys_service_tx(void *context, uint64_t now_ns);
bool tdma_pio_spi_phys_take_tx_completion(void *context,
                                          uint64_t *tx_timestamp_ns);
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
/* SCK training uses the same bounded raw-sample engine as DATA training but
 * has distinct source/destination roles and physical SCK pin ownership. */
bool tdma_pio_spi_phys_sck_train_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_data_train_request_t *request);
bool tdma_pio_spi_phys_sck_train_inject(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_sck_train_stop(tdma_pio_spi_phys_t *phys);
void tdma_pio_spi_phys_sck_train_service(tdma_pio_spi_phys_t *phys);
bool tdma_pio_spi_phys_get_sck_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_data_train_snapshot_t *snapshot);
bool tdma_pio_spi_phys_copy_sck_train_capture(
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
/* Copy bounded wire evidence without stopping PIO or DMA. The historical
 * function name is retained for SCPI/tool compatibility; NORMAL and both
 * FLIGHT personas are accepted. RX bytes are the newest physical DATA
 * samples, while TX is the newest complete origin frame when available. */
tdma_pio_spi_normal_capture_copy_result_t
tdma_pio_spi_phys_copy_normal_capture(
    tdma_pio_spi_phys_t *phys,
    uint32_t *rx_bytes,
    size_t rx_capacity,
    uint32_t *tx_bytes,
    size_t tx_capacity,
    tdma_pio_spi_normal_capture_snapshot_t *snapshot);
/* Ring waveform evidence is a request-scoped PIO persona, not clock-latch
 * timestamp evidence. It waits for physical RX CS, captures RX SCK at the
 * clk_sys grid, then restores the resident clock latch. */
bool tdma_pio_spi_phys_begin_ring_waveform_capture(
    tdma_pio_spi_phys_t *phys);
tdma_pio_spi_ring_waveform_capture_state_t
tdma_pio_spi_phys_service_ring_waveform_capture(
    tdma_pio_spi_phys_t *phys);

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
/* Pop the oldest reference-node TX-CS -> returned-RX-CS duration latched by
 * the dedicated PIO counter.  False means no complete hardware edge pair is
 * available for the just-received frame. */
bool tdma_pio_spi_phys_feedback_round_trip(
    void *context,
    uint32_t *round_trip_ns,
    uint32_t *resolution_ns,
    uint32_t *flags);

#endif
