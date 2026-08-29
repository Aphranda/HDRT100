#ifndef TDMA_PIO_SPI_RING_ADAPTER_H
#define TDMA_PIO_SPI_RING_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_flight_fifo.h"
#include "tdma_flight_engine.h"
#include "tdma_receive_health.h"
#include "tdma_ring_runtime.h"
#include "tdma_transport_frame.h"

/* TDMA PIO SPI ring adapter (bring-up).
 *
 * This adapter implements tdma_ring_adapter_ops_t for the minimum-system PIO
 * SPI transport. It is transport-level only: it builds and parses
 * TdmaTransportFrame packets (IDLE_BEACON short frames during bring-up) and
 * publishes adapter lifecycle evidence (start/stop/service counts, UP/DOWN
 * running state, idle beacon counters, sequence/identity CRC and timestamp
 * metadata) into tdma_ring_adapter_status_t. It never fabricates
 * simultaneous_feedback_loop_evidence: the TdmaRingRuntime only sets that bit
 * when sequence/identity CRC/schedule CRC/timestamp correlation all hold.
 *
 * Physical layer contract (P0.5-3): the firmware binds optional phys_tx /
 * phys_rx callbacks once the PIO/SM/DMA bidirectional legs are available.
 * Without them the adapter reports no running legs and service() returns
 * false, so the ring runtime reports EVIDENCE_MISSING instead of pretending
 * the ring is up.
 */

#define TDMA_PIO_SPI_RING_ADAPTER_VERSION 10u

#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_DISABLED (1u << 0u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_NO_SEQUENCE (1u << 1u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_TIMESTAMP_INELIGIBLE (1u << 2u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_RECORD_INVALID (1u << 3u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_SEQUENCE_MISMATCH (1u << 4u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_IDENTITY_MISSING (1u << 5u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_TIMESTAMP_MISSING (1u << 6u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_TIMESTAMP_INELIGIBLE (1u << 0u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_RECORD_INVALID (1u << 1u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_SEQUENCE_MISMATCH (1u << 2u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_IDENTITY_MISSING (1u << 3u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_TIMESTAMP_MISSING (1u << 4u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_COMPACT_DECODE (1u << 5u)
#define TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH 8u
#define TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH 8u
#define TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH 8u

typedef enum {
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE = 0u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT = 1u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_MISSING = 2u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED = 3u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME = 4u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_QUEUE_FULL = 5u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT = 6u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_GATE_REJECT = 7u,
} tdma_pio_spi_ring_adapter_error_t;

/* Ring node role (derived from the active ring config):
 * - REFERENCE: local_slot == reference_slot. It originates IDLE_BEACON /
 *   process-image short frames and correlates its own TX with the feedback
 *   frame that returns around the ring (this is the only node that may
 *   produce simultaneous_feedback_loop_evidence=1). It also publishes a
 *   normal DPLL observation for that returned process-image frame, using the
 *   calibrated diagonal (complete-loop) path entry.
 * - FORWARD: any other node. It receives the frame from the previous board
 *   and re-emits it toward the next board, keeping origin/sequence/identity
 *   CRC unchanged and advancing hop/transport CRC. Its up/down running state
 *   proves the ring is serviced; each forward node also consumes the DPLL
 *   trailer and publishes its own local observation before forwarding. */
typedef enum {
    TDMA_PIO_SPI_RING_ROLE_REFERENCE = 0u,
    TDMA_PIO_SPI_RING_ROLE_FORWARD = 1u,
} tdma_pio_spi_ring_role_t;

typedef enum {
    /* Host fake physical layers and maintenance compatibility path. */
    TDMA_PIO_SPI_RING_FORWARDING_STORE_FORWARD = 0u,
    /* Product short-frame path: the PIO forwards while bytes are arriving. */
    TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT = 1u,
    /* Product process image: PIO overlays local bytes while forwarding. */
    TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE = 2u,
} tdma_pio_spi_ring_forwarding_mode_t;

/* Optional resident physical-layer control. When set, start() arms the
 * physical layer with the active ring config and stop() disarms it. */
typedef bool (*tdma_pio_spi_ring_phys_arm_fn)(
    void *context,
    const tdma_ring_runtime_config_t *config);
typedef void (*tdma_pio_spi_ring_phys_disarm_fn)(void *context);
typedef bool (*tdma_pio_spi_ring_phys_train_fn)(void *context,
                                                uint32_t cycles);
