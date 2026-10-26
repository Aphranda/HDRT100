#include "refmem_function.h"

#include <stddef.h>
#include <string.h>

#include "distributed_refmem.h"
#include "osal.h"
#include "refmem_command.h"

typedef struct {
    refmem_function_state_t state;
    const refmem_function_entry_t *entry;
    uint32_t function_seq;
    uint32_t target_mask;
    uint32_t required_mask;
    uint32_t arg_words;
    uint32_t args[REFMEM_FUNCTION_ARG_MAX];
    uint32_t step_index;
    refmem_function_step_state_t step_state;
    uint32_t step_command_seq;
    uint32_t step_ack_mask;
    uint32_t step_nack_mask;
    uint32_t last_reason;
    uint32_t started_ms;
    uint32_t step_started_ms;
    uint32_t step_ready_ms;
    uint8_t step_retries;
    uint8_t had_failure;
} refmem_function_job_t;

static refmem_function_job_t s_jobs[REFMEM_FUNCTION_JOB_MAX];
static uint32_t s_function_seq;
static refmem_function_submit_fn s_submit;
static refmem_function_poll_fn s_poll;

static bool default_submit(uint16_t command_id, uint32_t target_mask,
                           uint32_t required_mask, const uint32_t *params,
                           uint16_t param_words, uint32_t *command_seq)
{
    return distributed_refmem_control_submit(command_id, target_mask,
                                             required_mask, params,
                                             param_words, command_seq);
}

static bool default_poll(uint32_t command_seq, uint32_t *state,
                         uint32_t *last_reason, uint32_t *ack_mask,
                         uint32_t *nack_mask)
{
    refmem_command_snapshot_t snapshot;
    if (!distributed_refmem_node_control_read(
            distributed_refmem_local_slot(), &snapshot, NULL, NULL) ||
        snapshot.command_seq != command_seq) {
        return false;
    }
    *state = snapshot.state;
    *last_reason = snapshot.last_reason;
    *ack_mask = snapshot.ack_flags;
    *nack_mask = snapshot.nack_flags;
    return true;
}

void refmem_function_init(void)
{
    memset(s_jobs, 0, sizeof(s_jobs));
    s_function_seq = 0u;
    s_submit = default_submit;
    s_poll = default_poll;
}

void refmem_function_set_hooks(refmem_function_submit_fn submit,
                               refmem_function_poll_fn poll)
{
    s_submit = submit != NULL ? submit : default_submit;
    s_poll = poll != NULL ? poll : default_poll;
}

static refmem_function_job_t *find_reusable_job(void)
{
    for (uint32_t i = 0u; i < REFMEM_FUNCTION_JOB_MAX; ++i) {
        if (s_jobs[i].state == REFMEM_FUNCTION_IDLE) {
            return &s_jobs[i];
        }
    }
    /* Keep at least one terminal snapshot readable: overwrite the first
     * non-running job. */
    for (uint32_t i = 0u; i < REFMEM_FUNCTION_JOB_MAX; ++i) {
        if (s_jobs[i].state != REFMEM_FUNCTION_RUNNING) {
            return &s_jobs[i];
        }
    }
    return NULL;
}

bool refmem_function_run(uint16_t function_id, uint32_t target_mask,
                         uint32_t required_mask, const uint32_t *args,
                         uint32_t arg_words, uint32_t *function_seq)
{
    const refmem_function_entry_t *entry =
        refmem_function_find_by_id(function_id);
    if (entry == NULL || function_seq == NULL || target_mask == 0u ||
        (required_mask & ~target_mask) != 0u ||
        arg_words != entry->arg_words ||
        (arg_words != 0u && args == NULL) ||
        refmem_function_active_count() != 0u) {
        return false;
    }
    refmem_function_job_t *job = find_reusable_job();
    if (job == NULL) {
        return false;
    }
    memset(job, 0, sizeof(*job));
    if (s_function_seq == 0u) {
        s_function_seq = osal_tick_ms();
        if (s_function_seq == 0u) {
            s_function_seq = 1u;
        }
    }
    ++s_function_seq;
    job->state = REFMEM_FUNCTION_RUNNING;
    job->entry = entry;
    job->function_seq = s_function_seq;
    job->target_mask = target_mask;
    job->required_mask = required_mask;
    job->arg_words = arg_words;
    if (arg_words != 0u) {
        memcpy(job->args, args, arg_words * sizeof(uint32_t));
    }
    job->step_index = 0u;
    job->step_state = REFMEM_FUNCTION_STEP_READY;
    job->started_ms = osal_tick_ms();
    job->step_ready_ms = job->started_ms;
    job->step_retries = 0u;
    *function_seq = job->function_seq;
    return true;
}

static void finish_job(refmem_function_job_t *job)
{
    if (job->step_index >= job->entry->step_count) {
        job->state = job->had_failure ? REFMEM_FUNCTION_PARTIAL
                                      : REFMEM_FUNCTION_COMPLETED;
    }
}

