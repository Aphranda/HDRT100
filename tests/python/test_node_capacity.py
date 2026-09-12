"""Execute real domain boundaries and exchange persisted images between capacities."""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
CAPACITIES = (2, 3, 4, 5, 6, 7, 8)


def run(command):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


@pytest.fixture(scope="module")
def binaries(tmp_path_factory):
    directory = tmp_path_factory.mktemp("node-capacity")
    (directory / "pico").mkdir()
    (directory / "pico/unique_id.h").write_text(
        "#include <stddef.h>\nvoid pico_get_unique_board_id_string(char *, size_t);\n", encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    includes = [directory, ROOT / "config", ROOT / "drivers/mcu/flash/inc"]
    includes.append(ROOT / "third_party/portable_ota/include")
    includes += [ROOT / "components" / domain / "inc" for domain in (
        "board_identity", "tdma", "vdc_domain", "calibration_manager",
        "distributed_refmem", "ota_manager", "flash_store", "flash_transaction")]
    sources = [ROOT / "tests/unit/test_node_capacity.c"]
    sources += [ROOT / "components" / path for path in (
        "board_identity/src/board_identity.c", "tdma/src/tdma_profile.c",
        "tdma/src/tdma_ring_runtime.c", "vdc_domain/src/vdc_domain.c",
        "vdc_domain/src/vdc_timestamp.c", "calibration_manager/src/calibration_path_snapshot.c",
        "calibration_manager/src/calibration_training_store.c",
        "distributed_refmem/src/refmem_sync.c", "distributed_refmem/src/refmem_sync_frame.c")]
    sources.append(ROOT / "third_party/portable_ota/src/pota_crc32.c")
    flags = [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]
    flags += ["-I" + str(p) for p in includes]
    compiled = {}
    for capacity in CAPACITIES:
        exe = directory / f"capacity-{capacity}.exe"
        run([*flags, f"-DPROJECT_NODE_CAPACITY={capacity}", *sources, "-o", exe])
        compiled[capacity] = exe
    return directory, compiled


@pytest.mark.parametrize("capacity", CAPACITIES)
def test_local_boundaries_and_fixed_abi(binaries, capacity):
    directory, compiled = binaries
    result = json.loads(run([compiled[capacity], "write", capacity, directory / f"max-{capacity}.bin"]))
    assert result["capacity"] == result["nodes"] == capacity


@pytest.mark.parametrize("nodes", (2, 4, 6))
def test_six_eight_configuration_and_persistence_compatibility(binaries, nodes):
    directory, compiled = binaries
    outputs = []
    for capacity in (6, 8):
        path = directory / f"shared-{capacity}-{nodes}.bin"
        result = json.loads(run([compiled[capacity], "write", nodes, path]))
        outputs.append((result["schedule_crc"], result["path_crc"], path.read_bytes()))
        run([compiled[14 - capacity], "read", nodes, path])
    assert outputs[0] == outputs[1]


@pytest.mark.parametrize("nodes", (7, 8))
def test_six_rejects_larger_persisted_topology(binaries, nodes):
    directory, compiled = binaries
    path = directory / f"unsupported-{nodes}.bin"
    run([compiled[8], "write", nodes, path])
    run([compiled[6], "read", nodes, path])


@pytest.mark.parametrize("value", ("0", "1", "9", "-1", "6.5"))
def test_invalid_compile_capacity_rejected(tmp_path, value):
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    source = tmp_path / "invalid.c"
    source.write_text('#include "project_node_capacity.h"\nint capacity;\n', encoding="utf-8")
    result = subprocess.run([gcc, "-std=c11", "-I" + str(ROOT / "config"),
                             "-DPROJECT_NODE_CAPACITY=" + value, "-c", str(source),
                             "-o", str(tmp_path / "invalid.o")], capture_output=True, text=True)
    assert result.returncode != 0
    assert "PROJECT_NODE_CAPACITY" in result.stderr
