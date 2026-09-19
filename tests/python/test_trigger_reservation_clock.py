"""Compile the production VDC reservation admission and deadline model."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("trigger-reservation-clock")
    output = directory / "trigger-reservation-clock.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    domain_sources = ["vdc_domain", "vdc_ring_observer", "vdc_sync_io_adapter",
                      "vdc_tdma_payload", "vdc_timestamp"]
    tdma_sources = ["tdma_service", "tdma_flight_fifo", "tdma_flight_engine",
                    "tdma_process_image_map", "tdma_profile", "tdma_operating_profile",
                    "tdma_payload_registry", "tdma_ring_runtime", "tdma_traffic_scheduler"]
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-DPROJECT_NODE_CAPACITY=8",
               "-I" + str(ROOT / "components/sync_trigger/inc"),
               "-I" + str(ROOT / "components/vdc_dpll_manager/inc"),
               "-I" + str(ROOT / "components/vdc_domain/inc"),
               "-I" + str(ROOT / "components/calibration_manager/inc"),
               "-I" + str(ROOT / "components/tdma/inc"),
               str(ROOT / "tests/unit/test_trigger_reservation_clock.c"),
               str(ROOT / "components/sync_trigger/src/trigger_reservation_clock.c"),
               *(str(ROOT / f"components/vdc_domain/src/{name}.c")
                 for name in domain_sources),
               *(str(ROOT / f"components/tdma/src/{name}.c")
                 for name in tdma_sources),
               "-o", str(output)]
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return output


def test_formal_admission_inverse_deadline_and_identity_freeze(executable):
    result = subprocess.run([str(executable)], text=True, capture_output=True,
                            timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "reservation clock passed" in result.stdout
