"""Run the production Core1 timing recorder with a deterministic clk_sys clock."""
import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def test_phase_attribution_preserves_one_complete_worst_case(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    exe = tmp_path / ("timing.exe" if os.name == "nt" else "timing")
    subprocess.run([
        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DTDMA_SERVICE_TIMING_ENABLED=1",
        "-I" + str(ROOT / "components/tdma/inc"),
        "-I" + str(ROOT / "components/vdc_domain/inc"),
        str(ROOT / "tests/unit/test_tdma_service_timing.c"), "-o", str(exe),
    ], check=True, timeout=60)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr
