#ifndef TDMA_RUNTIME_OWNER_H
#define TDMA_RUNTIME_OWNER_H

#include <stdbool.h>

#include "tdma_pio_spi_phys.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_service.h"

/* Product firmware has one TDMA owner. Domain wrappers register payloads and
 * adapter operations against it; they do not create parallel runtimes. */
bool tdma_runtime_owner_init(void);
tdma_service_service_t *tdma_runtime_owner_get(void);
tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void);
tdma_pio_spi_ring_adapter_t *tdma_runtime_owner_get_ring_adapter(void);

/* Read-only ring snapshot for low-frequency maintenance logging on core0
 * (the resident ring itself is driven by the core1 TDMA service). */
bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *snapshot);

/* Read-only physical-layer snapshot (RX capture stall/partial counters and
 * TX timeout counters) for bring-up diagnostics. */
bool tdma_runtime_owner_get_phys_snapshot(tdma_pio_spi_phys_snapshot_t *snapshot);

#endif
