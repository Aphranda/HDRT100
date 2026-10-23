#include "tdma_transport_load.h"

#include <string.h>

bool tdma_transport_load_init(tdma_transport_load_t *load,
                              const tdma_transport_load_descriptor_t *descriptor)
{
    if (load == NULL || descriptor == NULL ||
        descriptor->load_id == 0u ||
        descriptor->window_period_cycles == 0u ||
        descriptor->max_frame_size == 0u ||
        descriptor->max_frame_size > TDMA_TRANSPORT_LOAD_MAX_BYTES ||
        descriptor->window_bytes == 0u ||
        descriptor->window_bytes > TDMA_TRANSPORT_LOAD_MAX_BYTES) {
        return false;
    }
    memset(load, 0, sizeof(*load));
    load->descriptor = *descriptor;
    return true;
}

bool tdma_transport_load_submit(tdma_transport_load_t *load,
                                const uint8_t *payload,
                                uint32_t payload_size)
{
    if (load == NULL || payload == NULL || payload_size == 0u ||
        payload_size > load->descriptor.max_frame_size ||
        payload_size > TDMA_TRANSPORT_LOAD_MAX_BYTES ||
        load->pending_valid) {
        if (load != NULL) {
            load->reject_count++;
        }
        return false;
    }
    memcpy(load->pending, payload, payload_size);
    load->pending_size = payload_size;
    load->pending_valid = true;
    load->submit_count++;
    return true;
}

bool tdma_transport_load_window_open(const tdma_transport_load_t *load)
{
    if (load == NULL || load->descriptor.window_period_cycles == 0u) {
        return false;
    }
    return load->window_cycle < load->descriptor.window_bytes &&
        load->pending_valid;
}

bool tdma_transport_load_take(tdma_transport_load_t *load,
                              uint8_t *payload,
                              uint32_t payload_capacity,
                              uint32_t *payload_size)
{
    if (load == NULL || payload == NULL || payload_size == NULL ||
        !load->pending_valid) {
        return false;
    }
    if (!tdma_transport_load_window_open(load)) {
        load->window_closed_count++;
        return false;
    }
    if (load->pending_size > payload_capacity) {
        return false;
    }
    memcpy(payload, load->pending, load->pending_size);
    *payload_size = load->pending_size;
    load->pending_valid = false;
    load->pending_size = 0u;
    load->served_count++;
    return true;
}

bool tdma_transport_load_pending(const tdma_transport_load_t *load)
{
    return load != NULL && load->pending_valid;
}

void tdma_transport_load_advance(tdma_transport_load_t *load)
{
    if (load == NULL || load->descriptor.window_period_cycles == 0u) {
        return;
    }
    load->window_cycle =
        (load->window_cycle + 1u) % load->descriptor.window_period_cycles;
}

void tdma_transport_load_get_stats(const tdma_transport_load_t *load,
                                   uint32_t *submitted,
                                   uint32_t *served,
                                   uint32_t *rejected,
                                   uint32_t *window_closed)
{
    if (load == NULL) {
        return;
    }
    if (submitted != NULL) {
        *submitted = load->submit_count;
    }
    if (served != NULL) {
        *served = load->served_count;
    }
    if (rejected != NULL) {
        *rejected = load->reject_count;
    }
    if (window_closed != NULL) {
        *window_closed = load->window_closed_count;
    }
}
