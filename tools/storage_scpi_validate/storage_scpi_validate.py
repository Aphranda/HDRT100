#!/usr/bin/env python3
"""Validate generic Storage SCPI file and directory operations."""

from __future__ import annotations

import argparse
import binascii
import csv
import json
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import open_serial_port, read_serial_line_idle

DEFAULT_DIR = "/logs/scpi_storage_validate"
DEFAULT_FILE = f"{DEFAULT_DIR}/roundtrip.bin"
DEFAULT_RENAMED_FILE = f"{DEFAULT_DIR}/roundtrip_renamed.bin"
DEFAULT_RENAMED_DIR = "/logs/scpi_storage_validate_done"
DEFAULT_PAYLOAD = b"RP2350_TRIG storage scpi validate\n"


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
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--directory", default=DEFAULT_DIR)
    parser.add_argument("--file", default=DEFAULT_FILE)
    parser.add_argument("--renamed-file", default=DEFAULT_RENAMED_FILE)
    parser.add_argument("--renamed-directory", default=DEFAULT_RENAMED_DIR)
    parser.add_argument("--payload", default=DEFAULT_PAYLOAD.decode("ascii"))
    parser.add_argument("--chunk", type=int, default=32)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def read_serial_line(ser: serial.Serial, deadline: float) -> str | None:
    return read_serial_line_idle(ser, deadline)


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


def make_execute(args: argparse.Namespace):
    if args.port and args.visa_resource:
        raise SystemExit("use either a CDC port or --visa-resource, not both")
    if not args.port and not args.visa_resource:
        raise SystemExit("CDC port or --visa-resource is required")

    if args.visa_resource:
        rm, inst = open_visa_resource(args.visa_resource, args.timeout)

        def execute(command: str) -> str:
            return str(inst.query(command)).strip()

        return execute, [inst, rm]

    serial_context = open_serial_port(args.port, args.baud, args.timeout, args.settle)
    ser = serial_context.__enter__()

    def execute(command: str) -> str:
        ser.reset_input_buffer()
        ser.write((command + "\n").encode("ascii"))
        ser.flush()
        return read_response(ser, args.timeout)

    return execute, [serial_context]


def expect_prefix(response: str, expected: str) -> None:
    fields = parse_csv_response(response)
    if not fields or fields[0] != expected:
        raise AssertionError(f"expected prefix {expected!r}, got {response!r}")


def expect_info(response: str, *, path: str, size: int) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 8:
        raise AssertionError(f"INFO field_count={len(fields)}")
    if fields[0] != "OK" or fields[3] != "FILE":
        raise AssertionError(f"bad INFO response {response!r}")
    if int(fields[4], 0) != size:
        raise AssertionError(f"size mismatch {fields[4]} != {size}")
    if fields[7] != path:
        raise AssertionError(f"path mismatch {fields[7]!r}")


def expect_read(response: str, *, payload: bytes) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 10:
        raise AssertionError(f"READ field_count={len(fields)}")
    if fields[0] != "OK":
        raise AssertionError(f"bad READ response {response!r}")
    if int(fields[4], 0) != len(payload):
        raise AssertionError(f"returned mismatch {fields[4]} != {len(payload)}")
    if int(fields[5], 0) != len(payload):
        raise AssertionError(f"file_size mismatch {fields[5]} != {len(payload)}")
    if fields[9] != payload.hex().upper():
        raise AssertionError("readback payload mismatch")


def expect_catalog(response: str, *, filename: str) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 8:
        raise AssertionError(f"CATalog field_count={len(fields)}")
    if fields[0] != "OK":
        raise AssertionError(f"bad CATalog response {response!r}")
    if filename not in fields[7]:
        raise AssertionError(f"catalog does not include {filename!r}: {fields[7]!r}")


