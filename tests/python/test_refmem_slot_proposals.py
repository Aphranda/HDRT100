"""Exercise production claim derivation and the legacy claim/protocol suite."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("capacity", (2, 6, 8))
def test_slot_proposal_identity_epochs_and_atomicity(tmp_path, capacity):
    _compile_and_run(tmp_path, "test_refmem_slot_proposals", capacity)


def test_legacy_slot_claim_and_protocol(tmp_path):
    _compile_and_run(tmp_path, "test_refmem_slot_claim", 8,
                     [ROOT / "components/distributed_refmem/src/refmem_claim_protocol.c"])


def _compile_and_run(directory, test_name, capacity, extra_sources=()):
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    executable = directory / (test_name + (".exe" if os.name == "nt" else ""))
    includes = [ROOT / "tests/unit/host_stubs", ROOT / "config", ROOT / "boards/rp2350_trig/inc"]
    includes += sorted((ROOT / "components").glob("*/inc"))
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={capacity}", *[f"-I{p}" for p in includes],
               str(ROOT / "tests/unit" / (test_name + ".c")),
               str(ROOT / "components/distributed_refmem/src/refmem_slot_claim.c"),
               *map(str, extra_sources), "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
