"""Production exact C inverse vs independent unbounded-integer binary oracle."""
from dataclasses import dataclass
from pathlib import Path
import itertools
import random
import subprocess

import pytest

HERE = Path(__file__).resolve().parent
from test_vdc_command_owner import compile_executable
from test_vdc_local_phase_domain import sources

U64 = (1 << 64) - 1
I32_MIN, I32_MAX = -(1 << 31), (1 << 31) - 1
B = 1000000000


@dataclass(frozen=True)
class Case:
    base_local: int
    base_output: int
    rate: int
    phase: int
    target: int

    def line(self):
        return ' '.join(map(str, vars(self).values()))


def raw_forward(c, local):
    delta = local - c.base_local
    correction = delta * abs(c.rate) // B
    return c.base_output + delta + (correction if c.rate >= 0 else -correction) + c.phase


def oracle(c):
    if c.rate <= -B:
        return None
    lo, hi = c.base_local, U64
    # Independent monotone lower_bound; deliberately does not use the C formula.
    while lo < hi:
        mid = lo + (hi - lo) // 2
        if raw_forward(c, mid) >= c.target:
            hi = mid
        else:
            lo = mid + 1
    output = raw_forward(c, lo)
    if not c.target <= output <= U64:
        return None
    previous = None if lo == c.base_local else raw_forward(c, lo - 1)
    return lo, output, previous


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "vdc_domain.h"
static const uint64_t sentinel = UINT64_C(0x12345678abcdef01);
static vdc_dco_control_t make_dco(void)
{
    vdc_dco_control_t d;
    memset(&d, 0, sizeof(d));
    d.valid = 1u; d.nominal_period_ns = 1000000u;
    return d;
}
static void gates(void)
{
    vdc_dco_control_t d = make_dco();
    uint64_t out = sentinel;
    assert(!vdc_domain_dco_output_to_local_ns(NULL, 0, &out));
    assert(!vdc_domain_dco_output_to_local_ns(&d, 0, NULL));
    d.valid = 0u;
    assert(!vdc_domain_dco_output_to_local_ns(&d, 0, &out));
    d = make_dco(); d.nominal_period_ns = 0;
    assert(!vdc_domain_dco_output_to_local_ns(&d, 0, &out));
    d = make_dco(); d.lock_state = VDC_DOMAIN_LOCK_FAULT + 1u;
    assert(!vdc_domain_dco_output_to_local_ns(&d, 0, &out));
    d = make_dco(); d.period_adjust_ppb = -1000000000;
    assert(!vdc_domain_dco_output_to_local_ns(&d, 0, &out));
    d.period_adjust_ppb = INT32_MIN;
    assert(!vdc_domain_dco_output_to_local_ns(&d, 0, &out));
    assert(out == sentinel);
    /* Inverse exactly mirrors forward's valid!=0 convention. */
    d = make_dco(); d.valid = 7;
    assert(vdc_domain_dco_output_to_local_ns(&d, 42, &out) && out == 42);
}
static void cases(void)
{
    vdc_dco_control_t d = make_dco();
    uint64_t target;
    while (scanf("%" SCNu64 " %" SCNu64 " %" SCNd32 " %" SCNd32 " %" SCNu64,
        &d.base_local_tick64, &d.base_vdc_time64_ns, &d.period_adjust_ppb,
        &d.phase_offset_ns, &target) == 5) {
        const vdc_dco_control_t before = d;
        uint64_t local = sentinel, output = sentinel, previous = sentinel;
        const bool ok = vdc_domain_dco_output_to_local_ns(&d, target, &local);
        assert(memcmp(&d, &before, sizeof(d)) == 0);
        bool prev_valid = false;
        if (ok) {
            assert(local >= d.base_local_tick64);
            assert(vdc_domain_dco_local_to_output_ns(&d, local, &output));
            assert(output >= target);
            if (local > d.base_local_tick64) {
                prev_valid = vdc_domain_dco_local_to_output_ns(&d, local - 1u, &previous);
                assert(!prev_valid || previous < target);
            }
        } else assert(local == sentinel);
        printf("%u %" PRIu64 " %" PRIu64 " %u %" PRIu64 "\n",
            ok ? 1u : 0u, ok ? local : 0u, ok ? output : 0u,
            prev_valid ? 1u : 0u, prev_valid ? previous : 0u);
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "gates")) gates();
    else cases();
    return 0;
}
'''


@pytest.fixture(scope='module')
def executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp('inverse'), 'inverse',
                              HARNESS, sources())


def run(executable, name, cases):
    data = '\n'.join(c.line() for c in cases) + '\n'
    result = subprocess.run([str(executable), name], input=data, text=True,
                            capture_output=True, timeout=90)
    (executable.parent / (name + '.input')).write_text(data, encoding='utf-8')
    (executable.parent / (name + '.output')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stderr
    rows = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    for c, row in zip(cases, rows):
        want = oracle(c)
        if want is None:
            expected = (0, 0, 0, 0, 0)
        else:
            local, out, prev = want
            valid = prev is not None and 0 <= prev <= U64
            expected = (1, local, out, int(valid), prev if valid else 0)
        assert row == expected, (c, row, expected)


def test_invalid_models_and_null_output_unchanged(executable):
    result = subprocess.run([str(executable), 'gates'], capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr


def test_extreme_rates_phases_coordinates_and_unreachable_targets(executable):
    rates = [I32_MIN, -B, -B+1, -B+2, -500000001, -1, 0, 1, 500000001, B, I32_MAX]
    phases = [I32_MIN, -1, 0, 1, I32_MAX]
    bases = [(0, 0), (0, 1), (0, U64), (1, U64-1), (U64, 0), (U64, U64),
             (U64-B, 0), (1234, 1 << 63), (1 << 63, 1 << 63), (0, I32_MAX),
             (0, -I32_MIN), (0, -I32_MIN-1)]
    targets = [0, 1, 2, B-1, B, B+1, (1 << 63)-1, 1 << 63, U64-1, U64]
    cases = [Case(bl, bo, rate, phase, target) for (bl, bo), rate, phase, target
             in itertools.product(bases, rates, phases, targets)]
    run(executable, 'boundaries', cases)


def test_negative_rate_plateau_first_point_and_rounding_carry(executable):
    cases = []
    for rate in (-B+1, -B+2, -999999937, -500000000, -1, 0, 1, 999999937, I32_MAX):
        for delta in (0, 1, 2, 31, B-1, B, B+1, 2*B-1, (1 << 32)-1, U64 // 4, U64):
            c = Case(0, 0, rate, 0, 0)
            f = raw_forward(c, delta)
            for target in (f-1, f, f+1):
                if 0 <= target <= U64:
                    cases.append(Case(0, 0, rate, 0, target))
    run(executable, 'plateaus', cases)


def test_negative_intercept_65bit_need_and_skipped_last_output(executable):
    cases = [Case(0, base, rate, phase, target)
             for base, rate, phase, target in itertools.product(
                 (0, 1, I32_MAX), (-B+1, -1, 0, 1, B, I32_MAX),
                 (I32_MIN, -I32_MAX, -1), (0, 1, U64-I32_MAX, U64-1, U64))]
    # Positive rates skip output coordinates: a target can be below U64 while
    # the first mathematical crossing already lies above U64.
    cases += [Case(0, U64-2, I32_MAX, 0, U64), Case(0, U64-1, B, 0, U64)]
    run(executable, 'wide_need', cases)


def test_big_integer_binary_oracle_30000_random_cases(executable):
    rng = random.Random(0xDCC017)
    cases = []
    rates = [-B+1, -B+2, -1, 0, 1, I32_MAX]
    for i in range(30000):
        rate = rng.choice(rates) if i % 4 == 0 else rng.randint(-B+1, I32_MAX)
        phase = rng.choice([I32_MIN, I32_MAX, -1, 0, 1]) if i % 3 == 0 else rng.randint(I32_MIN, I32_MAX)
        bl, bo, target = (rng.getrandbits(64) for _ in range(3))
        if i % 5 == 0: bl, bo = 0, rng.randrange(1 << 31)
        if i % 5 == 1: target = U64-rng.randrange(1 << 31)
        if i % 5 == 2:
            delta = rng.randrange(U64-bl+1)
            f = raw_forward(Case(bl, bo, rate, phase, 0), bl+delta)
            target = min(U64, max(0, f+rng.randrange(-2, 3)))
        cases.append(Case(bl, bo, rate, phase, target))
    run(executable, 'random_30000', cases)
