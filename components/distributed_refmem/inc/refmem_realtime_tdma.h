#ifndef REFMEM_REALTIME_TDMA_H
#define REFMEM_REALTIME_TDMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_spi_physical_adapter.h"
#include "refmem_tdma_payload.h"
#include "tdma_service.h"

#define REFMEM_REALTIME_TDMA_FRAME_MAX REFMEM_TDMA_PAYLOAD_FRAME_MAX

typedef enum {
    REFMEM_REALTIME_TDMA_STATE_UNINIT = tdma_service_STATE_UNINIT,
    REFMEM_REALTIME_TDMA_STATE_IDLE = tdma_service_STATE_IDLE,
    REFMEM_REALTIME_TDMA_STATE_ARMED = tdma_service_STATE_ARMED,
    REFMEM_REALTIME_TDMA_STATE_PENDING = tdma_service_STATE_PENDING,
    REFMEM_REALTIME_TDMA_STATE_DONE = tdma_service_STATE_DONE,
    REFMEM_REALTIME_TDMA_STATE_ERROR = tdma_service_STATE_ERROR,
} refmem_realtime_tdma_state_t;

typedef enum {
    REFMEM_REALTIME_TDMA_INTENT_NONE = tdma_service_INTENT_NONE,
    REFMEM_REALTIME_TDMA_INTENT_TX_FRAME = tdma_service_INTENT_TX_FRAME,
    REFMEM_REALTIME_TDMA_INTENT_RX_WINDOW = tdma_service_INTENT_RX_WINDOW,
} refmem_realtime_tdma_intent_t;

typedef enum {
    REFMEM_REALTIME_TDMA_RESULT_NONE = tdma_service_RESULT_NONE,
    REFMEM_REALTIME_TDMA_RESULT_ACCEPTED = tdma_service_RESULT_ACCEPTED,
    REFMEM_REALTIME_TDMA_RESULT_FRAME_READY = tdma_service_RESULT_FRAME_READY,
    REFMEM_REALTIME_TDMA_RESULT_TIMEOUT = tdma_service_RESULT_TIMEOUT,
    REFMEM_REALTIME_TDMA_RESULT_OVERRUN = tdma_service_RESULT_OVERRUN,
    REFMEM_REALTIME_TDMA_RESULT_BAD_ARGUMENT = tdma_service_RESULT_BAD_ARGUMENT,
    REFMEM_REALTIME_TDMA_RESULT_BUSY = tdma_service_RESULT_BUSY,
    REFMEM_REALTIME_TDMA_RESULT_WAITING_FOR_WINDOW =
        tdma_service_RESULT_WAITING_FOR_WINDOW,
    REFMEM_REALTIME_TDMA_RESULT_WINDOW_MISSED =
        tdma_service_RESULT_WINDOW_MISSED,
} refmem_realtime_tdma_result_t;

typedef tdma_service_exec_result_t refmem_realtime_tdma_exec_result_t;
#define REFMEM_REALTIME_TDMA_EXEC_NONE tdma_service_EXEC_NONE
#define REFMEM_REALTIME_TDMA_EXEC_TX_OK tdma_service_EXEC_TX_OK
#define REFMEM_REALTIME_TDMA_EXEC_RX_OK tdma_service_EXEC_RX_OK
#define REFMEM_REALTIME_TDMA_EXEC_TIMEOUT tdma_service_EXEC_TIMEOUT
#define REFMEM_REALTIME_TDMA_EXEC_ERROR tdma_service_EXEC_ERROR
#define REFMEM_REALTIME_TDMA_EXEC_PENDING tdma_service_EXEC_PENDING

typedef enum {
    REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_NONE =
        tdma_service_TIMESTAMP_SOURCE_NONE,
    REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_SOFTWARE_US =
        tdma_service_TIMESTAMP_SOURCE_SOFTWARE_US,
    REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_HARDWARE_TICK =
        tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK,
} refmem_realtime_tdma_timestamp_source_t;

#define REFMEM_REALTIME_TDMA_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY \
    tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY
#define REFMEM_REALTIME_TDMA_TIMESTAMP_FLAG_DPLL_ELIGIBLE \
    tdma_service_TIMESTAMP_FLAG_DPLL_ELIGIBLE

typedef tdma_service_exec_status_t refmem_realtime_tdma_exec_status_t;

