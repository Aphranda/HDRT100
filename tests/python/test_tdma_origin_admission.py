"""Execute Calibration publication and TDMA owner admission against a host device.

The real includes implement the grant lifecycle. Physical callbacks and the
active-model projection are controlled boundaries, tested separately.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def admission_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("origin-admission")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler
    exe = build / ("admission.exe" if os.name == "nt" else "admission")
    includes = ["components/tdma/inc", "components/distributed_refmem/inc",
                "components/calibration_manager/inc", "components/ota_manager/inc"]
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror"]
    command += ["-I" + str(ROOT / path) for path in includes]
    # Compile the actual pure cadence calculation without the graph builder's
    # unrelated SDK register declarations.
    source = (ROOT / "components/tdma/src/tdma_origin_plan.c").read_text(encoding="utf-8")
    cadence = build / "cadence.c"
    cadence.write_text('#include "tdma_origin_plan.h"\n#include <string.h>\n'
        'bool tdma_origin_cadence_calculate(uint32_t clk_sys_hz, uint32_t baud_hz, '
        'uint32_t physical_bytes, uint32_t period_ns, tdma_origin_cadence_t *cadence) {'
        + c_definition_body(source, "tdma_origin_cadence_calculate") + '}\n', encoding="utf-8")
    command += [str(ROOT / "tests/unit/test_tdma_origin_admission.c"),
                str(cadence), "-o", str(exe)]
    subprocess.run(command, check=True, timeout=60)
    return exe


@pytest.mark.parametrize("case", ["publish", "stale", "expiry", "prepare", "fault"])
def test_origin_trial_lifecycle(admission_exe, case):
    result = subprocess.run([str(admission_exe), case], capture_output=True,
                            text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr
