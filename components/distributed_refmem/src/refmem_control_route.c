#include "refmem_control_route.h"

#define SLOT_BIT(slot) (1u << (slot))

refmem_control_route_t refmem_control_route_decide(uint32_t local_slot,
                                                   uint32_t target_mask,
                                                   bool unified_available)
{
    uint32_t local_bit;

    if (local_slot >= 32u) {
        return REFMEM_CONTROL_ROUTE_REJECT_BAD_SLOT;
    }
    if (target_mask == 0u) {
        return REFMEM_CONTROL_ROUTE_REJECT_NO_TARGET;
    }

    local_bit = SLOT_BIT(local_slot);
    if (target_mask == local_bit) {
        return REFMEM_CONTROL_ROUTE_LOCAL;
    }
    if (!unified_available) {
        return REFMEM_CONTROL_ROUTE_REJECT_UNIFIED_UNAVAILABLE;
    }
    return REFMEM_CONTROL_ROUTE_UNIFIED;
}

refmem_control_origin_t refmem_control_origin_decide(uint32_t origin_slot,
                                                     uint32_t local_slot,
                                                     uint32_t owner_slot,
                                                     bool owner_valid)
{
    if (origin_slot >= 32u) {
        return REFMEM_CONTROL_ORIGIN_REJECT;
    }
    if (origin_slot == local_slot) {
        return REFMEM_CONTROL_ORIGIN_LOCAL;
    }
    if (owner_valid && origin_slot == owner_slot) {
        return REFMEM_CONTROL_ORIGIN_COORDINATOR;
    }
    return REFMEM_CONTROL_ORIGIN_REJECT;
}
