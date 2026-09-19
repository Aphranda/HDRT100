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


@pytest.fixture(scope='module')
def seal_executable(summary_executable, tmp_path_factory):
    source = summary_executable.with_suffix('.c').read_text(encoding='utf-8')
    source = source.replace('int main(int argc,char **argv)', 'int prior_seal_summary_main(int argc,char **argv)', 1)
    old = ('if(!summary_clock_read_available)return false;'
           '*out=raw_now;return true; }')
    assert old in source
    source = ('#include <stdint.h>\nstatic int (*seal_probe_hook)(uint64_t *);\n' +
              source.replace(old, 'if(!summary_clock_read_available)return false;'
                  '*out=raw_now;if(seal_probe_hook)return seal_probe_hook(out);return true; }', 1))
    return compile_executable(tmp_path_factory.mktemp('priority-seal'), 'seal', source + SEAL_CASES,
        domain_sources() + [ROOT/'components/vdc_dpll_manager/src/vdc_feedback_match.c',
                           ROOT/'components/distributed_refmem/src/refmem_sync_vdc_feedback.c'])


@pytest.mark.parametrize('case', ['sixty', 'sixhundred', 'release_new_arm', 'ordinary',
    'terminal_clock_read', 'terminal_rollback', 'terminal_epoch', 'terminal_run',
    'terminal_counter_reset', 'terminal_counter_saturation', 'terminal_field_saturation',
    'terminal_owner', 'missing_terminal', 'late_checkpoint',
    'sixhundred_terminal_clock_read', 'sixhundred_terminal_counter_reset'])
def test_target_seal_preserves_terminal_veto_and_immutable_evidence(seal_executable, case):
    result = subprocess.run([str(seal_executable), case], capture_output=True, text=True, timeout=12)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.fixture(scope='module')
def startup_gap_executable(summary_executable, tmp_path_factory):
    source = summary_executable.with_suffix('.c').read_text(encoding='utf-8')
    source = source.replace('int main(int argc,char **argv)', 'int prior_startup_summary_main(int argc,char **argv)', 1)
    return compile_executable(tmp_path_factory.mktemp('priority-startup-gap'), 'startup_gap',
        source + STARTUP_GAP_CASES, domain_sources() + [
            ROOT/'components/vdc_dpll_manager/src/vdc_feedback_match.c',
            ROOT/'components/distributed_refmem/src/refmem_sync_vdc_feedback.c'])


@pytest.mark.parametrize('case', ['delayed_first', 'first_bin_gap', 'cross_bin_gap', 'tail_gap',
    'empty_first_bin', 'new_arm', 'one_second', 'one_second_plus_tick', 'startup_clock',
    'startup_service', 'startup_epoch', 'startup_binding', 'no_reference'])
def test_startup_wait_is_not_a_steady_gap(startup_gap_executable, case):
    from test_vdc_priority_summary import native
    result = subprocess.run([str(startup_gap_executable), case], capture_output=True, timeout=12)
    (startup_gap_executable.parent / f'{case}.stderr.log').write_bytes(result.stderr)
    assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    # Native bytes are available for independent decoder/acceptance tests.
    (startup_gap_executable.parent / f'{case}.ram.bin').write_bytes(result.stdout)
    status, rows = native(result.stdout, 9, 10000)
    assert rows and all(row['flags'] & 512 for row in rows)
    assert rows[-1]['flags'] & 256
    if case in ('delayed_first', 'new_arm', 'one_second'):
        assert status['reason'] == 8
        assert rows[0]['first_success_offset_ticks'] == 300_000_000
        assert max(row['max_success_gap_ticks'] for row in rows) <= 250_000_000
    if case == 'first_bin_gap':
        assert rows[0]['first_success_offset_ticks'] == 300_000_000
        assert rows[0]['max_success_gap_ticks'] == 375_000_000
    if case == 'one_second_plus_tick':
        assert max(row['max_success_gap_ticks'] for row in rows) == 250_000_001
    if case in ('no_reference', 'startup_clock', 'startup_service', 'startup_epoch', 'startup_binding'):
        assert status['reason'] != 8
    if case == 'no_reference':
        assert not any(row['success_count'] for row in rows)
        assert not any(row['max_success_gap_ticks'] for row in rows)


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
        assert(g.schema==3 && g.output_request==9u && g.reason_mask==reasons[guard_output_fault]);
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


