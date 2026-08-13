#!/usr/bin/env python3
"""Validate RefMem SCPI staging load commands over USB CDC or USBTMC/VISA."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]


@dataclass
class Record:
    command: str
    response: str
    status: str
    reason: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="USB CDC serial port, for example COM6")
    parser.add_argument("--visa-resource", help="USBTMC VISA resource")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--skip-sd", action="store_true", help="skip SYSTem:REFMEM:LOAD:SD")
    return parser.parse_args()


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def read_serial_line(ser: serial.Serial, deadline: float) -> str | None:
    raw = bytearray()
    while time.monotonic() < deadline:
        ch = ser.read(1)
        if not ch:
            continue
        raw.extend(ch)
        if ch == b"\n":
            break
    if not raw:
        return None
    return bytes(raw).decode("utf-8", errors="replace").strip()


def read_response(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_serial_line(ser, deadline)
        if line is None or is_log_line(line):
            continue
        return line
    return "<timeout>"


def open_visa_resource(resource: str, timeout_s: float):
    try:
        import pyvisa
    except ImportError as exc:
        raise SystemExit("pyvisa is required: python -m pip install pyvisa") from exc

    rm = pyvisa.ResourceManager()
    inst = rm.open_resource(resource)
    inst.timeout = int(timeout_s * 1000)
    inst.write_termination = "\n"
    inst.read_termination = "\n"
    return rm, inst


def load_fields(response: str, *, has_prefix: bool) -> list[str]:
    fields = parse_csv_response(response)
    expected = 25 if has_prefix else 24
    if len(fields) != expected:
        raise AssertionError(f"field_count={len(fields)} expected={expected}")
    if has_prefix:
        fields = fields[1:]
    return fields


def expect_status_idle(response: str) -> None:
    fields = load_fields(response, has_prefix=False)
    if fields[3] != "0":
        raise AssertionError(f"RefMem mode is not IDLE: {fields[3]}")


def expect_node_staged(response: str, *, node_id: str, instance_id: str) -> None:
    fields = load_fields(response, has_prefix=True)
    if response.split(",", 1)[0] != '"STAGED"':
        raise AssertionError("node load was not staged")
    if fields[2] != "2" or fields[3] != "0" or fields[4] != "2":
        raise AssertionError(f"unexpected source/mode/staging: {fields[2:5]}")
    if fields[14] != node_id or fields[15] != instance_id or fields[21] != "0":
        raise AssertionError(f"unexpected node staging fields: {fields[14:22]}")


def expect_node_rejected(response: str) -> None:
    fields = load_fields(response, has_prefix=True)
    if response.split(",", 1)[0] != '"REJECTED"':
        raise AssertionError("invalid node load was not rejected")
    if fields[3] != "0" or fields[4] != "3" or fields[21] == "0":
        raise AssertionError(f"unexpected reject fields: {fields[3:5]} last_error={fields[21]}")


def expect_sd_response(response: str) -> None:
    fields = load_fields(response, has_prefix=True)
    prefix = response.split(",", 1)[0]
    if prefix not in ('"STAGED"', '"REJECTED"'):
        raise AssertionError(f"unexpected SD prefix {prefix}")
    if fields[2] != "1" or fields[3] != "0":
        raise AssertionError(f"unexpected SD source/mode: {fields[2:4]}")


def expect_table_response(response: str, *, table_id: str, min_staging_mask: int = 0) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 16:
        raise AssertionError(f"field_count={len(fields)} expected=16")
    if fields[0] != "1" or fields[1] != "8":
        raise AssertionError(f"unexpected registry header: {fields[0:2]}")
    if fields[6] != table_id:
        raise AssertionError(f"unexpected table id: {fields[6]}")
    if int(fields[2], 0) != 0xFF:
        raise AssertionError(f"active mask is not complete: {fields[2]}")
    if int(fields[3], 0) < min_staging_mask:
        raise AssertionError(f"staging mask too small: {fields[3]} < {min_staging_mask}")
    if int(fields[9], 0) == 0:
        raise AssertionError("active CRC is zero")


def write_outputs(out_dir: Path, records: list[Record]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    transcript = out_dir / "refmem_scpi_load_transcript.txt"
    with transcript.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# RefMem SCPI load validation {datetime.now().isoformat(timespec='seconds')}\n")
        for record in records:
            handle.write(f"> {record.command}\n")
            handle.write(f"< {record.response}\n")
            handle.write(f"# {record.status} {record.reason}\n")
    summary = {
        "passed": all(record.status == "PASS" for record in records),
        "total": len(records),
        "failed": sum(1 for record in records if record.status != "PASS"),
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")


def run_validation(execute, *, skip_sd: bool) -> list[Record]:
    tests = [
        ("SYSTem:REFMEM:LOAD:STATus?", expect_status_idle),
        ("SYSTem:REFMEM:TABle? 0", lambda response: expect_table_response(response, table_id="0")),
        ("SYSTem:REFMEM:LOAD:NODE 5,9,32,32,1,0,0",
         lambda response: expect_node_staged(response, node_id="5", instance_id="9")),
        ("SYSTem:REFMEM:TABle? 2",
         lambda response: expect_table_response(response, table_id="2", min_staging_mask=0xFF)),
        ("SYSTem:REFMEM:LOAD:NODE 8,9,32,32,1,0,0", expect_node_rejected),
    ]
    if not skip_sd:
        tests.append(("SYSTem:REFMEM:LOAD:SD", expect_sd_response))

    records: list[Record] = []
    for command, check in tests:
        response = execute(command)
        try:
            check(response)
            record = Record(command, response, "PASS", "ok")
        except AssertionError as exc:
            record = Record(command, response, "FAIL", str(exc))
        records.append(record)
        print(f"{record.status} {command} => {response}")
        time.sleep(0.05)
    return records


def main() -> int:
    args = parse_args()
    if args.port and args.visa_resource:
        raise SystemExit("use either a CDC port or --visa-resource, not both")
    if not args.port and not args.visa_resource:
        raise SystemExit("CDC port or --visa-resource is required")

    close_handles = []
    try:
        if args.visa_resource:
            rm, inst = open_visa_resource(args.visa_resource, args.timeout)
            close_handles = [inst, rm]

            def execute(command: str) -> str:
                return str(inst.query(command)).strip()
        else:
            ser = serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout)
            close_handles = [ser]
            time.sleep(args.settle)
            ser.reset_input_buffer()

            def execute(command: str) -> str:
                ser.reset_input_buffer()
                ser.write((command + "\n").encode("ascii"))
                ser.flush()
                return read_response(ser, args.timeout)

        records = run_validation(execute, skip_sd=args.skip_sd)
    finally:
        for handle in close_handles:
            try:
                handle.close()
            except Exception:
                pass

    out_dir = args.out_dir or (ROOT / "build" / f"refmem_scpi_load_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    write_outputs(out_dir, records)
    failed = sum(1 for record in records if record.status != "PASS")
    print(f"summary: passed={failed == 0} failed={failed} out_dir={out_dir}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
