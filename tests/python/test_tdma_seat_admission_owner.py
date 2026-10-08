"""Host harness for the seat admission owner wiring.

The component is pure C with no SDK dependency, so the slice compiles and runs
directly. Cases assert that an unmeasured budget dimension keeps the seat
closed instead of granting assumed headroom.
"""
from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    "tests/unit/test_tdma_seat_admission_owner.c",
    "components/tdma/src/tdma_seat_admission_owner.c",
    "components/tdma/src/tdma_seat_admission.c",
]


def compiler() -> str:
    found = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not found and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        found = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert found, "A host C compiler is required"
    return found


def test_seat_admission_owner_cases(tmp_path: Path) -> None:
    exe = tmp_path / ("seat_owner.exe" if os.name == "nt" else "seat_owner")
    command = [compiler(), "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/tdma/inc")]
    command += [str(ROOT / source) for source in SOURCES]
    command += ["-o", str(exe)]
    built = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(exe)], cwd=ROOT, text=True, capture_output=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all seat admission owner cases passed" in result.stdout
    assert "FAIL " not in result.stdout