STARTUP_GAP_CASES = r'''
static void startup_guard_service(void) { core=1u; priority_guard_service_core1(); }
static void startup_arm(uint32_t capture, uint64_t elapsed)
{
    stopped_ring();
    assert(vdc_dpll_manager_priority_trace_guard_arm(capture,false,60u)); trace_service();
    assert(!s_priority_trace_work.summary.have_success);
    running_ring(); advance_summary(elapsed); startup_guard_service();
    assert(s_priority_guard_work.state==VDC_PRIORITY_GUARD_RUNNING);
    assert(priority_summary_record()->flags & 512u);
}
static void startup_run(const char *name,uint64_t base,uint32_t sequence)
{
    const bool expected_pass=!strcmp(name,"delayed_first") || !strcmp(name,"new_arm") ||
        !strcmp(name,"one_second");
    uint32_t evidence_count=0u;
    for(unsigned i=1u;i<=600u;++i) {
        const uint64_t elapsed=base+(uint64_t)i*100000000u;
        if(!strcmp(name,"startup_service") && i<12u) continue;
        if(!strcmp(name,"startup_clock") && i==5u) summary_clock_read_available=0;
        if(!strcmp(name,"startup_epoch") && i==5u) ++s_vdc_domain.clock.epoch_id;
        if(!strcmp(name,"startup_binding") && i==5u) ++ring.config_seq;
        advance_summary(elapsed);
        summary_clock_read_available=1;
        startup_guard_service();
        if(i<12u && s_priority_trace_work.summary.open)
            assert(!priority_summary_record()->max_success_gap_ticks);
        bool produce=i>=12u && i<600u;
        if(!strcmp(name,"no_reference")) produce=false;
        if(!strcmp(name,"empty_first_bin") && i<112u) produce=false;
        if(!strcmp(name,"first_bin_gap") && i>12u && i<27u) produce=false;
        if(!strcmp(name,"cross_bin_gap") && i>98u && i<113u) produce=false;
        if(!strcmp(name,"tail_gap") && i>584u) produce=false;
        if((!strcmp(name,"one_second") || !strcmp(name,"one_second_plus_tick")) && i>20u && i<30u)
            produce=false;
        if(produce && s_priority_trace_work.status.state!=VDC_PRIORITY_TRACE_FROZEN) {
            const uint64_t extra=!strcmp(name,"one_second_plus_tick") && i==30u ? 4u : 0u;
            event(sequence+i,elapsed+extra); tick(); ++evidence_count;
        }
        if(!strcmp(name,"startup_clock") || !strcmp(name,"startup_epoch")) {
            if(i==5u) break;
        }
    }
    startup_guard_service();
    assert(s_priority_guard_work.state==(expected_pass ? VDC_PRIORITY_GUARD_PASS : VDC_PRIORITY_GUARD_FAIL));
    if(expected_pass) {
        assert(!s_priority_guard_work.reason_mask && s_priority_guard_work.checked_s==60u);
        assert(trace_status().reason==VDC_PRIORITY_TRACE_TARGET_COMPLETE);
        assert(evidence_count>0u && s_priority_trace_work.summary.have_success);
    } else if(!strcmp(name,"first_bin_gap") || !strcmp(name,"cross_bin_gap") ||
              !strcmp(name,"tail_gap") || !strcmp(name,"one_second_plus_tick")) {
        assert(s_priority_guard_work.reason_mask & VDC_PRIORITY_GUARD_SUCCESS_GAP);
        assert(!(s_priority_guard_work.reason_mask & (SUMMARY_SERVICE_GAP|SUMMARY_CLOCK_INVALID)));
    } else if(!strcmp(name,"empty_first_bin") || !strcmp(name,"no_reference")) {
        assert(s_priority_guard_work.reason_mask & SUMMARY_NO_SUCCESS);
        if(!strcmp(name,"no_reference")) {
            assert(s_priority_guard_work.reason_mask & SUMMARY_UNBOUND);
            assert(!(s_priority_guard_work.reason_mask & VDC_PRIORITY_GUARD_SUCCESS_GAP));
        }
    } else if(!strcmp(name,"startup_service")) {
        assert(s_priority_guard_work.reason_mask & SUMMARY_SERVICE_GAP);
    } else if(!strcmp(name,"startup_clock") || !strcmp(name,"startup_epoch")) {
        assert(s_priority_guard_work.reason_mask & SUMMARY_CLOCK_INVALID);
    } else if(!strcmp(name,"startup_binding")) {
        assert(s_priority_guard_work.reason_mask & VDC_PRIORITY_GUARD_COVERAGE);
    }
}
int main(int argc,char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    assert(argc==2); const char *name=argv[1]; setup(6000); stopped_ring();
    assert(vdc_dpll_manager_set_priority_follow_phase(true));
    startup_arm(1u,0u);
    startup_run(name,0u,100u);
    if(!strcmp(name,"new_arm")) {
        frozen_trace(); assert(vdc_dpll_manager_priority_trace_release()); trace_service();
        startup_arm(2u,UINT64_C(70000000000));
        startup_run(name,UINT64_C(70000000000),1000u);
    }
    /* Complete the stopped owner acknowledgement after deliberate config drift. */
    if(!strcmp(name,"startup_binding")) ring.applied_config_seq=ring.config_seq;
    frozen_trace(); export_trace(); return 0;
}
'''


