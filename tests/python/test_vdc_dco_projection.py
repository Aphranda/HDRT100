"""Check the production DCO mapping against arbitrary-precision mathematics.

The manager wrapper and pulse-deadline body are extracted unchanged. Only
the snapshot getter and local clock are mocked; the complete Domain sources
are compiled. These checks establish mathematical output coordinates, not
GPIO timing, sample admission, transport correlation or lock eligibility.
"""
from dataclasses import dataclass, replace
import itertools
import random
import re
import subprocess

import pytest

from test_vdc_command_owner import ROOT, MANAGER, compile_executable, function_body

U64 = (1 << 64) - 1
U32 = (1 << 32) - 1
I32_MIN = -(1 << 31)
I32_MAX = (1 << 31) - 1
TIME_SENTINEL = U64 - 1234
PHASE_SENTINEL = I32_MIN + 1234


@dataclass(frozen=True)
class Case:
    valid: int = 1
    nominal: int = 1_500_000
    lock: int = 1
    base_local: int = 0
    base_vdc: int = 0
    local: int = 2_000_000_000
    rate: int = 0
    phase: int = 0
    reference: int = 0
    delay: int = 0
    period: int = 1_000_000
    flags: int = 0  # NULL model, NULL out, unavailable manager snapshot.
    not_before: int = 0

    def line(self, mode):
        return mode + " " + " ".join(str(v) for v in vars(self).values())


def exact_projection(case, local=None):
    """Unlimited Python integers, independent of the C carry decomposition."""
    local = case.local if local is None else local
    if (not case.valid or not case.nominal or case.lock > 8 or
            local < case.base_local or case.flags & 3):
        return None
    delta = local - case.base_local
    magnitude = delta * abs(case.rate) // 1_000_000_000
    correction = -magnitude if case.rate < 0 else magnitude
    result = case.base_vdc + delta + correction + case.phase
    return result if 0 <= result <= U64 else None


