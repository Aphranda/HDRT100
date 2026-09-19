"""Compile and execute the bounded position/VDC reservation planner."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("trigger-reservation-planner")
    output = directory / "trigger-reservation-planner.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-DPROJECT_NODE_CAPACITY=8",
               "-I" + str(ROOT / "components/sync_trigger/inc"),
               "-I" + str(ROOT / "components/vdc_dpll_manager/inc"),
               "-I" + str(ROOT / "components/vdc_domain/inc"),
               "-I" + str(ROOT / "components/calibration_manager/inc"),
               "-I" + str(ROOT / "components/tdma/inc"),
               str(ROOT / "tests/unit/test_trigger_reservation_planner.c"),
               str(ROOT / "components/sync_trigger/src/trigger_reservation_planner.c"),
               str(ROOT / "components/sync_trigger/src/trigger_reservation_config.c"),
               "-o", str(output)]
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return output


def test_bounded_position_prediction_and_fact_gate(executable):
    result = subprocess.run([str(executable)], text=True, capture_output=True,
                            timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "reservation planner passed" in result.stdout
