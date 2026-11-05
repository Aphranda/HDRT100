#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tdma_boot_sequence.h"

static tdma_boot_plan_t plan_for(tdma_boot_action_t action)
{
    tdma_boot_plan_t plan = {0};
    plan.action = action;
    plan.slot = action == TDMA_BOOT_ACTION_LISTEN ? 2u : 0u;
    plan.node_count = 4u;
    return plan;
}

int main(void)
{
    tdma_boot_step_t steps[TDMA_BOOT_SEQUENCE_MAX] = {0};

    /* No profile (v1 compatibility map): no step at all. */
    tdma_boot_plan_t none = plan_for(TDMA_BOOT_ACTION_NONE);
    assert(tdma_boot_sequence_build(&none, steps, TDMA_BOOT_SEQUENCE_MAX) == 0u);

    /* Coordinator: bring-up config, profile, ARM, then start TX. */
    tdma_boot_plan_t ref = plan_for(TDMA_BOOT_ACTION_START_REF);
    size_t count = tdma_boot_sequence_build(&ref, steps, TDMA_BOOT_SEQUENCE_MAX);
    assert(count == 7u);
    /* Mirrors the ring-start recipe: STOP, stage/apply, topology, bring-up. */
    assert(steps[0] == TDMA_BOOT_STEP_STOP);
    assert(steps[1] == TDMA_BOOT_STEP_STAGE_PROFILE);
    assert(steps[2] == TDMA_BOOT_STEP_APPLY_PROFILE);
    assert(steps[3] == TDMA_BOOT_STEP_SET_BRINGUP_TOPOLOGY);
    assert(steps[4] == TDMA_BOOT_STEP_SET_BRINGUP_NO_CAL);
    assert(steps[5] == TDMA_BOOT_STEP_RING_ARM);
    assert(steps[6] == TDMA_BOOT_STEP_RING_START);
    assert(strcmp(tdma_boot_step_name(steps[6]), "RING_START") == 0);

    /* Follower: same configuration, but ARM then RX listen, no TX. */
    tdma_boot_plan_t follower = plan_for(TDMA_BOOT_ACTION_LISTEN);
    count = tdma_boot_sequence_build(&follower, steps, TDMA_BOOT_SEQUENCE_MAX);
    assert(count == 7u);
    assert(steps[5] == TDMA_BOOT_STEP_RING_ARM);
    assert(steps[6] == TDMA_BOOT_STEP_RING_LISTEN);
    assert(strcmp(tdma_boot_step_name(steps[6]), "RING_LISTEN") == 0);

    /* Reject: stop and leave the reason to the caller. */
    tdma_boot_plan_t reject = plan_for(TDMA_BOOT_ACTION_REJECT);
    count = tdma_boot_sequence_build(&reject, steps, TDMA_BOOT_SEQUENCE_MAX);
    assert(count == 1u && steps[0] == TDMA_BOOT_STEP_STOP);

    /* The sequence never truncates silently into a partial start. */
    assert(tdma_boot_sequence_build(&ref, steps, 6u) == 0u);
    assert(tdma_boot_sequence_build(&ref, NULL, TDMA_BOOT_SEQUENCE_MAX) == 0u);
    assert(tdma_boot_sequence_build(NULL, steps, TDMA_BOOT_SEQUENCE_MAX) == 0u);

    printf("tdma boot sequence host tests passed\n");
    return 0;
}
