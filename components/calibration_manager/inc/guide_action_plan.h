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

/* The plan travels as the 16 inline words of CAL_GUIDE_CONFIG, so the byte
 * layout above must be assembled on the target. Both directions validate the
 * word count and the same ranges as the byte codec. */
#define GUIDE_ACTION_PLAN_WORDS (GUIDE_ACTION_PLAN_SIZE / 4u)

guide_action_plan_result_t guide_action_plan_from_words(const uint32_t *words,
                                                        uint32_t word_count,
                                                        guide_action_plan_t *out);
guide_action_plan_result_t guide_action_plan_to_words(const guide_action_plan_t *plan,
                                                      uint32_t *words,
                                                      uint32_t word_count);

/* ---------------------------------------------------------------- progress --
 * The R segment runs with the ring down, so its result cannot be reported then.
 * The board writes this record into a refmem parameter-pool slot while it runs
 * (the pool is plain storage and needs no link) and Q reads it back after the
 * ring is re-formed. It carries both the progress (which trials completed) and
 * the problem (why the window stopped), which is what "recover progress and
 * problems" means. */
#define GUIDE_ACTION_PROGRESS_MAGIC 0x47505247u /* "GPRG" */
#define GUIDE_ACTION_PROGRESS_VERSION 1u
#define GUIDE_ACTION_PROGRESS_SIZE 48u
#define GUIDE_ACTION_PROGRESS_MAX_TRIALS 32u

typedef enum {
    GUIDE_ACTION_PROGRESS_RUNNING = 0u,
    GUIDE_ACTION_PROGRESS_COMPLETE = 1u,
    GUIDE_ACTION_PROGRESS_STOPPED = 2u,
    GUIDE_ACTION_PROGRESS_FAILED = 3u,
} guide_action_progress_status_t;

typedef struct {
    uint32_t action_id;
    uint32_t plan_crc32;
    uint32_t generation;
    uint32_t trials_done;
    /* Highest trial index reached, so a resumed window continues from here. */
    uint32_t deepest_trial;
    /* 0 while healthy, otherwise why the window stopped. */
    uint32_t reason;
    uint32_t status;
    uint32_t passed_mask;
    uint32_t failed_mask;
} guide_action_progress_t;

/* Encodes/decodes the 48-byte record. Bit masks above MAX_TRIALS are rejected
 * rather than silently truncated. */
guide_action_plan_result_t guide_action_progress_encode(
    const guide_action_progress_t *progress, uint8_t *out, size_t capacity,
    size_t *out_size);
guide_action_plan_result_t guide_action_progress_decode(const uint8_t *payload,
                                                        size_t size,
                                                        guide_action_progress_t *out);

/* True when the record belongs to the given plan (Q must not mix generations). */
bool guide_action_progress_matches(const guide_action_progress_t *progress,
                                   uint32_t action_id, uint32_t plan_crc32,
                                   uint32_t generation);

/* What Q decides after reading the record back. */
typedef enum {
    /* Continue the same action from `resume_trial`. */
    GUIDE_ACTION_RECONCILE_RESUME = 0u,
    /* The action already finished: go to S. */
    GUIDE_ACTION_RECONCILE_COMPLETE = 1u,
    /* Nothing trustworthy was written (foreign plan, empty window): rerun. */
    GUIDE_ACTION_RECONCILE_RESTART = 2u,
    /* The window stopped for a reason that must not be retried blindly. */
    GUIDE_ACTION_RECONCILE_FAILED = 3u,
} guide_action_reconcile_t;

typedef struct {
    guide_action_reconcile_t decision;
    /* First trial index the next window should run (COMPLETE/RESTART: 0). */
    uint32_t resume_trial;
    /* Reason carried through for the caller/report (0 when healthy). */
    uint32_t reason;
} guide_action_reconcile_result_t;

/* Decides from a decoded progress record whether to resume, go on, rerun or
 * stop. A record from another plan/generation is never trusted. */
void guide_action_reconcile(const guide_action_progress_t *progress,
                            bool have_record, uint32_t action_id,
                            uint32_t plan_crc32, uint32_t generation,
                            guide_action_reconcile_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* GUIDE_ACTION_PLAN_H */
