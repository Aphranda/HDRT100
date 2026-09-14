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
    assert "13 production C case groups passed" in completed.stdout


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
        result = ctypes.c_uint64(0xA55AA55AA55AA55A)
        status = lift(previous, current, lo, hi, overhead, ctypes.byref(result))
        assert (status == 0) == (expected is not None), (previous, current, lo, hi, status)
        assert result.value == (expected if expected is not None else 0xA55AA55AA55AA55A)
