#ifndef REFMEM_CONTROL_ROUTE_H
#define REFMEM_CONTROL_ROUTE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REFMEM_CONTROL_ROUTE_LOCAL = 0u,
    REFMEM_CONTROL_ROUTE_UNIFIED = 1u,
    REFMEM_CONTROL_ROUTE_REJECT_NO_TARGET = 2u,
    REFMEM_CONTROL_ROUTE_REJECT_UNIFIED_UNAVAILABLE = 3u,
    REFMEM_CONTROL_ROUTE_REJECT_BAD_SLOT = 4u,
} refmem_control_route_t;

/*
 * Decide whether a resolved command goes local-only, unified, or is rejected.
 *
 * local_slot is this board's slot. target_mask is already resolved against the
 * active SlotClaimMap; this function does not perform claim lookup.
 */
refmem_control_route_t refmem_control_route_decide(uint32_t local_slot,
                                                   uint32_t target_mask,
                                                   bool unified_available);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_CONTROL_ROUTE_H */
