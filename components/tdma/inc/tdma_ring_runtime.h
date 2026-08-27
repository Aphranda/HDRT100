#ifndef TDMA_RING_RUNTIME_H
#define TDMA_RING_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_profile.h"

#define TDMA_RING_RUNTIME_VERSION 9u
#define TDMA_RING_CALIBRATION_LINK_MAX 8u
#define TDMA_RING_CALIBRATION_FLAG_ACCEPTED (1u << 0u)
#define TDMA_RING_CALIBRATION_FLAG_HARDWARE_LATCHED (1u << 1u)
#define TDMA_RING_CALIBRATION_FLAG_PROFILE_BOUND (1u << 2u)
#define TDMA_RING_CALIBRATION_FLAG_REPEAT_GATE (1u << 3u)
#define TDMA_RING_CALIBRATION_FLAG_FORWARD_RESIDENCE_VALID (1u << 4u)
#define TDMA_RING_CALIBRATION_FLAG_DIAGNOSTIC_ONLY (1u << 31u)
#define TDMA_RING_CALIBRATION_REQUIRED_FLAGS \
    (TDMA_RING_CALIBRATION_FLAG_ACCEPTED | \
     TDMA_RING_CALIBRATION_FLAG_HARDWARE_LATCHED | \
     TDMA_RING_CALIBRATION_FLAG_PROFILE_BOUND | \
     TDMA_RING_CALIBRATION_FLAG_REPEAT_GATE | \
     TDMA_RING_CALIBRATION_FLAG_FORWARD_RESIDENCE_VALID)
#define TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY 0x00000001u
#define TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED 0x00000002u

/* Correlated reference-TX/local-RX evidence published by the transport.
 * TDMA owns the wire correlation; clock policy remains in VDC. */
typedef struct {
    uint32_t valid;
    uint32_t node_count;
    uint32_t source_node;
    uint32_t reference_node;
    uint32_t correlated_sequence;
    uint32_t frame_crc32;
    uint32_t schedule_crc32;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t correlated_frame_evidence;
    uint64_t reference_tx_timestamp_ns;
    uint64_t local_rx_timestamp_ns;
} tdma_ring_clock_observation_t;

typedef enum {
    TDMA_RING_RUNTIME_REASON_NONE = 0u,
    TDMA_RING_RUNTIME_REASON_BAD_CONFIG = 1u,
    TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING = 2u,
    TDMA_RING_RUNTIME_REASON_DIRECTION_CONFLICT = 3u,
    TDMA_RING_RUNTIME_REASON_ADAPTER_MISSING = 4u,
    TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING = 5u,
    TDMA_RING_RUNTIME_REASON_PAYLOAD_STARVATION = 6u,
    TDMA_RING_RUNTIME_REASON_WINDOW_MISSED = 7u,
    TDMA_RING_RUNTIME_REASON_RESOURCE_CONFLICT = 8u,
} tdma_ring_runtime_reason_t;

typedef struct {
    uint32_t valid;
    uint32_t link_index;
    /* Frozen by Calibration step 1 (line-order matrix).  Node identifiers
     * are assigned only after that matrix closes; runtime code must follow
     * these directed endpoints instead of deriving wiring from Node values. */
    uint32_t marker_source_node;
    uint32_t marker_destination_node;
    uint32_t data_source_node;
    uint32_t data_destination_node;
    uint32_t evidence_flags;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t pio_persona;
    uint32_t clkdiv_q16;
    uint32_t clk_sys_hz;
    uint32_t instruction_period_ns;
    uint32_t bit_cycles;
    uint32_t marker_to_data_cycles;
    uint32_t forward_residence_cycles;
    uint32_t rx_arm_lead_cycles;
    uint32_t codeword_cycles;
    uint32_t guard_cycles;
    uint32_t link_budget_cycles;
    uint32_t loop_delay_cycles;
    int32_t marker_offset_sample_count;
    int32_t sck_offset_sample_count;
    int32_t data_offset_sample_count;
    uint32_t sample_period_ns;
    uint32_t link_base_delay_ns;
    uint32_t marker_phase_delay_cycles;
    uint32_t sck_phase_delay_cycles;
    uint32_t data_phase_delay_cycles;
} tdma_ring_calibration_link_t;

typedef struct {
    uint32_t enabled;
    uint32_t node_count;
    uint32_t evidence_flags;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    tdma_ring_calibration_link_t links[TDMA_RING_CALIBRATION_LINK_MAX];
} tdma_ring_calibration_stage_t;

typedef struct {
    uint32_t enabled;
    uint32_t node_count;
    uint32_t local_slot_id;
    uint32_t reference_slot_id;
    uint32_t up_group_id;
    uint32_t down_group_id;
    uint32_t flags;
    uint32_t ring_profile_crc32;
    uint32_t schedule_crc32;
    uint32_t operating_profile_crc32;
    uint32_t baud_hz;
    uint32_t cycle_period_ns;
    /* Measured P1/P2 full-ring return delay.  Zero means no calibrated
     * minimum is available yet; feedback_timeout_ns remains the hard upper
     * bound. */
    uint32_t loop_delay_ns;
    /* P1/P2 uncertainty budget used around loop_delay_ns. */
    uint32_t loop_delay_tolerance_ns;
    uint32_t feedback_timeout_ns;
    uint32_t tx_dma_channel_id;
    uint32_t rx_dma_channel_id;
} tdma_ring_runtime_config_t;

