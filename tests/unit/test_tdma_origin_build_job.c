#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_build_job.h"

static tdma_origin_build_job_t job;
static tdma_origin_plan_builder_t builder;
static uint32_t steps, finish_at, cancel_at, fail_at, writes;

/* Controlled build boundary: cancellation may interrupt an individual
 * output write, so a false cancellation ACK must keep the output leased. */
tdma_origin_build_result_t tdma_origin_plan_step(tdma_origin_plan_builder_t *b)
{
    assert(b == &builder && b->active);
    ++steps;
    if (steps == cancel_at) {
        assert(!tdma_origin_build_job_cancel(&job));
        assert(!tdma_origin_build_job_request(&job, b));
        assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_FAILED);
    }
    ++writes;
    if (steps == fail_at) return TDMA_ORIGIN_BUILD_FAILED;
    if (steps == finish_at) {
        b->complete = true;
        return TDMA_ORIGIN_BUILD_DONE;
    }
    return TDMA_ORIGIN_BUILD_BUSY;
}

void tdma_origin_plan_cancel(tdma_origin_plan_builder_t *b)
{
    assert(b == &builder);
    b->active = b->complete = false;
    b->failed = true;
}

static void reset(void)
{
    assert(tdma_origin_build_job_cancel(&job));
    memset(&builder, 0, sizeof(builder));
    builder.active = true;
    steps = cancel_at = fail_at = writes = 0u;
    finish_at = 3u;
}

int main(void)
{
    assert(tdma_origin_build_job_cancel(NULL));
    assert(!tdma_origin_build_job_request(NULL, &builder));
    assert(!tdma_origin_build_job_request(&job, NULL));
    assert(!tdma_origin_build_job_core0_claim(NULL));
    assert(tdma_origin_build_job_take(NULL) == TDMA_ORIGIN_BUILD_FAILED);
    reset();
    assert(tdma_origin_build_job_request(&job, &builder));
    for (unsigned i = 0; i < 100; ++i) {
        assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_BUSY);
        assert(writes == 0u); /* Core1 polling never constructs the graph. */
    }
    assert(!tdma_origin_build_job_request(&job, &builder));
    tdma_origin_build_job_core0_service(&job);
    assert(steps == 3u && writes == 3u && builder.complete);
    assert(tdma_origin_build_job_state(&job) == TDMA_ORIGIN_JOB_READY);
    assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_DONE);
    memset(&builder, 0xa5, sizeof(builder)); /* Owner can reuse after take. */
    assert(tdma_origin_build_job_cancel(&job));
    assert(writes == 3u);

    reset();
    assert(tdma_origin_build_job_request(&job, &builder));
    assert(tdma_origin_build_job_cancel(&job));
    tdma_origin_build_job_core0_service(&job);
    assert(writes == 0u && !builder.active);

    reset();
    assert(tdma_origin_build_job_request(&job, &builder));
    assert(tdma_origin_build_job_core0_claim(&job));
    assert(!tdma_origin_build_job_cancel(&job));
    assert(!tdma_origin_build_job_cancel(&job));
    assert(!tdma_origin_build_job_request(&job, &builder));
    tdma_origin_build_job_core0_build_claimed(&job);
    assert(tdma_origin_build_job_cancel(&job));
    assert(writes == 0u);

    for (uint32_t at = 1; at <= 3; ++at) {
        reset();
        cancel_at = at;
        assert(tdma_origin_build_job_request(&job, &builder));
        tdma_origin_build_job_core0_service(&job);
        assert(writes == at && !builder.complete);
        assert(tdma_origin_build_job_state(&job) == TDMA_ORIGIN_JOB_IDLE);
        assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_FAILED);
    }
    reset();
    fail_at = 2u;
    assert(tdma_origin_build_job_request(&job, &builder));
    tdma_origin_build_job_core0_service(&job);
    assert(writes == 2u && builder.failed);
    assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_FAILED);
    assert(tdma_origin_build_job_cancel(&job));

    reset();
    finish_at = UINT32_MAX;
    assert(tdma_origin_build_job_request(&job, &builder));
    tdma_origin_build_job_core0_service(&job);
    assert(writes == 2u * TDMA_ORIGIN_BUILD_LABEL_CAPACITY && builder.failed);
    assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_FAILED);
    assert(tdma_origin_build_job_cancel(&job));

    reset();
    assert(tdma_origin_build_job_request(&job, &builder));
    tdma_origin_build_job_core0_service(&job);
    assert(tdma_origin_build_job_cancel(&job)); /* Cancel a completed, untaken plan. */
    assert(!builder.complete && tdma_origin_build_job_state(&job) == TDMA_ORIGIN_JOB_IDLE);
    puts("origin build job: publication, cancellation, reuse and bounded failure passed");
    return 0;
}
