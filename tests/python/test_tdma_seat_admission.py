"""Execute the real seat admission model; no hardware windows are opened."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_seat_admission_state_and_budget_boundaries(tmp_path):
    cc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = tmp_path / "seat-admission.exe"
    command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
               "-I" + str(ROOT / "components/tdma/inc"),
               str(ROOT / "components/tdma/src/tdma_seat_admission.c"),
               str(ROOT / "tests/unit/test_tdma_seat_admission.c"), "-o", str(exe)]
    for name, args in (("compile", command), ("run", [str(exe)])):
        run = subprocess.run(args, capture_output=True, text=True, timeout=60)
        (tmp_path / (name + ".log")).write_text(run.stdout + run.stderr, encoding="utf-8")
        assert run.returncode == 0, run.stdout + run.stderr
    assert "5 case groups passed" in run.stdout