typedef struct {
    uint32_t up_configured;
    uint32_t down_configured;
    uint32_t up_running;
    uint32_t down_running;
    uint32_t up_tx_sequence;
    uint32_t down_rx_sequence;
    uint32_t up_tx_frame_crc32;
    uint32_t down_rx_frame_crc32;
    uint32_t schedule_crc32;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t idle_beacon_tx_count;
    uint32_t idle_beacon_rx_count;
    uint32_t last_error;
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
    /* TX identity selected by the adapter from its hardware-latched
     * sequence history for the current feedback RX frame.  These fields
     * deliberately differ from up_tx_* when one or more frames are in
     * flight around the ring. */
    uint32_t feedback_reference_sequence;
    uint32_t feedback_reference_frame_crc32;
    uint64_t reference_tx_timestamp_ns;
    uint64_t feedback_rx_timestamp_ns;
    tdma_ring_clock_observation_t clock_observation;
} tdma_ring_adapter_status_t;

typedef struct {
    bool (*start)(void *context, const tdma_ring_runtime_config_t *config);
    void (*stop)(void *context);
    bool (*train_clock)(void *context, uint32_t cycles);
    void (*train_clock_service)(void *context, uint64_t now_ns);
    bool (*service)(void *context,
                    uint64_t now_ns,
                    tdma_ring_adapter_status_t *status);
} tdma_ring_adapter_ops_t;

typedef struct {
    uint32_t version;
    uint32_t enabled;
    uint32_t config_seq;
    uint32_t applied_config_seq;
    uint32_t config_reject_count;
    uint32_t service_seq;
    uint32_t node_count;
    uint32_t local_slot_id;
    uint32_t reference_slot_id;
    uint32_t up_group_id;
    uint32_t down_group_id;
    uint32_t flags;
    uint32_t up_configured;
    uint32_t down_configured;
    uint32_t up_running;
    uint32_t down_running;
    uint32_t ring_seq;
    uint32_t last_reason;
    uint32_t simultaneous_feedback_loop_evidence;
    uint32_t ring_profile_crc32;
    uint32_t schedule_crc32;
    uint32_t operating_profile_crc32;
    uint32_t baud_hz;
    uint32_t cycle_period_ns;
    uint32_t loop_delay_ns;
    uint32_t loop_delay_tolerance_ns;
    uint32_t feedback_timeout_ns;
    uint32_t tx_dma_channel_id;
    uint32_t rx_dma_channel_id;
    uint32_t adapter_started;
    uint32_t data_enabled;
    uint32_t adapter_start_count;
    uint32_t adapter_stop_count;
    uint32_t adapter_service_count;
    uint32_t adapter_last_error;
    uint32_t up_tx_sequence;
    uint32_t down_rx_sequence;
    uint32_t up_tx_frame_crc32;
    uint32_t down_rx_frame_crc32;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t idle_beacon_tx_count;
    uint32_t idle_beacon_rx_count;
    uint32_t feedback_round_trip_ns;
    uint32_t adapter_tx_count;
    uint32_t adapter_rx_count;
    uint32_t adapter_rx_bad_count;
    uint32_t adapter_rx_transport_bad_count;
    uint32_t adapter_rx_schedule_bad_count;
    uint32_t adapter_rx_profile_bad_count;
    uint32_t adapter_last_bad_transport_result;
    uint32_t adapter_last_bad_sequence;
    uint32_t adapter_last_bad_schedule_crc32;
    uint32_t adapter_last_bad_profile_crc32;
    uint32_t adapter_last_bad_header_diff_count;
    uint32_t adapter_last_bad_header_first_diff_offset;
    uint32_t adapter_last_bad_header_expected_byte;
    uint32_t adapter_last_bad_header_observed_byte;
    uint32_t train_request_seq;
    uint32_t train_accepted_seq;
    uint32_t train_request_cycles;
    uint32_t train_start_count;
    uint32_t train_reject_count;
    uint32_t training_dirty;
    uint64_t reference_tx_timestamp_ns;
    uint64_t feedback_rx_timestamp_ns;
    tdma_ring_clock_observation_t clock_observation;
} tdma_ring_runtime_snapshot_t;

