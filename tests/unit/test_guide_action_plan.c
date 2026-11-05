/* Host cases for the guide action plan payload (guide_action_plan.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "guide_action_plan.h"

static guide_action_plan_t sample_plan(void)
{
    guide_action_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    plan.action_id = 3u;
    plan.plan_crc32 = 0xA1B2C3D4u;
    plan.generation = 7u;
    plan.profile_level = 23u;
    plan.window_ms = 4000u;
    plan.trial_count = 6u;
    plan.candidate_count = 3u;
    plan.candidates[0] = 11u;
    plan.candidates[1] = 12u;
    plan.candidates[2] = 13u;
    plan.threshold_low = 4u;
    plan.threshold_high = 9u;
    return plan;
}

static void test_round_trip(void)
{
    const guide_action_plan_t plan = sample_plan();
    uint8_t buffer[GUIDE_ACTION_PLAN_SIZE];
    size_t size = 0u;
    assert(guide_action_plan_encode(&plan, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_OK);
    assert(size == GUIDE_ACTION_PLAN_SIZE);
    guide_action_plan_t decoded;
    assert(guide_action_plan_decode(buffer, size, &decoded) == GUIDE_ACTION_PLAN_OK);
    assert(decoded.action_id == plan.action_id);
    assert(decoded.plan_crc32 == plan.plan_crc32);
    assert(decoded.generation == plan.generation);
    assert(decoded.profile_level == plan.profile_level);
    assert(decoded.window_ms == plan.window_ms);
    assert(decoded.trial_count == plan.trial_count);
    assert(decoded.candidate_count == plan.candidate_count);
    assert(decoded.candidates[2] == 13u);
    assert(decoded.threshold_low == 4u);
    assert(decoded.threshold_high == 9u);
}

static void test_rejects_bad_magic(void)
{
    guide_action_plan_t plan = sample_plan();
    uint8_t buffer[GUIDE_ACTION_PLAN_SIZE];
    size_t size = 0u;
    assert(guide_action_plan_encode(&plan, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_OK);
    buffer[0] ^= 0xffu;
    guide_action_plan_t decoded;
    assert(guide_action_plan_decode(buffer, size, &decoded) ==
           GUIDE_ACTION_PLAN_BAD_MAGIC);
}

static void test_rejects_truncated(void)
{
    guide_action_plan_t plan = sample_plan();
    uint8_t buffer[GUIDE_ACTION_PLAN_SIZE];
    size_t size = 0u;
    assert(guide_action_plan_encode(&plan, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_OK);
    guide_action_plan_t decoded;
    assert(guide_action_plan_decode(buffer, size - 1u, &decoded) ==
           GUIDE_ACTION_PLAN_BAD_SIZE);
}

static void test_rejects_out_of_range_counts(void)
{
    guide_action_plan_t plan = sample_plan();
    uint8_t buffer[GUIDE_ACTION_PLAN_SIZE];
    size_t size = 0u;
    plan.candidate_count = GUIDE_ACTION_PLAN_MAX_CANDIDATES + 1u;
    assert(guide_action_plan_encode(&plan, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_RANGE);
    plan = sample_plan();
    plan.trial_count = 0u;
    assert(guide_action_plan_encode(&plan, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_RANGE);
}

static void test_rejects_small_output(void)
{
    const guide_action_plan_t plan = sample_plan();
    uint8_t buffer[GUIDE_ACTION_PLAN_SIZE - 1u];
    size_t size = 0u;
    assert(guide_action_plan_encode(&plan, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_BAD_ARGUMENT);
}

int main(void)
{
    test_round_trip();
    test_rejects_bad_magic();
    test_rejects_truncated();
    test_rejects_out_of_range_counts();
    test_rejects_small_output();
    printf("guide_action_plan: all groups passed\n");
    return 0;
}
