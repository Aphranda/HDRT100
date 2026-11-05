#ifndef TDMA_BOOT_PLAN_H
#define TDMA_BOOT_PLAN_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_boot_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure decision for the single-port guide-ring self-formation (N1 option B).
 *
 * The coordinator (lowest slot, normally slot 0) starts transmitting; every
 * follower boots into RX listen and joins on the first valid frame. A board
 * without a persisted boot profile (v1 compatibility map) does nothing and
 * keeps the multi-port bring-up. A profile that disagrees with the expected
 * node set is rejected instead of forming a half ring.
 */

typedef enum {
    TDMA_BOOT_ACTION_NONE = 0u,      /* no profile: no self-formation */
    TDMA_BOOT_ACTION_START_REF = 1u, /* coordinator: arm + start TX */
    TDMA_BOOT_ACTION_LISTEN = 2u,    /* follower: RX listen, no TX */
    TDMA_BOOT_ACTION_REJECT = 3u,    /* inconsistent: stay STOPPED */
} tdma_boot_action_t;

typedef enum {
    TDMA_BOOT_REASON_NONE = 0u,
    TDMA_BOOT_REASON_NO_PROFILE = 1u,
    TDMA_BOOT_REASON_BAD_PROFILE = 2u,
    TDMA_BOOT_REASON_NODE_MISMATCH = 3u,
    TDMA_BOOT_REASON_SLOT_MISMATCH = 4u,
} tdma_boot_reason_t;

typedef struct {
    tdma_boot_action_t action;
    tdma_boot_reason_t reason;
    uint32_t slot;
    uint32_t node_count;
} tdma_boot_plan_t;

/* expected_nodes is the node count the deployment expects (0 = unknown/do not
 * check). local_slot, when nonzero, must match the profile slot. */
void tdma_boot_plan_decide(bool has_profile,
                           const tdma_boot_profile_t *profile,
                           uint32_t expected_nodes,
                           uint32_t local_slot,
                           tdma_boot_plan_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TDMA_BOOT_PLAN_H */
