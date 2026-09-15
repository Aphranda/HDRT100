"""Drive real libscpi streams through production configuration callbacks/model."""
import csv
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
def parser(tmp_path_factory):
    directory = tmp_path_factory.mktemp("sequence-scpi")
    executable = directory / "sequence-scpi.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    library = ROOT / "third_party/scpi-parser/libscpi"
    includes = [library / "inc", ROOT / "middleware/scpi_port/inc",
                ROOT / "components/sync_trigger/inc", ROOT / "components/sync_io/inc",
                ROOT / "components/distributed_config/inc", ROOT / "boards/rp2350_trig/inc",
                ROOT / "tests/unit/host_stubs", ROOT / "third_party/portable_ota/include"]
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-DSCPI_USER_CONFIG=1", *("-I" + str(path) for path in includes),
               str(ROOT / "tests/unit/test_sequence_scpi_config.c"),
               str(ROOT / "middleware/scpi_port/src/scpi_config_commands.c"),
               str(ROOT / "components/sync_trigger/src/trigger_sequence_config.c"),
               str(ROOT / "third_party/portable_ota/src/pota_crc32.c"),
               *(str(path) for path in sorted((library / "src").glob("*.c"))),
               *( ["-Wno-error=attributes"] if os.name == "nt" else ["-lm"] ),
               "-o", str(executable)]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


