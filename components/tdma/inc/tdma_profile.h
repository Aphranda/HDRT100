#ifndef TDMA_PROFILE_H
#define TDMA_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TDMA_RING_PROFILE_VERSION 1u
#define TDMA_FOUNDATION_PROFILE_VERSION 1u
#define TDMA_FOUNDATION_PROFILE_TABLE_VERSION 1u
#define TDMA_RING_NODE_MAX 8u
#define TDMA_PROFILE_DEFAULT_ACTIVE_NODE_COUNT 2u
#define TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN 0x00000001u
#define TDMA_TRAFFIC_CLASS_COUNT 5u
#define TDMA_RESOURCE_ID_UNUSED UINT32_MAX
#define TDMA_PROFILE_DEFAULT_TX_DMA_CHANNEL_ID 5u
#define TDMA_PROFILE_DEFAULT_RX_DMA_CHANNEL_ID 4u
#define TDMA_FOUNDATION_PROFILE_TABLE_COUNT 1u
#define TDMA_FOUNDATION_PROFILE_WIRE_WORDS 71u
#define TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE \
    ((2u + TDMA_FOUNDATION_PROFILE_WIRE_WORDS) * sizeof(uint32_t))

#define TDMA_PAYLOAD_BIT(payload_class) (1u << (payload_class))
#define TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE 1u
#define TDMA_PAYLOAD_CLASS_REFMEM_DELTA 2u
#define TDMA_PAYLOAD_CLASS_REFMEM_ACK_FENCE 3u
#define TDMA_PAYLOAD_CLASS_IDLE_BEACON 4u
#define TDMA_PAYLOAD_CLASS_OTA_BULK 5u
#define TDMA_PAYLOAD_CLASS_CONFIG_CONTROL 6u
#define TDMA_PAYLOAD_CLASS_LOG_STREAM 7u
#define TDMA_PAYLOAD_CLASS_STORAGE_BULK 8u
#define TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE 9u
#define TDMA_PAYLOAD_FOUNDATION_REQUIRED_MASK \
    (TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE) | \
     TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_REFMEM_DELTA) | \
     TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_REFMEM_ACK_FENCE) | \
     TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_IDLE_BEACON))
#define TDMA_PAYLOAD_FOUNDATION_DEFAULT_MASK \
    (TDMA_PAYLOAD_FOUNDATION_REQUIRED_MASK | \
     TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_OTA_BULK) | \
     TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_CONFIG_CONTROL) | \
     TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_LOG_STREAM) | \
     TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_STORAGE_BULK) | \
     TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE))

#define TDMA_TRAFFIC_FLAG_TIME_AWARE_GATE 0x00000001u
#define TDMA_TRAFFIC_FLAG_STRICT_RESERVED 0x00000002u
#define TDMA_TRAFFIC_FLAG_RELIABLE 0x00000004u
#define TDMA_TRAFFIC_FLAG_SHAPED 0x00000008u
#define TDMA_TRAFFIC_FLAG_PREEMPTIBLE 0x00000010u

typedef enum {
    TDMA_ADAPTER_NONE = 0u,
    TDMA_ADAPTER_PIO_SPI = 1u,
    TDMA_ADAPTER_BISS_C = 2u,
    TDMA_ADAPTER_UART = 3u,
    TDMA_ADAPTER_RS485 = 4u,
} tdma_adapter_type_t;

typedef enum {
    TDMA_TRAFFIC_VDC_REALTIME = 0u,
    TDMA_TRAFFIC_REFMEM_REALTIME = 1u,
    TDMA_TRAFFIC_CONFIG_CONTROL = 2u,
    TDMA_TRAFFIC_RELIABLE_BULK = 3u,
    TDMA_TRAFFIC_OTA_BULK = TDMA_TRAFFIC_RELIABLE_BULK,
    TDMA_TRAFFIC_LOG_BEST_EFFORT = 4u,
} tdma_traffic_class_id_t;

typedef enum {
    TDMA_OVERFLOW_FAULT = 0u,
    TDMA_OVERFLOW_BACKPRESSURE = 1u,
    TDMA_OVERFLOW_DROP_OLDEST = 2u,
    TDMA_OVERFLOW_DROP_NEWEST = 3u,
} tdma_overflow_policy_t;

