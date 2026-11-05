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
