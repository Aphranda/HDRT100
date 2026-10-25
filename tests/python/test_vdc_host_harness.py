"""Host harness: build the real VDC units for the developer machine and run
the scenarios. Uses the Pico SDK host platform (PICO_PLATFORM=host) plus
link-time doubles, so production code is compiled unmodified.
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tests/harness"
BUILD = ROOT / "out/host_harness"


def _tool(name: str) -> str | None:
    return os.environ.get(name.upper()) or shutil.which(name)


@pytest.fixture(scope="module")
def harness_build() -> Path:
    cmake = _tool("cmake")
    ninja = _tool("ninja")
    if not cmake or not ninja:
        pytest.skip("cmake/ninja not available")

    BUILD.mkdir(parents=True, exist_ok=True)
    configure = subprocess.run(
        [cmake, "-S", str(HARNESS), "-B", str(BUILD), "-G", "Ninja"],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    assert configure.returncode == 0, configure.stdout + configure.stderr

    build = subprocess.run(
        [cmake, "--build", str(BUILD)],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    assert build.returncode == 0, build.stdout + build.stderr
    return BUILD


@pytest.mark.parametrize("scenario,expected", [
    ("host_control_test", "host-control scenario: OK"),
    ("output_delay_test", "output-delay scenario: OK"),
])
def test_host_scenario(harness_build: Path, scenario: str, expected: str) -> None:
    exe = harness_build / (scenario + (".exe" if os.name == "nt" else ""))
    assert exe.is_file(), f"missing {exe}"
    run = subprocess.run([str(exe)], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    assert run.returncode == 0, run.stdout + run.stderr
    assert expected in run.stdout
