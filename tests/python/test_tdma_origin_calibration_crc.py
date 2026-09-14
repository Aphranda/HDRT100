"""Compare the realtime implementation with Calibration's actual CRC library."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("capacity", (2, 6, 8))
def test_origin_calibration_crc_bytes(tmp_path, capacity):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler
    exe = tmp_path / ("crc.exe" if os.name == "nt" else "crc")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        f"-DPROJECT_NODE_CAPACITY={capacity}", "-I" + str(ROOT / "components/tdma/inc"),
        "-I" + str(ROOT / "third_party/portable_ota/include"),
        str(ROOT / "components/tdma/src/tdma_origin_calibration_crc.c"),
        str(ROOT / "third_party/portable_ota/src/pota_crc32.c"),
        str(ROOT / "tests/unit/test_tdma_origin_calibration_crc.c"), "-o", str(exe)]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
