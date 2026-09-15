"""Execute production frozen geometry and adapter lifecycle with hardware seams."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def geometry_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("tdma-frozen-geometry")
    physical = ROOT / "components/tdma/src/tdma_pio_spi_phys.c"
    geometry = ROOT / "components/tdma/src/tdma_pio_spi_phys_geometry.inc"
    source = physical.read_text(encoding="utf-8")
    geometry_bytes = geometry.read_bytes()
    (build / geometry.name).write_bytes(geometry_bytes)
    extracted = {}
    for name, signature, filename in (
        ("tdma_pio_spi_phys_disarm", "bool tdma_pio_spi_phys_disarm(void *context)", "geometry_physical_stop.inc"),
        ("tdma_pio_spi_phys_arm_reject",
         "static bool tdma_pio_spi_phys_arm_reject(tdma_pio_spi_phys_t *phys, tdma_pio_spi_phys_error_t error)",
         "geometry_arm_reject.inc"),
    ):
        text = signature + " {\n" + c_definition_body(source, name) + "\n}\n"
        (build / filename).write_text(text, encoding="utf-8")
        extracted[filename] = hashlib.sha256(text.encode("utf-8")).hexdigest()
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "A host C compiler is required"
    exe = build / ("geometry.exe" if os.name == "nt" else "geometry")
    names = (
        "tdma_pio_spi_ring_adapter", "tdma_adapter_comm_fsm", "tdma_flight_fifo",
        "tdma_flight_engine", "tdma_flight_overlay", "tdma_overlay_prepare",
        "tdma_rx_prepare", "tdma_rx_scan", "tdma_receive_health", "tdma_process_image_map",
        "tdma_ring_runtime", "tdma_transport_frame", "tdma_profile", "tdma_rx_sequence",
    )
    includes = (
        "tests/unit/host_stubs", "boards/rp2350_trig/inc", "components/tdma/inc",
        "components/tdma/src", "components/vdc_domain/inc", "components/resource_arbiter/inc",
    )
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-DPROJECT_NODE_CAPACITY=6", "-I" + str(build)]
    command += ["-I" + str(ROOT / p) for p in includes]
    command += [str(ROOT / "tests/support/tdma_geometry_lifecycle.c")]
    command += [str(ROOT / f"components/tdma/src/{name}.c") for name in names]
    command += ["-o", str(exe)]
    observed = [physical, geometry,
                ROOT / "components/tdma/src/tdma_pio_spi_ring_adapter.c"]
    before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in observed}
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (build / "compile.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (build / "compile.stderr.txt").write_text(result.stderr, encoding="utf-8")
    (build / "source-observations.json").write_text(json.dumps({
        "command": command, "extracted_sha256": extracted,
        "compiled_geometry_sha256": hashlib.sha256(geometry_bytes).hexdigest(),
        "sources": [{"observed_path": p.relative_to(ROOT).as_posix(),
                     "observed_sha256": before[p],
                     "after_compile_sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in observed],
        "scope": "complete geometry.inc; complete adapter.c; verbatim disarm/arm_reject function bodies; actual worker cancellation code; hardware ARM reset seam"
    }, indent=2) + "\n", encoding="utf-8")
    assert all(before[p] == hashlib.sha256(p.read_bytes()).hexdigest() for p in observed), \
        "Production source changed during host compilation; rerun with a stable source"
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


CASES = [
    "cancel_ack", "select", "idle_before_training", "missing", "wrong_generation", "same_config", "zero_config",
    "adapter_early", "physical_early", "physical_late", "persona", "clock_training",
    "clock_binding", "clock_stopped", "arm_epoch", "observation_epoch", "untrained",
    "bit_shift", "byte_shift", "generation_exhaustion", "publication_exhaustion",
    "dma_stop", "resource_release", "reader", "request_seq_changed", "reused_arm_epoch",
    *[f"key_{field}" for field in range(29)],
]


@pytest.mark.parametrize("case", CASES)
def test_frozen_geometry_requires_retirement_and_exact_selection(geometry_exe, case):
    result = subprocess.run([str(geometry_exe), case], capture_output=True,
                            text=True, timeout=3)
    (geometry_exe.parent / f"{case}.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (geometry_exe.parent / f"{case}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
