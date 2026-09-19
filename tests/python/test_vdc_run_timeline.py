"""Production RUN client timelines and integer clock-enclosure boundaries.

The backend is observable hardware bookkeeping, not a PIO emulator. These
tests qualify admission arithmetic and ownership, not waveform continuity.
"""
import random
import subprocess

import pytest

from test_vdc_run_output import CLIENT_MAIN, CLIENT_PREFIX, ROOT, compile_host


@pytest.fixture(scope='module')
def timeline_client(tmp_path_factory):
    prefix = CLIENT_PREFIX.replace('admitted[4]', 'admitted[2100]')
    prefix = prefix.replace('admitted_count[4]', 'admitted_count[2100]')
    prefix = prefix.replace('submit_calls<4u', 'submit_calls<2100u')
    prefix = prefix.replace('duration==1000u', 'duration>=1u && duration<=20000u')
    prefix = prefix.replace('static bool ready=true',
        'static bool finish_dma_on_observation; static unsigned ready_observations;\nstatic bool ready=true')
    prefix = prefix.replace('{ return request==hardware.generation && ready && !cancelled; }',
        '{ ++ready_observations; if(finish_dma_on_observation) { '
        'finish_dma_on_observation=false;ready=true;return false; } '
        'return request==hardware.generation && ready && !cancelled; }')
    source = (ROOT / 'components/vdc_dpll_manager/src/vdc_run_output.inc').read_text(encoding='utf-8')
    return compile_host(tmp_path_factory.mktemp('timeline-client'), 'timeline',
        prefix + source + '\n#define main inherited_main\n' + CLIENT_MAIN +
        '\n#undef main\n' + TIMELINE_MAIN,
        [ROOT / 'components/vdc_domain/src/vdc_domain.c',
         ROOT / 'components/vdc_domain/src/vdc_timestamp.c',
         ROOT / 'components/tdma/src/tdma_profile.c'])


