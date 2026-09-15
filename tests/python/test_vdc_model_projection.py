"""Real interval bridge + Domain DCO mapper against independent exact arithmetic.

The test includes the production projection header and links full production
Domain sources. It proves internal coordinate bounds, not clock-tree admission,
model/event association, physical GPIO adoption, control eligibility or lock.
"""
from dataclasses import dataclass, replace
from fractions import Fraction
import itertools
import math
import random
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable

U64 = (1 << 64) - 1
LO_SENTINEL, HI_SENTINEL = U64 - 1234, U64 - 5678


@dataclass(frozen=True)
class Interval:
    valid: int = 1
    nominal: int = 1_500_000
    lock: int = 1
    base_local: int = 0
    base_output: int = 0
    rate: int = 0
    phase: int = 0
    raw_before: int = 1_000_000_000
    local_ns: int = 5_000_000_000
    raw_after: int = 1_000_000_004
    hz: int = 250_000_000
    event_lo: int = 999_999_000
    event_hi: int = 999_999_004
    flags: int = 0  # NULL dco/bridge/lo/hi; alias lo/hi.

    def line(self):
        return ' '.join(str(value) for value in vars(self).values())


def local_bounds(case):
    """Extrema over raw bridge/event brackets and TIMER0's full 999 ns bin."""
    far = Fraction(case.raw_after - case.event_lo, case.hz) * 1_000_000_000
    near = Fraction(case.raw_before - case.event_hi, case.hz) * 1_000_000_000
    return math.floor(case.local_ns - far), math.ceil(case.local_ns + 999 - near)


def dco_oracle(case, local):
    if not case.valid or not case.nominal or case.lock > 8 or local < case.base_local:
        return None
    elapsed = local - case.base_local
    # int(Fraction) truncates toward zero, as required by the real mapper.
    correction = int(Fraction(elapsed * case.rate, 1_000_000_000))
    output = case.base_output + elapsed + correction + case.phase
    return output if 0 <= output <= U64 else None


def interval_oracle(case):
    if (case.flags or not 0 < case.hz <= 500_000_000 or case.raw_before > case.raw_after or
            case.event_lo > case.event_hi or case.event_hi > case.raw_before or
            case.rate <= -1_000_000_000 or case.local_ns + 999 > U64 or
            case.raw_after - case.event_lo > case.hz * 2):
        return None
    lo, hi = local_bounds(case)
    if lo < 0 or hi > U64:
        return None
    output_lo, output_hi = dco_oracle(case, lo), dco_oracle(case, hi)
    if output_lo is None or output_hi is None or output_lo > output_hi:
        return None
    return output_lo, output_hi


@pytest.fixture(scope='module')
def interval_executable(tmp_path_factory):
    sources = [ROOT / f'components/vdc_domain/src/{name}.c' for name in (
        'vdc_domain', 'vdc_timestamp', 'vdc_ring_observer', 'vdc_sync_io_adapter', 'vdc_tdma_payload')]
    sources += [ROOT / f'components/tdma/src/{name}.c' for name in (
        'tdma_service', 'tdma_profile', 'tdma_operating_profile', 'tdma_payload_registry',
        'tdma_flight_fifo', 'tdma_flight_engine', 'tdma_process_image_map', 'tdma_ring_runtime',
        'tdma_traffic_scheduler', 'tdma_service_timing')]
    return compile_executable(tmp_path_factory.mktemp('model-projection'), 'model_projection', HARNESS, sources)


