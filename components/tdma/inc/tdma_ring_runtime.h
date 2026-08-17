#ifndef TDMA_RING_RUNTIME_H
#define TDMA_RING_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_profile.h"

#define TDMA_RING_RUNTIME_VERSION 2u
#define TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY 0x00000001u
#define TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED 0x00000002u

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
    uint32_t enabled;
    uint32_t node_count;
    uint32_t local_slot_id;
    uint32_t reference_slot_id;
    uint32_t up_group_id;
    uint32_t down_group_id;
    uint32_t flags;
    uint32_t ring_profile_crc32;
    uint32_t schedule_crc32;
    uint32_t feedback_timeout_ns;
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
    uint64_t reference_tx_timestamp_ns;
    uint64_t feedback_rx_timestamp_ns;
} tdma_ring_adapter_status_t;

typedef struct {
    bool (*start)(void *context, const tdma_ring_runtime_config_t *config);
    void (*stop)(void *context);
    bool (*service)(void *context,
                    uint64_t now_ns,
                    tdma_ring_adapter_status_t *status);
} tdma_ring_adapter_ops_t;

typedef struct {
    uint32_t version;
    uint32_t enabled;
    uint32_t config_seq;
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
    uint32_t feedback_timeout_ns;
    uint32_t adapter_started;
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
    uint64_t reference_tx_timestamp_ns;
    uint64_t feedback_rx_timestamp_ns;
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
    volatile uint32_t feedback_timeout_ns;
    volatile uint32_t service_seq;
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
    volatile uint64_t reference_tx_timestamp_ns;
    volatile uint64_t feedback_rx_timestamp_ns;
    const tdma_ring_adapter_ops_t *adapter_ops;
    void *adapter_context;
} tdma_ring_runtime_t;

bool tdma_ring_runtime_init(tdma_ring_runtime_t *runtime);
bool tdma_ring_runtime_validate_config(
    const tdma_ring_runtime_config_t *config,
    tdma_ring_runtime_reason_t *reason);
bool tdma_ring_runtime_configure(tdma_ring_runtime_t *runtime,
                                 const tdma_ring_runtime_config_t *config);
bool tdma_ring_runtime_bind_adapter(tdma_ring_runtime_t *runtime,
                                    const tdma_ring_adapter_ops_t *ops,
                                    void *context);
void tdma_ring_runtime_unbind_adapter(tdma_ring_runtime_t *runtime);
void tdma_ring_runtime_service(tdma_ring_runtime_t *runtime);
bool tdma_ring_runtime_get_snapshot(const tdma_ring_runtime_t *runtime,
                                    tdma_ring_runtime_snapshot_t *snapshot);

#endif
