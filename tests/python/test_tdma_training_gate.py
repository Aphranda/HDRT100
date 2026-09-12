"""Run production training admission/owner code at forced cross-core boundaries."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def _function(path, signature):
    source = path.read_text(encoding="utf-8")
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@pytest.fixture(scope="module")
def training_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("tdma-training-gate")
    service = _function(ROOT / "components/tdma/src/tdma_service.c",
                        "bool tdma_service_ring_train_clock(")
    (build / "training_service.inc").write_text(service, encoding="utf-8")
    owner = ROOT / "components/tdma/src/tdma_runtime_owner.c"
    functions = [_function(owner, signature) for signature in (
        "static bool tdma_runtime_owner_submit_training(",
        "bool tdma_runtime_owner_train_clock(",
        "void tdma_runtime_owner_update_training_gate(")]
    (build / "training_owner.inc").write_text("\n\n".join(functions), encoding="utf-8")
    physical = _function(ROOT / "components/tdma/src/tdma_pio_spi_phys.c",
                         "bool tdma_pio_spi_phys_clk_train_terminal_core1(")
    (build / "training_physical.inc").write_text(physical, encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    exe = build / ("training.exe" if os.name == "nt" else "training")
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-DPROJECT_NODE_CAPACITY=6", "-I" + str(build)]
    command += ["-I" + str(ROOT / include) for include in (
        "tests/unit/host_stubs", "boards/rp2350_trig/inc", "components/tdma/inc",
        "components/vdc_domain/inc", "components/resource_arbiter/inc", "osal/inc")]
    command += [str(ROOT / "tests/unit/test_tdma_training_gate.c"),
                str(ROOT / "components/tdma/src/tdma_ring_runtime.c"), "-o", str(exe)]
    subprocess.run(command, check=True, timeout=60)
    return exe


@pytest.mark.parametrize("case", [
    "idle", "stale", "publication", "completion_during_submit", "reject_previous",
    "stop_pending", "reset_pending", "wrap", "guard_wrap", "compatibility", "ota_flash",
    "repeat_active", "unreadable", "physical_reject", "physical_terminal", "config_busy",
    "flash_after_policy",
])
def test_training_reservation_survives_interleavings(training_exe, case):
    result = subprocess.run([str(training_exe), case], capture_output=True,
                            text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr
