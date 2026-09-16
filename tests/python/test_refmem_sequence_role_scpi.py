"""Real SCPI handlers and RMTP model for sequence role configuration."""
import csv
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def production_function(source, name):
    """Copy one whole function unchanged, excluding unrelated hardware handlers."""
    match = re.search(r"^(?:static )?[\w_]+ " + re.escape(name) + r"\([^;]*?\n\{", source, re.M)
    assert match, name
    opening = source.index("{", match.start())
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


@pytest.fixture(scope="module")
def parser(tmp_path_factory):
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    directory = tmp_path_factory.mktemp("role-parser")
    executable = directory / "role-parser.exe"
    config = (ROOT / "middleware/scpi_port/src/scpi_config_commands.c").read_text(encoding="utf-8")
    node = (ROOT / "middleware/scpi_port/src/scpi_sequence_node_commands.c").read_text(encoding="utf-8")
    owner = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    functions = [production_function(owner, name) for name in [
        "distributed_refmem_u32_payload_crc32", "distributed_refmem_command_state_is_complete",
        "distributed_refmem_next_command_seq", "distributed_refmem_post_command_replacing_complete",
        "distributed_refmem_stage_sequence_role"]]
    functions += [production_function(config, name) for name in [
        "sequence_end_parameters", "sequence_read_u32", "scpi_sequence_param_u32",
        "scpi_sequence_params_end"]]
    functions += [production_function(node, name) for name in [
        "sequence_node_config_allowed", "scpi_sequence_node_role", "scpi_sequence_node_role_q",
        "scpi_sequence_link_config", "scpi_sequence_link_q", "scpi_sequence_link_transport_q"]]
    (directory / "sequence_role_scpi_handlers.inc").write_text("\n\n".join(functions), encoding="utf-8")
    library = ROOT / "third_party/scpi-parser/libscpi"
    includes = [directory, ROOT / "tests/unit/host_stubs", ROOT / "config", ROOT / "osal/inc",
                ROOT / "boards/rp2350_trig/inc", ROOT / "third_party/portable_ota/include",
                ROOT / "middleware/scpi_port/inc", library / "inc"]
    includes += sorted((ROOT / "components").glob("*/inc"))
    sources = ["tests/unit/test_refmem_sequence_role_scpi.c",
               "components/distributed_refmem/src/refmem_application_model.c",
               "components/distributed_refmem/src/refmem_application_contract.c",
               "components/distributed_refmem/src/refmem_table_registry.c",
               "components/distributed_refmem/src/refmem_realtime_contract.c",
               "components/distributed_refmem/src/refmem_slot_claim.c",
               "components/distributed_refmem/src/refmem_command.c",
               "components/tdma/src/tdma_profile.c",
               "third_party/portable_ota/src/pota_crc32.c"]
    command = [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
               "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-DSCPI_USER_CONFIG=1",
               *[f"-I{path}" for path in includes],
               *[str(ROOT / path) for path in sources],
               *[str(path) for path in sorted((library / "src").glob("*.c"))],
               *(["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]), "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


def run(parser, commands):
    proc = subprocess.run([str(parser)], input="\n".join(commands)+"\n", text=True,
                          capture_output=True, timeout=15)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    rows = []
    for line in proc.stdout.splitlines():
        errors, response = line.split("|", 1)
        rows.append((int(errors), next(csv.reader([response]))))
    assert len(rows) == sum(not cmd.startswith("@") for cmd in commands)
    return rows


def test_stage_read_and_activate_real_roles(parser):
    rows = run(parser, ["READ:SEQ:NODE:ROLE? 5", "CONF:SEQ:NODE:ROLE 2,5,DUT",
                        "READ:SEQ:NODE:ROLE? 5", "CONF:SEQ:NODE:ROLE 3,7,VNA_GATEWAY",
                        "READ:SEQ:NODE:ROLE? 7", "@activate", "READ:SEQ:NODE:ROLE? 5",
                        "READ:SEQ:NODE:ROLE? 7"])
    assert all(errors == 0 for errors, _ in rows), rows
    assert rows[0][1][2:4] == ["0", "0"]
    assert rows[1][1] == ["STAGED"]
    assert rows[2][1][2:4] == ["0", "1"]
    assert rows[2][1][7:] == ["152", "11", "5"]
    assert rows[4][1][2:4] == ["0", "1"]
    assert rows[4][1][7:] == ["152", "3", "3"]
    assert rows[5][1][2:4] == rows[6][1][2:4] == ["1", "1"]


@pytest.mark.parametrize("bad", [
    "CONF:SEQ:NODE:ROLE", "CONF:SEQ:NODE:ROLE 2,5", "CONF:SEQ:NODE:ROLE 2,5,DUT,",
    "CONF:SEQ:NODE:ROLE 2,5,DUT,1", "CONF:SEQ:NODE:ROLE -1,5,DUT",
    "CONF:SEQ:NODE:ROLE 2,4294967296,DUT", "CONF:SEQ:NODE:ROLE 2,5.0,DUT",
    "CONF:SEQ:NODE:ROLE 2,5,VNA", "CONF:SEQ:NODE:ROLE 2,9,VNA",
    "CONF:SEQ:NODE:ROLE 2,5,UNKNOWN", "READ:SEQ:NODE:ROLE? 5,1",
    "READ:SEQ:NODE:ROLE? 9", "READ:SEQ:NODE:ROLE?",
])
def test_invalid_role_command_preserves_staging_and_active(parser, bad):
    rows = run(parser, ["CONF:SEQ:NODE:ROLE 2,5,DUT", "READ:SEQ:NODE:ROLE? 5",
                        bad, "READ:SEQ:NODE:ROLE? 5"])
    assert rows[2][0] > 0
    assert rows[1] == rows[3]


@pytest.mark.parametrize("busy", ["@active", "@legacy"])
def test_role_configuration_frozen_until_idle(parser, busy):
    rows = run(parser, [busy, "CONF:SEQ:NODE:ROLE 2,5,DUT", "READ:SEQ:NODE:ROLE? 5",
                        "@idle", "CONF:SEQ:NODE:ROLE 2,5,DUT", "READ:SEQ:NODE:ROLE? 5"])
    assert rows[0][0] > 0
    assert rows[1][1][2:4] == ["0", "0"]
    assert rows[2] == (0, ["STAGED"])
    assert rows[3][1][2:4] == ["0", "1"]


def test_busy_command_slot_rejects_without_mutating_model(parser):
    rows = run(parser, ["@command_busy", "CONF:SEQ:NODE:ROLE 2,5,DUT",
                        "READ:SEQ:NODE:ROLE? 5", "@command_clear",
                        "CONF:SEQ:NODE:ROLE 2,5,DUT", "READ:SEQ:NODE:ROLE? 5"])
    assert rows[0][0] > 0
    assert rows[1][1][2:4] == ["0", "0"]
    assert rows[2] == (0, ["STAGED"])
    assert rows[3][1][2:4] == ["0", "1"]


@pytest.mark.parametrize("mode", ["LOOPBACK", "RJ45", "loopback", "rj45"])
def test_link_physical_loopback_alias_and_off_use_real_parser(parser, mode):
    rows = run(parser, [f"CONF:SEQ:LINK {mode},2,3,IN4,OUT2,25,5000,FALL",
                        "READ:SEQ:LINK?", "CONF:SEQ:LINK OFF", "READ:SEQ:LINK?"])
    assert rows[0] == rows[2] == (0, ["1"])
    assert rows[1][0] == rows[3][0] == 0
    assert len(rows[1][1]) == len(rows[3][1]) == 23
    assert rows[1][1][0] == "1"
    assert rows[1][1][14:21] == ["2", "3", "4", "2", "25", "5000", "1"]
    assert rows[3][1][0] == "0"
    assert rows[3][1][14:21] == ["0"] * 7


@pytest.mark.parametrize("bad", [
    "CONF:SEQ:LINK", "CONF:SEQ:LINK SOFTWARE,2,3,IN1,OUT4,10,5000,RIS",
    "CONF:SEQ:LINK OFF,1", "CONF:SEQ:LINK OFF,",
    "CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,5000",
    "CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,5000,RIS,1",
    "CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,5000,RIS,",
    "CONF:SEQ:LINK LOOPBACK,-1,3,IN1,OUT4,10,5000,RIS",
    "CONF:SEQ:LINK LOOPBACK,2.0,3,IN1,OUT4,10,5000,RIS",
    "CONF:SEQ:LINK LOOPBACK,2,4294967296,IN1,OUT4,10,5000,RIS",
    "CONF:SEQ:LINK LOOPBACK,2,3,IN5,OUT4,10,5000,RIS",
    "CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT5,10,5000,RIS",
    "CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,4294967296,RIS",
    "CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,5000,BOTH",
    "READ:SEQ:LINK? 1",
])
def test_link_strict_parameters_preserve_previous_configuration(parser, bad):
    rows = run(parser, ["CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,5000,RIS",
                        "READ:SEQ:LINK?", bad, "READ:SEQ:LINK?"])
    assert rows[0] == (0, ["1"])
    assert rows[2][0] > 0
    assert rows[1] == rows[3]


@pytest.mark.parametrize("busy", ["@active", "@link_reject"])
def test_link_frozen_or_owner_rejected_preserves_configuration(parser, busy):
    rows = run(parser, ["CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,5000,RIS",
                        "READ:SEQ:LINK?", busy, "CONF:SEQ:LINK OFF", "READ:SEQ:LINK?",
                        "@idle", "@link_accept", "CONF:SEQ:LINK OFF", "READ:SEQ:LINK?"])
    assert rows[2][0] > 0
    assert rows[1] == rows[3]
    assert rows[4] == (0, ["1"])
    assert rows[5][1][0] == "0"


def test_link_query_exact_23_field_wire_order(parser):
    rows = run(parser, ["CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,5000,RIS",
                        "@link_status", "READ:SEQ:LINK?"])
    assert rows[1] == (0, ["1", "8", "0", "101", "102", "103", "104", "7",
                           "48", "16", "2", "8", "8", "7", "2", "3", "1", "8",
                           "10", "5000", "0", "1", "105"])


def test_transport_query_unavailable_is_typed_response_not_scpi_error(parser):
    assert run(parser, ["READ:SEQ:LINK:TRANSPORT?"]) == [
        (0, ["0", "0", "0", "0", "0", "0", "0"])
    ]
