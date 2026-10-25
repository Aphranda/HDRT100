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
    # The origin-timing fragment is a real .c with its own include prologue;
    # this harness supplies that context itself, so inject the body only.
    unit = (ROOT / "components/calibration_manager/src/calibration_origin_timing.c"
            ).read_text(encoding="utf-8")
    body = unit.splitlines(keepends=True)
    while body and (not body[0].strip() or body[0].lstrip().startswith("#include")):
        body.pop(0)
    (build / "origin_timing_body.inc").write_text("".join(body), encoding="utf-8")
    # Same for the TDMA origin unit: the harness defines its own owner state,
    # so it takes the body only (the RAM placement macro is a no-op on host).
    unit = (ROOT / "components/tdma/src/tdma_runtime_origin.c").read_text(encoding="utf-8")
    origin_body = ("#define TDMA_ORIGIN_OWNER_RAM\n"
                   + unit[unit.index("/* The sole TDMA owner consumes"):])
    (build / "runtime_origin_body.inc").write_text(origin_body, encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler
    exe = build / ("admission.exe" if os.name == "nt" else "admission")
    includes = ["components/tdma/inc", "components/distributed_refmem/inc",
                # distributed_refmem.h includes trigger_position_descriptor.h
                "components/sync_trigger/inc",
                "components/calibration_manager/inc", "components/ota_manager/inc", "third_party/portable_ota/include", "config"]
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
    wrapper = (ROOT / 'components/vdc_dpll_manager/src/vdc_dpll_manager.c').read_text(encoding='utf-8')
    (build / 'tdma_component_service.inc').write_text('void tdma_component_core1_service(void) {' +
        c_definition_body(wrapper, 'tdma_component_core1_service') + '}\n', encoding='utf-8')
    command += ['-I' + str(build)]
    scpi_source = (ROOT / 'middleware/scpi_port/src/scpi_calibration_commands.c').read_text(encoding='utf-8')
    callbacks = [
        ('scpi_calibration_origin_trial_flags', 'scpi_t *context, uint32_t flags'),
        ('scpi_calibration_origin_trial_ready', 'scpi_t *context'),
        ('scpi_calibration_origin_release', 'scpi_t *context'),
        ('scpi_calibration_origin_release_q', 'scpi_t *context'),
    ]
    (build / 'origin_release_scpi.inc').write_text('\n'.join(
        f'static scpi_result_t {name}({args}) {{' + c_definition_body(scpi_source, name) + '}\n'
        for name, args in callbacks), encoding='utf-8')
    command += [str(ROOT / "tests/unit/test_tdma_origin_admission.c"),
                str(ROOT / 'components/tdma/src/tdma_origin_handoff.c'),
                str(ROOT / 'components/tdma/src/tdma_origin_calibration_crc.c'),
                str(ROOT / 'third_party/portable_ota/src/pota_crc32.c'),
                str(ROOT / 'components/tdma/src/tdma_origin_blackout.c'), str(cadence), "-o", str(exe)]
    subprocess.run(command, check=True, timeout=60)
    return exe


@pytest.mark.parametrize("case", ["publish", "stale", "expiry", "continuous", "prepare", "fault", "record-mode",
    'build-cancel', 'handoff', 'batch', 'batch-yield', 'batch-revoke', 'batch-failure',
    'blackout', 'blackout-cancel', 'blackout-deadline', 'blackout-invalid',
    'release', 'release-cancel', 'release-expiry', 'release-exhaustion', 'release-scpi', 'release-mixed',
    'release-stop-race', 'release-physical-reject', 'begin-reject'])
def test_origin_trial_lifecycle(admission_exe, case):
    result = subprocess.run([str(admission_exe), case], capture_output=True,
                            text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr
