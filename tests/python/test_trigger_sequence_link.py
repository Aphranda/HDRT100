"""Exercise the production RJ45 role FSM and codec with owner doubles."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def link_executable(tmp_path_factory):
    path = tmp_path_factory.mktemp("sequence-link")
    (path / "tdma_runtime_owner.h").write_text(
        "#ifndef TEST_TDMA_OWNER_H\n#define TEST_TDMA_OWNER_H\n"
        "#include <stdbool.h>\n#include <stdint.h>\n"
        "#include \"tdma_ring_runtime.h\"\n"
        "bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out);\n"
        "bool tdma_runtime_owner_set_local_return_delivery(bool enabled);\n#endif\n", encoding="utf-8")
    executable = path / "sequence-link.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    includes = [path, ROOT / "osal/inc"]
    includes.extend(sorted((ROOT / "components").glob("*/inc")))
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
    command.extend("-I" + str(inc) for inc in includes)
    command.extend([str(ROOT / "components/sync_trigger/src/trigger_sequence_link.c"),
                    str(ROOT / "components/sync_trigger/src/trigger_sequence_link_protocol.c"),
                    str(ROOT / "tests/unit/test_trigger_sequence_link.c"), "-o", str(executable)])
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


@pytest.mark.parametrize("case", ["workflow", "stale", "stop", "pause", "timeout", "model", "config", "rollback", "first_settle", "once", "twice", "continuous", "start_view", "start_publication"])
def test_sequence_link_orchestrator(link_executable, case):
    result = subprocess.run([str(link_executable), case], text=True, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "sequence link orchestrator passed" in result.stdout
