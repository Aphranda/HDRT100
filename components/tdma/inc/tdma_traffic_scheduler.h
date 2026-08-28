#ifndef TDMA_TRAFFIC_SCHEDULER_H
#define TDMA_TRAFFIC_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_profile.h"

#define TDMA_TRAFFIC_SCHEDULER_VERSION 3u
#define TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT 32u
#define TDMA_TRAFFIC_SCHEDULER_RUNTIME_SLOT_COUNT 8u
#define TDMA_TRAFFIC_SCHEDULER_FRAME_MAX 1024u
#define TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_SHORT 1u
#define TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG 2u

typedef enum {
    TDMA_TRAFFIC_SCHEDULER_OK = 0u,
    TDMA_TRAFFIC_SCHEDULER_BAD_ARGUMENT = 1u,
    TDMA_TRAFFIC_SCHEDULER_NOT_CONFIGURED = 2u,
    TDMA_TRAFFIC_SCHEDULER_CLASS_REJECTED = 3u,
    TDMA_TRAFFIC_SCHEDULER_BACKPRESSURE = 4u,
    TDMA_TRAFFIC_SCHEDULER_DROPPED_OLDEST = 5u,
    TDMA_TRAFFIC_SCHEDULER_DROPPED_NEWEST = 6u,
    TDMA_TRAFFIC_SCHEDULER_FAULT = 7u,
    TDMA_TRAFFIC_SCHEDULER_BUSY = 8u,
    TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED = 9u,
    TDMA_TRAFFIC_SCHEDULER_BUDGET_EXHAUSTED = 10u,
    TDMA_TRAFFIC_SCHEDULER_DEADLINE_MISSED = 11u,
} tdma_traffic_scheduler_result_t;

typedef enum {
    TDMA_TRAFFIC_COMPLETION_SENT = 0u,
    TDMA_TRAFFIC_COMPLETION_LATE = 1u,
    TDMA_TRAFFIC_COMPLETION_RETRY = 2u,
    TDMA_TRAFFIC_COMPLETION_DROP = 3u,
    TDMA_TRAFFIC_COMPLETION_WINDOW_MISSED = 4u,
    TDMA_TRAFFIC_COMPLETION_ADAPTER_ERROR = 5u,
} tdma_traffic_completion_t;

typedef struct {
    uint32_t intent_type;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t rx_pin;
    uint32_t csn_pin;
    uint32_t sck_pin;
    uint32_t tx_pin;
    uint32_t deadline_us;
    uint32_t frame_class;
    uint32_t payload_class;
    uint32_t window_epoch;
    uint32_t window_index;
    uint32_t scheduled_window_valid;
    uint32_t scheduled_window_class;
    uint32_t schedule_crc32;
    uint64_t scheduled_window_start_ns;
    uint64_t scheduled_window_end_ns;
    uint64_t scheduled_guard_start_ns;
    uint64_t scheduled_guard_end_ns;
    uint64_t enqueue_time_ns;
    uint32_t estimated_duration_ns;
    size_t frame_size;
    const uint8_t *frame;
} tdma_traffic_request_t;

typedef struct {
    uint32_t sequence;
    uint32_t traffic_class;
    uint32_t is_recovery;
    uint32_t recovery_node_id;
    uint32_t recovery_generation;
    uint32_t recovery_reason;
    uint32_t original_sequence;
    tdma_traffic_request_t request;
    uint8_t frame[TDMA_TRAFFIC_SCHEDULER_FRAME_MAX];
} tdma_traffic_dispatch_t;

typedef struct {
    uint32_t queued_count;
    uint32_t dispatched_count;
    uint32_t sent_count;
    uint32_t late_count;
    uint32_t deadline_miss_count;
    uint32_t budget_overrun_count;
    uint32_t backpressure_count;
    uint32_t retry_count;
    uint32_t drop_count;
    uint32_t canceled_count;
    uint32_t adapter_error_count;
    uint32_t queue_high_watermark;
    uint32_t current_depth;
    uint32_t cycle_bytes;
    uint32_t cycle_frames;
    uint32_t last_dispatched_sequence;
    uint32_t last_completed_sequence;
} tdma_traffic_class_quality_t;

typedef struct {
    uint32_t queued_count;
    uint32_t dispatched_count;
    uint32_t sent_count;
    uint32_t retry_count;
    uint32_t exhausted_count;
    uint32_t backpressure_count;
    uint32_t current_depth;
    uint32_t queue_high_watermark;
    uint32_t cycle_bytes;
    uint32_t cycle_frames;
    uint32_t last_original_sequence;
    uint32_t last_node_id;
    uint32_t last_generation;
    uint32_t last_reason;
} tdma_recovery_quality_t;

typedef struct {
    uint32_t version;
    uint32_t configured;
    uint32_t admission_open;
    uint32_t config_seq;
    uint32_t enqueue_seq;
    uint32_t dispatch_seq;
    uint32_t cycle_seq;
    uint32_t cycle_period_ns;
    uint32_t cycle_capacity_bytes;
    uint32_t guard_band_bytes;
    uint32_t usable_cycle_bytes;
    uint32_t short_frame_capacity;
    uint32_t long_frame_capacity;
    uint32_t cycle_bytes;
    uint32_t fault_latched;
    uint32_t last_result;
    uint32_t last_traffic_class;
    uint32_t recovery_reserved_bytes_per_cycle;
    uint32_t recovery_buffer_count;
    uint32_t recovery_max_frames_per_cycle;
    tdma_recovery_quality_t recovery;
    tdma_traffic_class_quality_t traffic[TDMA_TRAFFIC_CLASS_COUNT];
} tdma_traffic_scheduler_snapshot_t;

