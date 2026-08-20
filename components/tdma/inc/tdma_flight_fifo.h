#ifndef TDMA_FLIGHT_FIFO_H
#define TDMA_FLIGHT_FIFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_transport_frame.h"

#define TDMA_FLIGHT_FIFO_VERSION 2u
#define TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT 2u
#define TDMA_FLIGHT_RX_FRAME_SLOT_COUNT 4u
#define TDMA_FLIGHT_PAYLOAD_CAPACITY TDMA_TRANSPORT_SHORT_PAYLOAD_MAX

typedef enum {
    TDMA_FLIGHT_TX_OWNER_CORE0_INACTIVE = 0u,
    TDMA_FLIGHT_TX_OWNER_CORE0_FILL = 1u,
    TDMA_FLIGHT_TX_OWNER_CORE0_READY = 2u,
    TDMA_FLIGHT_TX_OWNER_CORE1_ACTIVE = 3u,
} tdma_flight_tx_owner_t;

typedef enum {
    TDMA_FLIGHT_RX_OWNER_FREE = 0u,
    TDMA_FLIGHT_RX_OWNER_CORE1_FILL = 1u,
    TDMA_FLIGHT_RX_OWNER_CORE0_PARSE = 2u,
} tdma_flight_rx_owner_t;

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    uint32_t segment_mask;
    uint32_t data_size;
    const uint8_t *data;
    uint32_t slot_index;
    bool reused_previous;
} tdma_flight_tx_view_t;

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    uint32_t segment_mask;
    uint32_t data_size;
    uint64_t timestamp_ns;
    uint32_t quality_flags;
    const uint8_t *data;
    uint32_t slot_index;
} tdma_flight_rx_view_t;

typedef struct {
    uint32_t version;
    uint32_t tx_publish_count;
    uint32_t tx_publish_reject_count;
    uint32_t tx_acquire_count;
    uint32_t tx_image_stale_count;
    uint32_t tx_reuse_count;
    uint32_t tx_release_count;
    uint32_t tx_ready_count;
    uint32_t tx_active_slot;
    uint32_t tx_active_generation;
    uint32_t rx_publish_count;
    uint32_t rx_mirror_drop_count;
    uint32_t rx_publish_drop_count;
    uint32_t rx_acquire_count;
    uint32_t rx_release_count;
    uint32_t rx_queued_count;
    uint32_t rx_parse_count;
} tdma_flight_fifo_snapshot_t;

typedef struct {
    volatile uint32_t owner;
    volatile uint32_t generation;
    volatile uint32_t sequence;
    volatile uint32_t segment_mask;
    volatile uint32_t data_size;
    uint8_t data[TDMA_FLIGHT_PAYLOAD_CAPACITY];
} tdma_flight_tx_slot_t;

typedef struct {
    volatile uint32_t owner;
    volatile uint32_t generation;
    volatile uint32_t sequence;
    volatile uint32_t segment_mask;
    volatile uint32_t data_size;
    volatile uint64_t timestamp_ns;
    volatile uint32_t quality_flags;
    uint8_t data[TDMA_FLIGHT_PAYLOAD_CAPACITY];
} tdma_flight_rx_slot_t;

typedef struct {
    volatile uint32_t slot_index;
    volatile uint32_t generation;
    volatile uint32_t sequence;
} tdma_flight_fifo_descriptor_t;

typedef struct {
    tdma_flight_tx_slot_t tx_slots[TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT];
    tdma_flight_rx_slot_t rx_slots[TDMA_FLIGHT_RX_FRAME_SLOT_COUNT];
    tdma_flight_fifo_descriptor_t tx_ring[TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT];
    tdma_flight_fifo_descriptor_t rx_ring[TDMA_FLIGHT_RX_FRAME_SLOT_COUNT];
    volatile uint32_t tx_head;
    volatile uint32_t tx_tail;
    volatile uint32_t rx_head;
    volatile uint32_t rx_tail;
    volatile uint32_t tx_active_slot;
    volatile uint32_t tx_active_generation;
    volatile uint32_t tx_publish_count;
    volatile uint32_t tx_publish_reject_count;
    volatile uint32_t tx_acquire_count;
    volatile uint32_t tx_image_stale_count;
    volatile uint32_t tx_reuse_count;
    volatile uint32_t tx_release_count;
    volatile uint32_t rx_publish_count;
    volatile uint32_t rx_mirror_drop_count;
    volatile uint32_t rx_publish_drop_count;
    volatile uint32_t rx_acquire_count;
    volatile uint32_t rx_release_count;
} tdma_flight_fifo_t;

bool tdma_flight_fifo_init(tdma_flight_fifo_t *fifo);
bool tdma_flight_fifo_core0_publish_tx(tdma_flight_fifo_t *fifo,
                                       const uint8_t *data,
                                       size_t data_size,
                                       uint32_t generation,
                                       uint32_t sequence,
                                       uint32_t segment_mask);
bool tdma_flight_fifo_core1_acquire_tx(tdma_flight_fifo_t *fifo,
                                       tdma_flight_tx_view_t *view);
void tdma_flight_fifo_core1_release_tx(tdma_flight_fifo_t *fifo);
bool tdma_flight_fifo_core1_publish_rx(tdma_flight_fifo_t *fifo,
                                       const uint8_t *data,
                                       size_t data_size,
                                       uint32_t generation,
                                       uint32_t sequence,
                                       uint32_t segment_mask,
                                       uint64_t timestamp_ns,
                                       uint32_t quality_flags);
bool tdma_flight_fifo_core0_acquire_rx(tdma_flight_fifo_t *fifo,
                                       tdma_flight_rx_view_t *view);
bool tdma_flight_fifo_core0_release_rx(tdma_flight_fifo_t *fifo,
                                       uint32_t slot_index);
bool tdma_flight_fifo_get_snapshot(const tdma_flight_fifo_t *fifo,
                                   tdma_flight_fifo_snapshot_t *snapshot);

#endif
