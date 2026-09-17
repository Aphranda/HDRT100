"""Bounded retries of one actual local frequency window, with native trace.

Production matcher/projector/controller/Domain/recorder are linked. Counting
probes wrap the two real math calls in the generated host TU; neither their
math nor the control policy is duplicated. No hardware lock claim is made.
"""
import json
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_command_ingress import ingress_definition
from test_vdc_priority_follow import domain_sources, ratio_oracle
from test_vdc_priority_trace import trace_executable, native_trace  # noqa: F401


def instrument(source):
    source = source.replace("int main(int argc,char **argv)", "int previous_trace_main(int argc,char **argv)", 1)
    signature = "static uint32_t priority_follow_rate_interval"
    position = source.index(signature)
    source = source[:position] + (
        "static unsigned window_ratio_calls,window_projection_calls;\n"
        "#define vdc_model_project_correlated_delta(...) "
        "(window_projection_calls++,vdc_model_project_correlated_delta(__VA_ARGS__))\n"
    ) + source[position:]
    function = ingress_definition(source, "priority_follow_rate_interval")
    source = source.replace(function, function.replace("{", "{\n++window_ratio_calls;", 1), 1)
    return source + HARNESS


@pytest.fixture(scope="module")
def window_executable(trace_executable, tmp_path_factory):
    source = trace_executable.with_suffix(".c").read_text(encoding="utf-8")
    return compile_executable(tmp_path_factory.mktemp("priority-window"), "priority_window", instrument(source),
        domain_sources() + [ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c",
                           ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"])


def run(executable, case):
    command = [str(executable), case]
    result = subprocess.run(command, capture_output=True, timeout=30)
    (executable.parent / f"{case}.bin").write_bytes(result.stdout)
    (executable.parent / f"{case}.json").write_text(json.dumps(dict(command=command,
        returncode=result.returncode, stderr=result.stderr.decode("utf-8", errors="replace"),
        stdout_bytes=len(result.stdout)), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stderr.decode("utf-8", errors="replace")
    return result.stdout


@pytest.mark.parametrize("case,expected", [("refine", [101,201]), ("three_attempts", [101,201,301]),
                                         ("skipped_threshold", [201,301])])
def test_native_decisions_keep_same_baseline_but_consume_new_events(window_executable, case, expected):
    _, rows = native_trace(run(window_executable, case))
    decisions = [row for row in rows if row["kind"] == 2]
    assert [row["event"] for row in decisions] == expected
    assert {row["baseline"] for row in decisions} == {100}
    assert len({row["event"] for row in decisions}) == len(decisions)
    for row in decisions:
        assert (row["error_lo"], row["error_hi"]) == ratio_oracle(
            row["local_lo"], row["local_hi"], row["expected_lo"], row["expected_hi"])
        assert row["after_ppb"] == row["before_ppb"] + row["delta"]
        assert row["after_seq"] == row["before_seq"] + bool(row["delta"])
    if case == "refine":
        first, last = decisions
        assert first["error_lo"] <= 10 <= first["error_hi"] and not first["delta"]
        assert last["error_lo"] > 10 and last["delta"] < 0 and last["after_seq"] == last["before_seq"]+1
    else:
        assert all(not row["delta"] and row["after_seq"] == row["before_seq"] for row in decisions)


@pytest.mark.parametrize("case", ["duplicates", "waiting_cost", "max_reset", "model_reset", "age_reset",
                                 "stop_reset", "saturation_reset", "domain_failure_reset", "inside_deadband_reset",
                                 "pending_busy", "pending_supersession", "late_only", "exact_thresholds",
                                 "max_upper", "max_upper_exceeded"])
def test_window_lifetime_and_computation_budget(window_executable, case):
    run(window_executable, case)


HARNESS = r'''
static void window_setup(int32_t rate)
{
    setup(rate);armed_trace(1);running_ring();event(100,0);tick();
    assert(status().baselines==1u && !status().prepared && !window_ratio_calls && !window_projection_calls);
}
static void window_event(uint32_t sequence,uint64_t elapsed)
{ event(sequence,elapsed);tick(); }
static void window_noadjust(void)
{
    assert(status().no_adjust && !status().applied && status().last_reason==VDC_PRIORITY_FOLLOW_DEADBAND);
    assert(status().baseline_sequence==100u);
}
static void window_cost(unsigned expected)
{
    assert(window_ratio_calls==expected && window_projection_calls==expected);
    assert(status().prepared==expected);
}
static void window_trace(const char *name)
{
    window_setup(!strcmp(name,"refine")?700:0);
    if(strcmp(name,"skipped_threshold")) {
        window_event(101,1010000000u);window_noadjust();window_cost(1);
        assert(status().baselines==1u);
        for(unsigned i=0;i<20u;++i) {
            window_event(120+i,1020000000u+i*20000000u);window_cost(1);
        }
    }
    window_event(201,1510000000u);
    if(!strcmp(name,"refine")) {
        assert(status().applied==1u && status().baseline_sequence==100u && status().event_sequence==201u);
        assert(status().selected_delta_ppb<0 && s_vdc_domain.dco.period_adjust_ppb<700);
        window_cost(2);
    } else {
        window_noadjust();window_cost(!strcmp(name,"skipped_threshold")?1u:2u);
        window_event(250,1600000000u);window_event(251,1700000000u);
        window_cost(!strcmp(name,"skipped_threshold")?1u:2u);
        window_event(301,1810000000u);window_noadjust();
        window_cost(!strcmp(name,"skipped_threshold")?2u:3u);
        for(unsigned i=0;i<10u;++i)window_event(310+i,1820000000u+i*10000000u);
        window_cost(!strcmp(name,"skipped_threshold")?2u:3u);
    }
    assert_remote_metadata();frozen_trace();export_trace();
}
static void window_cases(const char *name)
{
    window_setup(!strcmp(name,"saturation_reset")?10000:0);
    if(!strcmp(name,"exact_thresholds")) {
        const uint64_t targets[]={999999999u,1000000000u,1499999999u,1500000000u,1799999999u,1800000000u};
        for(unsigned i=0;i<6u;++i) {
            event(101+i,((targets[i]+11u)/4u)*4u);
            priority_rx.typed_record.event_time_lower=UINT64_C(12000000000)+targets[i]+7u;
            tick();window_cost((i+1u)/2u);
        }
        assert(status().no_adjust==3u && !status().applied);
        assert(s_priority_follow_work.previous.sequence==106u);return;
    }
    if(!strcmp(name,"max_upper") || !strcmp(name,"max_upper_exceeded")) {
        event(201,1999998000u);
        priority_rx.typed_record.event_time_lower=UINT64_C(12000000000)+2000000000u-7u+
            (!strcmp(name,"max_upper_exceeded")?1u:0u);
        prepare();window_cost(!strcmp(name,"max_upper")?1u:0u);
        if(!strcmp(name,"max_upper"))assert(s_priority_follow_work.expected_delta_hi==2000000000u);
        else assert(status().baselines==2u && !s_priority_follow_work.pending);
        return;
    }
    if(!strcmp(name,"waiting_cost")) {
        for(unsigned i=1;i<100u;++i)window_event(100+i,i*10000000u);
        window_cost(0);assert(status().baselines==1u);return;
    }
    if(!strcmp(name,"domain_failure_reset")) {
        s_vdc_domain.dco.dco_update_seq=UINT32_MAX;publish();
        event(101,10000000u);tick();
        event(201,1520000000u);
        priority_rx.typed_record.event_time_lower-=100000u;
        prepare();assert(status().prepared==1u);
        const vdc_domain_context_t before=s_vdc_domain;apply();
        assert(status().last_reason==VDC_PRIORITY_FOLLOW_DOMAIN && status().rejected==1u);
        assert(!memcmp(&before,&s_vdc_domain,sizeof(before)) && !s_priority_follow_work.have_baseline);
        return;
    }
    if(!strcmp(name,"saturation_reset")) {
        /* Remote output runs faster, asking to increase an already saturated
         * real local DCO. The final Domain limit returns a zero correction. */
        event(101,1010000000u);priority_rx.typed_record.event_time_lower+=100000u;tick();
        assert(status().no_adjust==1u && status().last_reason==VDC_PRIORITY_FOLLOW_DOMAIN);
        assert(s_priority_follow_work.previous.sequence!=100u || !s_priority_follow_work.have_baseline);
        return;
    }
    event(101,1010000000u);prepare();window_cost(1);
    if(!strcmp(name,"inside_deadband_reset")) {
        /* Isolate final decision policy using a valid real owner ticket.
         * This narrow admitted interval cannot arise from the conservative
         * current bridge, so it is an explicit fault-injection policy test. */
        s_priority_follow_work.error_lo=-10;s_priority_follow_work.error_hi=10;
        apply();assert(status().no_adjust==1u && status().last_reason==VDC_PRIORITY_FOLLOW_DEADBAND);
        assert(s_priority_follow_work.previous.sequence!=100u || !s_priority_follow_work.have_baseline);return;
    }
    if(!strcmp(name,"pending_busy") || !strcmp(name,"pending_supersession")) {
        ring_available=false;
        for(unsigned i=0;i<10u;++i){apply();window_cost(1);}
        ring_available=true;
        if(!strcmp(name,"pending_busy")) {
            apply();window_noadjust();window_cost(1);assert(status().baselines==1u);return;
        }
        event(150,1050000000u);prepare();window_cost(1);apply();
        assert(status().baselines==1u);
        window_event(201,1510000000u);window_cost(2);
        window_event(301,1810000000u);window_cost(3);
        window_event(302,1850000000u);window_cost(3);return;
    }
    apply();window_noadjust();window_cost(1);
    if(!strcmp(name,"duplicates")) {
        const uint32_t decisions=trace_status().decision_count;
        for(unsigned i=0;i<100u;++i)tick();
        assert(trace_status().decision_count==decisions && status().baselines==1u);window_cost(1);return;
    }
    if(!strcmp(name,"late_only")) {
        window_event(201,1900000000u);window_cost(2);
        window_event(301,1910000000u);window_event(401,1920000000u);window_cost(2);return;
    }
    if(!strcmp(name,"max_reset")) {
        window_event(201,2010000000u);window_cost(1);
        assert(s_priority_follow_work.previous.sequence==201u && status().baselines==2u);
        window_event(301,3019999996u);window_cost(2);assert(status().baseline_sequence==201u);return;
    }
    if(!strcmp(name,"model_reset")) {
        ++s_vdc_domain.dco.period_adjust_ppb;publish();
        window_event(150,1100000000u);window_cost(1);
        assert(s_priority_follow_work.previous.sequence==150u && status().baselines==2u);
        window_event(201,1510000000u);window_cost(1);return;
    }
    if(!strcmp(name,"age_reset")) {
        now_ms+=VDC_PRIORITY_FOLLOW_MAX_AGE_MS+1u;
        raw_now+=(uint64_t)(VDC_PRIORITY_FOLLOW_MAX_AGE_MS+1u)*BOARD_SYS_CLOCK_HZ/1000u;
        tick();assert(status().last_reason==VDC_PRIORITY_FOLLOW_AGE && !s_priority_follow_work.have_baseline);
        window_event(201,1510000000u);window_cost(1);assert(status().baselines==2u);return;
    }
    if(!strcmp(name,"stop_reset")) {
        stopped_ring();core=1;tick();assert(!s_priority_follow_work.have_baseline && !status().active);
        running_ring();window_event(201,1510000000u);window_cost(1);return;
    }
    assert(0);
}
int main(int argc,char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    assert(argc==2);
    if(!strcmp(argv[1],"refine") || !strcmp(argv[1],"three_attempts") || !strcmp(argv[1],"skipped_threshold"))window_trace(argv[1]);
    else window_cases(argv[1]);
    return 0;
}
'''
