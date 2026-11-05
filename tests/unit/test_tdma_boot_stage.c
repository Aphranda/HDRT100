/* Host cases for the boot-time stage/profile decision (tdma_boot_stage.c).
 *
 * Pure logic only: no flash, no store, no profile manager. These cases pin the
 * fail-closed precedence the boot executor relies on: no stage -> guide
 * profile, ambiguous/unmatched stage -> reject (never a silent fallback). */
#include <assert.h>
#include <stdio.h>

#include "tdma_boot_stage.h"

static void test_no_stage(void)
{
    tdma_boot_stage_plan_t plan;
    const tdma_boot_stage_level_t levels[] = {{7u, 0x11u}};
    tdma_boot_stage_decide(false, 0x11u, levels, 1u, &plan);
    assert(plan.action == TDMA_BOOT_STAGE_ACTION_NONE);
    assert(plan.reason == TDMA_BOOT_STAGE_REASON_NO_STAGE);
    assert(plan.level == 0u);
}

static void test_matched_level(void)
{
    tdma_boot_stage_plan_t plan;
    const tdma_boot_stage_level_t levels[] = {
        {7u, 0x1111u}, {8u, 0x2222u}, {23u, 0x3333u}};
    tdma_boot_stage_decide(true, 0x2222u, levels, 3u, &plan);
    assert(plan.action == TDMA_BOOT_STAGE_ACTION_STAGE);
    assert(plan.reason == TDMA_BOOT_STAGE_REASON_NONE);
    assert(plan.level == 8u);
}

static void test_unmatched_is_rejected(void)
{
    tdma_boot_stage_plan_t plan;
    const tdma_boot_stage_level_t levels[] = {{7u, 0x1111u}};
    tdma_boot_stage_decide(true, 0x9999u, levels, 1u, &plan);
    assert(plan.action == TDMA_BOOT_STAGE_ACTION_REJECT);
    assert(plan.reason == TDMA_BOOT_STAGE_REASON_NO_MATCH);
    assert(plan.level == 0u);
}

static void test_bad_inputs_are_rejected(void)
{
    tdma_boot_stage_plan_t plan;
    const tdma_boot_stage_level_t levels[] = {{7u, 0x1111u}};
    tdma_boot_stage_decide(true, 0u, levels, 1u, &plan);
    assert(plan.action == TDMA_BOOT_STAGE_ACTION_REJECT);
    assert(plan.reason == TDMA_BOOT_STAGE_REASON_BAD_PROFILE);

    tdma_boot_stage_decide(true, 0x1111u, NULL, 1u, &plan);
    assert(plan.action == TDMA_BOOT_STAGE_ACTION_REJECT);
    tdma_boot_stage_decide(true, 0x1111u, levels, 0u, &plan);
    assert(plan.action == TDMA_BOOT_STAGE_ACTION_REJECT);

    /* A level id of 0 is not a valid catalog entry and must not match. */
    const tdma_boot_stage_level_t zero_level[] = {{0u, 0x1111u}};
    tdma_boot_stage_decide(true, 0x1111u, zero_level, 1u, &plan);
    assert(plan.action == TDMA_BOOT_STAGE_ACTION_REJECT);
    assert(plan.reason == TDMA_BOOT_STAGE_REASON_NO_MATCH);
}

static void test_null_output_is_safe(void)
{
    const tdma_boot_stage_level_t levels[] = {{7u, 0x1111u}};
    tdma_boot_stage_decide(true, 0x1111u, levels, 1u, NULL);
}

int main(void)
{
    test_no_stage();
    test_matched_level();
    test_unmatched_is_rejected();
    test_bad_inputs_are_rejected();
    test_null_output_is_safe();
    printf("tdma boot stage: 5 case groups passed\n");
    return 0;
}
