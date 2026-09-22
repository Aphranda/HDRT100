"""Exact-q VDC planning against Fraction arithmetic and a binary-search inverse."""
from dataclasses import dataclass, replace
from fractions import Fraction
from pathlib import Path
import random
import subprocess

import pytest

from test_vdc_command_owner import compile_executable
from test_vdc_local_phase_domain import sources

U64 = (1 << 64) - 1
ROOT = Path(__file__).resolve().parents[2]
OK, ARGUMENT, RESERVED, OVERFLOW, MAPPING, LATE, HORIZON, RESOLUTION = range(8)


@dataclass(frozen=True)
class Case:
    anchor: int = 1000000
    epoch: int = 1
    generation: int = 1
    period: int = 1000000
    subdivisions: int = 1000
    q: int = 10
    base_local: int = 0
    base_vdc: int = 0
    rate: int = 0
    phase: int = 0
    hz: int = 250000000
    now: int = 0
    lead: int = 1
    horizon: int = U64


def ceil(value):
    return -(-value.numerator // value.denominator)


def forward(c, local):
    delta = local - c.base_local
    correction = delta * abs(c.rate) // 1000000000
    return c.base_vdc + delta + (correction if c.rate >= 0 else -correction) + c.phase


def inverse(c, target):
    if c.rate <= -1000000000:
        return None
    lo, hi = c.base_local, U64
    while lo < hi:
        mid = (lo + hi) // 2
        if forward(c, mid) >= target:
            hi = mid
        else:
            lo = mid + 1
    return lo if target <= forward(c, lo) <= U64 else None


def oracle(c):
    if (not c.epoch or not c.generation or c.subdivisions < 2 or
            c.period < c.subdivisions or not 0 < c.hz <= 500000000 or
            not 0 < c.lead <= c.horizon):
        return ARGUMENT, None
    if c.q % c.subdivisions == c.subdivisions - 1:
        return RESERVED, None
    target = c.anchor + ceil(Fraction(c.q * c.period, c.subdivisions))
    if target > U64:
        return OVERFLOW, None
    if c.q == U64:
        return OVERFLOW, None
    origin = forward(c, c.base_local)
    if 0 <= origin <= U64 and target < origin:
        return MAPPING, None
    local = inverse(c, target)
    if local is None:
        return MAPPING, None
    deadline = ceil(Fraction(local * c.hz, 1000000000))
    if deadline > U64:
        return MAPPING, None
    if deadline - c.now < c.lead:
        return LATE, None
    if deadline - c.now > c.horizon:
        return HORIZON, None
    physical = deadline * 1000000000 // c.hz
    if physical > U64:
        return MAPPING, None
    projected = forward(c, physical)
    if not target <= projected <= U64:
        return MAPPING, None
    following = c.anchor + ceil(Fraction((c.q + 1) * c.period, c.subdivisions))
    if following > U64:
        return OVERFLOW, None
    next_local = inverse(c, following)
    if next_local is None:
        return MAPPING, None
    if ceil(Fraction(next_local * c.hz, 1000000000)) <= deadline:
        return RESOLUTION, None
    return OK, (c.q, target, local, deadline, projected)


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "trigger_time_grid.h"
static void gates(void) {
    trigger_time_grid_t grid={.epoch=1,.generation=1,.period_ns=1000000,.subdivisions=1000};
    vdc_dco_control_t dco={.valid=1,.nominal_period_ns=1000000};
    trigger_time_grid_plan_t out;memset(&out,0xa5,sizeof(out));
    const trigger_time_grid_plan_t saved=out;
    uint64_t target=123;
    assert(!trigger_time_grid_valid(NULL));
    assert(trigger_time_grid_target(NULL,0,&target)==TRIGGER_TIME_GRID_ARGUMENT && target==123);
    assert(trigger_time_grid_target(&grid,0,NULL)==TRIGGER_TIME_GRID_ARGUMENT);
    assert(trigger_time_grid_prepare(&grid,10,NULL,250000000,0,1,UINT64_MAX,&out)==TRIGGER_TIME_GRID_ARGUMENT);
    assert(trigger_time_grid_prepare(&grid,10,&dco,250000000,0,1,UINT64_MAX,NULL)==TRIGGER_TIME_GRID_ARGUMENT);
    dco.valid=0;
    assert(trigger_time_grid_prepare(&grid,10,&dco,250000000,0,1,UINT64_MAX,&out)==TRIGGER_TIME_GRID_MAPPING);
    dco.valid=1;dco.nominal_period_ns=0;
    assert(trigger_time_grid_prepare(&grid,10,&dco,250000000,0,1,UINT64_MAX,&out)==TRIGGER_TIME_GRID_MAPPING);
    dco.nominal_period_ns=1000000;dco.lock_state=VDC_DOMAIN_LOCK_FAULT+1u;
    assert(trigger_time_grid_prepare(&grid,10,&dco,250000000,0,1,UINT64_MAX,&out)==TRIGGER_TIME_GRID_MAPPING);
    assert(memcmp(&out,&saved,sizeof(out))==0);
}
int main(void) {
    gates();
    trigger_time_grid_t grid;
    vdc_dco_control_t dco={.valid=1,.nominal_period_ns=1000000};
    uint64_t q,now,lead,horizon;
    uint32_t hz;
    while(scanf("%"SCNu64" %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu32
                " %"SCNu64" %"SCNu64" %"SCNu64" %"SCNd32" %"SCNd32
                " %"SCNu32" %"SCNu64" %"SCNu64" %"SCNu64,
        &grid.anchor_vdc_ns,&grid.epoch,&grid.generation,&grid.period_ns,
        &grid.subdivisions,&q,&dco.base_local_tick64,&dco.base_vdc_time64_ns,
        &dco.period_adjust_ppb,&dco.phase_offset_ns,&hz,&now,&lead,&horizon)==14) {
        trigger_time_grid_plan_t plan;memset(&plan,0xa5,sizeof(plan));
        const trigger_time_grid_plan_t saved=plan;
        const trigger_time_grid_t grid_before=grid;
        const vdc_dco_control_t model_before=dco;
        const trigger_time_grid_result_t result=trigger_time_grid_prepare(
            &grid,q,&dco,hz,now,lead,horizon,&plan);
        assert(memcmp(&grid,&grid_before,sizeof(grid))==0);
        assert(memcmp(&dco,&model_before,sizeof(dco))==0);
        if(result!=TRIGGER_TIME_GRID_OK)assert(memcmp(&plan,&saved,sizeof(plan))==0);
        printf("%u",(unsigned)result);
        if(result==TRIGGER_TIME_GRID_OK)printf(" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64,
            plan.ordinal,plan.target_vdc_ns,plan.model_local_ns,plan.deadline_ticks,plan.projected_vdc_ns);
        uint64_t target=123;
        const trigger_time_grid_result_t target_result=trigger_time_grid_target(&grid,q,&target);
        if(target_result!=TRIGGER_TIME_GRID_OK)assert(target==123);
        printf(" | %u %"PRIu64"\n",(unsigned)target_result,target);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def executable(tmp_path_factory):
    header = ROOT / 'components/sync_trigger/inc/trigger_time_grid.h'
    return compile_executable(tmp_path_factory.mktemp('time-grid'), 'time_grid',
                              HARNESS.replace('"trigger_time_grid.h"', f'"{header.as_posix()}"'), sources())


def run_cases(executable, cases):
    result = subprocess.run([str(executable)], input='\n'.join(
        ' '.join(map(str, vars(c).values())) for c in cases) + '\n',
        capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    lines = result.stdout.splitlines()
    assert len(lines) == len(cases)
    answers = []
    for c, line in zip(cases, lines):
        plan_text, target_text = line.split(' | ')
        actual = tuple(map(int, plan_text.split()))
        status, plan = oracle(c)
        assert actual == (status, *(plan or ())), (c, actual, status, plan)
        target_status, target = map(int, target_text.split())
        if not c.epoch or not c.generation or c.subdivisions < 2 or c.period < c.subdivisions:
            want = ARGUMENT
        elif c.q % c.subdivisions == c.subdivisions - 1:
            want = RESERVED
        elif c.anchor + ceil(Fraction(c.q*c.period, c.subdivisions)) > U64:
            want = OVERFLOW
        else:
            want = OK
        assert target_status == want
        assert target == (c.anchor + ceil(Fraction(c.q*c.period, c.subdivisions)) if want == OK else 123)
        answers.append(actual)
    return answers


def test_boundaries_and_rejections(executable):
    c = Case()
    cases = [c, replace(c,q=0), replace(c,q=998), replace(c,q=999), replace(c,q=1000),
             replace(c,q=1999), replace(c,epoch=0), replace(c,generation=0),
             replace(c,subdivisions=1), replace(c,period=999), replace(c,period=0),
             replace(c,hz=0), replace(c,hz=500000001), replace(c,lead=0),
             replace(c,horizon=0), replace(c,lead=10,horizon=9),
             replace(c,now=252500), replace(c,now=252501), replace(c,now=U64,lead=2),
             replace(c,now=252499), replace(c,horizon=252499), replace(c,horizon=252500),
             replace(c,rate=-1000000000), replace(c,base_local=U64),
             replace(c,anchor=U64), replace(c,q=U64),
             Case(anchor=0,period=3,subdivisions=3,q=U64,hz=500000000),
             Case(anchor=U64-10000,period=10000,subdivisions=100,q=99),
             Case(anchor=U64-4,period=8,subdivisions=2,q=0,hz=500000000),
             replace(c,period=1000,subdivisions=1000,q=0),
             replace(c,period=1000,subdivisions=1000,q=2),
             replace(c,period=1001,subdivisions=1000,q=997),
             replace(c,base_vdc=100000000,q=0)]
    results = run_cases(executable,cases)
    assert {row[0] for row in results} == set(range(8))


def test_fractional_grid_and_signed_rate_matrix(executable):
    cases = [Case(period=1000003,subdivisions=997,q=q,rate=rate,hz=hz,phase=phase)
             for q in [0,1,995,996,997,998,1000000]
             for rate in [-999999999,-100000000,-1001,-1,0,1,1563,100000000,2147483647]
             for hz in [150000000,250000000,333333333]
             for phase in [-200,201]]
    run_cases(executable,cases)


def test_random_large_integer_grid(executable):
    rng = random.Random(817)
    cases=[]
    for _ in range(450):
        subdivisions=rng.randrange(2,2**32)
        cases.append(Case(anchor=rng.randrange(U64+1),
            period=rng.randrange(subdivisions,2**32),subdivisions=subdivisions,
            q=rng.choice([rng.randrange(2**32),rng.randrange(U64+1)]),
            base_local=rng.randrange(2**50),base_vdc=rng.randrange(2**50),
            rate=rng.randrange(-999999999,2147483648),phase=rng.randrange(-2**31,2**31),
            hz=rng.choice([150000000,250000000,333333333])))
    run_cases(executable,cases)


def test_four_local_models_share_target_not_deadline(executable):
    cases=[Case(q=1234,rate=rate,base_local=local,base_vdc=vdc,phase=phase)
           for rate,local,vdc,phase in [(0,0,0,0),(1563,123456,100000,-40),
                                     (-1001,80000,90000,30),(2501,40000,39000,90)]]
    results=run_cases(executable,cases)
    assert all(row[0]==OK for row in results)
    assert len({row[2] for row in results})==1
    assert len({row[4] for row in results})==4


def test_reserved_neighbor_still_limits_resolution(executable):
    # q=2 is allowed, but q=3 is reserved and shares its hardware tick.
    results=run_cases(executable,[Case(anchor=0,period=4,subdivisions=4,q=2),
                                  Case(anchor=0,period=16,subdivisions=4,q=2)])
    assert results[0][0]==RESOLUTION and results[1][0]==OK


def test_target_before_model_origin_is_not_clamped(executable):
    # q0 is before the origin but q1 is after it: comparing consecutive
    # deadlines alone would accept the incorrectly clamped q0 deadline.
    c = Case(anchor=1000,period=4000,subdivisions=4,q=0,
             base_local=8000,base_vdc=1500)
    results = run_cases(executable,[c,replace(c,anchor=1500),
        replace(c,base_vdc=0,phase=-2000),
        replace(c,base_vdc=U64,phase=100)])
    assert [row[0] for row in results] == [MAPPING,OK,OK,MAPPING]
