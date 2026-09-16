"""Real SCPI/config/service integration with a deterministic hardware backend."""
import csv
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
NO_INDEX = str(0xFFFFFFFF)
SETUP = [
    "CONF:SEQ:REP 0", "CONF:TRIG 3,0,1,1", "CONF:SEQ A,2,0,1", "CONF:SEQ:ACT A",
    "CONF:SEQ:IO 7,OUT4,10,5", "CONF:SEQ:CODE 0,1",
    "CONF:SEQ:CODE 1,2", "CONF:SEQ:CODE 2,4", "CONF:SEQ:SOUR MANUAL,RISING",
]


@pytest.fixture(scope="module")
def parser(tmp_path_factory):
    directory = tmp_path_factory.mktemp("sequence-runtime")
    executable = directory / "sequence-runtime.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    library = ROOT / "third_party/scpi-parser/libscpi"
    includes = [library / "inc", ROOT / "middleware/scpi_port/inc",
                ROOT / "components/sync_trigger/inc", ROOT / "components/sync_io/inc",
                ROOT / "components/distributed_config/inc", ROOT / "boards/rp2350_trig/inc",
                ROOT / "tests/unit/host_stubs", ROOT / "third_party/portable_ota/include",
                ROOT / "osal/inc"]
    sources = ["tests/unit/test_sequence_scpi_runtime.c",
               "middleware/scpi_port/src/scpi_config_commands.c",
               "middleware/scpi_port/src/scpi_sequence_commands.c",
               "middleware/scpi_port/src/scpi_trigger_commands.c",
               "components/sync_trigger/src/trigger_sequence_config.c",
               "components/sync_trigger/src/trigger_sequence_service.c",
               "third_party/portable_ota/src/pota_crc32.c"]
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-DSCPI_USER_CONFIG=1", *("-I" + str(path) for path in includes),
               *(str(ROOT / path) for path in sources),
               *(str(path) for path in sorted((library / "src").glob("*.c"))),
               *(["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]),
               "-o", str(executable)]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


def run(parser, commands, setup=True):
    all_commands = (SETUP if setup else []) + commands
    result = subprocess.run([str(parser)], input="\n".join(all_commands) + "\n",
                            text=True, capture_output=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    rows = []
    for line in result.stdout.splitlines():
        errors, reason, writes, response = line.split("|", 3)
        rows.append(dict(errors=int(errors), reason=reason, writes=int(writes),
                         fields=next(csv.reader([response]))))
    assert len(rows) == sum(not command.startswith("@") for command in all_commands)
    if setup:
        assert all(row["errors"] == 0 for row in rows[:len(SETUP)]), rows
        rows = rows[len(SETUP):]
    return rows


def test_configuration_start_and_ordered_bus_cycle(parser):
    rows = run(parser, [
        "READ:SEQ:SOUR?", "READ:SEQ:IO?", "READ:SEQ:OUTPUT?", "READ:SEQ:CODE? 2", "TRIG:START",
        "READ:SEQ:STATE?", "@service", "READ:SEQ:STATE?", "READ:IO:STATE?",
        "TRIG:SEQ:NEXT", "@service", "READ:SEQ:STATE?", "READ:IO:OUTP?",
        "@rise", "READ:IO:OUTP? 4", "READ:IO:OUTP?", "@complete", "@service",
        "READ:SEQ:STATE?", "READ:IO:OUTP? 4",
        "TRIG:SEQ:NEXT", "@service", "READ:IO:OUTP?", "@complete", "@service",
        "TRIG:SEQ:NEXT", "@service", "READ:IO:OUTP?", "@complete", "@service",
        "READ:SEQ:STATE?", "TRIG:SEQ:NEXT", "@service", "READ:SEQ:STATE?",
        "READ:TRIG:STATE?", "TRIG:STOP", "@service", "READ:IO:STATE?",
    ])
    assert all(row["errors"] == 0 for row in rows), rows
    assert rows[0]["fields"] == ["MANUAL", "RISING"]
    assert rows[1]["fields"] == ["7", "4", "10", "5", "1", "1"]
    assert rows[2]["fields"] == ["7", "8", "PULSE", "10", "5", "1", "1"]
    assert rows[3]["fields"] == ["2", "4"]
    assert rows[5]["fields"][0] == "STARTING"
    assert rows[6]["fields"][:7] == ["READY", "1", "1", "3", "0", "2", "1"]
    assert rows[7]["fields"] == ["0", "4", "15", "1", "0"]
    assert rows[9]["fields"][:14] == ["BUSY", "1", "1", "3", "1", "0", "1", "1", "0", NO_INDEX, NO_INDEX, "0", "1", "0"]
    assert rows[9]["fields"][19:22] == ["0", "0", "0"]
    assert rows[10]["fields"] == ["1"]
    assert rows[11]["fields"] == ["1"] and rows[12]["fields"] == ["9"]
    assert rows[13]["fields"][6:14] == ["2", "1", "0", "1", "0", "0", "1", "1"]
    assert rows[13]["fields"][19:22] == ["0", "0", "0"]
    assert rows[14]["fields"] == ["0"]
    assert rows[16]["fields"] == ["2"] and rows[18]["fields"] == ["4"]
    assert rows[19]["fields"][6:14] == ["1", "0", "2", "0", "2", "1", "3", "3"]
    assert rows[21]["fields"][4:14] == ["1", "0", "1", "1", "0", "0", "2", "1", "4", "3"]
    assert rows[21]["fields"][19:22] == ["0", "0", "0"]
    assert rows[22]["fields"][:4] == ["TRIG", "BUSY", "1", "A"]
    assert rows[24]["fields"] == ["0", "0", "0", "0", "0"]


def test_repeat_default_and_large_configurations(parser):
    rows = run(parser, ["READ:SEQ:REP?", "CONF:SEQ:REP 10", "READ:SEQ:REP?",
                        "CONF:SEQ:REP 100", "READ:SEQ:REP?", "CONF:SEQ:REP 10000",
                        "READ:SEQ:REP?", "CONF:SEQ:REP 0", "READ:SEQ:REP?"], setup=False)
    assert all(r["errors"] == 0 for r in rows)
    assert [rows[i]["fields"][0] for i in (0, 2, 4, 6, 8)] == ["1", "10", "100", "10000", "0"]


@pytest.mark.parametrize("command", ["CONF:SEQ:REP -1", "CONF:SEQ:REP 4294967296",
    "CONF:SEQ:REP 1,2", "CONF:SEQ:REP 1.5"])
def test_repeat_rejects_invalid_without_mutation(parser, command):
    rows = run(parser, ["CONF:SEQ:REP 10000", command, "READ:SEQ:REP?"])
    assert rows[1]["errors"] and rows[2]["fields"][0] == "10000"


def test_repeat_is_frozen_during_run(parser):
    rows = run(parser, ["TRIG:START", "CONF:SEQ:REP 1", "READ:SEQ:REP?", "TRIG:STOP", "@service"])
    assert rows[1]["reason"] == "FROZEN" and rows[2]["fields"][0] == "0"


def test_repeat_uint32_total_state_boundary(parser):
    maximum = 0xffffffff // 3  # SETUP selects three states.
    rows = run(parser, [f"CONF:SEQ:REP {maximum}", "TRIG:START", "@service",
                        "READ:SEQ:REP?", "TRIG:STOP", "@service",
                        f"CONF:SEQ:REP {maximum + 1}", "TRIG:START", "READ:IO:STATE?"])
    assert all(row["errors"] == 0 for row in rows[:5])
    assert rows[2]["fields"] == [str(maximum), str(maximum), "0"]
    assert rows[5]["errors"] > 0
    assert rows[6]["fields"] == ["0"] * 5


def test_level_status_and_multi_output_roles(parser):
    rows = run(parser, [
        "CONF:SEQ:OUTPUT 7,8,LEVEL,10,0", "READ:SEQ:OUTPUT?", "TRIG:START",
        "@service", "READ:IO:OUTP?", "TRIG:SEQ:NEXT", "@service",
        "READ:IO:OUTP?", "@rise", "READ:IO:OUTP?", "@complete", "@service",
        "READ:IO:OUTP?", "TRIG:STOP", "@service",
        "CONF:SEQ:OUTPUT 3,12,PULSE,10,5",
        "CONF:SEQ:CODE 0,1", "CONF:SEQ:CODE 1,2", "CONF:SEQ:CODE 2,0",
        "READ:SEQ:OUTPUT?",
    ])
    assert all(row["errors"] == 0 for row in rows), rows
    assert rows[1]["fields"] == ["7", "8", "LEVEL", "10", "0", "1", "1"]
    assert rows[3]["fields"] == ["12"]
    assert rows[5]["fields"] == ["1"]
    assert rows[6]["fields"] == ["9"]
    assert rows[7]["fields"] == ["9"]
    assert rows[-1]["fields"] == ["3", "12", "PULSE", "10", "5", "1", "1"]


def test_dut_only_has_no_status_output(parser):
    rows = run(parser, [
        "CONF:SEQ:OUTPUT 7,0,NONE,10,0", "READ:SEQ:OUTPUT?", "TRIG:START", "@service",
        "READ:IO:STATE?", "TRIG:SEQ:NEXT", "@service", "@rise", "READ:IO:OUTP?",
        "@complete", "@service", "TRIG:SEQ:NEXT?", "READ:IO:STATE?",
        "CONF:SEQ:OUTPUT 7,8,PULSE,10,5", "TRIG:STOP", "@service", "READ:IO:STATE?",
    ])
    assert all(row["errors"] == 0 for row in rows[:8]), rows
    assert rows[1]["fields"] == ["7", "0", "NONE", "10", "0", "1", "1"]
    assert rows[3]["fields"] == ["0", "4", "7", "1", "0"]
    assert rows[5]["fields"] == ["1"]  # no OUT4 even at status action
    assert rows[6]["fields"][12:14] == ["1", "1"]
    assert rows[7]["fields"] == ["0", "1", "7", "1", "0"]
    assert rows[8]["reason"] == "FROZEN"
    assert rows[-1]["fields"] == ["0", "0", "0", "0", "0"]


@pytest.mark.parametrize("command", [
    "CONF:SEQ:OUTPUT 7,8,NONE,10,0", "CONF:SEQ:OUTPUT 7,0,NONE,10,5",
    "CONF:SEQ:OUTPUT 7,0,LEVEL,10,0", "CONF:SEQ:OUTPUT 0,0,NONE,10,0",
])
def test_bad_dut_only_config_preserves_previous_outputs(parser, command):
    rows = run(parser, [command, "READ:SEQ:OUTPUT?"])
    assert rows[0]["errors"] and rows[0]["reason"] == "INVALID_ARGUMENT"
    assert rows[1]["fields"] == ["7", "8", "PULSE", "10", "5", "1", "1"]


@pytest.mark.parametrize("channel", range(1, 5))
@pytest.mark.parametrize("edge,falling", [("RISING", 0), ("FALLING", 1)])
def test_selectable_input_and_edge(parser, channel, edge, falling):
    rows = run(parser, [
        f"CONF:SEQ:SOUR IN{channel},{edge}", "READ:SEQ:SOUR?", "TRIG:START", "@service",
        "TRIG:SEQ:NEXT", f"@edge {channel % 4 + 1} {falling}", "@service",
        f"@edge {channel} {1 - falling}", "@service", "READ:SEQ:STATE?",
        f"@edge {channel} {falling}", f"@edge {channel} {falling}", "@service",
        "READ:SEQ:STATE?", "READ:IO:OUTP?", "@complete", "@service",
        "READ:SEQ:STATE?", "TRIG:PAUSE", "@service", f"@edge {channel} {falling}",
        "@service", "READ:SEQ:STATE?", "TRIG:CONT", "@service",
        f"@edge {channel} {falling}", "@service", "READ:IO:OUTP?",
    ])
    assert rows[1]["fields"] == [f"IN{channel}", edge]
    assert rows[3]["errors"] and rows[3]["reason"] == "SOURCE_MISMATCH"
    assert rows[4]["writes"] == 0 and rows[4]["fields"][0] == "READY"
    assert rows[5]["fields"][0] == "BUSY" and rows[5]["fields"][14] == "1"
    assert rows[6]["fields"] == ["1"]
    assert rows[7]["fields"][6] == "2" and rows[7]["fields"][13] == "1"
    assert rows[9]["fields"][0] == "PAUSED" and rows[9]["fields"][15] == "2"
    assert rows[11]["fields"] == ["2"]


def test_busy_pause_stop_and_restart(parser):
    rows = run(parser, [
        "TRIG:START", "TRIG:SEQ:NEXT", "@service", "TRIG:SEQ:NEXT",
        "TRIG:SEQ:NEXT", "@service", "TRIG:PAUSE", "@service", "READ:SEQ:STATE?",
        "@complete", "@service", "READ:SEQ:STATE?", "TRIG:SEQ:NEXT",
        "TRIG:CONT", "@service", "READ:SEQ:STATE?", "TRIG:SEQ:NEXT",
        "TRIG:STOP", "@service", "READ:SEQ:STATE?", "TRIG:START", "@service",
        "READ:SEQ:STATE?", "TRIG:ABOR", "@service", "READ:IO:STATE?",
    ])
    assert rows[1]["reason"] == rows[3]["reason"] == "BUSY"
    assert rows[5]["fields"][0] == "PAUSING"
    assert rows[6]["fields"][0] == "PAUSED"
    assert rows[7]["reason"] == "NOT_READY"
    assert rows[9]["fields"][0] == "READY" and rows[9]["writes"] == 1
    assert rows[12]["fields"][0] == "IDLE"
    assert rows[12]["fields"][12:17] == ["2", "1", "2", "1", "1"]
    assert rows[14]["fields"][:2] == ["READY", "2"]
    assert rows[14]["fields"][12:17] == ["0"] * 5
    assert rows[16]["fields"] == ["0"] * 5


@pytest.mark.parametrize("bad", [
    "CONF:SEQ:SOUR", "CONF:SEQ:SOUR BUS,RISING", "CONF:SEQ:SOUR IN0,RISING",
    "CONF:SEQ:SOUR IN5,RISING",
    "CONF:SEQ:SOUR IN1,BOTH", "CONF:SEQ:SOUR IN1,RISING,", "CONF:SEQ:SOUR IN1,RISING,1",
    "CONF:SEQ:IO 7,OUT4,10", "CONF:SEQ:IO 7,OUT4,10,5,", "CONF:SEQ:IO 7,OUT4,10,5,0",
    "CONF:SEQ:IO 8,OUT4,10,5", "CONF:SEQ:IO 0,OUT4,10,5", "CONF:SEQ:IO 16,OUT4,10,5",
    "CONF:SEQ:IO 7,OUT5,10,5", "CONF:SEQ:IO 7,OUT4,10,0",
    "CONF:SEQ:IO -4294967289,OUT4,10,5", "CONF:SEQ:IO 7,OUT4,-1,5",
    "CONF:SEQ:IO 7,OUT4,2147483648,5", "CONF:SEQ:IO 7,OUT4,10,2147483648",
    "CONF:SEQ:OUTPUT 0,8,PULSE,10,5", "CONF:SEQ:OUTPUT 7,0,PULSE,10,5",
    "CONF:SEQ:OUTPUT 7,8,LEVEL,10,5", "CONF:SEQ:OUTPUT 7,8,PULSE,10,0",
    "CONF:SEQ:OUTPUT 3,3,PULSE,10,5", "CONF:SEQ:OUTPUT 3,16,PULSE,10,5",
    "CONF:SEQ:OUTPUT 3,12,READY,10,5", "READ:SEQ:OUTPUT? 1",
    "CONF:SEQ:CODE 0", "CONF:SEQ:CODE 0,3,", "CONF:SEQ:CODE 0,3,1",
    "CONF:SEQ:CODE 0,8", "CONF:SEQ:CODE 3,1", "CONF:SEQ:CODE -4294967296,1",
    "CONF:SEQ:CODE 0,4294967297", "CONF:SEQ:CODE 0,1.0", "CONF:SEQ:CODE 0,#H1",
    "TRIG:START A,", "TRIG:START A,1", "TRIG:START ,", "TRIG:START UNKNOWN",
    "TRIG:SEQ:NEXT 1", "TRIG:PAUSE 1", "TRIG:CONT 1", "TRIG:STOP 1", "TRIG:ABOR 1",
    "READ:SEQ:CODE?", "READ:SEQ:CODE? 0,", "READ:SEQ:CODE? 3",
    "READ:SEQ:IO? 1", "READ:SEQ:SOUR? 1", "READ:SEQ:STATE? 1", "READ:IO:STATE? 1",
    "READ:IO:INP? 0", "READ:IO:INP? 5", "READ:IO:INP? -1", "READ:IO:INP? 1,",
    "READ:IO:OUTP? 1,2", "READ:IO:OUTP? 1.0", "READ:IO:OUTP? +1",
    "TRIG:MODE", "TRIG:MODE -4294967295", "TRIG:MODE 4294967297", "TRIG:MODE 1.0",
    "TRIG:MODE 5", "TRIG:MODE 1,", "TRIG:MODE? 1", "READ:TRIG:STATE? 1",
])
def test_bad_commands_do_not_change_configuration_or_start(parser, bad):
    queries = ["READ:SEQ:SOUR?", "READ:SEQ:IO?", "READ:SEQ:CODE? 0",
               "READ:SEQ:ACT?", "READ:SEQ:STATE?", "TRIG:MODE?"]
    rows = run(parser, queries + [bad] + queries)
    assert rows[len(queries)]["errors"] > 0, rows[len(queries)]
    assert [row["fields"] for row in rows[:len(queries)]] == [row["fields"] for row in rows[len(queries) + 1:]]
    assert all(row["writes"] == 0 for row in rows)


@pytest.mark.parametrize("advance,query", [
    ("TRIG:SEQ:NEXT", "TRIG:SEQ:NEXT?"),
    ("TRIGger:SEQuence:NEXT", "TRIGger:SEQuence:NEXT?"),
])
def test_next_command_and_read_query(parser, advance, query):
    rows = run(parser, ["TRIG:START", "@service", query, advance, "@service",
                        query, advance, "@complete", "@service", query, query,
                        "READ:SEQ:STAT?", "CONF:SEQ:NEXT", "READ:SEQ:NEXT?",
                        "TRIG:SEQ:STEP", query + " 1", advance + " 1"])
    assert rows[1]["fields"][12:14] == ["0", "0"]
    assert rows[2]["errors"] == 0 and rows[2]["fields"] == ["1"]
    assert rows[3]["fields"][12:14] == ["1", "0"]
    assert rows[4]["reason"] == "BUSY"
    assert rows[5]["fields"][12:14] == ["1", "1"]
    assert rows[5]["fields"] == rows[6]["fields"] == rows[7]["fields"]
    assert rows[5]["writes"] == rows[6]["writes"] == rows[7]["writes"] == 1
    assert all(row["errors"] > 0 for row in rows[8:])


def test_io_levels_are_logical_masks_and_channels(parser):
    rows = run(parser, ["@inputs 10", "READ:IO:INP?", *[f"READ:IO:INP? {i}" for i in range(1, 5)],
                        "READ:IO:STATE?"])
    assert [row["fields"] for row in rows] == [["10"], ["0"], ["1"], ["0"], ["1"], ["10", "0", "0", "0", "0"]]


def test_pio_timing_query_has_no_fabricated_physical_timestamps(parser):
    rows = run(parser, ["READ:SEQ:TIM?", "TRIG:START", "@service", "READ:SEQ:TIM?",
                        "TRIG:SEQ:NEXT", "@service", "@rise", "@complete", "@service",
                        "READ:SEQ:STATE?", "READ:SEQ:TIM?", "TRIG:STOP", "@service",
                        "READ:SEQ:TIM?", "READ:SEQ:TIM? 1"])
    assert rows[0]["fields"] == ["NONE", "0", "0"]
    assert rows[2]["fields"] == rows[5]["fields"] == rows[7]["fields"] == ["PIO0", "100", "0"]
    assert rows[4]["fields"][19:22] == ["0", "0", "0"]
    assert rows[8]["errors"] > 0


def test_rejection_query_marks_running_and_settled_counts(parser):
    rows = run(parser, ["CONF:SEQ:SOUR IN1,RISING", "TRIG:START", "@service",
                        "READ:SEQ:REJ?", "@edge 1 0", "@edge 1 0", "TRIG:PAUSE",
                        "@service", "READ:SEQ:REJ?", "@complete", "@service",
                        "READ:SEQ:REJ?", "TRIG:CONT", "@service", "READ:SEQ:REJ?",
                        "TRIG:STOP", "@service", "READ:SEQ:REJ?", "READ:SEQ:REJ? 1"])
    assert rows[2]["fields"] == ["1", "1", "0", "0", "1"]
    assert rows[4]["fields"] == ["1", "1", "1", "0", "1"]
    assert rows[5]["fields"] == ["1", "1", "1", "0", "0"]
    assert rows[7]["fields"][-1] == "1"
    assert rows[9]["fields"][-1] == "0"
    assert rows[10]["errors"] > 0


@pytest.mark.parametrize("command", ["CONF:TRIG 3,0,1,1", "CONF:SEQ A,0", "CONF:SEQ:ACT A",
                                    "CONF:SEQ:SOUR IN4,FALLING", "CONF:SEQ:IO 3,OUT4,20,5",
                                    "CONF:SEQ:CODE 0,3", "TRIG:START", "TRIG:MODE 2"])
def test_running_configuration_is_frozen(parser, command):
    rows = run(parser, ["TRIG:START", "@service", command, "TRIG:SEQ:NEXT", "@service", "READ:IO:OUTP?"])
    assert rows[1]["errors"] > 0
    assert rows[2]["errors"] == 0 and rows[3]["fields"] == ["1"]


def test_failed_start_and_generation_require_reconfiguration(parser):
    rows = run(parser, ["CONF:SEQ B,0", "@legacy 1", "TRIG:START B", "READ:SEQ:ACT?",
                        "@legacy 0", "CONF:TRIG 3,0,1,1", "READ:SEQ:IO?", "READ:SEQ:CODE? 0",
                        "TRIG:START", "CONF:SEQ A,2,0,1", "CONF:SEQ:ACT A", "TRIG:START",
                        "CONF:SEQ:IO 7,OUT4,10,5", "TRIG:START"])
    assert rows[1]["reason"] == "RESOURCE_BUSY" and rows[2]["fields"][0] == "A"
    assert rows[4]["fields"][-2:] == ["2", "0"]
    assert rows[5]["reason"] == "CODE_MISSING" and rows[6]["reason"] == "CONFIG_INVALID"
    assert rows[9]["reason"] == "IO_CONFIG_INVALID" and rows[11]["reason"] == "CODE_MISSING"


@pytest.mark.parametrize("control", ["@armok 0", "@submitok 0"])
def test_backend_fault_is_reported_and_stop_releases_outputs(parser, control):
    commands = [control, "TRIG:START", "@service"]
    if "submit" in control:
        commands += ["TRIG:SEQ:NEXT", "@service"]
    rows = run(parser, commands + ["READ:SEQ:STATE?", "TRIG:STOP", "@service", "READ:IO:STATE?"])
    assert rows[-3]["fields"][0] == "FAULT"
    assert int(rows[-3]["fields"][23]) != 0
    assert rows[-1]["fields"] == ["0"] * 5
