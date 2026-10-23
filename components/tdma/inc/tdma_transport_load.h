#ifndef TDMA_TRANSPORT_LOAD_H
#define TDMA_TRANSPORT_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#define TDMA_TRANSPORT_LOAD_MAX_BYTES 96u
#define TDMA_TRANSPORT_LOAD_VERSION 1u

/* A table-declared payload that rides a TDMA seat. The load owns no PIO, DMA,
 * ring or command semantics; it only declares capacity and window facts. */
typedef struct {
    uint32_t load_id;
    uint32_t node_id;
    uint32_t payload_class;
    uint32_t seat_class;      /* TDMA_LOOP_SLOT_* */
    uint32_t window_period_cycles;
    uint32_t window_bytes;
    uint32_t max_frame_size;
    uint32_t required_mask;
} tdma_transport_load_descriptor_t;

typedef struct tdma_transport_load {
    tdma_transport_load_descriptor_t descriptor;
    uint8_t pending[TDMA_TRANSPORT_LOAD_MAX_BYTES];
    uint32_t pending_size;
    bool pending_valid;
    uint32_t window_cycle;
    uint32_t submit_count;
    uint32_t served_count;
    uint32_t reject_count;
    uint32_t window_closed_count;
} tdma_transport_load_t;

bool tdma_transport_load_init(tdma_transport_load_t *load,
                              const tdma_transport_load_descriptor_t *descriptor);
bool tdma_transport_load_submit(tdma_transport_load_t *load,
                                const uint8_t *payload,
                                uint32_t payload_size);
bool tdma_transport_load_window_open(const tdma_transport_load_t *load);
bool tdma_transport_load_take(tdma_transport_load_t *load,
                              uint8_t *payload,
                              uint32_t payload_capacity,
                              uint32_t *payload_size);
bool tdma_transport_load_pending(const tdma_transport_load_t *load);
void tdma_transport_load_advance(tdma_transport_load_t *load);
void tdma_transport_load_get_counts(const tdma_transport_load_t *load,
                                    uint32_t *served,
                                    uint32_t *rejected);
void tdma_transport_load_get_stats(const tdma_transport_load_t *load,
                                   uint32_t *submitted,
                                   uint32_t *served,
                                   uint32_t *rejected,
                                   uint32_t *window_closed);

#endif /* TDMA_TRANSPORT_LOAD_H */
