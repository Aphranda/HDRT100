/* Guide action plan payload: the C segment of one CTRQS round.
 *
 * The plan travels as the payload of a REFMEM_CMD (payload_kind/payload_ref/
 * payload_crc32 point at a refmem_param_pool slot); this module only defines and
 * validates the bytes, it does not store them and does not compute the slot CRC
 * (refmem_param_pool_crc owns that). One plan per action, fixed 64-byte layout.
 */
#ifndef GUIDE_ACTION_PLAN_H
#define GUIDE_ACTION_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GUIDE_ACTION_PLAN_MAGIC 0x47504C4Eu /* "GPLN" */
#define GUIDE_ACTION_PLAN_VERSION 1u
#define GUIDE_ACTION_PLAN_SIZE 64u
#define GUIDE_ACTION_PLAN_MAX_CANDIDATES 5u
#define GUIDE_ACTION_PLAN_MAX_TRIALS 64u

typedef enum {
    GUIDE_ACTION_PLAN_OK = 0u,
    GUIDE_ACTION_PLAN_BAD_ARGUMENT = 1u,
    GUIDE_ACTION_PLAN_BAD_MAGIC = 2u,
    GUIDE_ACTION_PLAN_BAD_VERSION = 3u,
    GUIDE_ACTION_PLAN_BAD_SIZE = 4u,
    GUIDE_ACTION_PLAN_RANGE = 5u,
} guide_action_plan_result_t;

typedef struct {
    uint32_t action_id;
    /* Identity of the candidate/threshold content; the caller derives it with
     * refmem_param_pool_crc so the same bytes always map to the same plan. */
    uint32_t plan_crc32;
    uint32_t generation;
    uint32_t profile_level;
    uint32_t window_ms;
    uint32_t trial_count;
    uint32_t candidate_count;
    uint32_t candidates[GUIDE_ACTION_PLAN_MAX_CANDIDATES];
    uint32_t threshold_low;
    uint32_t threshold_high;
} guide_action_plan_t;

/* Encodes into a buffer of at least GUIDE_ACTION_PLAN_SIZE bytes. Rejects
 * out-of-range candidate/trial counts instead of truncating. */
guide_action_plan_result_t guide_action_plan_encode(const guide_action_plan_t *plan,
                                                    uint8_t *out,
                                                    size_t capacity,
                                                    size_t *out_size);

/* Decodes and validates. A malformed or out-of-range payload is rejected as a
 * whole; partial plans are never handed to the executor. */
guide_action_plan_result_t guide_action_plan_decode(const uint8_t *payload,
                                                    size_t size,
                                                    guide_action_plan_t *out);

#ifdef __cplusplus
}
#endif

#endif /* GUIDE_ACTION_PLAN_H */
