"""Correlated raw interval bounds against real absolute Domain outputs.

The independent oracle samples common oscillator/divider phases, including
TIMER0's microsecond staircase. Production mapper and controller are linked;
no new timestamp coordinate or physical precision claim is inferred here.
"""
from dataclasses import dataclass, replace
from fractions import Fraction
import itertools
import json
import math
import random
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import follow_executable, domain_sources  # noqa: F401

M = 10**9
U64 = (1 << 64) - 1
LO_SENTINEL, HI_SENTINEL = U64 - 1234, U64 - 5678


@dataclass(frozen=True)
class Case:
    valid: int = 1
    nominal: int = 1_500_000
    lock: int = 1
    base: int = 1234
    output: int = 5_000_000_000
    rate: int = 0
    phase: int = -77
    hz: int = 250_000_000
    ticks: int = 375_000_000
    first: int = 3_000_000_000
    second: int = 4_500_000_000
    flags: int = 0

    def line(self):
        return " ".join(map(str, vars(self).values()))


def absolute_oracle(case, local):
    if not case.valid or not case.nominal or case.lock > 8 or local < case.base:
        return None
    elapsed = local - case.base
    result = case.output + elapsed + int(Fraction(elapsed * case.rate, M)) + case.phase
    return result if 0 <= result <= U64 else None


def delta_oracle(case):
    if (case.flags or not case.valid or not case.nominal or case.lock > 8 or
            not 0 < case.hz <= 500_000_000 or not 0 < case.ticks <= 10 * case.hz or
            case.rate <= -M):
        return None
    interval = Fraction(case.ticks * M, case.hz)
    low = max(0, math.floor(interval) - 1000)
    high = math.ceil(interval) + 1000
    factor = Fraction(M + case.rate, M)
    return math.floor(low * factor), math.ceil(high * factor)