typedef void (*tdma_pio_spi_ring_phys_train_service_fn)(void *context,
                                                        uint64_t now_ns);
typedef bool (*tdma_pio_spi_ring_phys_overlay_fn)(
    void *context,
    const uint8_t *incoming_packet,
    const uint8_t *processed_packet,
    size_t packet_size);
typedef bool (*tdma_pio_spi_ring_phys_overlay_boundary_fn)(void *context);

/* phys_tx pushes one complete packet onto the wire. On success it may fill
 * *tx_timestamp_ns with the hardware latch timestamp (0 means no hardware
 * timestamp is available for this TX). */
typedef bool (*tdma_pio_spi_ring_tx_fn)(void *context,
                                        const uint8_t *packet,
                                        size_t packet_size,
                                        uint64_t *tx_timestamp_ns);

/* phys_rx pulls one complete packet received from the wire. It returns true
 * when a packet is available and fills packet/packet_size/rx_timestamp_ns. */
typedef bool (*tdma_pio_spi_ring_rx_fn)(void *context,
                                        uint8_t *packet,
                                        size_t packet_capacity,
                                        size_t *packet_size,
                                        uint64_t *rx_timestamp_ns);
typedef bool (*tdma_pio_spi_ring_feedback_fn)(void *context,
                                              uint32_t *round_trip_ns,
                                              uint32_t *resolution_ns,
                                              uint32_t *flags);
/* Optional readiness probe for the physical timestamp spine. The adapter
 * calls this only after phys_arm() succeeds and uses the returned metadata for
 * DPLL admission. A false result keeps timestamps diagnostic/invalid. */
typedef bool (*tdma_pio_spi_ring_phys_timestamp_ready_fn)(
    void *context,
    uint32_t *resolution_ns,
    uint32_t *flags);
/* A flight-origin TX is accepted at hardware launch and completed later.
 * The adapter consumes this one-shot completion token to attach the PIO
 * clock-latch timestamp to the corresponding reference evidence entry. */
typedef bool (*tdma_pio_spi_ring_phys_tx_complete_fn)(
    void *context,
    uint64_t *tx_timestamp_ns);

typedef struct {
    uint32_t version;
    uint32_t started;
    uint32_t service_count;
    uint32_t role;
    uint32_t forwarding_mode;
    uint32_t forward_count;
    uint32_t up_sequence;
    uint32_t down_rx_sequence;
    uint32_t up_tx_frame_crc32;
    uint32_t down_rx_frame_crc32;
    uint32_t idle_beacon_tx_count;
    uint32_t idle_beacon_rx_count;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_bad_count;
    uint32_t rx_transport_bad_count;
    uint32_t rx_schedule_bad_count;
    uint32_t rx_profile_bad_count;
    uint32_t last_bad_transport_result;
    uint32_t last_bad_sequence;
    uint32_t last_bad_schedule_crc32;
    uint32_t last_bad_profile_crc32;
    uint32_t last_bad_header_diff_count;
    uint32_t last_bad_header_first_diff_offset;
    uint32_t last_bad_header_expected_byte;
    uint32_t last_bad_header_observed_byte;
    uint32_t last_bad_packet_diff_count;
    uint32_t last_bad_packet_first_diff_offset;
    uint32_t last_bad_packet_expected_byte;
    uint32_t last_bad_packet_observed_byte;
    uint32_t last_bad_clock_evidence;
    uint32_t last_bad_expected_transport_crc32;
    uint32_t last_bad_observed_transport_crc32;
    uint32_t last_bad_recomputed_transport_crc32;
    uint32_t last_bad_expected_payload_crc32;
    uint32_t last_bad_observed_payload_crc32;
    uint32_t clock_evidence_enabled;
    uint32_t rx_drop_count;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t feedback_reference_sequence;
    uint32_t feedback_reference_frame_crc32;
    uint64_t reference_tx_timestamp_ns;
    uint64_t feedback_rx_timestamp_ns;
    uint64_t last_rx_service_ns;
    uint64_t last_service_ns;
    tdma_ring_clock_observation_t clock_observation;
    uint32_t clock_observation_count;
    uint32_t clock_observation_reject_count;
    uint32_t clock_observation_last_reject_reason;
    uint32_t clock_evidence_build_count;
    uint32_t clock_evidence_build_reject_count;
    uint32_t clock_evidence_build_last_reason;
    uint32_t clock_evidence_last_tx_encoded;
    uint32_t clock_evidence_last_rx_encoded;
    uint32_t pending_tx_evidence;
    uint32_t pending_tx_evidence_sequence;
    uint32_t last_error;
    uint32_t local_slot_id;
    uint32_t schedule_crc32;
    uint32_t ring_profile_crc32;
    uint32_t feedback_timeout_ns;
    uint32_t loop_delay_ns;
    uint32_t loop_delay_tolerance_ns;
    uint64_t rx_ready_timestamp_ns;
    uint32_t flight_map_configured;
    uint32_t flight_map_active;
    uint32_t flight_map_crc32;
    uint32_t flight_map_generation;
    uint32_t flight_map_apply_count;
    uint32_t flight_input_bytes;
    uint32_t flight_output_bytes;
    uint32_t flight_tx_stale_reuse_count;
    uint32_t flight_map_reject_count;
    uint32_t flight_length_reject_count;
    uint32_t flight_tx_unavailable_count;
    tdma_receive_health_snapshot_t receive_health;
} tdma_pio_spi_ring_adapter_snapshot_t;

