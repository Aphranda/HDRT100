"""Affine rate coordinate against exact rational arithmetic, including uptime overflow."""
from fractions import Fraction
import itertools
import math
import random
import subprocess

import pytest

from test_vdc_command_owner import compile_executable

U64 = (1 << 64) - 1
SENTINEL = U64 - 1234


@pytest.fixture(scope="module")
def rate_coordinate_executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp("rate-coordinate"), "coordinate", r'''
#include <inttypes.h>
#include <stdio.h>
#include "vdc_model_projection.h"
int main(void) {
    int32_t rate;uint32_t hz,up,null_out;uint64_t ticks;
    while(scanf("%"SCNd32" %"SCNu32" %"SCNu64" %"SCNu32" %"SCNu32,
        &rate,&hz,&ticks,&up,&null_out)==5) {
        uint64_t out=UINT64_MAX-1234;
        const bool ok=vdc_model_rate_coordinate_ns(rate,hz,ticks,up!=0,null_out?NULL:&out);
        printf("%u %"PRIu64"\n",ok,out);
    }
    return 0;
}
''')


def evaluate(executable, cases):
    result = subprocess.run([str(executable)], input="\n".join(
        " ".join(map(str, row)) for row in cases) + "\n", text=True,
        capture_output=True, timeout=20)
    assert result.returncode == 0, result.stderr
    rows = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    for case, actual in zip(cases, rows, strict=True):
        rate, hz, ticks, up, null_out = case
        expected = None
        if not null_out and 0 < hz <= 500_000_000 and rate > -1_000_000_000:
            value = Fraction(ticks * (1_000_000_000 + rate), hz)
            expected = math.ceil(value) if up else math.floor(value)
            if expected > U64:
                expected = None
        assert actual == ((0, SENTINEL) if expected is None else (1, expected)), (case, actual, expected)
    return rows


def test_fractional_sign_and_large_uptime(rate_coordinate_executable):
    rng = random.Random(16091604)
    cases = [(p, h, t, up, 0) for p, h, t, up in itertools.product(
        [-999_999_999, -10000, -1, 0, 1, 10000, 2_147_483_647],
        [1, 3, 125_000_000, 250_000_000, 499_999_999, 500_000_000],
        [0, 1, 251, 30_319_960_738, U64 // 4, U64 // 4 + 1, U64], [0, 1])]
    cases += [(rng.randint(-999_999_999, 2_147_483_647), rng.randint(1, 500_000_000),
               rng.getrandbits(64), rng.randrange(2), 0) for _ in range(600)]
    evaluate(rate_coordinate_executable, cases)


def test_bad_inputs_and_rounding_overflow_preserve_output(rate_coordinate_executable):
    cases = [(p, h, U64, up, null) for p, h, up, null in itertools.product(
        [-2_147_483_648, -1_000_000_000, 0], [0, 1, 500_000_001], [0, 1], [0, 1])]
    # floor fits exactly while ceil does not; exercise the final-addition guard.
    q, h = 1_000_000_001, 500_000_000
    ticks = (U64 * h) // q
    cases += [(q - 1_000_000_000, h, ticks + offset, up, 0)
              for offset, up in itertools.product([0, 1], [0, 1])]
    evaluate(rate_coordinate_executable, cases)


def test_shared_anchor_drops_out_without_claiming_absolute_time(rate_coordinate_executable):
    p, h, a, b = 1591, 250_000_000, 1_000_000_123, 1_250_000_123
    rows = evaluate(rate_coordinate_executable, [(p, h, t, up, 0)
        for t, up in itertools.product([a, b], [0, 1])])
    delta_lo, delta_hi = rows[2][1] - rows[1][1], rows[3][1] - rows[0][1]
    ideal = Fraction((b - a) * (1_000_000_000 + p), h)
    assert delta_lo <= ideal <= delta_hi and delta_hi - delta_lo <= 2
    # Any fixed observer enable anchor is absent from both source coordinates.
    assert ideal == 1_000_001_591
