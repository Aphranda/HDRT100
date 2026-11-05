/* Guide action plan payload (see guide_action_plan.h). */
#include "guide_action_plan.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8u) & 0xffu);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8u) & 0xffu);
    p[2] = (uint8_t)((v >> 16u) & 0xffu);
    p[3] = (uint8_t)((v >> 24u) & 0xffu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static bool plan_ranges_ok(const guide_action_plan_t *plan)
{
    return plan != NULL && plan->candidate_count <= GUIDE_ACTION_PLAN_MAX_CANDIDATES &&
           plan->trial_count <= GUIDE_ACTION_PLAN_MAX_TRIALS &&
           plan->trial_count > 0u;
}

guide_action_plan_result_t guide_action_plan_encode(const guide_action_plan_t *plan,
                                                    uint8_t *out,
                                                    size_t capacity,
                                                    size_t *out_size)
{
    if (plan == NULL || out == NULL || capacity < GUIDE_ACTION_PLAN_SIZE) {
        return GUIDE_ACTION_PLAN_BAD_ARGUMENT;
    }
    if (!plan_ranges_ok(plan)) {
        return GUIDE_ACTION_PLAN_RANGE;
    }
    memset(out, 0, GUIDE_ACTION_PLAN_SIZE);
    put_u32(out + 0u, GUIDE_ACTION_PLAN_MAGIC);
    put_u16(out + 4u, (uint16_t)GUIDE_ACTION_PLAN_VERSION);
    put_u16(out + 6u, (uint16_t)GUIDE_ACTION_PLAN_SIZE);
    put_u32(out + 8u, plan->action_id);
    put_u32(out + 12u, plan->plan_crc32);
    put_u32(out + 16u, plan->generation);
    put_u32(out + 20u, plan->profile_level);
    put_u32(out + 24u, plan->window_ms);
    put_u32(out + 28u, plan->trial_count);
    put_u32(out + 32u, plan->candidate_count);
    for (size_t index = 0u; index < GUIDE_ACTION_PLAN_MAX_CANDIDATES; index++) {
        put_u32(out + 36u + (index * 4u),
                index < plan->candidate_count ? plan->candidates[index] : 0u);
    }
    put_u32(out + 56u, plan->threshold_low);
    put_u32(out + 60u, plan->threshold_high);
    if (out_size != NULL) {
        *out_size = GUIDE_ACTION_PLAN_SIZE;
    }
    return GUIDE_ACTION_PLAN_OK;
}

guide_action_plan_result_t guide_action_plan_from_words(const uint32_t *words,
                                                        uint32_t word_count,
                                                        guide_action_plan_t *out)
{
    uint8_t payload[GUIDE_ACTION_PLAN_SIZE];
    if (words == NULL || out == NULL) {
        return GUIDE_ACTION_PLAN_BAD_ARGUMENT;
    }
    if (word_count != GUIDE_ACTION_PLAN_WORDS) {
        return GUIDE_ACTION_PLAN_BAD_SIZE;
    }
    for (uint32_t index = 0u; index < GUIDE_ACTION_PLAN_WORDS; index++) {
        put_u32(payload + (index * 4u), words[index]);
    }
    return guide_action_plan_decode(payload, sizeof(payload), out);
}

guide_action_plan_result_t guide_action_plan_to_words(const guide_action_plan_t *plan,
                                                      uint32_t *words,
                                                      uint32_t word_count)
{
    uint8_t payload[GUIDE_ACTION_PLAN_SIZE];
    size_t size = 0u;
    if (words == NULL) {
        return GUIDE_ACTION_PLAN_BAD_ARGUMENT;
    }
    if (word_count != GUIDE_ACTION_PLAN_WORDS) {
        return GUIDE_ACTION_PLAN_BAD_SIZE;
    }
    const guide_action_plan_result_t encoded =
        guide_action_plan_encode(plan, payload, sizeof(payload), &size);
    if (encoded != GUIDE_ACTION_PLAN_OK) {
        return encoded;
    }
    for (uint32_t index = 0u; index < GUIDE_ACTION_PLAN_WORDS; index++) {
        words[index] = get_u32(payload + (index * 4u));
    }
    return GUIDE_ACTION_PLAN_OK;
}

guide_action_plan_result_t guide_action_plan_decode(const uint8_t *payload,
                                                    size_t size,
                                                    guide_action_plan_t *out)
{
    if (payload == NULL || out == NULL) {
        return GUIDE_ACTION_PLAN_BAD_ARGUMENT;
    }
    if (size != GUIDE_ACTION_PLAN_SIZE) {
        return GUIDE_ACTION_PLAN_BAD_SIZE;
    }
    if (get_u32(payload + 0u) != GUIDE_ACTION_PLAN_MAGIC) {
        return GUIDE_ACTION_PLAN_BAD_MAGIC;
    }
    if (get_u16(payload + 4u) != (uint16_t)GUIDE_ACTION_PLAN_VERSION) {
        return GUIDE_ACTION_PLAN_BAD_VERSION;
    }
    if (get_u16(payload + 6u) != (uint16_t)GUIDE_ACTION_PLAN_SIZE) {
        return GUIDE_ACTION_PLAN_BAD_SIZE;
    }
    guide_action_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    plan.action_id = get_u32(payload + 8u);
    plan.plan_crc32 = get_u32(payload + 12u);
    plan.generation = get_u32(payload + 16u);
    plan.profile_level = get_u32(payload + 20u);
    plan.window_ms = get_u32(payload + 24u);
    plan.trial_count = get_u32(payload + 28u);
    plan.candidate_count = get_u32(payload + 32u);
    for (size_t index = 0u; index < GUIDE_ACTION_PLAN_MAX_CANDIDATES; index++) {
        plan.candidates[index] = get_u32(payload + 36u + (index * 4u));
    }
    plan.threshold_low = get_u32(payload + 56u);
    plan.threshold_high = get_u32(payload + 60u);
    if (!plan_ranges_ok(&plan)) {
        return GUIDE_ACTION_PLAN_RANGE;
    }
    *out = plan;
    return GUIDE_ACTION_PLAN_OK;
}

/* ----------------------------------------------------------- progress ----- */

static bool progress_masks_ok(const guide_action_progress_t *progress)
{
    const uint32_t limit =
        GUIDE_ACTION_PROGRESS_MAX_TRIALS >= 32u
            ? 0xFFFFFFFFu
            : ((uint32_t)1u << GUIDE_ACTION_PROGRESS_MAX_TRIALS) - 1u;
    return progress != NULL && (progress->passed_mask & ~limit) == 0u &&
           (progress->failed_mask & ~limit) == 0u &&
           progress->trials_done <= GUIDE_ACTION_PROGRESS_MAX_TRIALS &&
           progress->status <= GUIDE_ACTION_PROGRESS_FAILED;
}

guide_action_plan_result_t guide_action_progress_encode(
    const guide_action_progress_t *progress, uint8_t *out, size_t capacity,
    size_t *out_size)
{
    if (progress == NULL || out == NULL || capacity < GUIDE_ACTION_PROGRESS_SIZE) {
        return GUIDE_ACTION_PLAN_BAD_ARGUMENT;
    }
    if (!progress_masks_ok(progress)) {
        return GUIDE_ACTION_PLAN_RANGE;
    }
    memset(out, 0, GUIDE_ACTION_PROGRESS_SIZE);
    put_u32(out + 0u, GUIDE_ACTION_PROGRESS_MAGIC);
    put_u16(out + 4u, (uint16_t)GUIDE_ACTION_PROGRESS_VERSION);
    put_u16(out + 6u, (uint16_t)GUIDE_ACTION_PROGRESS_SIZE);
    put_u32(out + 8u, progress->action_id);
    put_u32(out + 12u, progress->plan_crc32);
    put_u32(out + 16u, progress->generation);
    put_u32(out + 20u, progress->trials_done);
    put_u32(out + 24u, progress->deepest_trial);
    put_u32(out + 28u, progress->reason);
    put_u32(out + 32u, progress->status);
    put_u32(out + 36u, progress->passed_mask);
    put_u32(out + 40u, progress->failed_mask);
    if (out_size != NULL) {
        *out_size = GUIDE_ACTION_PROGRESS_SIZE;
    }
    return GUIDE_ACTION_PLAN_OK;
}

guide_action_plan_result_t guide_action_progress_decode(const uint8_t *payload,
                                                        size_t size,
                                                        guide_action_progress_t *out)
{
    if (payload == NULL || out == NULL) {
        return GUIDE_ACTION_PLAN_BAD_ARGUMENT;
    }
    if (size != GUIDE_ACTION_PROGRESS_SIZE) {
        return GUIDE_ACTION_PLAN_BAD_SIZE;
    }
    if (get_u32(payload + 0u) != GUIDE_ACTION_PROGRESS_MAGIC) {
        return GUIDE_ACTION_PLAN_BAD_MAGIC;
    }
    if (get_u16(payload + 4u) != (uint16_t)GUIDE_ACTION_PROGRESS_VERSION) {
        return GUIDE_ACTION_PLAN_BAD_VERSION;
    }
    if (get_u16(payload + 6u) != (uint16_t)GUIDE_ACTION_PROGRESS_SIZE) {
        return GUIDE_ACTION_PLAN_BAD_SIZE;
    }
    guide_action_progress_t progress;
    memset(&progress, 0, sizeof(progress));
    progress.action_id = get_u32(payload + 8u);
    progress.plan_crc32 = get_u32(payload + 12u);
    progress.generation = get_u32(payload + 16u);
    progress.trials_done = get_u32(payload + 20u);
    progress.deepest_trial = get_u32(payload + 24u);
    progress.reason = get_u32(payload + 28u);
    progress.status = get_u32(payload + 32u);
    progress.passed_mask = get_u32(payload + 36u);
    progress.failed_mask = get_u32(payload + 40u);
    if (!progress_masks_ok(&progress)) {
        return GUIDE_ACTION_PLAN_RANGE;
    }
    *out = progress;
    return GUIDE_ACTION_PLAN_OK;
}

bool guide_action_progress_matches(const guide_action_progress_t *progress,
                                   uint32_t action_id, uint32_t plan_crc32,
                                   uint32_t generation)
{
    return progress != NULL && progress->action_id == action_id &&
           progress->plan_crc32 == plan_crc32 &&
           progress->generation == generation;
}

void guide_action_reconcile(const guide_action_progress_t *progress,
                            bool have_record, uint32_t action_id,
                            uint32_t plan_crc32, uint32_t generation,
                            guide_action_reconcile_result_t *out)
{
    guide_action_reconcile_result_t result;
    memset(&result, 0, sizeof(result));
    if (out == NULL) {
        return;
    }
    if (!have_record || progress == NULL ||
        !guide_action_progress_matches(progress, action_id, plan_crc32,
                                       generation)) {
        /* No trustworthy record: rerun the action from its first trial. The
         * caller keeps the reason so a report can say why it restarted. */
        result.decision = GUIDE_ACTION_RECONCILE_RESTART;
    } else if (progress->status == GUIDE_ACTION_PROGRESS_COMPLETE) {
        result.decision = GUIDE_ACTION_RECONCILE_COMPLETE;
    } else if (progress->status == GUIDE_ACTION_PROGRESS_FAILED ||
               progress->status == GUIDE_ACTION_PROGRESS_STOPPED) {
        result.decision = GUIDE_ACTION_RECONCILE_FAILED;
        result.reason = progress->reason;
        result.resume_trial = progress->deepest_trial;
    } else {
        /* Still running when the ring came back: resume after the deepest trial
         * that was reached, not after the number of passes. */
        result.decision = GUIDE_ACTION_RECONCILE_RESUME;
        result.resume_trial = progress->deepest_trial + 1u;
        result.reason = progress->reason;
    }
    *out = result;
}

/* ------------------------------------------------------------ ladder ------ */

void guide_action_ladder_reset(guide_action_ladder_t *ladder)
{
    if (ladder == NULL) {
        return;
    }
    ladder->action_index = 0u;
    ladder->resume_trial = 0u;
    ladder->reason = 0u;
}

bool guide_action_ladder_is_last(const guide_action_ladder_t *ladder)
{
    return ladder != NULL && ladder->action_index + 1u >= GUIDE_ACTION_LADDER_COUNT;
}

guide_action_ladder_step_t guide_action_ladder_advance(
    guide_action_ladder_t *ladder,
    const guide_action_reconcile_result_t *reconcile,
    guide_action_ladder_t *out)
{
    guide_action_ladder_t next;
    if (ladder == NULL || reconcile == NULL) {
        return GUIDE_ACTION_LADDER_STOPPED;
    }
    next.action_index = ladder->action_index;
    next.resume_trial = 0u;
    next.reason = reconcile->reason;

    switch (reconcile->decision) {
    case GUIDE_ACTION_RECONCILE_COMPLETE:
        /* This action is finished; the report reason stays clean. */
        next.reason = 0u;
        if (ladder->action_index + 1u >= GUIDE_ACTION_LADDER_COUNT) {
            next.action_index = ladder->action_index;
            if (out != NULL) {
                *out = next;
            }
            return GUIDE_ACTION_LADDER_DONE;
        }
        next.action_index = ladder->action_index + 1u;
        if (out != NULL) {
            *out = next;
        }
        return GUIDE_ACTION_LADDER_NEXT;

    case GUIDE_ACTION_RECONCILE_RESUME:
    case GUIDE_ACTION_RECONCILE_RESTART:
        /* Stay on the same action; RESUME keeps the deepest trial reached. */
        next.resume_trial = reconcile->resume_trial;
        if (out != NULL) {
            *out = next;
        }
        return GUIDE_ACTION_LADDER_RUN_SAME;

    case GUIDE_ACTION_RECONCILE_FAILED:
    default:
        next.reason = reconcile->reason != 0u ? reconcile->reason : 1u;
        if (out != NULL) {
            *out = next;
        }
        return GUIDE_ACTION_LADDER_STOPPED;
    }
}

/* ------------------------------------------------------------ commit ------ */

guide_action_commit_t guide_action_commit_decision(bool session_closed,
                                                    bool already_committed)
{
    if (!session_closed) {
        return GUIDE_ACTION_COMMIT_DENY_SESSION_OPEN;
    }
    if (already_committed) {
        return GUIDE_ACTION_COMMIT_SKIP_ALREADY_DONE;
    }
    return GUIDE_ACTION_COMMIT_ALLOW;
}