#!/usr/bin/env python3
"""Validate RP2350_TRIG SD-card firmware functions over SCPI.

This tool intentionally does not flash firmware. Use the project flashing
script or picotool command first, then run this validator against the USB CDC
port after the board has booted.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.sd_trace_decode.sd_trace_decode import decode_trace  # noqa: E402

BUILD_CACHE = ROOT / "build-rtos-multicore-smoke" / "CMakeCache.txt"

BASELINE_COMMANDS = (
    "*IDN?",
    "SYST:FW:BUILD?",
    "SYST:LOG:STAT?",
    "SYST:SD:STAT?",
    "SYST:SD:INFO?",
    "SYST:SD:INIT",
    "SYST:SD:MAN?",
    "SYST:STOR:STAT?",
)

CATALOG_COMMANDS = (
    ("root", "MMEM:CAT?"),
    ("update", 'MMEM:CAT? "/update"'),
    ("profile", 'MMEM:CAT? "/profile"'),
    ("mission", 'MMEM:CAT? "/mission"'),
    ("cal", 'MMEM:CAT? "/cal"'),
    ("reports", 'MMEM:CAT? "/reports"'),
)

SYSTEM_PACK_FILE_COMMANDS = (
    ("manifest_json", 'MMEM:INFO? "/manifest.json"', "/manifest.json"),
    ("manifest_idx", 'MMEM:INFO? "/manifest.idx"', "/manifest.idx"),
    ("update_package", 'MMEM:INFO? "/update/DHRT100_UPDATE.pkg"', "/update/DHRT100_UPDATE.pkg"),
    ("active_profile", 'MMEM:INFO? "/profile/active.json"', "/profile/active.json"),
    ("mission_recipe", 'MMEM:INFO? "/mission/recipe.json"', "/mission/recipe.json"),
    ("mission_node_map", 'MMEM:INFO? "/mission/node_map.json"', "/mission/node_map.json"),
    ("board_cal", 'MMEM:INFO? "/cal/board_cal.json"', "/cal/board_cal.json"),
)

RESOURCE_PIO1 = 1 << 4
RESOURCE_PIO2 = 1 << 5
RESOURCE_DMA = 1 << 6
RESOURCE_AUX = 1 << 9
RESOURCE_SEQ_STEP = RESOURCE_PIO1 | RESOURCE_DMA
RESOURCE_ENC_COUNT = RESOURCE_PIO1 | RESOURCE_DMA
RESOURCE_BISS_TAP = RESOURCE_PIO2 | RESOURCE_AUX

STORAGE_JOB_INFO_KEY = 'SYST:STOR:JOB:INFO "/manifest.idx"'
STORAGE_JOB_QUERY_PREFIX = "SYST:STOR:JOB?"
INIT_JOB_QUERY_KEY = "INIT:SYST:STOR:JOB?"
MANIFEST_JOB_QUERY_KEY = "MAN:SYST:STOR:JOB?"
READ_JOB_QUERY_KEY = "READ:SYST:STOR:JOB?"
CATALOG_JOB_QUERY_KEY = "PAGE:SYST:STOR:JOB?"


def build_uses_multicore() -> bool:
    try:
        text = BUILD_CACHE.read_text(encoding="utf-8")
    except OSError:
        return False
    return "PROJECT_USE_MULTICORE:BOOL=ON" in text


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM4")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0, help="seconds to wait after opening the port")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    parser.add_argument("--skip-negative-path", action="store_true", help="do not check path-denial behavior")
    parser.add_argument("--skip-snapshot", action="store_true", help="do not write and validate a boot snapshot")
    parser.add_argument("--skip-arm-snapshot", action="store_true", help="do not trigger and validate ARM snapshot")
    parser.add_argument("--skip-fault-snapshot", action="store_true", help="do not trigger and validate FAULT snapshot")
    parser.add_argument("--validate-trigger-release", action="store_true",
                        help="exercise RESET/FAULT while SEQ_STEP is armed and require resource_release trace records")
    parser.add_argument("--validate-resource-owner", action="store_true",
                        help="arm/disarm SEQ, ENC and BISS, then assert SYST:RES? active resource masks")
    return parser.parse_args()


def normalize_scpi_line(line: str) -> str:
    normalized = line.strip()
    # Diagnostics and SCPI share USB CDC.  If a log record is appended to a
    # response without an intervening newline, retain the SCPI prefix only.
    log_start = normalized.find("[", 1)
    if log_start > 0:
        normalized = normalized[:log_start].rstrip()
    return normalized


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"') else line
    if maybe_log.startswith("[") or maybe_log.startswith("log:"):
        return True
    # A read can begin in the middle of a diagnostic record after the opening
    # '[' was consumed together with a preceding SCPI response.
    close = maybe_log.find("]")
    if close > 0 and maybe_log[:close].strip().isdigit():
        tail = maybe_log[close + 1:].lstrip()
        return tail.startswith(("DBG ", "INF ", "WRN ", "ERR "))
    return False


def is_log_or_ack(line: str) -> bool:
    return not line or is_log_line(line) or line in ('"OK"', "OK", "1")


def strip_leading_ack(line: str) -> str:
    if line in ('"OK"', "OK", "1"):
        return line
    for prefix in ('OK""', '"OK""'):
        if line.startswith(prefix):
            return '"' + line[len(prefix):].strip()
    if line.startswith('OK"') and not line.startswith('OK",'):
        return line[2:].strip()
    return line


def read_scpi_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = normalize_scpi_line(raw.decode("utf-8", errors="replace"))
        line = strip_leading_ack(line)
        if not is_log_or_ack(line):
            return line
    return "<timeout>"


def read_any_scpi_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = normalize_scpi_line(raw.decode("utf-8", errors="replace"))
        if not line or is_log_line(line):
            continue
        if line.startswith('"OK"') or line.startswith("OK"):
            return '"OK"'
        if line == "1":
            return "1"
        if line:
            return line
    return "<timeout>"


def ack_accepted(response: str) -> bool:
    return response in ('"OK"', "OK", "1")


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_line(ser, timeout_s)


def command_ack(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_any_scpi_line(ser, timeout_s)


def command_no_response(ser: serial.Serial, command: str) -> str:
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return "<sent>"


def parse_csv_response(response: str) -> list[str]:
    return next(csv.reader([response], skipinitialspace=True))


def trigger_state(response: str) -> int:
    fields = parse_csv_response(response)
    if len(fields) < 2:
        return -1
    try:
        return int(fields[1], 0)
    except ValueError:
        return -1


def trigger_error(response: str) -> int:
    fields = parse_csv_response(response)
    if len(fields) < 9:
        return -1
    try:
        return int(fields[8], 0)
    except ValueError:
        return -1


def resource_active_mask(response: str) -> int:
    fields = parse_csv_response(response)
    if not fields:
        return -1
    try:
        return int(fields[0], 0)
    except ValueError:
        return -1


def resource_contains(response: str, expected_mask: int) -> bool:
    active = resource_active_mask(response)
    return active >= 0 and (active & expected_mask) == expected_mask


def resource_released(response: str, released_mask: int) -> bool:
    active = resource_active_mask(response)
    return active >= 0 and (active & released_mask) == 0


def wait_until_trigger_state(ser: serial.Serial,
                             state: int,
                             timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    last = ""
    while time.monotonic() < deadline:
        last = query(ser, "STAT:TRIG?", min(0.5, timeout_s))
        if trigger_state(last) == state:
            return last
        time.sleep(0.05)
    return last or "<timeout>"


def catalog_entries(response: str) -> dict[str, tuple[int, str]]:
    fields = parse_csv_response(response)
    if len(fields) < 2 or fields[0] != "OK":
        return {}
    entries: dict[str, tuple[int, str]] = {}
    for item in fields[1].split(";"):
        if not item:
            continue
        parts = item.split(",")
        if len(parts) != 3:
            continue
        name, size_text, kind = parts
        try:
            size = int(size_text, 0)
        except ValueError:
            size = 0
        entries[name] = (size, kind)
    return entries


def catalog_complete(response: str) -> bool:
    fields = parse_csv_response(response)
    return len(fields) >= 2 and fields[0] == "OK" and fields[1].endswith(";")


def catalog_page_entries(response: str) -> dict[str, tuple[int, str]]:
    fields = parse_csv_response(response)
    if len(fields) < 8 or fields[0] != "OK":
        return {}
    entries: dict[str, tuple[int, str]] = {}
    for item in fields[7].split(";"):
        if not item or item == "EMPTY":
            continue
        parts = item.split(",")
        if len(parts) != 3:
            continue
        name, size_text, kind = parts
        try:
            size = int(size_text, 0)
        except ValueError:
            size = 0
        entries[name] = (size, kind)
    return entries


def path_from_response(response: str, index: int) -> str:
    fields = parse_csv_response(response)
    if len(fields) <= index:
        return ""
    return fields[index]


def quote_path(path: str) -> str:
    return '"' + path.replace('"', '') + '"'


def expect(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def expect_file_info(results: dict[str, str],
                     key: str,
                     expected_path: str,
                     expected_kind: str,
                     failures: list[str],
                     min_size: int = 1) -> None:
    if key not in results:
        failures.append(f"{key} was not queried")
        return
    fields = parse_csv_response(results[key])
    expect(len(fields) >= 6, failures, f"{key} returned too few fields")
    if len(fields) >= 6:
        expect(fields[0] == "OK", failures, f"{key} status is {fields[0]!r}, expected OK")
        expect(fields[1] == expected_path, failures, f"{key} path is {fields[1]!r}, expected {expected_path!r}")
        expect(int(fields[2], 0) >= min_size,
               failures,
               f"{key} size is {fields[2]!r}, expected >= {min_size}")
        expect(fields[3] == expected_kind, failures, f"{key} kind is {fields[3]!r}, expected {expected_kind}")
        expect(int(fields[4], 0) != 0, failures, f"{key} path hash is zero")
        expect(fields[5] == "0", failures, f"{key} error is {fields[5]!r}, expected 0")


def trace_has_release(decoded: dict[str, Any],
                      trigger_event_name: str,
                      before_state_name: str,
                      required_resource_names: set[str]) -> bool:
    for record in decoded.get("records", []):
        if record.get("event_name") != "trigger.resource_release":
            continue
        details = record.get("details", {})
        if details.get("trigger_event_name") != trigger_event_name:
            continue
        if details.get("before_state_name") != before_state_name:
            continue
        released = set(details.get("released_resource_names", []))
        if required_resource_names.issubset(released):
            return True
    return False


def file_info_size(response: str) -> int:
    fields = parse_csv_response(response)
    if len(fields) < 6 or fields[0] != "OK" or fields[3] != "FILE":
        return 0
    try:
        return int(fields[2], 0)
    except ValueError:
        return 0


def read_file_via_scpi(ser: serial.Serial,
                       path: str,
                       expected_size: int,
                       timeout_s: float,
                       results: dict[str, str],
                       key_prefix: str,
                       chunk_size: int = 128) -> bytes:
    data = bytearray()
    for _ in range(64):
        if expected_size > 0 and len(data) >= expected_size:
            break
        command = f"MMEM:READ? {quote_path(path)},{len(data)},{chunk_size}"
        response = query(ser, command, timeout_s)
        results[f"{key_prefix}:{command}"] = response
        fields = parse_csv_response(response)
        if len(fields) < 9 or fields[0] != "OK":
            break
        try:
            returned = int(fields[4], 0)
            offset = int(fields[2], 0)
        except ValueError:
            break
        if offset != len(data) or returned < 0:
            break
        if returned == 0:
            break
        chunk = bytes.fromhex(fields[8])
        if len(chunk) != returned:
            break
        data.extend(chunk)
        if fields[5] == "1":
            break
        time.sleep(0.05)
    return bytes(data)


def expect_catalog_page(results: dict[str, str],
                        key: str,
                        expected_path: str,
                        expected_offset: int,
                        failures: list[str]) -> int:
    if key not in results:
        failures.append(f"{key} was not queried")
        return 0
    fields = parse_csv_response(results[key])
    expect(len(fields) >= 8, failures, f"{key} returned too few fields")
    if len(fields) < 8:
        return 0
    expect(fields[0] == "OK", failures, f"{key} status is {fields[0]!r}, expected OK")
    expect(fields[1] == expected_path, failures, f"{key} path is {fields[1]!r}, expected {expected_path!r}")
    expect(int(fields[2], 0) == expected_offset,
           failures,
           f"{key} offset is {fields[2]!r}, expected {expected_offset}")
    returned_count = int(fields[3], 0)
    next_offset = int(fields[4], 0)
    complete = fields[5] == "1"
    truncated = fields[6] == "1"
    expect(returned_count <= 4, failures, f"{key} returned_count is {returned_count}, expected <= 4")
    expect(not truncated, failures, f"{key} page was truncated")
    if complete:
        expect(next_offset == 0, failures, f"{key} complete page has next_offset {next_offset}, expected 0")
    else:
        expect(next_offset > expected_offset, failures, f"{key} next_offset did not advance")
    return next_offset


def page_key_offset(key: str) -> int:
    try:
        return int(key.rsplit(",", 2)[1], 0)
    except (IndexError, ValueError):
        return 0


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def run_resource_owner_checks(ser: serial.Serial,
                              results: dict[str, str],
                              timeout_s: float) -> None:
    results["OWNER:*RST"] = command_no_response(ser, "*RST")
    time.sleep(0.3)
    results["OWNER:RESET:STAT:TRIG?"] = wait_until_trigger_state(ser, 0, timeout_s)
    results["OWNER:IDLE:SYST:RES?"] = query(ser, "SYST:RES?", timeout_s)

    results["OWNER:SEQ:TRIG:MODE 1"] = command_ack(ser, "TRIG:MODE 1", timeout_s)
    time.sleep(0.2)
    results["OWNER:SEQ:TRIG:ARM"] = command_ack(ser, "TRIG:ARM", timeout_s)
    time.sleep(0.2)
    results["OWNER:SEQ:ARMED:STAT:TRIG?"] = wait_until_trigger_state(ser, 2, timeout_s)
    results["OWNER:SEQ:ARMED:SYST:RES?"] = query(ser, "SYST:RES?", timeout_s)
    results["OWNER:SEQ:TRIG:DISarm"] = command_ack(ser, "TRIG:DISarm", timeout_s)
    time.sleep(0.2)
    results["OWNER:SEQ:DISARMED:SYST:RES?"] = query(ser, "SYST:RES?", timeout_s)

    results["OWNER:ENC:TRIG:ENC:TARG 4"] = command_ack(ser, "TRIG:ENC:TARG 4", timeout_s)
    time.sleep(0.1)
    results["OWNER:ENC:TRIG:MODE 2"] = command_ack(ser, "TRIG:MODE 2", timeout_s)
    time.sleep(0.2)
    results["OWNER:ENC:TRIG:ARM"] = command_ack(ser, "TRIG:ARM", timeout_s)
    time.sleep(0.2)
    results["OWNER:ENC:ARMED:STAT:TRIG?"] = wait_until_trigger_state(ser, 4, timeout_s)
    results["OWNER:ENC:ARMED:SYST:RES?"] = query(ser, "SYST:RES?", timeout_s)
    results["OWNER:ENC:TRIG:DISarm"] = command_ack(ser, "TRIG:DISarm", timeout_s)
    time.sleep(0.2)
    results["OWNER:ENC:DISARMED:SYST:RES?"] = query(ser, "SYST:RES?", timeout_s)

    biss_config = [
        "TRIG:BISS:ROLE 0",
        "TRIG:BISS:DEV 0",
        "TRIG:BISS:CLOC 1000000",
        "TRIG:BISS:FBIT 48",
        "TRIG:BISS:POFF 8",
        "TRIG:BISS:PBIT 24",
        "TRIG:BISS:PMOD 16777216",
        "TRIG:BISS:TARG 100",
        "TRIG:BISS:SAMP:EDGE 0",
        "TRIG:BISS:SAMP:DEL 8",
        "TRIG:BISS:TIME 1000000",
        "TRIG:BISS:ANCH:BITS 0",
        "TRIG:BISS:ERR:BIT 4294967295",
        "TRIG:BISS:WARN:BIT 4294967295",
        "TRIG:BISS:STAT:GATE 0",
        "TRIG:BISS:CRC:BITS 0",
        "TRIG:BISS:CRC:COV:BITS 0",
        "TRIG:BISS:CRC:GATE 0",
        "TRIG:BISS:SAMP:SCAN 0",
    ]
    for command in biss_config:
        results[f"OWNER:BISS:{command}"] = command_ack(ser, command, timeout_s)
        time.sleep(0.05)
    results["OWNER:BISS:TRIG:MODE 3"] = command_ack(ser, "TRIG:MODE 3", timeout_s)
    time.sleep(0.2)
    results["OWNER:BISS:TRIG:ARM"] = command_ack(ser, "TRIG:ARM", timeout_s)
    time.sleep(0.2)
    results["OWNER:BISS:ARMED:STAT:TRIG?"] = wait_until_trigger_state(ser, 7, timeout_s)
    results["OWNER:BISS:ARMED:SYST:RES?"] = query(ser, "SYST:RES?", timeout_s)
    results["OWNER:BISS:TRIG:DISarm"] = command_ack(ser, "TRIG:DISarm", timeout_s)
    time.sleep(0.2)
    results["OWNER:BISS:DISARMED:SYST:RES?"] = query(ser, "SYST:RES?", timeout_s)


def run_queries(port: str,
                baud: int,
                timeout_s: float,
                settle_s: float,
                out_dir: Path,
                commands: Iterable[str],
                validate_snapshot: bool,
                validate_arm_snapshot: bool,
                validate_fault_snapshot: bool,
                validate_trigger_release: bool,
                validate_resource_owner: bool) -> dict[str, str]:
    results: dict[str, str] = {}
    with serial.Serial(port, baud, timeout=0.1, write_timeout=timeout_s) as ser:
        time.sleep(settle_s)
        ser.reset_input_buffer()
        for command in commands:
            results[command] = query(ser, command, timeout_s)
            time.sleep(0.1)
            if command == "SYST:SD:INIT":
                results[INIT_JOB_QUERY_KEY] = query(ser, "SYST:STOR:JOB?", timeout_s)
                time.sleep(0.1)
            if command == "SYST:SD:MAN?":
                results[MANIFEST_JOB_QUERY_KEY] = query(ser, "SYST:STOR:JOB?", timeout_s)
                time.sleep(0.1)
        results[STORAGE_JOB_INFO_KEY] = query(ser, 'SYST:STOR:JOB:INFO "/manifest.idx"', timeout_s)
        time.sleep(0.1)
        for attempt in range(8):
            key = f"{STORAGE_JOB_QUERY_PREFIX}:{attempt}"
            results[key] = query(ser, "SYST:STOR:JOB?", timeout_s)
            fields = parse_csv_response(results[key])
            if fields and fields[0] in ("DONE", "FAILED"):
                break
            time.sleep(0.1)
        if validate_resource_owner:
            run_resource_owner_checks(ser, results, timeout_s)
        if validate_snapshot:
            results["AUTO:SYST:SNAP:LAST?"] = query(ser, "SYST:SNAP:LAST?", timeout_s)
            time.sleep(0.1)
            results['SYST:SNAP:WRIT "boot"'] = command_ack(ser, 'SYST:SNAP:WRIT "boot"', timeout_s)
            time.sleep(0.2)
            results["SNAP:SYST:STOR:JOB?"] = query(ser, "SYST:STOR:JOB?", timeout_s)
            time.sleep(0.1)
            results["SYST:SNAP:LAST?"] = query(ser, "SYST:SNAP:LAST?", timeout_s)
            time.sleep(0.1)
            boot_path = path_from_response(results["SYST:SNAP:LAST?"], 3)
            if boot_path:
                results["INFO:SYST:SNAP:LAST?"] = query(ser, f"MMEM:INFO? {quote_path(boot_path)}", timeout_s)
                time.sleep(0.1)
            results['MMEM:CAT? "/snapshots/boot"'] = query(ser, 'MMEM:CAT? "/snapshots/boot"', timeout_s)
        if validate_arm_snapshot:
            results["TRIG:SOUR 17"] = command_ack(ser, "TRIG:SOUR 17", timeout_s)
            time.sleep(0.1)
            results["TRIG:SOUR 16"] = command_ack(ser, "TRIG:SOUR 16", timeout_s)
            time.sleep(0.1)
            results["TRIG:EDGE 1"] = command_ack(ser, "TRIG:EDGE 1", timeout_s)
            time.sleep(0.1)
            results["TRIG:EDGE 0"] = command_ack(ser, "TRIG:EDGE 0", timeout_s)
            time.sleep(0.1)
            results["TRIG:GATE 1"] = command_ack(ser, "TRIG:GATE 1", timeout_s)
            time.sleep(0.1)
            results["TRIG:GATE 0"] = command_ack(ser, "TRIG:GATE 0", timeout_s)
            time.sleep(0.1)
            results["TRIG:SAFE 1"] = command_ack(ser, "TRIG:SAFE 1", timeout_s)
            time.sleep(0.1)
            results["TRIG:SAFE 0"] = command_ack(ser, "TRIG:SAFE 0", timeout_s)
            time.sleep(0.1)
            results["TRIG:MODE 1"] = command_ack(ser, "TRIG:MODE 1", timeout_s)
            time.sleep(0.3)
            results["SEQ:TRIG:MODE?"] = query(ser, "TRIG:MODE?", timeout_s)
            time.sleep(0.1)
            results["TRIG:ARM"] = command_ack(ser, "TRIG:ARM", timeout_s)
            time.sleep(0.3)
            results["ARM:SYST:STOR:JOB?"] = query(ser, "SYST:STOR:JOB?", timeout_s)
            time.sleep(0.1)
            results["ARM:STAT:TRIG?"] = query(ser, "STAT:TRIG?", timeout_s)
            time.sleep(0.1)
            results["ARM:SYST:SNAP:LAST?"] = query(ser, "SYST:SNAP:LAST?", timeout_s)
            time.sleep(0.1)
            arm_path = path_from_response(results["ARM:SYST:SNAP:LAST?"], 3)
            results["TRIG:DISarm"] = command_ack(ser, "TRIG:DISarm", timeout_s)
            time.sleep(0.2)
            if arm_path:
                results["INFO:ARM:SYST:SNAP:LAST?"] = query(ser, f"MMEM:INFO? {quote_path(arm_path)}", timeout_s)
                time.sleep(0.1)
            results['MMEM:CAT? "/snapshots/arm"'] = query(ser, 'MMEM:CAT? "/snapshots/arm"', timeout_s)
        if validate_fault_snapshot:
            if validate_trigger_release and validate_arm_snapshot:
                results["RESET:TRIG:MODE 1"] = command_ack(ser, "TRIG:MODE 1", timeout_s)
                time.sleep(0.2)
                results["RESET:TRIG:ARM"] = command_ack(ser, "TRIG:ARM", timeout_s)
                time.sleep(0.2)
                results["RESET:ARMED:STAT:TRIG?"] = wait_until_trigger_state(ser, 2, timeout_s)
                results["*RST"] = command_no_response(ser, "*RST")
                time.sleep(0.2)
                results["RESET:STAT:TRIG?"] = wait_until_trigger_state(ser, 0, timeout_s)
                results["FAULT:TRIG:MODE 1"] = command_ack(ser, "TRIG:MODE 1", timeout_s)
                time.sleep(0.2)
                results["FAULT:TRIG:ARM"] = command_ack(ser, "TRIG:ARM", timeout_s)
                time.sleep(0.2)
                results["FAULT:ARMED:STAT:TRIG?"] = wait_until_trigger_state(ser, 2, timeout_s)
            results["TRIG:FAULT"] = command_ack(ser, "TRIG:FAULT", timeout_s)
            time.sleep(0.2)
            results["FAULT:STAT:TRIG?"] = wait_until_trigger_state(ser, 5, timeout_s)
            results["FAULT:SYST:STOR:JOB?"] = query(ser, "SYST:STOR:JOB?", timeout_s)
            time.sleep(0.1)
            results["FAULT:SYST:SNAP:LAST?"] = query(ser, "SYST:SNAP:LAST?", timeout_s)
            time.sleep(0.1)
            fault_snapshot_path = path_from_response(results["FAULT:SYST:SNAP:LAST?"], 3)
            if fault_snapshot_path:
                results["INFO:FAULT:SYST:SNAP:LAST?"] = query(ser, f"MMEM:INFO? {quote_path(fault_snapshot_path)}", timeout_s)
                time.sleep(0.1)
            results["FAULT:SYST:TRAC:LAST?"] = query(ser, "SYST:TRAC:LAST?", timeout_s)
            time.sleep(0.1)
            fault_trace_path = path_from_response(results["FAULT:SYST:TRAC:LAST?"], 3)
            if fault_trace_path:
                results["INFO:FAULT:SYST:TRAC:LAST?"] = query(ser, f"MMEM:INFO? {quote_path(fault_trace_path)}", timeout_s)
                idx_path = fault_trace_path[:-4] + ".idx" if fault_trace_path.endswith(".bin") else ""
                if idx_path:
                    time.sleep(0.1)
                    results["INFO:FAULT:SYST:TRAC:IDX?"] = query(ser, f"MMEM:INFO? {quote_path(idx_path)}", timeout_s)
                time.sleep(0.1)
                page_offset = 0
                for _ in range(32):
                    page_key = f'PAGE:MMEM:CAT:PAGE? "/traces/fault",{page_offset},4'
                    results[page_key] = query(ser,
                                              f'MMEM:CAT:PAGE? "/traces/fault",{page_offset},4',
                                              timeout_s)
                    fields = parse_csv_response(results[page_key])
                    if len(fields) < 8 or fields[0] != "OK" or fields[5] == "1":
                        break
                    try:
                        next_offset = int(fields[4], 0)
                    except ValueError:
                        break
                    if next_offset <= page_offset:
                        break
                    page_offset = next_offset
                    time.sleep(0.1)
                results[CATALOG_JOB_QUERY_KEY] = query(ser, "SYST:STOR:JOB?", timeout_s)
                time.sleep(0.1)
                trace_size = file_info_size(results.get("INFO:FAULT:SYST:TRAC:LAST?", ""))
                idx_size = file_info_size(results.get("INFO:FAULT:SYST:TRAC:IDX?", ""))
                trace_dir = out_dir / "trace_readback"
                if trace_size > 0:
                    trace_data = read_file_via_scpi(ser,
                                                    fault_trace_path,
                                                    trace_size,
                                                    timeout_s,
                                                    results,
                                                    "READ:FAULT:SYST:TRAC:LAST?")
                    trace_out = trace_dir / Path(fault_trace_path).name
                    trace_out.parent.mkdir(parents=True, exist_ok=True)
                    trace_out.write_bytes(trace_data)
                    results["READBACK:FAULT:SYST:TRAC:LAST?"] = f"OK,{fault_trace_path},{trace_size},{len(trace_data)},{trace_out}"
                if idx_path and idx_size > 0:
                    idx_data = read_file_via_scpi(ser,
                                                  idx_path,
                                                  idx_size,
                                                  timeout_s,
                                                  results,
                                                  "READ:FAULT:SYST:TRAC:IDX?")
                    idx_out = trace_dir / Path(idx_path).name
                    idx_out.parent.mkdir(parents=True, exist_ok=True)
                    idx_out.write_bytes(idx_data)
                    results["READBACK:FAULT:SYST:TRAC:IDX?"] = f"OK,{idx_path},{idx_size},{len(idx_data)},{idx_out}"
                results[READ_JOB_QUERY_KEY] = query(ser, "SYST:STOR:JOB?", timeout_s)
            results["FAULT:SYST:FAULT:LAST?"] = query(ser, "SYST:FAULT:LAST?", timeout_s)
            time.sleep(0.1)
            fault_report_path = path_from_response(results["FAULT:SYST:FAULT:LAST?"], 2)
            if fault_report_path:
                results["INFO:FAULT:SYST:FAULT:LAST?"] = query(ser, f"MMEM:INFO? {quote_path(fault_report_path)}", timeout_s)
                time.sleep(0.1)
            results['MMEM:CAT? "/snapshots/fault"'] = query(ser, 'MMEM:CAT? "/snapshots/fault"', timeout_s)
            time.sleep(0.1)
            results['MMEM:CAT? "/traces/fault"'] = query(ser, 'MMEM:CAT? "/traces/fault"', timeout_s)
            time.sleep(0.1)
            results['MMEM:CAT? "/reports/fault"'] = query(ser, 'MMEM:CAT? "/reports/fault"', timeout_s)
            results["FAULT:TRIG:DISarm"] = command_ack(ser, "TRIG:DISarm", timeout_s)
    return results


def main() -> int:
    args = parse_args()
    started = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = (args.out_dir or ROOT / "build" / f"sd_validation_{started}").resolve()
    multicore_trace = build_uses_multicore()

    failures: list[str] = []
    system_pack_failures: list[str] = []
    trigger_evidence_failures: list[str] = []
    commands = (list(BASELINE_COMMANDS) +
                [command for _, command in CATALOG_COMMANDS] +
                [command for _, command, _ in SYSTEM_PACK_FILE_COMMANDS])
    if not args.skip_negative_path:
        commands.append('MMEM:CAT? "/../"')
        commands.append('MMEM:INFO? "/../"')
        commands.append('MMEM:CAT:PAGE? "/../",0,4')
        commands.append('MMEM:READ? "/../",0,16')

    results = run_queries(args.port,
                          args.baud,
                          args.timeout,
                          args.settle,
                          out_dir,
                          commands,
                          validate_snapshot=not args.skip_snapshot,
                          validate_arm_snapshot=(not args.skip_snapshot and not args.skip_arm_snapshot),
                          validate_fault_snapshot=(not args.skip_snapshot and not args.skip_fault_snapshot),
                          validate_trigger_release=args.validate_trigger_release,
                          validate_resource_owner=args.validate_resource_owner)

    if args.validate_resource_owner:
        expect(trigger_state(results.get("OWNER:RESET:STAT:TRIG?", "")) == 0,
               failures,
               f"resource-owner reset did not return IDLE: {results.get('OWNER:RESET:STAT:TRIG?')!r}")
        expect(resource_released(results.get("OWNER:IDLE:SYST:RES?", ""),
                                 RESOURCE_SEQ_STEP | RESOURCE_BISS_TAP),
               failures,
               f"resource-owner idle resources not released: {results.get('OWNER:IDLE:SYST:RES?')!r}")
        expect(ack_accepted(results.get("OWNER:SEQ:TRIG:MODE 1", "")),
               failures,
               f"resource-owner SEQ mode was not accepted: {results.get('OWNER:SEQ:TRIG:MODE 1')!r}")
        expect(results.get("OWNER:SEQ:TRIG:ARM") == '"OK"',
               failures,
               f"resource-owner SEQ ARM did not return OK: {results.get('OWNER:SEQ:TRIG:ARM')!r}")
        expect(trigger_state(results.get("OWNER:SEQ:ARMED:STAT:TRIG?", "")) == 2,
               failures,
               f"resource-owner SEQ did not reach SEQ_ARMED: {results.get('OWNER:SEQ:ARMED:STAT:TRIG?')!r}")
        expect(resource_contains(results.get("OWNER:SEQ:ARMED:SYST:RES?", ""),
                                 RESOURCE_SEQ_STEP),
               failures,
               f"resource-owner SEQ active resources do not include PIO1/DMA: {results.get('OWNER:SEQ:ARMED:SYST:RES?')!r}")
        expect(resource_released(results.get("OWNER:SEQ:DISARMED:SYST:RES?", ""),
                                 RESOURCE_SEQ_STEP),
               failures,
               f"resource-owner SEQ resources not released: {results.get('OWNER:SEQ:DISARMED:SYST:RES?')!r}")

        expect(ack_accepted(results.get("OWNER:ENC:TRIG:MODE 2", "")),
               failures,
               f"resource-owner ENC mode was not accepted: {results.get('OWNER:ENC:TRIG:MODE 2')!r}")
        expect(results.get("OWNER:ENC:TRIG:ARM") == '"OK"',
               failures,
               f"resource-owner ENC ARM did not return OK: {results.get('OWNER:ENC:TRIG:ARM')!r}")
        expect(trigger_state(results.get("OWNER:ENC:ARMED:STAT:TRIG?", "")) == 4,
               failures,
               f"resource-owner ENC did not reach ENC_ARMED: {results.get('OWNER:ENC:ARMED:STAT:TRIG?')!r}")
        expect(resource_contains(results.get("OWNER:ENC:ARMED:SYST:RES?", ""),
                                 RESOURCE_ENC_COUNT),
               failures,
               f"resource-owner ENC active resources do not include PIO1/DMA: {results.get('OWNER:ENC:ARMED:SYST:RES?')!r}")
        expect(resource_released(results.get("OWNER:ENC:DISARMED:SYST:RES?", ""),
                                 RESOURCE_ENC_COUNT),
               failures,
               f"resource-owner ENC resources not released: {results.get('OWNER:ENC:DISARMED:SYST:RES?')!r}")

        expect(ack_accepted(results.get("OWNER:BISS:TRIG:MODE 3", "")),
               failures,
               f"resource-owner BISS mode was not accepted: {results.get('OWNER:BISS:TRIG:MODE 3')!r}")
        expect(results.get("OWNER:BISS:TRIG:ARM") == '"OK"',
               failures,
               f"resource-owner BISS ARM did not return OK: {results.get('OWNER:BISS:TRIG:ARM')!r}")
        expect(trigger_state(results.get("OWNER:BISS:ARMED:STAT:TRIG?", "")) == 7,
               failures,
               f"resource-owner BISS did not reach BISS_ARMED: {results.get('OWNER:BISS:ARMED:STAT:TRIG?')!r}")
        expect(resource_contains(results.get("OWNER:BISS:ARMED:SYST:RES?", ""),
                                 RESOURCE_BISS_TAP),
               failures,
               f"resource-owner BISS active resources do not include PIO2/AUX: {results.get('OWNER:BISS:ARMED:SYST:RES?')!r}")
        expect(resource_released(results.get("OWNER:BISS:DISARMED:SYST:RES?", ""),
                                 RESOURCE_BISS_TAP),
               failures,
               f"resource-owner BISS resources not released: {results.get('OWNER:BISS:DISARMED:SYST:RES?')!r}")

    log_status = parse_csv_response(results["SYST:LOG:STAT?"])
    expect(len(log_status) >= 3, failures, "SYST:LOG:STAT? returned too few fields")
    if len(log_status) >= 3:
        expect(log_status[0] in ("DEBUG", "INFO", "WARN", "ERROR"),
               failures,
               f"log level is {log_status[0]!r}")
        try:
            level_value = int(log_status[1], 0)
        except ValueError:
            level_value = 0xFFFFFFFF
        expect(level_value <= 3, failures, f"log level value is {log_status[1]!r}")
        # Current firmware appends last_log_path after all numeric counters.
        # Older firmware has no path field, so only exclude the tail when the
        # expanded 33-field schema is present.
        numeric_end = len(log_status) - 1 if len(log_status) >= 33 else len(log_status)
        for index, field in enumerate(log_status[2:numeric_end], start=2):
            try:
                int(field, 0)
            except ValueError:
                failures.append(f"SYST:LOG:STAT? field {index} is not numeric: {field!r}")
        if len(log_status) >= 33:
            expect(log_status[-1] == "" or log_status[-1].startswith("/logs/"),
                   failures,
                   f"SYST:LOG:STAT? last_log_path is invalid: {log_status[-1]!r}")

    status = parse_csv_response(results["SYST:SD:STAT?"])
    expect(len(status) >= 5, failures, "SYST:SD:STAT? returned too few fields")
    sd_ready = False
    if len(status) >= 5:
        expect(status[0] == "CARD_READY", failures, f"SD state is {status[0]!r}, expected CARD_READY")
        expect(status[1] == "1", failures, "card_present is not true")
        expect(status[2] == "1", failures, "fs_mounted is not true")
        expect(status[3] == "OK", failures, f"card status is {status[3]!r}, expected OK")
        expect(status[4] == "0", failures, f"storage_error is {status[4]!r}, expected 0")
        sd_ready = status[0] == "CARD_READY" and status[1] == "1" and status[2] == "1" and status[3] == "OK"

    info = parse_csv_response(results["SYST:SD:INFO?"])
    expect(len(info) >= 8, failures, "SYST:SD:INFO? returned too few fields")
    if len(info) >= 8:
        expect(info[0] == "CARD_READY", failures, f"SD info state is {info[0]!r}, expected CARD_READY")
        expect(int(info[3], 0) > 0, failures, "SD block_count is zero")
        expect(int(info[4], 0) > 0, failures, "SD capacity_kib is zero")
        expect(info[5] == "1", failures, "fatfs_available is not true")
        expect(info[6] == "1", failures, "fs_mounted is not true in info")

    init_result = parse_csv_response(results["SYST:SD:INIT"])
    expect(len(init_result) >= 7, system_pack_failures, "SYST:SD:INIT returned too few fields")
    if len(init_result) >= 7:
        expect(init_result[0] == "OK", system_pack_failures, f"SYST:SD:INIT status is {init_result[0]!r}, expected OK")
        expect(init_result[1] == "OK", system_pack_failures, f"SYST:SD:INIT manifest is {init_result[1]!r}, expected OK")
        expect(init_result[2] == "1", system_pack_failures, f"SYST:SD:INIT schema is {init_result[2]!r}, expected 1")
        expect(init_result[3] not in ("", "<timeout>"), system_pack_failures, "SYST:SD:INIT build_id is empty")
        expect(int(init_result[4], 0) >= 4, system_pack_failures, "SYST:SD:INIT required_count expected >= 4")
        expect(init_result[5] == "0", system_pack_failures, f"SYST:SD:INIT missing_count is {init_result[5]!r}, expected 0")
        expect(init_result[6] == "0", system_pack_failures, f"SYST:SD:INIT job error is {init_result[6]!r}, expected 0")

    init_job = parse_csv_response(results.get(INIT_JOB_QUERY_KEY, ""))
    expect(len(init_job) >= 8, system_pack_failures, "INIT SYST:STOR:JOB? returned too few fields")
    if len(init_job) >= 8:
        expect(init_job[0] == "DONE", system_pack_failures, f"init job state is {init_job[0]!r}, expected DONE")
        expect(init_job[2] == "SYSTEM_INIT", system_pack_failures, f"init job type is {init_job[2]!r}, expected SYSTEM_INIT")
        expect(init_job[3] == "/manifest.idx", system_pack_failures, f"init job path is {init_job[3]!r}, expected /manifest.idx")
        expect(int(init_job[4], 0) >= 4, system_pack_failures, "init job required_count expected >= 4")
        expect(init_job[5] == "MANIFEST", system_pack_failures, f"init job kind is {init_job[5]!r}, expected MANIFEST")
        expect(int(init_job[6], 0) != 0, system_pack_failures, "init job path hash is zero")
        expect(init_job[7] == "0", system_pack_failures, f"init job error is {init_job[7]!r}, expected 0")

    manifest = parse_csv_response(results["SYST:SD:MAN?"])
    expect(len(manifest) >= 7, system_pack_failures, "SYST:SD:MAN? returned too few fields")
    if len(manifest) >= 7:
        expect(manifest[0] == "OK", system_pack_failures, f"manifest status is {manifest[0]!r}, expected OK")
        expect(manifest[1] == "1", system_pack_failures, f"manifest schema is {manifest[1]!r}, expected 1")
        expect(manifest[2] == "DHRT100", system_pack_failures, f"manifest product is {manifest[2]!r}, expected DHRT100")
        expect(manifest[3] == "dhrt100", system_pack_failures, f"manifest hardware is {manifest[3]!r}, expected dhrt100")
        expect(manifest[4] not in ("", "<timeout>"), system_pack_failures, "manifest build_id is empty")
        expect(int(manifest[5], 0) >= 4, system_pack_failures, f"manifest required_count is {manifest[5]!r}, expected >= 4")
        expect(manifest[6] == "0", system_pack_failures, f"manifest missing_count is {manifest[6]!r}, expected 0")

    manifest_job = parse_csv_response(results.get(MANIFEST_JOB_QUERY_KEY, ""))
    expect(len(manifest_job) >= 8, system_pack_failures, "manifest SYST:STOR:JOB? returned too few fields")
    if len(manifest_job) >= 8:
        expect(manifest_job[0] == "DONE",
               system_pack_failures,
               f"manifest job state is {manifest_job[0]!r}, expected DONE")
        expect(manifest_job[2] == "MANIFEST_SCAN",
               system_pack_failures,
               f"manifest job type is {manifest_job[2]!r}, expected MANIFEST_SCAN")
        expect(manifest_job[3] == "/manifest.idx",
               system_pack_failures,
               f"manifest job path is {manifest_job[3]!r}, expected /manifest.idx")
        expect(int(manifest_job[4], 0) >= 4,
               system_pack_failures,
               f"manifest job required_count is {manifest_job[4]!r}, expected >= 4")
        expect(manifest_job[5] == "MANIFEST",
               system_pack_failures,
               f"manifest job kind is {manifest_job[5]!r}, expected MANIFEST")
        expect(int(manifest_job[6], 0) != 0, system_pack_failures, "manifest job path hash is zero")
        expect(manifest_job[7] == "0",
               system_pack_failures,
               f"manifest job error is {manifest_job[7]!r}, expected 0")

    job_start = parse_csv_response(results.get(STORAGE_JOB_INFO_KEY, ""))
    expect(len(job_start) >= 2, failures, "SYST:STOR:JOB:INFO returned too few fields")
    if len(job_start) >= 2:
        expect(job_start[0] == "OK", failures, f"SYST:STOR:JOB:INFO status is {job_start[0]!r}, expected OK")
        expect(int(job_start[1], 0) > 0, failures, "SYST:STOR:JOB:INFO job id is zero")
    job_keys = sorted(k for k in results if k.startswith(f"{STORAGE_JOB_QUERY_PREFIX}:"))
    expect(bool(job_keys), failures, "SYST:STOR:JOB? was not queried")
    if job_keys:
        job = parse_csv_response(results[job_keys[-1]])
        expect(len(job) >= 8, failures, "SYST:STOR:JOB? returned too few fields")
        if len(job) >= 8:
            expect(job[0] == "DONE", failures, f"storage job state is {job[0]!r}, expected DONE")
            expect(job[2] == "FILE_INFO", failures, f"storage job type is {job[2]!r}, expected FILE_INFO")
            expect(job[3] == "/manifest.idx", failures, f"storage job path is {job[3]!r}, expected /manifest.idx")
            expect(int(job[4], 0) > 0, failures, "storage job file size is zero")
            expect(job[5] == "FILE", failures, f"storage job kind is {job[5]!r}, expected FILE")
            expect(int(job[6], 0) != 0, failures, "storage job path hash is zero")
            expect(job[7] == "0", failures, f"storage job error is {job[7]!r}, expected 0")

    if sd_ready:
        root_entries = catalog_entries(results["MMEM:CAT?"])
        if "sdcard" in root_entries and "manifest.idx" not in root_entries:
            system_pack_failures.append("SD staging appears nested under /sdcard; copy the contents of build-sd-verify/sdcard to the card root")
        for name in ("manifest.json", "manifest.idx", "update", "profile", "mission", "cal", "reports"):
            expect(name in root_entries, system_pack_failures, f"root catalog missing {name}")

        # Required content is confirmed with direct INFO queries.  A catalog
        # response may be truncated or delayed by unrelated CDC traffic and
        # must not create a false "missing file" result.
        for name, command, path in SYSTEM_PACK_FILE_COMMANDS:
            expect_file_info(results,
                             command,
                             path,
                             "FILE",
                             system_pack_failures)

    if not args.skip_negative_path:
        denied = parse_csv_response(results['MMEM:CAT? "/../"'])
        expect(denied and denied[0] != "OK", failures, 'MMEM:CAT? "/../" was not denied')
        info_denied = parse_csv_response(results['MMEM:INFO? "/../"'])
        expect(info_denied and info_denied[0] != "OK", failures, 'MMEM:INFO? "/../" was not denied')
        page_denied = parse_csv_response(results['MMEM:CAT:PAGE? "/../",0,4'])
        expect(page_denied and page_denied[0] != "OK", failures, 'MMEM:CAT:PAGE? "/../",0,4 was not denied')
        read_denied = parse_csv_response(results['MMEM:READ? "/../",0,16'])
        expect(read_denied and read_denied[0] != "OK", failures, 'MMEM:READ? "/../",0,16 was not denied')

    if not args.skip_snapshot:
        auto_snapshot = parse_csv_response(results["AUTO:SYST:SNAP:LAST?"])
        expect(len(auto_snapshot) >= 6, failures, "automatic SYST:SNAP:LAST? returned too few fields")
        if len(auto_snapshot) >= 6:
            expect(auto_snapshot[0] == "OK", failures, f"auto snapshot status is {auto_snapshot[0]!r}, expected OK")
            expect(auto_snapshot[1] in ("boot", "arm", "fault", "run"),
                   failures,
                   f"auto snapshot kind is {auto_snapshot[1]!r}")
            expect(int(auto_snapshot[2], 0) > 0, failures, "auto snapshot sequence is zero")
            expect(auto_snapshot[3].startswith(f"/snapshots/{auto_snapshot[1]}/"),
                   failures,
                   f"auto snapshot path is {auto_snapshot[3]!r}")
            expect(auto_snapshot[5] == "0", failures, f"auto snapshot error is {auto_snapshot[5]!r}, expected 0")

        snapshot_write_key = 'SYST:SNAP:WRIT "boot"'
        expect(results[snapshot_write_key] == '"OK"',
               failures,
               f"SYST:SNAP:WRIT did not return OK: {results[snapshot_write_key]!r}")
        snapshot_job = parse_csv_response(results.get("SNAP:SYST:STOR:JOB?", ""))
        expect(len(snapshot_job) >= 8, failures, "SNAP SYST:STOR:JOB? returned too few fields")
        if len(snapshot_job) >= 8:
            expect(snapshot_job[0] == "DONE",
                   failures,
                   f"snapshot job state is {snapshot_job[0]!r}, expected DONE")
            expect(snapshot_job[2] == "SNAPSHOT_WRITE",
                   failures,
                   f"snapshot job type is {snapshot_job[2]!r}, expected SNAPSHOT_WRITE")
            expect(snapshot_job[5] == "SNAPSHOT",
                   failures,
                   f"snapshot job kind is {snapshot_job[5]!r}, expected SNAPSHOT")
            expect(snapshot_job[7] == "0",
                   failures,
                   f"snapshot job error is {snapshot_job[7]!r}, expected 0")
        snapshot = parse_csv_response(results["SYST:SNAP:LAST?"])
        expect(len(snapshot) >= 6, failures, "SYST:SNAP:LAST? returned too few fields")
        snapshot_name = ""
        if len(snapshot) >= 6:
            expect(snapshot[0] == "OK", failures, f"snapshot status is {snapshot[0]!r}, expected OK")
            expect(snapshot[1] == "boot", failures, f"snapshot kind is {snapshot[1]!r}, expected boot")
            expect(int(snapshot[2], 0) > 0, failures, "snapshot sequence is zero")
            expect(snapshot[3].startswith("/snapshots/boot/boot_"), failures, f"snapshot path is {snapshot[3]!r}")
            expect(snapshot[3].endswith(".json"), failures, f"snapshot path is {snapshot[3]!r}")
            expect(int(snapshot[4], 0) != 0, failures, "snapshot path hash is zero")
            expect(snapshot[5] == "0", failures, f"snapshot error is {snapshot[5]!r}, expected 0")
            if len(snapshot_job) >= 8 and snapshot_job[0] == "DONE":
                expect(snapshot_job[3] == snapshot[3],
                       failures,
                       f"snapshot job path {snapshot_job[3]!r} does not match snapshot {snapshot[3]!r}")
                expect(snapshot_job[4] == snapshot[2],
                       failures,
                       f"snapshot job sequence {snapshot_job[4]!r} does not match snapshot {snapshot[2]!r}")
            snapshot_name = Path(snapshot[3]).name
            expect_file_info(results,
                             "INFO:SYST:SNAP:LAST?",
                             snapshot[3],
                             "FILE",
                             failures)
        boot_snapshot_entries = catalog_entries(results['MMEM:CAT? "/snapshots/boot"'])
        if snapshot_name:
            expect(snapshot_name in boot_snapshot_entries or
                   not catalog_complete(results['MMEM:CAT? "/snapshots/boot"']),
                   failures,
                   f"/snapshots/boot catalog missing {snapshot_name}")

    trigger_failure_start = len(failures)
    if not args.skip_snapshot and not args.skip_arm_snapshot:
        for command in (
            "TRIG:SOUR 17",
            "TRIG:SOUR 16",
            "TRIG:EDGE 1",
            "TRIG:EDGE 0",
            "TRIG:GATE 1",
            "TRIG:GATE 0",
            "TRIG:SAFE 1",
            "TRIG:SAFE 0",
        ):
            expect(results[command] == '"OK"',
                   failures,
                   f"{command} did not return OK: {results[command]!r}")
        expect(ack_accepted(results["TRIG:MODE 1"]),
               failures,
               f"TRIG:MODE 1 was not accepted: {results['TRIG:MODE 1']!r}")
        seq_mode = parse_csv_response(results["SEQ:TRIG:MODE?"])
        expect(len(seq_mode) >= 2, failures, "SEQ TRIG:MODE? returned too few fields")
        if len(seq_mode) >= 2:
            expect(seq_mode[0] == "TRIG", failures, f"TRIG mode is {seq_mode[0]!r}, expected TRIG")
            expect(seq_mode[1] == "1", failures, f"TRIG mode id is {seq_mode[1]!r}, expected 1")
        expect(results["TRIG:ARM"] == '"OK"',
               failures,
               f"TRIG:ARM did not return OK: {results['TRIG:ARM']!r}")
        arm_job = parse_csv_response(results.get("ARM:SYST:STOR:JOB?", ""))
        expect(len(arm_job) >= 8, failures, "ARM SYST:STOR:JOB? returned too few fields")
        if len(arm_job) >= 8:
            expect(arm_job[0] == "DONE",
                   failures,
                   f"ARM job state is {arm_job[0]!r}, expected DONE")
            expect(arm_job[2] == "SNAPSHOT_WRITE",
                   failures,
                   f"ARM job type is {arm_job[2]!r}, expected SNAPSHOT_WRITE")
            expect(arm_job[3].startswith("/snapshots/arm/arm_"),
                   failures,
                   f"ARM job path is {arm_job[3]!r}, expected /snapshots/arm/arm_*")
            expect(arm_job[5] == "SNAPSHOT",
                   failures,
                   f"ARM job kind is {arm_job[5]!r}, expected SNAPSHOT")
            expect(arm_job[7] == "0",
                   failures,
                   f"ARM job error is {arm_job[7]!r}, expected 0")
        arm_status = parse_csv_response(results["ARM:STAT:TRIG?"])
        expect(len(arm_status) >= 9, failures, "ARM STAT:TRIG? returned too few fields")
        if len(arm_status) >= 9:
            expect(arm_status[0] == "SEQ_STEP", failures, f"ARM trigger mode is {arm_status[0]!r}, expected SEQ_STEP")
            expect(arm_status[1] == "2", failures, f"ARM trigger state id is {arm_status[1]!r}, expected 2")
            expect(arm_status[8] == "0", failures, f"ARM trigger error_code is {arm_status[8]!r}, expected 0")
        arm_snapshot = parse_csv_response(results["ARM:SYST:SNAP:LAST?"])
        expect(len(arm_snapshot) >= 6, failures, "ARM SYST:SNAP:LAST? returned too few fields")
        arm_snapshot_name = ""
        if len(arm_snapshot) >= 6:
            expect(arm_snapshot[0] == "OK", failures, f"ARM snapshot status is {arm_snapshot[0]!r}, expected OK")
            expect(arm_snapshot[1] == "arm", failures, f"ARM snapshot kind is {arm_snapshot[1]!r}, expected arm")
            expect(int(arm_snapshot[2], 0) > 0, failures, "ARM snapshot sequence is zero")
            expect(arm_snapshot[3].startswith("/snapshots/arm/arm_"), failures, f"ARM snapshot path is {arm_snapshot[3]!r}")
            expect(arm_snapshot[3].endswith(".json"), failures, f"ARM snapshot path is {arm_snapshot[3]!r}")
            expect(arm_snapshot[5] == "0", failures, f"ARM snapshot error is {arm_snapshot[5]!r}, expected 0")
            arm_snapshot_name = Path(arm_snapshot[3]).name
            expect_file_info(results,
                             "INFO:ARM:SYST:SNAP:LAST?",
                             arm_snapshot[3],
                             "FILE",
                             failures)
        arm_snapshot_entries = catalog_entries(results['MMEM:CAT? "/snapshots/arm"'])
        if arm_snapshot_name:
            expect(arm_snapshot_name in arm_snapshot_entries or
                   not catalog_complete(results['MMEM:CAT? "/snapshots/arm"']),
                   failures,
                   f"/snapshots/arm catalog missing {arm_snapshot_name}")

    if not args.skip_snapshot and not args.skip_fault_snapshot:
        if args.validate_trigger_release and not args.skip_arm_snapshot:
            expect(ack_accepted(results["RESET:TRIG:MODE 1"]),
                   failures,
                   f"RESET TRIG:MODE 1 was not accepted: {results['RESET:TRIG:MODE 1']!r}")
            expect(results["RESET:TRIG:ARM"] == '"OK"',
                   failures,
                   f"RESET TRIG:ARM did not return OK: {results['RESET:TRIG:ARM']!r}")
            expect(trigger_state(results["RESET:ARMED:STAT:TRIG?"]) == 2,
                   failures,
                   f"RESET arm state is not SEQ_ARMED: {results['RESET:ARMED:STAT:TRIG?']!r}")
            expect(trigger_state(results["RESET:STAT:TRIG?"]) == 0,
                   failures,
                   f"RESET did not return trigger state to IDLE: {results['RESET:STAT:TRIG?']!r}")
            expect(trigger_error(results["RESET:STAT:TRIG?"]) == 0,
                   failures,
                   f"RESET did not clear trigger error: {results['RESET:STAT:TRIG?']!r}")
            expect(ack_accepted(results["FAULT:TRIG:MODE 1"]),
                   failures,
                   f"FAULT TRIG:MODE 1 was not accepted: {results['FAULT:TRIG:MODE 1']!r}")
            expect(results["FAULT:TRIG:ARM"] == '"OK"',
                   failures,
                   f"FAULT TRIG:ARM did not return OK: {results['FAULT:TRIG:ARM']!r}")
            expect(trigger_state(results["FAULT:ARMED:STAT:TRIG?"]) == 2,
                   failures,
                   f"FAULT arm state is not SEQ_ARMED: {results['FAULT:ARMED:STAT:TRIG?']!r}")
        expect(results["TRIG:FAULT"] == '"OK"',
               failures,
               f"TRIG:FAULT did not return OK: {results['TRIG:FAULT']!r}")
        expect(trigger_state(results["FAULT:STAT:TRIG?"]) == 5,
               failures,
               f"TRIG:FAULT did not move trigger state to FAULT: {results['FAULT:STAT:TRIG?']!r}")
        expect(trigger_error(results["FAULT:STAT:TRIG?"]) == 100,
               failures,
               f"TRIG:FAULT did not latch forced fault error: {results['FAULT:STAT:TRIG?']!r}")
        fault_job = parse_csv_response(results.get("FAULT:SYST:STOR:JOB?", ""))
        expect(len(fault_job) >= 8, failures, "FAULT SYST:STOR:JOB? returned too few fields")
        if len(fault_job) >= 8:
            expect(fault_job[0] == "DONE",
                   failures,
                   f"fault evidence job state is {fault_job[0]!r}, expected DONE")
            expect(fault_job[2] == "FAULT_EVIDENCE",
                   failures,
                   f"fault evidence job type is {fault_job[2]!r}, expected FAULT_EVIDENCE")
            expect(fault_job[3].startswith("/reports/fault/pulse_fault_"),
                   failures,
                   f"fault evidence job path is {fault_job[3]!r}, expected /reports/fault/pulse_fault_*")
            expect(int(fault_job[4], 0) > 0,
                   failures,
                   "fault evidence job report id is zero")
            expect(fault_job[5] == "FAULT_EVIDENCE",
                   failures,
                   f"fault evidence job kind is {fault_job[5]!r}, expected FAULT_EVIDENCE")
            expect(fault_job[7] == "0",
                   failures,
                   f"fault evidence job error is {fault_job[7]!r}, expected 0")
        fault_snapshot = parse_csv_response(results["FAULT:SYST:SNAP:LAST?"])
        expect(len(fault_snapshot) >= 6, failures, "FAULT SYST:SNAP:LAST? returned too few fields")
        fault_snapshot_name = ""
        if len(fault_snapshot) >= 6:
            expect(fault_snapshot[0] == "OK", failures, f"FAULT snapshot status is {fault_snapshot[0]!r}, expected OK")
            expect(fault_snapshot[1] == "fault", failures, f"FAULT snapshot kind is {fault_snapshot[1]!r}, expected fault")
            expect(int(fault_snapshot[2], 0) > 0, failures, "FAULT snapshot sequence is zero")
            expect(fault_snapshot[3].startswith("/snapshots/fault/fault_"), failures, f"FAULT snapshot path is {fault_snapshot[3]!r}")
            expect(fault_snapshot[3].endswith(".json"), failures, f"FAULT snapshot path is {fault_snapshot[3]!r}")
            expect(fault_snapshot[5] == "0", failures, f"FAULT snapshot error is {fault_snapshot[5]!r}, expected 0")
            fault_snapshot_name = Path(fault_snapshot[3]).name
            expect_file_info(results,
                             "INFO:FAULT:SYST:SNAP:LAST?",
                             fault_snapshot[3],
                             "FILE",
                             failures)
        fault_snapshot_entries = catalog_entries(results['MMEM:CAT? "/snapshots/fault"'])
        if fault_snapshot_name:
            expect(fault_snapshot_name in fault_snapshot_entries or
                   not catalog_complete(results['MMEM:CAT? "/snapshots/fault"']),
                   failures,
                   f"/snapshots/fault catalog missing {fault_snapshot_name}")
        fault_trace = parse_csv_response(results["FAULT:SYST:TRAC:LAST?"])
        expect(len(fault_trace) >= 7, failures, "FAULT SYST:TRAC:LAST? returned too few fields")
        fault_trace_name = ""
        if len(fault_trace) >= 7:
            expected_fault_trace_events = 12 if not args.skip_arm_snapshot else 3
            expect(fault_trace[0] == "OK", failures, f"FAULT trace status is {fault_trace[0]!r}, expected OK")
            expect(fault_trace[1] == "fault", failures, f"FAULT trace kind is {fault_trace[1]!r}, expected fault")
            expect(int(fault_trace[2], 0) > 0, failures, "FAULT trace sequence is zero")
            expect(fault_trace[3].startswith("/traces/fault/fault_"), failures, f"FAULT trace path is {fault_trace[3]!r}")
            expect(fault_trace[3].endswith(".bin"), failures, f"FAULT trace path is {fault_trace[3]!r}")
            expect(int(fault_trace[5], 0) >= expected_fault_trace_events,
                   failures,
                   f"FAULT trace event_count is {fault_trace[5]!r}, expected >= {expected_fault_trace_events}")
            expect(fault_trace[6] == "0", failures, f"FAULT trace error is {fault_trace[6]!r}, expected 0")
            fault_trace_name = Path(fault_trace[3]).name
            expect_file_info(results,
                             "INFO:FAULT:SYST:TRAC:LAST?",
                             fault_trace[3],
                             "FILE",
                             failures,
                             min_size=36)
            expect_file_info(results,
                             "INFO:FAULT:SYST:TRAC:IDX?",
                             fault_trace[3].replace(".bin", ".idx"),
                             "FILE",
                             failures)
        fault_trace_entries = catalog_entries(results['MMEM:CAT? "/traces/fault"'])
        if fault_trace_name:
            fault_trace_catalog_complete = catalog_complete(results['MMEM:CAT? "/traces/fault"'])
            expect(fault_trace_name in fault_trace_entries or not fault_trace_catalog_complete,
                   failures,
                   f"/traces/fault catalog missing {fault_trace_name}")
            expect(fault_trace_name.replace(".bin", ".idx") in fault_trace_entries or
                   not fault_trace_catalog_complete,
                   failures,
                   f"/traces/fault catalog missing {fault_trace_name.replace('.bin', '.idx')}")
            paged_entries: dict[str, tuple[int, str]] = {}
            expected_offset = 0
            page_keys = sorted((k for k in results if k.startswith("PAGE:MMEM:CAT:PAGE?")),
                               key=page_key_offset)
            for key in page_keys:
                next_offset = expect_catalog_page(results,
                                                  key,
                                                  "/traces/fault",
                                                  expected_offset,
                                                  failures)
                paged_entries.update(catalog_page_entries(results[key]))
                if next_offset == 0:
                    break
                expected_offset = next_offset
            expect(fault_trace_name in paged_entries,
                   failures,
                   f"paged /traces/fault catalog missing {fault_trace_name}")
            expect(fault_trace_name.replace(".bin", ".idx") in paged_entries,
                   failures,
                   f"paged /traces/fault catalog missing {fault_trace_name.replace('.bin', '.idx')}")
            catalog_job = parse_csv_response(results.get(CATALOG_JOB_QUERY_KEY, ""))
            expect(len(catalog_job) >= 8,
                   failures,
                   "PAGE SYST:STOR:JOB? returned too few fields")
            if len(catalog_job) >= 8:
                expect(catalog_job[0] == "DONE",
                       failures,
                       f"catalog page job state is {catalog_job[0]!r}, expected DONE")
                expect(catalog_job[2] == "CATALOG_PAGE",
                       failures,
                       f"catalog page job type is {catalog_job[2]!r}, expected CATALOG_PAGE")
                expect(catalog_job[5] == "CATALOG",
                       failures,
                       f"catalog page job kind is {catalog_job[5]!r}, expected CATALOG")
                expect(catalog_job[7] == "0",
                       failures,
                       f"catalog page job error is {catalog_job[7]!r}, expected 0")
            readback_bin = parse_csv_response(results.get("READBACK:FAULT:SYST:TRAC:LAST?", ""))
            readback_idx = parse_csv_response(results.get("READBACK:FAULT:SYST:TRAC:IDX?", ""))
            expect(len(readback_bin) >= 5 and readback_bin[0] == "OK",
                   failures,
                   "fault trace .bin was not read back over MMEM:READ?")
            expect(len(readback_idx) >= 5 and readback_idx[0] == "OK",
                   failures,
                   "fault trace .idx was not read back over MMEM:READ?")
            read_job = parse_csv_response(results.get(READ_JOB_QUERY_KEY, ""))
            expect(len(read_job) >= 8,
                   failures,
                   "READ SYST:STOR:JOB? returned too few fields")
            if len(read_job) >= 8:
                expect(read_job[0] == "DONE",
                       failures,
                       f"READ job state is {read_job[0]!r}, expected DONE")
                expect(read_job[2] == "FILE_READ",
                       failures,
                       f"READ job type is {read_job[2]!r}, expected FILE_READ")
                expect(read_job[5] == "READ",
                       failures,
                       f"READ job kind is {read_job[5]!r}, expected READ")
                expect(read_job[7] == "0",
                       failures,
                       f"READ job error is {read_job[7]!r}, expected 0")
            if len(readback_bin) >= 5 and len(readback_idx) >= 5:
                expect(readback_bin[2] == readback_bin[3],
                       failures,
                       f"fault trace .bin readback size {readback_bin[3]} does not match expected {readback_bin[2]}")
                expect(readback_idx[2] == readback_idx[3],
                       failures,
                       f"fault trace .idx readback size {readback_idx[3]} does not match expected {readback_idx[2]}")
                try:
                    decoded = decode_trace(Path(readback_bin[4]), Path(readback_idx[4]))
                    checks = decoded["checks"]
                    for check_name in ("magic_ok", "schema_ok", "size_ok", "crc_ok", "idx_ok"):
                        expect(bool(checks.get(check_name)),
                               failures,
                               f"trace decode check failed: {check_name}")
                    expect(int(decoded["header"]["event_count"]) == int(fault_trace[5], 0),
                           failures,
                           "decoded trace event_count does not match SYST:TRAC:LAST?")
                    event_names = {str(record.get("event_name")) for record in decoded["records"]}
                    if not args.skip_arm_snapshot:
                        if multicore_trace:
                            expected_event_names = [
                                "trigger.scpi_arm",
                                "sync_io.seq_armed",
                                "sync_io.seq_runtime",
                                "sync_io.seq_pio_state",
                                "sync_io.seq_dma_restart",
                                "sync_io.seq_dma_overflow",
                                "trigger.scpi_fault",
                            ]
                        else:
                            expected_event_names = [
                                "trigger.runtime_sample",
                                "trigger.resource_snapshot",
                                "sync_io.seq_runtime",
                                "sync_io.seq_pio_state",
                                "sync_io.seq_dma_restart",
                                "sync_io.seq_dma_overflow",
                                "sync_io.aux_snapshot",
                                "sync_io.ready_redy",
                                "sync_io.aux_timeout",
                            ]
                            if not args.validate_trigger_release:
                                expected_event_names.extend(
                                    [
                                        "trigger.source_config",
                                        "trigger.edge_config",
                                        "trigger.gate_config",
                                        "trigger.safe_config",
                                    ]
                                )
                        for event_name in expected_event_names:
                            expect(event_name in event_names,
                                   failures,
                                   f"decoded trace missing {event_name}")
                    if args.validate_trigger_release and not args.skip_arm_snapshot:
                        expect("trigger.resource_release" in event_names,
                               failures,
                               "decoded trace missing trigger.resource_release")
                        required_release_resources = {"PIO1", "DMA"}
                        expect(trace_has_release(decoded,
                                                 "RESET",
                                                 "SEQ_ARMED",
                                                 required_release_resources),
                               failures,
                               "decoded trace missing RESET release from SEQ_ARMED for PIO1/DMA")
                        expect(trace_has_release(decoded,
                                                 "FAULT",
                                                 "SEQ_ARMED",
                                                 required_release_resources),
                               failures,
                               "decoded trace missing FAULT release from SEQ_ARMED for PIO1/DMA")
                    write_text(out_dir / "trace_readback" / "decoded_fault_trace.json",
                               json.dumps(decoded, indent=2, ensure_ascii=False) + "\n")
                except (OSError, ValueError) as exc:
                    failures.append(f"trace readback decode failed: {exc}")
        fault_report = parse_csv_response(results["FAULT:SYST:FAULT:LAST?"])
        expect(len(fault_report) >= 7, failures, "FAULT SYST:FAULT:LAST? returned too few fields")
        fault_report_name = ""
        if len(fault_report) >= 7:
            expect(fault_report[0] == "OK", failures, f"FAULT report status is {fault_report[0]!r}, expected OK")
            expect(int(fault_report[1], 0) > 0, failures, "FAULT report sequence is zero")
            expect(fault_report[2].startswith("/reports/fault/pulse_fault_"), failures, f"FAULT report path is {fault_report[2]!r}")
            expect(fault_report[2].endswith(".json"), failures, f"FAULT report path is {fault_report[2]!r}")
            expect(int(fault_report[3], 0) != 0, failures, "FAULT report path hash is zero")
            if len(fault_snapshot) >= 6:
                expect(fault_report[4] == fault_snapshot[2],
                       failures,
                       f"FAULT report snapshot id {fault_report[4]!r} does not match snapshot {fault_snapshot[2]!r}")
            if len(fault_trace) >= 7:
                expect(fault_report[5] == fault_trace[2],
                       failures,
                       f"FAULT report trace id {fault_report[5]!r} does not match trace {fault_trace[2]!r}")
            expect(fault_report[6] == "0", failures, f"FAULT report error is {fault_report[6]!r}, expected 0")
            fault_report_name = Path(fault_report[2]).name
            expect_file_info(results,
                             "INFO:FAULT:SYST:FAULT:LAST?",
                             fault_report[2],
                             "FILE",
                             failures)
        fault_report_entries = catalog_entries(results['MMEM:CAT? "/reports/fault"'])
        if fault_report_name:
            expect(fault_report_name in fault_report_entries or
                   not catalog_complete(results['MMEM:CAT? "/reports/fault"']),
                   failures,
                   f"/reports/fault catalog missing {fault_report_name}")

    trigger_evidence_failures.extend(failures[trigger_failure_start:])
    del failures[trigger_failure_start:]

    lines = [f"{command} -> {response}" for command, response in results.items()]
    write_text(out_dir / "queries.txt", "\n".join(lines) + "\n")

    all_failures = failures + system_pack_failures + trigger_evidence_failures
    summary = {
        "started": started,
        "port": args.port,
        "passed": not all_failures,
        "sd_function_passed": not failures,
        "system_pack_passed": not system_pack_failures,
        "trigger_evidence_passed": not trigger_evidence_failures,
        "failures": failures,
        "system_pack_failures": system_pack_failures,
        "trigger_evidence_failures": trigger_evidence_failures,
        "results": results,
    }
    write_text(out_dir / "summary.json", json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    summary_lines = [
        f"SD_FUNCTION={'PASS' if not failures else 'FAIL'}",
        f"SYSTEM_PACK={'PASS' if not system_pack_failures else 'FAIL'}",
        f"TRIGGER_EVIDENCE={'PASS' if not trigger_evidence_failures else 'FAIL'}",
    ]
    if failures:
        summary_lines.append("SD function failures:")
        summary_lines.extend(f"- {item}" for item in failures)
    if system_pack_failures:
        summary_lines.append("System Pack content failures:")
        summary_lines.extend(f"- {item}" for item in system_pack_failures)
    if trigger_evidence_failures:
        summary_lines.append("Trigger evidence failures:")
        summary_lines.extend(f"- {item}" for item in trigger_evidence_failures)
    summary_text = "\n".join(summary_lines) + "\n"
    write_text(out_dir / "summary.txt", summary_text)

    print(f"out_dir={out_dir}")
    print(summary_text, end="")
    return 0 if not all_failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
