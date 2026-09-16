"""Compile and exercise the actual portable role-message codec."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_sequence_link_codec(tmp_path):
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    executable = tmp_path / "sequence-link-codec.exe"
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/sync_trigger/inc"),
               str(ROOT / "components/sync_trigger/src/trigger_sequence_link_protocol.c"),
               str(ROOT / "tests/unit/test_trigger_sequence_link_protocol.c"),
               "-o", str(executable)]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "sequence link codec passed" in result.stdout