typedef struct {
    uint32_t valid;
    uint32_t node_count;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t marker_next_node[TDMA_RING_CALIBRATION_LINK_MAX];
    uint32_t data_next_node[TDMA_RING_CALIBRATION_LINK_MAX];
} tdma_pio_spi_ring_topology_t;

typedef struct {
    tdma_ring_runtime_config_t config;
    bool configured;
    tdma_pio_spi_ring_tx_fn phys_tx;
    tdma_pio_spi_ring_rx_fn phys_rx;
    tdma_pio_spi_ring_feedback_fn phys_feedback;
    void *phys_context;
    tdma_pio_spi_ring_phys_arm_fn phys_arm;
    tdma_pio_spi_ring_phys_disarm_fn phys_disarm;
    tdma_pio_spi_ring_phys_timestamp_ready_fn phys_timestamp_ready;
    tdma_pio_spi_ring_phys_tx_complete_fn phys_tx_complete;
    tdma_pio_spi_ring_phys_train_fn phys_train;
    tdma_pio_spi_ring_phys_train_service_fn phys_train_service;
    tdma_pio_spi_ring_phys_overlay_fn phys_prepare_overlay;
    tdma_pio_spi_ring_phys_overlay_boundary_fn phys_service_overlay_boundary;
    void *phys_ctrl_context;
    tdma_flight_fifo_t *flight_fifo;
    tdma_flight_engine_t *flight_engine;
    tdma_receive_health_t receive_health;
    tdma_pio_spi_ring_topology_t topology;
    uint32_t topology_probe_mode;
    volatile uint32_t snapshot_guard;
    tdma_pio_spi_ring_role_t role;
    tdma_pio_spi_ring_forwarding_mode_t forwarding_mode;
    uint32_t started;
    uint32_t service_count;
    uint32_t forward_count;
    uint32_t up_sequence;
    uint32_t down_rx_sequence;
    uint32_t up_tx_frame_crc32;
    uint32_t down_rx_frame_crc32;
    uint32_t idle_beacon_tx_count;
    uint32_t idle_beacon_rx_count;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_bad_count;
    uint32_t rx_transport_bad_count;
    uint32_t rx_schedule_bad_count;
    uint32_t rx_profile_bad_count;
    uint32_t last_bad_transport_result;
    uint32_t last_bad_sequence;
    uint32_t last_bad_schedule_crc32;
    uint32_t last_bad_profile_crc32;
    uint32_t last_bad_header_diff_count;
    uint32_t last_bad_header_first_diff_offset;
    uint32_t last_bad_header_expected_byte;
    uint32_t last_bad_header_observed_byte;
    uint32_t last_bad_packet_diff_count;
    uint32_t last_bad_packet_first_diff_offset;
    uint32_t last_bad_packet_expected_byte;
    uint32_t last_bad_packet_observed_byte;
    uint32_t last_bad_clock_evidence;
    uint32_t last_bad_expected_transport_crc32;
    uint32_t last_bad_observed_transport_crc32;
    uint32_t last_bad_recomputed_transport_crc32;
    uint32_t last_bad_expected_payload_crc32;
    uint32_t last_bad_observed_payload_crc32;
    uint32_t clock_evidence_enabled;
    uint32_t rx_drop_count;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t feedback_timestamp_resolution_ns;
    uint32_t feedback_timestamp_flags;
    uint32_t feedback_reference_sequence;
    uint32_t feedback_reference_frame_crc32;
    uint64_t reference_tx_timestamp_ns;
    uint64_t feedback_rx_timestamp_ns;
    uint64_t last_rx_service_ns;
    uint64_t last_service_ns;
    tdma_ring_clock_observation_t clock_observation;
    uint32_t clock_observation_count;
    uint32_t clock_observation_reject_count;
    uint32_t clock_observation_last_reject_reason;
    uint32_t clock_evidence_build_count;
    uint32_t clock_evidence_build_reject_count;
    uint32_t clock_evidence_build_last_reason;
    uint32_t clock_evidence_last_tx_encoded;
    uint32_t clock_evidence_last_rx_encoded;
    uint32_t last_error;
    uint8_t last_rx_packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t last_rx_packet_size;
    bool last_rx_gate_accepted;
    uint32_t last_rx_new_segment_mask;
    uint64_t next_tx_deadline_ns;
    uint64_t rx_ready_timestamp_ns;
    uint32_t pending_tx_evidence_sequence;
    uint32_t pending_tx_evidence_identity_crc32;
    bool pending_tx_evidence;
    struct {
        uint32_t sequence;
        uint32_t identity_crc32;
        uint64_t timestamp_ns;
        uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t packet_size;
        bool clock_evidence;
        bool valid;
    } reference_tx_evidence[TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH];
    struct {
        uint32_t sequence;
        uint32_t identity_crc32;
        uint64_t timestamp_ns;
        bool valid;
    } local_rx_evidence[TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH];
    struct {
        uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t packet_size;
        uint64_t timestamp_ns;
        bool valid;
    } rx_queue[TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH];
    uint32_t rx_queue_head;
    uint32_t rx_queue_count;
} tdma_pio_spi_ring_adapter_t;

