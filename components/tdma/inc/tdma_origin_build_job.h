#ifndef TDMA_ORIGIN_BUILD_JOB_H
#define TDMA_ORIGIN_BUILD_JOB_H

#include "tdma_origin_plan.h"

typedef enum {
    TDMA_ORIGIN_JOB_IDLE = 0u,
    TDMA_ORIGIN_JOB_REQUESTED,
    TDMA_ORIGIN_JOB_BUILDING,
    TDMA_ORIGIN_JOB_READY,
    TDMA_ORIGIN_JOB_FAILED,
    TDMA_ORIGIN_JOB_CANCELLED,
} tdma_origin_build_job_state_t;

typedef enum {
    TDMA_ORIGIN_BUILD_PROBE_OFF = 0u,
    TDMA_ORIGIN_BUILD_PROBE_ARMED,
    TDMA_ORIGIN_BUILD_PROBE_PAUSED,
    TDMA_ORIGIN_BUILD_PROBE_RETIRED,
} tdma_origin_build_probe_state_t;

/* Diagnostic facts only. Read after ring STOP/ACK and worker IDLE. PAUSED
 * is a live worker lease; RETIRED is historical and can never resume it. */
typedef struct {
    uint32_t state, trial_epoch, config_seq, emitted_runs;
    uint32_t deferred_cancels, entries_cleared;
} tdma_origin_build_probe_t;

/* One owner-authorized, unpublished graph. Core1 initializes and lends the
 * builder and its output storage; Core0 only executes the pure C builder.
 * Neither side may recycle that storage until take/cancel acknowledges it. */
typedef struct {
    uint32_t state;
    tdma_origin_plan_builder_t *builder;
    tdma_origin_build_probe_t probe;
} tdma_origin_build_job_t;

uint32_t tdma_origin_build_job_state(const tdma_origin_build_job_t *job);
bool tdma_origin_build_job_request(tdma_origin_build_job_t *job,
                                 tdma_origin_plan_builder_t *builder);
/* Core1 only; a nonzero finite trial identity enables one yield after real
 * descriptor emission. The owner must cancel through the common STOP path. */
bool tdma_origin_build_job_request_probe(tdma_origin_build_job_t *job,
    tdma_origin_plan_builder_t *builder, uint32_t trial_epoch, uint32_t config_seq);
bool tdma_origin_build_job_probe_paused(const tdma_origin_build_job_t *job);
bool tdma_origin_build_job_get_stopped_probe(const tdma_origin_build_job_t *job,
    tdma_origin_build_probe_t *out);
bool tdma_origin_build_job_core0_claim(tdma_origin_build_job_t *job);
void tdma_origin_build_job_core0_build_claimed(tdma_origin_build_job_t *job);
void tdma_origin_build_job_core0_service(tdma_origin_build_job_t *job);
/* One attempt, no wait. DONE transfers the complete graph back to Core1. */
tdma_origin_build_result_t tdma_origin_build_job_take(tdma_origin_build_job_t *job);
/* false retains the lease until the active writer publishes cancellation. */
bool tdma_origin_build_job_cancel(tdma_origin_build_job_t *job);

#endif
