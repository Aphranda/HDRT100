"""Common VDC grid and independent physical delay, against integer oracles."""
from dataclasses import dataclass, replace
from pathlib import Path
import random
import subprocess

import pytest

from test_vdc_command_owner import compile_executable
from test_vdc_local_phase_domain import sources
import test_vdc_follower_rate_boundary as rate_tests

ROOT = Path(__file__).resolve().parents[2]
U64 = (1 << 64) - 1


@dataclass(frozen=True)
class Case:
    base_local: int = 0
    base_vdc: int = 0
    rate: int = 0
    phase: int = 0
    anchor: int = 0
    period: int = 1000000
    first: int = 0
    minimum: int = 10000000
    delay: int = 0


def forward(c, local):
    delta = local - c.base_local
    rate = delta * abs(c.rate) // 1000000000
    return c.base_vdc + delta + (rate if c.rate >= 0 else -rate) + c.phase


def oracle(c):
    if not c.period or c.rate <= -1000000000:
        return None
    lower = max(c.base_local, c.minimum - c.delay, 0)
    if lower > U64 or not 0 <= forward(c, lower) < U64:
        return None
    # Binary search the grid; deliberately independent of the C quotient.
    lo, hi = c.first, (U64 - c.anchor) // c.period
    while lo < hi:
        mid = (lo + hi) // 2
        if c.anchor + mid * c.period > forward(c, lower):
            hi = mid
        else:
            lo = mid + 1
    target = c.anchor + lo * c.period
    if lo < c.first or not forward(c, lower) < target <= U64:
        return None
    start, end = c.base_local, U64
    while start < end:
        mid = (start + end) // 2
        if forward(c, mid) >= target:
            end = mid
        else:
            start = mid + 1
    mapped = forward(c, start)
    physical = start + c.delay
    if not target <= mapped <= U64 or not c.minimum < physical <= U64:
        return None
    return lo, target, start, physical