bool tdma_pio_spi_ring_adapter_init(tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_set_calibration_topology(
    tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_ring_calibration_stage_t *stage);
void tdma_pio_spi_ring_adapter_clear_calibration_topology(
    tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_set_topology_probe_mode(
    tdma_pio_spi_ring_adapter_t *adapter, bool enabled);
void tdma_pio_spi_ring_adapter_set_phys(tdma_pio_spi_ring_adapter_t *adapter,
                                        tdma_pio_spi_ring_tx_fn tx,
                                        tdma_pio_spi_ring_rx_fn rx,
                                        void *phys_context);
void tdma_pio_spi_ring_adapter_set_phys_feedback(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_feedback_fn feedback);
void tdma_pio_spi_ring_adapter_set_phys_ctrl(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_arm_fn arm,
    tdma_pio_spi_ring_phys_disarm_fn disarm,
    tdma_pio_spi_ring_phys_train_fn train,
    tdma_pio_spi_ring_phys_train_service_fn train_service,
    void *phys_ctrl_context);
void tdma_pio_spi_ring_adapter_set_phys_timestamp_ready(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_timestamp_ready_fn timestamp_ready);
void tdma_pio_spi_ring_adapter_set_phys_tx_complete(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_tx_complete_fn tx_complete);
void tdma_pio_spi_ring_adapter_set_phys_overlay(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_overlay_fn prepare_overlay,
    tdma_pio_spi_ring_phys_overlay_boundary_fn service_overlay_boundary);
void tdma_pio_spi_ring_adapter_set_timestamp_metadata(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t resolution_ns,
    uint32_t flags);
void tdma_pio_spi_ring_adapter_set_flight_fifo(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_flight_fifo_t *fifo);
void tdma_pio_spi_ring_adapter_set_flight_engine(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_flight_engine_t *engine);
bool tdma_pio_spi_ring_adapter_set_forwarding_mode(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_forwarding_mode_t mode);
bool tdma_pio_spi_ring_adapter_set_clock_evidence_enabled(
    tdma_pio_spi_ring_adapter_t *adapter, bool enabled);
bool tdma_pio_spi_ring_adapter_inject_rx(tdma_pio_spi_ring_adapter_t *adapter,
                                         const uint8_t *packet,
                                         size_t packet_size,
                                         uint64_t rx_timestamp_ns);
const tdma_ring_adapter_ops_t *tdma_pio_spi_ring_adapter_ops(void);
bool tdma_pio_spi_ring_adapter_get_snapshot(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_adapter_snapshot_t *snapshot);
bool tdma_pio_spi_ring_adapter_read_accepted_image(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint64_t now_ns,
    tdma_receive_image_t *image);

#endif
