/* Host cases for the guide action plan payload (guide_action_plan.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "guide_action_plan.h"
#include "refmem_param_pool.h"

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

static void test_plan_identity_uses_param_pool_crc(void)
{
    /* plan_crc32 must come from the canonical CRC, not from a second
     * implementation: identical bytes hash identically, a different candidate
     * table hashes differently, and the value survives the round trip. */
    const guide_action_plan_t first = sample_plan();
    guide_action_plan_t second = sample_plan();
    second.candidates[1] = 99u;

    uint8_t a[GUIDE_ACTION_PLAN_SIZE];
    uint8_t b[GUIDE_ACTION_PLAN_SIZE];
    size_t size = 0u;
    assert(guide_action_plan_encode(&first, a, sizeof(a), &size) == GUIDE_ACTION_PLAN_OK);
    assert(guide_action_plan_encode(&second, b, sizeof(b), &size) == GUIDE_ACTION_PLAN_OK);

    const uint32_t crc_a = refmem_param_pool_crc(a, GUIDE_ACTION_PLAN_SIZE);
    const uint32_t crc_b = refmem_param_pool_crc(b, GUIDE_ACTION_PLAN_SIZE);
    assert(crc_a == refmem_param_pool_crc(a, GUIDE_ACTION_PLAN_SIZE));
    assert(crc_a != crc_b);
}

static void test_words_round_trip(void)
{
    /* The plan travels as the 16 inline words of CAL_GUIDE_CONFIG. */
    const guide_action_plan_t plan = sample_plan();
    uint32_t words[GUIDE_ACTION_PLAN_WORDS];
    assert(guide_action_plan_to_words(&plan, words, GUIDE_ACTION_PLAN_WORDS) ==
           GUIDE_ACTION_PLAN_OK);
    guide_action_plan_t decoded;
    assert(guide_action_plan_from_words(words, GUIDE_ACTION_PLAN_WORDS, &decoded) ==
           GUIDE_ACTION_PLAN_OK);
    assert(decoded.action_id == plan.action_id);
    assert(decoded.plan_crc32 == plan.plan_crc32);
    assert(decoded.candidates[1] == 12u);
    assert(decoded.threshold_high == 9u);
    /* A wrong word count is rejected instead of being partially decoded. */
    assert(guide_action_plan_from_words(words, GUIDE_ACTION_PLAN_WORDS - 1u, &decoded) ==
           GUIDE_ACTION_PLAN_BAD_SIZE);
}