typedef struct {
    uint32_t sequence;
    uint32_t traffic_class;
    uint32_t intent_type;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t rx_pin;
    uint32_t csn_pin;
    uint32_t sck_pin;
    uint32_t tx_pin;
    uint32_t deadline_us;
    uint32_t frame_class;
    uint32_t payload_class;
    uint32_t window_epoch;
    uint32_t window_index;
    uint32_t scheduled_window_valid;
    uint32_t scheduled_window_class;
    uint32_t schedule_crc32;
    uint64_t scheduled_window_start_ns;
    uint64_t scheduled_window_end_ns;
    uint64_t scheduled_guard_start_ns;
    uint64_t scheduled_guard_end_ns;
    uint64_t enqueue_time_ns;
    uint32_t estimated_duration_ns;
    uint32_t frame_size;
    uint8_t frame[TDMA_TRAFFIC_SCHEDULER_FRAME_MAX];
} tdma_traffic_scheduler_slot_t;

typedef struct {
    uint32_t base;
    uint32_t depth;
    uint32_t read_index;
    uint32_t write_index;
    uint32_t count;
} tdma_traffic_scheduler_queue_t;

typedef enum {
    TDMA_RECOVERY_BUFFER_EMPTY = 0u,
    TDMA_RECOVERY_BUFFER_READY = 1u,
    TDMA_RECOVERY_BUFFER_IN_FLIGHT = 2u,
} tdma_recovery_buffer_state_t;

typedef struct {
    uint32_t state;
    uint32_t sequence;
    uint32_t traffic_class;
    uint32_t node_id;
    uint32_t generation;
    uint32_t reason;
    uint32_t original_sequence;
    uint32_t retry_attempt;
    tdma_traffic_scheduler_slot_t slot;
} tdma_recovery_buffer_t;

typedef struct {
    volatile uint32_t lock;
    uint32_t configured;
    uint32_t admission_open;
    uint32_t config_seq;
    uint32_t enqueue_seq;
    uint32_t dispatch_seq;
    uint32_t cycle_seq;
    uint64_t cycle_number;
    uint32_t cycle_period_ns;
    uint32_t cycle_capacity_bytes;
    uint32_t guard_band_bytes;
    uint32_t usable_cycle_bytes;
    uint32_t short_frame_capacity;
    uint32_t long_frame_capacity;
    uint32_t cycle_bytes;
    uint32_t fault_latched;
    uint32_t last_result;
    uint32_t last_traffic_class;
    uint32_t recovery_write_index;
    uint32_t recovery_read_index;
    uint32_t recovery_in_flight_index;
    uint32_t recovery_sequence;
    tdma_recovery_quality_t recovery_quality;
    tdma_recovery_buffer_t recovery[TDMA_RECOVERY_BUFFER_COUNT];
    tdma_traffic_class_profile_t profile[TDMA_TRAFFIC_CLASS_COUNT];
    uint64_t budget_reported_cycle[TDMA_TRAFFIC_CLASS_COUNT];
    tdma_traffic_scheduler_queue_t queue[TDMA_TRAFFIC_CLASS_COUNT];
    tdma_traffic_class_quality_t quality[TDMA_TRAFFIC_CLASS_COUNT];
    tdma_traffic_scheduler_slot_t *slot;
    uint32_t slot_capacity;
} tdma_traffic_scheduler_t;

bool tdma_traffic_scheduler_init(
    tdma_traffic_scheduler_t *scheduler,
    tdma_traffic_scheduler_slot_t *slot_storage,
    uint32_t slot_capacity);
bool tdma_traffic_scheduler_configure(
    tdma_traffic_scheduler_t *scheduler,
    const tdma_foundation_profile_t *profile);
bool tdma_traffic_scheduler_set_cycle_period(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t cycle_period_ns);
bool tdma_traffic_scheduler_cancel_pending(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t *canceled_count);
bool tdma_traffic_scheduler_suspend(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t *canceled_count);
bool tdma_traffic_scheduler_resume(tdma_traffic_scheduler_t *scheduler);
tdma_traffic_scheduler_result_t tdma_traffic_scheduler_enqueue(
    tdma_traffic_scheduler_t *scheduler,
    const tdma_traffic_request_t *request);
tdma_traffic_scheduler_result_t tdma_traffic_scheduler_enqueue_recovery(
    tdma_traffic_scheduler_t *scheduler,
    const tdma_traffic_request_t *request,
    uint32_t traffic_class,
    uint32_t node_id,
    uint32_t generation,
    uint32_t reason,
    uint32_t original_sequence);
tdma_traffic_scheduler_result_t tdma_traffic_scheduler_select(
    tdma_traffic_scheduler_t *scheduler,
    uint64_t now_ns,
    bool maintenance_gate_open,
    tdma_traffic_dispatch_t *dispatch);
bool tdma_traffic_scheduler_complete(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t traffic_class,
    tdma_traffic_completion_t completion);
bool tdma_traffic_scheduler_clear_fault(tdma_traffic_scheduler_t *scheduler);
bool tdma_traffic_scheduler_get_snapshot(
    tdma_traffic_scheduler_t *scheduler,
    tdma_traffic_scheduler_snapshot_t *snapshot);

#endif
