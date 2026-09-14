"""Compile and execute the complete owner-local history implementation."""
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_production_event_history_batch_lifetime_and_eviction(tmp_path: Path) -> None:
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "event_history.exe"
    commands = [
        [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-I" + str(ROOT / "components/tdma/inc"),
         str(ROOT / "components/tdma/src/tdma_event_history.c"),
         str(ROOT / "tests/unit/test_tdma_event_history.c"), "-o", str(executable)],
        [str(executable)],
    ]
    for number, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True)
        (tmp_path / f"command-{number}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        (tmp_path / f"command-{number}.json").write_text(json.dumps({
            "command": command, "returncode": result.returncode,
        }, indent=2), encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
    assert "11 production case groups passed; compact=32" in result.stdout
