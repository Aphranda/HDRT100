"""Independent schema4 wire oracle; absolute age and RATE coordinate are distinct."""
import random
import struct
import subprocess
import zlib

import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_refmem_vdc_model_feedback import HARNESS

U64 = (1 << 64) - 1
FIELDS = "schema source target flags epoch run arm observer sequence hz absolute coordinate token applied session".split()
DEFAULT = dict(zip(FIELDS, [4, 1, 0, 31, 3, 4, 55, 8, 100, 250_000_000,
                           99_000_000_000, 4_000_000_000, 7, 2, 123], strict=True))


def wire(**changes):
    values = DEFAULT | changes
    body = struct.pack("<BBBBIIQIIIQQIII", *(values[k] for k in FIELDS))
    return body + struct.pack("<I", zlib.crc32(body))


@pytest.fixture(scope="module")
def rate_codec(tmp_path_factory):
    harness = HARNESS.replace("output.schema_version==2", "output.schema_version==4")
    return compile_executable(tmp_path_factory.mktemp("rate-codec"), "rate_codec", harness,
                              [ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"])


def run(executable, *args):
    result = subprocess.run([str(executable), *map(str, args)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def test_rate_roundtrip_keeps_absolute_and_coordinate_independent(rate_codec):
    rng = random.Random(16091641)
    cases = [{}, {"absolute": U64, "coordinate": 0}, {"absolute": 0, "coordinate": U64 - 1}]
    cases += [{"absolute": rng.getrandbits(64), "coordinate": rng.randrange(U64),
               "token": rng.randint(1, 0xFFFFFFFF), "applied": rng.getrandbits(32)} for _ in range(50)]
    for changes in cases:
        row = DEFAULT | changes
        assert list(map(int, run(rate_codec, "decode", wire(**changes).hex()).split(','))) == [row[k] for k in FIELDS]


@pytest.mark.parametrize("changes", [{"flags": 15}, {"flags": 0}, {"schema": 2},
    {"coordinate": U64}, {"token": 0}, {"session": 0}, {"arm": 0}, {"observer": 0}, {"hz": 0}])
def test_rate_invalid_crc_valid_records_preserve_output(rate_codec, changes):
    assert run(rate_codec, "decode", wire(**changes).hex()) == "INVALID"


@pytest.mark.parametrize("changes,expected", [({}, 3), ({"sequence": 101}, 2),
    ({"coordinate": 0}, 5), ({"absolute": 0}, 5), ({"sequence": 99}, 4),
    ({"sequence": 101, "token": 6}, 4), ({"sequence": 101, "applied": 1}, 4),
    ({"sequence": 1, "session": 124}, 1), ({"sequence": 1, "observer": 9}, 1),
    ({"sequence": 101, "hz": 125_000_000}, 5)])
def test_rate_ordering_and_namespace(rate_codec, changes, expected):
    assert int(run(rate_codec, "compare", wire(**changes).hex(), wire().hex())) == expected


@pytest.mark.parametrize("mode", ["complete", "duplicate", "expired", "reordered", "conflict", "schema_restart", "bad_record"])
def test_rate_assembly_is_bounded_and_typed(rate_codec, mode):
    assert run(rate_codec, "assembly", mode, wire().hex()) == "assembly passed"
