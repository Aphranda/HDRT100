"""Build and execute the fixed four-slot reservation configuration model."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("trigger-reservation-config")
    output = directory / "trigger-reservation-config.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/sync_trigger/inc"),
               str(ROOT / "tests/unit/test_trigger_reservation_config.c"),
               str(ROOT / "components/sync_trigger/src/trigger_reservation_config.c"),
               "-o", str(output)]
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return output


def test_fixed_four_slot_plan_and_validation(executable):
    result = subprocess.run([str(executable)], text=True, capture_output=True,
                            timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "reservation configuration passed" in result.stdout


def test_contract_constants_are_fixed_to_requested_io():
    header = (ROOT / "components/sync_trigger/inc/trigger_reservation_config.h").read_text(
        encoding="utf-8")
    expected = {
        "TRIGGER_RESERVATION_COUNTER_INPUT": "1u",
        "TRIGGER_RESERVATION_READY_INPUT": "2u",
        "TRIGGER_RESERVATION_SP8T_OUTPUT_MASK": "0x03u",
        "TRIGGER_RESERVATION_SP2T_OUTPUT_MASK": "0x04u",
        "TRIGGER_RESERVATION_VNA_OUTPUT_MASK": "0x08u",
        "TRIGGER_RESERVATION_ITEM_COUNT": "8u",
    }
    for name, value in expected.items():
        assert f"#define {name} {value}" in header