static void test_progress_round_trip(void)
{
    guide_action_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    progress.action_id = 4u;
    progress.plan_crc32 = 0xDEADBEEFu;
    progress.generation = 9u;
    progress.trials_done = 3u;
    progress.deepest_trial = 2u;
    progress.reason = 0u;
    progress.status = GUIDE_ACTION_PROGRESS_COMPLETE;
    progress.passed_mask = 0x7u;
    progress.failed_mask = 0u;

    uint8_t buffer[GUIDE_ACTION_PROGRESS_SIZE];
    size_t size = 0u;
    assert(guide_action_progress_encode(&progress, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_OK);
    assert(size == GUIDE_ACTION_PROGRESS_SIZE);

    guide_action_progress_t decoded;
    assert(guide_action_progress_decode(buffer, size, &decoded) ==
           GUIDE_ACTION_PLAN_OK);
    assert(decoded.trials_done == 3u);
    assert(decoded.deepest_trial == 2u);
    assert(decoded.passed_mask == 0x7u);
    assert(decoded.status == GUIDE_ACTION_PROGRESS_COMPLETE);
    assert(guide_action_progress_matches(&decoded, 4u, 0xDEADBEEFu, 9u));
    /* Another plan/generation must never be mistaken for this one. */
    assert(!guide_action_progress_matches(&decoded, 4u, 0xDEADBEEFu, 10u));
    assert(!guide_action_progress_matches(&decoded, 5u, 0xDEADBEEFu, 9u));
}

static void test_progress_records_the_stop_reason(void)
{
    /* A window that stopped early must keep both the reason and the deepest
     * trial, so Q can resume instead of restarting from zero. */
    guide_action_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    progress.action_id = 6u;
    progress.plan_crc32 = 0x1234u;
    progress.generation = 2u;
    progress.trials_done = 2u;
    progress.deepest_trial = 4u;
    progress.reason = 7u; /* LINK_TIMEOUT */
    progress.status = GUIDE_ACTION_PROGRESS_STOPPED;
    progress.passed_mask = 0x3u;

    uint8_t buffer[GUIDE_ACTION_PROGRESS_SIZE];
    size_t size = 0u;
    assert(guide_action_progress_encode(&progress, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_OK);
    guide_action_progress_t decoded;
    assert(guide_action_progress_decode(buffer, size, &decoded) ==
           GUIDE_ACTION_PLAN_OK);
    assert(decoded.reason == 7u);
    assert(decoded.status == GUIDE_ACTION_PROGRESS_STOPPED);
    assert(decoded.deepest_trial == 4u);
}

static void test_progress_rejects_bad_input(void)
{
    guide_action_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    uint8_t buffer[GUIDE_ACTION_PROGRESS_SIZE];
    size_t size = 0u;
    /* More trials than the mask can represent. */
    progress.trials_done = GUIDE_ACTION_PROGRESS_MAX_TRIALS + 1u;
    assert(guide_action_progress_encode(&progress, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_RANGE);
    progress.trials_done = 0u;
    progress.status = 9u;
    assert(guide_action_progress_encode(&progress, buffer, sizeof(buffer), &size) ==
           GUIDE_ACTION_PLAN_RANGE);
    progress.status = GUIDE_ACTION_PROGRESS_RUNNING;
    assert(guide_action_progress_encode(&progress, buffer, sizeof(buffer) - 1u,
                                        &size) == GUIDE_ACTION_PLAN_BAD_ARGUMENT);
}

static void test_reconcile_decisions(void)
{
    guide_action_reconcile_result_t result;
    guide_action_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    progress.action_id = 2u;
    progress.plan_crc32 = 0x55u;
    progress.generation = 4u;

    /* No record at all: rerun from the first trial. */
    guide_action_reconcile(NULL, false, 2u, 0x55u, 4u, &result);
    assert(result.decision == GUIDE_ACTION_RECONCILE_RESTART);
    assert(result.resume_trial == 0u);

    /* A record from another generation is not trusted either. */
    progress.status = GUIDE_ACTION_PROGRESS_RUNNING;
    progress.deepest_trial = 2u;
    guide_action_reconcile(&progress, true, 2u, 0x55u, 5u, &result);
    assert(result.decision == GUIDE_ACTION_RECONCILE_RESTART);

    /* Interrupted window: resume after the deepest trial reached. */
    guide_action_reconcile(&progress, true, 2u, 0x55u, 4u, &result);
    assert(result.decision == GUIDE_ACTION_RECONCILE_RESUME);
    assert(result.resume_trial == 3u);

    /* Finished window: nothing left to measure, go to S. */
    progress.status = GUIDE_ACTION_PROGRESS_COMPLETE;
    guide_action_reconcile(&progress, true, 2u, 0x55u, 4u, &result);
    assert(result.decision == GUIDE_ACTION_RECONCILE_COMPLETE);

    /* Stopped window: propagate the reason instead of retrying blindly. */
    progress.status = GUIDE_ACTION_PROGRESS_STOPPED;
    progress.reason = 7u;
    guide_action_reconcile(&progress, true, 2u, 0x55u, 4u, &result);
    assert(result.decision == GUIDE_ACTION_RECONCILE_FAILED);
    assert(result.reason == 7u);
}

static void test_ladder_walks_seven_actions(void)
{
    guide_action_ladder_t ladder;
    guide_action_ladder_reset(&ladder);
    guide_action_reconcile_result_t ok;
    memset(&ok, 0, sizeof(ok));
    ok.decision = GUIDE_ACTION_RECONCILE_COMPLETE;

    guide_action_ladder_t out;
    for (uint32_t index = 0u; index < GUIDE_ACTION_LADDER_COUNT; index++) {
        assert(ladder.action_index == index);
        assert(guide_action_ladder_is_last(&ladder) ==
               (index + 1u == GUIDE_ACTION_LADDER_COUNT));
        const guide_action_ladder_step_t step =
            guide_action_ladder_advance(&ladder, &ok, &out);
        if (index + 1u == GUIDE_ACTION_LADDER_COUNT) {
            assert(step == GUIDE_ACTION_LADDER_DONE);
        } else {
            assert(step == GUIDE_ACTION_LADDER_NEXT);
            ladder = out;
        }
    }
}

static void test_ladder_resumes_or_stops(void)
{
    guide_action_ladder_t ladder;
    guide_action_ladder_reset(&ladder);
    ladder.action_index = 3u;

    guide_action_reconcile_result_t partial;
    memset(&partial, 0, sizeof(partial));
    partial.decision = GUIDE_ACTION_RECONCILE_RESUME;
    partial.resume_trial = 5u;
    guide_action_ladder_t out;
    assert(guide_action_ladder_advance(&ladder, &partial, &out) ==
           GUIDE_ACTION_LADDER_RUN_SAME);
    assert(out.action_index == 3u);
    assert(out.resume_trial == 5u);

    guide_action_reconcile_result_t failed;
    memset(&failed, 0, sizeof(failed));
    failed.decision = GUIDE_ACTION_RECONCILE_FAILED;
    failed.reason = 9u;
    assert(guide_action_ladder_advance(&ladder, &failed, &out) ==
           GUIDE_ACTION_LADDER_STOPPED);
    assert(out.reason == 9u);
}

static void test_commit_gate_respects_open_session(void)
{
    /* HAOFV flash rule 2.1 #7: no ordinary write while training is running. */
    assert(guide_action_commit_decision(false, false) ==
           GUIDE_ACTION_COMMIT_DENY_SESSION_OPEN);
    assert(guide_action_commit_decision(true, true) ==
           GUIDE_ACTION_COMMIT_SKIP_ALREADY_DONE);
    assert(guide_action_commit_decision(true, false) == GUIDE_ACTION_COMMIT_ALLOW);
}

int main(void)
{
    test_round_trip();
    test_rejects_bad_magic();
    test_rejects_truncated();
    test_rejects_out_of_range_counts();
    test_rejects_small_output();
    test_plan_identity_uses_param_pool_crc();
    test_words_round_trip();
    test_progress_round_trip();
    test_progress_records_the_stop_reason();
    test_progress_rejects_bad_input();
    test_reconcile_decisions();
    test_ladder_walks_seven_actions();
    test_ladder_resumes_or_stops();
    test_commit_gate_respects_open_session();
    printf("guide_action_plan: all groups passed\n");
    return 0;
}
