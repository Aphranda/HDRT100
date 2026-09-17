"""Execute production RefMem geometry and serialized-package owner paths."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]


def _run(command, directory, label):
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    (directory / f"{label}.json").write_text(json.dumps({
        "command": command, "returncode": result.returncode,
        "stdout": result.stdout, "stderr": result.stderr,
    }, indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def _compile(directory, name, sources, node_capacity, short_enums):
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert gcc, "host C compiler required"
    includes = [ROOT / "components/distributed_refmem/inc", ROOT / "components/tdma/inc",
                ROOT / "components/ota_manager/inc"]
    exe = directory / (name + (".exe" if os.name == "nt" else ""))
    command = [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={node_capacity}"]
    if short_enums:
        command.append("-fshort-enums")
    command += [f"-I{path}" for path in includes]
    command += [str(ROOT / path) for path in sources] + ["-o", str(exe)]
    _run(command, directory, "compile")
    return exe


@pytest.mark.parametrize("node_capacity", [2, 6, 8])
@pytest.mark.parametrize("short_enums", [False, True])
def test_live_vector_geometry_and_boundaries(tmp_path, node_capacity, short_enums):
    exe = _compile(tmp_path, "vector_layout", [
        "tests/unit/test_refmem_vector_table.c",
        "components/distributed_refmem/src/refmem_vector_table.c",
    ], node_capacity, short_enums)
    result = json.loads(_run([str(exe)], tmp_path, "run"))
    assert result["table_bytes"] == 18432
    assert result["node_bytes"] == 128
    assert result["header_reserved"] > 0
    assert result["vdc_payload"] < 1024 and result["vdc_reserved"] > 0
    assert result["dpll_payload"] < 1024 and result["dpll_reserved"] > 0


@pytest.mark.parametrize("node_capacity", [6, 8])
@pytest.mark.parametrize("short_enums", [False, True])
def test_tool_package_owner_validation_and_lifetime(tmp_path, node_capacity, short_enums):
    exe = _compile(tmp_path, "registry_layout", [
        "tests/unit/test_refmem_table_registry.c",
        "components/distributed_refmem/src/refmem_application_model.c",
        "components/distributed_refmem/src/refmem_table_registry.c",
        "components/distributed_refmem/src/refmem_application_contract.c",
        "components/distributed_refmem/src/refmem_realtime_contract.c",
        "components/distributed_refmem/src/refmem_slot_claim.c",
        "components/tdma/src/tdma_profile.c",
    ], node_capacity, short_enums)
    _run([sys.executable, str(ROOT / "tools/refmem_pack_build/refmem_pack_build.py"),
          "--output-dir", str(tmp_path), "--tdma-node-count", str(node_capacity)],
         tmp_path, "pack-cli")
    path = tmp_path / "refmem/app_model.rmtp"
    manifest = json.loads((tmp_path / "refmem/app_model.json").read_text(encoding="utf-8"))
    assert manifest["tdma_node_count"] == node_capacity
    assert manifest["layout_version"] == 2 and manifest["format_version"] == 1
    result = _run([str(exe), str(path)], tmp_path, "run")
    # Both the established C fixture and the actual Python serializer reach
    # the same production owner validation, activation and rejection paths.
    assert result.count("serialized layout lifecycle passed:") == 2
    assert "refmem_table_registry tests passed" in result
