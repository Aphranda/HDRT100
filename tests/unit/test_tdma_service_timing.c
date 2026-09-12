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
    ticks += 20;
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
    assert(after.reset_generation != reset);

    ticks += 1;
    tdma_service_timing_phase_begin();
    ticks += 120;
    tdma_service_timing_record(TDMA_TIMING_RX_PARSE, ticks - 120);
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.reset_generation == reset && after.phase_count == 1);
    assert(after.last.calls[TDMA_TIMING_RX_CAPTURE] == 0);
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
    puts("PASS: timing wrap, inclusive stages, coherent peak, deferred reset, interrupted writer and invalid clock");
    return 0;
}
