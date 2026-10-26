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

/* N5.2b: who may execute a config-stage command on this target. */
typedef enum {
    REFMEM_CONTROL_ORIGIN_LOCAL = 0u,
    REFMEM_CONTROL_ORIGIN_COORDINATOR = 1u,
    REFMEM_CONTROL_ORIGIN_REJECT = 2u,
} refmem_control_origin_t;

/*
 * Authorize a config-stage command origin against the derived control owner.
 *
 * The local maintenance entry keeps the CP9 exception: a command whose origin
 * is this board is always accepted (regardless of whether an owner exists).
 * Otherwise only the derived control owner may issue config commands; every
 * other origin is rejected (fail closed) and must not fall through to the idle
 * window decision.
 */
refmem_control_origin_t refmem_control_origin_decide(uint32_t origin_slot,
                                                     uint32_t local_slot,
                                                     uint32_t owner_slot,
                                                     bool owner_valid);

/* N5.5 entry convergence: a remote system command may only be originated by the
 * control owner.  Non-owner entries keep local control (CP9), but must be told
 * that a remote command cannot be delivered instead of queueing a dead intent
 * (the non-owner has no TDMA maintenance uplink). */
typedef enum {
    REFMEM_CONTROL_ENTRY_LOCAL_ONLY = 0u,
    REFMEM_CONTROL_ENTRY_REMOTE_ALLOWED = 1u,
    REFMEM_CONTROL_ENTRY_REJECT_REMOTE = 2u,
} refmem_control_entry_t;

refmem_control_entry_t refmem_control_entry_decide(uint32_t local_slot,
                                                   uint32_t owner_slot,
                                                   bool owner_valid,
                                                   uint32_t target_mask);

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
