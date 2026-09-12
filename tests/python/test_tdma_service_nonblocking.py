"""Execute the real TDMA service with a frozen clock and interrupted writers."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def service_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("tdma-service-nonblocking")
    pico = build / "pico"
    pico.mkdir()
    (pico / "time.h").write_text(
        "#include <stdint.h>\nuint64_t time_us_64(void);\n", encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    sources = [ROOT / "tests/unit/test_tdma_service_nonblocking.c"]
    sources += [ROOT / f"components/tdma/src/{name}.c" for name in (
        "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map",
        "tdma_ring_runtime", "tdma_traffic_scheduler", "tdma_service_timing")]
    exe = build / ("service.exe" if os.name == "nt" else "service")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DTDMA_SERVICE_TIMING_ENABLED=1",
                    "-I" + str(build), "-I" + str(ROOT / "components/tdma/inc"),
                    "-I" + str(ROOT / "components/vdc_domain/inc"),
                    *map(str, sources), "-o", str(exe)], check=True, timeout=60)
    return exe


@pytest.mark.parametrize("case", ["writer", "changed", "window", "miss", "abort", "resident"])
def test_service_yields_without_losing_intent(service_exe, case):
    result = subprocess.run([str(service_exe), case], capture_output=True,
                            text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr
