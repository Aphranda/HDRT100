#ifndef TDMA_SERVICE_H
#define TDMA_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_payload_registry.h"
#include "tdma_profile.h"
#include "tdma_ring_runtime.h"
#include "tdma_traffic_scheduler.h"

#define TDMA_SERVICE_SHORT_FRAME_MAX 292u
#define TDMA_SERVICE_LONG_FRAME_MAX 1024u
#define TDMA_SERVICE_FRAME_MAX TDMA_SERVICE_LONG_FRAME_MAX
#define TDMA_SERVICE_PAYLOAD_REGISTRY_COUNT TDMA_PAYLOAD_REGISTRY_COUNT
#define tdma_service_FRAME_MAX TDMA_SERVICE_FRAME_MAX
#define TDMA_SERVICE_TIMESTAMP_RESOLUTION_LIMIT_NS 100u
#define TDMA_SERVICE_RING_FLAG_SIMULTANEOUS_UP_DOWN TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN
#define TDMA_SERVICE_RING_ERROR_NONE TDMA_RING_RUNTIME_REASON_NONE
#define TDMA_SERVICE_RING_ERROR_BAD_CONFIG TDMA_RING_RUNTIME_REASON_BAD_CONFIG
#define TDMA_SERVICE_RING_ERROR_EVIDENCE_MISSING TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING

typedef enum {
    TDMA_SERVICE_WINDOW_CLASS_NONE = 0u,
    TDMA_SERVICE_WINDOW_CLASS_VDC_OBSERVATION = 1u,
    TDMA_SERVICE_WINDOW_CLASS_REFMEM_DATA = 2u,
    TDMA_SERVICE_WINDOW_CLASS_IDLE_BEACON = 3u,
} tdma_service_window_class_t;

typedef enum {
    TDMA_SERVICE_ROLE_DISABLED = 0u,
    TDMA_SERVICE_ROLE_MASTER = 1u,
    TDMA_SERVICE_ROLE_SLAVE = 2u,
} tdma_service_role_t;

typedef enum {
    TDMA_SERVICE_FRAME_CLASS_SHORT = TDMA_PAYLOAD_FRAME_CLASS_SHORT,
    TDMA_SERVICE_FRAME_CLASS_LONG = TDMA_PAYLOAD_FRAME_CLASS_LONG,
} tdma_service_frame_class_t;

typedef enum {
    TDMA_SERVICE_PAYLOAD_CLASS_NONE = 0u,
    TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE = TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
    TDMA_SERVICE_PAYLOAD_CLASS_REFMEM_DELTA = TDMA_PAYLOAD_CLASS_REFMEM_DELTA,
    TDMA_SERVICE_PAYLOAD_CLASS_REFMEM_ACK_FENCE = TDMA_PAYLOAD_CLASS_REFMEM_ACK_FENCE,
    TDMA_SERVICE_PAYLOAD_CLASS_IDLE_BEACON = TDMA_PAYLOAD_CLASS_IDLE_BEACON,
    TDMA_SERVICE_PAYLOAD_CLASS_OTA_BULK = TDMA_PAYLOAD_CLASS_OTA_BULK,
    TDMA_SERVICE_PAYLOAD_CLASS_CONFIG_CONTROL = TDMA_PAYLOAD_CLASS_CONFIG_CONTROL,
    TDMA_SERVICE_PAYLOAD_CLASS_LOG_STREAM = TDMA_PAYLOAD_CLASS_LOG_STREAM,
    TDMA_SERVICE_PAYLOAD_CLASS_OTA_CONFIG_LOG = TDMA_SERVICE_PAYLOAD_CLASS_OTA_BULK,
} tdma_service_payload_class_t;

typedef struct {
    uint32_t rx_pin;
    uint32_t csn_pin;
    uint32_t sck_pin;
    uint32_t tx_pin;
} tdma_service_pin_config_t;

typedef tdma_payload_binding_t tdma_service_payload_binding_t;