def check_cases(executable, name, cases):
    data = '\n'.join(case.line() for case in cases) + '\n'
    result = subprocess.run([str(executable)], input=data, text=True, capture_output=True, timeout=30)
    (executable.parent / (name + '.input')).write_text(data, encoding='utf-8')
    (executable.parent / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    rows = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    for case, actual in zip(cases, rows, strict=True):
        expected = interval_oracle(case)
        assert actual == ((0, LO_SENTINEL, HI_SENTINEL) if expected is None else
                          (1, *expected)), (case, actual, expected)
    return rows


def test_full_999ns_bin_and_raw_bracket_are_never_collapsed(interval_executable):
    instant = Interval(raw_before=100, raw_after=100, event_lo=100, event_hi=100, local_ns=10_000)
    cases = [instant, replace(instant, raw_after=104), replace(instant, event_lo=96),
             replace(instant, raw_after=104, event_lo=96)]
    rows = check_cases(interval_executable, 'quantization', cases)
    assert rows == [(1, 10_000, 10_999), (1, 9984, 10_999),
                    (1, 9984, 10_999), (1, 9968, 10_999)]


def test_fractional_tick_rounding_is_outward_at_both_ends(interval_executable):
    case = Interval(hz=3, raw_before=10, raw_after=11, event_lo=8, event_hi=9,
                    local_ns=2_000_000_000)
    assert local_bounds(case) == (1_000_000_000, 1_666_667_666)
    check_cases(interval_executable, 'fractional_ticks', [case,
        replace(case, hz=499_999_999), replace(case, hz=7)])


def test_positive_negative_rate_and_phase_use_real_mapper(interval_executable):
    cases = [replace(Interval(), rate=rate, phase=phase, base_output=base)
             for rate, phase, base in itertools.product(
                [-999_999_999, -10000, -1, 0, 1, 10000, 2_147_483_647],
                [-2_147_483_648, -1, 0, 1, 2_147_483_647], [0, 10_000_000_000])]
    check_cases(interval_executable, 'signed_rate_phase', cases)


def test_anchor_before_inside_and_after_interval(interval_executable):
    base = Interval()
    lo, hi = local_bounds(base)
    cases = [replace(base, base_local=anchor, base_output=100_000)
             for anchor in [0, lo - 1, lo, lo + 1, hi, hi + 1]]
    rows = check_cases(interval_executable, 'anchor', cases)
    assert [row[0] for row in rows] == [1, 1, 1, 0, 0, 0]


def test_two_second_bound_includes_entire_raw_bridge(interval_executable):
    cases = []
    for hz, extra in itertools.product([1, 3, 125_000_000, 250_000_000, 500_000_000], [-1, 0, 1]):
        # The after edge, including bridge width, determines maximum age.
        distance = 2 * hz + extra
        cases.append(Interval(hz=hz, raw_before=U64 - 1, raw_after=U64,
            event_lo=U64 - distance, event_hi=U64 - distance, local_ns=3_000_000_000))
    rows = check_cases(interval_executable, 'two_second', cases)
    assert [row[0] for row in rows] == [1, 1, 0] * 5


@pytest.mark.parametrize('flag', [1, 2, 4, 8, 16, 31])
def test_null_and_alias_outputs_reject_without_writes(interval_executable, flag):
    check_cases(interval_executable, f'bad_pointer_{flag}', [replace(Interval(), flags=flag)])


def test_invalid_structure_and_mapper_failure_preserve_both_outputs(interval_executable):
    cases = [replace(Interval(), **change) for change in [
        dict(hz=0), dict(hz=500_000_001), dict(raw_before=1_000_000_005),
        dict(event_lo=999_999_005), dict(event_hi=1_000_000_001), dict(local_ns=1),
        dict(rate=-1_000_000_000), dict(rate=-2_147_483_648),
        dict(valid=0), dict(nominal=0), dict(lock=9), dict(lock=0xFFFFFFFF),
        dict(local_ns=U64 - 998), dict(base_output=U64),
    ]]
    cases += [Interval(raw_before=100, raw_after=100, event_lo=100, event_hi=100,
        local_ns=1000, base_local=1000, base_output=U64 - 998),
        Interval(raw_before=100, raw_after=100, event_lo=100, event_hi=100,
        local_ns=1000, base_local=1000, phase=-1)]
    assert all(interval_oracle(case) is None for case in cases)
    check_cases(interval_executable, 'invalid', cases)


def test_uint64_edges_and_large_term_cancellation(interval_executable):
    cases = [Interval(raw_before=U64, raw_after=U64, event_lo=U64, event_hi=U64,
        local_ns=U64 - 999, base_local=U64 - 999, base_output=U64 - 999),
        Interval(raw_before=U64, raw_after=U64, event_lo=U64, event_hi=U64,
        local_ns=U64 - 999, rate=-999_999_999, base_output=U64 // 2),
        Interval(raw_before=0, raw_after=0, event_lo=0, event_hi=0, local_ns=0),
        Interval(raw_before=1, raw_after=1, event_lo=0, event_hi=0, local_ns=4),
    ]
    rows = check_cases(interval_executable, 'uint64', cases)
    assert all(row[0] for row in rows)
    assert rows[0] == (1, U64 - 999, U64)


def test_random_exact_oracle_and_corner_enclosure(interval_executable):
    rng = random.Random(0xB21D6E)
    cases = []
    for _ in range(5000):
        hz = rng.choice([1, 3, 125_000_000, 250_000_000, 333_333_333, 500_000_000])
        far = rng.randrange(2 * hz + 2)
        near = rng.randrange(far + 1)
        event_width = rng.randrange(far - near + 1)
        event_lo = rng.choice([0, U64 - far, rng.randrange(U64 - far + 1)])
        event_hi = event_lo + event_width
        raw_after = event_lo + far
        raw_before = event_hi + near
        local = rng.choice([0, 5_000_000_000, U64 - 999, rng.randrange(U64 + 1)])
        base_local = rng.choice([0, local, rng.randrange(local + 1)])
        cases.append(Interval(hz=hz, event_lo=event_lo, event_hi=event_hi,
            raw_before=raw_before, raw_after=raw_after, local_ns=local,
            base_local=base_local, base_output=rng.randrange(U64 + 1),
            rate=rng.randint(-999_999_999, 2_147_483_647), phase=rng.randint(-2_147_483_648, 2_147_483_647)))
    rows = check_cases(interval_executable, 'random', cases)
    # Independent extremal samples: every integer coordinate permitted at
    # each raw bracket/bin corner must map inside the returned output bounds.
    for case, (ok, output_lo, output_hi) in zip(cases, rows, strict=True):
        if not ok:
            continue
        for bridge_raw, event, quantization in itertools.product(
                [case.raw_before, case.raw_after], [case.event_lo, case.event_hi], [0, 999]):
            coordinate = case.local_ns + quantization - Fraction(
                (bridge_raw - event) * 1_000_000_000, case.hz)
            for local in (math.floor(coordinate), math.ceil(coordinate)):
                projected = dco_oracle(case, local)
                assert projected is not None and output_lo <= projected <= output_hi


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "vdc_model_projection.h"
int main(void)
{
    vdc_dco_control_t dco={0};vdc_timestamp_clock_bridge_t bridge={0};
    uint64_t event_lo,event_hi;uint32_t flags;
    while(scanf("%"SCNu32" %"SCNu32" %"SCNu32" %"SCNu64" %"SCNu64" %"SCNd32" %"SCNd32
        " %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu32" %"SCNu64" %"SCNu64" %"SCNu32,
        &dco.valid,&dco.nominal_period_ns,&dco.lock_state,&dco.base_local_tick64,&dco.base_vdc_time64_ns,
        &dco.period_adjust_ppb,&dco.phase_offset_ns,&bridge.raw_before,&bridge.local_ns,&bridge.raw_after,
        &bridge.tick_hz,&event_lo,&event_hi,&flags)==14) {
        const vdc_dco_control_t saved_dco=dco;
        const vdc_timestamp_clock_bridge_t saved_bridge=bridge;
        uint64_t lo=UINT64_MAX-1234,hi=UINT64_MAX-5678;
        bool ok=vdc_model_project_interval(flags&1?NULL:&dco,flags&2?NULL:&bridge,event_lo,event_hi,
            flags&4?NULL:&lo,flags&8?NULL:flags&16?&lo:&hi);
        assert(!memcmp(&dco,&saved_dco,sizeof(dco)) && !memcmp(&bridge,&saved_bridge,sizeof(bridge)));
        printf("%u %"PRIu64" %"PRIu64"\n",(unsigned)ok,lo,hi);
    }
    return 0;
}
'''
