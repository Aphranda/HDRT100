"""Execute the production admission and SCPI callbacks against controlled owners.

The C fixture retains the pre-diagnostic admission function as an independent
behavior oracle. Hardware/model ownership and the SCPI parser are boundaries;
the real publication, cadence, callbacks and diagnostic serialization execute.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def diagnostic_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("origin-diagnostic")
    source = (ROOT / "middleware/scpi_port/src/scpi_calibration_commands.c").read_text(encoding="utf-8")
    callbacks = [
        ("scpi_calibration_origin_trial_flags", "scpi_t *context, uint32_t flags"),
        ("scpi_calibration_origin_trial", "scpi_t *context"),
        ("scpi_calibration_origin_trial_no_record", "scpi_t *context"),
        ("scpi_calibration_origin_trial_blackout", "scpi_t *context"),
        ("scpi_calibration_origin_trial_build_cancel", "scpi_t *context"),
        ("scpi_calibration_origin_revoke", "scpi_t *context"),
        ("scpi_calibration_origin_diagnostic_q", "scpi_t *context"),
    ]
    (build / "origin_scpi.inc").write_text("\n".join(
        f"static scpi_result_t {name}({args}) {{" + c_definition_body(source, name) + "}\n"
        for name, args in callbacks), encoding="utf-8")
    cadence = (ROOT / "components/tdma/src/tdma_origin_plan.c").read_text(encoding="utf-8")
    (build / "origin_cadence.inc").write_text(
        "static bool production_cadence(uint32_t clk_sys_hz, uint32_t baud_hz, "
        "uint32_t physical_bytes, uint32_t period_ns, tdma_origin_cadence_t *cadence) {"
        + c_definition_body(cadence, "tdma_origin_cadence_calculate") + "}\n", encoding="utf-8")
    header = (ROOT / "middleware/scpi_port/inc/scpi_calibration_commands.h").read_text(encoding="utf-8")
    pattern = r'\{\.pattern = "READ:CALibration:ORIGin:DIAGnostic\?",\s*\.callback = scpi_calibration_origin_diagnostic_q\}'
    assert len(re.findall(pattern, header)) == 1
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    executable = build / ("origin_diagnostic.exe" if os.name == "nt" else "origin_diagnostic")
    includes = ["components/tdma/inc", "components/distributed_refmem/inc",
                "components/calibration_manager/inc", "components/ota_manager/inc",
                "third_party/portable_ota/include", "config"]
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    command += ["-I" + str(ROOT / path) for path in includes] + ["-I" + str(build)]
    command += [str(ROOT / "tests/unit/test_calibration_origin_diagnostic.c"),
                str(ROOT / "third_party/portable_ota/src/pota_crc32.c"), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (build / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    (build / "compile.json").write_text(json.dumps({
        "command": command, "returncode": result.returncode,
    }, indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("case", ["oracle", "lifetime", "scpi"])
def test_production_admission_diagnostic(diagnostic_exe, case):
    command = [str(diagnostic_exe), case]
    result = subprocess.run(command, capture_output=True, text=True, timeout=10)
    (diagnostic_exe.parent / f"{case}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    (diagnostic_exe.parent / f"{case}.json").write_text(json.dumps({
        "command": command, "returncode": result.returncode,
    }, indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "passed" in result.stdout
