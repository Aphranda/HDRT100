#include "tdma_maintenance.h"

#include <string.h>

bool tdma_maintenance_init(tdma_maintenance_service_t *service,
                           tdma_long_frame_fifo_t *tx,
                           tdma_long_frame_fifo_t *rx,
                           const tdma_maintenance_config_t *config,
                           const tdma_maintenance_transport_ops_t *ops,
                           void *ops_context)
{
    if (service == NULL || tx == NULL || rx == NULL || ops == NULL) {
        return false;
    }
    memset(service, 0, sizeof(*service));
    service->tx = tx;
    service->rx = rx;
    service->ops = *ops;
    service->ops_context = ops_context;
    service->config.period_cycles = TDMA_MAINTENANCE_DEFAULT_PERIOD_CYCLES;
    service->config.window_cycles = TDMA_MAINTENANCE_DEFAULT_WINDOW_CYCLES;
    service->config.max_window_bytes = TDMA_MAINTENANCE_MAX_WINDOW_BYTES;
    if (config != NULL) {
        service->config = *config;
    }
    if (service->config.period_cycles == 0u ||
        service->config.window_cycles == 0u ||
        service->config.window_cycles > service->config.period_cycles ||
        service->config.max_window_bytes == 0u ||
        service->config.max_window_bytes > TDMA_MAINTENANCE_MAX_WINDOW_BYTES) {
        return false;
    }
    service->initialized = true;
    return true;
}

static void poll_rx(tdma_maintenance_service_t *service)
{
    if (service->ops.poll == NULL) {
        return;
    }
    uint8_t frame[TDMA_LONG_FRAME_MAX];
    size_t frame_size = 0u;
    uint32_t generation = 0u;
    uint32_t sequence = 0u;
    uint32_t segment_mask = 0u;
    if (!service->ops.poll(service->ops_context,
                           frame,
                           sizeof(frame),
                           &frame_size,
                           &generation,
                           &sequence,
                           &segment_mask)) {
        return;
    }
    if (frame_size == 0u || frame_size > sizeof(frame) ||
        generation == 0u) {
        service->rx_reject_count++;
        return;
    }
    if (!tdma_long_frame_publish_rx(service->rx,
                                    frame,
                                    frame_size,
                                    generation,
                                    sequence,
                                    segment_mask)) {
        service->rx_reject_count++;
        return;
    }
    service->rx_published_count++;
}

static void send_tx(tdma_maintenance_service_t *service)
{
    if (!service->window_open || service->ops.send == NULL) {
        return;
    }
    if (!service->pending_valid) {
        tdma_long_frame_view_t view;
        if (!tdma_long_frame_acquire_tx(service->tx, &view)) {
            return;
        }
        if (view.data_size > service->config.max_window_bytes ||
            view.data_size > TDMA_LONG_FRAME_MAX) {
            service->tx_blocked_count++;
            tdma_long_frame_release_tx(service->tx, view.slot_index);
            return;
        }
        memcpy(service->pending_frame, view.data, view.data_size);
        service->pending_size = view.data_size;
        service->pending_generation = view.generation;
        service->pending_sequence = view.sequence;
        service->pending_segment_mask = view.segment_mask;
        service->pending_valid = true;
        tdma_long_frame_release_tx(service->tx, view.slot_index);
    }
    if (!service->ops.send(service->ops_context,
                           service->pending_frame,
                           service->pending_size,
                           service->pending_generation,
                           service->pending_sequence,
                           service->pending_segment_mask)) {
        service->tx_blocked_count++;
        /* Keep the pending frame for the next maintenance window. */
        service->last_error = 1u;
        return;
    }
    service->pending_valid = false;
    service->tx_sent_count++;
}

void tdma_maintenance_on_cycle(tdma_maintenance_service_t *service)
{
    if (service == NULL || !service->initialized) {
        return;
    }
    const uint32_t phase = service->cycle_count % service->config.period_cycles;
    service->cycle_count++;
    const bool was_open = service->window_open;
    service->window_open = phase < service->config.window_cycles;
    if (service->window_open && !was_open) {
        service->window_open_count++;
    }
    poll_rx(service);
    send_tx(service);
}

bool tdma_maintenance_submit_tx(tdma_maintenance_service_t *service,
                                const uint8_t *frame,
                                size_t frame_size,
                                uint32_t generation,
                                uint32_t sequence,
                                uint32_t segment_mask)
{
    if (service == NULL || !service->initialized) {
        return false;
    }
    return tdma_long_frame_publish_tx(service->tx,
                                      frame,
                                      frame_size,
                                      generation,
                                      sequence,
                                      segment_mask);
}

void tdma_maintenance_get_snapshot(const tdma_maintenance_service_t *service,
                                   tdma_maintenance_snapshot_t *snapshot)
{
    if (service == NULL || snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = TDMA_MAINTENANCE_VERSION;
    snapshot->cycle_count = service->cycle_count;
    snapshot->window_open_count = service->window_open_count;
    snapshot->tx_sent_count = service->tx_sent_count;
    snapshot->tx_blocked_count = service->tx_blocked_count;
    snapshot->rx_published_count = service->rx_published_count;
    snapshot->rx_reject_count = service->rx_reject_count;
    snapshot->last_error = service->last_error;
    snapshot->window_open = service->window_open;
}
