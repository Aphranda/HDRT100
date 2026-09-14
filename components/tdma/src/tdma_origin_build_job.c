#include "tdma_origin_build_job.h"

uint32_t tdma_origin_build_job_state(const tdma_origin_build_job_t *job)
{
    return job == NULL ? TDMA_ORIGIN_JOB_IDLE :
        __atomic_load_n(&job->state, __ATOMIC_ACQUIRE);
}

bool tdma_origin_build_job_request(tdma_origin_build_job_t *job,
                                 tdma_origin_plan_builder_t *builder)
{
    return tdma_origin_build_job_request_probe(job, builder, 0u, 0u);
}

bool tdma_origin_build_job_request_probe(tdma_origin_build_job_t *job,
    tdma_origin_plan_builder_t *builder, uint32_t trial_epoch, uint32_t config_seq)
{
    if (job == NULL || builder == NULL ||
        tdma_origin_build_job_state(job) != TDMA_ORIGIN_JOB_IDLE ||
        !builder->active || builder->complete || builder->failed)
        return false;
    job->builder = builder;
    job->probe = (tdma_origin_build_probe_t){.trial_epoch = trial_epoch,
        .config_seq = config_seq, .state = trial_epoch == 0u ?
            TDMA_ORIGIN_BUILD_PROBE_OFF : TDMA_ORIGIN_BUILD_PROBE_ARMED};
    __atomic_store_n(&job->state, TDMA_ORIGIN_JOB_REQUESTED, __ATOMIC_RELEASE);
    return true;
}

bool tdma_origin_build_job_probe_paused(const tdma_origin_build_job_t *job)
{
    return job != NULL && __atomic_load_n(&job->probe.state, __ATOMIC_ACQUIRE) ==
        TDMA_ORIGIN_BUILD_PROBE_PAUSED;
}

bool tdma_origin_build_job_get_stopped_probe(const tdma_origin_build_job_t *job,
    tdma_origin_build_probe_t *out)
{
    if (job == NULL || out == NULL || tdma_origin_build_job_state(job) != TDMA_ORIGIN_JOB_IDLE ||
        job->probe.trial_epoch == 0u) return false;
    *out = job->probe;
    return true;
}

/* Called by the current writer before releasing its lease. Never inspect
 * the shared persona workspace from a later stopped diagnostic query. */
static void tdma_origin_build_job_probe_retire(tdma_origin_build_job_t *job)
{
    if (job->probe.trial_epoch == 0u) return;
    const tdma_origin_plan_t *p = job->builder->p;
    bool cleared = p != NULL && p->seed_entry == 0u && p->boundary_entry == 0u &&
        p->fault_entry == 0u && p->record_entry == 0u;
    if (p != NULL) for (uint32_t bank = 0u; bank < TDMA_ORIGIN_PLAN_BANK_COUNT; ++bank)
        cleared = cleared && p->local_entry[bank] == 0u;
    job->probe.entries_cleared = cleared && !job->builder->active &&
        !job->builder->complete && job->builder->failed;
    /* Clear the live PAUSED marker BEFORE IDLE permits union reuse. */
    __atomic_store_n(&job->probe.state, TDMA_ORIGIN_BUILD_PROBE_RETIRED, __ATOMIC_RELEASE);
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
    if (tdma_origin_build_job_probe_paused(job) && state != TDMA_ORIGIN_JOB_CANCELLED) return;
    tdma_origin_build_result_t result = TDMA_ORIGIN_BUILD_BUSY;
    /* Two passes over the fixed label catalog. Every step retains its
     * descriptor bound. No MMIO, wire poll, delay or Core1 phase is needed. */
    for (uint32_t step = 0u; step < 2u * TDMA_ORIGIN_BUILD_LABEL_CAPACITY &&
         tdma_origin_build_job_state(job) == TDMA_ORIGIN_JOB_BUILDING &&
         result == TDMA_ORIGIN_BUILD_BUSY; ++step) {
        result = tdma_origin_plan_step(job->builder);
        if (result == TDMA_ORIGIN_BUILD_BUSY &&
            job->probe.state == TDMA_ORIGIN_BUILD_PROBE_ARMED &&
            job->builder->emitting && job->builder->p->run_count != 0u) {
            job->probe.emitted_runs = job->builder->p->run_count;
            __atomic_store_n(&job->probe.state, TDMA_ORIGIN_BUILD_PROBE_PAUSED, __ATOMIC_RELEASE);
            return; /* No wait: Core0 remains available to finish cancellation. */
        }
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
        tdma_origin_build_job_probe_retire(job);
        __atomic_store_n(&job->state, TDMA_ORIGIN_JOB_IDLE, __ATOMIC_RELEASE);
    }
}

void tdma_origin_build_job_core0_service(tdma_origin_build_job_t *job)
{
    /* CANCELLED alone is insufficient: Core1 can transiently exchange an
     * already IDLE job to CANCELLED. Only a live paused writer may resume. */
    if (tdma_origin_build_job_probe_paused(job)) {
        if (tdma_origin_build_job_state(job) == TDMA_ORIGIN_JOB_CANCELLED)
            tdma_origin_build_job_core0_build_claimed(job);
        return;
    }
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
    if (state == TDMA_ORIGIN_JOB_BUILDING || state == TDMA_ORIGIN_JOB_CANCELLED) {
        if (job->probe.trial_epoch != 0u && job->probe.deferred_cancels != UINT32_MAX)
            ++job->probe.deferred_cancels;
        return false;
    }
    if (state != TDMA_ORIGIN_JOB_IDLE) {
        tdma_origin_plan_cancel(job->builder);
        tdma_origin_build_job_probe_retire(job);
    }
    __atomic_store_n(&job->state, TDMA_ORIGIN_JOB_IDLE, __ATOMIC_RELEASE);
    return true;
}
