"""Run the RefMem C host-unit runners that are not pytest-native.

The repository keeps most RefMem host coverage in PowerShell runners under
``tools/tests``.  ``host_acceptance.py`` only executes pytest paths, so this
wrapper makes the two new (2026-10-29) runners part of the host gate instead of
relying on someone remembering to call ``run_refmem_all_host_tests.ps1``.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
RUNNERS = (
    "run_refmem_command_aggregate_tests.ps1",
    "run_refmem_position_reset_policy_tests.ps1",
)


@pytest.mark.parametrize("runner", RUNNERS)
def test_refmem_host_runner(runner: str) -> None:
    if sys.platform != "win32":
        pytest.skip("PowerShell host runners are Windows-only")
    script = ROOT / "tools" / "tests" / runner
    assert script.is_file(), f"missing runner: {script}"
    completed = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(script),
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=300,
        check=False,
    )
    assert completed.returncode == 0, (
        f"{runner} failed\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
    )
