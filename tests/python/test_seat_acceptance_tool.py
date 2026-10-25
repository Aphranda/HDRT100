"""Seat acceptance tool: the read-only judge must be right about healthy and
degraded captures, must never talk to a port without an explicit mode, and the
device-syntax guard must reproduce the target build's own flags."""
import json
import os
import re
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

def _read_scpi_snapshot() -> str:
    """N5.0.4n: the snapshot command surface is split into four units."""
    return chr(10).join(
        (ROOT / "middleware/scpi_port/src" / name).read_text(encoding="utf-8")
        for name in ("scpi_system_snapshot_commands.c", "scpi_platform_snapshot_commands.c", "scpi_tdma_snapshot_commands.c", "scpi_node_snapshot_commands.c")
    )


sys.path.insert(0, str(ROOT))

from tools.sync_trigger.seat_acceptance import (  # noqa: E402
    COUNTERS, EXPECTED_FIELDS, HEALTH, POSITION, REASON_BITS, REASON_EVIDENCE,
    ROTATION, ROTATION_HOLD_ATTEMPTS, ROTATION_HOLD_DEPTH, ROTATION_SCHEDULE_ENABLED,
    ROTATION_WAIT_BUDGET, device_syntax_check, evaluate, find_compile_database,
    healthy_capture, main, self_test, syntax_command,
)


def test_builtin_cases_pass():
    result = self_test()
    assert result["passed"], result
    assert len(result["cases"]) == len(REASON_BITS) + 4


def test_declared_budgets_are_reported():
    """A capture must be readable without the firmware source: the page reports
    the declared limits the counters are evidence against."""
    verdict = evaluate(healthy_capture())
    assert any(entry["item"] == "declared budgets readable" and entry["status"] == "PASS"
               for entry in verdict["checks"]), verdict["checks"]


def test_starvation_without_the_window_schedule_is_rejected():
    """seat_starved_count can only move inside the seat-window gate, which is
    compiled out when the schedule flag is 0: a non-zero counter then means the
    firmware and this page disagree about the build."""
    capture = healthy_capture()
    capture[ROTATION][ROTATION_SCHEDULE_ENABLED] = 0
    capture[HEALTH][9] = 3
    verdict = evaluate(capture)
    assert not verdict["passed"]
    assert any(entry["item"] == "starvation implies the seat-window schedule"
               and entry["status"] == "FAIL" for entry in verdict["checks"])

    # With the gate compiled in, the same counters are legitimate evidence.
    capture[ROTATION][ROTATION_SCHEDULE_ENABLED] = 1
    verdict = evaluate(capture)
    assert any(entry["item"] == "starvation implies the seat-window schedule"
               and entry["status"] == "PASS" for entry in verdict["checks"])


def test_healthy_capture_passes():
    verdict = evaluate(healthy_capture())
    assert verdict["passed"], verdict["checks"]
    assert verdict["reason_mask"] == 0 and verdict["reasons"] == []


@pytest.mark.parametrize("bit,name", [(bit, name) for bit, name, _ in REASON_BITS])
def test_each_reason_bit_fails_with_its_own_evidence(bit, name):
    capture = healthy_capture()
    capture[HEALTH][1] = 1 << bit
    capture[HEALTH][0] = 0
    capture[HEALTH][REASON_EVIDENCE[name][0]] = 1
    verdict = evaluate(capture)
    assert not verdict["passed"]
    assert name in " ".join(verdict["reasons"])
    assert any(entry["item"] == f"evidence {name}" and entry["status"] == "PASS"
               for entry in verdict["checks"])


def test_counter_without_reason_bit_is_rejected():
    capture = healthy_capture()
    capture[HEALTH][REASON_EVIDENCE["DECODE_REJECT"][0]] = 2
    verdict = evaluate(capture)
    assert not verdict["passed"]
    assert any(entry["item"] == "evidence DECODE_REJECT" and entry["status"] == "FAIL"
               for entry in verdict["checks"])


def test_short_page_is_refused_not_guessed():
    capture = healthy_capture()
    capture[HEALTH] = capture[HEALTH][:-1]
    verdict = evaluate(capture)
    assert not verdict["passed"]
    assert verdict["reason_mask"] is None
    assert any(entry["item"].startswith("page-shape") and entry["status"] == "FAIL"
               for entry in verdict["checks"])


def test_starvation_evidence_is_reported():
    capture = healthy_capture()
    capture[COUNTERS][9], capture[COUNTERS][10] = 4, 1
    verdict = evaluate(capture)
    assert not verdict["passed"]
    assert any(entry["item"] == "starvation evidence" and entry["status"] == "FAIL"
               for entry in verdict["checks"])


def test_mode_is_required_so_no_port_is_touched_by_accident():
    with pytest.raises(SystemExit):
        main([])


def test_self_test_mode_exits_zero():
    assert main(["--self-test"]) == 0


def test_field_counts_match_the_firmware_pages():
    """EXPECTED_FIELDS is what every positional read depends on: derive it from
    the SCPI handlers so a page change cannot silently shift this tool."""
    source = _read_scpi_snapshot()

    def field_count(handler: str) -> int:
        start = source.index(handler)
        block = source[start:start + 6000]
        return block[:block.index("SCPI_RES_OK")].count("SCPI_ResultUInt32")

    assert EXPECTED_FIELDS[HEALTH] == field_count("scpi_cmd_system_tdma_seat_health_q")
    assert EXPECTED_FIELDS[COUNTERS] == field_count("scpi_cmd_system_tdma_seat_counters_q")
    assert EXPECTED_FIELDS[ROTATION] == field_count("scpi_cmd_system_tdma_seat_rotation_q")
    assert EXPECTED_FIELDS[POSITION] == field_count("scpi_cmd_refmem_sync_flight_position_q")

