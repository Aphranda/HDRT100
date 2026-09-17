"""Exact affine edge cursor versus Fraction oracle and production scalar plans."""
from dataclasses import dataclass, replace
from fractions import Fraction
import random
import subprocess

import pytest

from test_vdc_command_owner import compile_executable
from test_vdc_local_phase_domain import sources
import test_vdc_follower_rate_boundary as rate_tests

BILLION = 10**9
U64 = (1 << 64) - 1
I32_MIN, I32_MAX = -(1 << 31), (1 << 31) - 1


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
    high: int = 1000
    count: int = 16


def forward(case, local):
    delta = local - case.base_local
    # int(Fraction) truncates toward zero, independently of the C split.
    return case.base_vdc + delta + int(Fraction(delta * case.rate, BILLION)) + case.phase


def supported(case):
    return (case.period > 0 and case.rate > -BILLION and
            Fraction(case.period * BILLION, BILLION + case.rate) >= case.high + 1)


def scalar_oracle(case, first, minimum):
    lower = max(case.base_local, minimum - case.delay, 0)
    if lower > U64:
        return None
    value = forward(case, lower)
    if not 0 <= value < U64:
        return None
    ordinal = max(first, 0 if value < case.anchor else (value - case.anchor) // case.period + 1)
    target = case.anchor + ordinal * case.period
    if target > U64:
        return None
    # Search the monotone Fraction-based forward model, without using either
    # sign-specific inverse formula or the cursor's remainder recurrence.
    lo, hi = case.base_local, U64
    while lo < hi:
        middle = (lo + hi) // 2
        if forward(case, middle) >= target:
            hi = middle
        else:
            lo = middle + 1
    physical = lo + case.delay
    if not target <= forward(case, lo) <= U64 or not minimum < physical <= U64:
        return None
    assert lo == case.base_local or forward(case, lo - 1) < target
    return ordinal, target, lo, physical


def oracle(case):
    if not supported(case):
        return False, []
    plans = []
    first, minimum = case.first, case.minimum
    for _ in range(case.count):
        plan = scalar_oracle(case, first, minimum)
        if plan is None:
            break
        plans.append(plan)
        if plan[0] == U64 or plan[3] > U64 - case.high:
            break
        first, minimum = plan[0] + 1, plan[3] + case.high
    return True, plans


HARNESS = r'''
#include <inttypes.h>
#include "vdc_output_edge_cursor.h"

static void same_plan(const vdc_output_edge_plan_t *a, const vdc_output_edge_plan_t *b) {
    assert(a->ordinal==b->ordinal && a->target_vdc_ns==b->target_vdc_ns &&
        a->model_local_ns==b->model_local_ns && a->physical_local_ns==b->physical_local_ns);
}

static void invalid_arguments(void) {
    vdc_domain_context_t context;
    fixture(&context);
    vdc_dco_control_t model={.valid=1,.nominal_period_ns=1};
    vdc_output_edge_cursor_t cursor;
    vdc_output_edge_plan_t plan;
    memset(&cursor,0xa5,sizeof(cursor));
    memset(&plan,0x5a,sizeof(plan));
    const vdc_output_edge_cursor_t old_cursor=cursor;
    const vdc_output_edge_plan_t old_plan=plan;
    assert(!vdc_output_edge_cursor_supported(NULL,100,1));
    assert(!vdc_output_edge_cursor_init(NULL,0,100,0,0,0,1,&cursor,&plan));
    assert(!vdc_output_edge_cursor_init(&model,0,100,0,0,0,1,NULL,&plan));
    assert(!vdc_output_edge_cursor_init(&model,0,100,0,0,0,1,&cursor,NULL));
    model.valid=0;
    assert(!vdc_output_edge_cursor_init(&model,0,100,0,0,0,1,&cursor,&plan));
    model.valid=1; model.nominal_period_ns=0;
    assert(!vdc_output_edge_cursor_init(&model,0,100,0,0,0,1,&cursor,&plan));
    model.nominal_period_ns=1; model.lock_state=VDC_DOMAIN_LOCK_FAULT+1;
    assert(!vdc_output_edge_cursor_init(&model,0,100,0,0,0,1,&cursor,&plan));
    assert(memcmp(&cursor,&old_cursor,sizeof(cursor))==0);
    assert(memcmp(&plan,&old_plan,sizeof(plan))==0);
    memset(&cursor,0,sizeof(cursor));
    const vdc_output_edge_cursor_t inactive=cursor;
    assert(!vdc_output_edge_cursor_next(&cursor,&plan));
    assert(!vdc_output_edge_cursor_next(NULL,&plan));
    assert(!vdc_output_edge_cursor_next(&cursor,NULL));
    assert(memcmp(&cursor,&inactive,sizeof(cursor))==0);
    assert(memcmp(&plan,&old_plan,sizeof(plan))==0);
}

int main(void) {
    invalid_arguments();
    uint64_t bl,bv,anchor,first,minimum;
    int32_t rate,phase,delay;
    uint32_t period,high,count;
    while (scanf("%" SCNu64 " %" SCNu64 " %" SCNd32 " %" SCNd32
                 " %" SCNu64 " %" SCNu32 " %" SCNu64 " %" SCNu64
                 " %" SCNd32 " %" SCNu32 " %" SCNu32,
                 &bl,&bv,&rate,&phase,&anchor,&period,&first,&minimum,
                 &delay,&high,&count)==11) {
        assert(count>0 && count<=64);
        vdc_dco_control_t model={.valid=1,.base_local_tick64=bl,
            .base_vdc_time64_ns=bv,.period_adjust_ppb=rate,
            .phase_offset_ns=phase,.nominal_period_ns=1000000};
        const vdc_dco_control_t original=model;
        vdc_output_edge_cursor_t cursor;
        vdc_output_edge_plan_t plan;
        memset(&cursor,0xa5,sizeof(cursor));
        memset(&plan,0x5a,sizeof(plan));
        const vdc_output_edge_cursor_t old_cursor=cursor;
        const vdc_output_edge_plan_t old_plan=plan;
        const bool eligible=vdc_output_edge_cursor_supported(&model,period,high);
        const bool initialized=vdc_output_edge_cursor_init(&model,anchor,period,first,
            minimum,delay,high,&cursor,&plan);
        printf("%u",eligible ? 1u : 0u);
        if (!initialized) {
            assert(memcmp(&cursor,&old_cursor,sizeof(cursor))==0);
            assert(memcmp(&plan,&old_plan,sizeof(plan))==0);
            if (eligible) {
                vdc_output_edge_plan_t scalar;
                assert(!vdc_output_edge_plan(&model,anchor,period,first,minimum,delay,&scalar));
            }
            puts("");
            continue;
        }
        assert(eligible);
        vdc_output_edge_plan_t scalar;
        assert(vdc_output_edge_plan(&model,anchor,period,first,minimum,delay,&scalar));
        same_plan(&plan,&scalar);
        /* A cursor owns all its arithmetic state; mutating the seed object
         * must not mutate that state. The actual client still invalidates it. */
        memset(&model,0,sizeof(model));
        for (uint32_t i=0;i<count;++i) {
            printf(" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64,
                plan.ordinal,plan.target_vdc_ns,plan.model_local_ns,plan.physical_local_ns);
            if (i+1u==count) break;
            const vdc_output_edge_cursor_t before_cursor=cursor;
            const vdc_output_edge_plan_t before_plan=plan;
            const bool next=vdc_output_edge_cursor_next(&cursor,&plan);
            const bool expected=before_plan.ordinal!=UINT64_MAX &&
                before_plan.physical_local_ns<=UINT64_MAX-high &&
                vdc_output_edge_plan(&original,anchor,period,before_plan.ordinal+1u,
                    before_plan.physical_local_ns+high,delay,&scalar);
            assert(next==expected);
            if (!next) {
                assert(memcmp(&cursor,&before_cursor,sizeof(cursor))==0);
                assert(memcmp(&plan,&before_plan,sizeof(plan))==0);
                /* Repeating a rejected step is equally nondestructive. */
                assert(!vdc_output_edge_cursor_next(&cursor,&plan));
                assert(memcmp(&cursor,&before_cursor,sizeof(cursor))==0);
                assert(memcmp(&plan,&before_plan,sizeof(plan))==0);
                break;
            }
            same_plan(&plan,&scalar);
            assert(plan.ordinal==before_plan.ordinal+1u);
        }
        puts("");
    }
    return 0;
}
'''


@pytest.fixture(scope="module")
def cursor_executable(tmp_path_factory):
    prelude = rate_tests.HARNESS.split("static vdc_dpll_follower_rate_delta_t command_for")[0]
    return compile_executable(tmp_path_factory.mktemp("edge-cursor"), "edge_cursor",
                              prelude + HARNESS, sources())


def run_cases(executable, cases):
    payload = "\n".join(" ".join(map(str, vars(case).values())) for case in cases) + "\n"
    result = subprocess.run([str(executable)], input=payload, capture_output=True,
                            text=True, timeout=45)
    assert result.returncode == 0, result.stdout[-3000:] + result.stderr
    lines = result.stdout.splitlines()
    assert len(lines) == len(cases)
    rows = []
    for case, line in zip(cases, lines):
        values = tuple(map(int, line.split()))
        assert len(values) % 4 == 1
        actual = (bool(values[0]), [values[i:i+4] for i in range(1, len(values), 4)])
        expected = oracle(case)
        assert actual == expected, (case, actual, expected)
        rows.append(actual)
    return rows


def test_fraction_oracle_and_scalar_match_both_rounding_rules(cursor_executable):
    cases = [Case(rate=rate, period=period, high=0, count=64)
             for rate in (-999999999, -999999998, -500000001, -1, 0, 1, 999999999, I32_MAX)
             for period in (4, 999999, 1000000, 100000001, (1 << 32) - 1)]
    rows = run_cases(cursor_executable, cases)
    assert all(eligible and plans for eligible, plans in rows)
    assert sum(len(plans) == 64 for _, plans in rows) >= 30


def test_eligibility_is_conservative_and_does_not_overflow(cursor_executable):
    cases = [Case(period=0), Case(rate=-BILLION), Case(rate=I32_MIN),
             Case(period=1, high=0, rate=1), Case(period=100, high=100),
             Case(period=100, high=99), Case(period=4, high=0, rate=I32_MAX),
             Case(period=(1 << 32)-1, high=(1 << 32)-1, rate=I32_MAX),
             Case(period=(1 << 32)-1, high=(1 << 32)-1, rate=-999999999)]
    rows = run_cases(cursor_executable, cases)
    assert [row[0] for row in rows] == [False]*5 + [True, True, False, True]
    # Eligibility at and just below the exact minimum-spacing boundary.
    cases = []
    for rate in (-999999999, -1, 0, 1, I32_MAX):
        for high in (0, 1, 1000, 500000):
            boundary = ((high + 1)*(BILLION+rate) + BILLION-1)//BILLION
            for period in (max(1, boundary-1), boundary, boundary+1):
                cases.append(Case(rate=rate, high=high, period=period))
    run_cases(cursor_executable, cases)


def test_signed_delay_base_clamp_and_negative_intercept(cursor_executable):
    cases = [Case(base_local=1000, base_vdc=base, rate=rate, phase=phase,
                  minimum=minimum, delay=delay, period=10000000)
             for base, phase in ((0, I32_MIN), (0, -1), (100, -100), (100, I32_MAX))
             for rate in (-999999999, -1, 0, 1, I32_MAX)
             for delay in (I32_MIN, -1, 0, 1, I32_MAX)
             for minimum in (0, 10000000000)]
    run_cases(cursor_executable, cases)


def test_uint64_endpoints_need_65_bits_and_failures_are_atomic(cursor_executable):
    cases = [Case(period=1, high=0, minimum=U64-2),
             Case(period=1, high=0, first=U64-1, minimum=0),
             Case(period=1, high=0, minimum=U64-2, delay=1),
             Case(period=1, high=1, minimum=U64-2),
             Case(period=2, high=1, minimum=U64-3),
             Case(base_local=U64-2, period=1, high=0, minimum=U64-2),
             Case(base_vdc=U64, minimum=0), Case(minimum=U64, delay=-1),
             Case(period=2, first=U64, high=0),
             Case(anchor=U64, first=1),
             Case(base_local=U64, minimum=0),
             Case(base_vdc=U64, phase=1, minimum=0)]
    # Positive rate can invert a target whose distance from a negative
    # intercept exceeds UINT64_MAX. Include final forward overshoot failures.
    cases += [Case(rate=rate, phase=phase, anchor=U64-gap, period=period,
                   high=0, minimum=10000000000, count=64)
              for rate in (-1, 0, 1, I32_MAX)
              for phase in (I32_MIN, -1, 0, I32_MAX)
              for period in (4, 5, 1000000)
              for gap in (0, 1, 2, 3, 63, 1000001)]
    rows = run_cases(cursor_executable, cases)
    assert any(plans and len(plans) < case.count for case, (_, plans) in zip(cases, rows))
    assert any(plans and case.anchor - (case.base_vdc + case.phase) > U64
               for case, (_, plans) in zip(cases, rows))


def test_random_full_width_models_and_finite_sequences(cursor_executable):
    rng = random.Random(0xC0750A)
    cases = []
    rates = [-999999999, -999999998, -1, 0, 1, I32_MAX]
    for i in range(1000):
        rate = rng.choice(rates) if i % 2 else rng.randrange(-999999999, I32_MAX+1)
        period = rng.randrange(1, 1 << 32)
        spacing = period * BILLION // (BILLION + rate)
        high = rng.randrange(min(spacing, (1 << 32)-1)+1)
        base_local = rng.randrange(1 << (64 if i % 3 == 0 else 30))
        base_vdc = rng.randrange(1 << (64 if i % 3 == 1 else 30))
        minimum = rng.randrange(base_local, U64+1) if i % 3 == 2 else base_local+rng.randrange(1 << 30)
        minimum = min(minimum, U64)
        cases.append(Case(base_local=base_local, base_vdc=base_vdc, rate=rate,
                          phase=rng.randrange(I32_MIN, I32_MAX+1), period=period,
                          anchor=rng.randrange(1 << 64) if i % 7 == 0 else 0,
                          first=rng.randrange(1 << 32) if i % 5 == 0 else 0,
                          minimum=minimum, delay=rng.randrange(I32_MIN, I32_MAX+1), high=high))
    rows = run_cases(cursor_executable, cases)
    assert sum(len(plans) == 16 for _, plans in rows) > 200


def test_reseed_uses_new_model_and_first_unqueued(cursor_executable):
    seed = Case(rate=-12345, delay=-123, minimum=80000000)
    _, plans = run_cases(cursor_executable, [seed])[0]
    last = plans[-1]
    cases = [replace(seed, base_vdc=base, rate=rate, first=last[0]+1,
                     minimum=last[3]+seed.high)
             for base in (0, 100000000, 1100000000)
             for rate in (-999999999, -1000, 0, 1000, I32_MAX)]
    rows = run_cases(cursor_executable, cases)
    assert all(eligible and plans and plans[0][0] > last[0] and plans[0][3] > last[3]
               for eligible, plans in rows)
