"""Run the real sequence service against deterministic physical-event doubles."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_sequence_service_lifecycle(tmp_path):
    (tmp_path / "sync_trigger.h").write_text(
        "#include <stdbool.h>\nbool sync_trigger_sequence_can_start(void);\n",
        encoding="utf-8")
    executable = tmp_path / "sequence-service.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(tmp_path),
               "-I" + str(ROOT / "components/sync_trigger/inc"),
               "-I" + str(ROOT / "components/sync_io/inc"),
               "-I" + str(ROOT / "osal/inc"),
               "-I" + str(ROOT / "third_party/portable_ota/include"),
               str(ROOT / "tests/unit/test_trigger_sequence_service.c"),
               str(ROOT / "components/sync_trigger/src/trigger_sequence_config.c"),
               str(ROOT / "third_party/portable_ota/src/pota_crc32.c"),
               "-o", str(executable)]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "sequence service lifecycle passed" in result.stdout
