"""Execute unmodified production RefMem handlers against the real SCPI parser.

The snapshot translation unit also contains hardware/TDMA diagnostics. Extract
whole functions (never rewrite their bodies) to keep this host test scoped to
configuration ingress, with observable model/storage boundary fakes.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r"^(?:static )?[\w_]+ " + re.escape(name) + r"\([^;]*?\n\{", source, re.M)
    assert match, name
    start = match.start()
    opening = source.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@pytest.fixture(scope="module")
def parser(tmp_path_factory):
    directory = tmp_path_factory.mktemp("refmem-scpi-freeze")
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    names = ["scpi_refmem_model_mode_idle", "scpi_refmem_realtime_idle",
             "scpi_refmem_sequence_config_allowed", "scpi_refmem_result_load_snapshot",
             "scpi_refmem_result_board_load_snapshot", "scpi_refmem_result_table_image_descriptor",
             "scpi_refmem_result_activation_diagnostic", "scpi_cmd_refmem_load_activation_status_q",
             "scpi_cmd_refmem_load_sd", "scpi_cmd_refmem_load_node", "scpi_cmd_refmem_load_board",
             "scpi_cmd_refmem_load_activate", "scpi_refmem_sync_apply_node_load_delta"]
    config = (ROOT / "middleware/scpi_port/src/scpi_config_commands.c").read_text(encoding="utf-8")
    service = (ROOT / "components/sync_trigger/src/trigger_sequence_service.c").read_text(encoding="utf-8")
    bodies = [function(service, "trigger_sequence_service_is_active")]
    bodies += [function(config, name) for name in ["sequence_end_parameters", "sequence_read_u32",
              "scpi_sequence_param_u32", "scpi_sequence_params_end"]]
    bodies += [function(source, name) for name in names]
    node = (ROOT / "middleware/scpi_port/src/scpi_sequence_node_commands.c").read_text(encoding="utf-8")
    bodies += [function(node, name) for name in ["sequence_node_config_allowed",
               "scpi_sequence_node_load", "scpi_sequence_node_activate"]]
    model = (ROOT / "middleware/scpi_port/src/scpi_model_commands.c").read_text(encoding="utf-8")
    bodies += [function(model, "scpi_cmd_model_turntable_load")]
    (directory / "refmem_scpi_handlers.inc").write_text("\n\n".join(bodies), encoding="utf-8")
    library = ROOT / "third_party/scpi-parser/libscpi"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
                or "D:/Microsoft/mingw64/bin/gcc.exe")
    includes = [directory, library / "inc", ROOT / "middleware/scpi_port/inc",
                ROOT / "components/distributed_refmem/inc", ROOT / "components/sync_trigger/inc",
                ROOT / "components/sync_io/inc",
                ROOT / "components/distributed_config/inc", ROOT / "components/tdma/inc",
                ROOT / "components/storage_manager/inc", ROOT / "middleware/fatfs_port/inc",
                ROOT / "drivers/external/sd_card/inc", ROOT / "third_party/fatfs/source",
                ROOT / "tests/unit/host_stubs", ROOT / "osal/inc", ROOT / "boards/rp2350_trig/inc"]
    executable = directory / "refmem-scpi.exe"
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-DSCPI_USER_CONFIG=1", *("-I" + str(path) for path in includes),
               str(ROOT / "tests/unit/test_refmem_scpi_freeze.c"),
               *(str(path) for path in sorted((library / "src").glob("*.c"))),
               *(["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]), "-o", str(executable)]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


def run(parser, commands):
    result = subprocess.run([str(parser)], input="\n".join(commands) + "\n", text=True,
                            capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    return [[int(value) for value in line.split(",")] for line in result.stdout.splitlines()]


MUTATIONS = ["SYST:REFMEM:LOAD:NODE 0,0,4,2", "CONF:SEQ:NODE:LOAD 0,0,4,2",
             "SYST:REFMEM:LOAD:BOARD 0,1,15,15,15,1,0,0,1", "SYST:REFMEM:LOAD:SD",
             "SYST:REFMEM:LOAD:ACT", "CONF:SEQ:NODE:ACT"]


@pytest.mark.parametrize("state", range(1, 8))
@pytest.mark.parametrize("command", MUTATIONS)
def test_all_sequence_states_freeze_refmem_and_idle_reopens(parser, state, command):
    rows = run(parser, [f"@state {state}", command, "@state 0", command])
    assert rows[0][0] > 0 and rows[0][1] == 0, rows
    assert rows[1][0] == 0 and rows[1][1] == 1, rows


@pytest.mark.parametrize("command", MUTATIONS)
def test_legacy_nonidle_still_rejects(parser, command):
    rows = run(parser, ["@legacy 1", command, "@legacy 0", command])
    assert rows[0][0] > 0 and rows[0][1] == 0
    assert rows[1][0] == 0 and rows[1][1] == 1


@pytest.mark.parametrize("parameters", ["0,0,4,2", "0,0,4,2,0", "0,0,4,2,1,1", "0,0,4,2,0,1,7"])
def test_node_optional_defaults(parser, parameters):
    rows = run(parser, ["CONF:SEQ:NODE:LOAD " + parameters])
    values = [int(value) for value in parameters.split(",")]
    expected = (values + [1, 0, 0][len(values) - 4:])[4:]
    assert rows == [[0, 1, *expected]]


@pytest.mark.parametrize("parameters", [
    "0,0,4", "0,0,4,2,", "0,0,4,2,BAD", "0,0,4,2,1,BAD", "0,0,4,2,1,0,BAD",
    "0,0,4,2,1,0,0,1", "0,0,4,2,1,0,0,", "0,0,4,2,,0,0", "0,0,4,2,2",
    "0,0,4,2,1,2", "0,0,4,2,-1", "0,0,4,2,1,0,-1", "0,0,4,2,1,0,4294967296",
    "0,0,4,2,1,0,1.5", "-4294967296,0,4,2", "4294967296,0,4,2",
])
def test_bad_node_parameters_do_not_mutate(parser, parameters):
    rows = run(parser, ["CONF:SEQ:NODE:LOAD 0,0,4,2,0,1,7",
                        "CONF:SEQ:NODE:LOAD " + parameters])
    assert rows[0] == [0, 1, 0, 1, 7]
    assert rows[1][0] > 0 and rows[1][1:] == rows[0][1:]


@pytest.mark.parametrize("command", [
    "SYST:REFMEM:LOAD:BOARD 0,1,15,15,15,1,0,0,1,1", "SYST:REFMEM:LOAD:BOARD 0,1,15,15,15,1,0,0,1,",
    "SYST:REFMEM:LOAD:BOARD -4294967296,1,15,15,15,1,0,0,1", "SYST:REFMEM:LOAD:ACT 1",
    "SYST:REFMEM:LOAD:SD /path,/extra", "SYST:REFMEM:LOAD:SD /path,",
])
def test_other_malformed_loads_do_not_mutate(parser, command):
    rows = run(parser, [command])
    assert rows[0][0] > 0 and rows[0][1] == 0


def test_sync_delta_cannot_bypass_active_sequence_freeze(parser):
    rows = run(parser, ["@state 2", "@delta", "@state 5", "@delta", "@state 0", "@delta"])
    assert [row[1] for row in rows] == [0, 0, 1]


def test_sync_mutation_admission_rejects_until_idle(parser):
    rows = run(parser, ["@state 1", "@syncguard", "@state 5", "@syncguard", "@state 0", "@syncguard"])
    assert [row[0] for row in rows] == [1, 1, 0]


@pytest.mark.parametrize("state", range(1, 8))
def test_model_turntable_load_cannot_bypass_sequence_freeze(parser, state):
    rows = run(parser, [f"@state {state}", "CONF:MODEL:TURN:LOAD 2,3", "@state 0", "CONF:MODEL:TURN:LOAD 2,3"])
    assert rows[0][0] > 0 and rows[0][1] == 0
    assert rows[1][0] == 0 and rows[1][1] == 1
