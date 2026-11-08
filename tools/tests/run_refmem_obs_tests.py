"""Host test for the REFMEM observation encoder (REFTMEM-READ-01 / R1-4)."""
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def check():
    import subprocess
    import tempfile

    cc = "gcc"
    src = REPO / "components/distributed_refmem/src/refmem_obs.c"
    table = REPO / "components/distributed_refmem/src/refmem_read_catalog_table.c"
    inc = REPO / "components/distributed_refmem/inc"
    test = REPO / "tests/unit/test_refmem_obs.c"
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / ("test_refmem_obs.exe" if sys.platform == "win32"
                           else "test_refmem_obs")
        build = subprocess.run(
            [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(inc),
             str(test), str(src), str(table), "-o", str(exe)],
            capture_output=True, text=True)
        if build.returncode != 0:
            print(build.stderr)
            return 1
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        print(run.stdout.strip())
        if run.returncode != 0:
            print(run.stderr)
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(check())
