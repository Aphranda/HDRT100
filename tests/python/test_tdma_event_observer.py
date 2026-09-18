"""Compile the complete production observer; compare lifts with a bigint oracle."""
import ctypes
from pathlib import Path
import random
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/tdma/src/tdma_event_observer.c"
INCLUDE = ROOT / "components/tdma/inc"
PERIOD = 2 * (1 << 32) + 1
U64 = (1 << 64) - 1
OK, BAD_ARGUMENT, NO_CANDIDATE, AMBIGUOUS = 0, 1, 10, 11
SENTINEL = 0xA55AA55AA55AA55A


def compile_c(arguments: list[str]) -> None:
    cc = shutil.which("gcc")
    assert cc, "host gcc is required for the production event observer"
    completed = subprocess.run(
        [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-I" + str(INCLUDE), str(SOURCE), *arguments],
        capture_output=True, text=True,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr


def test_production_owner_lifecycle_and_pairing(tmp_path: Path) -> None:
    executable = tmp_path / "event_observer.exe"
    compile_c([str(ROOT / "tests/unit/test_tdma_event_observer.c"), "-o", str(executable)])
    completed = subprocess.run([str(executable)], capture_output=True, text=True)
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert "14 production C case groups passed" in completed.stdout


@pytest.fixture
def lift(tmp_path: Path):
    library = tmp_path / "event_observer.dll"
    compile_c(["-shared", "-o", str(library)])
    loaded = ctypes.CDLL(str(library))
    fn = loaded.tdma_event_observer_lift
    fn.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint64,
                  ctypes.c_uint64, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint64)]
    fn.restype = ctypes.c_int
    return fn


def test_lift_uint64_boundaries_against_bigint_oracle(lift) -> None:
    # Enumerate possible q independently using Python integers. These windows
    # include no lift, unique lifts, multiple lifts and maximum representable
    # clock values; output remains untouched on every rejection.
    rng = random.Random(0xE90C)
    cases = []
    values = [0, 1, 2, (1 << 31), (1 << 32) - 2, (1 << 32) - 1]
    for previous in values:
        for current in values:
            for overhead in (1, 5):
                base = 2 * ((previous - current) % (1 << 32)) + overhead + (current > previous)
                max_q = (U64 - base) // PERIOD
                for q in (0, 1, 7, max_q):
                    target = base + q * PERIOD
                    for margin in (0, 1, PERIOD - 1, PERIOD, PERIOD + 1):
                        cases.append((previous, current, overhead,
                                      max(0, target - margin), min(U64, target + margin)))
    for _ in range(2000):
        lo = rng.randrange(U64 + 1)
        hi = min(U64, lo + rng.randrange(3 * PERIOD))
        cases.append((rng.getrandbits(32), rng.getrandbits(32), rng.choice((1, 5)), lo, hi))
    for previous, current, overhead, lo, hi in cases:
        base = 2 * ((previous - current) % (1 << 32)) + overhead + int(current > previous)
        # Floor/ceil are exact unbounded operations, no firmware arithmetic.
        qmin = max(0, -((base - lo) // PERIOD))
        qmax = (hi - base) // PERIOD
        expected = base + qmin * PERIOD if qmin == qmax and qmax >= 0 else None
        reason = NO_CANDIDATE if qmax < qmin else AMBIGUOUS if qmax > qmin else OK
        result = ctypes.c_uint64(SENTINEL)
        status = lift(previous, current, lo, hi, overhead, ctypes.byref(result))
        assert status == reason, (previous, current, lo, hi, status)
        assert result.value == (expected if expected is not None else SENTINEL)


def test_lift_first_period_threshold_and_rejection_priority(lift) -> None:
    # Enumerate q directly: all these windows end no later than base + P + 1.
    # This independently checks both sides of the fast/general path boundary,
    # including base > P, a raw wrap, and intervals that exclude q=0.
    values = [0, 1, 2, (1 << 31), (1 << 32) - 2, (1 << 32) - 1]
    pairs = [(a, b) for a in values for b in values]
    rng = random.Random(0xFA57)
    pairs += [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(512)]
    for previous, current in pairs:
        for overhead in (1, 5):
            base = 2 * ((previous - current) % (1 << 32)) + overhead + (current > previous)
            bounds = [0, base - 1, base, base + 1,
                      base + PERIOD - 1, base + PERIOD, base + PERIOD + 1]
            for lo in bounds:
                for hi in bounds:
                    candidates = [base + q * PERIOD for q in (0, 1)
                                  if lo <= base + q * PERIOD <= hi]
                    expected = (BAD_ARGUMENT if lo > hi else
                                NO_CANDIDATE if not candidates else
                                AMBIGUOUS if len(candidates) > 1 else OK)
                    result = ctypes.c_uint64(SENTINEL)
                    status = lift(previous, current, lo, hi, overhead, ctypes.byref(result))
                    assert status == expected, (previous, current, overhead, lo, hi, status)
                    assert result.value == (candidates[0] if expected == OK else SENTINEL)
            for overhead_bad in (0, 2, 4, 6, (1 << 32) - 1):
                result = ctypes.c_uint64(SENTINEL)
                assert lift(previous, current, base, base, overhead_bad,
                            ctypes.byref(result)) == BAD_ARGUMENT
                assert result.value == SENTINEL
            assert lift(previous, current, base, base, overhead, None) == BAD_ARGUMENT
