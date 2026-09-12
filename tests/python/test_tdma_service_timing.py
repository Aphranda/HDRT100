"""Run the production Core1 timing recorder with a deterministic clk_sys clock."""
import os
from pathlib import Path
import shutil
import subprocess
import pytest

from tools.tdma_ring_monitor.tdma_service_timing import parse_service_timing


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


@pytest.mark.parametrize("version,count", [(1, 11), (2, 15), (3, 19), (4, 25)])
def test_profile_wire_versions_and_inclusive_intervals(version, count):
    fields = [version, 250000000, 2, 9, count, 1, 8, 2**40, 1000, 0]
    fields += [value for i in range(count) for value in (100 + i, i + 1)]
    result = parse_service_timing(",".join(map(str, fields)))
    assert result["start_ticks"] == 2**40 and result["total_ticks"] == 1000
    assert result["stages"]["rx_capture"] == {"ticks": 107, "calls": 8}
    if version >= 2:
        assert result["stages"]["rx_latch"] == {"ticks": 114, "calls": 15}
    else:
        assert "rx_latch" not in result["stages"]
    if version >= 3:
        assert result["stages"]["rx_dma_observe"] == {"ticks": 115, "calls": 16}
        assert result["stages"]["rx_ring_copy"] == {"ticks": 118, "calls": 19}
    else:
        assert "rx_dma_observe" not in result["stages"]
    if version == 4:
        assert result["stages"]["ring_runtime"] == {"ticks": 119, "calls": 20}
        assert result["stages"]["adapter_status"] == {"ticks": 124, "calls": 25}
    else:
        assert "ring_runtime" not in result["stages"]


@pytest.mark.parametrize("raw", [
    "2,250000000", "9,250000000,0,1,15,0,1,20,100,0",
    "2,250000000,0,1,11,0,1,20,100,0" + ",0,0"*11,
    "1,250000000,0,1,11,0,1,20,100,0" + ",0,0"*15,
    "2,250000000,0,1,15,0,1,20,100,0" + ",0,0"*14,
    "3,250000000,0,1,15,0,1,20,100,0" + ",0,0"*15,
    "3,250000000,0,1,19,0,1,20,100,0" + ",0,0"*18,
    "4,250000000,0,1,19,0,1,20,100,0" + ",0,0"*19,
    "4,250000000,0,1,25,0,1,20,100,0" + ",0,0"*24,
])
def test_profile_rejects_unknown_truncated_or_mismatched_schema(raw):
    with pytest.raises(ValueError):
        parse_service_timing(raw)


def test_profile_unavailable():
    assert parse_service_timing('"UNAVAILABLE"') is None
