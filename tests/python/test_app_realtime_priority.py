"""Execute the production phase dispatcher against deterministic clocks/IRQs.

The physical ingress tests own PIO/DMA correctness. This seam checks the
scheduler's release, quota, attribution and protected-phase boundaries.
"""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "app.h"
#include "app_realtime_profile.h"
#include "tdma_priority_rx.h"
#define BOARD_SYS_CLOCK_HZ 250000000u
#define MASK 0xffffffu
typedef void (*app_realtime_load_service_fn)(void);
static app_realtime_schedule_snapshot_t s_realtime_schedule;
static app_realtime_priority_snapshot_t s_realtime_priority;
static bool s_realtime_priority_active;
static uint32_t s_realtime_load_enabled_mask, s_realtime_load_quarantined_mask;
static tdma_priority_rx_snapshot_t lane;
static uint64_t now, timer_offset;
static uint32_t entry_deadline, entries, opens, closes, calls;
static uint32_t body_cost, tail_cost, timer_read_delay, service_cost;
static uint32_t snapshot_cost, snapshot_calls, accounting_cost;
static uint32_t cached_calls, cached_cost, cached_request, cached_notes;
static uint32_t planned_calls, planned_cost, planned_request, planned_notes;
static uint32_t planned_noted_request, planned_noted_wall, planned_noted_budget;
static uint32_t noted_request, noted_wall, noted_budget, note_cost, scheduler_ends;
static uint64_t cached_started, planned_started;
static uint32_t timer_reads;
static uint64_t events[32];
static uint32_t event_count, event_next;
static bool enabled, pending, snapshot_ok, timer_ok, arm_in_service, rebase_in_service, stop_in_service;
static void deliver(void) {
    while (enabled && entries && pending) {
        if ((int32_t)((uint32_t)(now+timer_offset)-entry_deadline)>=0) {
            enabled=false; return;
        }
        pending=false; --entries;
        ++lane.irq_count; lane.irq_total_cycles+=body_cost;
        if (body_cost>lane.irq_max_cycles) lane.irq_max_cycles=body_cost;
        now+=body_cost+tail_cost;
        while (event_next<event_count && events[event_next]<=now) {
            ++event_next; pending=true;
        }
        if (!entries) enabled=false;
    }
}
static void advance(uint32_t cycles) {
    const uint64_t target=now+cycles;
    while (event_next<event_count && events[event_next]<=target) {
        if (now<events[event_next]) now=events[event_next];
        ++event_next; pending=true; deliver();
    }
    if (now<target) now=target;
}
static uint32_t app_realtime_cycle_now(void) { advance(1u); return (uint32_t)now & MASK; }
static uint32_t app_realtime_elapsed_cycles(uint32_t start,uint32_t end) { return (end-start)&MASK; }
static void tight_loop_contents(void) { advance(1u); }
static void app_realtime_schedule_write_begin(void) { advance(accounting_cost); }
static void app_realtime_schedule_write_end(void) { advance(accounting_cost); }
static void tdma_service_timing_scheduler_end(uint32_t cycles) { (void)cycles; ++scheduler_ends; }
static uint32_t vdc_run_output_service_cached_core1(void) {
    assert(!enabled); /* Fallback never inherits the priority IRQ lease. */
    assert(!planned_calls);
    ++cached_calls; cached_started=now;
    advance(cached_cost);
    return cached_request;
}
static void vdc_run_output_note_cached_wall_core1(uint32_t request,uint32_t cycles,uint32_t budget) {
    assert(!enabled && cached_calls==cached_notes+1u);
    ++cached_notes; noted_request=request; noted_wall=cycles; noted_budget=budget;
    advance(note_cost);
}
static uint32_t vdc_run_output_service_planned_core1(void) {
    assert(!enabled && !cached_calls);
    ++planned_calls; planned_started=now;
    advance(planned_cost);
    return planned_request;
}
static void vdc_run_output_note_planned_wall_core1(uint32_t request,uint32_t cycles,uint32_t budget) {
    assert(!enabled && planned_calls==planned_notes+1u && !cached_notes);
    ++planned_notes; planned_noted_request=request;
    planned_noted_wall=cycles; planned_noted_budget=budget;
    advance(note_cost);
}
static bool tdma_runtime_owner_priority_rx_counters_core1(tdma_priority_rx_counters_t *out) {
    assert(!enabled); /* The production owner API rejects an open IRQ source. */
    ++snapshot_calls; advance(snapshot_cost);
    if (!snapshot_ok) return false;
    *out=(tdma_priority_rx_counters_t){lane.irq_total_cycles,lane.epoch,lane.irq_count,
        lane.irq_max_cycles,lane.active}; return true;
}
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *ticks) {
    assert(hz==BOARD_SYS_CLOCK_HZ);
    ++timer_reads;
    if (!timer_ok) return false;
    *ticks=now+timer_offset; advance(timer_read_delay); return true;
}
static void tdma_runtime_owner_priority_rx_window_core1(bool open,uint32_t quota,uint32_t deadline) {
    enabled=open; entries=quota; entry_deadline=deadline;
    if (open) { ++opens; deliver(); } else ++closes;
}
static void service(void) {
    ++calls;
    if (arm_in_service) {
        ++lane.epoch; lane.active=1u; lane.irq_count=0u;
        lane.irq_total_cycles=0u; lane.irq_max_cycles=0u;
    }
    if (rebase_in_service) ++lane.epoch;
    if (stop_in_service) lane.active=0u;
    /* CPU work excludes exception time. One-cycle steps make the wall clock
     * include every injected ISR in addition to the requested foreground. */
    for(uint32_t i=0u;i<service_cost;++i) advance(1u);
}
static void reset_baseline(void);
static void reset(void) {
    memset(&s_realtime_schedule,0,sizeof(s_realtime_schedule));
    memset(&s_realtime_priority,0,sizeof(s_realtime_priority));
    memset(&lane,0,sizeof(lane));
    s_realtime_priority_active=false;
    assert(app_realtime_profile_install(&s_realtime_schedule,375000u,1u));
    s_realtime_schedule.cycle_count=100u;
    s_realtime_load_enabled_mask=APP_REALTIME_LOAD_ALL_MASK;
    s_realtime_load_quarantined_mask=0u;
    lane.epoch=1u; lane.active=1u;
    now=0u; timer_offset=0xfffff000u; entries=opens=closes=calls=0u;
    event_count=event_next=0u; pending=enabled=false;
    body_cost=1000u; tail_cost=500u; service_cost=100u;
    timer_read_delay=0u; timer_ok=snapshot_ok=true;
    arm_in_service=rebase_in_service=stop_in_service=false;
    snapshot_cost=snapshot_calls=timer_reads=0u;
    accounting_cost=0u;
    cached_calls=cached_notes=noted_request=noted_wall=noted_budget=note_cost=scheduler_ends=0u;
    cached_cost=100u; cached_request=7u; cached_started=0u;
    planned_calls=planned_notes=planned_noted_request=planned_noted_wall=planned_noted_budget=0u;
    planned_cost=100u; planned_request=9u; planned_started=0u;
    reset_baseline();
}
'''

CASES = r'''
static void reset_baseline(void) {
    s_realtime_priority_baseline_valid=false;
    memset(&s_realtime_priority_baseline,0,sizeof(s_realtime_priority_baseline));
    memset(&s_realtime_priority_clock,0,sizeof(s_realtime_priority_clock));
}
static void test_cached_fallback(void) {
    const uint32_t budget=PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES;
    const uint32_t periods[]={375000u,1250000u,2500000u,3750000u};
    /* Full successful service must never invoke the fallback or its report. */
    reset();
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(calls==1u && scheduler_ends==1u && cached_calls==0u && cached_notes==0u);
    for(unsigned i=0u;i<4u;++i) {
        reset();
        assert(app_realtime_profile_install(&s_realtime_schedule,periods[i],2u));
        const uint32_t close=s_realtime_schedule.phase_end_cycle[0]-PROJECT_CORE1_PRIORITY_RX_CLOSE_CYCLES;
        now=close-budget-1000u;
        pending=true;
        events[event_count++]=now+2000u;
        events[event_count++]=now+4000u;
        assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
        assert(calls==0u && cached_calls==1u && cached_notes==1u && scheduler_ends==0u);
        assert(noted_request==7u && noted_wall==cached_cost+1u && noted_budget==budget);
        assert(s_realtime_schedule.phase_run_count[0]==0u);
        assert(s_realtime_schedule.phase_skip_count[0]==1u && s_realtime_schedule.phase_start_miss_count[0]==1u);
        assert(s_realtime_schedule.phase_last_start_cycle[0]==cached_started);
        assert(s_realtime_schedule.phase_last_runtime_cycles[0]==noted_wall);
        assert(s_realtime_schedule.phase_max_runtime_cycles[0]==noted_wall);
        assert(s_realtime_priority.background_max_cycles[0]==noted_wall);
        assert(s_realtime_schedule.phase_overrun_count[0]==0u);
        assert(s_realtime_schedule.schedule_miss_count==1u);
        /* Only the original waiting quota opens after the cached service. */
        const uint32_t quota=(uint32_t)((periods[i]-1u)/PROJECT_CORE1_PRIORITY_RX_MIN_PHYSICAL_CYCLES+1u);
        assert(lane.irq_count==(quota<3u?quota:3u));
        assert(opens==1u && !enabled);
    }
    /* Exact budget fit and one-cycle-short exclusion use the fresh clock.
     * Inactive lane: wait, phase-start and admission are three clock reads. */
    reset(); lane.active=0u;
    uint32_t close=s_realtime_schedule.phase_end_cycle[0]-PROJECT_CORE1_PRIORITY_RX_CLOSE_CYCLES;
    now=close-budget-3u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(cached_calls==1u && cached_started==close-budget);
    reset(); lane.active=0u; now=close-budget-2u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(cached_calls==0u && cached_notes==0u && calls==0u);
    /* Skip publication can invalidate an apparent fit. */
    reset(); lane.active=0u; accounting_cost=10u; now=close-budget-10u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(cached_calls==0u);
    reset(); now=close;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(cached_calls==0u && opens==0u);
    reset(); now=PROJECT_CORE1_PHASE_GUARD_START_CYCLE;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(cached_calls==0u && calls==0u);
    /* Disabled TDMA and every other phase retain their old skip policy. */
    reset(); now=close-budget-1000u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,NULL));
    assert(cached_calls==0u);
    reset(); now=close-budget-1000u; s_realtime_load_enabled_mask=0u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,APP_REALTIME_LOAD_VDC,service));
    assert(cached_calls==0u);
    for(uint32_t phase=1u;phase<APP_REALTIME_PHASE_COUNT;++phase) {
        reset(); now=s_realtime_schedule.phase_end_cycle[phase]+1u;
        assert(!app_realtime_run_phase(0u,phase,-1,service));
        assert(cached_calls==0u && calls==0u);
    }
    /* IRQ sample/mapping failure stays failed and never opens the lane,
     * while the independent core-local bounded handoff can still execute. */
    for(unsigned failure=0u;failure<2u;++failure) {
        reset(); now=close-budget-1000u; pending=true; s_realtime_priority_active=true;
        if(failure==0u) snapshot_ok=false; else timer_ok=false;
        assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
        assert(cached_calls==1u && cached_notes==1u && opens==0u && lane.irq_count==0u);
        assert(s_realtime_priority.sample_failures>0u && s_realtime_priority.budget_misses[0]>0u);
    }
    /* An 80 us candidate breach cannot be washed into the larger TDMA WCET. */
    reset(); now=close-budget-1000u; cached_cost=budget+10u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(noted_wall==budget+11u && s_realtime_schedule.phase_overrun_count[0]==1u);
    assert(s_realtime_schedule.phase_run_count[0]==0u && s_realtime_schedule.phase_skip_count[0]==1u);
    assert(s_realtime_priority.budget_misses[0]==1u);
    assert(s_realtime_schedule.schedule_miss_count>=2u);
    /* No active request still costs real caller wall time, including the
     * empty fast call; report receives zero and is not counted as full TDMA. */
    reset(); now=close-budget-1000u; cached_request=0u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(cached_calls==1u && noted_request==0u && noted_wall==cached_cost+1u);
    assert(s_realtime_schedule.phase_last_runtime_cycles[0]==noted_wall);
    /* Report runs after the measured end clock. Its cost is excluded from
     * fast-wall but remains visible to the dispatcher phase-tail deadline. */
    reset(); now=close-budget-1000u; note_cost=budget+10000u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(noted_wall==cached_cost+1u && s_realtime_schedule.phase_overrun_count[0]==0u);
    assert(s_realtime_schedule.phase_deadline_miss_count[0]==1u);
    reset(); now=close-budget-1000u; cached_cost=budget+10000u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_schedule.phase_overrun_count[0]==1u && s_realtime_schedule.phase_deadline_miss_count[0]==1u);
}
static void test_planned_fallback(void) {
    const uint32_t budget=PROJECT_CORE1_RUN_OUTPUT_PLAN_WCET_CYCLES;
    const uint32_t cached_budget=PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES;
    const uint32_t periods[]={375000u,1250000u,2500000u,3750000u};
    reset();
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(calls==1u && scheduler_ends==1u && !planned_calls && !planned_notes && !cached_calls);
    for(unsigned i=0u;i<4u;++i) {
        reset();
        assert(app_realtime_profile_install(&s_realtime_schedule,periods[i],2u));
        const uint32_t close=s_realtime_schedule.phase_end_cycle[0]-PROJECT_CORE1_PRIORITY_RX_CLOSE_CYCLES;
        now=close-budget-1000u; pending=true;
        events[event_count++]=now+2000u;
        events[event_count++]=now+4000u;
        assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
        assert(!calls && !scheduler_ends && planned_calls==1u && planned_notes==1u);
        assert(!cached_calls && !cached_notes);
        assert(planned_noted_request==9u && planned_noted_wall==planned_cost+1u && planned_noted_budget==budget);
        assert(s_realtime_schedule.phase_run_count[0]==0u && s_realtime_schedule.phase_skip_count[0]==1u);
        assert(s_realtime_schedule.phase_start_miss_count[0]==1u);
        assert(s_realtime_schedule.phase_last_start_cycle[0]==planned_started);
        assert(s_realtime_schedule.phase_last_runtime_cycles[0]==planned_noted_wall);
        assert(s_realtime_schedule.phase_max_runtime_cycles[0]==planned_noted_wall);
        assert(s_realtime_priority.background_max_cycles[0]==planned_noted_wall);
        assert(!s_realtime_schedule.phase_overrun_count[0] && s_realtime_schedule.schedule_miss_count==1u);
        const uint32_t quota=(periods[i]-1u)/PROJECT_CORE1_PRIORITY_RX_MIN_PHYSICAL_CYCLES+1u;
        assert(lane.irq_count==(quota<3u?quota:3u) && opens==1u && !enabled);
    }
    /* Selection is based on a fresh read after skip accounting. A one-cycle
     * planned deficit must select cached only, never both or the full owner. */
    reset(); lane.active=0u;
    const uint32_t close=s_realtime_schedule.phase_end_cycle[0]-PROJECT_CORE1_PRIORITY_RX_CLOSE_CYCLES;
    now=close-budget-3u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(planned_calls==1u && planned_started==close-budget && !cached_calls);
    reset(); lane.active=0u; now=close-budget-2u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(!planned_calls && cached_calls==1u && noted_budget==cached_budget && !calls);
    reset(); lane.active=0u; accounting_cost=10u; now=close-budget-10u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(!planned_calls && cached_calls==1u);
    reset(); lane.active=0u; now=close-cached_budget-2u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(!planned_calls && !cached_calls && !calls);
    reset(); now=close;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(!planned_calls && !cached_calls && !opens);
    reset(); now=PROJECT_CORE1_PHASE_GUARD_START_CYCLE;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(!planned_calls && !cached_calls && !calls);
    /* NULL, disabled and quarantined work cannot gain a fallback. */
    reset(); now=close-budget-1000u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,NULL));
    assert(!planned_calls && !cached_calls);
    for(unsigned disabled=0u;disabled<2u;++disabled) {
        reset(); now=close-budget-1000u;
        if(disabled) s_realtime_load_quarantined_mask=APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_VDC);
        else s_realtime_load_enabled_mask=0u;
        assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,APP_REALTIME_LOAD_VDC,service));
        assert(!planned_calls && !cached_calls && !calls);
    }
    for(uint32_t phase=1u;phase<APP_REALTIME_PHASE_COUNT;++phase) {
        reset(); now=s_realtime_schedule.phase_end_cycle[phase]+1u;
        assert(!app_realtime_run_phase(0u,phase,-1,service));
        assert(!planned_calls && !cached_calls && !calls);
        /* Give the non-TDMA test slot TDMA-sized capacity to ensure the
         * phase-id guard, rather than insufficient time, excludes fallback. */
        reset();
        s_realtime_schedule.phase_start_cycle[phase]=0u;
        s_realtime_schedule.phase_end_cycle[phase]=s_realtime_schedule.phase_end_cycle[0];
        s_realtime_schedule.phase_wcet_cycles[phase]=s_realtime_schedule.phase_wcet_cycles[0];
        now=close-budget-1000u;
        assert(!app_realtime_run_phase(0u,phase,-1,service));
        assert(!planned_calls && !cached_calls && !calls);
    }
    /* IRQ attribution failure remains recorded; the independently sampled
     * core-local phase clock can still admit output work with ingress shut. */
    for(unsigned failure=0u;failure<2u;++failure) {
        reset(); now=close-budget-1000u; pending=true; s_realtime_priority_active=true;
        if(failure)timer_ok=false; else snapshot_ok=false;
        assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
        assert(planned_calls==1u && planned_notes==1u && !cached_calls && !opens && !lane.irq_count);
        assert(s_realtime_priority.sample_failures && s_realtime_priority.budget_misses[0]);
    }
    reset(); now=close-budget-1000u; planned_cost=budget+10u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(planned_noted_wall==budget+11u && s_realtime_schedule.phase_overrun_count[0]==1u);
    assert(!s_realtime_schedule.phase_run_count[0] && s_realtime_schedule.phase_skip_count[0]==1u);
    assert(s_realtime_priority.budget_misses[0]==1u && s_realtime_schedule.schedule_miss_count>=2u);
    reset(); now=close-budget-1000u; planned_request=0u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(planned_calls==1u && !planned_noted_request && planned_noted_wall==planned_cost+1u);
    assert(s_realtime_schedule.phase_last_runtime_cycles[0]==planned_noted_wall);
    reset(); now=close-budget-1000u; note_cost=budget+10000u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(planned_noted_wall==planned_cost+1u && !s_realtime_schedule.phase_overrun_count[0]);
    assert(s_realtime_schedule.phase_deadline_miss_count[0]==1u);
    reset(); now=close-budget-1000u; planned_cost=budget+10000u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_schedule.phase_overrun_count[0]==1u && s_realtime_schedule.phase_deadline_miss_count[0]==1u);
}
int main(void) {
    test_cached_fallback();
    test_planned_fallback();
    /* Every allowed phase services pending work during an optional/omitted
     * foreground slot; the protected three never enable this IRQ. */
    for(uint32_t phase=0u;phase<APP_REALTIME_PHASE_COUNT;++phase) {
        reset();
        const bool allowed=phase!=APP_REALTIME_PHASE_MODEL &&
            phase!=APP_REALTIME_PHASE_TRIGGER_MEASURE && phase!=APP_REALTIME_PHASE_GUARD;
        events[event_count++]=s_realtime_schedule.phase_start_cycle[phase]+100u;
        assert(app_realtime_run_phase(0u,phase,-1,NULL));
        assert(calls==0u && !enabled);
        assert(lane.irq_count==(allowed?1u:0u));
        assert((opens!=0u)==allowed);
        assert(s_realtime_priority.budget_misses[phase]==0u);
    }
    reset();
    s_realtime_load_enabled_mask=0u;
    events[event_count++]=s_realtime_schedule.phase_start_cycle[APP_REALTIME_PHASE_VDC]+100u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(calls==0u && lane.irq_count==1u);
    reset();
    s_realtime_load_quarantined_mask=APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_VDC);
    pending=true;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(calls==0u && lane.irq_count==1u);
    /* Foreground IRQs remain charged to wall runtime; body-only attribution
     * retains the unmeasured tail in the foreground estimate. */
    reset(); pending=true; service_cost=2000u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(lane.irq_count==1u);
    assert(s_realtime_schedule.phase_last_runtime_cycles[0]>=3500u);
    assert(s_realtime_priority.background_max_cycles[0]>=2500u);
    assert(s_realtime_priority.irq_max_cycles[0]==body_cost);
    /* Reopening the wait window must consume only the remaining quota. */
    reset(); pending=true; service_cost=100u;
    events[event_count++]=3000u; events[event_count++]=5000u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(lane.irq_count==2u && pending && !enabled);
    /* Longer table periods use quotas based on the independent 1 ms
     * physical admission floor, not just two entries per table period. */
    reset();
    assert(app_realtime_profile_install(&s_realtime_schedule,3750000u,2u));
    for(uint32_t i=0u;i<14u;++i) events[event_count++]=100u+250000u*i;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,NULL));
    assert(lane.irq_count==14u);
    /* Failed clock/sample reads close the lane but cannot suppress the
     * ordinary TDMA foreground service. */
    reset(); snapshot_ok=false; pending=true; s_realtime_priority_active=true;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(calls==1u && lane.irq_count==0u && opens==0u);
    assert(s_realtime_priority.sample_failures>0u);
    reset(); timer_ok=false; pending=true;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(calls==1u && lane.irq_count==0u && opens==0u);
    /* A foreground-only breach cannot borrow its reserved IRQ allowance,
     * even while the original combined wall-time gate still passes. */
    reset(); service_cost=PROJECT_CORE1_TDMA_BACKGROUND_WCET_CYCLES+10u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_priority.budget_misses[0]==1u);
    assert(s_realtime_schedule.phase_overrun_count[0]==0u);
    reset(); service_cost=PROJECT_CORE1_PHASE_VDC_WCET_CYCLES+10u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(s_realtime_schedule.phase_overrun_count[APP_REALTIME_PHASE_VDC]==1u);
    assert(s_realtime_load_quarantined_mask&APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_VDC));
    reset(); service_cost=PROJECT_CORE1_PHASE_DPLL_WCET_CYCLES+10u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_DPLL,APP_REALTIME_LOAD_DPLL,service));
    assert(s_realtime_load_quarantined_mask==0u);
    /* Inherited lateness skips the foreground without quarantining it;
     * the remaining permitted IRQ waiting interval still remains useful. */
    reset(); now=PROJECT_CORE1_PHASE_VDC_START_CYCLE+6000u; pending=true;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(calls==0u && lane.irq_count==1u && s_realtime_load_quarantined_mask==0u);
    /* Translation brackets are conservative, including low32 rollover. */
    reset(); timer_read_delay=500u;
    events[event_count++]=PROJECT_CORE1_PHASE_TDMA_END_CYCLE-
        PROJECT_CORE1_PRIORITY_RX_CLOSE_CYCLES-100u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,NULL));
    assert(lane.irq_count==0u && pending && !enabled);
    /* An IRQ exceeding its candidate budget is visible. A late closure
     * reports failure instead of consuming the following protected phase. */
    reset(); body_cost=4000u;
    events[event_count++]=PROJECT_CORE1_PHASE_TDMA_END_CYCLE-
        PROJECT_CORE1_PRIORITY_RX_CLOSE_CYCLES-100u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,NULL));
    assert(s_realtime_priority.close_misses==1u);
    assert(s_realtime_priority.budget_misses[0]==1u);
    assert(!enabled);
    /* Rebase preserves historical totals and cannot be treated as ARM
     * resetting them. A single stopped ARM may legitimately reset them. */
    reset(); lane.irq_count=100u; lane.irq_total_cycles=100000u;
    rebase_in_service=true;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_priority.sample_failures>0u && opens==1u);
    assert(s_realtime_priority.background_max_cycles[0]>=service_cost);
    /* A real publication read has nonzero cost. The old extra baseline
     * read, here 3000 cycles, exceeds VDC's remaining entry slack after
     * previous-phase bookkeeping. The retained closed sample eliminates
     * that read without changing start/end/WCET or moving the epoch. */
    reset(); snapshot_cost=3000u;
    now=PROJECT_CORE1_PHASE_VDC_START_CYCLE-500u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(calls==0u && s_realtime_schedule.phase_start_miss_count[APP_REALTIME_PHASE_VDC]==1u);
    assert(snapshot_calls==3u);
    reset(); snapshot_cost=3000u;
    now=PROJECT_CORE1_PHASE_VDC_START_CYCLE-500u;
    s_realtime_priority_baseline=(app_priority_counts_t){0u,1u,0u,0u,1u};
    s_realtime_priority_baseline_valid=true;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(calls==1u && snapshot_calls==2u);
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_DPLL,APP_REALTIME_LOAD_DPLL,service));
    assert(calls==2u && snapshot_calls==4u);
    assert(timer_reads==1u);
    /* Cache invalidation re-observes an owner transition; a failed final
     * read cannot authorize reuse of an earlier successful baseline. */
    reset(); s_realtime_priority_baseline_valid=true;
    s_realtime_priority_baseline=(app_priority_counts_t){0u,1u,0u,0u,1u};
    snapshot_ok=false;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(!s_realtime_priority_baseline_valid);
    /* Accounting itself crossing the phase boundary remains a deadline
     * failure even with the foreground and IRQ wall time inside budget. */
    reset(); snapshot_cost=6000u;
    s_realtime_priority_baseline=(app_priority_counts_t){0u,1u,0u,0u,1u};
    s_realtime_priority_baseline_valid=true;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_schedule.phase_overrun_count[0]==0u);
    assert(s_realtime_schedule.phase_deadline_miss_count[0]==1u);
    /* Signed low32 deadlines and low24 SysTick rollover share one stable
     * clock mapping per table, but a new table rebinds its independent origin. */
    reset();
    const uint32_t wrapped_epoch=MASK-100u;
    now=wrapped_epoch;
    events[event_count++]=now+200u;
    assert(app_realtime_run_phase(wrapped_epoch,APP_REALTIME_PHASE_TDMA,-1,NULL));
    assert(lane.irq_count==1u && timer_reads==1u);
    assert(app_realtime_run_phase(wrapped_epoch,APP_REALTIME_PHASE_VDC,-1,NULL));
    assert(timer_reads==1u);
    s_realtime_priority_clock.valid=false;
    timer_offset+=12345u;
    assert(app_realtime_run_phase(wrapped_epoch,APP_REALTIME_PHASE_DPLL,-1,NULL));
    assert(timer_reads==2u);
    reset(); lane.active=0u; lane.irq_count=100u; lane.irq_total_cycles=100000u;
    arm_in_service=true;
    pending=true;
    s_realtime_priority.budget_misses[APP_REALTIME_PHASE_VDC]=12u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_priority.sample_failures==0u && lane.epoch==2u);
    assert(s_realtime_priority.budget_misses[APP_REALTIME_PHASE_VDC]==0u);
    assert(lane.irq_count==1u && opens==1u && !enabled);
    /* No lane: a bounded but nonzero diagnostic/accounting cost must not
     * consume the closing lead and starve VDC. These are injected host
     * costs, not a measured board WCET. Only TDMA observes ARM/STOP. */
    reset(); lane.active=0u; snapshot_cost=3000u; accounting_cost=1000u;
    const app_realtime_priority_snapshot_t inactive_budget=s_realtime_priority;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_DPLL,APP_REALTIME_LOAD_DPLL,service));
    assert(calls==3u && snapshot_calls==2u && opens==0u && timer_reads==0u);
    assert(s_realtime_schedule.phase_start_miss_count[APP_REALTIME_PHASE_VDC]==0u);
    assert(memcmp(&inactive_budget,&s_realtime_priority,sizeof(inactive_budget))==0);
    /* STOP completes in the TDMA service; its final run evidence remains,
     * while later inactive phases do not reset it or reopen the source. */
    reset(); pending=true; stop_in_service=true;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(!lane.active && lane.irq_count==1u && !enabled);
    const uint32_t stopped_reads=snapshot_calls, stopped_opens=opens;
    const app_realtime_priority_snapshot_t stopped_budget=s_realtime_priority;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(snapshot_calls==stopped_reads && opens==stopped_opens);
    assert(memcmp(&stopped_budget,&s_realtime_priority,sizeof(stopped_budget))==0);
    /* Attribution failure includes an expired window even with every owner
     * read succeeding. It is not a count of snapshot read failures. */
    reset(); now=PROJECT_CORE1_PHASE_VDC_END_CYCLE+1u;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_VDC,APP_REALTIME_LOAD_VDC,service));
    assert(snapshot_ok && s_realtime_priority.sample_failures==1u && calls==0u);
    /* Post-STOP maintenance cannot contaminate the retained run readback. */
    reset(); lane.active=0u;
    s_realtime_priority.background_max_cycles[0]=777u;
    s_realtime_priority.budget_misses[0]=3u;
    const app_realtime_priority_snapshot_t retained=s_realtime_priority;
    service_cost=PROJECT_CORE1_TDMA_BACKGROUND_WCET_CYCLES+10u;
    assert(app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(memcmp(&retained,&s_realtime_priority,sizeof(retained))==0);
    service_cost=PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES+10u;
    const uint32_t stopped_epoch=app_realtime_cycle_now();
    assert(!app_realtime_run_phase(stopped_epoch,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_schedule.phase_overrun_count[0]==1u);
    assert(memcmp(&retained,&s_realtime_priority,sizeof(retained))==0);
    /* Saturated cumulative counters cannot replenish a used quota. */
    reset(); lane.irq_count=UINT32_MAX;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_priority.sample_failures>0u && opens==1u);
    reset(); lane.irq_total_cycles=UINT64_MAX;
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_TDMA,-1,service));
    assert(s_realtime_priority.sample_failures>0u && opens==1u);
    /* Invalid phase rejection never invokes user work. */
    reset();
    assert(!app_realtime_run_phase(0u,APP_REALTIME_PHASE_COUNT,-1,service));
    assert(calls==0u);
    return 0;
}
'''


def test_production_dispatcher_priority_boundaries(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    source = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    helpers = source[source.index("typedef tdma_priority_rx_counters_t app_priority_counts_t;"):
                     source.index("bool app_realtime_request_period_us")]
    dispatcher = source[source.index("static void app_realtime_record_skip"):
                        source.index("static void app_realtime_tdma_phase")]
    fixture = tmp_path / "dispatcher.c"
    fixture.write_text(PRELUDE + helpers + dispatcher + CASES, encoding="utf-8")
    exe = tmp_path / ("dispatcher.exe" if os.name == "nt" else "dispatcher")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "application/inc"),
                    "-I" + str(ROOT / "components/tdma/inc"),
                    "-I" + str(ROOT / "config"), str(fixture),
                    str(ROOT / "application/src/app_realtime_profile.c"),
                    "-o", str(exe)], check=True, capture_output=True, text=True, timeout=60)
    subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=10)
