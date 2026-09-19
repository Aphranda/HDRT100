"""Execute the real recorder/guard with controlled retirement boundaries."""
import subprocess
import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import domain_sources
from test_vdc_priority_trace import trace_executable  # noqa: F401
from test_vdc_priority_summary import summary_executable  # noqa: F401


@pytest.fixture(scope='module')
def guard_executable(summary_executable, tmp_path_factory):
    source = summary_executable.with_suffix('.c').read_text(encoding='utf-8')
    source = source.replace('int main(int argc,char **argv)', 'int prior_summary_main(int argc,char **argv)', 1)
    return compile_executable(tmp_path_factory.mktemp('priority-guard'), 'guard', source + CASES,
        domain_sources() + [ROOT/'components/vdc_dpll_manager/src/vdc_feedback_match.c',
                           ROOT/'components/distributed_refmem/src/refmem_sync_vdc_feedback.c'])


@pytest.mark.parametrize('case', ['sixty', 'sixhundred', 'no_success', 'gap', 'early_freeze',
    'busy', 'stale_config', 'stale_capture', 'session', 'release', 'invalid', 'counter',
    'clock', 'read_busy', 'new_arm', 'first_clock', 'first_clock_new_config', 'zero_config',
    'retire_latch', 'retire_new_config', 'initial_output_read', 'initial_output_session',
    *['output_'+str(i) for i in range(1,12)]])
def test_guard_checkpoint_and_stop_ownership(guard_executable, case):
    result = subprocess.run([str(guard_executable), case], capture_output=True, text=True, timeout=8)
    assert result.returncode == 0, result.stdout + result.stderr