def run(parser, commands):
    result = subprocess.run([str(parser)], input="\n".join(commands) + "\n",
                            text=True, capture_output=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    rows = []
    for line in result.stdout.splitlines():
        errors, unchanged, generation, active, response = line.split("|", 4)
        rows.append(dict(errors=int(errors), unchanged=bool(int(unchanged)),
                         generation=int(generation), active=int(active),
                         fields=next(csv.reader([response]))))
    assert len(rows) == sum(not command.startswith("@") for command in commands)
    return rows


def crc_values(params, ids):
    chan, pol, freq, wave = params
    pols = (0, 1) if pol == 2 else (pol,)
    mapping = [(index, channel, polarization, polarization, frequency, wave_index)
               for index, (channel, polarization, frequency, wave_index) in enumerate(
                   itertools.product(range(1, chan + 1), pols, range(freq), range(wave)))]
    param_crc = zlib.crc32(struct.pack("<4I", *params))
    map_crc = zlib.crc32(b"".join(struct.pack("<6I", *row) for row in mapping))
    words = [param_crc, map_crc, len(ids), *ids]
    seq_crc = zlib.crc32(struct.pack(f"<{len(words)}I", *words))
    return param_crc, map_crc, seq_crc, mapping


def test_queries_are_real_and_check_is_pure(parser):
    params, ids = (8, 2, 5, 1), [0, 5, 79, 5]
    pcrc, mcrc, scrc, mapping = crc_values(params, ids)
    rows = run(parser, [
        "READ:TRIG:PAR?", "READ:SEQ:ACT?", "CONF:TRIG 8,2,5,1",
        "CONFigure:SEQuence plan_a,0,5,79,5", "READ:TRIG:PARameter?",
        "READ:SEQ? plan_a", "READ:SEQ? PLAN_A,2", "READ:SEQ:CHECK? plan_a",
        "READ:SEQ:ACT?", "CONF:SEQ:ACT plan_a", "READ:SEQ?",
        "READ:SEQ:MAP?", "READ:SEQ:CHECK?", "READ:SEQ:ACT?",
    ])
    assert all(row["errors"] == 0 for row in rows)
    assert rows[0]["fields"] == ["TRIGGER", "0", "1", "0", "0", "0", "0", "0", "0", "0", "0", "0"]
    assert rows[1]["fields"] == ["NONE", "0", "0", "0", "NONE", "NO_ACTIVE_PLAN"]
    assert rows[4]["fields"] == list(map(str, ["TRIGGER", pcrc, 1, 1, 1, 0, *params, 80, mcrc]))
    assert rows[5]["fields"] == list(map(str, ["PLAN_A", 0xFFFFFFFF, scrc, 4, 0, *ids]))
    assert rows[6]["fields"] == list(map(str, ["PLAN_A", 2, scrc, 4, 1, 79]))
    assert rows[7]["fields"] == list(map(str, ["PLAN_A", 4, scrc, 1, 1, 1, 1, 1, 1, 1, 1, "NONE"]))
    assert rows[8]["fields"] == rows[1]["fields"]
    assert rows[10]["fields"] == rows[5]["fields"]
    assert rows[11]["fields"] == list(map(str, itertools.chain.from_iterable(mapping[i] for i in ids)))
    assert rows[12]["fields"] == rows[7]["fields"]
    assert rows[13]["fields"] == list(map(str, ["PLAN_A", scrc, 4, 1, "PASS", "NONE"]))
    for index in [0, 1, 4, 5, 6, 7, 8, 10, 11, 12, 13]:
        assert rows[index]["unchanged"]


@pytest.mark.parametrize("bad", [
    "CONF:TRIG", "CONF:TRIG 1,0,1", "CONF:TRIG 1,0,1,1,2",
    "CONF:TRIG 1,0,1,1,", "CONF:TRIG 1,,1,1", "CONF:TRIG 9,0,1,1",
    "CONF:TRIG 1,3,1,1", "CONF:TRIG 1,0,0,1", "CONF:TRIG -4294967295,0,1,1",
    "CONF:TRIG 4294967297,0,1,1", "CONF:TRIG 1.0,0,1,1", "CONF:TRIG 1e0,0,1,1",
    "CONF:TRIG #H1,0,1,1", "CONF:TRIG 1,0,18446744073709551616,1",
    "CONF:TRIG 1,0,1,1 garbage", "CONF:TRIG 1,0,1,1,,", "CONF:TRIG 1,0,1,1 @",
    "CONF:SEQ A", "CONF:SEQ A,", "CONF:SEQ A,0,", "CONF:SEQ A,0,,1",
    "CONF:SEQ A,0,-4294967296", "CONF:SEQ A,0,4294967296", "CONF:SEQ A,0,2",
    "CONF:SEQ A,0,1x", "CONF:SEQ A,0,1.1", "CONF:SEQ 0A,0", "CONF:SEQ A-B,0",
    "CONF:SEQ A,\"0\"", "CONF:SEQ A,#11a", "CONF:SEQ A,0x1",
    "CONF:SEQ ABCDEFGHIJKLMNOPQRSTUVWXYZ123456,0", "CONF:SEQ \"\",0",
    "CONF:SEQ:ACT", "CONF:SEQ:ACT A,1", "CONF:SEQ:ACT A,", "CONF:SEQ:ACT UNKNOWN",
    "CONF:SEQ:ACT A @", "CONF:SEQ:ACT A extra",
    "READ:TRIG:PAR? extra", "READ:SEQ? A,-1", "READ:SEQ? A,2",
    "READ:SEQ? A,0,1", "READ:SEQ? A,", "READ:SEQ:MAP? A,0",
    "READ:SEQ? ,0", "READ:SEQ? A,4294967296", "READ:SEQ? A,0,",
    "READ:SEQ:CHECK? A,0", "READ:SEQ:ACT? A",
])
def test_invalid_commands_preserve_active_config(parser, bad):
    rows = run(parser, ["CONF:TRIG 1,0,2,1", "CONF:SEQ A,0,1", "CONF:SEQ:ACT A",
                        "READ:SEQ:ACT?", bad, "READ:SEQ:ACT?"])
    assert rows[4]["errors"] > 0
    assert rows[4]["unchanged"]
    assert rows[3]["fields"] == rows[5]["fields"]


def test_generation_invalidation_rebind_and_two_freeze_guards(parser):
    rows = run(parser, [
        "CONF:TRIG 1,0,2,1", "CONF:SEQ A,0,1", "CONF:SEQ B,1", "CONF:SEQ:ACT A",
        "CONF:TRIG 1,0,2,1", "READ:SEQ:CHECK?", "READ:SEQ:ACT?",
        "READ:SEQ? A", "READ:SEQ:MAP? A", "CONF:SEQ:ACT B",
        "CONF:SEQ A,1,0", "READ:SEQ:ACT?", "CONF:SEQ:ACT A",
        "@freeze", "CONF:TRIG 1,0,2,1", "CONF:SEQ A,0", "CONF:SEQ:ACT A",
        "READ:SEQ:ACT?", "@thaw", "@legacy", "CONF:TRIG 1,0,2,1",
        "CONF:SEQ A,0", "CONF:SEQ:ACT A", "@idle", "READ:SEQ:ACT?",
    ])
    assert rows[4]["generation"] == 2
    assert rows[5]["fields"][-1] == "STALE_GENERATION" and rows[5]["unchanged"]
    assert rows[6]["fields"][-3:] == ["0", "FAIL", "STALE_GENERATION"]
    assert rows[7]["errors"] == 0
    assert rows[11]["fields"] == ["NONE", "0", "0", "0", "NONE", "NO_ACTIVE_PLAN"]
    for index in [8, 9, 13, 14, 15, 17, 18, 19]:
        assert rows[index]["errors"] > 0 and rows[index]["unchanged"]
    assert rows[16]["fields"] == rows[20]["fields"]


def test_capacity_is_atomic_and_full_reply_not_truncated(parser):
    ids = list(range(256))
    plan = "CONF:SEQ A," + ",".join(map(str, ids))
    rows = run(parser, ["CONF:TRIG +8,2,8,2", plan, "CONF:SEQ:ACT A", plan + ",0",
                        "READ:SEQ?", "READ:SEQ:MAP?", "CONF:SEQ B,0", "CONF:SEQ C,0",
                        "CONF:SEQ D,0", "CONF:SEQ E,0", "CONF:SEQ C,1"])
    assert rows[0]["errors"] == 0
    assert rows[3]["errors"] > 0 and rows[3]["unchanged"]
    assert rows[4]["fields"][5:] == list(map(str, ids))
    assert len(rows[5]["fields"]) == 256 * 6
    assert rows[9]["errors"] > 0 and rows[9]["unchanged"]
    assert rows[10]["errors"] == 0
