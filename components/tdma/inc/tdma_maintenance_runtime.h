#ifndef TDMA_MAINTENANCE_RUNTIME_H
#define TDMA_MAINTENANCE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_pio_spi_ring_adapter.h"

#define TDMA_MAINTENANCE_RUNTIME_MAX TDMA_LONG_FRAME_MAX

bool tdma_maintenance_runtime_init(tdma_pio_spi_ring_adapter_t *adapter,
                                   uint32_t local_slot);
bool tdma_maintenance_runtime_ready(void);
bool tdma_maintenance_runtime_submit(const uint8_t *frame,
                                     size_t frame_size,
                                     uint8_t stream_id,
                                     uint8_t generation);
bool tdma_maintenance_runtime_take(uint8_t *frame,
                                   size_t frame_capacity,
                                   size_t *frame_size,
                                   uint8_t *stream_id,
                                   uint8_t *generation);
void tdma_maintenance_runtime_get_counters(uint32_t *mailbox_calls,
                                           uint32_t *chunks,
                                           uint32_t *frames,
                                           uint32_t *rejects);

#endif /* TDMA_MAINTENANCE_RUNTIME_H */
