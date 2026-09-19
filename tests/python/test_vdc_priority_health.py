"""Typed source freshness through real MATCH/FOLLOW/phase/Domain ownership."""
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import domain_sources
from test_vdc_priority_phase import phase_follow_executable


@pytest.fixture(scope='module')
def health_exe(phase_follow_executable, tmp_path_factory):
    source = phase_follow_executable.with_suffix('.c').read_text(encoding='utf-8')
    source = source.replace('int main(int argc,char **argv)', 'int phase_main(int argc,char **argv)')
    source = source.replace('bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)',
        'static bool health_clock_ok=true;\nbool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)')
    source = source.replace('*out=raw_now;return true;', '*out=raw_now;return health_clock_ok;')
    source += CASES
    return compile_executable(tmp_path_factory.mktemp('typed-health'), 'health', source,
        domain_sources() + [ROOT/'components/vdc_dpll_manager/src/vdc_feedback_match.c',
                           ROOT/'components/distributed_refmem/src/refmem_sync_vdc_feedback.c'])


@pytest.mark.parametrize('case', ['waiting', 'fresh', 'repeat', 'threshold', 'saturation',
    'clock_failure', 'backwards', 'recovery', 'phase_recovery', 'busy', 'generation',
    'rearm', 'reader', 'disable', 'model_reject', 'counter_saturation', 'no_adjust',
    'old_sequence', 'owner_stop', 'owner_session', 'owner_arm', 'owner_observer',
    'owner_rx_epoch', 'owner_path', 'owner_role', 'owner_config', 'owner_clock_run'])