typedef enum {
    tdma_service_STATE_UNINIT = 0u,
    tdma_service_STATE_IDLE = 1u,
    tdma_service_STATE_ARMED = 2u,
    tdma_service_STATE_PENDING = 3u,
    tdma_service_STATE_DONE = 4u,
    tdma_service_STATE_ERROR = 5u,
} tdma_service_state_t;

typedef enum {
    tdma_service_INTENT_NONE = 0u,
    tdma_service_INTENT_TX_FRAME = 1u,
    tdma_service_INTENT_RX_WINDOW = 2u,
} tdma_service_intent_t;

typedef enum {
    tdma_service_RESULT_NONE = 0u,
    tdma_service_RESULT_ACCEPTED = 1u,
    tdma_service_RESULT_FRAME_READY = 2u,
    tdma_service_RESULT_TIMEOUT = 3u,
    tdma_service_RESULT_OVERRUN = 4u,
    tdma_service_RESULT_BAD_ARGUMENT = 5u,
    tdma_service_RESULT_BUSY = 6u,
    tdma_service_RESULT_WAITING_FOR_WINDOW = 7u,
    tdma_service_RESULT_WINDOW_MISSED = 8u,
} tdma_service_result_t;

typedef enum {
    tdma_service_EXEC_NONE = 0u,
    tdma_service_EXEC_TX_OK = 1u,
    tdma_service_EXEC_RX_OK = 2u,
    tdma_service_EXEC_TIMEOUT = 3u,
    tdma_service_EXEC_ERROR = 4u,
    tdma_service_EXEC_PENDING = 5u,
} tdma_service_exec_result_t;

typedef enum {
    tdma_service_TIMESTAMP_SOURCE_NONE = 0u,
    tdma_service_TIMESTAMP_SOURCE_SOFTWARE_US = 1u,
    tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK = 2u,
} tdma_service_timestamp_source_t;

#define tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY 0x00000001u
#define tdma_service_TIMESTAMP_FLAG_DPLL_ELIGIBLE   0x00000002u

typedef struct {
    tdma_service_exec_result_t result;
    uint32_t error;
    size_t frame_size;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
} tdma_service_exec_status_t;

typedef struct {
    bool (*transmit)(void *context,
                     const uint8_t *frame,
                     size_t frame_size,
                     tdma_service_role_t role,
                     uint32_t baud_hz,
                     const tdma_service_pin_config_t *pins,
                     uint32_t deadline_1e3ns,
                     tdma_service_exec_status_t *status);
    bool (*receive)(void *context,
                    uint8_t *frame,
                    size_t frame_capacity,
                    tdma_service_role_t role,
                    uint32_t baud_hz,
                    const tdma_service_pin_config_t *pins,
                    uint32_t deadline_1e3ns,
                    tdma_service_exec_status_t *status);
} tdma_service_ops_t;

