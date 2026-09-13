#include <assert.h>
#include "tdma_service_timing.h"
#define main scheduler_legacy_main
#include "test_tdma_traffic_scheduler.c"
#undef main

static uint64_t timing_ticks;
uint64_t vdc_timestamp_clock_read_ticks64(void) { return ++timing_ticks; }
uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }
#include "../../components/tdma/src/tdma_service_timing.c"

static void check_select(tdma_traffic_scheduler_t *scheduler, bool maintenance,
    tdma_traffic_scheduler_result_t expected, tdma_service_timing_stage_t outcome,
    bool refreshed)
{
    tdma_traffic_dispatch_t dispatch;
    tdma_service_timing_phase_begin();
    assert(tdma_traffic_scheduler_select(scheduler, 1000000u, maintenance, &dispatch) == expected);
    tdma_service_timing_phase_end();
    tdma_service_timing_snapshot_t snapshot;
    assert(tdma_service_timing_try_snapshot(&snapshot) && snapshot.last.invalid_count == 0u);
    for (uint32_t stage = TDMA_TIMING_SELECT_EMPTY; stage <= TDMA_TIMING_SELECT_DISPATCH; ++stage)
        assert(snapshot.last.calls[stage] == (stage == (uint32_t)outcome));
    assert(snapshot.last.calls[TDMA_TIMING_SELECT_REFRESH] == (uint32_t)refreshed);
    assert(snapshot.last.elapsed_ticks[TDMA_TIMING_SELECT_REFRESH] <= snapshot.last.elapsed_ticks[outcome]);
}

int main(void)
{
    tdma_foundation_profile_t profile;
    tdma_traffic_scheduler_t scheduler;
    tdma_traffic_scheduler_slot_t slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
    assert(tdma_foundation_profile_default(&profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI));
    assert(tdma_traffic_scheduler_init(&scheduler, slots, TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT));
    assert(tdma_traffic_scheduler_configure(&scheduler, &profile));
    check_select(&scheduler, false, TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED, TDMA_TIMING_SELECT_EMPTY, true);
    scheduler.lock = 1u;
    check_select(&scheduler, false, TDMA_TRAFFIC_SCHEDULER_BUSY, TDMA_TIMING_SELECT_BUSY, false);
    scheduler.lock = 0u;
    tdma_traffic_request_t request = make_request(TDMA_PAYLOAD_CLASS_CONFIG_CONTROL, 0xC1u, 1000000u);
    assert(tdma_traffic_scheduler_enqueue(&scheduler, &request) == TDMA_TRAFFIC_SCHEDULER_OK);
    check_select(&scheduler, false, TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED, TDMA_TIMING_SELECT_BLOCKED, true);
    check_select(&scheduler, true, TDMA_TRAFFIC_SCHEDULER_OK, TDMA_TIMING_SELECT_DISPATCH, true);
    assert(tdma_traffic_scheduler_complete(&scheduler, TDMA_TRAFFIC_CONFIG_CONTROL, TDMA_TRAFFIC_COMPLETION_SENT));
    check_select(&scheduler, true, TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED, TDMA_TIMING_SELECT_EMPTY, true);
    assert(tdma_traffic_scheduler_suspend(&scheduler, NULL));
    check_select(&scheduler, true, TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED, TDMA_TIMING_SELECT_BLOCKED, false);
    return scheduler_legacy_main();
}
