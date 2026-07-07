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

STORAGE_JOB_INFO_KEY = 'SYST:STOR:JOB:INFO "/manifest.idx"'
STORAGE_JOB_QUERY_PREFIX = "SYST:STOR:JOB?"
INIT_JOB_QUERY_KEY = "INIT:SYST:STOR:JOB?"
MANIFEST_JOB_QUERY_KEY = "MAN:SYST:STOR:JOB?"
READ_JOB_QUERY_KEY = "READ:SYST:STOR:JOB?"
CATALOG_JOB_QUERY_KEY = "PAGE:SYST:STOR:JOB?"


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
    return parser.parse_args()


def normalize_scpi_line(line: str) -> str:
    return line.strip()


def is_log_or_ack(line: str) -> bool:
    return not line or line.startswith("[") or line == '"OK"'


def read_scpi_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = normalize_scpi_line(raw.decode("utf-8", errors="replace"))
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
        if line.startswith('"OK"'):
            return '"OK"'
        if line and not line.startswith("["):
            return line
    return "<timeout>"


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_line(ser, timeout_s)


def command_ack(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_any_scpi_line(ser, timeout_s)


def parse_csv_response(response: str) -> list[str]:
    return next(csv.reader([response], skipinitialspace=True))


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


def run_queries(port: str,
                baud: int,
                timeout_s: float,
                settle_s: float,
                out_dir: Path,
                commands: Iterable[str],
                validate_snapshot: bool,
                validate_arm_snapshot: bool,
                validate_fault_snapshot: bool) -> dict[str, str]:
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
            results["TRIG:FAULT"] = command_ack(ser, "TRIG:FAULT", timeout_s)
            time.sleep(0.2)
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

    failures: list[str] = []
    commands = list(BASELINE_COMMANDS) + [command for _, command in CATALOG_COMMANDS]
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
                          validate_fault_snapshot=(not args.skip_snapshot and not args.skip_fault_snapshot))

    log_status = parse_csv_response(results["SYST:LOG:STAT?"])
    expect(len(log_status) >= 23, failures, "SYST:LOG:STAT? returned too few fields")
    if len(log_status) >= 23:
        expect(log_status[0] in ("DEBUG", "INFO", "WARN", "ERROR"),
               failures,
               f"log level is {log_status[0]!r}")
        try:
            level_value = int(log_status[1], 0)
        except ValueError:
            level_value = 0xFFFFFFFF
        expect(level_value <= 3, failures, f"log level value is {log_status[1]!r}")
        for index, field in enumerate(log_status[2:23], start=2):
            try:
                int(field, 0)
            except ValueError:
                failures.append(f"SYST:LOG:STAT? field {index} is not numeric: {field!r}")

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
    expect(len(init_result) >= 7, failures, "SYST:SD:INIT returned too few fields")
    if len(init_result) >= 7:
        expect(init_result[0] == "OK", failures, f"SYST:SD:INIT status is {init_result[0]!r}, expected OK")
        expect(init_result[1] == "OK", failures, f"SYST:SD:INIT manifest is {init_result[1]!r}, expected OK")
        expect(init_result[2] == "1", failures, f"SYST:SD:INIT schema is {init_result[2]!r}, expected 1")
        expect(init_result[3] not in ("", "<timeout>"), failures, "SYST:SD:INIT build_id is empty")
        expect(int(init_result[4], 0) >= 4, failures, "SYST:SD:INIT required_count expected >= 4")
        expect(init_result[5] == "0", failures, f"SYST:SD:INIT missing_count is {init_result[5]!r}, expected 0")
        expect(init_result[6] == "0", failures, f"SYST:SD:INIT job error is {init_result[6]!r}, expected 0")

    init_job = parse_csv_response(results.get(INIT_JOB_QUERY_KEY, ""))
    expect(len(init_job) >= 8, failures, "INIT SYST:STOR:JOB? returned too few fields")
    if len(init_job) >= 8:
        expect(init_job[0] == "DONE", failures, f"init job state is {init_job[0]!r}, expected DONE")
        expect(init_job[2] == "SYSTEM_INIT", failures, f"init job type is {init_job[2]!r}, expected SYSTEM_INIT")
        expect(init_job[3] == "/manifest.idx", failures, f"init job path is {init_job[3]!r}, expected /manifest.idx")
        expect(int(init_job[4], 0) >= 4, failures, "init job required_count expected >= 4")
        expect(init_job[5] == "MANIFEST", failures, f"init job kind is {init_job[5]!r}, expected MANIFEST")
        expect(int(init_job[6], 0) != 0, failures, "init job path hash is zero")
        expect(init_job[7] == "0", failures, f"init job error is {init_job[7]!r}, expected 0")

    manifest = parse_csv_response(results["SYST:SD:MAN?"])
    expect(len(manifest) >= 7, failures, "SYST:SD:MAN? returned too few fields")
    if len(manifest) >= 7:
        expect(manifest[0] == "OK", failures, f"manifest status is {manifest[0]!r}, expected OK")
        expect(manifest[1] == "1", failures, f"manifest schema is {manifest[1]!r}, expected 1")
        expect(manifest[2] == "RP2350_TRIG", failures, f"manifest product is {manifest[2]!r}, expected RP2350_TRIG")
        expect(manifest[3] == "rp2350_trig", failures, f"manifest hardware is {manifest[3]!r}, expected rp2350_trig")
        expect(manifest[4] not in ("", "<timeout>"), failures, "manifest build_id is empty")
        expect(int(manifest[5], 0) >= 4, failures, f"manifest required_count is {manifest[5]!r}, expected >= 4")
        expect(manifest[6] == "0", failures, f"manifest missing_count is {manifest[6]!r}, expected 0")

    manifest_job = parse_csv_response(results.get(MANIFEST_JOB_QUERY_KEY, ""))
    expect(len(manifest_job) >= 8, failures, "manifest SYST:STOR:JOB? returned too few fields")
    if len(manifest_job) >= 8:
        expect(manifest_job[0] == "DONE",
               failures,
               f"manifest job state is {manifest_job[0]!r}, expected DONE")
        expect(manifest_job[2] == "MANIFEST_SCAN",
               failures,
               f"manifest job type is {manifest_job[2]!r}, expected MANIFEST_SCAN")
        expect(manifest_job[3] == "/manifest.idx",
               failures,
               f"manifest job path is {manifest_job[3]!r}, expected /manifest.idx")
        expect(int(manifest_job[4], 0) >= 4,
               failures,
               f"manifest job required_count is {manifest_job[4]!r}, expected >= 4")
        expect(manifest_job[5] == "MANIFEST",
               failures,
               f"manifest job kind is {manifest_job[5]!r}, expected MANIFEST")
        expect(int(manifest_job[6], 0) != 0, failures, "manifest job path hash is zero")
        expect(manifest_job[7] == "0",
               failures,
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
            failures.append("SD staging appears nested under /sdcard; copy the contents of build-sd-verify/sdcard to the card root")
        for name in ("manifest.json", "manifest.idx", "update", "profile", "mission", "cal", "reports"):
            expect(name in root_entries, failures, f"root catalog missing {name}")

        update_entries = catalog_entries(results['MMEM:CAT? "/update"'])
        expect("RP2350_TRIG_UPDATE.pkg" in update_entries, failures, "/update missing RP2350_TRIG_UPDATE.pkg")

        profile_entries = catalog_entries(results['MMEM:CAT? "/profile"'])
        expect("active.json" in profile_entries, failures, "/profile missing active.json")

        mission_entries = catalog_entries(results['MMEM:CAT? "/mission"'])
        expect("recipe.json" in mission_entries, failures, "/mission missing recipe.json")
        expect("node_map.json" in mission_entries, failures, "/mission missing node_map.json")

        cal_entries = catalog_entries(results['MMEM:CAT? "/cal"'])
        expect("board_cal.json" in cal_entries, failures, "/cal missing board_cal.json")

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
            expect(auto_snapshot[1] == "boot", failures, f"auto snapshot kind is {auto_snapshot[1]!r}, expected boot")
            expect(int(auto_snapshot[2], 0) > 0, failures, "auto boot snapshot sequence is zero")
            expect(auto_snapshot[3].startswith("/snapshots/boot/boot_"), failures, f"auto snapshot path is {auto_snapshot[3]!r}")
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
        expect(results["TRIG:MODE 1"] == '"OK"',
               failures,
               f"TRIG:MODE 1 did not return OK: {results['TRIG:MODE 1']!r}")
        seq_mode = parse_csv_response(results["SEQ:TRIG:MODE?"])
        expect(len(seq_mode) >= 2, failures, "SEQ TRIG:MODE? returned too few fields")
        if len(seq_mode) >= 2:
            expect(seq_mode[0] == "SEQ_STEP", failures, f"TRIG mode is {seq_mode[0]!r}, expected SEQ_STEP")
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
        expect(results["TRIG:FAULT"] == '"OK"',
               failures,
               f"TRIG:FAULT did not return OK: {results['TRIG:FAULT']!r}")
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
                        for event_name in (
                            "trigger.source_config",
                            "trigger.edge_config",
                            "trigger.gate_config",
                            "trigger.safe_config",
                            "trigger.runtime_sample",
                            "trigger.resource_snapshot",
                            "sync_io.seq_runtime",
                            "sync_io.seq_pio_state",
                            "sync_io.seq_dma_restart",
                            "sync_io.seq_dma_overflow",
                            "sync_io.aux_snapshot",
                            "sync_io.ready_redy",
                            "sync_io.aux_timeout",
                        ):
                            expect(event_name in event_names,
                                   failures,
                                   f"decoded trace missing {event_name}")
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

    lines = [f"{command} -> {response}" for command, response in results.items()]
    write_text(out_dir / "queries.txt", "\n".join(lines) + "\n")

    summary = {
        "started": started,
        "port": args.port,
        "passed": not failures,
        "failures": failures,
        "results": results,
    }
    write_text(out_dir / "summary.json", json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    summary_text = "PASS\n" if not failures else "FAIL\n" + "\n".join(f"- {item}" for item in failures) + "\n"
    write_text(out_dir / "summary.txt", summary_text)

    print(f"out_dir={out_dir}")
    print(summary_text, end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
