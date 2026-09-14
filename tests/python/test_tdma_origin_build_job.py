"""Exercise the real cross-core construction lease at interrupted writes."""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_origin_build_job_ownership(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler
    exe = tmp_path / ("origin-job.exe" if os.name == "nt" else "origin-job")
    subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "components/tdma/inc"),
                    str(ROOT / "tests/unit/test_tdma_origin_build_job.c"),
                    str(ROOT / "components/tdma/src/tdma_origin_build_job.c"),
                    "-o", str(exe)], check=True, capture_output=True, text=True, timeout=60)
    subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=5)


@pytest.mark.parametrize("capacity", (2, 6, 8))
def test_origin_build_job_actual_graph(tmp_path, capacity):
    sdk = os.environ.get("PICO_SDK_PATH")
    candidates = [Path(sdk)] if sdk else sorted((Path.home()/".pico-sdk/sdk").glob("*"))
    includes = [p/"src/rp2350/hardware_regs/include" for p in candidates]
    registers = next((p for p in reversed(includes) if (p/"hardware/regs/dma.h").is_file()), None)
    if registers is None:
        pytest.skip("PICO_SDK_PATH with RP2350 register declarations is required")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler
    exe = tmp_path / ("origin-job-graph.exe" if os.name == "nt" else "origin-job-graph")
    common = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    f"-DPROJECT_NODE_CAPACITY={capacity}",
                    "-I" + str(ROOT/"components/tdma/inc"), "-I" + str(registers)]
    plan_object = tmp_path / "origin-plan.o"
    subprocess.run([*common, "-Dtdma_origin_plan_step=tdma_origin_plan_step_actual", "-c",
                    str(ROOT/"components/tdma/src/tdma_origin_plan.c"), "-o", str(plan_object)],
                   check=True, capture_output=True, text=True, timeout=60)
    subprocess.run([*common,
                    str(ROOT/"tests/unit/tdma_origin_build_graph_cases.c"),
                    str(ROOT/"components/tdma/src/tdma_origin_build_job.c"),
                    str(plan_object),
                    str(ROOT/"components/tdma/src/tdma_origin_exchange.c"),
                    str(ROOT/"components/tdma/src/tdma_transport_frame.c"),
                    str(ROOT/"components/tdma/src/tdma_receive_health.c"),
                    "-o", str(exe)], check=True, capture_output=True, text=True, timeout=60)
    result = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=5)
    report = json.loads(result.stdout)
    assert report["graph_pairs"] == 2 * (capacity - 1)
    assert report["cancellation_cases"] > 2 * report["graph_pairs"]
    assert report["probe_cases"] == 5 * report["graph_pairs"]
    (tmp_path / "cancellation-summary.json").write_text(result.stdout, encoding="utf-8")