def run_validation(execute,
                   *,
                   directory: str,
                   file_path: str,
                   renamed_file: str,
                   renamed_directory: str,
                   payload: bytes,
                   chunk_size: int) -> list[Record]:
    if chunk_size <= 0 or chunk_size > 256:
        raise SystemExit("--chunk must be 1..256 bytes")

    records: list[Record] = []

    def run(command: str, checker) -> str:
        response = execute(command)
        try:
            checker(response)
            records.append(Record(command, response, "PASS", "ok"))
        except AssertionError as exc:
            records.append(Record(command, response, "FAIL", str(exc)))
        print(f"{records[-1].status} {command[:96]} => {response}")
        if records[-1].status != "PASS":
            raise SystemExit(1)
        return response

    # Cleanup first so repeated validation runs are deterministic.
    cleanup_file = execute(f'SYSTem:STORage:FILE:DELete "{file_path}"')
    records.append(Record(f'SYSTem:STORage:FILE:DELete "{file_path}"', cleanup_file, "INFO", "pre-clean"))
    cleanup_renamed_file = execute(f'SYSTem:STORage:FILE:DELete "{renamed_file}"')
    records.append(Record(f'SYSTem:STORage:FILE:DELete "{renamed_file}"',
                          cleanup_renamed_file,
                          "INFO",
                          "pre-clean"))
    cleanup_dir = execute(f'SYSTem:STORage:DIRectory:DELete "{directory}"')
    records.append(Record(f'SYSTem:STORage:DIRectory:DELete "{directory}"', cleanup_dir, "INFO", "pre-clean"))
    cleanup_renamed_dir = execute(f'SYSTem:STORage:DIRectory:DELete "{renamed_directory}"')
    records.append(Record(f'SYSTem:STORage:DIRectory:DELete "{renamed_directory}"',
                          cleanup_renamed_dir,
                          "INFO",
                          "pre-clean"))

    run(f'SYSTem:STORage:DIRectory:CREate "{directory}"', lambda r: expect_prefix(r, "CREATED"))
    crc = binascii.crc32(payload) & 0xFFFFFFFF
    begin = run(f'SYSTem:STORage:FILE:WRITe:BEGIN "{file_path}",{len(payload)},{crc}',
                lambda r: expect_prefix(r, "OK"))
    begin_fields = parse_csv_response(begin)
    if len(begin_fields) < 2:
        raise SystemExit("BEGIN did not return txn id")
    txn_id = begin_fields[1]

    offset = 0
    while offset < len(payload):
        chunk = payload[offset:offset + chunk_size]
        expected_received = offset + len(chunk)

        def check_data(response: str, expected: int = expected_received) -> None:
            fields = parse_csv_response(response)
            if len(fields) < 3 or fields[0] != "OK":
                raise AssertionError(f"bad DATA response {response!r}")
            if fields[1] != txn_id:
                raise AssertionError(f"txn mismatch {fields[1]} != {txn_id}")
            if int(fields[2], 0) != expected:
                raise AssertionError(f"received mismatch {fields[2]} != {expected}")

        run(f'SYSTem:STORage:FILE:WRITe:DATA {txn_id},{offset},"{chunk.hex().upper()}"', check_data)
        offset += len(chunk)

    run(f"SYSTem:STORage:FILE:WRITe:END {txn_id}", lambda r: expect_prefix(r, "WRITTEN"))
    run(f'SYSTem:STORage:FILE:INFO? "{file_path}"',
        lambda r: expect_info(r, path=file_path, size=len(payload)))
    run(f'SYSTem:STORage:FILE:READ? "{file_path}",0,{len(payload)}',
        lambda r: expect_read(r, payload=payload))
    run(f'SYSTem:STORage:FILE:REName "{file_path}","{renamed_file}"',
        lambda r: expect_prefix(r, "RENAMED"))
    run(f'SYSTem:STORage:FILE:INFO? "{renamed_file}"',
        lambda r: expect_info(r, path=renamed_file, size=len(payload)))
    run(f'SYSTem:STORage:FILE:READ? "{renamed_file}",0,{len(payload)}',
        lambda r: expect_read(r, payload=payload))
    filename = file_path.rsplit("/", 1)[-1]
    renamed_filename = renamed_file.rsplit("/", 1)[-1]
    run(f'SYSTem:STORage:DIRectory:CATalog? "{directory}",0,8',
        lambda r: expect_catalog(r, filename=renamed_filename))
    run(f'SYSTem:STORage:FILE:DELete "{renamed_file}"', lambda r: expect_prefix(r, "DELETED"))
    run(f'SYSTem:STORage:DIRectory:REName "{directory}","{renamed_directory}"',
        lambda r: expect_prefix(r, "RENAMED"))
    run(f'SYSTem:STORage:DIRectory:DELete "{renamed_directory}"', lambda r: expect_prefix(r, "DELETED"))
    return records


def write_outputs(out_dir: Path, records: list[Record], payload: bytes) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    transcript = out_dir / "storage_scpi_validate_transcript.txt"
    with transcript.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# Storage SCPI validation {datetime.now().isoformat(timespec='seconds')}\n")
        for record in records:
            handle.write(f"> {record.command}\n")
            handle.write(f"< {record.response}\n")
            handle.write(f"# {record.status} {record.reason}\n")
    summary = {
        "passed": all(record.status in ("PASS", "INFO") for record in records),
        "total": len(records),
        "failed": sum(1 for record in records if record.status == "FAIL"),
        "size": len(payload),
        "crc32": f"{binascii.crc32(payload) & 0xFFFFFFFF:08X}",
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")


def main() -> int:
    args = parse_args()
    payload = args.payload.encode("utf-8")
    execute, handles = make_execute(args)
    try:
        records = run_validation(execute,
                                 directory=args.directory,
                                 file_path=args.file,
                                 renamed_file=args.renamed_file,
                                 renamed_directory=args.renamed_directory,
                                 payload=payload,
                                 chunk_size=args.chunk)
    finally:
        for handle in handles:
            try:
                handle.close() if hasattr(handle, "close") else handle.__exit__(None, None, None)
            except Exception:
                pass

    out_dir = args.out_dir or (ROOT / "build" / f"storage_scpi_validate_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    write_outputs(out_dir, records, payload)
    failed = sum(1 for record in records if record.status == "FAIL")
    print(f"summary: passed={failed == 0} failed={failed} out_dir={out_dir}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