typedef struct {
    volatile uint32_t config_guard;
    volatile uint32_t result_guard;
    volatile uint32_t enabled;
    volatile uint32_t config_seq;
    volatile uint32_t config_reject_count;
    volatile uint32_t node_count;
    volatile uint32_t local_slot_id;
    volatile uint32_t reference_slot_id;
    volatile uint32_t up_group_id;
    volatile uint32_t down_group_id;
    volatile uint32_t flags;
    volatile uint32_t ring_profile_crc32;
    volatile uint32_t schedule_crc32;
    volatile uint32_t operating_profile_crc32;
    volatile uint32_t baud_hz;
    volatile uint32_t cycle_period_ns;
    volatile uint32_t loop_delay_ns;
    volatile uint32_t loop_delay_tolerance_ns;
    volatile uint32_t feedback_timeout_ns;
    volatile uint32_t tx_dma_channel_id;
    volatile uint32_t rx_dma_channel_id;
    volatile uint32_t service_seq;
    volatile uint32_t applied_config_seq;
    volatile uint32_t up_configured;
    volatile uint32_t down_configured;
    volatile uint32_t up_running;
    volatile uint32_t down_running;
    volatile uint32_t ring_seq;
    volatile uint32_t last_reason;
    volatile uint32_t simultaneous_feedback_loop_evidence;
    volatile uint32_t adapter_started;
    volatile uint32_t adapter_config_seq;
    volatile uint32_t adapter_start_count;
    volatile uint32_t adapter_stop_count;
    volatile uint32_t adapter_service_count;
    volatile uint32_t adapter_last_error;
    volatile uint32_t up_tx_sequence;
    volatile uint32_t down_rx_sequence;
    volatile uint32_t up_tx_frame_crc32;
    volatile uint32_t down_rx_frame_crc32;
    volatile uint32_t timestamp_resolution_ns;
    volatile uint32_t timestamp_flags;
    volatile uint32_t idle_beacon_tx_count;
    volatile uint32_t idle_beacon_rx_count;
    volatile uint32_t feedback_round_trip_ns;
    volatile uint32_t adapter_tx_count;
    volatile uint32_t adapter_rx_count;
    volatile uint32_t adapter_rx_bad_count;
    volatile uint32_t adapter_rx_transport_bad_count;
    volatile uint32_t adapter_rx_schedule_bad_count;
    volatile uint32_t adapter_rx_profile_bad_count;
    volatile uint32_t adapter_last_bad_transport_result;
    volatile uint32_t adapter_last_bad_sequence;
    volatile uint32_t adapter_last_bad_schedule_crc32;
    volatile uint32_t adapter_last_bad_profile_crc32;
    volatile uint32_t adapter_last_bad_header_diff_count;
    volatile uint32_t adapter_last_bad_header_first_diff_offset;
    volatile uint32_t adapter_last_bad_header_expected_byte;
    volatile uint32_t adapter_last_bad_header_observed_byte;
    volatile uint32_t data_enabled;
    volatile uint32_t train_command_seq;
    volatile uint32_t train_command_cycles;
    volatile uint32_t train_request_seq;
    volatile uint32_t train_accepted_seq;
    volatile uint32_t train_request_cycles;
    volatile uint32_t train_start_count;
    volatile uint32_t train_reject_count;
    volatile uint32_t training_dirty;
    volatile uint64_t reference_tx_timestamp_ns;
    volatile uint64_t feedback_rx_timestamp_ns;
    tdma_ring_clock_observation_t clock_observation;
    /* A correlated hardware-latched feedback sample remains valid between
     * frame arrivals for one configured feedback timeout.  This prevents the
     * evidence bit from flickering low on every core1 service tick while the
     * ring is otherwise healthy. */
    volatile uint32_t feedback_evidence_valid;
    volatile uint64_t feedback_evidence_service_ns;
    volatile uint32_t feedback_evidence_sequence;
    const tdma_ring_adapter_ops_t *adapter_ops;
    void *adapter_context;
} tdma_ring_runtime_t;

bool tdma_ring_runtime_init(tdma_ring_runtime_t *runtime);
bool tdma_ring_runtime_validate_config(
    const tdma_ring_runtime_config_t *config,
    tdma_ring_runtime_reason_t *reason);
bool tdma_ring_runtime_validate_calibration_stage(
    const tdma_ring_calibration_stage_t *stage,
    uint32_t expected_node_count,
    tdma_ring_runtime_reason_t *reason);
bool tdma_ring_runtime_validate_calibration_link_phase(
    const tdma_ring_calibration_link_t *link);
bool tdma_ring_runtime_configure(tdma_ring_runtime_t *runtime,
                                 const tdma_ring_runtime_config_t *config);
bool tdma_ring_runtime_bind_adapter(tdma_ring_runtime_t *runtime,
                                    const tdma_ring_adapter_ops_t *ops,
                                    void *context);
void tdma_ring_runtime_unbind_adapter(tdma_ring_runtime_t *runtime);
void tdma_ring_runtime_service(tdma_ring_runtime_t *runtime);
bool tdma_ring_runtime_get_snapshot(const tdma_ring_runtime_t *runtime,
                                    tdma_ring_runtime_snapshot_t *snapshot);
bool tdma_ring_runtime_set_data_enabled(tdma_ring_runtime_t *runtime,
                                        bool enabled);
bool tdma_ring_runtime_train_clock(tdma_ring_runtime_t *runtime,
                                   uint32_t cycles);

#endif
