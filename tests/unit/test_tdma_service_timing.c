#include <assert.h>
#include <stdio.h>
#include "tdma_service_timing.h"

static uint64_t ticks;
uint64_t vdc_timestamp_clock_read_ticks64(void) { return ticks; }
uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }

/* Include the real recorder to inject a suspended publication deterministically. */
#include "../../components/tdma/src/tdma_service_timing.c"

static void test_rx_aggregate_lifecycle(void)
{
    tdma_rx_timing_snapshot_t rx, previous;
    assert(!tdma_service_timing_rx_try_snapshot(NULL));
    const uint32_t generation = tdma_service_timing_request_reset();
    ticks += 1000u;
    tdma_service_timing_phase_begin();
    const tdma_service_timing_context_t running = {.state=1u,.config_generation=7u};
    tdma_service_timing_context(running, true);
    const uint64_t first = ticks;
    tdma_service_timing_rx_initial(first, 900u);
    tdma_service_timing_rx_station(0u, 0u, UINT64_MAX); /* IDLE has no age. */
    tdma_service_timing_rx_station(1u, UINT64_C(1)<<40u, 10u);
    tdma_service_timing_rx_station(2u, 200u, 100u);
    tdma_service_timing_rx_station(3u, 300u, 100u);
    tdma_service_timing_rx_station(4u, 400u, 100u);
    tdma_service_timing_rx_drop(TDMA_RX_DROP_CLAMP, UINT64_C(1)<<40u);
    tdma_service_timing_rx_drop(TDMA_RX_DROP_STALE_HINT, 0u);
    tdma_service_timing_rx_drop(TDMA_RX_DROP_FRAME_COPY, 0u);
    tdma_service_timing_rx_drop(TDMA_RX_DROP_DISCOVERY_COPY, 0u);
    assert(ticks == first); /* Hooks do not add clock reads. */
    assert(tdma_service_timing_rx_try_snapshot(&rx) && rx.initial_observation_count == 0u);
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_rx_try_snapshot(&rx));
    assert(rx.version == TDMA_RX_TIMING_VERSION && rx.reset_generation == generation);
    assert(rx.initial_observation_count == 1u && rx.initial_gap_count == 0u);
    assert(rx.initial_backlog_max_words == 900u && rx.invalid_count == 0u);
    for (uint32_t i=0; i<TDMA_RX_TIMING_STATION_STATES; ++i) assert(rx.station_polls[i] == 1u);
    assert(rx.station_age_max_ns[0] == (UINT64_C(1)<<40u)-10u);
    assert(rx.clamp_skipped_words == (UINT64_C(1)<<40u));
    previous = rx;
    tdma_service_timing_rx_initial(0u, 0u);
    tdma_service_timing_rx_station(99u, 0u, 1u);
    tdma_service_timing_rx_drop(TDMA_RX_DROP_CAUSE_COUNT, 0u);
    assert(tdma_service_timing_rx_try_snapshot(&rx) && memcmp(&rx,&previous,sizeof(rx)) == 0);

    ticks += UINT64_C(1)<<40u;
    tdma_service_timing_phase_begin();
    tdma_service_timing_context(running, true);
    tdma_service_timing_rx_initial(ticks, 12u);
    tdma_service_timing_rx_drop(TDMA_RX_DROP_EPOCH, 0u);
    ticks += 17u;
    tdma_service_timing_rx_initial(ticks, 10u);
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_rx_try_snapshot(&rx));
    assert(rx.initial_gap_count == 2u && rx.initial_gap_max_ticks == (UINT64_C(1)<<40u));
    assert(rx.drop_count[TDMA_RX_DROP_EPOCH] == 1u);
    /* Re-arm and persona changes break the gap chain without hiding totals. */
    for (unsigned change=0; change<3; ++change) {
        ticks += UINT64_C(1)<<41u;
        tdma_service_timing_phase_begin();
        tdma_service_timing_context_t context = running;
        if (change==0) context.state=0u;
        if (change==1) context.config_generation++;
        if (change==2) context.trial_epoch++;
        tdma_service_timing_context(context, true);
        tdma_service_timing_rx_initial(ticks, 7u);
        tdma_service_timing_phase_end();
        assert(tdma_service_timing_rx_try_snapshot(&rx) && rx.initial_gap_count == 2u);
    }
    /* Internal FSM progress within the same running class is not a new session. */
    ticks += 20u;
    tdma_service_timing_phase_begin();
    tdma_service_timing_context((tdma_service_timing_context_t){.state=0x701u,
        .config_generation=7u,.trial_epoch=1u}, true);
    tdma_service_timing_rx_initial(ticks, 10u);
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_rx_try_snapshot(&rx) && rx.initial_gap_count == 3u);
    tdma_service_timing_phase_begin();
    tdma_service_timing_rx_station(TDMA_RX_TIMING_STATION_STATES, 0u, 0u);
    tdma_service_timing_rx_station(1u, 9u, 10u);
    tdma_service_timing_rx_initial(ticks-1u, 0u);
    tdma_service_timing_rx_initial(ticks, UINT64_C(1)<<32u);
    tdma_service_timing_rx_drop(TDMA_RX_DROP_CAUSE_COUNT, 0u);
    s_rx_work.station_polls[0] = UINT32_MAX;
    tdma_service_timing_rx_station(0u, 0u, 0u);
    s_rx_work.clamp_skipped_words = UINT64_MAX-3u;
    tdma_service_timing_rx_drop(TDMA_RX_DROP_CLAMP, 4u);
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_rx_try_snapshot(&rx));
    assert(rx.invalid_count == 7u && rx.clamp_skipped_words == UINT64_MAX);
    assert(rx.station_polls[0] == UINT32_MAX);
    ++s_guard;
    assert(!tdma_service_timing_rx_try_snapshot(&rx));
    ++s_guard;
    const uint32_t reset = tdma_service_timing_request_reset();
    assert(tdma_service_timing_rx_try_snapshot(&rx) && rx.reset_generation == generation);
    tdma_service_timing_phase_begin();
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_rx_try_snapshot(&rx) && rx.reset_generation == reset);
    assert(rx.invalid_count == 0u && rx.initial_observation_count == 0u);
    assert(rx.initial_gap_max_ticks == 0u && rx.clamp_skipped_words == 0u);
}

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
    assert(before.version == TDMA_SERVICE_TIMING_VERSION);
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
    /* RUN peak survives a larger STOP phase. Complete scheduler duration,
     * rather than the nested body, selects each state-specific peak. */
    tdma_service_timing_request_reset();
    const tdma_service_timing_context_t run = {2u, 4u, 6u, 9u};
    tdma_service_timing_phase_begin();
    tdma_service_timing_context(run, true);
    ticks += 100;
    tdma_service_timing_context(run, false);
    tdma_service_timing_phase_end();
    tdma_service_timing_scheduler_end(120);
    tdma_service_timing_scheduler_end(900); /* duplicate notification ignored */
    assert(tdma_service_timing_try_snapshot(&before));
    assert(before.autonomous_peak.total_ticks == 100 && before.autonomous_peak.full_phase_ticks == 120);
    assert(before.autonomous_phase_count == 1 && before.other_phase_count == 0);
    tdma_service_timing_phase_begin();
    tdma_service_timing_context(run, true);
    ticks += 90;
    tdma_service_timing_context(run, false);
    tdma_service_timing_phase_end();
    tdma_service_timing_scheduler_end(130);
    tdma_service_timing_phase_begin();
    tdma_service_timing_context(run, true);
    ticks += 900;
    tdma_service_timing_context((tdma_service_timing_context_t){0u, 5u, 6u, 9u}, false);
    tdma_service_timing_phase_end();
    tdma_service_timing_scheduler_end(950);
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.autonomous_peak.total_ticks == 90 && after.autonomous_peak.full_phase_ticks == 130);
    assert(after.other_peak.total_ticks == 900 && after.other_peak.full_phase_ticks == 950);
    assert(after.peak.total_ticks == 900 && after.peak.full_phase_ticks == 950);
    assert(after.autonomous_phase_count == 2 && after.other_phase_count == 1);
    /* Same state with a replaced generation/epoch is transitional, not RUN. */
    tdma_service_timing_phase_begin();
    tdma_service_timing_context(run, true);
    ticks += 1000;
    tdma_service_timing_context((tdma_service_timing_context_t){2u, 4u, 8u, 10u}, false);
    tdma_service_timing_phase_end();
    tdma_service_timing_scheduler_end(1100);
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.autonomous_phase_count == 2 && after.other_phase_count == 2);
    assert(after.other_peak.exit.trial_epoch == 8);
    tdma_service_timing_phase_begin();
    tdma_service_timing_context(run, true);
    ticks += 2000;
    tdma_service_timing_context(run, false);
    tdma_service_timing_phase_end();
    tdma_service_timing_scheduler_end(1);
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.last.invalid_count && after.autonomous_peak.full_phase_ticks == 130);
    tdma_service_timing_request_reset();
    tdma_service_timing_phase_begin();
    ticks++;
    tdma_service_timing_phase_end();
    tdma_service_timing_scheduler_end(5);
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.autonomous_peak.sequence == 0 && after.autonomous_phase_count == 0);
    assert(after.other_phase_count == 1 && after.other_peak.full_phase_ticks == 5);
    /* Compact per-phase counts must never wrap or replace a valid peak.
     * Exercise the production accumulator through the saturation boundary. */
    const uint32_t valid_peak_sequence = after.peak.sequence;
    tdma_service_timing_phase_begin();
    const uint64_t count_start = ticks;
    for (uint32_t i = 0u; i < UINT16_MAX; ++i)
        tdma_service_timing_record(TDMA_TIMING_SELECT_EMPTY, count_start);
    assert(s_work.calls[TDMA_TIMING_SELECT_EMPTY] == UINT16_MAX);
    assert(s_work.invalid_count == 0u);
    ticks += 10;
    tdma_service_timing_record(TDMA_TIMING_SELECT_EMPTY, count_start);
    tdma_service_timing_phase_end();
    tdma_service_timing_scheduler_end(15);
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.last.calls[TDMA_TIMING_SELECT_EMPTY] == UINT16_MAX);
    assert(after.last.invalid_count == 1u);
    assert(after.peak.sequence == valid_peak_sequence);
    assert(after.other_peak.full_phase_ticks == 5u);
    tdma_service_timing_request_reset();
    tdma_service_timing_phase_begin();
    ticks++;
    tdma_service_timing_phase_end();
    tdma_service_timing_scheduler_end(5);
    assert(tdma_service_timing_try_snapshot(&after));
    assert(after.last.calls[TDMA_TIMING_SELECT_EMPTY] == 0u);
    assert(after.last.invalid_count == 0u);
    test_rx_aggregate_lifecycle();
    puts("PASS: timing, RX aggregates, state attribution, scheduler interval, deferred reset, count saturation and invalid clock");
    return 0;
}
