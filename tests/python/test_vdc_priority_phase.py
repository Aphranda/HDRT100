"""Actual MATCH/FOLLOW/Domain and model publisher with external owner stubs."""
from pathlib import Path
import re
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/python'))
import test_vdc_priority_follow as follow
from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import compile_executable


@pytest.fixture(scope='module')
def phase_follow_executable(tmp_path_factory):
    manager = ROOT / 'components/vdc_dpll_manager/src'
    physical = (ROOT / 'components/tdma/inc/tdma_pio_spi_phys.h').read_text(encoding='utf-8')
    begin = physical.index('enum {\n    TDMA_EVENT_LIVE_RETAINED')
    end = physical.index('} tdma_pio_spi_event_exact_t;', begin) + len('} tdma_pio_spi_event_exact_t;')
    event_types = '#include "tdma_event_history.h"\n' + physical[begin:end]
    matcher = (manager / 'vdc_dpll_feedback_match.inc').read_text(encoding='utf-8')
    source_type = re.search(r'typedef struct \{\s*uint64_t next_ordinal.*?\} vdc_feedback_match_source_t;', matcher, re.S)
    helpers = matcher[matcher.index('static uint32_t match_inc'):matcher.index('/* Keep authorization')]
    prelude = follow.OWNER_PRELUDE.replace('EVENT_TYPES', event_types).replace(
        '#include <assert.h>', '#include <assert.h>\n#include <inttypes.h>\n#include "vdc_priority_follow.h"\n'
        '#include "' + (ROOT / 'components/vdc_dpll_manager/inc/vdc_priority_phase.h').as_posix() + '"\n'
        '#include "vdc_priority_rx.h"\n#include "vdc_priority_match.h"\n'
        'static unsigned core;\nstatic unsigned get_core_num(void) { return core; }')
    prelude = prelude.replace('{ (void)hz;(void)out;return false; }',
        '{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=(vdc_timestamp_clock_bridge_t){'
        '.tick_hz=hz,.raw_before=raw_now,.raw_after=raw_now+1,.local_ns=now_ns};return true; }')
    harness = prelude + follow.EXTERNAL_INPUTS + source_type.group(0) + follow.MATCH_STORAGE + helpers
    harness += HOOKS
    for name in ('vdc_model_feedback.inc', 'vdc_boundary_control.inc', 'vdc_priority_match.inc'):
        text = (manager / name).read_text(encoding='utf-8')
        if name == 'vdc_model_feedback.inc' and 'VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK' not in text:
            text = text.replace('    (void)__atomic_add_fetch(&s_committed_model_guard, 1u, __ATOMIC_RELEASE);',
                '    VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK(&model);\n'
                '    (void)__atomic_add_fetch(&s_committed_model_guard, 1u, __ATOMIC_RELEASE);')
        harness += '\n' + text
    harness += '\n' + (manager / 'vdc_priority_follow.inc').read_text(encoding='utf-8').replace(
        '#include "vdc_priority_phase.inc"', (manager / 'vdc_priority_phase.inc').read_text(encoding='utf-8'))
    harness += '\n' + ingress_definition(follow.DOMAIN_HARNESS, 'fixture')
    harness += follow.SCENARIOS.replace('OWNER_REMOTE_INPUT', ingress_definition(follow.OWNER_TESTS, 'follower_input')).replace(
        'int main(int argc,char **argv)', 'int old_main(int argc,char **argv)')
    assert 'int old_main(' in harness
    harness += SCENARIOS
    sources = follow.domain_sources() + [
        manager / 'vdc_feedback_match.c',
        ROOT / 'components/distributed_refmem/src/refmem_sync_vdc_feedback.c']
    return compile_executable(tmp_path_factory.mktemp('phase-follow'), 'phase_follow', harness, sources)