HARNESS = r'''
#include <inttypes.h>
#include "vdc_output_edge_plan.h"
int main(void) {
    vdc_domain_context_t ctx;
    fixture(&ctx);
    vdc_output_edge_plan_t probe;
    memset(&probe,0xa5,sizeof(probe));
    const vdc_output_edge_plan_t unchanged=probe;
    vdc_dco_control_t invalid=ctx.dco;
    assert(!vdc_output_edge_plan(NULL,0,1,0,0,0,&probe));
    assert(!vdc_output_edge_plan(&invalid,0,1,0,0,0,NULL));
    invalid.valid=0;
    assert(!vdc_output_edge_plan(&invalid,0,1,0,0,0,&probe));
    invalid.valid=1; invalid.nominal_period_ns=0;
    assert(!vdc_output_edge_plan(&invalid,0,1,0,0,0,&probe));
    invalid.nominal_period_ns=1; invalid.lock_state=VDC_DOMAIN_LOCK_FAULT+1;
    assert(!vdc_output_edge_plan(&invalid,0,1,0,0,0,&probe));
    assert(memcmp(&probe,&unchanged,sizeof(probe))==0);
    uint64_t bl,bv,anchor,first,minimum;
    int32_t rate,phase,delay;
    uint32_t period;
    while (scanf("%" SCNu64 " %" SCNu64 " %" SCNd32 " %" SCNd32
                 " %" SCNu64 " %" SCNu32 " %" SCNu64 " %" SCNu64
                 " %" SCNd32,&bl,&bv,&rate,&phase,&anchor,&period,&first,
                 &minimum,&delay)==9) {
        vdc_dco_control_t dco={.valid=1,.base_local_tick64=bl,
            .base_vdc_time64_ns=bv,.period_adjust_ppb=rate,
            .phase_offset_ns=phase,.nominal_period_ns=1000000};
        vdc_dco_control_t original=dco;
        vdc_output_edge_plan_t result;
        memset(&result,0xa5,sizeof(result));
        vdc_output_edge_plan_t sentinel=result;
        bool ok=vdc_output_edge_plan(&dco,anchor,period,first,minimum,delay,&result);
        assert(memcmp(&dco,&original,sizeof(dco))==0);
        if(!ok) { assert(memcmp(&result,&sentinel,sizeof(result))==0); puts("0"); }
        else printf("1 %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
                    result.ordinal,result.target_vdc_ns,result.model_local_ns,
                    result.physical_local_ns);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def planner(tmp_path_factory):
    prelude = rate_tests.HARNESS.split('static vdc_dpll_follower_rate_delta_t command_for')[0]
    return compile_executable(tmp_path_factory.mktemp('edge-plan'), 'edge_plan',
                              prelude + HARNESS, sources())


def run_cases(exe, cases):
    data = '\n'.join(' '.join(map(str, vars(c).values())) for c in cases) + '\n'
    result = subprocess.run([str(exe)], input=data, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    rows = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    for c, row in zip(cases, rows):
        expected = oracle(c)
        assert row == ((0,) if expected is None else (1, *expected)), (c, row, expected)
    return rows


def test_signed_delay_is_on_local_axis_once_and_grid_is_unchanged(planner):
    base = Case(first=1000, rate=2147483647, phase=-123)
    rows = run_cases(planner, [replace(base, delay=d) for d in (-125,0,125)])
    assert rows[0][1:4] == rows[1][1:4] == rows[2][1:4]
    assert rows[2][4] - rows[1][4] == rows[1][4] - rows[0][4] == 125


def test_model_updates_respect_next_ordinal_and_physical_floor(planner):
    first = Case(base_vdc=100000000, minimum=8000000)
    row = run_cases(planner, [first])[0]
    cases = [replace(first, base_vdc=b, rate=r, first=row[1]+1,
                     minimum=row[4]+1000) for b in (0,100000000,1100000000)
             for r in (-999999999,-1000,0,1000,2147483647)]
    rows = run_cases(planner, cases)
    for result in rows:
        assert result[0] == 1 and result[1] > row[1] and result[4] > row[4]+1000


def test_exact_grid_plateau_first_ordinal_and_uint64_endpoint(planner):
    cases = [Case(period=1,minimum=12),
             Case(period=1,minimum=1,rate=-999999999),
             Case(first=5000,minimum=12),
             Case(period=1,minimum=U64-1),
             Case(anchor=U64,minimum=0),
             Case(period=1,minimum=U64-1,delay=1)]
    rows = run_cases(planner,cases)
    assert rows[0] == (1,13,13,13,13)
    assert rows[1] == (1,2,2,1000000001,1000000001)
    assert rows[2][1] == 5000
    assert rows[3][4] == rows[4][4] == rows[5][4] == U64


def test_boundaries_and_failures_preserve_model_and_output(planner):
    cases = [Case(period=0), Case(rate=-1000000000), Case(rate=-(1<<31)),
             Case(minimum=U64), Case(minimum=U64,delay=-1),
             Case(anchor=U64,first=1), Case(first=U64,period=2),
             Case(base_local=U64,base_vdc=0,minimum=U64-1),
             Case(base_vdc=U64,minimum=0), Case(phase=-(1<<31),minimum=0)]
    for rate in (-999999999,-1,0,1,2147483647):
        for delay in (-(1<<31),-1,0,1,(1<<31)-1):
            for minimum in (0,1,999999,U64-1,U64):
                cases.append(Case(rate=rate,delay=delay,minimum=minimum))
    run_cases(planner,cases)


def test_random_full_width_integer_oracle(planner):
    rng=random.Random(0x100d311)
    cases=[]
    for i in range(1800):
        base = rng.randrange(1<<64) if i%3==0 else rng.randrange(1<<40)
        cases.append(Case(base_local=base,base_vdc=rng.randrange(1<<64),
            rate=rng.choice([-999999999,-1000,0,1000,2147483647]),
            phase=rng.randrange(-(1<<31),1<<31),anchor=rng.randrange(1<<64),
            period=rng.randrange(1,1<<32),first=rng.randrange(1<<32),
            minimum=rng.randrange(base,1<<64),delay=rng.randrange(-(1<<31),1<<31)))
    run_cases(planner,cases)