typedef struct {
    bool (*transmit)(void *context,
                     const uint8_t *frame,
                     size_t frame_size,
                     refmem_spi_physical_role_t role,
                     uint32_t baud_hz,
                     const refmem_spi_physical_pin_config_t *pins,
                     uint32_t deadline_us,
                     refmem_realtime_tdma_exec_status_t *status);
    bool (*receive)(void *context,
                    uint8_t *frame,
                    size_t frame_capacity,
                    refmem_spi_physical_role_t role,
                    uint32_t baud_hz,
                    const refmem_spi_physical_pin_config_t *pins,
                    uint32_t deadline_us,
                    refmem_realtime_tdma_exec_status_t *status);
} refmem_realtime_tdma_ops_t;

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
    uint32_t deadline_us;
    uint32_t frame_size;
    uint32_t ready_count;
    uint32_t timeout_count;
    uint32_t overrun_count;
    uint32_t reject_count;
    uint32_t last_result;
    uint32_t last_error;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t vdc_window_plan_valid;
    uint32_t vdc_window_class;
    uint32_t vdc_schedule_crc32;
    uint32_t vdc_window_miss_count;
    uint32_t vdc_window_wait_ns;
    uint32_t vdc_window_late_ns;
    uint32_t vdc_window_start_ns_lo;
    uint32_t vdc_window_start_ns_hi;
    uint32_t vdc_window_end_ns_lo;
    uint32_t vdc_window_end_ns_hi;
    uint32_t vdc_guard_start_ns_lo;
    uint32_t vdc_guard_start_ns_hi;
    uint32_t vdc_guard_end_ns_lo;
    uint32_t vdc_guard_end_ns_hi;
    uint32_t submit_time_ns_lo;
    uint32_t submit_time_ns_hi;
    uint32_t core1_arm_time_ns_lo;
    uint32_t core1_arm_time_ns_hi;
    uint32_t core1_start_time_ns_lo;
    uint32_t core1_start_time_ns_hi;
    uint32_t core1_done_time_ns_lo;
    uint32_t core1_done_time_ns_hi;
    uint32_t core1_elapsed_ns;
    uint32_t foundation_profile_crc32;
    uint32_t foundation_owner_instance_id;
    uint32_t adapter_type;
    uint32_t payload_whitelist_mask;
    uint32_t ring_enabled;
    uint32_t ring_config_seq;
    uint32_t ring_config_reject_count;
    uint32_t ring_node_count;
    uint32_t ring_local_slot_id;
    uint32_t ring_reference_slot_id;
    uint32_t ring_up_group_id;
    uint32_t ring_down_group_id;
    uint32_t ring_profile_crc32;
    uint32_t ring_schedule_crc32;
    uint32_t ring_up_running;
    uint32_t ring_down_running;
    uint32_t ring_seq;
    uint32_t ring_last_error;
    uint32_t simultaneous_feedback_loop_evidence;
    uint32_t ring_feedback_timeout_ns;
    uint32_t ring_adapter_started;
    uint32_t ring_adapter_start_count;
    uint32_t ring_adapter_stop_count;
    uint32_t ring_adapter_service_count;
    uint32_t ring_adapter_last_error;
    uint32_t ring_adapter_tx_count;
    uint32_t ring_adapter_rx_count;
    uint32_t ring_adapter_rx_bad_count;
    uint32_t ring_adapter_rx_transport_bad_count;
    uint32_t ring_adapter_rx_schedule_bad_count;
    uint32_t ring_adapter_rx_profile_bad_count;
    uint32_t ring_adapter_last_bad_transport_result;
    uint32_t ring_adapter_last_bad_sequence;
    uint32_t ring_adapter_last_bad_schedule_crc32;
    uint32_t ring_adapter_last_bad_profile_crc32;
    uint32_t ring_adapter_last_bad_header_diff_count;
    uint32_t ring_adapter_last_bad_header_first_diff_offset;
    uint32_t ring_adapter_last_bad_header_expected_byte;
    uint32_t ring_adapter_last_bad_header_observed_byte;
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
} refmem_realtime_tdma_snapshot_t;

typedef struct {
    uint32_t window_epoch;
    uint32_t window_index;
    uint32_t deadline_us;
    refmem_spi_physical_role_t role;
    uint32_t baud_hz;
    refmem_spi_physical_pin_config_t pins;
    uint32_t vdc_window_plan_valid;
    uint32_t vdc_window_class;
    uint32_t vdc_schedule_crc32;
    uint64_t vdc_window_start_ns;
    uint64_t vdc_window_end_ns;
    uint64_t vdc_guard_start_ns;
    uint64_t vdc_guard_end_ns;
    const uint8_t *frame;
    size_t frame_size;
} refmem_realtime_tdma_intent_config_t;

typedef struct {
    tdma_service_service_t *scheduler;
    uint32_t last_submit_seq;
    const refmem_realtime_tdma_ops_t *ops;
    void *ops_context;
} refmem_realtime_tdma_service_t;

bool refmem_realtime_tdma_init(refmem_realtime_tdma_service_t *service);
bool refmem_realtime_tdma_init_shared(
    refmem_realtime_tdma_service_t *service,
    tdma_service_service_t *scheduler);
bool refmem_realtime_tdma_bind_ops(refmem_realtime_tdma_service_t *service,
                                   const refmem_realtime_tdma_ops_t *ops,
                                   void *ops_context);
bool refmem_realtime_tdma_configure_foundation_profile(
    refmem_realtime_tdma_service_t *service,
    const tdma_foundation_profile_t *profile,
    uint32_t schedule_crc32);
bool refmem_realtime_tdma_submit_tx(
    refmem_realtime_tdma_service_t *service,
    const refmem_realtime_tdma_intent_config_t *config);
bool refmem_realtime_tdma_submit_rx(
    refmem_realtime_tdma_service_t *service,
    const refmem_realtime_tdma_intent_config_t *config);
void refmem_realtime_tdma_abort(refmem_realtime_tdma_service_t *service);
void refmem_realtime_tdma_core1_service(refmem_realtime_tdma_service_t *service);
bool refmem_realtime_tdma_get_snapshot(
    const refmem_realtime_tdma_service_t *service,
    refmem_realtime_tdma_snapshot_t *snapshot);
bool refmem_realtime_tdma_get_result_frame(
    const refmem_realtime_tdma_service_t *service,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size);

#endif
