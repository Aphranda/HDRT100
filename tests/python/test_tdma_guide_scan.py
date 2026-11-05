"""Compile and run the P0T pair-scan model host cases."""
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_guide_scan_model(tmp_path: Path) -> None:
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "guide_scan.exe"
    commands = [
        [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-I" + str(ROOT / "components/tdma/inc"),
         str(ROOT / "components/tdma/src/tdma_guide_scan.c"),
         str(ROOT / "tests/unit/test_tdma_guide_scan.c"), "-o", str(executable)],
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
    assert "tdma guide scan: 7 case groups passed" in result.stdout