static void fail_job(refmem_function_job_t *job, uint32_t reason)
{
    job->last_reason = reason;
    job->step_state = REFMEM_FUNCTION_STEP_NACKED;
    job->had_failure = 1u;
    if (job->entry->failure_policy ==
        (uint8_t)REFMEM_FUNCTION_ABORT_REMAINING) {
        job->state = REFMEM_FUNCTION_FAILED;
        return;
    }
    ++job->step_index;
    if (job->step_index >= job->entry->step_count) {
        job->state = REFMEM_FUNCTION_PARTIAL;
    } else {
        job->step_state = REFMEM_FUNCTION_STEP_READY;
        job->step_ready_ms = osal_tick_ms() + REFMEM_FUNCTION_STEP_GAP_MS;
    }
}

static void advance_job(refmem_function_job_t *job)
{
    ++job->step_index;
    job->step_retries = 0u;
    if (job->step_index >= job->entry->step_count) {
        finish_job(job);
    } else {
        job->step_state = REFMEM_FUNCTION_STEP_READY;
        job->step_ready_ms = osal_tick_ms() + REFMEM_FUNCTION_STEP_GAP_MS;
    }
}

void refmem_function_service(void)
{
    const uint32_t now = osal_tick_ms();
    for (uint32_t i = 0u; i < REFMEM_FUNCTION_JOB_MAX; ++i) {
        refmem_function_job_t *job = &s_jobs[i];
        if (job->state != REFMEM_FUNCTION_RUNNING || job->entry == NULL) {
            continue;
        }
        if (job->step_state == REFMEM_FUNCTION_STEP_READY) {
            if (job->step_index >= job->entry->step_count) {
                finish_job(job);
                continue;
            }
            if ((int32_t)(now - job->step_ready_ms) < 0) {
                continue;
            }
            const refmem_function_step_t *step =
                &job->entry->steps[job->step_index];
            const uint32_t *params = step->arg_words != 0u
                ? &job->args[step->arg_base] : NULL;
            uint32_t command_seq = 0u;
            if (!s_submit(step->command_id, job->target_mask,
                          job->required_mask, params, step->arg_words,
                          &command_seq)) {
                fail_job(job, (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
                continue;
            }
            job->step_state = REFMEM_FUNCTION_STEP_SUBMITTED;
            job->step_command_seq = command_seq;
            job->step_started_ms = now;
            continue;
        }
        if (job->step_state != REFMEM_FUNCTION_STEP_SUBMITTED) {
            continue;
        }
        uint32_t state = 0u;
        uint32_t reason = 0u;
        uint32_t ack = 0u;
        uint32_t nack = 0u;
        if (s_poll(job->step_command_seq, &state, &reason, &ack, &nack)) {
            if (state == (uint32_t)REFMEM_COMMAND_STATE_ACKED) {
                job->step_ack_mask |= ack;
                job->step_state = REFMEM_FUNCTION_STEP_ACKED;
                advance_job(job);
                continue;
            }
            if (state == (uint32_t)REFMEM_COMMAND_STATE_NACKED ||
                state == (uint32_t)REFMEM_COMMAND_STATE_TIMED_OUT) {
                const uint32_t terminal_reason = reason != 0u
                    ? reason
                    : (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
                if (terminal_reason ==
                        (uint32_t)REFMEM_COMMAND_REASON_RUN_STATE_DENIED &&
                    job->step_retries < REFMEM_FUNCTION_STEP_RETRY_MAX) {
                    ++job->step_retries;
                    job->step_state = REFMEM_FUNCTION_STEP_READY;
                    job->step_ready_ms =
                        now + REFMEM_FUNCTION_STEP_RETRY_GAP_MS;
                    continue;
                }
                job->step_nack_mask |= nack;
                fail_job(job, terminal_reason);
                continue;
            }
        }
        if ((int32_t)(now - job->step_started_ms) >
            (int32_t)REFMEM_FUNCTION_STEP_TIMEOUT_MS) {
            fail_job(job, (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);
        }
    }
}

bool refmem_function_get_status(uint32_t function_seq,
                                refmem_function_snapshot_t *out)
{
    if (out == NULL || function_seq == 0u) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_FUNCTION_JOB_MAX; ++i) {
        const refmem_function_job_t *job = &s_jobs[i];
        if (job->state == REFMEM_FUNCTION_IDLE ||
            job->function_seq != function_seq) {
            continue;
        }
        memset(out, 0, sizeof(*out));
        out->function_seq = job->function_seq;
        out->function_id = job->entry != NULL ? job->entry->id : 0u;
        out->state = (uint32_t)job->state;
        out->target_mask = job->target_mask;
        out->required_mask = job->required_mask;
        out->arg_words = job->arg_words;
        out->step_count = job->entry != NULL ? job->entry->step_count : 0u;
        out->step_index = job->step_index;
        out->step_command_id =
            (job->entry != NULL && job->step_index < job->entry->step_count)
            ? job->entry->steps[job->step_index].command_id : 0u;
        out->step_command_seq = job->step_command_seq;
        out->step_state = (uint32_t)job->step_state;
        out->step_ack_mask = job->step_ack_mask;
        out->step_nack_mask = job->step_nack_mask;
        out->last_reason = job->last_reason;
        out->started_ms = job->started_ms;
        out->step_started_ms = job->step_started_ms;
        return true;
    }
    return false;
}

uint32_t refmem_function_active_count(void)
{
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < REFMEM_FUNCTION_JOB_MAX; ++i) {
        if (s_jobs[i].state == REFMEM_FUNCTION_RUNNING) {
            ++count;
        }
    }
    return count;
}

