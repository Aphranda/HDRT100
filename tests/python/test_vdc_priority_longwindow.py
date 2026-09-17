"""Long follower windows: real matcher, Domain, recorder and exact arithmetic.

Counting probes retain the production arithmetic and controller. The
independent Python oracle uses Fraction. Host tests do not qualify pad phase,
clock lifetime or hardware lock; only fresh endpoints enter each secant.
"""
import json
from fractions import Fraction
import math
import random
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import domain_sources, ratio_oracle
from test_vdc_priority_trace import native_trace, trace_executable  # noqa: F401
from test_vdc_priority_window import window_executable  # noqa: F401


@pytest.fixture(scope="module")
def longwindow_executable(window_executable,tmp_path_factory):
    source=window_executable.with_suffix(".c").read_text(encoding="utf-8")
    source=source.replace("int main(int argc,char **argv)","int previous_window_main(int argc,char **argv)",1)
    return compile_executable(tmp_path_factory.mktemp("long-window"),"long_window",source+HARNESS,
        domain_sources()+[ROOT/"components/vdc_dpll_manager/src/vdc_feedback_match.c",
                         ROOT/"components/distributed_refmem/src/refmem_sync_vdc_feedback.c"])


def run(executable,name):
    result=subprocess.run([str(executable),name],capture_output=True,timeout=30)
    (executable.parent/f"{name}.bin").write_bytes(result.stdout)
    (executable.parent/f"{name}.json").write_text(json.dumps(dict(returncode=result.returncode,
        stderr=result.stderr.decode("utf-8",errors="replace"),stdout_bytes=len(result.stdout)),indent=2),encoding="utf-8")
    assert result.returncode==0,result.stderr.decode("utf-8",errors="replace")
    return result.stdout


def test_five_real_decisions_retain_one_baseline_until_final(longwindow_executable):
    _,rows=native_trace(run(longwindow_executable,"five_windows"))
    decisions=[r for r in rows if r["kind"]==2]
    assert [r["event"] for r in decisions]==[101,201,301,401,501]
    assert {r["baseline"] for r in decisions}=={100}
    for r in decisions:
        assert (r["error_lo"],r["error_hi"])==ratio_oracle(r["local_lo"],r["local_hi"],r["expected_lo"],r["expected_hi"])
        assert not r["delta"] and r["before_seq"]==r["after_seq"]


@pytest.mark.parametrize("case",["skip_four","skip_final","busy_supersession","long_stop","long_age","long_model"])
def test_long_window_budget_and_lifetime(longwindow_executable,case):
    run(longwindow_executable,case)


def test_small_frequency_error_becomes_real_domain_update_at_eight_seconds(longwindow_executable):
    _,rows=native_trace(run(longwindow_executable,"refine_long"))
    decisions=[r for r in rows if r["kind"]==2]
    assert len(decisions)==5 and {r["baseline"] for r in decisions}=={100}
    assert all(not r["delta"] for r in decisions[:-1])
    final=decisions[-1]
    assert final["error_lo"]>10 and final["delta"]<0
    assert final["after_seq"]==final["before_seq"]+1
    assert final["after_ppb"]==final["before_ppb"]+final["delta"]
    for r in decisions:
        assert (r["error_lo"],r["error_hi"])==ratio_oracle(r["local_lo"],r["local_hi"],r["expected_lo"],r["expected_hi"])


def numeric(executable,name,rows):
    data="\n".join(" ".join(map(str,r)) for r in rows)+"\n"
    result=subprocess.run([str(executable),name],input=data,text=True,capture_output=True,timeout=30)
    (executable.parent/f"{name}.json").write_text(json.dumps(dict(input=data,returncode=result.returncode,
        stdout=result.stdout,stderr=result.stderr),indent=2),encoding="utf-8")
    assert result.returncode==0,result.stdout+result.stderr
    values=[tuple(map(int,line.split())) for line in result.stdout.splitlines()]
    assert len(values)==len(rows)
    return values


