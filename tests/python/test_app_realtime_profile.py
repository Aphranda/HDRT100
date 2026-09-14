"""Compile the actual static profile catalog and check every supported table."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import scpi_response_matches_command


@pytest.mark.parametrize("header", [
    "SYST:TDMA:PER", "SYST:TDMA:PERiod",
    "SYSTem:TDMA:PER", "SYSTem:TDMA:PERiod",
])
def test_period_ack_is_matched_without_accepting_unrelated_serial_output(header):
    for response in ('"PENDING",0', '"PENDING",4294967295', 'PENDING,17'):
        assert scpi_response_matches_command(header + " 1500", response)
    for response in ('OK', '"PENDING",-1', '"PENDING",4294967296',
                     '"PENDING",1,2', '"PENDING,1', '375000,1,0,1,0'):
        assert not scpi_response_matches_command(header + " 1500", response)
    assert scpi_response_matches_command(header + "?", "375000,1,0,1,0")
    assert not scpi_response_matches_command(header + "?", '"PENDING",1')
    assert not scpi_response_matches_command(header + "?", '375000,1,0,1')

def test_profile_catalog_is_exact_and_preserves_phase_boundaries(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    exe = tmp_path / ("profile.exe" if os.name == "nt" else "profile")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I"+str(ROOT/"application/inc"), "-I"+str(ROOT/"config"),
        str(ROOT/"application/src/app_realtime_profile.c"),
        str(ROOT/"tests/unit/test_app_realtime_profile.c"), "-o", str(exe)],
        check=True, capture_output=True, text=True, timeout=60)
    subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=3)