@pytest.mark.parametrize('scenario', [
    'default', 'count_zero', 'count_over', 'schedule_missing', 'schedule_zero',
    'schedule_too_slow', 'plan_capacity', 'low_capacity', 'latched',
    'table_prepared', 'table_running', 'bridge_jitter', 'long_wrap',
    'coordinate_expired', 'coordinate_rewind', 'coordinate_deadline',
    'batch_model', 'watermarks', 'failed_time', 'horizon',
    'first_stop', 'first_cancel', 'cached_pristine', 'dma_finishes_during_plan',
])
def test_actual_client_timeline(timeline_client, scenario):
    result = subprocess.run([str(timeline_client), scenario], capture_output=True,
                            text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.fixture(scope='module')
def continuous_client(tmp_path_factory):
    # Keep only four recent admitted blocks, even over hundreds of seconds.
    prefix = CLIENT_PREFIX.replace('duration==1000u', 'duration<=20000u')
    prefix = prefix.replace(' && submit_calls<4u', '')
    prefix = prefix.replace('admitted[submit_calls]', 'admitted[submit_calls%4u]')
    prefix = prefix.replace('admitted_count[submit_calls++]=count;',
                            'admitted_count[submit_calls%4u]=count;++submit_calls;')
    source = (ROOT / 'components/vdc_dpll_manager/src/vdc_run_output.inc').read_text(encoding='utf-8')
    return compile_host(tmp_path_factory.mktemp('continuous-client'), 'continuous',
        prefix + source + '\n#define main inherited_main\n' + CLIENT_MAIN +
        '\n#undef main\n' + CONTINUOUS_MAIN,
        [ROOT / 'components/vdc_domain/src/vdc_domain.c',
         ROOT / 'components/vdc_domain/src/vdc_timestamp.c',
         ROOT / 'components/tdma/src/tdma_profile.c'])


@pytest.mark.parametrize('scenario', ['600', 'stop', 'cancel', 'session', 'epoch',
                                    'clock', 'rollback', 'saturation', 'overflow'])
def test_continuous_actual_planner(continuous_client, scenario):
    result = subprocess.run([str(continuous_client), scenario], capture_output=True,
                            text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr


CONTINUOUS_MAIN = r'''
int main(int argc,char **argv)
{
    assert(argc==2);initialize();const char *kind=argv[1];
    timing=(vdc_output_timing_profile_t){24000u,32000u,16000u};
    uint32_t request=0u;assert(vdc_run_output_prepare(1000000u,1000u,0u,&request));arm(true);
    const unsigned blocks=!strcmp(kind,"600")?37502u:2100u;
    uint64_t previous_ordinal=0u;unsigned low_wraps=0u;
    uint32_t previous_hi=0u;
    for (unsigned b=0;b<blocks;++b) {
        if(b)raw_override=hardware.last_falling_tick-4000000u;
        for(unsigned step=0;step<4u && submit_calls==b;++step)vdc_run_output_service_core1();
        assert(submit_calls==b+1u && !cancelled);
        const unsigned retained=b%4u;assert(admitted_count[retained]==16u);
        for(unsigned i=0;i<16u;++i) {
            const sync_io_run_output_edge_t *e=&admitted[retained][i];
            if(b||i)assert(e->ordinal==previous_ordinal+1u);
            assert(e->rising_tick==(e->ordinal*UINT64_C(1000000)+100u+3u)/4u);
            assert(e->falling_tick-e->rising_tick==250u);previous_ordinal=e->ordinal;
        }
        const uint32_t hi=(uint32_t)(hardware.last_falling_tick>>32u);
        if(hi>previous_hi)low_wraps+=hi-previous_hi;
        previous_hi=hi;
    }
    assert(hardware.last_falling_tick-s_run_output.initial_raw_tick>UINT64_C(32)*BOARD_SYS_CLOCK_HZ);
    if(!strcmp(kind,"600")) {
        assert(hardware.last_falling_tick-s_run_output.initial_raw_tick>UINT64_C(600)*BOARD_SYS_CLOCK_HZ);
        assert(low_wraps>=34u);
    }
    assert(!bridge_calls && !s_run_output.timeline_bridge_samples);
    raw_override=hardware.last_falling_tick-4000000u;
    if(!strcmp(kind,"saturation")) {
        s_run_output.blocks_planned=UINT32_MAX;s_run_output.fast_submissions=UINT32_MAX;
        s_run_output.partial_plan_steps=UINT32_MAX;
        vdc_run_output_service_core1();
        assert(submit_calls==blocks+1u && s_run_output.blocks_planned==UINT32_MAX &&
            s_run_output.partial_plan_steps==UINT32_MAX);
    } else if(!strcmp(kind,"rollback")) {
        raw_override=s_run_output_last_raw_tick-1u;
        assert(raw_override>s_run_output.initial_raw_tick);vdc_run_output_service_core1();assert(cancelled);
    } else if(!strcmp(kind,"overflow")) {
        raw_override=UINT64_MAX;vdc_run_output_service_core1();assert(cancelled);
    } else if(!strcmp(kind,"session")) {++session;vdc_run_output_service_core1();assert(cancelled);}
    else if(!strcmp(kind,"epoch")) {++s_vdc_domain.clock.epoch_id;vdc_run_output_service_core1();assert(cancelled);}
    else if(!strcmp(kind,"clock")) {clock_supported=false;vdc_run_output_service_core1();assert(cancelled);}
    else if(!strcmp(kind,"stop")) {ring.enabled=0u;++ring.config_seq;vdc_run_output_service_core1();assert(cancelled);}
    else {vdc_run_output_cancel();vdc_run_output_service_core1();assert(cancelled);}
    if(!cancelled)vdc_run_output_cancel();
    core=1u;vdc_run_output_service_core1();core=0u;run_output_release_core0();
    assert(!s_run_output_request && !s_run_output_coordinate_valid);
    ring.enabled=ring.adapter_started=ring.data_enabled=0u;
    ring.config_seq=ring.applied_config_seq=7u;clock_supported=true;raw_override=0u;
    model.session=session;model.clock_epoch_id=s_vdc_domain.clock.epoch_id;
    const uint32_t old_request=request;
    assert(vdc_run_output_prepare(1000000u,1000u,1000u,&request));
    assert(request!=old_request && s_run_output.duration_ms==1000u && !s_run_output.blocks_planned &&
        !s_run_output_last_raw_tick && !s_run_output_pending.valid);
    return 0;
}
'''


@pytest.fixture(scope='module')
def late_model_client(tmp_path_factory):
    source = (ROOT / 'components/vdc_dpll_manager/src/vdc_run_output.inc').read_text(encoding='utf-8')
    return compile_host(tmp_path_factory.mktemp('late-model-client'), 'late_model',
        CLIENT_PREFIX + source + '\n#define main inherited_main\n' + CLIENT_MAIN +
        '\n#undef main\n' + LATE_MODEL_MAIN,
        [ROOT / 'components/vdc_domain/src/vdc_domain.c',
         ROOT / 'components/vdc_domain/src/vdc_timestamp.c',
         ROOT / 'components/tdma/src/tdma_profile.c'])


@pytest.mark.parametrize('change', ['phase', 'rate'])
@pytest.mark.parametrize('cached_passes', [1, 2])
def test_late_model_replan_survives_skipped_full_service(late_model_client, change, cached_passes):
    result = subprocess.run([str(late_model_client), change, str(cached_passes)],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


LATE_MODEL_MAIN = r'''
int main(int argc,char **argv)
{
    assert(argc==3); initialize();
    timing=(vdc_output_timing_profile_t){40000u,48000u,32000u};
    prepare();arm(true);
    for(unsigned i=0;i<4u;++i)vdc_run_output_service_core1();
    assert(submit_calls==1u && admitted_count[0]==16u);
    sync_io_run_output_edge_t prefix[16];memcpy(prefix,admitted[0],sizeof(prefix));
    const uint64_t tail=hardware.last_falling_tick;
    ready=false;raw_override=tail-UINT64_C(3000000); /* 12 ms before tail. */
    for(unsigned i=0;i<4u;++i)vdc_run_output_service_core1();
    assert(s_run_output_pending.valid && submit_calls==1u);
    ++model.token;
    if(!strcmp(argv[1],"phase"))model.dco.phase_offset_ns=60000;
    else { assert(!strcmp(argv[1],"rate"));model.dco.period_adjust_ppb=500; }
    raw_override=tail-UINT64_C(1687500); /* 6.75 ms runway at invalidation. */
    ready=true;
    const unsigned skips=(unsigned)atoi(argv[2]);assert(skips==1u || skips==2u);
    for(unsigned pass=0;pass<5u && submit_calls==1u;++pass) {
        assert(raw_override<tail);
        if(pass && pass<=skips)(void)vdc_run_output_service_cached_core1();
        else vdc_run_output_service_core1();
        if(submit_calls==2u)break;
        raw_override+=375000u; /* Actual 1.5 ms table time, including skips. */
    }
    /* No admission after an exhausted tail can count as successful recovery. */
    assert(submit_calls==2u && raw_override+20000u<tail && !cancelled);
    assert(!memcmp(prefix,admitted[0],sizeof(prefix)));
    assert(s_run_output.cache_invalidations==1u);
    uint64_t previous_local=prefix[15].ordinal*UINT64_C(1000000)+100u+1000u;
    uint64_t first_unqueued=prefix[15].ordinal+1u;
    for(unsigned i=0;i<16u;++i) {
        vdc_output_edge_plan_t scalar;
        assert(vdc_output_edge_plan(&model.dco,0u,1000000u,first_unqueued,
                                   previous_local,100,&scalar));
        assert(admitted[1][i].model_token==model.token);
        assert(admitted[1][i].ordinal==scalar.ordinal);
        assert(admitted[1][i].rising_tick==(scalar.physical_local_ns+3u)/4u);
        previous_local=scalar.physical_local_ns+1000u;first_unqueued=scalar.ordinal+1u;
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def planned_fallback_client(tmp_path_factory):
    # The production client/planner is unchanged. This backend seam retires
    # when the admitted tail has passed; it does not emulate PIO or DMA.
    prefix = CLIENT_PREFIX.replace('    ++service_calls;', '''    ++service_calls;
    if(hardware.state==SYNC_IO_RUN_OUTPUT_RUNNING && raw_override) {
        hardware.service_last_tick=raw_override;
        ++hardware.service_observations;
        if(raw_override>hardware.last_falling_tick) {
            hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED;
            hardware.reason=SYNC_IO_RUN_OUTPUT_STARVED;
        }
    }''')
    source = (ROOT / 'components/vdc_dpll_manager/src/vdc_run_output.inc').read_text(encoding='utf-8')
    return compile_host(tmp_path_factory.mktemp('planned-fallback-client'), 'planned_fallback',
        prefix + source + '\n#define main inherited_main\n' + CLIENT_MAIN +
        '\n#undef main\n' + PLANNED_FALLBACK_MAIN,
        [ROOT / 'components/vdc_domain/src/vdc_domain.c',
         ROOT / 'components/vdc_domain/src/vdc_timestamp.c',
         ROOT / 'components/tdma/src/tdma_profile.c'])


@pytest.mark.parametrize('change', ['phase', 'rate'])
@pytest.mark.parametrize('mode', ['cached_only', 'planned_first', 'planned_after_cached',
                                 'unchanged', 'dma_busy'])
def test_planned_fallback_recovers_invalidated_cache(planned_fallback_client, change, mode):
    result = subprocess.run([str(planned_fallback_client), mode, change],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('mode', ['first_block', 'no_request', 'wrong_core', 'busy',
                                  'wall', 'wall_stale', 'wall_busy', 'wall_core',
                                  'wall_zero', 'saturate', 'cancel'])
def test_planned_fallback_accounting_and_owner(planned_fallback_client, mode):
    result = subprocess.run([str(planned_fallback_client), mode, 'phase'],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


PLANNED_FALLBACK_MAIN = r'''
static void assert_current_suffix(const sync_io_run_output_edge_t prefix[16])
{
    uint64_t previous_local=prefix[15].falling_tick*4u;
    uint64_t next=prefix[15].ordinal+1u;
    for(unsigned i=0;i<16u;++i) {
        vdc_output_edge_plan_t scalar;
        assert(vdc_output_edge_plan(&model.dco,0u,1000000u,next,previous_local,100,&scalar));
        assert(admitted[1][i].model_token==model.token);
        assert(admitted[1][i].ordinal==scalar.ordinal);
        assert(admitted[1][i].rising_tick==(scalar.physical_local_ns+3u)/4u);
        previous_local=scalar.physical_local_ns+1000u;next=scalar.ordinal+1u;
    }
}
int main(int argc,char **argv)
{
    assert(argc==3);initialize();const char *mode=argv[1];
    timing=(vdc_output_timing_profile_t){24000u,32000u,16000u};
    if(!strcmp(mode,"no_request")) {
        core=1u;assert(!vdc_run_output_service_planned_core1());
        assert(!s_run_output.planned_calls && !s_run_output.planned_rebuilds &&
               !s_run_output.planned_submissions && !submit_calls);
        return 0;
    }
    prepare();arm(true);
    if(!strcmp(mode,"wrong_core") || !strcmp(mode,"busy")) {
        if(!strcmp(mode,"wrong_core"))core=0u;else s_run_output_busy=1u;
        const unsigned services=service_calls;
        assert(!vdc_run_output_service_planned_core1());
        assert(service_calls==services && !s_run_output.planned_calls && !s_run_output.planned_rebuilds);
        return 0;
    }
    if(!strcmp(mode,"first_block")) {
        for(unsigned i=0;i<4u;++i) {
            assert(vdc_run_output_service_planned_core1()==generation);
            assert(s_run_output.planned_calls==i+1u && s_run_output.planned_rebuilds==i+1u);
            assert(submit_calls==(i==3u?1u:0u));
        }
        assert(s_run_output.planned_submissions==1u && !s_run_output.fast_calls);
        return 0;
    }
    for(unsigned i=0;i<4u;++i)vdc_run_output_service_core1();
    assert(submit_calls==1u && admitted_count[0]==16u);
    const uint64_t old_tail=hardware.last_falling_tick;
    sync_io_run_output_edge_t prefix[16];memcpy(prefix,admitted[0],sizeof(prefix));
    ready=false;raw_override=old_tail-2500000u;
    vdc_run_output_service_core1();
    assert(s_run_output_pending.valid && s_run_output.last_outcome==VDC_RUN_OUTPUT_DMA_NOT_READY);
    assert(!s_run_output.planned_calls && !s_run_output.planned_rebuilds);
    if(strcmp(mode,"unchanged")) {
        ++model.token;
        if(!strcmp(argv[2],"phase"))model.dco.phase_offset_ns=60;
        else { assert(!strcmp(argv[2],"rate"));model.dco.period_adjust_ppb=500; }
    }
    ready=true;raw_override=old_tail-1875000u; /* 7.5 ms, then 1.5 ms calls. */
    if(!strcmp(mode,"cancel")) {
        vdc_run_output_cancel();
        assert(vdc_run_output_service_planned_core1()==generation);
        assert(hardware.state==SYNC_IO_RUN_OUTPUT_RETIRED && submit_calls==1u);
        assert(s_run_output.planned_calls==1u && !s_run_output.planned_rebuilds &&
               !s_run_output.planned_submissions && !s_run_output_pending.valid);
        return 0;
    }
    if(!strncmp(mode,"wall",4u) || !strcmp(mode,"saturate")) {
        const uint32_t budget=100000u;
        if(!strcmp(mode,"saturate")) {
            s_run_output.planned_calls=s_run_output.planned_rebuilds=UINT32_MAX;
            s_run_output.planned_submissions=s_run_output.planned_wall_samples=UINT32_MAX;
            s_run_output.planned_budget_overruns=UINT32_MAX;
        }
        const uint32_t request=vdc_run_output_service_planned_core1();
        assert(request==generation && submit_calls==2u);
        const bool saturation=!strcmp(mode,"saturate");
        assert(s_run_output.planned_calls==(saturation?UINT32_MAX:1u));
        assert(s_run_output.planned_rebuilds==(saturation?UINT32_MAX:1u));
        assert(s_run_output.planned_submissions==(saturation?UINT32_MAX:1u));
        const bool rejected=strcmp(mode,"wall") && !saturation;
        if(!strcmp(mode,"wall_stale"))++s_run_output_request;
        if(!strcmp(mode,"wall_busy"))s_run_output_busy=1u;
        if(!strcmp(mode,"wall_core"))core=0u;
        const uint32_t report_request=!strcmp(mode,"wall_zero")?0u:request;
        vdc_run_output_note_planned_wall_core1(report_request,budget,budget);
        vdc_run_output_note_planned_wall_core1(report_request,budget+1u,budget);
        assert(s_run_output.planned_wall_samples==(saturation?UINT32_MAX:rejected?0u:2u));
        assert(s_run_output.planned_budget_overruns==(saturation?UINT32_MAX:rejected?0u:1u));
        assert(s_run_output.planned_wall_max_cycles==(rejected?0u:budget+1u));
        assert(!s_run_output.fast_calls && !s_run_output.fast_wall_samples);
        return 0;
    }
    for(unsigned i=0;i<7u;++i) {
        const bool planned=(!strcmp(mode,"planned_first") && i==0u) ||
            (!strcmp(mode,"planned_after_cached") && i==2u) ||
            (!strcmp(mode,"dma_busy") && i==0u);
        if(!strcmp(mode,"dma_busy"))ready=i!=0u;
        if(planned)assert(vdc_run_output_service_planned_core1()==generation);
        else assert(vdc_run_output_service_cached_core1()==generation);
        if(hardware.state==SYNC_IO_RUN_OUTPUT_RETIRED || submit_calls==2u)break;
        raw_override+=375000u;
    }
    assert(!memcmp(prefix,admitted[0],sizeof(prefix)) && !cancelled);
    if(!strcmp(mode,"cached_only")) {
        /* Negative control: backend services continue, but an empty cached
         * suffix makes no forward progress without a planning opportunity. */
        assert(submit_calls==1u && hardware.reason==SYNC_IO_RUN_OUTPUT_STARVED);
        assert(s_run_output.fast_empty>=5u && s_run_output.cache_invalidations==1u);
        assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_DMA_NOT_READY);
        assert(!s_run_output.planned_calls && !s_run_output.planned_rebuilds);
        return 0;
    }
    assert(submit_calls==2u && hardware.state==SYNC_IO_RUN_OUTPUT_RUNNING);
    assert(raw_override+20000u<old_tail); /* Admission preceded tail expiry. */
    if(!strcmp(mode,"unchanged")) {
        assert(!s_run_output.cache_invalidations && !s_run_output.planned_calls);
        assert(s_run_output.fast_submissions==1u);
    } else {
        assert(s_run_output.cache_invalidations==1u && s_run_output.planned_calls==1u);
        assert(s_run_output.planned_rebuilds==1u);
        assert(s_run_output.planned_submissions==(!strcmp(mode,"dma_busy")?0u:1u));
        assert(s_run_output.fast_submissions==(!strcmp(mode,"dma_busy")?1u:0u));
    }
    assert_current_suffix(prefix);
    return 0;
}
'''


TIMELINE_MAIN = r'''
static void step(void) { vdc_run_output_service_core1(); }
static void begin(void) { prepare(); arm(true); }
static void initial_block(void)
{
    step(); assert(!submit_calls && s_run_output_pending.planned==4u);
    step(); assert(!submit_calls && s_run_output_pending.planned==8u);
    step(); assert(submit_calls==1u && admitted_count[0]==10u);
    assert(s_run_output.partial_plan_steps==3u && !s_run_output_pending.planned);
}
static void assert_edges(unsigned block,int64_t phase)
{
    assert(admitted_count[block]==10u);
    for(unsigned i=0;i<10u;++i) {
        const sync_io_run_output_edge_t *e=&admitted[block][i];
        /* Independent zero-frequency model: local = ordinal*1 ms-phase+delay.
         * TIMER1's known rate gives raw = ceil(local/4). */
        const uint64_t local=e->ordinal*UINT64_C(1000000)-phase+100u;
        assert(e->rising_tick==(local+3u)/4u);
        assert(e->falling_tick==e->rising_tick+250u);
        if(i)assert(e->ordinal==admitted[block][i-1u].ordinal+1u);
        if(block)assert(e->rising_tick>admitted[block-1u][9].falling_tick);
    }
}
int main(int argc,char **argv)
{
    assert(argc==2); const char *kind=argv[1]; initialize();
    timing=(vdc_output_timing_profile_t){12000u,16000u,6000u};
    if(!strncmp(kind,"count_",6u) || !strncmp(kind,"schedule_",9u) ||
       !strcmp(kind,"plan_capacity") || !strcmp(kind,"low_capacity")) {
        uint32_t period=1000000u,request=0x12345678u;
        if(!strcmp(kind,"count_zero"))period=100000000u;
        else if(!strcmp(kind,"count_over"))timing.commit_ahead_us=23000u;
        else if(!strcmp(kind,"schedule_missing"))schedule_ok=false;
        else if(!strcmp(kind,"schedule_zero"))table_cycles=0u;
        else if(!strcmp(kind,"schedule_too_slow"))++table_cycles;
        else if(!strcmp(kind,"plan_capacity"))--timing.plan_ahead_us;
        else if(!strcmp(kind,"low_capacity"))
            timing=(vdc_output_timing_profile_t){8000u,9000u,1000u};
        else assert(false);
        assert(!vdc_run_output_prepare(period,1000u,1000u,&request));
        assert(request==0x12345678u && !prepare_calls && !submit_calls && !cancels);
        assert(!s_run_output_request && !s_run_output_busy);return 0;
    }
    if(!strcmp(kind,"long_wrap")) {
        uint32_t request=0u;
        assert(vdc_run_output_prepare(1000000u,1000u,20000u,&request));arm(true);
    } else begin();
    if(!strcmp(kind,"cached_pristine")) {
        assert(vdc_run_output_service_cached_core1()==generation);
        assert(!bridge_calls && !s_run_output.partial_plan_steps && !submit_calls);
        assert(hardware.state==SYNC_IO_RUN_OUTPUT_PREPARED);return 0;
    }
    if(!strcmp(kind,"table_prepared")) {
        ++table_cycles;step();assert(cancelled && !submit_calls && !bridge_calls);return 0;
    }
    if(!strcmp(kind,"latched")) {
        timing=(vdc_output_timing_profile_t){1u,2u,3u};delay=50000;
        assert(s_run_output.plan_ahead_us==12000u && s_run_output.commit_ahead_us==16000u);
        assert(s_run_output.refill_low_us==6000u && s_run_output.block_edges==10u);
    }
    if(!strcmp(kind,"first_stop") || !strcmp(kind,"first_cancel")) {
        step();assert(s_run_output_pending.planned==4u && !submit_calls);
        if(!strcmp(kind,"first_stop")) { ring.enabled=0u;++ring.config_seq; }
        else vdc_run_output_cancel();
        step();assert(cancelled && !submit_calls && !s_run_output_pending.planned);
        assert(!s_run_output_coordinate_valid);return 0;
    }
    initial_block();assert_edges(0u,0);
    assert(!bridge_calls && !s_run_output.timeline_bridge_samples);
    assert(s_run_output.timebase==VDC_RUN_OUTPUT_TIMEBASE_TIMER1_NS);
    assert(s_run_output.initial_raw_tick==raw_clock && s_run_output.initial_local_ns==raw_clock*4u);
    if(!strcmp(kind,"default") || !strcmp(kind,"latched"))return 0;
    const sync_io_run_output_snapshot_t saved=hardware;
    sync_io_run_output_edge_t prefix[10];memcpy(prefix,admitted[0],sizeof(prefix));
    if(!strcmp(kind,"dma_finishes_during_plan")) {
        ready=false;finish_dma_on_observation=true;
        const unsigned before=ready_observations;
        step();
        assert(ready_observations==before+2u && submit_calls==2u && !cancelled);
        assert_edges(1u,0);
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));return 0;
    }
    if(!strcmp(kind,"table_running")) {
        ++table_cycles;step();assert(cancelled && submit_calls==1u);
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));return 0;
    }
    if(!strcmp(kind,"bridge_jitter")) {
        bridge.raw_before+=9000000u;bridge.raw_after+=19000000u;bridge.local_ns+=70000000u;
        raw_override=hardware.last_falling_tick-1500000u;
        bridge_available=false;
        step();step();step();assert(submit_calls==2u && !bridge_calls);
        assert_edges(1u,0);return 0;
    }
    if(!strcmp(kind,"long_wrap")) {
        bool passed_two=false,wrapped=false;
        for(unsigned n=1u;n<2000u;++n) {
            raw_override=hardware.last_falling_tick-1500000u;
            if(raw_override>UINT64_C(501000002))passed_two=true;
            if(raw_override>UINT32_MAX)wrapped=true;
            step();step();step();assert(submit_calls==n+1u && !cancelled);
            assert_edges(n,0);
        }
        assert(passed_two && wrapped && hardware.last_falling_tick>UINT64_C(5000000000));
        assert(!bridge_calls && !s_run_output.timeline_bridge_samples);
        assert(s_run_output.partial_plan_steps==3u+submit_calls-1u);return 0;
    }
    if(!strcmp(kind,"batch_model")) {
        ready=false;raw_override=hardware.last_falling_tick-1500000u;
        step();assert(s_run_output_pending.valid && s_run_output_pending.planned==10u);
        ++model.token;model.dco.phase_offset_ns=2000;
        step();assert(s_run_output_pending.valid && s_run_output_pending.planned==10u &&
                      s_run_output.cache_invalidations==1u && submit_calls==1u);
        assert(!memcmp(&saved,&hardware,sizeof(saved)));
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));
        ready=true;vdc_run_output_service_cached_core1();assert(submit_calls==2u);
        for(unsigned i=0;i<10u;++i)assert(admitted[1][i].model_token==model.token);
        assert_edges(1u,2000);assert(!bridge_calls);return 0;
    }
    if(!strcmp(kind,"watermarks")) {
        const uint64_t tail=hardware.last_falling_tick;
        raw_override=tail-3000001u;step();
        assert(s_run_output.plan_waits==1u && !s_run_output_pending.planned);
        raw_override=tail-3000000u;step();
        assert(s_run_output_pending.valid && submit_calls==1u);
        raw_override=tail-1500001u;vdc_run_output_service_cached_core1();
        assert(submit_calls==1u && s_run_output.refill_waits==2u);
        /* One tick of enable uncertainty must count against commitment,
         * without postponing planning/refill based on a later tail. */
        hardware.anchor_before=100u;hardware.anchor_after=101u;
        raw_override=tail-1500000u;vdc_run_output_service_cached_core1();
        assert(submit_calls==1u && s_run_output.commit_waits==1u);
        ++raw_override;vdc_run_output_service_cached_core1();
        assert(submit_calls==2u && !s_run_output_pending.valid);return 0;
    }
    if(!strcmp(kind,"failed_time")) {
        ready=false;
        raw_override=hardware.last_falling_tick-1500000u;
        step();assert(s_run_output_pending.valid);
        ready=true;
        raw_ok=false;step();
        assert(s_run_output_pending.valid && s_run_output_coordinate_valid);
        assert(!memcmp(&saved,&hardware,sizeof(saved)) && !bridge_calls);
        raw_ok=true;step();assert(submit_calls==2u && !bridge_calls);return 0;
    }
    if(!strcmp(kind,"horizon")) {
        raw_override=hardware.last_falling_tick-1500000u;
        hardware.anchor_after=2000000u; /* 8 ms enable uncertainty. */
        step();assert(s_run_output_pending.valid && submit_calls==1u && s_run_output.commit_waits==1u);
        const unsigned plans=s_run_output.partial_plan_steps;
        raw_ok=false;vdc_run_output_service_cached_core1();
        assert(submit_calls==1u && s_run_output_pending.valid && !bridge_calls);
        raw_ok=true;raw_override+=2000000u;vdc_run_output_service_cached_core1();
        assert(submit_calls==2u && s_run_output.partial_plan_steps==plans);
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));return 0;
    }
    if(!strncmp(kind,"coordinate_",11u)) {
        if(!strcmp(kind,"coordinate_expired"))raw_override=raw_clock+UINT64_C(8000000001);
        else if(!strcmp(kind,"coordinate_rewind"))raw_override=raw_clock-1u;
        else {
            assert(!strcmp(kind,"coordinate_deadline"));
            raw_override=raw_clock+UINT64_C(8000000000);
        }
        step();assert(submit_calls==1u && !s_run_output_pending.valid);
        assert(s_run_output.last_outcome==VDC_RUN_OUTPUT_PLAN_REJECTED);
        if(strcmp(kind,"coordinate_deadline"))assert(cancelled && !s_run_output_coordinate_valid);
        assert(!memcmp(prefix,admitted[0],sizeof(prefix)));return 0;
    }
    assert(false);return 1;
}
'''


HELPER_MAIN = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "vdc_future_raw.h"
int main(void)
{
    unsigned operation;uint64_t rb,ra,local,argument;uint32_t hz;
    while(scanf("%u %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu32" %"SCNu64,
        &operation,&rb,&ra,&local,&hz,&argument)==6) {
        const vdc_timestamp_clock_bridge_t b={.raw_before=rb,.raw_after=ra,
            .local_ns=local,.tick_hz=hz};
        vdc_local_raw_bracket_t out={123u,456u};uint64_t now=789u;
        bool ok;
        if(operation==2u) {
            ok=vdc_timestamp_timeline_local_now_upper(&b,argument,&now);
            if(ok)printf("1 %"PRIu64"\n",now);else { assert(now==789u);puts("0"); }
        } else {
            ok=operation==0u ? vdc_timestamp_bridge_local_to_raw(&b,argument,&out) :
                vdc_timestamp_timeline_local_to_raw(&b,argument,&out);
            if(ok)printf("1 %"PRIu64" %"PRIu64"\n",out.lo,out.hi);
            else { assert(out.lo==123u && out.hi==456u);puts("0"); }
        }
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def timeline_helpers(tmp_path_factory):
    return compile_host(tmp_path_factory.mktemp('timeline-helpers'), 'helpers', HELPER_MAIN)


U64 = (1 << 64) - 1


def helper_oracle(row):
    operation, before, after, local, hz, argument = row
    if not 0 < hz <= 500_000_000 or after < before or local > U64 - 1000:
        return (0,)
    if operation == 2:
        if argument < after or (argument - before) > 32 * hz:
            return (0,)
        # Unbounded Python integers, one rational ceiling; no C decomposition.
        result = local + 1000 + ((argument - before) * 1_000_000_000 + hz - 1) // hz
        return (1, result) if result <= U64 else (0,)
    horizon = 2_000_000_000 if operation == 0 else 32_000_000_000
    delta = argument - local
    if after == U64 or not 1000 <= delta <= horizon:
        return (0,)
    lo = before + (delta - 1000) * hz // 1_000_000_000 + 1
    hi = after + (delta * hz + 999_999_999) // 1_000_000_000 + 1
    return (1, lo, hi) if lo <= hi <= U64 else (0,)


def check_helper_rows(executable, rows):
    result = subprocess.run([str(executable)],
        input=''.join(' '.join(map(str, row)) + '\n' for row in rows),
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    assert len(actual) == len(rows)
    for row, got in zip(rows, actual):
        assert got == helper_oracle(row), (row, got, helper_oracle(row))


def test_timeline_exact_horizons_overflow_and_legacy_two_seconds(timeline_helpers):
    rows = []
    for hz in (1, 12_000_000, 249_999_937, 250_000_000, 500_000_000):
        for delta in (999, 1000, 2_000_000_000, 2_000_000_001,
                      20_000_000_000, 32_000_000_000, 32_000_000_004):
            for operation in (0, 1):
                rows.append((operation, 100, 103, 4000, hz, 4000 + delta))
        for ticks in (0, 1, 2 * hz, 20 * hz, 32 * hz, 32 * hz + 1):
            rows.append((2, 100, 100, 4000, hz, 100 + ticks))
    rows += [(op, before, after, local, hz, argument)
             for op in (0, 1, 2)
             for before, after, local, hz, argument in (
                 (U64 - 10, U64 - 1, 0, 500_000_000, U64),
                 (0, 0, U64 - 1000, 1, U64),
                 (0, 0, U64 - 999, 1, U64),
                 (101, 100, 0, 250_000_000, 1000),
                 (0, U64, 0, 250_000_000, U64),
                 (0, 0, 0, 500_000_001, 1000),
                 (0, 0, 0, 0, 1000))]
    check_helper_rows(timeline_helpers, rows)


def test_timeline_integer_oracle_full_width_randomized(timeline_helpers):
    rng = random.Random(0x32_20_02)
    rows = []
    for _ in range(1500):
        hz = rng.choice((1, 12_000_000, 249_999_937, 250_000_000, 500_000_000))
        before = rng.choice((rng.randrange(1 << 33), U64 - rng.randrange(1 << 33)))
        after = min(U64, before + rng.randrange(1000))
        local = rng.choice((rng.randrange(1 << 33), U64 - rng.randrange(1 << 36)))
        target = min(U64, local + rng.randrange(33_000_000_000))
        rows.append((rng.randrange(2), before, after, local, hz, target))
        rows.append((2, before, after, local, hz, min(U64, before + rng.randrange(33 * hz))))
    check_helper_rows(timeline_helpers, rows)