def test_typed_health_lifetime(health_exe, case):
    result = subprocess.run([str(health_exe), case], capture_output=True, text=True, timeout=20)
    (health_exe.parent/f'{case}.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout+result.stderr


CASES = r'''
static vdc_priority_follow_health_t health(void)
{
    vdc_priority_follow_health_t h;
    assert(vdc_dpll_manager_get_priority_follow_health(&h));return h;
}
static void advance_health(uint64_t ns)
{ now_ns+=ns;now_ms=(uint32_t)(now_ns/1000000u);raw_now=now_ns/4u; }
int main(int argc,char **argv)
{
    assert(argc==2);const char *name=argv[1];
    setup(!strcmp(name,"phase_recovery") || !strcmp(name,"no_adjust")?0:6000);
    if(!strcmp(name,"phase_recovery"))enable_phase();
    const vdc_quality_table_t formal=s_vdc_domain.quality;
    if(!strcmp(name,"waiting")) {
        priority_rx_available=false;tick();
        assert(health().state==VDC_PRIORITY_HEALTH_WAITING && !health().accepted);
        advance_health(UINT64_C(1000000000));tick();
        assert(!health().tick_hz && !health().age_us);return 0;
    }
    tick();vdc_priority_follow_health_t h=health();
    assert(h.schema==1 && h.state==VDC_PRIORITY_HEALTH_FRESH && h.accepted==1);
    assert(h.request==status().request && h.session==123 && h.generation==101);
    assert(h.event_sequence==100 && h.raw_lo==s_priority_follow_work.ticket.match.raw_lo);
    assert(!memcmp(&formal,&s_vdc_domain.quality,sizeof(formal)));
    if(!strcmp(name,"no_adjust")) {
        event(200,1500000000u);tick();
        assert(status().no_adjust==1 && !status().applied && health().accepted==2);
        assert(health().state==VDC_PRIORITY_HEALTH_FRESH);
        assert(!memcmp(&formal,&s_vdc_domain.quality,sizeof(formal)));return 0;
    }
    if(!strcmp(name,"disable")) {
        core=0;stopped=true;assert(vdc_dpll_manager_set_priority_follow(false));
        core=1;stopped=false;tick();
        assert(health().state==VDC_PRIORITY_HEALTH_DISABLED && health().accepted==1);
        assert(health().raw_lo==h.raw_lo);return 0;
    }
    if(!strcmp(name,"model_reject")) {
        event(200,10000000u);prepare();s_vdc_domain.ready=0;apply();
        assert(status().last_reason==VDC_PRIORITY_FOLLOW_MODEL);
        assert(health().accepted==1 && health().raw_lo==h.raw_lo);return 0;
    }
    if(!strcmp(name,"counter_saturation")) {
        s_priority_health.accepted=s_priority_health.stale_transitions=s_priority_health.recoveries=UINT32_MAX;
        advance_health(UINT64_C(500000000));tick();
        assert(health().stale_transitions==UINT32_MAX);
        event(200,600000000u);tick();
        assert(health().accepted==UINT32_MAX && health().recoveries==UINT32_MAX);return 0;
    }
    if(!strcmp(name,"fresh")) {
        event(200,10000000u);tick();
        assert(health().accepted==2 && health().event_sequence==200 && !status().applied);
        assert(!memcmp(&formal,&s_vdc_domain.quality,sizeof(formal)));return 0;
    }
    if(!strcmp(name,"reader")) {
        vdc_priority_follow_health_t out=h;++s_priority_health_guard;
        assert(!vdc_dpll_manager_get_priority_follow_health(&out) && !memcmp(&out,&h,sizeof(h)));
        --s_priority_health_guard;assert(!vdc_dpll_manager_get_priority_follow_health(NULL));return 0;
    }
    const vdc_domain_context_t held=s_vdc_domain;
    const vdc_dpll_manager_committed_model_t held_model=model();
    if(!strncmp(name,"owner_",6)) {
        change_kind=name+6;change_owner();tick();
        assert(health().state==VDC_PRIORITY_HEALTH_RETIRED && health().accepted==1);
        assert(!memcmp(&held.dco,&s_vdc_domain.dco,sizeof(held.dco)));return 0;
    }
    if(!strcmp(name,"clock_failure")) {
        health_clock_ok=false;tick();
        assert(health().state==VDC_PRIORITY_HEALTH_STALE && health().age_us==UINT32_MAX);
        assert(health().reason==VDC_PRIORITY_FOLLOW_BUSY && health().accepted==1);
        health_clock_ok=true;tick();
        assert(health().state==VDC_PRIORITY_HEALTH_FRESH && health().accepted==1);return 0;
    }
    if(!strcmp(name,"backwards")) {
        raw_now=h.raw_hi-1u;tick();
        assert(health().state==VDC_PRIORITY_HEALTH_STALE && health().age_us==UINT32_MAX);return 0;
    }
    if(!strcmp(name,"threshold")) {
        raw_now=h.raw_lo+(uint64_t)h.tick_hz*VDC_PRIORITY_FOLLOW_MAX_AGE_MS/1000u;
        priority_follow_health_service_core1();assert(health().state==VDC_PRIORITY_HEALTH_FRESH);
        ++raw_now;priority_follow_health_service_core1();
        assert(health().state==VDC_PRIORITY_HEALTH_STALE && health().stale_transitions==1);return 0;
    }
    if(!strcmp(name,"saturation")) {
        raw_now=UINT64_MAX;priority_follow_health_service_core1();
        assert(health().age_us==UINT32_MAX && health().state==VDC_PRIORITY_HEALTH_STALE);return 0;
    }
    if(!strcmp(name,"busy"))priority_rx_available=false;
    if(!strcmp(name,"generation"))++priority_rx.typed_record.binding_generation;
    if(!strcmp(name,"old_sequence"))priority_rx.typed_record.event_sequence=99;
    advance_health((uint64_t)(VDC_PRIORITY_FOLLOW_MAX_AGE_MS+1u)*1000000u);
    for(unsigned i=0;i<3;++i) { tick();advance_health(1000000u); }
    assert(health().state==VDC_PRIORITY_HEALTH_STALE && health().accepted==1);
    assert(health().stale_transitions==1 && health().raw_lo==h.raw_lo);
    assert(!s_priority_follow_work.have_baseline && !s_priority_follow_work.pending);
    assert(!memcmp(&held,&s_vdc_domain,sizeof(held)));
    vdc_dpll_manager_committed_model_t after=model();
    assert(!memcmp(&held_model,&after,sizeof(after)));
    if(!strcmp(name,"repeat") || !strcmp(name,"busy") || !strcmp(name,"generation") || !strcmp(name,"old_sequence"))return 0;
    if(!strcmp(name,"rearm")) {
        ring.enabled=ring.adapter_started=0;tick();
        assert(health().state==VDC_PRIORITY_HEALTH_RETIRED);
        ring.enabled=ring.adapter_started=1;event(200,500000000u);tick();
        assert(health().state==VDC_PRIORITY_HEALTH_RETIRED && health().accepted==1);
        core=0;stopped=true;
        assert(vdc_dpll_manager_set_priority_match(102));
        assert(vdc_dpll_manager_set_priority_follow(true));
        core=1;stopped=false;priority_rx.typed_record.binding_generation=102;
        event(300,600000000u);tick();
        assert(health().state==VDC_PRIORITY_HEALTH_FRESH && health().accepted==1);
        assert(health().generation==102 && !health().stale_transitions);return 0;
    }
    assert(!strcmp(name,"recovery") || !strcmp(name,"phase_recovery"));
    event(200,500000000u);tick();
    assert(health().state==VDC_PRIORITY_HEALTH_FRESH && health().accepted==2 && health().recoveries==1);
    assert(s_priority_follow_work.have_baseline && s_priority_follow_work.previous.sequence==200);
    assert(!status().applied && !status().prepared);
    assert(!memcmp(&formal,&s_vdc_domain.quality,sizeof(formal)));
    assert(s_vdc_domain.dco.period_adjust_ppb==held.dco.period_adjust_ppb);
    return 0;
}
'''
