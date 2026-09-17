"""Execute the real VDC client/planner and libscpi callbacks at owner boundaries.

Hardware, clock samples and transport snapshots are observable stubs. The
client source, model inverse, edge planner, bridge arithmetic and parser are
production code. No board or serial connection is used.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def compile_host(directory, name, text, sources=(), parser=False):
    source = directory / (name + '.c')
    source.write_text(text, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler
    include = [ROOT / f'components/{domain}/inc' for domain in (
        'tdma', 'vdc_domain', 'vdc_dpll_manager', 'distributed_refmem',
        'calibration_manager', 'ota_manager', 'sync_io')]
    flags = []
    if parser:
        include += [ROOT / 'third_party/scpi-parser/libscpi/inc',
                    ROOT / 'middleware/scpi_port/inc']
        flags += ['-DSCPI_USER_CONFIG=1', '-Wno-error=attributes']
    executable = directory / (name + ('.exe' if os.name == 'nt' else ''))
    result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffunction-sections', '-fdata-sections', *flags,
        *['-I' + str(p) for p in include], str(source), *map(str, sources),
        '-Wl,--gc-sections', '-o', str(executable)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.fixture(scope='module')
def client(tmp_path_factory):
    source = (ROOT / 'components/vdc_dpll_manager/src/vdc_run_output.inc').read_text(encoding='utf-8')
    return compile_host(tmp_path_factory.mktemp('vdc-run-client'), 'client',
                        CLIENT_PREFIX + source + CLIENT_MAIN,
                        [ROOT / 'components/vdc_domain/src/vdc_domain.c',
                         ROOT / 'components/vdc_domain/src/vdc_timestamp.c',
                         ROOT / 'components/tdma/src/tdma_profile.c'])


@pytest.mark.parametrize('scenario', [
    'arm_wait', 'pre_arm_wait', 'pre_first_stop', 'unseen_arm_stop',
    'retire_interleave', 'prepare_interleave', 'status_interleave', 'wrong_generation',
    'busy_session', 'busy_role', 'busy_epoch', 'busy_run', 'busy_invalid',
    'busy_slot', 'busy_schedule', 'busy_owner_without_model',
    'first_session', 'first_role', 'first_epoch', 'first_run', 'first_slot',
    'first_schedule', 'first_invalid', 'model_tail', 'delay_latched',
    'stop_during_plan', 'session_during_plan', 'gate_busy', 'core_guard', 'no_request',
])
def test_production_client(client, scenario):
    result = subprocess.run([str(client), scenario], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('phase', ['prepared', 'running'])
@pytest.mark.parametrize('outcome', [
    'ring', 'wait', 'model', 'identity', 'dma', 'bridge', 'snapshot',
    'second_snapshot', 'postplan', 'plan', 'submit', 'submitted', 'cancel',
    'saturate', 'terminal', 'busy', 'reset',
])
def test_diagnostic_outcomes(client, phase, outcome):
    result = subprocess.run([str(client), f'diag_{phase}_{outcome}'],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('scenario', [
    'busy_private', 'bridge_free_hit', 'token_ready', 'token_busy',
    'tail_ready', 'tail_busy', 'falling_ready', 'falling_busy', 'clock_changed',
    'stop', 'session', 'role', 'epoch', 'slot',
    'cancel', 'prepare_reset', 'terminal', 'reject_retry', 'retry_not_ready', 'postplan_stop',
    'postplan_unavailable', 'missing_model', 'saturate_prefetched',
    'saturate_hits', 'saturate_invalidations',
])
def test_prefetch_lifecycle(client, scenario):
    result = subprocess.run([str(client), 'prefetch_' + scenario],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('scenario', [
    'first', 'empty', 'hit', 'busy', 'token', 'tail', 'fall', 'stop',
    'session', 'role', 'epoch', 'run', 'slot', 'schedule', 'invalid',
    'missing_model', 'clock', 'poststop', 'cancel', 'terminal', 'reject',
    'gate', 'core', 'wall', 'wall_stale', 'wall_busy', 'saturate',
])
def test_cached_only_handoff(client, scenario):
    result = subprocess.run([str(client), 'fast_' + scenario],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


CLIENT_PREFIX = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "vdc_run_output.h"
#include "vdc_output_edge_plan.h"
#include "vdc_future_raw.h"
#define BOARD_SYS_CLOCK_HZ 250000000u
#define PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES 20000u
typedef struct { uint32_t cycle_cycles; } app_realtime_schedule_snapshot_t;
static uint32_t table_cycles=375000u;
static bool schedule_ok=true, raw_ok=true;
static uint64_t raw_override;
static vdc_output_timing_profile_t timing={11000u,12000u,8000u};
bool app_realtime_get_schedule_snapshot(app_realtime_schedule_snapshot_t *out)
{ out->cycle_cycles=table_cycles; return schedule_ok; }
uint32_t app_realtime_cycle_cycles_core1(void) { return table_cycles; }
bool vdc_dpll_manager_get_output_timing_profile(vdc_output_timing_profile_t *out)
{ *out=timing;return true; }
static vdc_domain_context_t s_vdc_domain;
static tdma_service_service_t *s_vdc_tdma_service;
static unsigned core, prepare_calls, release_calls, service_calls, submit_calls, cancels;
static unsigned hook, hook_seen, generation, snapshot_calls;
static unsigned ring_calls, fail_ring_call, fail_snapshot_call;
static unsigned bridge_calls, submit_attempts, stop_ring_call;
static bool bridge_available=true, submit_allowed=true, clock_supported=true;
static bool ready=true, model_available=true, cancelled;
static int32_t delay=100;
static uint32_t session=8u;
static tdma_ring_clock_snapshot_t ring;
static vdc_dpll_manager_committed_model_t model;
static sync_io_run_output_snapshot_t hardware;
static sync_io_run_output_edge_t admitted[4][SYNC_IO_RUN_OUTPUT_MAX_EDGES];
static uint32_t admitted_count[4];
static sync_io_run_output_submit_failure_t submit_failure;
static vdc_timestamp_clock_bridge_t bridge={.raw_before=1000000u,.raw_after=1000002u,
    .local_ns=4000000u,.tick_hz=BOARD_SYS_CLOCK_HZ};
static void interleave(void);
static unsigned get_core_num(void) { return core; }
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
bool vdc_dpll_manager_get_output_delay_ns(int32_t *out) { *out=delay; return true; }
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{
    ++ring_calls; if(ring_calls==fail_ring_call)return false;
    if(ring_calls==stop_ring_call) { ring.enabled=0u; ++ring.config_seq; }
    *out=ring; return true;
}
bool tdma_service_run_stopped_maintenance(tdma_service_service_t *service,
    bool(*callback)(void*),void *context)
{ (void)service; return !ring.enabled && !ring.adapter_started && callback(context); }
bool vdc_timestamp_clock_configuration_supported(uint32_t hz)
{ assert(hz==BOARD_SYS_CLOCK_HZ); return clock_supported; }
bool vdc_timestamp_clock_try_read_bridge(uint32_t hz,vdc_timestamp_clock_bridge_t *out)
{
    assert(hz==BOARD_SYS_CLOCK_HZ); ++bridge_calls; *out=bridge;
    if(!bridge_available)return false;
    if(hook==4u) { ring.enabled=0u; ring.data_enabled=0u; ++ring.config_seq; }
    if(hook==5u) ++session;
    return true;
}
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{
    assert(hz==BOARD_SYS_CLOCK_HZ); if(!raw_ok)return false;
    *out=raw_override ? raw_override : hardware.state==SYNC_IO_RUN_OUTPUT_RUNNING ?
        hardware.last_falling_tick-500000u : bridge.raw_after;
    if(hook==4u) { ring.enabled=0u; ring.data_enabled=0u; ++ring.config_seq; }
    if(hook==5u) ++session;
    return true;
}
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{ if(!model_available)return false; *out=model; return true; }
bool sync_io_run_output_prepare_uniform(uint32_t hz,uint32_t duration,uint32_t high_ticks,uint32_t *request)
{
    assert(hz==BOARD_SYS_CLOCK_HZ && duration==1000u); ++prepare_calls;
    assert(high_ticks==250u);
    if(hook==2u)interleave();
    memset(&hardware,0,sizeof(hardware)); hardware.state=SYNC_IO_RUN_OUTPUT_PREPARED;
    hardware.generation=++generation; *request=generation; cancelled=false; return true;
}
void sync_io_run_output_cancel(void) { ++cancels; cancelled=true; }
void sync_io_run_output_service_core1(void)
{
    ++service_calls;
    if(cancelled)hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED;
    if(hook==1u) { hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED; interleave(); }
}
bool sync_io_run_output_snapshot(sync_io_run_output_snapshot_t *out)
{
    ++snapshot_calls; if(snapshot_calls==fail_snapshot_call)return false;
    if(hook==3u && snapshot_calls==2u)interleave();
    *out=hardware; return true;
}
bool sync_io_run_output_release(uint32_t request)
{
    assert(request==hardware.generation && hardware.state==SYNC_IO_RUN_OUTPUT_RETIRED);
    ++release_calls; hardware.state=SYNC_IO_RUN_OUTPUT_IDLE; return true;
}
bool sync_io_run_output_can_submit_core1(uint32_t request)
{ return request==hardware.generation && ready && !cancelled; }
bool sync_io_run_output_submit_count_core1(uint32_t request,
    const sync_io_run_output_edge_t *edges,uint32_t count)
{
    assert(request==hardware.generation && ready && !cancelled && submit_calls<4u);
    ++submit_attempts;
    if(!submit_allowed) {
        if (submit_failure==SYNC_IO_RUN_OUTPUT_SUBMIT_FAILURE_NONE)
            submit_failure=SYNC_IO_RUN_OUTPUT_SUBMIT_FAILURE_GUARD;
        return false;
    }
    assert(count && count<=SYNC_IO_RUN_OUTPUT_MAX_EDGES);
    memcpy(admitted[submit_calls],edges,count*sizeof(edges[0]));
    admitted_count[submit_calls++]=count;
    hardware.last_ordinal=edges[count-1u].ordinal;
    hardware.last_falling_tick=edges[count-1u].falling_tick;
    hardware.state=SYNC_IO_RUN_OUTPUT_RUNNING; return true;
}
sync_io_run_output_submit_failure_t sync_io_run_output_last_submit_failure(void)
{ return submit_failure; }
'''


CLIENT_MAIN = r'''
static void interleave(void)
{
    const unsigned kind=hook; hook=0u; ++hook_seen;
    if(kind==1u) {
        core=0u; run_output_release_core0();
        assert(!release_calls && s_run_output_request==generation);
        uint32_t request=999u; vdc_run_output_status_t output;
        assert(!vdc_run_output_prepare(1000000u,1000u,1000u,&request) && request==999u);
        assert(!vdc_run_output_status(&output)); core=1u;
    } else {
        const unsigned before=service_calls; core=1u;
        vdc_run_output_service_core1(); assert(service_calls==before); core=0u;
    }
}
static void initialize(void)
{
    ring.config_seq=ring.applied_config_seq=2u;
    ring.local_slot_id=1u; ring.schedule_crc32=0x123u;
    model.token=11u; model.session=session; model.role_generation=3u;
    model.clock_epoch_id=4u; model.clock_run_id=5u; model.local_slot=1u;
    model.dco.valid=1u; model.dco.nominal_period_ns=1000000u;
    model.dco.tdma_schedule_crc32=ring.schedule_crc32;
    s_vdc_domain.control.profile.generation=model.role_generation;
    s_vdc_domain.clock.epoch_id=model.clock_epoch_id;
    s_vdc_domain.clock.run_id=model.clock_run_id;
    s_vdc_domain.schedule.local_slot_id=model.local_slot;
    s_vdc_domain.schedule.schedule_crc32=model.dco.tdma_schedule_crc32;
}
static void prepare(void)
{
    uint32_t request=0u; core=0u;
    assert(vdc_run_output_prepare(1000000u,1000u,1000u,&request));
    assert(request==generation && s_run_output_request==generation);
}
static void arm(bool start)
{
    ring.enabled=ring.adapter_started=1u; ring.config_seq=ring.applied_config_seq=3u;
    ring.data_enabled=start; core=1u;
}
static void finish_cancel(void)
{
    assert(cancels); core=1u; vdc_run_output_service_core1(); core=0u;
    run_output_release_core0(); assert(release_calls==1u && !s_run_output_request);
}
static void diagnostic(const char *name)
{
    const bool running=!strncmp(name,"running_",8u);
    const char *kind=name+(running?8u:9u);
    const uint32_t phase=running?VDC_RUN_OUTPUT_RUNNING_PHASE:VDC_RUN_OUTPUT_PREPARED_PHASE;
    prepare();arm(true);
    assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_OUTCOME_NONE);
    if(running)vdc_run_output_service_core1();
    ring_calls=snapshot_calls=0u;
    uint32_t expected=VDC_RUN_OUTPUT_RING_UNAVAILABLE;
    bool should_cancel=false,should_submit=false;
    if(!strcmp(kind,"ring"))fail_ring_call=1u;
    else if(!strcmp(kind,"wait")) {
        ring.data_enabled=0u;expected=running?VDC_RUN_OUTPUT_BINDING_CANCELLED:VDC_RUN_OUTPUT_RING_WAIT;
        should_cancel=running;
    } else if(!strcmp(kind,"model")) { model_available=false;expected=VDC_RUN_OUTPUT_MODEL_UNAVAILABLE; }
    else if(!strcmp(kind,"identity")) {
        ++model.local_slot;expected=running?VDC_RUN_OUTPUT_BINDING_CANCELLED:VDC_RUN_OUTPUT_IDENTITY_WAIT;
        should_cancel=running;
    } else if(!strcmp(kind,"dma")) { ready=false;expected=VDC_RUN_OUTPUT_DMA_NOT_READY; }
    else if(!strcmp(kind,"bridge")) {
        if(running)raw_ok=false;else bridge_available=false;
        expected=VDC_RUN_OUTPUT_BRIDGE_UNAVAILABLE;
    }
    else if(!strcmp(kind,"snapshot") || !strcmp(kind,"second_snapshot")) {
        fail_snapshot_call=!strcmp(kind,"snapshot")?1u:2u;expected=VDC_RUN_OUTPUT_BACKEND_SNAPSHOT_UNAVAILABLE;
    } else if(!strcmp(kind,"postplan")) { fail_ring_call=2u;expected=VDC_RUN_OUTPUT_POSTPLAN_RING_UNAVAILABLE; }
    else if(!strcmp(kind,"plan")) { model.dco.nominal_period_ns=0u;expected=VDC_RUN_OUTPUT_PLAN_REJECTED; }
    else if(!strcmp(kind,"submit")) { submit_allowed=false;expected=VDC_RUN_OUTPUT_SUBMIT_REJECTED; }
    else if(!strcmp(kind,"submitted")) { expected=VDC_RUN_OUTPUT_SUBMITTED;should_submit=true; }
    else if(!strcmp(kind,"cancel")) { ++session;expected=VDC_RUN_OUTPUT_BINDING_CANCELLED;should_cancel=true; }
    else if(!strcmp(kind,"saturate")) {
        ready=false;expected=VDC_RUN_OUTPUT_DMA_NOT_READY;
        s_run_output.outcomes[phase][expected]=UINT32_MAX;
    } else if(!strcmp(kind,"terminal") || !strcmp(kind,"busy")) {
        vdc_run_output_status_t before=s_run_output;
        if(!strcmp(kind,"terminal"))hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED;
        else s_run_output_busy=1u;
        vdc_run_output_service_core1();assert(!memcmp(&before,&s_run_output,sizeof(before)));return;
    } else if(!strcmp(kind,"reset")) {
        ready=false;vdc_run_output_service_core1();assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_DMA_NOT_READY);
        vdc_run_output_cancel();finish_cancel();
        ring.enabled=ring.adapter_started=ring.data_enabled=0u;
        prepare();
        assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_OUTCOME_NONE && !s_run_output.last_phase);
        for(unsigned p=0;p<VDC_RUN_OUTPUT_PHASE_COUNT;++p)
            for(unsigned o=0;o<VDC_RUN_OUTPUT_OUTCOME_COUNT;++o)assert(!s_run_output.outcomes[p][o]);
        return;
    } else assert(false);
    vdc_run_output_status_t before=s_run_output;
    const unsigned submits=submit_calls,cancels_before=cancels;
    vdc_run_output_service_core1();
    assert(s_run_output.last_phase==phase && s_run_output.last_outcome==expected);
    for(unsigned p=0;p<VDC_RUN_OUTPUT_PHASE_COUNT;++p) {
        for(unsigned o=0;o<VDC_RUN_OUTPUT_OUTCOME_COUNT;++o) {
            uint32_t count=before.outcomes[p][o];
            if(p==phase && o==expected && count!=UINT32_MAX)++count;
            assert(s_run_output.outcomes[p][o]==count);
        }
    }
    assert(submit_calls==submits+(should_submit?1u:0u));
    assert(cancels==cancels_before+(should_cancel?1u:0u));
    if(should_cancel) {
        vdc_run_output_service_core1();
        assert(s_run_output.last_phase==phase && s_run_output.last_outcome==expected);
    }
}
static void prefetch_step(unsigned plans,unsigned submissions)
{
    unsigned b=bridge_calls,s=submit_attempts,p=s_run_output.partial_plan_steps;
    const uint32_t sequence=s_run_output.service_sequence;
    vdc_run_output_service_core1();
    /* A backend that retired during the entry call must leave the client
     * metadata untouched; otherwise an active service advances exactly once. */
    assert(s_run_output.service_sequence==sequence || s_run_output.service_sequence==sequence+1u);
    assert(s_run_output.partial_plan_steps-p==plans && submit_attempts-s==submissions);
    assert(bridge_calls-b<=1u && submit_attempts-s<=1u);
}
static void assert_tail_unchanged(const vdc_run_output_status_t *before,
                                const sync_io_run_output_snapshot_t *old_hw)
{
    assert(s_run_output.last_local_ns==before->last_local_ns);
    assert(s_run_output.last_target_vdc_ns==before->last_target_vdc_ns);
    assert(s_run_output.maximum_bridge_width_ticks==before->maximum_bridge_width_ticks);
    assert(s_run_output.blocks_planned==before->blocks_planned);
    assert(hardware.last_ordinal==old_hw->last_ordinal);
    assert(hardware.last_falling_tick==old_hw->last_falling_tick);
    assert(s_run_output.ring_config==before->ring_config);
    assert(s_run_output.role_generation==before->role_generation);
    assert(s_run_output.clock_epoch==before->clock_epoch && s_run_output.clock_run==before->clock_run);
}
static void prefetch_case(const char *kind)
{
    prepare();arm(true);prefetch_step(1u,1u);
    assert(s_run_output.service_sequence==1u);
    assert(submit_calls==1u && !s_run_output_pending.valid);
    const vdc_run_output_status_t initial=s_run_output;
    const sync_io_run_output_snapshot_t old_hw=hardware;
    sync_io_run_output_edge_t prefix[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES];
    memcpy(prefix,admitted[0],sizeof(prefix));
    ready=false;prefetch_step(1u,0u);
    assert(s_run_output_pending.valid && s_run_output.prefetched_blocks==1u);
    assert(submit_calls==1u && !s_run_output.cache_hits);
    assert_tail_unchanged(&initial,&old_hw);
    assert(!memcmp(prefix,admitted[0],sizeof(prefix)));
    sync_io_run_output_edge_t cached[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES];
    memcpy(cached,s_run_output_pending.edges,sizeof(cached));
    assert(cached[0].ordinal>old_hw.last_ordinal);
    /* Repeated busy service must neither rewrite the private suffix nor
     * perform another inverse/bridge, even when bridge is now unavailable. */
    bridge_available=false;prefetch_step(0u,0u);
    assert_tail_unchanged(&initial,&old_hw);
    assert(!memcmp(cached,s_run_output_pending.edges,sizeof(cached)));
    if(!strcmp(kind,"busy_private"))return;
    if(!strcmp(kind,"saturate_hits")) {
        s_run_output.cache_hits=UINT32_MAX;ready=true;prefetch_step(0u,1u);
        assert(s_run_output.cache_hits==UINT32_MAX && submit_calls==2u);return;
    }
    if(!strcmp(kind,"saturate_prefetched") || !strcmp(kind,"saturate_invalidations")) {
        const bool invalidations=!strcmp(kind,"saturate_invalidations");
        if(invalidations)s_run_output.cache_invalidations=UINT32_MAX;
        else s_run_output.prefetched_blocks=UINT32_MAX;
        ++model.token;bridge_available=true;prefetch_step(1u,0u);
        assert(s_run_output_pending.valid && s_run_output_pending.model_token==model.token);
        assert(submit_calls==1u);
        assert((invalidations?s_run_output.cache_invalidations:s_run_output.prefetched_blocks)==UINT32_MAX);
        assert_tail_unchanged(&initial,&old_hw);return;
    }
    if(!strcmp(kind,"bridge_free_hit")) {
        ready=true;prefetch_step(0u,1u);
        assert(submit_calls==2u && s_run_output.cache_hits==1u && !s_run_output_pending.valid);
        assert(!memcmp(cached,admitted[1],sizeof(cached)));
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));return;
    }
    if(!strcmp(kind,"clock_changed")) {
        ready=true;clock_supported=false;prefetch_step(0u,0u);
        assert(!s_run_output_pending.valid && submit_calls==1u);
        assert(cancelled && s_run_output.last_outcome==VDC_RUN_OUTPUT_BINDING_CANCELLED);
        assert_tail_unchanged(&initial,&old_hw);return;
    }
    if(!strncmp(kind,"token_",6u) || !strncmp(kind,"tail_",5u) || !strncmp(kind,"falling_",8u)) {
        const bool token=!strncmp(kind,"token_",6u);
        const bool tail=!strncmp(kind,"tail_",5u);
        if(token) { ++model.token;model.dco.phase_offset_ns=2000; }
        else if(tail) { hardware.last_ordinal+=100u;hardware.last_falling_tick+=25000000u; }
        else hardware.last_falling_tick+=4u;
        const bool busy=strstr(kind,"busy")!=NULL;
        ready=!busy;raw_ok=false;prefetch_step(0u,0u);
        assert(!s_run_output_pending.valid && s_run_output.cache_invalidations==1u);
        assert(submit_calls==1u);
        raw_ok=true;bridge_available=true;prefetch_step(1u,busy?0u:1u);
        if(busy) { assert(s_run_output_pending.valid);ready=true;bridge_available=false;prefetch_step(0u,1u); }
        assert(submit_calls==2u && !s_run_output_pending.valid);
        for(unsigned i=0;i<SYNC_IO_RUN_OUTPUT_BLOCK_EDGES;++i) {
            assert(admitted[1][i].model_token==model.token);
            if(tail)assert(admitted[1][i].ordinal>old_hw.last_ordinal+100u);
        }
        if(token || tail)assert(memcmp(cached,admitted[1],sizeof(cached))!=0);
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));return;
    }
    if(!strcmp(kind,"reject_retry")) {
        ready=true;submit_allowed=false;prefetch_step(0u,1u);
        assert(!s_run_output_pending.valid && submit_calls==1u);
        assert(!s_run_output.cache_hits);
        assert_tail_unchanged(&initial,&old_hw);
        assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_SUBMIT_REJECTED);
        submit_allowed=true;bridge_available=true;prefetch_step(1u,1u);
        assert(submit_calls==2u && !memcmp(cached,admitted[1],sizeof(cached)));
        return;
    }
    if(!strcmp(kind,"retry_not_ready")) {
        ready=true;submit_allowed=false;
        submit_failure=SYNC_IO_RUN_OUTPUT_SUBMIT_FAILURE_NOT_READY;
        prefetch_step(0u,1u);
        /* The backend rejected only its final readiness check. The complete
         * suffix remains private and the admitted prefix is unchanged. */
        assert(s_run_output_pending.valid && submit_calls==1u);
        assert(!s_run_output.cache_hits);
        assert_tail_unchanged(&initial,&old_hw);
        assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_SUBMIT_REJECTED);
        submit_allowed=true;bridge_available=true;prefetch_step(0u,1u);
        assert(submit_calls==2u && !s_run_output_pending.valid);
        assert(!memcmp(cached,admitted[1],sizeof(cached)));
        return;
    }
    if(!strcmp(kind,"postplan_unavailable")) {
        ready=true;fail_ring_call=ring_calls+2u;prefetch_step(0u,0u);
        assert_tail_unchanged(&initial,&old_hw);assert(s_run_output_pending.valid);
        assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_POSTPLAN_RING_UNAVAILABLE);
        fail_ring_call=0u;prefetch_step(0u,1u);
        assert(submit_calls==2u && !memcmp(cached,admitted[1],sizeof(cached)));return;
    }
    if(!strcmp(kind,"missing_model")) {
        ready=true;model_available=false;prefetch_step(0u,0u);
        assert_tail_unchanged(&initial,&old_hw);
        assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_MODEL_UNAVAILABLE);
        model_available=true;prefetch_step(0u,1u);assert(submit_calls==2u);return;
    }
    ready=true;
    if(!strcmp(kind,"terminal"))hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED;
    else if(!strcmp(kind,"stop")) { ring.enabled=0u; ++ring.config_seq; }
    else if(!strcmp(kind,"session"))++session;
    else if(!strcmp(kind,"role"))++s_vdc_domain.control.profile.generation;
    else if(!strcmp(kind,"epoch"))++s_vdc_domain.clock.epoch_id;
    else if(!strcmp(kind,"slot"))++model.local_slot;
    else if(!strcmp(kind,"postplan_stop"))stop_ring_call=ring_calls+2u;
    else {
        assert(!strcmp(kind,"cancel") || !strcmp(kind,"prepare_reset"));
        const unsigned before_services=service_calls;
        const sync_io_run_output_snapshot_t before_hw=hardware;
        vdc_run_output_cancel();
        assert(s_run_output_pending.valid && service_calls==before_services);
        assert(!memcmp(&before_hw,&hardware,sizeof(hardware)));
    }
    prefetch_step(0u,0u);assert_tail_unchanged(&initial,&old_hw);
    assert(!s_run_output_pending.valid); /* Owner cancel/terminal clears immediately. */
    if(strcmp(kind,"terminal")) { assert(cancelled);prefetch_step(0u,0u); }
    assert(!s_run_output_pending.valid && submit_calls==1u);
    if(!strcmp(kind,"prepare_reset")) {
        core=0u;run_output_release_core0();assert(!s_run_output_request);
        ring.enabled=ring.adapter_started=ring.data_enabled=0u;
        /* Poison a retired private buffer: prepare itself must reset it. */
        memset(&s_run_output_pending,0xa5,sizeof(s_run_output_pending));
        prepare();assert(!s_run_output_pending.valid && !s_run_output_pending.model_token);
        assert(!s_run_output.prefetched_blocks && !s_run_output.cache_hits && !s_run_output.cache_invalidations);
        arm(true);bridge_available=true;prefetch_step(1u,1u);assert(submit_calls==2u);
    }
}
static void fast_case(const char *kind)
{
    prepare();arm(true);
    if(!strcmp(kind,"first")) {
        assert(vdc_run_output_service_cached_core1()==generation);
        assert(!bridge_calls && !submit_calls && s_run_output.fast_empty==1u);
        return;
    }
    prefetch_step(1u,1u);
    if(!strcmp(kind,"empty")) {
        const unsigned bridges=bridge_calls;
        assert(vdc_run_output_service_cached_core1()==generation);
        assert(bridge_calls==bridges && submit_calls==1u && s_run_output.fast_empty==1u);
        return;
    }
    ready=false;prefetch_step(1u,0u);ready=true;
    const unsigned bridges=bridge_calls, attempts=submit_attempts, plans=s_run_output.partial_plan_steps;
    const vdc_run_output_status_t before=s_run_output;
    const sync_io_run_output_snapshot_t old_hw=hardware;
    sync_io_run_output_edge_t cached[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES];
    memcpy(cached,s_run_output_pending.edges,sizeof(cached));
    bridge_available=false; /* The retained mapping never needs re-sampling. */
    bool hit=false;
    if(!strcmp(kind,"hit") || !strcmp(kind,"wall") ||
       !strcmp(kind,"wall_stale") || !strcmp(kind,"wall_busy") || !strcmp(kind,"saturate")) hit=true;
    else if(!strcmp(kind,"busy"))ready=false;
    else if(!strcmp(kind,"token"))++model.token;
    else if(!strcmp(kind,"tail"))++s_run_output_pending.predecessor_ordinal;
    else if(!strcmp(kind,"fall"))++s_run_output_pending.predecessor_fall;
    else if(!strcmp(kind,"stop")) { ring.enabled=0u;++ring.config_seq; }
    else if(!strcmp(kind,"session"))++session;
    else if(!strcmp(kind,"role"))++s_vdc_domain.control.profile.generation;
    else if(!strcmp(kind,"epoch"))++s_vdc_domain.clock.epoch_id;
    else if(!strcmp(kind,"run"))++s_vdc_domain.clock.run_id;
    else if(!strcmp(kind,"slot"))++model.local_slot;
    else if(!strcmp(kind,"schedule"))++model.dco.tdma_schedule_crc32;
    else if(!strcmp(kind,"invalid"))model.dco.valid=false;
    else if(!strcmp(kind,"missing_model"))model_available=false;
    else if(!strcmp(kind,"clock"))clock_supported=false;
    else if(!strcmp(kind,"poststop"))stop_ring_call=ring_calls+2u;
    else if(!strcmp(kind,"cancel"))vdc_run_output_cancel();
    else if(!strcmp(kind,"terminal"))hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED;
    else if(!strcmp(kind,"reject"))submit_allowed=false;
    else if(!strcmp(kind,"gate"))s_run_output_busy=1u;
    else if(!strcmp(kind,"core"))core=0u;
    else assert(false);
    if(!strcmp(kind,"saturate")) {
        s_run_output.fast_calls=s_run_output.fast_submissions=UINT32_MAX;
        s_run_output.fast_wall_samples=s_run_output.fast_budget_overruns=UINT32_MAX;
    }
    const uint32_t request=vdc_run_output_service_cached_core1();
    assert(bridge_calls==bridges);
    assert(s_run_output.partial_plan_steps==plans);
    if(hit) {
        assert(request==generation && submit_calls==2u && submit_attempts==attempts+1u);
        assert(!memcmp(cached,admitted[1],sizeof(cached)) && !s_run_output_pending.valid);
        assert(s_run_output.fast_submissions==(!strcmp(kind,"saturate")?UINT32_MAX:1u));
    } else {
        assert(submit_calls==1u && !s_run_output.fast_submissions);
        assert_tail_unchanged(&before,&old_hw);
        if(!strcmp(kind,"gate") || !strcmp(kind,"core")) {
            assert(!request && !s_run_output.fast_calls);
            return;
        }
        assert(request==generation && s_run_output.fast_calls==1u);
    }
    if(!strcmp(kind,"wall_stale")) {
        ++s_run_output_request;
        vdc_run_output_note_cached_wall_core1(request,30000u,20000u);
        assert(!s_run_output.fast_wall_samples && !s_run_output.fast_wall_max_cycles);
    } else if(!strcmp(kind,"wall_busy")) {
        s_run_output_busy=1u;
        vdc_run_output_note_cached_wall_core1(request,30000u,20000u);
        assert(!s_run_output.fast_wall_samples && s_run_output.fast_calls==1u);
    } else if(!strcmp(kind,"wall") || !strcmp(kind,"saturate")) {
        vdc_run_output_note_cached_wall_core1(request,20000u,20000u);
        vdc_run_output_note_cached_wall_core1(request,20001u,20000u);
        assert(s_run_output.fast_wall_max_cycles==20001u);
        assert(s_run_output.fast_wall_samples==(!strcmp(kind,"saturate")?UINT32_MAX:2u));
        assert(s_run_output.fast_budget_overruns==(!strcmp(kind,"saturate")?UINT32_MAX:1u));
    }
}
int main(int argc,char **argv)
{
    assert(argc==2); const char *test=argv[1]; initialize();
    if(!strncmp(test,"diag_",5u)) { diagnostic(test+5u);return 0; }
    if(!strncmp(test,"prefetch_",9u)) { prefetch_case(test+9u);return 0; }
    if(!strncmp(test,"fast_",5u)) { fast_case(test+5u);return 0; }
    if(!strcmp(test,"no_request")) {
        core=1u; vdc_run_output_service_core1();
        assert(service_calls==1u && !s_run_output_request && !s_run_output_busy);
        assert(!snapshot_calls && !submit_calls && !prepare_calls && !cancels); return 0;
    }
    if(!strcmp(test,"prepare_interleave"))hook=2u;
    prepare();
    if(!strcmp(test,"prepare_interleave")) { assert(hook_seen==1u); return 0; }
    if(!strcmp(test,"pre_arm_wait")) {
        core=1u; vdc_run_output_service_core1(); assert(!submit_calls && !cancels); return 0;
    }
    if(!strcmp(test,"unseen_arm_stop")) {
        ring.config_seq=ring.applied_config_seq=4u; core=1u;
        vdc_run_output_service_core1(); finish_cancel(); return 0;
    }
    if(!strcmp(test,"gate_busy")) {
        s_run_output_busy=1u; core=1u; vdc_run_output_service_core1();
        assert(!service_calls); return 0;
    }
    if(!strcmp(test,"core_guard")) {
        vdc_run_output_service_core1(); assert(!service_calls); return 0;
    }
    arm(false); vdc_run_output_service_core1(); assert(!submit_calls && !cancels);
    if(!strcmp(test,"arm_wait"))return 0;
    if(!strcmp(test,"pre_first_stop")) {
        ring.enabled=ring.adapter_started=0u; ring.config_seq=ring.applied_config_seq=4u;
        vdc_run_output_service_core1(); finish_cancel(); return 0;
    }
    if(!strncmp(test,"first_",6u)) {
        vdc_dpll_manager_committed_model_t saved=model;
        if(!strcmp(test,"first_session"))++model.session;
        if(!strcmp(test,"first_role"))++model.role_generation;
        if(!strcmp(test,"first_epoch"))++model.clock_epoch_id;
        if(!strcmp(test,"first_run"))++model.clock_run_id;
        if(!strcmp(test,"first_slot"))++model.local_slot;
        if(!strcmp(test,"first_schedule"))++model.dco.tdma_schedule_crc32;
        if(!strcmp(test,"first_invalid"))model.dco.valid=0u;
        ring.data_enabled=1u; vdc_run_output_service_core1(); assert(!submit_calls && !cancels);
        model=saved; vdc_run_output_service_core1(); assert(submit_calls==1u); return 0;
    }
    if(!strcmp(test,"stop_during_plan"))hook=4u;
    if(!strcmp(test,"session_during_plan"))hook=5u;
    ring.data_enabled=1u; vdc_run_output_service_core1();
    if(hook==4u || hook==5u) { assert(!submit_calls && cancels); return 0; }
    assert(submit_calls==1u && s_run_output.blocks_planned==1u);
    if(!strncmp(test,"busy_",5u)) {
        ready=false;
        if(!strcmp(test,"busy_session"))++session;
        if(!strcmp(test,"busy_role"))++model.role_generation;
        if(!strcmp(test,"busy_epoch"))++model.clock_epoch_id;
        if(!strcmp(test,"busy_run"))++model.clock_run_id;
        if(!strcmp(test,"busy_slot"))++model.local_slot;
        if(!strcmp(test,"busy_schedule"))++model.dco.tdma_schedule_crc32;
        if(!strcmp(test,"busy_invalid"))model.dco.valid=0u;
        if(!strcmp(test,"busy_owner_without_model")) {
            model_available=false; ++s_vdc_domain.clock.epoch_id;
        }
        vdc_run_output_service_core1(); assert(submit_calls==1u); finish_cancel(); return 0;
    }
    if(!strcmp(test,"retire_interleave")) {
        hook=1u; vdc_run_output_service_core1(); assert(hook_seen==1u && !release_calls);
        core=0u; run_output_release_core0(); assert(release_calls==1u && !s_run_output_request);
        ring.enabled=ring.adapter_started=ring.data_enabled=0u;
        prepare(); assert(generation==2u && !cancelled); return 0;
    }
    if(!strcmp(test,"wrong_generation")) {
        ++hardware.generation; hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED; core=0u;
        run_output_release_core0(); assert(!release_calls && s_run_output_request); return 0;
    }
    if(!strcmp(test,"status_interleave")) {
        hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED; core=0u;
        snapshot_calls=0u; hook=3u;
        vdc_run_output_status_t out; assert(vdc_run_output_status(&out));
        assert(hook_seen==1u && out.blocks_planned==1u); return 0;
    }
    assert(!strcmp(test,"model_tail") || !strcmp(test,"delay_latched"));
    sync_io_run_output_edge_t prefix[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES];
    memcpy(prefix,admitted[0],sizeof(prefix));
    ready=false; ++model.token; model.dco.phase_offset_ns=2000;
    delay=50000; vdc_run_output_service_core1(); assert(submit_calls==1u && !cancels);
    assert(!memcmp(prefix,admitted[0],sizeof(prefix)));
    ready=true; vdc_run_output_service_core1(); assert(submit_calls==2u && !cancels);
    assert(s_run_output.delay_ns==100);
    assert(!memcmp(prefix,admitted[0],sizeof(prefix)));
    for(unsigned i=0;i<SYNC_IO_RUN_OUTPUT_BLOCK_EDGES;++i) {
        assert(admitted[1][i].model_token==model.token);
        assert(admitted[1][i].ordinal>prefix[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES-1u].ordinal);
        assert(admitted[1][i].rising_tick>prefix[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES-1u].falling_tick);
        /* Independent zero-rate oracle: F(local)=local+2000; physical
         * delay remains +100, not the newly requested +50000. Bridge raw
         * upper sample is local/4+2, and the enclosure adds one tick. */
        const uint64_t local=admitted[1][i].ordinal*UINT64_C(1000000)-2000u+100u;
        assert(admitted[1][i].rising_tick==local/4u+3u);
        assert(admitted[1][i].falling_tick-admitted[1][i].rising_tick==250u);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def parser_host(tmp_path_factory):
    source = (ROOT / 'middleware/scpi_port/src/scpi_system_snapshot_commands.c').read_text(encoding='utf-8')
    callbacks = source[source.index('static bool scpi_vdc_run_u32('):
                       source.index('scpi_result_t scpi_cmd_vdc_fixed_output(')]
    library = ROOT / 'third_party/scpi-parser/libscpi/src'
    return compile_host(tmp_path_factory.mktemp('vdc-run-parser'), 'parser',
                        PARSER_PREFIX + callbacks + PARSER_MAIN,
                        sorted(library.glob('*.c')), parser=True)


@pytest.mark.parametrize('command', [
    'RUN 1000000,1000,1000', 'RUN +1000000,+1000,+1000',
    'RUN 0001000000,001000,1000', 'RUN 1000000, 1000, 1000', 'STOP',
])
def test_real_parser_accepts_complete_commands(parser_host, command):
    result = subprocess.run([str(parser_host), command, 'accept'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('command', [
    'RUN', 'RUN 1000000', 'RUN 1000000,1000', 'RUN 1000000,1000,1000,1',
    'RUN 1000000,1000,1000,', 'RUN 1000000,1000,1000 1',
    'RUN -1000000,1000,1000', 'RUN 1000000,-1000,1000', 'RUN 1000000,1000,-1',
    'RUN 4294967296,1000,1000', 'RUN 1000000,4294967296,1000',
    'RUN 1000000,1000,4294967296', 'RUN 999999999999999999999999999999,1000,1000',
    'RUN 1e6,1000,1000', 'RUN 1000000.0,1000,1000', 'RUN 1000000ns,1000,1000',
    'RUN #Hf4240,1000,1000', 'RUN "1000000",1000,1000',
    'RUN 1000000,1000,+', 'RUN 1000000,1000,-', 'RUN 1000000,1000,abc',
    'RUN 1000000 , 1000 , 1000', 'RUN 1000000 ,1000,1000',
    'RUN 1000000,1000 ,1000',
    'STOP 1', 'STOP ,', 'STOP abc',
])
def test_real_parser_rejects_without_side_effects(parser_host, command):
    result = subprocess.run([str(parser_host), command, 'reject'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_real_parser_exports_start_observation_receipt(parser_host):
    result = subprocess.run([str(parser_host), 'RUN?', 'query'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    fields = [int(value) for value in result.stdout.strip().split(',')]
    assert len(fields) == 113
    # Preserve every old position: 26 small fields, ten uint64, two config.
    assert fields[:36] == [8] + [0] * 35
    assert fields[36:43] == [20, 21, 13, 12, 3, 4294967303, 4294967311]
    assert fields[43:50] == list(range(4294967400, 4294967407))
    assert fields[50:59] == list(range(101, 110))
    assert fields[59:61] == [1, 5]
    assert fields[61:85] == list(range(200, 212)) + list(range(300, 312))
    assert fields[85:105] == list(range(401, 421))
    assert fields[105:107] == [1, 500]
    assert fields[107:110] == [0, 0, 0]
    assert fields[110:112] == [0, 0]
    assert fields[112:] == [0]


PARSER_PREFIX = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "scpi/scpi.h"
#include "vdc_run_output.h"
static unsigned prepares, cancels, writes;
static char response[4096];
static size_t response_size;
static size_t output(scpi_t *context,const char *data,size_t length)
{
    (void)context; ++writes; assert(response_size+length<sizeof(response));
    memcpy(response+response_size,data,length);response_size+=length;response[response_size]=0;
    return length;
}
static scpi_result_t flush(scpi_t *context) { (void)context; return SCPI_RES_OK; }
static int error(scpi_t *context,int_fast16_t value) { (void)context; (void)value; return 0; }
static void scpi_port_push_exec_error(scpi_t *context,const char *message)
{ (void)message; SCPI_ErrorPush(context,SCPI_ERROR_EXECUTION_ERROR); }
static scpi_result_t scpi_port_result_ok(scpi_t *context)
{ SCPI_ResultText(context,"OK"); return SCPI_RES_OK; }
bool vdc_run_output_prepare(uint32_t period,uint32_t high,uint32_t duration,uint32_t *request)
{
    ++prepares;
    fprintf(stderr,"prepare %lu,%lu,%lu\n",(unsigned long)period,(unsigned long)high,(unsigned long)duration);
    assert(period==1000000u && high==1000u && duration==1000u);
    *request=7u; return true;
}
void vdc_run_output_cancel(void) { ++cancels; }
bool vdc_run_output_status(vdc_run_output_status_t *out)
{
    memset(out,0,sizeof(*out));out->prepared_config=20u;out->arm_config=21u;
    out->hardware.schema=SYNC_IO_RUN_OUTPUT_SCHEMA;
    out->hardware.start_pc=13u;out->hardware.program_offset=12u;out->hardware.start_raw_flags=3u;
    out->hardware.start_raw_observed=UINT64_C(4294967303);
    out->hardware.start_raw_after=UINT64_C(4294967311);
    out->hardware.service_last_tick=UINT64_C(4294967400);
    out->hardware.service_last_gap_ticks=UINT64_C(4294967401);
    out->hardware.service_max_gap_ticks=UINT64_C(4294967402);
    out->hardware.submit_last_tick=UINT64_C(4294967403);
    out->hardware.submit_max_gap_ticks=UINT64_C(4294967404);
    out->hardware.refill_min_margin_ticks=UINT64_C(4294967405);
    out->hardware.retire_raw_tick=UINT64_C(4294967406);
    out->hardware.service_observations=101u;out->hardware.submit_service_observation=102u;
    out->hardware.retire_raw_valid=103u;out->hardware.retire_pc=104u;
    out->hardware.retire_fstat=105u;out->hardware.retire_fdebug=106u;
    out->hardware.retire_dma_ctrl=107u;out->hardware.retire_dma_remaining=108u;
    out->hardware.retire_pio_ctrl=109u;
    out->last_phase=VDC_RUN_OUTPUT_RUNNING_PHASE;out->last_outcome=VDC_RUN_OUTPUT_BRIDGE_UNAVAILABLE;
    out->prefetched_blocks=401u;out->cache_hits=402u;out->cache_invalidations=403u;
    out->fast_calls=404u;out->fast_submissions=405u;out->fast_empty=406u;
    out->fast_body_max_us=407u;out->fast_wall_samples=408u;
    out->fast_wall_max_cycles=409u;out->fast_budget_overruns=410u;
    out->plan_ahead_us=411u;out->commit_ahead_us=412u;out->refill_low_us=413u;
    out->timeline_bridge_samples=414u;out->partial_plan_steps=415u;
    out->plan_waits=416u;out->refill_waits=417u;out->commit_waits=418u;
    out->block_edges=419u;out->schedule_cycles=420u;
    out->hardware.fifo_words_per_edge=1u;out->hardware.fixed_high_ticks=500u;
    for(unsigned p=0;p<VDC_RUN_OUTPUT_PHASE_COUNT;++p)
        for(unsigned o=0;o<VDC_RUN_OUTPUT_OUTCOME_COUNT;++o)out->outcomes[p][o]=200u+p*100u+o;
    return true;
}
'''


PARSER_MAIN = r'''
static const scpi_command_t commands[]={
    {.pattern="RUN",.callback=scpi_cmd_vdc_run_output},
    {.pattern="RUN?",.callback=scpi_cmd_vdc_run_output_q},
    {.pattern="STOP",.callback=scpi_cmd_vdc_run_output_stop}, SCPI_CMD_LIST_END
};
int main(int argc,char **argv)
{
    assert(argc==3); char input[1024],line[1024]; scpi_t context; scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&context,commands,&interface,scpi_units_def,"a","b","c","d",
        input,sizeof(input),errors,16);
    const int length=snprintf(line,sizeof(line),"%s\n",argv[1]);
    assert(length>0 && (size_t)length<sizeof(line));
    const bool parsed=SCPI_Input(&context,line,length);
    if(!strcmp(argv[2],"query")) {
        assert(parsed && !SCPI_ErrorCount(&context) && writes && !prepares && !cancels);
        printf("%s",response);
    } else if(!strcmp(argv[2],"accept")) {
        assert(parsed && !SCPI_ErrorCount(&context) && writes);
        if(!strncmp(argv[1],"RUN",3u))assert(prepares==1u && !cancels);
        else assert(!prepares && cancels==1u);
    } else {
        assert(!prepares && !cancels && !writes && SCPI_ErrorCount(&context)>0);
    }
    return 0;
}
'''
