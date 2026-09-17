"""Continuous-event differences against real Domain outputs and Fraction.

The unknown common affine phase is varied independently of rate/model base.
The old helper retains its TIMER0 staircase scope. Neither helper establishes
clock lifetime, event identity, owner eligibility or physical timing accuracy.
"""
from dataclasses import dataclass, replace
from fractions import Fraction
import itertools
import json
import math
import random
import subprocess

import pytest

from test_vdc_command_owner import compile_executable
from test_vdc_priority_follow import domain_sources

M = 10**9
U64 = (1 << 64) - 1
LO_SENTINEL, HI_SENTINEL = U64 - 1234, U64 - 5678


@dataclass(frozen=True)
class Case:
    valid: int = 1
    nominal: int = 1500000
    lock: int = 1
    base: int = 1234
    output: int = 5000000000
    rate: int = 0
    phase: int = -77
    hz: int = 250000000
    ticks: int = 375000001
    first: int = 3000000999
    second: int = 4500001003
    flags: int = 0

    def line(self):
        return " ".join(map(str, vars(self).values()))


def domain_oracle(case, local):
    if not case.valid or not case.nominal or case.lock > 8 or local < case.base:
        return None
    elapsed = local - case.base
    value = case.output + elapsed + int(Fraction(elapsed * case.rate, M)) + case.phase
    return value if 0 <= value <= U64 else None


def bounds(case, coarse=False):
    if (case.flags or not case.valid or not case.nominal or case.lock > 8 or
            not 0 < case.hz <= 500000000 or not 0 < case.ticks <= 10*case.hz or
            case.rate <= -M):
        return None
    elapsed = Fraction(case.ticks*M, case.hz)
    low, high = math.floor(elapsed), math.ceil(elapsed)
    if coarse:
        low, high = max(0, low-1000), high+1000
    gain = Fraction(M+case.rate, M)
    return math.floor(low*gain), math.ceil(high*gain)


@pytest.fixture(scope="module")
def event_delta_executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp("event-delta"), "event_delta",
                              HARNESS, domain_sources())