typedef struct {
    uint32_t state;
    uint32_t owner_core;
    uint32_t armed;
    uint32_t service_count;
    uint32_t intent_seq;
    uint32_t completed_seq;
    uint32_t dropped_seq;
    uint32_t window_epoch;
    uint32_t window_index;
    uint32_t intent_type;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t rx_pin;
    uint32_t csn_pin;
    uint32_t sck_pin;
    uint32_t tx_pin;
    uint32_t deadline_1e3ns;
    uint32_t frame_size;
    uint32_t frame_class;
    uint32_t payload_class;
    uint32_t ready_count;
    uint32_t timeout_count;
    uint32_t overrun_count;
    uint32_t reject_count;
    uint32_t last_result;
    uint32_t last_error;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t scheduled_window_valid;
    uint32_t scheduled_window_class;
    uint32_t schedule_crc32;
    uint32_t scheduled_window_miss_count;
    uint32_t scheduled_window_wait_ns;
    uint32_t scheduled_window_late_ns;
    uint32_t scheduled_window_start_ns_lo;
    uint32_t scheduled_window_start_ns_hi;
    uint32_t scheduled_window_end_ns_lo;
    uint32_t scheduled_window_end_ns_hi;
    uint32_t scheduled_guard_start_ns_lo;
    uint32_t scheduled_guard_start_ns_hi;
    uint32_t scheduled_guard_end_ns_lo;
    uint32_t scheduled_guard_end_ns_hi;
    uint32_t submit_time_ns_lo;
    uint32_t submit_time_ns_hi;
    uint32_t core1_arm_time_ns_lo;
    uint32_t core1_arm_time_ns_hi;
    uint32_t core1_start_time_ns_lo;
    uint32_t core1_start_time_ns_hi;
    uint32_t core1_done_time_ns_lo;
    uint32_t core1_done_time_ns_hi;
    uint32_t core1_elapsed_ns;
    uint32_t ring_enabled;
    uint32_t ring_config_seq;
    uint32_t ring_config_reject_count;
    uint32_t ring_service_seq;
    uint32_t ring_node_count;
    uint32_t ring_local_slot_id;
    uint32_t ring_reference_slot_id;
    uint32_t ring_up_group_id;
    uint32_t ring_down_group_id;
    uint32_t ring_flags;
    uint32_t ring_up_configured;
    uint32_t ring_down_configured;
    uint32_t ring_up_running;
    uint32_t ring_down_running;
    uint32_t ring_seq;
    uint32_t ring_last_error;
    uint32_t simultaneous_feedback_loop_evidence;
    uint32_t ring_profile_crc32;
    uint32_t ring_schedule_crc32;
    uint32_t ring_feedback_timeout_ns;
    uint32_t ring_adapter_started;
    uint32_t ring_adapter_start_count;
    uint32_t ring_adapter_stop_count;
    uint32_t ring_adapter_service_count;
    uint32_t ring_adapter_last_error;
    uint32_t ring_up_tx_sequence;
    uint32_t ring_down_rx_sequence;
    uint32_t ring_up_tx_frame_crc32;
    uint32_t ring_down_rx_frame_crc32;
    uint32_t ring_timestamp_resolution_ns;
    uint32_t ring_timestamp_flags;
    uint32_t ring_idle_beacon_tx_count;
    uint32_t ring_idle_beacon_rx_count;
    uint32_t ring_feedback_round_trip_ns;
    uint32_t ring_reference_tx_timestamp_ns_lo;
    uint32_t ring_reference_tx_timestamp_ns_hi;
    uint32_t ring_feedback_rx_timestamp_ns_lo;
    uint32_t ring_feedback_rx_timestamp_ns_hi;
    uint32_t foundation_profile_crc32;
    uint32_t foundation_owner_instance_id;
    uint32_t adapter_type;
    uint32_t pio_block_id;
    uint32_t up_state_machine_id;
    uint32_t down_state_machine_id;
    uint32_t tx_dma_channel_id;
    uint32_t rx_dma_channel_id;
    uint32_t core1_service_id;
    uint32_t short_frame_capacity;
    uint32_t long_frame_capacity;
    uint32_t payload_whitelist_mask;
    uint32_t io_claim_mask;
    uint32_t ip_core_claim_mask;
    uint32_t payload_registry_config_seq;
    uint32_t payload_registry_registration_seq;
    uint32_t payload_registry_used_count;
    uint32_t payload_registry_admitted_count;
    uint32_t payload_registry_reject_count;
    uint32_t payload_registry_last_result;
    uint32_t payload_registry_last_payload_class;
    uint32_t traffic_scheduler_configured;
    uint32_t traffic_scheduler_enqueue_seq;
    uint32_t traffic_scheduler_dispatch_seq;
    uint32_t traffic_scheduler_queued_count;
    uint32_t traffic_scheduler_fault_latched;
    uint32_t traffic_scheduler_last_result;
    uint32_t traffic_scheduler_last_class;
    uint32_t traffic_scheduler_completed_seq[TDMA_TRAFFIC_CLASS_COUNT];
    uint32_t traffic_class_last_result[TDMA_TRAFFIC_CLASS_COUNT];
    uint32_t traffic_class_last_error[TDMA_TRAFFIC_CLASS_COUNT];
    uint32_t traffic_class_timestamp_source[TDMA_TRAFFIC_CLASS_COUNT];
    uint32_t traffic_class_timestamp_resolution_ns[TDMA_TRAFFIC_CLASS_COUNT];
    uint32_t traffic_class_timestamp_flags[TDMA_TRAFFIC_CLASS_COUNT];
    uint32_t traffic_class_result_frame_size[TDMA_TRAFFIC_CLASS_COUNT];
} tdma_service_snapshot_t;

