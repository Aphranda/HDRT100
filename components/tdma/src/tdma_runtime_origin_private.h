#ifndef TDMA_RUNTIME_ORIGIN_PRIVATE_H
#define TDMA_RUNTIME_ORIGIN_PRIVATE_H

#include "tdma_origin_owner_ram.h"
#include "tdma_runtime_origin.h"

/* The owner keeps its device state file-private; this unit drives it
 * through these accessors, aliased to the names the fragment used. */
tdma_service_service_t *tdma_owner_service_state(void);
tdma_pio_spi_phys_t *tdma_owner_phys_state(void);
tdma_pio_spi_ring_adapter_t *tdma_owner_ring_state(void);

#define s_tdma_runtime_owner (*tdma_owner_service_state())
#define s_tdma_pio_spi_phys (*tdma_owner_phys_state())
#define s_tdma_pio_spi_ring_adapter (*tdma_owner_ring_state())

#endif /* TDMA_RUNTIME_ORIGIN_PRIVATE_H */
