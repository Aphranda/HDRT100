"""Production projection: retain legacy bytes after the explicit LOCKED fix.

The native harness validates current payload CRCs and rejects unqualified lock,
then normalizes only that historical flag for the unchanged legacy-byte golden.
Healthy/aging/recovery semantics run through test_vdc_publication_generation.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def _function(source, name):
    start = source.index(name + ")(")
    start = source.rfind("\n", 0, source.rfind("\n", 0, start)) + 1
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@pytest.fixture(scope="module", params=(2, 6, 8))
def projection_exe(request, tmp_path_factory):
    directory = tmp_path_factory.mktemp(f"refmem-vector-{request.param}")
    manager = (ROOT/"components/vdc_dpll_manager/src/vdc_dpll_manager.c").read_text(encoding="utf-8")
    refmem = (ROOT/"components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    functions = _function(manager, "vdc_dpll_manager_get_vector_snapshot")
    functions += "\n#undef __atomic_load_n\n"
    for name in ["distributed_refmem_vector_hardware_evidence_valid",
                 "distributed_refmem_vector_flags",
                 "distributed_refmem_fill_vdc_vector_payload",
                 "distributed_refmem_fill_dpll_vector_payload"]:
        functions += "\n" + _function(refmem, name)
    (directory/"projection.inc").write_text(functions, encoding="utf-8")
    includes = [directory, ROOT/"tests/unit/host_stubs", ROOT/"config", ROOT/"boards/rp2350_trig/inc"]
    includes += sorted((ROOT/"components").glob("*/inc"))
    exe = directory/("projection.exe" if os.name == "nt" else "projection")
    command = [shutil.which("gcc") or shutil.which("clang"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={request.param}", *[f"-I{p}" for p in includes],
               str(ROOT/"tests/unit/test_refmem_vector_projection.c"),
               str(ROOT/"components/distributed_refmem/src/refmem_vector_table.c"), "-o", str(exe)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return exe, request.param


def test_legacy_vector_bytes_and_snapshot_rejection(projection_exe):
    exe, capacity = projection_exe
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    result = json.loads(run.stdout)
    golden = json.loads((ROOT/"tests/unit/refmem_vector_projection_golden.json").read_text(encoding="utf-8"))
    assert result["vectors"] == golden[str(capacity)]
    assert result["rejection_checks"] == 5
    assert result["projected_bytes"] < result["domain_bytes"]