typedef tdma_ring_runtime_config_t tdma_service_ring_runtime_config_t;

typedef struct {
    uint32_t window_epoch;
    uint32_t window_index;
    uint32_t deadline_1e3ns;
    tdma_service_role_t role;
    uint32_t baud_hz;
    tdma_service_pin_config_t pins;
    uint32_t frame_class;
    uint32_t payload_class;
    uint32_t scheduled_window_valid;
    uint32_t scheduled_window_class;
    uint32_t schedule_crc32;
    uint64_t scheduled_window_start_ns;
    uint64_t scheduled_window_end_ns;
    uint64_t scheduled_guard_start_ns;
    uint64_t scheduled_guard_end_ns;
    const uint8_t *frame;
    size_t frame_size;
} tdma_service_intent_config_t;

typedef struct {
    volatile uint32_t intent_guard;
    volatile uint32_t result_guard;

    /* Core0 writer: intent mailbox. */
    volatile uint32_t intent_seq;
    volatile uint32_t abort_seq;
    volatile uint32_t window_epoch;
    volatile uint32_t window_index;
    volatile uint32_t intent_type;
    volatile uint32_t role;
    volatile uint32_t baud_hz;
    volatile uint32_t rx_pin;
    volatile uint32_t csn_pin;
    volatile uint32_t sck_pin;
    volatile uint32_t tx_pin;
    volatile uint32_t deadline_1e3ns;
    volatile uint32_t frame_class;
    volatile uint32_t payload_class;
    volatile uint32_t scheduled_window_valid;
    volatile uint32_t scheduled_window_class;
    volatile uint32_t schedule_crc32;
    volatile uint64_t scheduled_window_start_ns;
    volatile uint64_t scheduled_window_end_ns;
    volatile uint64_t scheduled_guard_start_ns;
    volatile uint64_t scheduled_guard_end_ns;
    volatile uint32_t frame_size;
    volatile uint32_t reject_count;
    volatile uint32_t scheduler_submit_seq;
    volatile uint32_t active_traffic_class;
    volatile uint32_t active_scheduler_sequence;
    volatile uint32_t maintenance_gate_open;
    volatile uint64_t submit_time_ns;
    volatile uint32_t foundation_profile_crc32;
    volatile uint32_t foundation_owner_instance_id;
    volatile uint32_t adapter_type;
    volatile uint32_t pio_block_id;
    volatile uint32_t up_state_machine_id;
    volatile uint32_t down_state_machine_id;
    volatile uint32_t tx_dma_channel_id;
    volatile uint32_t rx_dma_channel_id;
    volatile uint32_t core1_service_id;
    volatile uint32_t short_frame_capacity;
    volatile uint32_t long_frame_capacity;
    volatile uint32_t payload_whitelist_mask;
    volatile uint32_t io_claim_mask;
    volatile uint32_t ip_core_claim_mask;
    uint8_t frame[tdma_service_FRAME_MAX];

    /* Core1 writer: realtime execution snapshot. */
    volatile uint32_t state;
    volatile uint32_t owner_core;
    volatile uint32_t armed;
    volatile uint32_t service_count;
    volatile uint32_t completed_seq;
    volatile uint32_t dropped_seq;
    volatile uint32_t ready_count;
    volatile uint32_t timeout_count;
    volatile uint32_t overrun_count;
    volatile uint32_t last_result;
    volatile uint32_t last_error;
    volatile uint32_t result_frame_size;
    volatile uint32_t timestamp_source;
    volatile uint32_t timestamp_resolution_ns;
    volatile uint32_t timestamp_flags;
    volatile uint32_t timing_intent_seq;
    volatile uint32_t scheduled_window_miss_count;
    volatile uint32_t scheduled_window_wait_ns;
    volatile uint32_t scheduled_window_late_ns;
    volatile uint64_t core1_arm_time_ns;
    volatile uint64_t core1_start_time_ns;
    volatile uint64_t core1_done_time_ns;
    volatile uint32_t core1_elapsed_ns;
    uint8_t result_frame[tdma_service_FRAME_MAX];
    volatile uint32_t traffic_class_last_result[TDMA_TRAFFIC_CLASS_COUNT];
    volatile uint32_t traffic_class_last_error[TDMA_TRAFFIC_CLASS_COUNT];
    volatile uint32_t traffic_class_timestamp_source[TDMA_TRAFFIC_CLASS_COUNT];
    volatile uint32_t
        traffic_class_timestamp_resolution_ns[TDMA_TRAFFIC_CLASS_COUNT];
    volatile uint32_t traffic_class_timestamp_flags[TDMA_TRAFFIC_CLASS_COUNT];
    volatile uint32_t traffic_class_result_frame_size[TDMA_TRAFFIC_CLASS_COUNT];
    uint8_t traffic_class_result_frame[TDMA_TRAFFIC_CLASS_COUNT]
                                      [tdma_service_FRAME_MAX];

    const tdma_service_ops_t *ops;
    void *ops_context;
    tdma_payload_registry_t payload_registry;
    tdma_ring_runtime_t ring_runtime;
    tdma_traffic_scheduler_t *traffic_scheduler;
} tdma_service_service_t;