CASES = r'''
static void guard_service(void)
{ core=1;priority_guard_service_core1(); }
static vdc_priority_guard_status_t guard_status(void)
{ core=0;vdc_priority_guard_status_t g;assert(vdc_dpll_manager_get_priority_guard(&g));return g; }
int main(int argc,char **argv)
{
    assert(argc==2);const char *name=argv[1];setup(6000);stopped_ring();
    assert(vdc_dpll_manager_set_priority_follow_phase(true));
    if(!strcmp(name,"invalid")) {
        const unsigned bad[]={0,1,59,61,601,UINT32_MAX};
        for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
            assert(!vdc_dpll_manager_priority_trace_guard_arm(1,false,bad[i]));
        assert(!s_priority_trace_request.sequence);return 0;
    }
    const unsigned seconds=!strcmp(name,"sixhundred")?600:60;
    const bool output_fault=!strncmp(name,"output_",7);
    const bool initial_output=!strncmp(name,"initial_output_",15);
    const bool positive=!strcmp(name,"sixty")||!strcmp(name,"sixhundred");
    assert(vdc_dpll_manager_priority_trace_guard_arm(1,false,seconds));trace_service();
    assert(guard_status().state==VDC_PRIORITY_GUARD_ARMED);
    if(!strcmp(name,"zero_config")) ring.config_seq=ring.applied_config_seq=0u;
    if(!strncmp(name,"first_clock",11)) {
        summary_clock_read_available=0;running_ring();trace_service();guard_service();
        vdc_priority_guard_status_t failed=guard_status();
        assert(failed.state==VDC_PRIORITY_GUARD_FAIL);
        assert(failed.ring_config_seq==ring.config_seq && failed.ring_config_seq);
        assert(!s_priority_trace_work.status.record_count);
        if(!strcmp(name,"first_clock_new_config")) ++ring.config_seq;
        priority_guard_service_core0();
        assert(guard_cancels==(!strcmp(name,"first_clock")?1u:0u));
        assert(guard_status().stop_accepted==guard_cancels);return 0;
    }
    if(initial_output) guard_output_fault=!strcmp(name,"initial_output_read")?1u:4u;
    running_ring();advance_summary(0);guard_service();
    if(initial_output) guard_output_fault=0u;
    if(!strcmp(name,"early_freeze")) {
        advance_summary(500000000);stopped_ring();trace_service();guard_service();
        assert(guard_status().state==VDC_PRIORITY_GUARD_FAIL);
        assert(guard_status().reason_mask & VDC_PRIORITY_GUARD_EARLY_FREEZE);return 0;
    }
    for(unsigned i=0;i<seconds*10u;++i) {
        core=1;
        if(!strcmp(name,"no_success")) advance_summary((uint64_t)i*100000000);
        else {event(100u+i,(uint64_t)i*100000000);tick();}
        guard_service();
        if(i==5u && output_fault) guard_output_fault=(unsigned)atoi(name+7);
        if(i==5u && !strcmp(name,"counter"))
            priority_guard_note_bin(SUMMARY_COUNTER_RESET,0,true);
        if(i==5u && !strcmp(name,"gap"))
            priority_guard_note_bin(0,BOARD_SYS_CLOCK_HZ+1u,true);
        if(i==5u && !positive && strcmp(name,"no_success") && strcmp(name,"gap") &&
            strcmp(name,"zero_config") && !output_fault && !initial_output)
            priority_guard_note_bin(SUMMARY_COUNTER_RESET,0,true);
        if(i==5u && !strcmp(name,"clock")) {
            --raw_now;raw_now=0;guard_service();assert(guard_status().state==VDC_PRIORITY_GUARD_FAIL);return 0;
        }
    }
    advance_summary((uint64_t)seconds*1000000000);guard_service();
    vdc_priority_guard_status_t g=guard_status();
    /* Existing matcher rejects zero config; the guard must still stop it. */
    const bool failed=!positive;
    assert(g.state==(failed?VDC_PRIORITY_GUARD_FAIL:VDC_PRIORITY_GUARD_PASS));
    assert(g.checked_s==(failed?60:seconds));
    assert(g.passed_mask==(failed?0u:(1u<<(seconds/60))-1u));
    assert(!guard_cancels && !g.stop_accepted);
    if(initial_output) {
        assert(g.reason_mask & VDC_PRIORITY_GUARD_OUTPUT_IDENTITY);
        if(!strcmp(name,"initial_output_read")) {
            assert(g.reason_mask & VDC_PRIORITY_GUARD_OUTPUT_READ);
            assert(!g.output_request);
        } else assert(g.output_request==9u);
    }
    if(output_fault) {
        const unsigned reasons[]={0,VDC_PRIORITY_GUARD_OUTPUT_READ,VDC_PRIORITY_GUARD_OUTPUT_STOPPED,
            VDC_PRIORITY_GUARD_OUTPUT_IDENTITY,VDC_PRIORITY_GUARD_OUTPUT_IDENTITY,
            VDC_PRIORITY_GUARD_OUTPUT_IDENTITY,VDC_PRIORITY_GUARD_OUTPUT_STALE,
            VDC_PRIORITY_GUARD_OUTPUT_STALE,VDC_PRIORITY_GUARD_OUTPUT_PROGRESS,
            VDC_PRIORITY_GUARD_OUTPUT_STALE,VDC_PRIORITY_GUARD_OUTPUT_STALE,
            VDC_PRIORITY_GUARD_OUTPUT_STOPPED};
        assert(g.schema==2 && g.output_request==9u && g.reason_mask==reasons[guard_output_fault]);
        assert(g.first_failure_ms==60000u);
        if(guard_output_fault==2u) assert(g.output_reason==SYNC_IO_RUN_OUTPUT_STARVED);
    }
    if(positive) {
        priority_guard_service_core0();g=guard_status();
        assert(!guard_cancels && !g.stop_accepted && ring.enabled);return 0;
    }
    if(!strcmp(name,"stale_config")) {
        ++ring.config_seq;priority_guard_service_core0();assert(!guard_cancels);return 0;
    }
    if(!strcmp(name,"session")) {
        ++s_model_feedback_session;priority_guard_service_core0();assert(!guard_cancels);return 0;
    }
    if(!strcmp(name,"stale_capture")) {
        ++s_priority_trace_work.status.capture_id;priority_trace_publish();
        priority_guard_service_core0();assert(!guard_cancels);return 0;
    }
    if(!strcmp(name,"busy")) {
        guard_stop_busy=true;priority_guard_service_core0();assert(!guard_cancels);
        assert(!guard_status().stop_accepted);guard_stop_busy=false;
    }
    if(!strcmp(name,"read_busy")) {
        ++s_priority_guard_sequence;assert(!vdc_dpll_manager_get_priority_guard(&g));
        priority_guard_service_core0();assert(!guard_cancels);++s_priority_guard_sequence;
    }
    priority_guard_service_core0();g=guard_status();
    assert(guard_cancels==1 && g.stop_accepted && !g.ring_retired && !g.output_retired);
    priority_guard_service_core0();assert(guard_cancels==1);
    if(!strcmp(name,"retire_new_config")) {
        ++ring.config_seq;ring.adapter_started=0;ring.applied_config_seq=ring.config_seq;
        guard_output_idle=true;priority_guard_service_core0();g=guard_status();
        assert(!g.ring_retired && !g.output_retired);return 0;
    }
    ring.adapter_started=0;ring.applied_config_seq=ring.config_seq;guard_output_idle=true;
    g=guard_status();assert(g.ring_retired && g.output_retired);
    if(!strcmp(name,"retire_latch")) {
        priority_guard_service_core0();++ring.config_seq;g=guard_status();
        assert(g.ring_retired && g.output_retired);return 0;
    }
    stopped_ring();trace_service();
    assert(guard_status().state==(failed?VDC_PRIORITY_GUARD_FAIL:VDC_PRIORITY_GUARD_PASS));
    if(!strcmp(name,"release")||!strcmp(name,"new_arm")) {
        assert(vdc_dpll_manager_priority_trace_release());trace_service();
        assert(guard_status().state==VDC_PRIORITY_GUARD_DISABLED);
        if(!strcmp(name,"new_arm")) {
            assert(vdc_dpll_manager_priority_trace_guard_arm(2,false,60));trace_service();
            g=guard_status();assert(g.capture_id==2 && !g.stop_accepted && !g.reason_mask);
            priority_guard_service_core0();assert(guard_cancels==1);
        }
    }
    return 0;
}
'''
