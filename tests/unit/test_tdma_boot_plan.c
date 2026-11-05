#include <assert.h>
#include <stdio.h>

#include "tdma_boot_plan.h"

static tdma_boot_profile_t profile_for(uint32_t slot, uint32_t nodes)
{
    tdma_boot_profile_t profile = {0};
    profile.version = TDMA_BOOT_PROFILE_VERSION;
    profile.profile_level = 23u;
    profile.node_count = nodes;
    profile.local_slot = slot;
    profile.flags = TDMA_BOOT_PROFILE_FLAG_BRINGUP_NO_CAL;
    profile.topology_crc32 = 0x1234u;
    profile.generation = 1u;
    return profile;
}

int main(void)
{
    tdma_boot_plan_t plan = {0};

    /* v1 compatibility map: no profile, no self-formation. */
    tdma_boot_plan_decide(false, NULL, 4u, 0u, &plan);
    assert(plan.action == TDMA_BOOT_ACTION_NONE);
    assert(plan.reason == TDMA_BOOT_REASON_NO_PROFILE);

    /* Coordinator starts; followers listen. */
    const tdma_boot_profile_t ref = profile_for(0u, 4u);
    tdma_boot_plan_decide(true, &ref, 4u, 0u, &plan);
    assert(plan.action == TDMA_BOOT_ACTION_START_REF);
    assert(plan.reason == TDMA_BOOT_REASON_NONE);
    assert(plan.slot == 0u);
    assert(plan.node_count == 4u);

    const tdma_boot_profile_t follower = profile_for(2u, 4u);
    tdma_boot_plan_decide(true, &follower, 4u, 2u, &plan);
    assert(plan.action == TDMA_BOOT_ACTION_LISTEN);
    assert(plan.reason == TDMA_BOOT_REASON_NONE);

    /* A profile that disagrees with the deployment is rejected, not formed. */
    tdma_boot_plan_decide(true, &follower, 3u, 2u, &plan);
    assert(plan.action == TDMA_BOOT_ACTION_REJECT);
    assert(plan.reason == TDMA_BOOT_REASON_NODE_MISMATCH);

    tdma_boot_plan_decide(true, &follower, 4u, 1u, &plan);
    assert(plan.action == TDMA_BOOT_ACTION_REJECT);
    assert(plan.reason == TDMA_BOOT_REASON_SLOT_MISMATCH);

    /* A corrupt record is rejected before any action. */
    tdma_boot_profile_t bad = profile_for(1u, 4u);
    bad.local_slot = 4u;
    tdma_boot_plan_decide(true, &bad, 4u, 0u, &plan);
    assert(plan.action == TDMA_BOOT_ACTION_REJECT);
    assert(plan.reason == TDMA_BOOT_REASON_BAD_PROFILE);

    /* Unknown expected node count skips the node check, not the slot check. */
    tdma_boot_plan_decide(true, &follower, 0u, 2u, &plan);
    assert(plan.action == TDMA_BOOT_ACTION_LISTEN);
    tdma_boot_plan_decide(true, &ref, 0u, 0u, &plan);
    assert(plan.action == TDMA_BOOT_ACTION_START_REF);

    /* Null output is a no-op, not a crash. */
    tdma_boot_plan_decide(true, &ref, 4u, 0u, NULL);

    printf("tdma boot plan host tests passed\n");
    return 0;
}