def check(executable, name, cases, containment=True):
    data = "\n".join(c.line() for c in cases)+"\n"
    result = subprocess.run([str(executable), "numeric"], input=data,
                            text=True, capture_output=True, timeout=30)
    (executable.parent/f"{name}.json").write_text(json.dumps(dict(input=data,
        returncode=result.returncode, stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    rows = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    admitted = 0
    for c, row in zip(cases, rows, strict=True):
        ok, lo, hi, old_ok, old_lo, old_hi, valid0, y0, valid1, y1 = row
        want, old = bounds(c), bounds(c, coarse=True)
        assert (ok, lo, hi) == ((0, LO_SENTINEL, HI_SENTINEL) if want is None else (1, *want))
        assert (old_ok, old_lo, old_hi) == ((0, LO_SENTINEL, HI_SENTINEL) if old is None else (1, *old))
        for valid, actual, local in ((valid0, y0, c.first), (valid1, y1, c.second)):
            expected = domain_oracle(c, local)
            assert (valid, actual) == ((0, LO_SENTINEL) if expected is None else (1, expected))
        if containment and ok and valid0 and valid1:
            assert lo <= y1-y0 <= hi, (c, row)
            admitted += 1
    return rows, admitted


def continuous_case(hz, ticks, common_phase, rate, **kwargs):
    first = Fraction(3000000000)+common_phase
    second = first+Fraction(ticks*M, hz)
    return Case(hz=hz, ticks=ticks, first=math.floor(first), second=math.floor(second),
                rate=rate, **kwargs)


def test_common_fractional_phase_signed_rates_and_long_horizon(event_delta_executable):
    cases = []
    for hz, seconds, extra, rate, phase in itertools.product(
            [1, 3, 7, 125000000, 250000000, 333333333, 499999999, 500000000],
            [1, 4, 8, 10], [-1, 0, 1], [-999999999, -10000, -1, 0, 1, 10000, 2147483647],
            [Fraction(0), Fraction(1, 3), Fraction(999), Fraction(999999, 1000)]):
        ticks = seconds*hz+extra
        if 0 < ticks <= 10*hz:
            cases.append(continuous_case(hz, ticks, phase, rate))
    assert check(event_delta_executable, "common-phase", cases)[1] == len(cases)


def test_all_microsecond_phases_use_continuous_event_floor(event_delta_executable):
    cases = [continuous_case(250000000, 375000000+extra, Fraction(phase)+Fraction(1, 7), rate)
             for phase, extra, rate in itertools.product(range(1000), [0, 1, 249], [-10000, 0, 10000])]
    rows, admitted = check(event_delta_executable, "us-crossings", cases)
    assert admitted == len(cases)
    assert all(row[2]-row[1] <= 1 for row in rows)


def test_two_rounding_layers_cannot_be_collapsed(event_delta_executable):
    # D=10/3 ns, q=3.147483647. The inner event floor can move by either 3
    # or 4 ns; its Domain difference can escape floor/ceil(D*q).
    cases = [continuous_case(300000000, 1, phase, 2147483647, base=3000000000,
                             output=3000000000, phase=0)
             for phase in [Fraction(0), Fraction(3, 4)]]
    rows, admitted = check(event_delta_executable, "two-rounding-layers", cases)
    assert admitted == 2
    direct = Fraction(10, 3)*Fraction(3147483647, M)
    narrow = math.floor(direct), math.ceil(direct)
    differences = [row[9]-row[7] for row in rows]
    assert any(not narrow[0] <= d <= narrow[1] for d in differences)
    negative = continuous_case(333333333, 1, Fraction(999999999, M), -10000,
                               base=0, output=0, phase=0)
    negative_rows, _ = check(event_delta_executable, "negative-inner-rounding", [negative])
    assert negative_rows[0][1:3] == (2, 4)
    assert negative_rows[0][9]-negative_rows[0][7] == 4
    merged = Fraction(M-10000, 333333333)
    assert (math.floor(merged), math.ceil(merged)) == (2, 3)


def test_staircase_is_a_different_coordinate_and_old_helper_stays(event_delta_executable):
    continuous = Case(base=0, output=0, phase=0)
    coarse = replace(continuous, first=3000000000, second=4500001000)
    rows, _ = check(event_delta_executable, "staircase-counterexample", [continuous, coarse], False)
    assert rows[0][1] <= rows[0][9]-rows[0][7] <= rows[0][2]
    assert not rows[1][1] <= rows[1][9]-rows[1][7] <= rows[1][2]
    assert rows[1][4] <= rows[1][9]-rows[1][7] <= rows[1][5]


def test_uint64_bases_phase_cancellation_and_random_fractional_clocks(event_delta_executable):
    rng = random.Random(0xE7E17)
    cases = []
    for _ in range(3000):
        hz = rng.choice([1, 3, 7, 250000000, 333333333, 500000000])
        ticks = rng.randrange(1, 10*hz+1)
        first = Fraction(rng.choice([30*M, U64-30*M, rng.randrange(10**15)]))+Fraction(rng.randrange(1000), 1000)
        second = first+Fraction(ticks*M, hz)
        a,b = math.floor(first),math.floor(second)
        cases.append(Case(hz=hz,ticks=ticks,first=a,second=b,
            base=rng.choice([0,a,a+1]),output=rng.choice([0,M,U64//2,U64-M]),
            phase=rng.choice([-2147483648,0,2147483647]),
            rate=rng.choice([-999999999,-10000,-1,0,1,10000,2147483647])))
    assert check(event_delta_executable, "uint64-random", cases)[1] > 500


def test_invalid_arguments_keep_output_and_inputs(event_delta_executable):
    cases = [replace(Case(), **change) for change in [
        dict(hz=0),dict(hz=500000001),dict(ticks=0),dict(ticks=2500000001),dict(ticks=U64),
        dict(valid=0),dict(nominal=0),dict(lock=9),dict(lock=0xffffffff),dict(rate=-M),
        dict(rate=-2147483648),*[dict(flags=f) for f in [1,2,4,8,15]]]]
    check(event_delta_executable, "invalid", cases)


def test_absolute_projector_and_mapping_target_continuous_event(event_delta_executable):
    result = subprocess.run([str(event_delta_executable), "absolute"], text=True,
                            capture_output=True, timeout=30)
    assert result.returncode == 0, result.stdout+result.stderr
    assert "continuous absolute pair" in result.stdout


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "vdc_clock_mapping.h"

static void absolute_pair(void)
{
    const vdc_dco_control_t d={.valid=1,.nominal_period_ns=1500000};
    const uint64_t r0=1000000000u,r1=r0+375000001u;
    const uint64_t x0=3000000733u,x1=x0+1500000004u;
    const vdc_timestamp_clock_bridge_t b0={r0+96u,3000001000u,r0+96u,250000000u};
    const vdc_timestamp_clock_bridge_t b1={r1+96u,4500001000u,r1+96u,250000000u};
    uint64_t lo0,hi0,lo1,hi1,y0,y1,dl,dh;
    assert(vdc_model_project_interval(&d,&b0,r0,r0,&lo0,&hi0));
    assert(vdc_model_project_interval(&d,&b1,r1,r1,&lo1,&hi1));
    assert(vdc_domain_dco_local_to_output_ns(&d,x0,&y0));
    assert(vdc_domain_dco_local_to_output_ns(&d,x1,&y1));
    assert(lo0<=y0 && y0<=hi0 && lo1<=y1 && y1<=hi1);
    /* TIMER0(event) would read 3000000000 ns, below this valid projection. */
    assert(lo0==3000000616u && hi0==3000001615u && lo0>3000000000u);
    vdc_clock_mapping_cache_t cache={0};vdc_clock_mapping_result_t mapped;
    assert(vdc_clock_mapping_project(&cache,&d,&b0,1,r0,r0,&mapped)==VDC_CLOCK_MAPPING_OK);
    assert(mapped.output_lo==lo0 && mapped.output_hi==hi0);
    assert(vdc_model_project_event_delta(&d,250000000u,r1-r0,&dl,&dh));
    assert(dl<=y1-y0 && y1-y0<=dh && lo1-hi0<=dh && hi1-lo0>=dl);
    /* This raw arithmetic helper does not authorize a before-base event. */
    vdc_dco_control_t invalid=d;invalid.base_local_tick64=hi0+1u;
    assert(!vdc_model_project_interval(&invalid,&b0,r0,r0,&lo0,&hi0));
    assert(vdc_model_project_event_delta(&invalid,250000000u,r1-r0,&dl,&dh));
    puts("continuous absolute pair; lifecycle/admission still caller-owned");
}

int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"absolute")){absolute_pair();return 0;}
    assert(!strcmp(argv[1],"numeric"));
    vdc_dco_control_t d={0};uint32_t hz,flags;uint64_t ticks,first,second;
    while(scanf("%"SCNu32" %"SCNu32" %"SCNu32" %"SCNu64" %"SCNu64
                " %"SCNd32" %"SCNd32" %"SCNu32" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu32,
        &d.valid,&d.nominal_period_ns,&d.lock_state,&d.base_local_tick64,&d.base_vdc_time64_ns,
        &d.period_adjust_ppb,&d.phase_offset_ns,&hz,&ticks,&first,&second,&flags)==12){
        const vdc_dco_control_t saved=d;
        uint64_t lo=UINT64_MAX-1234,hi=UINT64_MAX-5678;
        uint64_t oldlo=lo,oldhi=hi,y0=lo,y1=lo;
        const bool ok=vdc_model_project_event_delta(flags&1?NULL:&d,hz,ticks,
            flags&2?NULL:&lo,flags&4?NULL:(flags&8?&lo:&hi));
        const bool oldok=vdc_model_project_correlated_delta(flags&1?NULL:&d,hz,ticks,
            flags&2?NULL:&oldlo,flags&4?NULL:(flags&8?&oldlo:&oldhi));
        const bool valid0=vdc_domain_dco_local_to_output_ns(&d,first,&y0);
        const bool valid1=vdc_domain_dco_local_to_output_ns(&d,second,&y1);
        assert(!memcmp(&d,&saved,sizeof(d)));
        printf("%u %"PRIu64" %"PRIu64" %u %"PRIu64" %"PRIu64" %u %"PRIu64" %u %"PRIu64"\n",
            (unsigned)ok,lo,hi,(unsigned)oldok,oldlo,oldhi,(unsigned)valid0,y0,(unsigned)valid1,y1);
    }
    return 0;
}
'''
