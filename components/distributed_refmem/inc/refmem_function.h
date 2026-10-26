#ifndef REFMEM_FUNCTION_H
#define REFMEM_FUNCTION_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_function_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFMEM_FUNCTION_JOB_MAX 4u
#define REFMEM_FUNCTION_STEP_TIMEOUT_MS 5000u
/* Leave the single command/reply slot time to drain between steps.  The HIL
 * showed a remote step-1 timeout when the next step was submitted on the
 * immediately following tick. */
#define REFMEM_FUNCTION_STEP_GAP_MS 20u
/* A domain STOP window may still be retiring when the next step reaches it
 * (VDC reference retirement is asynchronous).  RUN_STATE_DENIED is retried a
 * bounded number of times instead of failing the whole function at once. */
#define REFMEM_FUNCTION_STEP_RETRY_MAX 3u
#define REFMEM_FUNCTION_STEP_RETRY_GAP_MS 100u

typedef enum {
    REFMEM_FUNCTION_IDLE = 0u,
    REFMEM_FUNCTION_RUNNING = 1u,
    REFMEM_FUNCTION_COMPLETED = 2u,
    REFMEM_FUNCTION_FAILED = 3u,
    REFMEM_FUNCTION_PARTIAL = 4u,
} refmem_function_state_t;

typedef enum {
    REFMEM_FUNCTION_STEP_READY = 0u,
    REFMEM_FUNCTION_STEP_SUBMITTED = 1u,
    REFMEM_FUNCTION_STEP_ACKED = 2u,
    REFMEM_FUNCTION_STEP_NACKED = 3u,
    REFMEM_FUNCTION_STEP_TIMED_OUT = 4u,
} refmem_function_step_state_t;

typedef struct {
    uint32_t function_seq;
    uint32_t function_id;
    uint32_t state;
    uint32_t target_mask;
    uint32_t required_mask;
    uint32_t arg_words;
    uint32_t step_count;
    uint32_t step_index;
    uint32_t step_command_id;
    uint32_t step_command_seq;
    uint32_t step_state;
    uint32_t step_ack_mask;
    uint32_t step_nack_mask;
    uint32_t last_reason;
    uint32_t started_ms;
    uint32_t step_started_ms;
} refmem_function_snapshot_t;

typedef bool (*refmem_function_submit_fn)(
    uint16_t command_id, uint32_t target_mask, uint32_t required_mask,
    const uint32_t *params, uint16_t param_words, uint32_t *command_seq);
typedef bool (*refmem_function_poll_fn)(
    uint32_t command_seq, uint32_t *state, uint32_t *last_reason,
    uint32_t *ack_mask, uint32_t *nack_mask);

void refmem_function_init(void);
/* Host tests replace the transport hooks; NULL restores the defaults. */
void refmem_function_set_hooks(refmem_function_submit_fn submit,
                               refmem_function_poll_fn poll);
bool refmem_function_run(uint16_t function_id, uint32_t target_mask,
                         uint32_t required_mask, const uint32_t *args,
                         uint32_t arg_words, uint32_t *function_seq);
void refmem_function_service(void);
bool refmem_function_get_status(uint32_t function_seq,
                                refmem_function_snapshot_t *out);
uint32_t refmem_function_active_count(void);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_FUNCTION_H */