@pytest.fixture(scope="module")
def delta_executable(follow_executable, tmp_path_factory):
    # Reuse the production-expanded owner TU, without replacing its matcher,
    # model, Domain or controller. The old main remains compiled but uncalled.
    source = follow_executable.with_suffix(".c").read_text(encoding="utf-8")
    assert "int main(int argc,char **argv)" in source
    source = source.replace("int main(int argc,char **argv)", "int prior_follow_main(int argc,char **argv)", 1)
    return compile_executable(tmp_path_factory.mktemp("priority-delta"), "priority_delta", source + HARNESS,
        domain_sources() + [ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c",
                           ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"])


def run(executable, name, cases=None):
    data = None if cases is None else "\n".join(case.line() for case in cases) + "\n"
    command = [str(executable), name]
    result = subprocess.run(command, input=data, capture_output=True, text=True, timeout=30)
    (executable.parent / f"{name}.json").write_text(json.dumps(dict(command=command, input=data,
        returncode=result.returncode, stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def check(executable, name, cases, containment=False):
    output = run(executable, name, cases)
    rows = [tuple(map(int, row.split())) for row in output.splitlines()]
    assert len(rows) == len(cases)
    valid_pairs = 0
    for case, (ok, lo, hi, valid0, y0, valid1, y1) in zip(cases, rows, strict=True):
        expected = delta_oracle(case)
        assert (ok, lo, hi) == ((0, LO_SENTINEL, HI_SENTINEL) if expected is None else (1, *expected)), case
        for valid, actual, local in ((valid0, y0, case.first), (valid1, y1, case.second)):
            point = absolute_oracle(case, local)
            assert (valid, actual) == ((0, LO_SENTINEL) if point is None else (1, point)), case
        if containment and ok and valid0 and valid1:
            assert lo <= y1 - y0 <= hi, (case, lo, y1-y0, hi)
            valid_pairs += 1
    return valid_pairs


def test_real_mapper_signed_rates_and_divider_phases(delta_executable):
    cases = []
    for hz, ticks, rate, phase in itertools.product(
            [125_000_000, 250_000_000, 333_333_333, 499_999_999, 500_000_000],
            [1, 123, 10001, 123456789], [-999_999_999, -10000, -1, 0, 1, 10000, 2147483647],
            [Fraction(0), Fraction(1, 4), Fraction(999), Fraction(999999, 1000)]):
        first = Fraction(3_000_000_000) + phase
        second = first + Fraction(ticks * M, hz)
        pairs = set(itertools.product([math.floor(first), math.ceil(first)], [math.floor(second), math.ceil(second)]))
        pairs.add((1000 * math.floor(first / 1000), 1000 * math.floor(second / 1000)))
        cases.extend(Case(hz=hz, ticks=ticks, rate=rate, first=a, second=b) for a, b in pairs)
    assert check(delta_executable, "phases", cases, True) > 2000


def test_real_timer0_all_microsecond_phases(delta_executable):
    cases = []
    for phase, rate, extra in itertools.product(range(1000), [-10000, -1, 0, 1, 10000], [0, 1, 249]):
        ticks = 375_000_000 + extra
        first = Fraction(3_000_000_000 + phase)
        second = first + ticks * 4
        cases.append(Case(rate=rate, ticks=ticks, first=1000*math.floor(first/1000),
                          second=1000*math.floor(second/1000)))
    assert check(delta_executable, "timer0_phases", cases, True) == len(cases)


def test_uint64_base_crossing_cancellation_and_random_phases(delta_executable):
    rng = random.Random(0xC021)
    cases = []
    for _ in range(3000):
        hz = rng.choice([1, 3, 125_000_000, 250_000_000, 333_333_333, 500_000_000])
        ticks = rng.randrange(1, 2 * hz + 1)
        first = Fraction(rng.choice([3_000_000_000, U64-3_000_000_000, rng.randrange(10**15)])) + Fraction(rng.randrange(1000), 1000)
        second = first + Fraction(ticks*M, hz)
        quantize = rng.choice([math.floor, math.ceil, lambda n: 1000*math.floor(n/1000)])
        a, b = quantize(first), quantize(second)
        cases.append(Case(hz=hz, ticks=ticks, first=a, second=b,
            base=rng.choice([0, a, a+1]), output=rng.choice([0, M, U64//2]),
            phase=rng.choice([-2147483648, 0, 2147483647]),
            rate=rng.choice([-999999999, -10000, -1, 0, 1, 10000, 2147483647])))
    assert check(delta_executable, "uint64_random", cases, True) > 500


def test_invalid_bounds_and_null_alias_leave_outputs_unchanged(delta_executable):
    cases = [replace(Case(), **change) for change in [
        dict(hz=0), dict(hz=500000001), dict(ticks=0), dict(ticks=2500000001),
        dict(ticks=U64), dict(valid=0), dict(nominal=0), dict(lock=9), dict(lock=U64 & 0xffffffff),
        dict(rate=-M), dict(rate=-2147483648), *[dict(flags=flag) for flag in (1,2,4,8,15)],
    ]]
    check(delta_executable, "invalid", cases)


def test_long_elapsed_quantization_scaling_and_uint64_intermediate(delta_executable):
    cases=[]
    for hz,seconds,extra,rate in itertools.product(
        [1,3,125000000,250000000,333333333,499999999,500000000],
        [2,4,8,10],[-1,0,1],[-999999999,-10000,-1,0,1,10000,2147483647]):
        ticks=seconds*hz+extra
        first=3*M+999
        second=math.floor(Fraction(first)+Fraction(ticks*M,hz))
        cases.append(Case(hz=hz,ticks=ticks,rate=rate,first=first,second=second))
    assert check(delta_executable,"long_scale",cases,True)>500


@pytest.mark.parametrize("case", ["wide_bridge", "empty_intersection", "model_revision", "model_valid_from",
                                 "anchor_change", "clock_change", "session_change", "path_change", "raw_reverse",
                                 "base_crossing"])
def test_owner_bound_interval_intersection(delta_executable, case):
    run(delta_executable, case)


HARNESS = r'''
static void differential_owner(const char *name)
{
    setup(6000);
    /* Preserve real bridge/absolute projection but widen the common anchor
     * from one raw tick to 1001 ticks. This is the uncertainty being removed
     * from the interval; it remains present in each absolute MATCH. */
    live.timer1_enable_after=live.timer1_enable_before+1001u;
    event(100,0);raw_now+=2000u;tick();
    assert(status().baselines==1u && !status().applied);
    const vdc_priority_match_snapshot_t first=s_priority_match_work.status;
    event(200,1500000000u);raw_now+=2000u;
    if(!strcmp(name,"anchor_change"))++live.timer1_enable_before;
    if(!strcmp(name,"clock_change"))++s_vdc_domain.clock.run_id;
    if(!strcmp(name,"session_change"))++s_model_feedback_session;
    if(!strcmp(name,"path_change"))++s_vdc_domain.path_delay.table_crc32;
    if(!strcmp(name,"model_revision")){++s_vdc_domain.dco.period_adjust_ppb;publish();}
    if(!strcmp(name,"model_valid_from")) {
        /* Simulate a corrupt/revised published validity start while retaining
         * its token: stale baseline must never feed a new correlated pair. */
        vdc_dpll_manager_committed_model_t revised=model();
        revised.valid_from_raw=first.raw_hi+1u;
        boundary_store(s_committed_model_words,&revised,sizeof(revised));
    }
    if(!strcmp(name,"empty_intersection"))
        /* The old fixture moved the TIMER0 bridge read.  TIMER1 projections
         * no longer consume that coordinate, so inject an inconsistent remote
         * encoded timestamp while retaining the same transport binding. */
        priority_rx.typed_record.event_time_lower += 10000000000ull;
    if(!strcmp(name,"raw_reverse"))exact.record.rx_elapsed_cycles=first.raw_lo-live.timer1_enable_before-1u;
    const vdc_domain_context_t before=s_vdc_domain;
    prepare();
    if(!strcmp(name,"empty_intersection")) {
        fprintf(stderr,"empty status matched=%u rejected=%u prepared=%u reason=%u baselines=%u local=%" PRIu64 "..%" PRIu64 " expected=%" PRIu64 "..%" PRIu64 "\n",
            s_priority_match_work.status.matched,status().rejected,status().prepared,status().last_reason,status().baselines,
            s_priority_follow_work.local_delta_lo,s_priority_follow_work.local_delta_hi,
            s_priority_follow_work.expected_delta_lo,s_priority_follow_work.expected_delta_hi);
        assert(s_priority_match_work.status.matched==2u && status().rejected==0u && !status().prepared);
        assert(status().last_reason==VDC_PRIORITY_FOLLOW_INTERVAL && status().baselines==2u);
    }
    if(!strcmp(name,"model_valid_from")) {
        assert(s_priority_match_work.status.matched==2u && status().baselines==2u && !status().prepared);
        assert(status().last_reason==VDC_PRIORITY_FOLLOW_MODEL);
    }
    if(!strcmp(name,"wide_bridge")) {
        assert(status().prepared==1u);
        const vdc_priority_match_snapshot_t last=s_priority_match_work.status;
        const uint64_t absolute_lo=last.local_lo-first.local_hi;
        const uint64_t absolute_hi=last.local_hi-first.local_lo;
        apply();const vdc_priority_follow_snapshot_t decision=status();
        assert(decision.applied==1u && decision.local_interval_lo>=absolute_lo && decision.local_interval_hi<=absolute_hi);
        assert(decision.local_interval_hi-decision.local_interval_lo<absolute_hi-absolute_lo);
        printf("NARROW %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",absolute_lo,absolute_hi,
            decision.local_interval_lo,decision.local_interval_hi);
    } else {
        apply();assert(!status().applied && !memcmp(&before,&s_vdc_domain,sizeof(before)));
    }
}
static void base_crossing(void)
{
    setup(6000);
    /* The pure difference helper cannot authorize endpoints before base.
     * Real absolute projection must reject a bracket crossing that base. */
    s_vdc_domain.dco.base_local_tick64=now_ns-100u;
    raw_now=500000000u;publish();event(100,0);tick();
    assert(!s_priority_match_work.status.matched && !status().baselines && !status().applied);
    assert(s_priority_match_work.status.last_reason==VDC_PRIORITY_MATCH_PROJECTION);
    event(200,1500000000u);tick();
    assert(s_priority_match_work.status.matched==1u && status().baselines==1u && !status().applied);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"phases") || !strcmp(argv[1],"timer0_phases") ||
       !strcmp(argv[1],"uint64_random") || !strcmp(argv[1],"invalid") || !strcmp(argv[1],"long_scale")) {
        vdc_dco_control_t d={0};uint32_t hz,flags;uint64_t ticks,first,second;
        while(scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu64 " %" SCNu64 " %" SCNd32 " %" SCNd32
            " %" SCNu32 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu32,
            &d.valid,&d.nominal_period_ns,&d.lock_state,&d.base_local_tick64,&d.base_vdc_time64_ns,
            &d.period_adjust_ppb,&d.phase_offset_ns,&hz,&ticks,&first,&second,&flags)==12) {
            uint64_t lo=UINT64_MAX-1234,hi=UINT64_MAX-5678,y0=UINT64_MAX-1234,y1=UINT64_MAX-1234;
            const vdc_dco_control_t saved=d;
            bool ok=vdc_model_project_correlated_delta(flags&1?NULL:&d,hz,ticks,
                flags&2?NULL:&lo,flags&4?NULL:flags&8?&lo:&hi);
            assert(!memcmp(&saved,&d,sizeof(d)));
            bool valid0=vdc_domain_dco_local_to_output_ns(&d,first,&y0);
            bool valid1=vdc_domain_dco_local_to_output_ns(&d,second,&y1);
            printf("%u %" PRIu64 " %" PRIu64 " %u %" PRIu64 " %u %" PRIu64 "\n",
                (unsigned)ok,lo,hi,(unsigned)valid0,y0,(unsigned)valid1,y1);
        }
    } else if(!strcmp(argv[1],"base_crossing"))base_crossing();
    else differential_owner(argv[1]);
    return 0;
}
'''