SEAL_CASES = r'''
static const char *seal_case;
static unsigned seal_reads;
static int seal_read_hook(uint64_t *raw)
{
    ++seal_reads;
    assert(s_priority_guard_work.state==VDC_PRIORITY_GUARD_RUNNING);
    if(seal_reads==1u) {
        /* These owner/counter changes occur after the checkpoint's clock
         * observation and are first consumed by the terminal recorder. */
        if(!strcmp(seal_case,"terminal_epoch"))++s_vdc_domain.clock.epoch_id;
        if(!strcmp(seal_case,"terminal_run"))++s_vdc_domain.clock.run_id;
        if(!strcmp(seal_case,"terminal_counter_reset")) {
            s_priority_trace_work.summary.counters[0]=10u;
            s_priority_match_work.status.rejected=0u;
        }
        if(!strcmp(seal_case,"terminal_counter_saturation"))
            s_priority_match_work.status.rejected=UINT32_MAX;
        if(!strcmp(seal_case,"terminal_field_saturation"))
            s_priority_match_work.status.rejected+=UINT16_MAX+1u;
        if(!strcmp(seal_case,"terminal_owner"))s_dpll_capture_pool_owner=DPLL_CAPTURE_POOL_LEGACY;
        if(!strcmp(seal_case,"missing_terminal"))s_priority_trace_work.summary.open=0u;
    }
    if(seal_reads==2u) {
        if(!strcmp(seal_case,"terminal_clock_read"))return 0;
        if(!strcmp(seal_case,"terminal_rollback"))--*raw;
    }
    return 1;
}
static void seal_guard_service(void) { core=1u;priority_guard_service_core1(); }
static void seal_assert_unchanged(const vdc_priority_trace_status_t *status,
    const unsigned char *records,size_t bytes)
{
    const vdc_priority_trace_status_t current=trace_status();
    assert(!memcmp(&current,status,sizeof(current)));
    assert(!memcmp(s_dpll_capture_records,records,bytes));
}
int main(int argc,char **argv)
{
    assert(argc==2);seal_case=argv[1];setup(6000);stopped_ring();
    assert(vdc_dpll_manager_set_priority_follow_phase(true));
    const bool ordinary=!strcmp(seal_case,"ordinary");
    const bool long_run=!strcmp(seal_case,"sixhundred") || !strncmp(seal_case,"sixhundred_",11u);
    const unsigned seconds=long_run?600u:60u;
    if(!strncmp(seal_case,"sixhundred_",11u))seal_case+=11u;
    if(ordinary)assert(vdc_dpll_manager_priority_trace_summary_window_arm(1u,false,10000u));
    else assert(vdc_dpll_manager_priority_trace_guard_arm(1u,false,seconds));
    trace_service();running_ring();advance_summary(0u);seal_guard_service();
    for(unsigned i=0u;i<seconds*10u;++i) {
        core=1u;event(100u+i,(uint64_t)i*100000000u);tick();
    }
    advance_summary((uint64_t)seconds*1000000000u);
    if(ordinary) {
        seal_guard_service();
        assert(s_priority_guard_work.state==VDC_PRIORITY_GUARD_DISABLED);
        assert(trace_status().state==VDC_PRIORITY_TRACE_RUNNING && s_priority_trace_work.summary.open);
        advance_summary(UINT64_C(60100000000));
        assert(trace_status().state==VDC_PRIORITY_TRACE_RUNNING);
        frozen_trace();assert(trace_status().reason==VDC_PRIORITY_TRACE_STOP);return 0;
    }
    const bool success=!strcmp(seal_case,"sixty") || !strcmp(seal_case,"sixhundred") ||
        !strcmp(seal_case,"release_new_arm");
    const uint32_t previous_mask=s_priority_guard_work.passed_mask;
    assert(previous_mask==(seconds==600u?511u:0u));
    assert(s_priority_guard_work.state==VDC_PRIORITY_GUARD_RUNNING);
    if(!strcmp(seal_case,"late_checkpoint"))raw_now+=UINT64_C(60)*BOARD_SYS_CLOCK_HZ;
    if(!success && strcmp(seal_case,"late_checkpoint"))seal_probe_hook=seal_read_hook;
    seal_guard_service();seal_probe_hook=NULL;
    assert(s_priority_guard_work.schema==3u);
    const vdc_priority_trace_status_t sealed=trace_status();
    if(!success) {
        assert(s_priority_guard_work.state==VDC_PRIORITY_GUARD_FAIL);
        assert(s_priority_guard_work.reason_mask && s_priority_guard_work.passed_mask==previous_mask);
        assert(s_priority_guard_work.first_failure_ms>=seconds*1000u);
        if(!strcmp(seal_case,"late_checkpoint") || !strcmp(seal_case,"missing_terminal")) {
            assert(s_priority_guard_work.reason_mask & VDC_PRIORITY_GUARD_COVERAGE);return 0;
        }
        assert(sealed.state==VDC_PRIORITY_TRACE_FROZEN && sealed.reason==VDC_PRIORITY_TRACE_BINDING);
        if(!strcmp(seal_case,"terminal_owner")) {
            assert(s_priority_guard_work.reason_mask & VDC_PRIORITY_GUARD_COVERAGE);
            assert(s_priority_trace_work.summary.open);return 0;
        }
        const vdc_priority_summary_record_t *last=(const void *)&s_dpll_capture_records[sealed.record_count-1u];
        assert(last->flags & SUMMARY_TERMINAL);
        uint32_t expected=SUMMARY_CLOCK_INVALID;
        if(!strcmp(seal_case,"terminal_counter_reset"))expected=SUMMARY_COUNTER_RESET;
        if(!strcmp(seal_case,"terminal_counter_saturation"))expected=SUMMARY_COUNTER_SATURATED;
        if(!strcmp(seal_case,"terminal_field_saturation"))expected=SUMMARY_FIELD_SATURATED;
        assert((last->flags & expected) && (s_priority_guard_work.reason_mask & expected));
        return 0;
    }
    assert(s_priority_guard_work.state==VDC_PRIORITY_GUARD_PASS && !s_priority_guard_work.reason_mask);
    assert(s_priority_guard_work.checked_s==seconds &&
        s_priority_guard_work.passed_mask==(1u<<(seconds/60u))-1u);
    assert(sealed.state==VDC_PRIORITY_TRACE_FROZEN && sealed.reason==VDC_PRIORITY_TRACE_TARGET_COMPLETE);
    assert(sealed.schema==9u && sealed.record_count==seconds/10u+1u && !s_priority_trace_work.summary.open);
    assert(sealed.match_count==seconds*10u);
    const vdc_priority_summary_record_t *first=(const void *)&s_dpll_capture_records[0];
    const vdc_priority_summary_record_t *last=(const void *)&s_dpll_capture_records[sealed.record_count-1u];
    assert((last->flags & SUMMARY_TERMINAL) && last->observed_end_raw-first->observed_start_raw==
        (uint64_t)seconds*BOARD_SYS_CLOCK_HZ);
    for(unsigned i=1u;i<sealed.record_count;++i) {
        const vdc_priority_summary_record_t *prior=(const void *)&s_dpll_capture_records[i-1u];
        const vdc_priority_summary_record_t *current=(const void *)&s_dpll_capture_records[i];
        assert(prior->observed_end_raw==current->observed_start_raw);
    }
    assert(ring.enabled && ring.adapter_started && !guard_cancels);
    unsigned char records[sizeof(s_dpll_capture_records)];
    memcpy(records,s_dpll_capture_records,sizeof(records));
    const vdc_priority_guard_status_t guard=s_priority_guard_work;
    for(unsigned i=1u;i<4u;++i) {
        core=1u;event(100u+seconds*10u+i,((uint64_t)seconds*1000u+i*100u)*1000000u);tick();
        seal_guard_service();core=0u;priority_guard_service_core0();
        seal_assert_unchanged(&sealed,records,sizeof(records));
        assert(!memcmp(&guard,&s_priority_guard_work,sizeof(guard)) && ring.enabled && !guard_cancels);
    }
    stopped_ring();trace_service();seal_guard_service();
    seal_assert_unchanged(&sealed,records,sizeof(records));
    assert(!memcmp(&guard,&s_priority_guard_work,sizeof(guard)));
    if(!strcmp(seal_case,"release_new_arm")) {
        core=0u;assert(vdc_dpll_manager_priority_trace_release());trace_service();
        assert(s_priority_guard_work.state==VDC_PRIORITY_GUARD_DISABLED);
        assert(vdc_dpll_manager_priority_trace_guard_arm(2u,false,60u));trace_service();
        assert(s_priority_guard_work.state==VDC_PRIORITY_GUARD_ARMED && s_priority_guard_work.capture_id==2u);
        assert(!s_priority_guard_work.passed_mask && !s_priority_guard_work.reason_mask &&
            !s_priority_guard_work.checked_s && !s_priority_guard_started);
        assert(!trace_status().record_count && !trace_status().match_count);
    }
    return 0;
}
'''
