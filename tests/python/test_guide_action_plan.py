"""Compile and run the guide action plan payload host cases."""
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_guide_action_plan(tmp_path: Path) -> None:
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "guide_action_plan.exe"
    commands = [
        [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-I" + str(ROOT / "components/calibration_manager/inc"),
         str(ROOT / "components/calibration_manager/src/guide_action_plan.c"),
         str(ROOT / "tests/unit/test_guide_action_plan.c"), "-o", str(executable)],
        [str(executable)],
    ]
    result = None
    for number, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True)
        (tmp_path / f"command-{number}.log").write_text(
            result.stdout + result.stderr, encoding="utf-8")
        (tmp_path / f"command-{number}.json").write_text(json.dumps({
            "command": command, "returncode": result.returncode,
        }, indent=2), encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
    assert result is not None
