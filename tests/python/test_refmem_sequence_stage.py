"""Exercise real RMTP staging/validation/activation for DUT and VNA roles."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_real_sequence_role_transaction(tmp_path):
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    executable = tmp_path / "sequence-stage.exe"
    includes = [ROOT / "tests/unit/host_stubs", ROOT / "config",
                ROOT / "boards/rp2350_trig/inc", ROOT / "third_party/portable_ota/include"]
    includes += sorted((ROOT / "components").glob("*/inc"))
    sources = ["tests/unit/test_refmem_sequence_stage.c",
               "components/distributed_refmem/src/refmem_application_model.c",
               "components/distributed_refmem/src/refmem_application_contract.c",
               "components/distributed_refmem/src/refmem_table_registry.c",
               "components/distributed_refmem/src/refmem_realtime_contract.c",
               "components/distributed_refmem/src/refmem_slot_claim.c",
               "components/tdma/src/tdma_profile.c",
               "third_party/portable_ota/src/pota_crc32.c"]
    command = [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
               *[f"-I{path}" for path in includes],
               *[str(ROOT / path) for path in sources], "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "real sequence role staging, activation and rollback passed" in run.stdout
