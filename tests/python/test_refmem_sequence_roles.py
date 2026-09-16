"""Compile and execute the actual pure RefMem sequence-role resolver."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("node_capacity", (2, 6, 8))
def test_role_identity_claims_wiring_and_failure_atomicity(tmp_path, node_capacity):
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    executable = tmp_path / ("roles.exe" if os.name == "nt" else "roles")
    includes = [ROOT / "tests/unit/host_stubs", ROOT / "config",
                ROOT / "boards/rp2350_trig/inc"]
    includes += sorted((ROOT / "components").glob("*/inc"))
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={node_capacity}",
               *[f"-I{path}" for path in includes],
               str(ROOT / "tests/unit/test_refmem_sequence_roles.c"),
               str(ROOT / "components/distributed_refmem/src/refmem_sequence_roles.c"),
               "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "role identity, claims, wiring and failure atomicity passed" in run.stdout
