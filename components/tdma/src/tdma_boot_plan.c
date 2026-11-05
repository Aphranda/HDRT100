#include "tdma_boot_plan.h"

#include <stddef.h>

void tdma_boot_plan_decide(bool has_profile,
                           const tdma_boot_profile_t *profile,
                           uint32_t expected_nodes,
                           uint32_t local_slot,
                           tdma_boot_plan_t *out)
{
    if (out == NULL) {
        return;
    }
    out->action = TDMA_BOOT_ACTION_NONE;
    out->reason = TDMA_BOOT_REASON_NONE;
    out->slot = 0u;
    out->node_count = 0u;

    if (!has_profile || profile == NULL) {
        out->reason = TDMA_BOOT_REASON_NO_PROFILE;
        return;
    }
    if (!tdma_boot_profile_valid(profile)) {
        out->action = TDMA_BOOT_ACTION_REJECT;
        out->reason = TDMA_BOOT_REASON_BAD_PROFILE;
        return;
    }
    out->slot = profile->local_slot;
    out->node_count = profile->node_count;
    if (expected_nodes != 0u && profile->node_count != expected_nodes) {
        out->action = TDMA_BOOT_ACTION_REJECT;
        out->reason = TDMA_BOOT_REASON_NODE_MISMATCH;
        return;
    }
    if (local_slot != 0u && profile->local_slot != local_slot) {
        out->action = TDMA_BOOT_ACTION_REJECT;
        out->reason = TDMA_BOOT_REASON_SLOT_MISMATCH;
        return;
    }
    /* The coordinator is the lowest slot; every other slot listens first and
     * joins only after hearing the coordinator, so a missing board shows up as
     * a missing slot instead of a silent partial ring. */
    out->action = profile->local_slot == 0u ? TDMA_BOOT_ACTION_START_REF
                                            : TDMA_BOOT_ACTION_LISTEN;
}