typedef enum {
    TDMA_PROFILE_OK = 0u,
    TDMA_PROFILE_BAD_ARGUMENT = 1u,
    TDMA_PROFILE_BAD_VERSION = 2u,
    TDMA_PROFILE_BAD_TOPOLOGY = 3u,
    TDMA_PROFILE_DIRECTION_CONFLICT = 4u,
    TDMA_PROFILE_ADAPTER_MISSING = 5u,
    TDMA_PROFILE_RESOURCE_CONFLICT = 6u,
    TDMA_PROFILE_CAPACITY_INVALID = 7u,
    TDMA_PROFILE_PAYLOAD_MISSING = 8u,
    TDMA_PROFILE_CRC_MISMATCH = 9u,
} tdma_profile_result_t;

typedef struct {
    uint32_t version;
    uint32_t flags;
    uint32_t node_count;
    uint32_t local_index;
    uint32_t reference_index;
    uint32_t up_group_id;
    uint32_t down_group_id;
    uint32_t upstream_slot_id;
    uint32_t downstream_slot_id;
    uint32_t feedback_slot_id;
    uint32_t profile_crc32;
} tdma_ring_profile_t;

typedef struct {
    uint32_t class_id;
    uint32_t payload_mask;
    uint32_t reserved_bytes_per_cycle;
    uint32_t max_frames_per_cycle;
    uint32_t queue_depth;
    uint32_t deadline_ns;
    uint32_t flags;
    uint32_t overflow_policy;
} tdma_traffic_class_profile_t;

typedef struct {
    uint32_t adapter_type;
    uint32_t pio_block_id;
    uint32_t up_state_machine_id;
    uint32_t down_state_machine_id;
    uint32_t tx_dma_channel_id;
    uint32_t rx_dma_channel_id;
    uint32_t core1_service_id;
    uint32_t io_claim_mask;
    uint32_t ip_core_claim_mask;
    uint32_t short_frame_capacity;
    uint32_t long_frame_capacity;
    uint32_t payload_whitelist_mask;
    uint32_t cycle_period_ns;
    uint32_t cycle_capacity_bytes;
    uint32_t guard_band_bytes;
    uint32_t queue_memory_capacity_bytes;
    tdma_traffic_class_profile_t traffic[TDMA_TRAFFIC_CLASS_COUNT];
} tdma_resource_profile_t;

typedef struct {
    uint32_t version;
    uint32_t enabled;
    uint32_t owner_instance_id;
    tdma_ring_profile_t ring;
    tdma_resource_profile_t resource;
    uint32_t profile_crc32;
} tdma_foundation_profile_t;

uint32_t tdma_ring_profile_crc32(const tdma_ring_profile_t *profile);
bool tdma_ring_profile_default(tdma_ring_profile_t *profile,
                               uint32_t local_slot_id,
                               uint32_t reference_slot_id,
                               uint32_t node_count);
bool tdma_ring_profile_validate(const tdma_ring_profile_t *profile,
                                tdma_profile_result_t *result);

uint32_t tdma_foundation_profile_crc32(const tdma_foundation_profile_t *profile);
bool tdma_foundation_profile_default(tdma_foundation_profile_t *profile,
                                     uint32_t owner_instance_id,
                                     uint32_t local_slot_id,
                                     uint32_t reference_slot_id,
                                     uint32_t adapter_type);
bool tdma_foundation_profile_default_for_topology(
    tdma_foundation_profile_t *profile,
    uint32_t owner_instance_id,
    uint32_t local_slot_id,
    uint32_t reference_slot_id,
    uint32_t node_count,
    uint32_t adapter_type);
bool tdma_foundation_profile_validate(const tdma_foundation_profile_t *profile,
                                      tdma_profile_result_t *result);
bool tdma_foundation_profile_encode_table(const tdma_foundation_profile_t *profile,
                                          uint8_t *data,
                                          size_t size);
bool tdma_foundation_profile_decode_table(const uint8_t *data,
                                          size_t size,
                                          tdma_foundation_profile_t *profile,
                                          tdma_profile_result_t *result);

#endif
