"""Execute the production sequence model and compare its wire CRCs to zlib."""
import itertools
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def model(tmp_path_factory):
    directory = tmp_path_factory.mktemp("sequence-config")
    executable = directory / "sequence-config.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/sync_trigger/inc"),
               "-I" + str(ROOT / "third_party/portable_ota/include"),
               str(ROOT / "tests/unit/test_trigger_sequence_config.c"),
               str(ROOT / "components/sync_trigger/src/trigger_sequence_config.c"),
               str(ROOT / "third_party/portable_ota/src/pota_crc32.c"),
               "-o", str(executable)]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


def test_model_lifecycle_bounds_atomicity_and_freeze(model):
    result = subprocess.run([str(model)], text=True, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "atomicity passed" in result.stdout


@pytest.mark.parametrize("params", [
    (1, 0, 1, 1), (1, 1, 1, 1), (1, 2, 1, 1),
    (8, 2, 5, 1), (8, 2, 8, 2), (2, 1, 3, 4),
    (1, 0, 256, 1), (1, 1, 1, 256),
])
def test_expansion_and_canonical_crc_against_independent_oracle(model, params):
    result = subprocess.run([str(model), *map(str, params)], text=True,
                            capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    lines = result.stdout.splitlines()
    actual_map = [tuple(map(int, line.split()[1:])) for line in lines[:-1]]
    channels, polarization, frequencies, waves = params
    pols = (0, 1) if polarization == 2 else (polarization,)
    expected_map = [(index, channel, pol, pol, frequency, wave)
                    for index, (channel, pol, frequency, wave) in enumerate(
                        itertools.product(range(1, channels + 1), pols,
                                          range(frequencies), range(waves)))]
    assert actual_map == expected_map
    parameter_crc = zlib.crc32(struct.pack("<4I", *params))
    map_crc = zlib.crc32(b"".join(struct.pack("<6I", *row) for row in expected_map))
    ids = list(reversed(range(len(expected_map))))
    plan_words = [parameter_crc, map_crc, len(ids), *ids]
    sequence_crc = zlib.crc32(struct.pack(f"<{len(plan_words)}I", *plan_words))
    assert tuple(map(int, lines[-1].split()[1:])) == (parameter_crc, map_crc, sequence_crc)
