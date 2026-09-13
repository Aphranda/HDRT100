#include "tdma_origin_build_job.h"

uint32_t tdma_origin_build_job_state(const tdma_origin_build_job_t *job)
{
    return job == NULL ? TDMA_ORIGIN_JOB_IDLE :
        __atomic_load_n(&job->state, __ATOMIC_ACQUIRE);
}

bool tdma_origin_build_job_request(tdma_origin_build_job_t *job,
                                 tdma_origin_plan_builder_t *builder)
{
    if (job == NULL || builder == NULL ||
        tdma_origin_build_job_state(job) != TDMA_ORIGIN_JOB_IDLE ||
        !builder->active || builder->complete || builder->failed)
        return false;
    job->builder = builder;
    __atomic_store_n(&job->state, TDMA_ORIGIN_JOB_REQUESTED, __ATOMIC_RELEASE);
    return true;
}

bool tdma_origin_build_job_core0_claim(tdma_origin_build_job_t *job)
{
    if (job == NULL) return false;
    uint32_t expected = TDMA_ORIGIN_JOB_REQUESTED;
    return __atomic_compare_exchange_n(&job->state, &expected, TDMA_ORIGIN_JOB_BUILDING,
        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

void tdma_origin_build_job_core0_build_claimed(tdma_origin_build_job_t *job)
{
    if (job == NULL) return;
    const uint32_t state = tdma_origin_build_job_state(job);
    if (state != TDMA_ORIGIN_JOB_BUILDING && state != TDMA_ORIGIN_JOB_CANCELLED) return;
    tdma_origin_build_result_t result = TDMA_ORIGIN_BUILD_BUSY;
    /* Two passes over the fixed label catalog. Every step retains its
     * descriptor bound. No MMIO, wire poll, delay or Core1 phase is needed. */
    for (uint32_t step = 0u; step < 2u * TDMA_ORIGIN_BUILD_LABEL_CAPACITY &&
         tdma_origin_build_job_state(job) == TDMA_ORIGIN_JOB_BUILDING &&
         result == TDMA_ORIGIN_BUILD_BUSY; ++step) {
        result = tdma_origin_plan_step(job->builder);
    }
    if (result != TDMA_ORIGIN_BUILD_DONE) tdma_origin_plan_cancel(job->builder);
    uint32_t expected = TDMA_ORIGIN_JOB_BUILDING;
    const uint32_t completed = result == TDMA_ORIGIN_BUILD_DONE ?
        TDMA_ORIGIN_JOB_READY : TDMA_ORIGIN_JOB_FAILED;
    if (!__atomic_compare_exchange_n(&job->state, &expected, completed,
            false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        /* Cancellation can race the last block or its publication. The
         * cancelled worker makes its final write before acknowledging IDLE. */
        tdma_origin_plan_cancel(job->builder);
        __atomic_store_n(&job->state, TDMA_ORIGIN_JOB_IDLE, __ATOMIC_RELEASE);
    }
}

void tdma_origin_build_job_core0_service(tdma_origin_build_job_t *job)
{
    if (tdma_origin_build_job_core0_claim(job))
        tdma_origin_build_job_core0_build_claimed(job);
}

tdma_origin_build_result_t tdma_origin_build_job_take(tdma_origin_build_job_t *job)
{
    if (job == NULL) return TDMA_ORIGIN_BUILD_FAILED;
    uint32_t state = tdma_origin_build_job_state(job);
    if (state == TDMA_ORIGIN_JOB_REQUESTED || state == TDMA_ORIGIN_JOB_BUILDING)
        return TDMA_ORIGIN_BUILD_BUSY;
    if (state == TDMA_ORIGIN_JOB_READY &&
        __atomic_compare_exchange_n(&job->state, &state, TDMA_ORIGIN_JOB_IDLE,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return TDMA_ORIGIN_BUILD_DONE;
    return TDMA_ORIGIN_BUILD_FAILED;
}

bool tdma_origin_build_job_cancel(tdma_origin_build_job_t *job)
{
    if (job == NULL) return true;
    const uint32_t state = __atomic_exchange_n(&job->state, TDMA_ORIGIN_JOB_CANCELLED,
                                               __ATOMIC_ACQ_REL);
    if (state == TDMA_ORIGIN_JOB_BUILDING || state == TDMA_ORIGIN_JOB_CANCELLED) return false;
    if (state != TDMA_ORIGIN_JOB_IDLE) tdma_origin_plan_cancel(job->builder);
    __atomic_store_n(&job->state, TDMA_ORIGIN_JOB_IDLE, __ATOMIC_RELEASE);
    return true;
}
