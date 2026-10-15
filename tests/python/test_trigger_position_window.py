"""Host cases for the T2 position-instance window (Step 1 of T2-DEPLOY-003).

The module is header-only on purpose: it is the abstraction the source and the
followers will share, so it must build and behave on the host before either side
is switched onto it.
"""
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INCLUDE = ROOT / "components/sync_trigger/inc"
CASE = ROOT / "tests/unit/test_trigger_position_window.c"


def test_position_window_host_cases(tmp_path: Path) -> None:
    cc = shutil.which("gcc")
    assert cc, "host gcc is required for the position window cases"
    executable = tmp_path / "trigger_position_window.exe"
    completed = subprocess.run(
        [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-I" + str(INCLUDE), str(CASE), "-o", str(executable)],
        capture_output=True, text=True,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "6 production C case groups passed" in run.stdout