def centered_phase(raw, period):
    result = (raw + period // 2) % period - period // 2
    if period % 2 == 0 and result == -(period // 2) and raw > 0:
        result = period // 2
    return result


def exact_residual(case):
    output = exact_projection(case)
    if output is None or not case.period or case.reference >= case.period:
        return None
    return centered_phase(output % case.period -
                          (case.reference + case.delay) % case.period, case.period)


@pytest.fixture(scope="module")
def projection_executable(tmp_path_factory):
    manager = MANAGER.read_text(encoding="utf-8")
    arm_define = re.search(
        r"^#define VDC_DPLL_MANAGER_PHASE_ARM_AHEAD_PERIODS (\d+)u$", manager, re.M)
    assert arm_define
    wrapper = function_body(manager, "vdc_dpll_manager_dco_time_at_local_ns")
    deadline = function_body(manager, "vdc_dpll_manager_compute_dco_phase_pulse_deadline")
    harness = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "vdc_domain.h"
/* The real deadline needs only snapshot.dco; the owner/getter is outside this
 * mathematical test. Do not mistake this mock for runtime-owner evidence. */
typedef struct { vdc_dco_control_t dco; } vdc_dpll_manager_runtime_snapshot_t;
static vdc_dpll_manager_runtime_snapshot_t fixture_snapshot;
static bool fixture_available;
static uint64_t fixture_now;
static uint64_t vdc_dpll_manager_now_ns(void) { return fixture_now; }
static bool vdc_dpll_manager_get_runtime_snapshot(vdc_dpll_manager_runtime_snapshot_t *out)
{
    if (!fixture_available) return false;
    *out = fixture_snapshot;
    return true;
}
''' + arm_define.group(0) + r'''
static bool vdc_dpll_manager_dco_time_at_local_ns(const vdc_dco_control_t *dco,
                                                uint64_t local_ns, uint64_t *dco_ns)
{
''' + wrapper + r'''
}
static bool vdc_dpll_manager_compute_dco_phase_pulse_deadline(uint64_t not_before_ns,
    uint32_t pulse_period_ns, uint64_t *target_local_ns)
{
''' + deadline + r'''
}
int main(void)
{
    char mode;
    while (scanf(" %c", &mode) == 1) {
        vdc_dco_control_t dco = {0};
        uint64_t local_ns, not_before_ns;
        uint32_t reference, delay, period, flags;
        assert(scanf("%" SCNu32 " %" SCNu32 " %" SCNu32
                     " %" SCNu64 " %" SCNu64 " %" SCNu64
                     " %" SCNd32 " %" SCNd32 " %" SCNu32
                     " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu64,
            &dco.valid, &dco.nominal_period_ns, &dco.lock_state,
            &dco.base_local_tick64, &dco.base_vdc_time64_ns, &local_ns,
            &dco.period_adjust_ppb, &dco.phase_offset_ns,
            &reference, &delay, &period, &flags, &not_before_ns) == 13);
        const vdc_dco_control_t before = dco;
        const vdc_dco_control_t *model = flags & 1u ? NULL : &dco;
        uint64_t output = UINT64_MAX - 1234u;
        int32_t residual = INT32_MIN + 1234;
        bool ok;
        if (mode == 'P') {
            ok = vdc_domain_dco_local_to_output_ns(model, local_ns,
                                                   flags & 2u ? NULL : &output);
        } else if (mode == 'W') {
            ok = vdc_dpll_manager_dco_time_at_local_ns(model, local_ns,
                                                      flags & 2u ? NULL : &output);
        } else if (mode == 'R') {
            ok = vdc_domain_dco_output_phase_residual_ns(model, local_ns,
                reference, delay, period, flags & 2u ? NULL : &residual);
        } else {
            assert(mode == 'D');
            fixture_snapshot.dco = dco;
            fixture_now = local_ns;
            fixture_available = !(flags & 4u);
            ok = vdc_dpll_manager_compute_dco_phase_pulse_deadline(not_before_ns,
                period, flags & 2u ? NULL : &output);
            assert(memcmp(&fixture_snapshot.dco, &before, sizeof(before)) == 0);
        }
        assert(memcmp(&dco, &before, sizeof(before)) == 0);
        if (mode == 'R') printf("%u %" PRId32 "\n", (unsigned)ok, residual);
        else printf("%u %" PRIu64 "\n", (unsigned)ok, output);
    }
    return 0;
}
'''
    sources = [ROOT / f"components/vdc_domain/src/{name}.c" for name in (
        "vdc_domain", "vdc_timestamp", "vdc_ring_observer", "vdc_sync_io_adapter",
        "vdc_tdma_payload")]
    sources += [ROOT / f"components/tdma/src/{name}.c" for name in (
        "tdma_service", "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map", "tdma_ring_runtime",
        "tdma_traffic_scheduler", "tdma_service_timing")]
    exe = compile_executable(tmp_path_factory.mktemp("dco-projection"),
                             "dco_projection", harness, sources)
    return exe, int(arm_define.group(1))


def run_cases(executable, mode, cases):
    result = subprocess.run([str(executable[0])],
                            input="\n".join(c.line(mode) for c in cases) + "\n",
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    rows = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    assert len(rows) == len(cases), result.stdout
    return rows


def check_oracle(executable, mode, cases):
    oracle = exact_residual if mode == "R" else exact_projection
    sentinel = PHASE_SENTINEL if mode == "R" else TIME_SENTINEL
    for case, actual in zip(cases, run_cases(executable, mode, cases)):
        expected = oracle(case)
        assert actual == (int(expected is not None),
                          sentinel if expected is None else expected), (mode, case, actual, expected)


@pytest.mark.parametrize("mode", ["P", "W"])
def test_integer_edges_and_cancellation(projection_executable, mode):
    deltas = [0, 1, 2, 999_999_999, 1_000_000_000, 1_000_000_001,
              (1 << 32) - 1, 1 << 32, (1 << 63) - 1, 1 << 63, U64 - 1, U64]
    rates = [I32_MIN, -2_000_000_000, -1_000_000_000, -10000, -1,
             0, 1, 10000, 1_000_000_000, I32_MAX]
    phases = [I32_MIN, -1, 0, 1, I32_MAX]
    cases = [Case(local=delta, rate=rate, phase=phase, base_vdc=base)
             for delta, rate, phase, base in itertools.product(
                 deltas, rates, phases, [0, 1234, U64])]
    # Final results remain legal despite overflowing unsigned intermediate
    # base+delta or a rate magnitude greater than UINT64_MAX.
    cases += [Case(local=U64, base_vdc=U64, rate=-2_000_000_000),
              Case(local=100, base_vdc=U64, phase=-100),
              Case(local=2, rate=I32_MIN, phase=2)]
    assert [exact_projection(c) for c in cases[-3:]] == [0, U64, 0]
    check_oracle(projection_executable, mode, cases)


@pytest.mark.parametrize("mode", ["P", "W", "R"])
def test_invalid_model_or_argument_preserves_output(projection_executable, mode):
    cases = [Case(valid=0), Case(nominal=0), Case(lock=9), Case(lock=U32),
             Case(base_local=2_000_000_001), Case(flags=1), Case(flags=2),
             Case(local=0, phase=-1), Case(local=U64, phase=1)]
    if mode == "R":
        cases += [Case(period=0), Case(reference=1_000_000), Case(reference=U32)]
    check_oracle(projection_executable, mode, cases)


def test_random_full_range_against_big_integer_oracle(projection_executable):
    rng = random.Random(20260916)
    cases = []
    for _ in range(6000):
        base_local = rng.randrange(U64 + 1)
        local = rng.randrange(base_local, U64 + 1)
        cases.append(Case(base_local=base_local, local=local,
                          base_vdc=rng.randrange(U64 + 1),
                          rate=rng.randint(I32_MIN, I32_MAX),
                          phase=rng.randint(I32_MIN, I32_MAX),
                          lock=rng.randrange(9)))
    for mode in ("P", "W"):
        check_oracle(projection_executable, mode, cases)


def test_modulo_delay_and_odd_even_half_period(projection_executable):
    cases = []
    for period in (1, 2, 3, 5, 999_999, 1_000_000, U32 - 1, U32):
        phases = {0, period // 2, period - 1}
        cases.extend(Case(local=local, reference=ref, delay=delay, period=period)
                     for local, ref, delay in itertools.product(phases, phases,
                                                               (0, 1, U32)))
    cases += [Case(local=5, reference=0, period=10),
              Case(local=0, reference=5, period=10)]
    assert [exact_residual(c) for c in cases[-2:]] == [5, -5]
    check_oracle(projection_executable, "R", cases)


def test_random_output_residual_against_big_integer_oracle(projection_executable):
    rng = random.Random(190_916)
    cases = []
    for _ in range(4000):
        period = rng.randint(1, U32)
        cases.append(Case(local=rng.randrange(U64 + 1),
                          base_vdc=rng.randrange(U64 + 1),
                          rate=rng.randint(I32_MIN, I32_MAX),
                          phase=rng.randint(I32_MIN, I32_MAX), period=period,
                          reference=rng.randrange(period), delay=rng.randrange(U32 + 1)))
    check_oracle(projection_executable, "R", cases)


def test_same_event_phase_and_rate_steps_change_output_residual(projection_executable):
    # One correlated event: NO1 output phase is explicitly supplied, while
    # the unrelated raw hardware phase remains fixed through DCO steps.
    baseline = Case(base_local=1_000_000_000_000, base_vdc=5_000_000_800,
                    local=1_002_000_000_200, reference=0, delay=1000)
    cases = [baseline, replace(baseline, phase=300), replace(baseline, phase=-300),
             replace(baseline, rate=1000), replace(baseline, rate=-1000)]
    raw_hardware_residuals = [centered_phase(c.local % c.period -
        (12345 + c.delay) % c.period, c.period) for c in cases]
    assert len(set(raw_hardware_residuals)) == 1
    assert run_cases(projection_executable, "R", cases) == [
        (1, 0), (1, 300), (1, -300), (1, 2000), (1, -2000)]
    # Delay and local-clock origin changes are separate coordinates. Equal
    # local/base translation preserves output; an incorrect reference phase
    # changes the result, demonstrating that correlation remains caller-owned.
    shifted = replace(baseline, base_local=baseline.base_local + 77_000,
                      local=baseline.local + 77_000)
    wrong_reference = replace(baseline, reference=12345)
    assert run_cases(projection_executable, "R", [shifted, wrong_reference]) == [
        (1, 0), (1, -12345)]


def test_actual_output_deadline_reaches_next_dco_boundary(projection_executable):
    cases = [Case(base_local=origin, base_vdc=17_000_123, local=origin + elapsed,
                  rate=rate, phase=phase, period=period,
                  not_before=origin + elapsed + not_before_delta)
             for origin, elapsed, rate, phase, period, not_before_delta in itertools.product(
                 (0, 1_000_000_000_000), (1_234_567, 8_640_000_000_000_000),
                 (-10000, -1, 0, 1, 10000), (-1000, 0, 1000),
                 (1_000_000, 1_500_000, 5_000_000, 10_000_000, 15_000_000),
                 (0, 40_000_000))]
    for case, (ok, target) in zip(cases, run_cases(projection_executable, "D", cases)):
        minimum = max(case.not_before, case.local + case.period * projection_executable[1])
        minimum_output = exact_projection(case, minimum)
        next_boundary = (minimum_output // case.period + 1) * case.period
        assert ok == 1 and minimum <= target <= case.local + U32, (case, ok, target)
        assert abs(exact_projection(case, target) - next_boundary) <= 1, (case, target)


def test_output_phase_step_moves_deadline_with_opposite_coordinate_sign(projection_executable):
    base = Case(local=2_000_123_000)
    cases = [base, replace(base, phase=300), replace(base, phase=-300)]
    rows = run_cases(projection_executable, "D", cases)
    assert [ok for ok, _ in rows] == [1, 1, 1]
    baseline = rows[0][1]
    assert [target - baseline for _, target in rows] == [0, -300, 300]


def test_deadline_rejects_unavailable_or_invalid_model(projection_executable):
    cases = [Case(valid=0), Case(nominal=0), Case(lock=9), Case(period=0),
             Case(flags=2), Case(flags=4), Case(base_local=3_000_000_000),
             Case(not_before=2_000_000_000 + U32 + 1)]
    assert run_cases(projection_executable, "D", cases) == [(0, TIME_SENTINEL)] * len(cases)
