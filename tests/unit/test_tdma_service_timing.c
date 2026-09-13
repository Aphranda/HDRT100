#include <assert.h>
#include <stdio.h>
#include "tdma_service_timing.h"

static uint64_t ticks;
uint64_t vdc_timestamp_clock_read_ticks64(void) { return ticks; }
uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }

/* Include the real recorder to inject a suspended publication deterministically. */
#include "../../components/tdma/src/tdma_service_timing.c"

int main(void)
{
    tdma_service_timing_snapshot_t before, after;
    assert(!tdma_service_timing_try_snapshot(&before));
    assert(!tdma_service_timing_try_snapshot(NULL));
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, 0);
    tdma_service_timing_phase_end();
    assert(!tdma_service_timing_try_snapshot(&before));

    ticks = UINT32_MAX - 10ull;
    tdma_service_timing_phase_begin();
    const uint64_t beginning = ticks;
    ticks += 1;
    tdma_service_timing_record(TDMA_TIMING_RX_DMA_OBSERVE, beginning);
    ticks += 1;
    tdma_service_timing_record(TDMA_TIMING_RX_LOCATE, beginning + 1);
    ticks += 1;
    tdma_service_timing_record(TDMA_TIMING_RX_HEADER_CHECK, beginning + 2);
    ticks += 2;
    tdma_service_timing_record(TDMA_TIMING_RX_RING_COPY, beginning + 3);
    ticks += 2;
    tdma_service_timing_record(TDMA_TIMING_RX_DMA_OBSERVE, beginning + 5);
    tdma_service_timing_record(TDMA_TIMING_RX_ACQUIRE, beginning);
    ticks += 5;
    tdma_service_timing_record(TDMA_TIMING_RX_PACKET_COPY, beginning + 7);
    ticks += 3;
    tdma_service_timing_record(TDMA_TIMING_RX_CLOCK, beginning + 12);
    ticks += 4;
    tdma_service_timing_record(TDMA_TIMING_RX_LATCH, beginning + 15);
    ticks += 1;
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, beginning);
    ticks += 30;
    tdma_service_timing_record(TDMA_TIMING_RX_PARSE, beginning + 20);
    ticks += 10;
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, beginning + 50);
    tdma_service_timing_record(TDMA_TIMING_ADAPTER, beginning);
    ticks += 40;
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&before));
    assert(before.last.total_ticks == 100 && before.last.invalid_count == 0);
    assert(before.version == 5);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_DMA_OBSERVE] == 3);
    assert(before.last.calls[TDMA_TIMING_RX_DMA_OBSERVE] == 2);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_LOCATE] == 1);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_HEADER_CHECK] == 1);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_RING_COPY] == 2);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_ACQUIRE] == 7);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_PACKET_COPY] == 5);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_CLOCK] == 3);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_LATCH] == 4);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_CAPTURE] == 30);
    assert(before.last.calls[TDMA_TIMING_RX_CAPTURE] == 2);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_PARSE] == 30);
    assert(before.last.elapsed_ticks[TDMA_TIMING_ADAPTER] == 60);
    assert(before.last.sequence == before.peak.sequence && before.phase_count == 1);

    /* Peak decomposition belongs to one whole phase, even when a later
     * shorter phase has a slower individual action. */
    ticks += 100;
    tdma_service_timing_phase_begin();
    const uint64_t next = ticks;
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.phase_count == 1 && after.last.total_ticks == 100);
    ticks += 70;
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, next);
    ticks += 10;
    const uint32_t reset = tdma_service_timing_request_reset();
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.phase_count == 2 && after.last.total_ticks == 80);
    assert(after.peak.total_ticks == 100 && after.peak.elapsed_ticks[TDMA_TIMING_RX_CAPTURE] == 30);
    assert(after.last.calls[TDMA_TIMING_RX_ACQUIRE] == 0);
    assert(after.last.calls[TDMA_TIMING_RX_DMA_OBSERVE] == 0);
    assert(after.peak.elapsed_ticks[TDMA_TIMING_RX_DMA_OBSERVE] == 3);
    assert(after.peak.elapsed_ticks[TDMA_TIMING_RX_ACQUIRE] == 7);
    assert(after.reset_generation != reset);

    ticks += 1;
    tdma_service_timing_phase_begin();
    ticks += 120;
    tdma_service_timing_record(TDMA_TIMING_RX_PARSE, ticks - 120);
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.reset_generation == reset && after.phase_count == 1);
    assert(after.last.calls[TDMA_TIMING_RX_CAPTURE] == 0);
    assert(after.last.calls[TDMA_TIMING_RX_DMA_OBSERVE] == 0);
    assert(after.peak.total_ticks == 120 && after.peak.elapsed_ticks[TDMA_TIMING_RX_PARSE] == 120);

    /* A suspended writer never makes a Core0 query spin. */
    s_guard++;
    assert(!tdma_service_timing_try_snapshot(&before));
    s_guard++;
    assert(tdma_service_timing_try_snapshot(&before));

    /* Lost clock epoch, excessive intervals, and invalid stages must not
     * become a plausible small WCET or replace the valid peak. */
    tdma_service_timing_phase_begin();
    ticks -= 1;
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.last.invalid_count != 0 && after.last.total_ticks == UINT32_MAX);
    assert(after.peak.total_ticks == 120);
    tdma_service_timing_phase_begin();
    ticks += (uint64_t)UINT32_MAX + 1;
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, s_work.start_ticks);
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, ticks - 2);
    tdma_service_timing_record(TDMA_TIMING_STAGE_COUNT, ticks);
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.last.invalid_count >= 3 && after.peak.total_ticks == 120);
    assert(after.last.elapsed_ticks[TDMA_TIMING_RX_CAPTURE] == UINT32_MAX);
    /* New owner/adapter intervals stay in one complete peak, including a
     * handoff which does more work than its physical capture child. */
    tdma_service_timing_request_reset();
    tdma_service_timing_phase_begin();
    const uint64_t owner_start = ticks;
    ticks += 1;
    tdma_service_timing_record(TDMA_TIMING_ADAPTER_PROLOGUE, owner_start);
    ticks += 4;
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, owner_start + 1);
    ticks += 2;
    tdma_service_timing_record(TDMA_TIMING_RX_HANDOFF, owner_start + 1);
    ticks += 2;
    tdma_service_timing_record(TDMA_TIMING_ADAPTER_STATUS, owner_start + 7);
    tdma_service_timing_record(TDMA_TIMING_ADAPTER, owner_start);
    ticks += 3;
    tdma_service_timing_record(TDMA_TIMING_RING_PUBLISH, owner_start + 9);
    tdma_service_timing_record(TDMA_TIMING_RING_RUNTIME, owner_start);
    ticks += 4;
    tdma_service_timing_record(TDMA_TIMING_INTENT_DISPATCH, owner_start + 12);
    tdma_service_timing_record(TDMA_TIMING_OWNER_SERVICE, owner_start);
    ticks += 2;
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&before));
    assert(before.last.total_ticks == 18 && before.last.invalid_count == 0);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RING_RUNTIME] == 12);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RING_PUBLISH] == 3);
    assert(before.last.elapsed_ticks[TDMA_TIMING_INTENT_DISPATCH] == 4);
    assert(before.last.elapsed_ticks[TDMA_TIMING_ADAPTER_PROLOGUE] == 1);
    assert(before.last.elapsed_ticks[TDMA_TIMING_RX_HANDOFF] == 6);
    assert(before.last.elapsed_ticks[TDMA_TIMING_ADAPTER_STATUS] == 2);
    for (unsigned stage = TDMA_TIMING_RING_RUNTIME; stage <= TDMA_TIMING_ADAPTER_STATUS; ++stage)
        assert(before.last.calls[stage] == 1);
    tdma_service_timing_phase_begin();
    ticks += 1;
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.peak.total_ticks == 18 && after.peak.elapsed_ticks[TDMA_TIMING_RX_HANDOFF] == 6);
    for (unsigned stage = TDMA_TIMING_RING_RUNTIME; stage < TDMA_TIMING_STAGE_COUNT; ++stage)
        assert(after.last.calls[stage] == 0);
    /* Accepting a prepared RX includes several owner actions. Preserve all
     * children of the same longest phase, even if a shorter phase later
     * spends more time on one child. Publication and freshness commit are
     * measured separately, and absent work must stay absent after RESET. */
    tdma_service_timing_request_reset();
    tdma_service_timing_phase_begin();
    const uint64_t accept_start = ticks;
    for (unsigned stage = TDMA_TIMING_RX_INSPECT; stage <= TDMA_TIMING_RX_COMPLETE; ++stage) {
        const uint64_t child_start = ticks;
        ticks += stage - TDMA_TIMING_RX_INSPECT + 1;
        tdma_service_timing_record((tdma_service_timing_stage_t)stage, child_start);
    }
    tdma_service_timing_record(TDMA_TIMING_RX_PARSE, accept_start);
    ticks += 4;
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&before));
    assert(before.peak.total_ticks == 25);
    assert(before.peak.elapsed_ticks[TDMA_TIMING_RX_PARSE] == 21);
    assert(before.peak.elapsed_ticks[TDMA_TIMING_RX_FIFO_PUBLISH] == 4);
    assert(before.peak.elapsed_ticks[TDMA_TIMING_RX_COMMIT] == 5);
    tdma_service_timing_phase_begin();
    const uint64_t short_start = ticks;
    ticks += 10;
    tdma_service_timing_record(TDMA_TIMING_RX_HEALTH, short_start);
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.last.elapsed_ticks[TDMA_TIMING_RX_HEALTH] == 10);
    assert(after.last.calls[TDMA_TIMING_RX_COMMIT] == 0);
    assert(after.peak.total_ticks == 25);
    assert(after.peak.elapsed_ticks[TDMA_TIMING_RX_HEALTH] == 2);
    for (unsigned stage = TDMA_TIMING_RX_INSPECT; stage <= TDMA_TIMING_RX_COMPLETE; ++stage)
        assert(after.peak.calls[stage] == 1);
    tdma_service_timing_request_reset();
    tdma_service_timing_phase_begin();
    ticks++;
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&after));
    for (unsigned stage = TDMA_TIMING_RX_INSPECT; stage <= TDMA_TIMING_RX_COMPLETE; ++stage)
        assert(after.peak.calls[stage] == 0 && after.peak.elapsed_ticks[stage] == 0);
    puts("PASS: timing wrap, inclusive stages, coherent peak, deferred reset, interrupted writer and invalid clock");
    return 0;
}