bool tdma_service_init(tdma_service_service_t *service);
bool tdma_service_bind_ops(tdma_service_service_t *service,
                                   const tdma_service_ops_t *ops,
                                   void *ops_context);
bool tdma_service_bind_traffic_scheduler(
    tdma_service_service_t *service,
    tdma_traffic_scheduler_t *scheduler);
bool tdma_service_set_maintenance_gate(tdma_service_service_t *service,
                                       bool open);
bool tdma_service_register_payload(tdma_service_service_t *service,
                                   const tdma_service_payload_binding_t *binding);
bool tdma_service_configure_ring_runtime(
    tdma_service_service_t *service,
    const tdma_service_ring_runtime_config_t *config);
bool tdma_service_bind_ring_adapter(tdma_service_service_t *service,
                                    const tdma_ring_adapter_ops_t *ops,
                                    void *context);
bool tdma_service_configure_foundation_profile(
    tdma_service_service_t *service,
    const tdma_foundation_profile_t *profile,
    uint32_t schedule_crc32);
bool tdma_service_submit_tx(tdma_service_service_t *service,
                                    const tdma_service_intent_config_t *config);
bool tdma_service_submit_rx(tdma_service_service_t *service,
                                    const tdma_service_intent_config_t *config);
void tdma_service_abort(tdma_service_service_t *service);
void tdma_service_core1_service(tdma_service_service_t *service);
bool tdma_service_get_snapshot(const tdma_service_service_t *service,
                                       tdma_service_snapshot_t *snapshot);
bool tdma_service_get_result_frame(const tdma_service_service_t *service,
                                           uint8_t *frame,
                                           size_t frame_capacity,
                                           size_t *frame_size);
bool tdma_service_get_class_result_frame(
    const tdma_service_service_t *service,
    uint32_t traffic_class,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size);

#endif