def test_scale_split_product_extremes_without_unsigned_wrap(longwindow_executable):
    u64=2**64-1
    values=[0,1,999,1000,2*10**9-1000,2*10**9+1000,4*10**9+1000,8*10**9+1000,10**10+1000,u64//2,u64-1,u64]
    rows=[(rate,n,up) for rate in [-2**31,-10**9,-999999999,-10000,-1,0,1,10000,2**31-1]
          for n in values for up in [0,1]]
    for (rate,n,up),(ok,actual) in zip(rows,numeric(longwindow_executable,"scale",rows),strict=True):
        exact=Fraction(n*(10**9+rate),10**9)
        expected=math.ceil(exact) if up else math.floor(exact)
        valid=rate>-10**9 and 0<=expected<=u64
        assert (ok,actual)==((1,expected) if valid else (0,u64-1234)),(rate,n,up,ok,actual,expected)


def test_ten_second_ratio_and_signed_conversion_boundaries(longwindow_executable):
    rows=[(10**10,10**10,1,1),(9223372036,9223372036,1,1),(9223372037,9223372037,1,1),
          (9223372038,9223372038,1,1),(10**10,10**10,10**10,10**10),
          (10**10,10**10,2,3),(1,1,10**10,10**10),(0,1,1,1),(10**10+1,10**10+1,10**10,10**10)]
    rng=random.Random(0x10A6)
    for _ in range(1000):
        local=rng.randrange(1,10**10+1);ref=rng.choice([1,2,3,rng.randrange(1,10**10+1)])
        rows.append((local,rng.randrange(local,10**10+1),ref,rng.randrange(ref,10**10+1)))
    for row,(reason,lo,hi) in zip(rows,numeric(longwindow_executable,"ratio",rows),strict=True):
        expected=ratio_oracle(*row) if max(row)<=10**10 else None
        assert (reason==0)==(expected is not None),(row,reason,lo,hi,expected)
        if expected is not None:assert (lo,hi)==expected
        else:assert (lo,hi)==(12345,-6789)


HARNESS=r'''
static void long_five_windows(void)
{
    const uint64_t times[]={UINT64_C(1010000000),UINT64_C(1510000000),UINT64_C(1810000000),
        UINT64_C(4010000000),UINT64_C(8010000000)};
    window_setup(0);
    for(unsigned i=0;i<5u;++i) {
        window_event(101u+i*100u,times[i]);window_cost(i+1u);
        assert(status().baseline_sequence==100u && status().event_sequence==101u+i*100u);
        assert(status().no_adjust==i+1u && !status().applied);
        if(i<4u)assert(s_priority_follow_work.previous.sequence==100u);
    }
    assert(s_priority_follow_work.previous.sequence==501u);
    window_event(601u,UINT64_C(8020000000));window_cost(5u);
    frozen_trace();export_trace();
}
static void long_cases(const char *name)
{
    window_setup(!strcmp(name,"refine_long")?150:0);
    const uint64_t times[]={UINT64_C(1010000000),UINT64_C(1510000000),UINT64_C(1810000000),
        UINT64_C(4010000000),UINT64_C(8010000000)};
    if(!strcmp(name,"skip_four") || !strcmp(name,"skip_final")) {
        const bool final=!strcmp(name,"skip_final");
        window_event(401u,final?times[4]:times[3]);window_cost(1u);
        window_event(402u,(final?times[4]:times[3])+UINT64_C(10000000));window_cost(1u);
        if(!final){window_event(501u,times[4]);window_cost(2u);}
        return;
    }
    for(unsigned i=0;i<4u;++i) {
        if(!strcmp(name,"busy_supersession")) {
            event(101u+i*100u,times[i]);prepare();window_cost(i+1u);
            ring_available=false;
            for(unsigned j=0;j<10u;++j){apply();window_cost(i+1u);}
            ring_available=true;event(102u+i*100u,times[i]+10000000u);prepare();apply();window_cost(i+1u);
        } else {window_event(101u+i*100u,times[i]);window_cost(i+1u);}
    }
    if(!strcmp(name,"refine_long")) {
        window_event(501u,times[4]);window_cost(5u);
        assert(status().applied==1u && s_vdc_domain.dco.period_adjust_ppb<150);
        frozen_trace();export_trace();return;
    }
    if(!strcmp(name,"busy_supersession")) {
        event(501u,times[4]);prepare();window_cost(5u);
        ring_available=false;apply();ring_available=true;
        window_event(502u,times[4]+10000000u);window_cost(5u);
        assert(s_priority_follow_work.previous.sequence==502u && !s_priority_follow_work.pending);
        window_event(503u,times[4]+20000000u);window_cost(5u);return;
    }
    if(!strcmp(name,"long_stop")){stopped_ring();core=1;tick();assert(!s_priority_follow_work.have_baseline);return;}
    if(!strcmp(name,"long_age")) {
        now_ms+=121u;raw_now+=(uint64_t)121u*BOARD_SYS_CLOCK_HZ/1000u;tick();
        assert(!s_priority_follow_work.have_baseline && status().last_reason==VDC_PRIORITY_FOLLOW_AGE);return;
    }
    if(!strcmp(name,"long_model")) {
        ++s_vdc_domain.dco.period_adjust_ppb;publish();window_event(450u,UINT64_C(4110000000));window_cost(4u);
        assert(s_priority_follow_work.previous.sequence==450u);
        window_event(501u,UINT64_C(4500000000));window_cost(4u);return;
    }
    assert(0);
}
int main(int argc,char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    assert(argc==2);
    if(!strcmp(argv[1],"five_windows"))long_five_windows();
    else if(!strcmp(argv[1],"scale")) {
        int32_t rate;uint64_t value;unsigned up;
        while(scanf("%"SCNd32" %"SCNu64" %u",&rate,&value,&up)==3) {
            uint64_t out=UINT64_MAX-1234;
            bool ok=vdc_model_scale_elapsed_ns(rate,value,up!=0u,&out);
            printf("%u %"PRIu64"\n",(unsigned)ok,out);
        }
    } else if(!strcmp(argv[1],"ratio")) {
        uint64_t a,b,c,d;
        while(scanf("%"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64,&a,&b,&c,&d)==4) {
            int64_t lo=12345,hi=-6789;uint32_t reason=priority_follow_rate_interval(a,b,c,d,&lo,&hi);
            printf("%u %"PRId64" %"PRId64"\n",reason,lo,hi);
        }
    } else long_cases(argv[1]);
    return 0;
}
'''