def run(exe, name, data=None):
    result = subprocess.run([str(exe), name], input=data, text=True,
                            capture_output=True, timeout=30)
    (exe.parent / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


@pytest.mark.parametrize('name', ['basic', 'no_fresh', 'no_fresh_8s', 'rate_priority', 'no_adjust',
    'unknown_model', 'publication_failure', 'publication_zero_token', 'stop_pending',
    'stop_second_validation', 'zero_crossing', 'throttle', 'config_latch', 'default_off',
    'formal_lock', 'cumulative_overflow', 'normalization_overflow', 'atomic_read', 'sizes'])
def test_real_phase_controller(phase_follow_executable, name):
    run(phase_follow_executable, 'phase_' + name)


def test_nearest_zero_phase_boundary_and_int64_edges(phase_follow_executable):
    low, high = -(1 << 63), (1 << 63) - 1
    cases = [(low, low), (low, -1), (-11, -10), (-1, 0), (-100, 100), (0, 1),
             (1, 99), (1000000001, high), (high, high), (2, 1)]
    actual = list(map(int, run(phase_follow_executable, 'phase_delta',
                             '\n'.join(f'{lo} {hi}' for lo, hi in cases) + '\n').splitlines()))
    expected = [min(1000000000, -hi) if hi < 0 else -min(1000000000, lo)
                if lo > 0 and lo <= hi else 0 for lo, hi in cases]
    assert actual == expected


@pytest.mark.parametrize('change', ['stop', 'session', 'arm', 'observer', 'rx_epoch',
                                  'path', 'role', 'config', 'clock_run'])
def test_phase_second_validation_retires_changed_lifetime(phase_follow_executable, change):
    run(phase_follow_executable, 'phase_lifetime_' + change)


@pytest.mark.parametrize('name', ['multi_4s', 'multi_8s', 'multi_rate_4s', 'multi_rate_8s'])
def test_multiple_confirmed_phase_commits_preserve_frequency_window(phase_follow_executable, name):
    run(phase_follow_executable, 'phase_' + name)


HOOKS = r'''
static void phase_model_committed_core1(const vdc_dpll_manager_committed_model_t *model);
static unsigned phase_rows, corrupt_publication;
static vdc_priority_phase_snapshot_t last_phase_row;
static void phase_test_trace(const vdc_priority_phase_snapshot_t *phase)
{ ++phase_rows; last_phase_row=*phase; }
#define VDC_PRIORITY_TRACE_PHASE_HOOK(snapshot) phase_test_trace(snapshot)
static void phase_test_publication(const vdc_dpll_manager_committed_model_t *model)
{
    vdc_dpll_manager_committed_model_t copy=*model;
    if(corrupt_publication==1u)copy.dco.period_adjust_ppb++;
    if(corrupt_publication==2u)copy.token=0u;
    phase_model_committed_core1(&copy);
}
#define VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK(model) phase_test_publication(model)
'''

SCENARIOS = r'''
static vdc_priority_phase_snapshot_t phase_status(void)
{
    vdc_priority_phase_snapshot_t out;
    assert(vdc_dpll_manager_get_priority_follow_phase(&out));
    return out;
}
static void enable_phase(void)
{
    core=0;stopped=true;
    assert(vdc_dpll_manager_set_priority_follow_phase(true));
    stopped=false;core=1;
}
static void hook_phase_stop(void) { ring.enabled=0; }
static void hook_phase_second_read(void) { now_hook=hook_phase_stop; }
static void hook_phase_second_owner_read(void) { now_hook=change_owner; }
static void phase_test(const char *name)
{
    if(!strcmp(name,"sizes")) {
        printf("%zu %zu %zu %zu\n",sizeof(vdc_priority_phase_work_t),sizeof(vdc_priority_phase_snapshot_t),
            sizeof(vdc_priority_follow_work_t),sizeof(vdc_priority_follow_ticket_t));return;
    }
    setup(!strcmp(name,"rate_priority") || !strncmp(name,"multi_rate_",11)?6000:0);
    if(strcmp(name,"default_off"))enable_phase();
    if(!strncmp(name,"lifetime_",9)) {
        prepare();change_kind=name+9;now_hook=hook_phase_second_owner_read;
        const vdc_dco_control_t unchanged=s_vdc_domain.dco;
        apply();
        assert(!memcmp(&unchanged,&s_vdc_domain.dco,sizeof(unchanged)));
        assert(!phase_status().committed && !phase_rows);
        assert(!s_priority_follow_work.have_baseline && !status().active);return;
    }
    if(!strcmp(name,"config_latch")) {
        core=0;stopped=true;assert(vdc_dpll_manager_set_priority_follow_phase(false));core=1;stopped=false;
        tick();assert(!phase_status().enabled && !phase_status().applied);
        enable_phase();event(200,100000000u);tick();
        assert(!phase_status().enabled && !phase_status().applied);
        bool requested=false;assert(vdc_dpll_manager_try_priority_follow_phase_enabled(&requested)&&requested);
        assert(!vdc_dpll_manager_set_priority_follow_phase(false));
        core=0;assert(!vdc_dpll_manager_set_priority_follow_phase(false));
        return;
    }
    if(!strcmp(name,"publication_failure"))corrupt_publication=1u;
    if(!strcmp(name,"publication_zero_token"))corrupt_publication=2u;
    if(!strcmp(name,"formal_lock")) {
        s_vdc_domain.dco.lock_state=VDC_DOMAIN_LOCK_LOCKED;publish();event(100,10000000u);
    }
    if(!strcmp(name,"zero_crossing")) {
        prepare();
        s_priority_follow_work.ticket.match.residual_lo=-100;
        s_priority_follow_work.ticket.match.residual_hi=100;
        apply();assert(phase_status().reason==VDC_PRIORITY_PHASE_ZERO_CROSSING);
        assert(!phase_status().committed && !phase_rows);return;
    }
    if(!strcmp(name,"cumulative_overflow")) {
        prepare();s_priority_phase_work.status.cumulative_ns=INT64_MAX;
        apply();assert(phase_status().reason==VDC_PRIORITY_PHASE_OVERFLOW);
        assert(!phase_status().committed && !phase_rows);return;
    }
    if(!strcmp(name,"stop_pending") || !strcmp(name,"stop_second_validation")) {
        prepare();
        if(!strcmp(name,"stop_pending"))ring.enabled=0;
        else now_hook=hook_phase_second_read;
        apply();assert(!phase_status().committed && !phase_rows);
        assert(phase_status().reason==VDC_PRIORITY_PHASE_STOP);return;
    }
    const vdc_dco_control_t before=s_vdc_domain.dco;
    tick();
    if(!strcmp(name,"default_off")) {
        assert(!phase_status().enabled && !phase_status().committed && !phase_rows);
        assert(!memcmp(&before,&s_vdc_domain.dco,sizeof(before)));return;
    }
    if(!strcmp(name,"formal_lock")) {
        assert(phase_status().reason==VDC_PRIORITY_PHASE_FORMAL_LOCK);
        assert(!phase_status().committed && !phase_rows);return;
    }
    if(!strcmp(name,"publication_failure") || !strcmp(name,"publication_zero_token")) {
        assert(phase_status().committed==1u && !phase_status().applied && !phase_rows);
        assert(phase_status().reason==VDC_PRIORITY_PHASE_LEDGER);
        assert(!s_priority_follow_work.have_baseline && !s_priority_phase_work.known_model);
        corrupt_publication=0;event(200,10000000u);tick();
        assert(phase_status().rate_epoch==2u && phase_status().cumulative_ns==0);return;
    }
    assert(phase_status().applied==1u && phase_rows==1u);
    assert(phase_status().delta_ns==VDC_PRIORITY_PHASE_MAX_DELTA_NS);
    assert(phase_status().cumulative_ns==VDC_PRIORITY_PHASE_MAX_DELTA_NS);
    assert(s_vdc_domain.dco.base_local_tick64==before.base_local_tick64);
    assert(s_vdc_domain.dco.period_adjust_ppb==before.period_adjust_ppb);
    assert(s_priority_follow_work.previous.sequence==100u);
    assert(s_priority_follow_work.previous.local_lo==s_priority_follow_work.previous.normalized_lo);
    const uint32_t epoch=phase_status().rate_epoch;
    const uint64_t retained_actual=s_priority_follow_work.previous.local_lo;
    if(!strncmp(name,"multi_",6)) {
        for(unsigned i=1;i<=8;++i) {
            event(100u+i*10u,(uint64_t)i*100000000u);tick();
            assert(s_priority_follow_work.previous.sequence==100u);
            assert(s_priority_follow_work.previous.local_lo==retained_actual);
            assert(phase_status().rate_epoch==epoch && !status().prepared);
        }
        assert(phase_status().applied>=4u && phase_rows>=4u);
        const uint32_t before_phase=phase_status().applied;
        const bool rate_case=!strncmp(name,"multi_rate_",11);
        const uint64_t interval=strstr(name,"8s")?UINT64_C(8010000000):UINT64_C(4010000000);
        event(500,interval);tick();
        assert(status().prepared==1u && status().baseline_sequence==100u);
        if(rate_case) {
            assert(status().applied==1u && phase_status().applied==before_phase);
            assert(status().error_lo_ppb>0 && status().error_hi_ppb>=status().error_lo_ppb);
        } else {
            assert(status().no_adjust==1u);
            assert(status().error_lo_ppb<=0 && status().error_hi_ppb>=0);
        }
        return;
    }
    if(!strcmp(name,"basic") || !strcmp(name,"throttle")) {
        event(200,10000000u);tick();assert(phase_status().applied==1u);
        event(300,100000000u);tick();assert(phase_status().applied==2u);
        assert(phase_status().rate_epoch==epoch && s_priority_follow_work.previous.sequence==100u);
        assert(s_priority_follow_work.previous.local_lo==retained_actual);return;
    }
    if(!strcmp(name,"no_fresh") || !strcmp(name,"no_fresh_8s")) {
        for(unsigned i=0;i<5;++i) {
            now_ns+=1000000u;now_ms++;raw_now+=250000u;tick();
            assert(phase_status().applied==1u && s_priority_follow_work.have_baseline);
            assert(s_priority_follow_work.previous.local_lo==retained_actual);
        }
        event(500,!strcmp(name,"no_fresh_8s")?UINT64_C(8010000000):UINT64_C(4010000000));tick();
        assert(status().prepared==1u && status().baseline_sequence==100u);
        assert(phase_status().rate_epoch==epoch);
        assert(status().error_lo_ppb<=0 && status().error_hi_ppb>=0);return;
    }
    if(!strcmp(name,"no_adjust") || !strcmp(name,"rate_priority")) {
        const uint32_t phase_applied=phase_status().applied;
        event(200,UINT64_C(1010000000));tick();
        if(!strcmp(name,"rate_priority")) {
            assert(status().applied==1u && phase_status().applied==phase_applied);
            assert(phase_status().reason==VDC_PRIORITY_PHASE_RATE_PRIORITY);
            event(300,UINT64_C(1020000000));tick();assert(phase_status().rate_epoch==epoch+1u);
        } else assert(status().no_adjust==1u && phase_status().applied==phase_applied+1u);
        return;
    }
    if(!strcmp(name,"unknown_model")) {
        vdc_dpll_manager_committed_model_t unknown=model();
        unknown.token=++s_committed_model_serial;
        boundary_store(s_committed_model_words,&unknown,sizeof(unknown));
        event(200,10000000u);tick();
        assert(phase_status().rate_epoch==epoch+1u && phase_status().cumulative_ns==0);
        assert(s_priority_follow_work.previous.sequence==200u);return;
    }
    if(!strcmp(name,"normalization_overflow")) {
        s_priority_phase_work.status.cumulative_ns=INT64_MIN;
        /* Force the retained admitted positive coordinate above the U64
         * cancellation range; exact identity is retained for this test. */
        event(200,10000000u);vdc_priority_match_core1();
        s_priority_match_work.status.local_lo=UINT64_MAX-10u;
        s_priority_match_work.status.local_hi=UINT64_MAX;
        priority_follow_prepare_core1();
        assert(!status().active && !s_priority_follow_work.pending);return;
    }
    if(!strcmp(name,"atomic_read")) {
        vdc_priority_phase_snapshot_t expected;
        memset(&expected,0xA5,sizeof(expected));vdc_priority_phase_snapshot_t out=expected;
        ++s_priority_phase_guard;
        assert(!vdc_dpll_manager_get_priority_follow_phase(&out));
        assert(!memcmp(&out,&expected,sizeof(out)));--s_priority_phase_guard;
        assert(!vdc_dpll_manager_get_priority_follow_phase(NULL));
        assert(!vdc_dpll_manager_try_priority_follow_phase_enabled(NULL));return;
    }
    assert(!"unknown phase test");
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"phase_delta")) {
        int64_t lo,hi;
        while(scanf("%" SCNd64 " %" SCNd64,&lo,&hi)==2)
            printf("%" PRId64 "\n",priority_phase_delta(lo,hi));
        return 0;
    }
    if(!strncmp(argv[1],"phase_",6)) {phase_test(argv[1]+6);return 0;}
    return old_main(argc,argv);
}
'''