# --- device-syntax guard ---------------------------------------------------

def test_syntax_command_uses_arguments_verbatim():
    entry = {"arguments": ["cc", "-std=c11", "-O2", "-c", "src.c", "-o", "src.o"],
             "file": "src.c", "directory": "."}
    command = syntax_command(entry, "src.c")
    assert command[-1] == "-fsyntax-only"
    assert "-c" not in command and "-o" not in command
    assert "cc" in command and "-std=c11" in command


def test_syntax_command_keeps_windows_paths_and_macro_quotes():
    windows = (r'C:\toolchain\bin\arm-none-eabi-gcc.exe -DPICO_BOARD=\"pico2\" '
               r'-ID:/repo/components/tdma/inc -c D:/repo/src.c -o src.c.obj')
    entry = {"command": windows, "file": "D:/repo/src.c", "directory": "."}
    command = syntax_command(entry, "D:/repo/src.c")
    assert command[0] == r"C:\toolchain\bin\arm-none-eabi-gcc.exe"
    assert '-DPICO_BOARD="pico2"' in command
    assert "-ID:/repo/components/tdma/inc" in command
    assert not any(word.endswith(".obj") for word in command)
    assert command.count("D:/repo/src.c") == 1
    assert command[-1] == "-fsyntax-only"


def test_find_compile_database_reports_absence(tmp_path):
    assert find_compile_database(tmp_path) is None
    database = tmp_path / "compile_commands.json"
    database.write_text("[]", encoding="utf-8")
    assert find_compile_database(tmp_path) == database


def test_device_syntax_check_without_database_is_explicit(tmp_path):
    result = device_syntax_check(tmp_path)
    assert not result["passed"] and result["database"] is None
    assert "canonical device build" in result["errors"][0]


def test_device_syntax_check_compiles_with_host_flags(tmp_path):
    """The guard must be exercisable without the target toolchain: point the fake
    compile database at the host compiler and a dependency-free source."""
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        pytest.skip("no host compiler")
    source = tmp_path / "probe.c"
    source.write_text("int probe(void) { return 0; }\n", encoding="utf-8")
    database = tmp_path / "compile_commands.json"
    database.write_text(json.dumps([{
        "directory": str(tmp_path), "file": str(source),
        "command": f'"{compiler}" -std=c11 -Wall -Wextra -c "{source}" -o probe.o'}]),
        encoding="utf-8")
    result = device_syntax_check(tmp_path, ("probe.c",))
    assert result["passed"], result
    assert result["results"][0]["status"] == "PASS"


def test_device_syntax_check_reports_a_broken_header(tmp_path):
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        pytest.skip("no host compiler")
    (tmp_path / "broken.h").write_text("int value = NULL;\n", encoding="utf-8")
    source = tmp_path / "probe.c"
    source.write_text('#include "broken.h"\nint probe(void) { return value; }\n',
                      encoding="utf-8")
    database = tmp_path / "compile_commands.json"
    database.write_text(json.dumps([{
        "directory": str(tmp_path), "file": str(source),
        "command": f'"{compiler}" -std=c11 -Werror -c "{source}" -o probe.o'}]),
        encoding="utf-8")
    result = device_syntax_check(tmp_path, ("probe.c",))
    assert not result["passed"]
    assert result["results"][0]["status"] == "FAIL"


def test_preservation_budgets_have_one_value():
    """The seat's hold depth and retry budget are declared in the public header
    (so the SCPI page can report them) and repeated as a guarded fallback in the
    receiver translation unit (so host harnesses compile it alone). Two copies of
    one budget must not drift."""
    public = (ROOT / "components/distributed_refmem/inc/distributed_refmem.h").read_text(
        encoding="utf-8")
    receiver = (ROOT / "components/distributed_refmem/src/distributed_refmem_position_ack.c"
                ).read_text(encoding="utf-8")

    for macro in ("DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH",
                  "DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS"):
        values = []
        for text in (public, receiver):
            start = text.index("#define " + macro)
            values.append(text[start:start + 80].split()[2])
        assert values[0] == values[1], (macro, values)

def test_t2_page_tail_is_append_only():
    """Positional readers of TRIGger:RESERVation:T2? depend on new fields being
    APPENDED. Bind that contract to the handler, the way the seat pages are bound
    to their counts: the seed counters and their successors must stay in order at
    the end of the handler."""
    source = (ROOT / "middleware/scpi_port/src/scpi_trigger_t2_runtime_commands.c"
              ).read_text(encoding="utf-8")
    start = source.index("scpi_cmd_trigger_t2_q")
    block = source[start:source.index("SCPI_RES_OK", start)]
    tail = ["freeze_complete_mask", "freeze_abandoned_mask", "freeze_resend_count",
            "freeze_abandoned_count", "late_arrival_count",
            "late_last_current_ms", "late_last_expiry_sync",
            "s.descriptors[i].target_ms_index", "s.descriptors[i].expiry_sync"]
    positions = [block.index(name) for name in tail]
    assert positions == sorted(positions), list(zip(tail, positions))
