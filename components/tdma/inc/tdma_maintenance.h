#ifndef TDMA_MAINTENANCE_H
#define TDMA_MAINTENANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_long_frame.h"

/*
 * TDMA maintenance service.
 *
 * Owns a bounded, low-priority maintenance window that drains the independent
 * tdma_long_frame TX FIFO and publishes received frames to its RX FIFO.  It
 * never reads or writes tdma_flight_fifo; the T2 fast path can keep running
 * while a window is open.
 *
 * The ring integration supplies tdma_maintenance_transport_ops_t.  The service
 * itself is transport-agnostic and host-testable.
 */

#define TDMA_MAINTENANCE_VERSION 1u
#define TDMA_MAINTENANCE_MAX_WINDOW_BYTES TDMA_LONG_FRAME_MAX
#define TDMA_MAINTENANCE_DEFAULT_PERIOD_CYCLES 64u
#define TDMA_MAINTENANCE_DEFAULT_WINDOW_CYCLES 8u

typedef struct {
    bool (*send)(void *context,
                 const uint8_t *frame,
                 size_t frame_size,
                 uint32_t generation,
                 uint32_t sequence,
                 uint32_t segment_mask);
    bool (*poll)(void *context,
                 uint8_t *frame,
                 size_t frame_capacity,
                 size_t *frame_size,
                 uint32_t *generation,
                 uint32_t *sequence,
                 uint32_t *segment_mask);
} tdma_maintenance_transport_ops_t;

typedef struct {
    uint32_t period_cycles;
    uint32_t window_cycles;
    uint32_t max_window_bytes;
} tdma_maintenance_config_t;

typedef struct {
    uint32_t version;
    uint32_t cycle_count;
    uint32_t window_open_count;
    uint32_t tx_sent_count;
    uint32_t tx_blocked_count;
    uint32_t rx_published_count;
    uint32_t rx_reject_count;
    uint32_t last_error;
    bool window_open;
} tdma_maintenance_snapshot_t;

typedef struct {
    tdma_long_frame_fifo_t *tx;
    tdma_long_frame_fifo_t *rx;
    tdma_maintenance_transport_ops_t ops;
    void *ops_context;
    tdma_maintenance_config_t config;
    uint32_t cycle_count;
    uint32_t window_open_count;
    uint32_t tx_sent_count;
    uint32_t tx_blocked_count;
    uint32_t rx_published_count;
    uint32_t rx_reject_count;
    uint32_t last_error;
    bool window_open;
    bool initialized;
    bool pending_valid;
    uint32_t pending_generation;
    uint32_t pending_sequence;
    uint32_t pending_segment_mask;
    size_t pending_size;
    uint8_t pending_frame[TDMA_LONG_FRAME_MAX];
} tdma_maintenance_service_t;

bool tdma_maintenance_init(tdma_maintenance_service_t *service,
                           tdma_long_frame_fifo_t *tx,
                           tdma_long_frame_fifo_t *rx,
                           const tdma_maintenance_config_t *config,
                           const tdma_maintenance_transport_ops_t *ops,
                           void *ops_context);

/* Call once per ring cycle from core1; caller gives way to T2 first. */
void tdma_maintenance_on_cycle(tdma_maintenance_service_t *service);

bool tdma_maintenance_submit_tx(tdma_maintenance_service_t *service,
                                const uint8_t *frame,
                                size_t frame_size,
                                uint32_t generation,
                                uint32_t sequence,
                                uint32_t segment_mask);

void tdma_maintenance_get_snapshot(const tdma_maintenance_service_t *service,
                                   tdma_maintenance_snapshot_t *snapshot);

#endif /* TDMA_MAINTENANCE_H */
