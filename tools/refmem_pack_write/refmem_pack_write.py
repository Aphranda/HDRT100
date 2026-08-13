#!/usr/bin/env python3
"""Upload and validate /refmem/app_model.rmtp through generic Storage SCPI."""

from __future__ import annotations

import argparse
import binascii
import csv
import json
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PACKAGE = ROOT / "build-rtos-multicore-smoke" / "sdcard_refmem_parser" / "refmem" / "app_model.rmtp"


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
    parser.add_argument("--package", type=Path, default=DEFAULT_PACKAGE)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--chunk", type=int, default=128)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--skip-load", action="store_true", help="skip SYSTem:REFMEM:LOAD:SD after upload")
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


def make_execute(args: argparse.Namespace):
    if args.port and args.visa_resource:
        raise SystemExit("use either a CDC port or --visa-resource, not both")
    if not args.port and not args.visa_resource:
        raise SystemExit("CDC port or --visa-resource is required")

    close_handles = []
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

    return execute, close_handles


def check_prefix(response: str, expected: str) -> None:
    fields = parse_csv_response(response)
    if not fields or fields[0] != expected:
        raise AssertionError(f"expected prefix {expected!r}, got {response!r}")


def upload_package(execute, payload: bytes, chunk_size: int, skip_load: bool) -> list[Record]:
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

    crc = binascii.crc32(payload) & 0xFFFFFFFF
    path = "/refmem/app_model.rmtp"
    run('SYSTem:STORage:DIRectory:CREate "/refmem"', lambda r: check_prefix(r, "CREATED"))
    run('SYSTem:STORage:FILE:INFO? "/refmem"', check_refmem_directory)
    begin_response = run(f'SYSTem:STORage:FILE:WRITe:BEGIN "{path}",{len(payload)},{crc}',
                         lambda r: check_prefix(r, "OK"))
    begin_fields = parse_csv_response(begin_response)
    if len(begin_fields) < 2:
        raise SystemExit("BEGIN did not return txn id")
    txn_id = begin_fields[1]

    offset = 0
    while offset < len(payload):
        chunk = payload[offset:offset + chunk_size]
        hex_data = chunk.hex().upper()

        def check_data(response: str, expected_offset: int = offset + len(chunk)) -> None:
            fields = parse_csv_response(response)
            if len(fields) < 3 or fields[0] != "OK":
                raise AssertionError(f"bad DATA response {response!r}")
            if fields[1] != txn_id:
                raise AssertionError(f"txn mismatch {fields[1]} != {txn_id}")
            if int(fields[2], 0) != expected_offset:
                raise AssertionError(f"received offset mismatch {fields[2]} != {expected_offset}")

        run(f'SYSTem:STORage:FILE:WRITe:DATA {txn_id},{offset},"{hex_data}"', check_data)
        offset += len(chunk)

    run(f"SYSTem:STORage:FILE:WRITe:END {txn_id}", lambda r: check_prefix(r, "WRITTEN"))
    run(f'SYSTem:STORage:FILE:INFO? "{path}"', lambda r: check_info(r, len(payload)))
    run(f'SYSTem:STORage:FILE:READ? "{path}",0,16', lambda r: check_read(r, payload[:16], len(payload)))
    if not skip_load:
        run("SYSTem:REFMEM:LOAD:SD", check_load_sd)
    return records


def check_info(response: str, expected_size: int) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 8:
        raise AssertionError(f"INFO field_count={len(fields)}")
    if fields[0] != "OK" or fields[3] != "FILE":
        raise AssertionError(f"bad INFO response {response!r}")
    if int(fields[4], 0) != expected_size:
        raise AssertionError(f"size mismatch {fields[4]} != {expected_size}")
    if fields[7] != "/refmem/app_model.rmtp":
        raise AssertionError(f"path mismatch {fields[7]!r}")


def check_refmem_directory(response: str) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 8:
        raise AssertionError(f"directory INFO field_count={len(fields)}")
    if fields[0] != "OK" or fields[3] != "DIR" or fields[7] != "/refmem":
        raise AssertionError(f"/refmem is not a directory: {response!r}")


def check_read(response: str, expected_prefix: bytes, expected_size: int) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 10:
        raise AssertionError(f"READ field_count={len(fields)}")
    if fields[0] != "OK":
        raise AssertionError(f"bad READ response {response!r}")
    if int(fields[4], 0) != len(expected_prefix):
        raise AssertionError(f"returned mismatch {fields[4]}")
    if int(fields[5], 0) != expected_size:
        raise AssertionError(f"file size mismatch {fields[5]}")
    if fields[9] != expected_prefix.hex().upper():
        raise AssertionError("readback prefix mismatch")


def check_load_sd(response: str) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 25:
        raise AssertionError(f"LOAD:SD field_count={len(fields)}")
    if fields[0] != "STAGED":
        raise AssertionError(f"LOAD:SD did not stage: {response!r}")
    if fields[24] != "/refmem/app_model.rmtp":
        raise AssertionError(f"LOAD:SD path mismatch {fields[24]!r}")


def write_outputs(out_dir: Path, records: list[Record], payload: bytes) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    transcript = out_dir / "refmem_pack_write_transcript.txt"
    with transcript.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# RefMem package write validation {datetime.now().isoformat(timespec='seconds')}\n")
        for record in records:
            handle.write(f"> {record.command}\n")
            handle.write(f"< {record.response}\n")
            handle.write(f"# {record.status} {record.reason}\n")
    summary = {
        "passed": all(record.status == "PASS" for record in records),
        "total": len(records),
        "failed": sum(1 for record in records if record.status != "PASS"),
        "size": len(payload),
        "crc32": f"{binascii.crc32(payload) & 0xFFFFFFFF:08X}",
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")


def main() -> int:
    args = parse_args()
    package_path = args.package
    if not package_path.exists():
        raise SystemExit(f"package not found: {package_path}")
    payload = package_path.read_bytes()

    execute, close_handles = make_execute(args)
    try:
        records = upload_package(execute, payload, args.chunk, args.skip_load)
    finally:
        for handle in close_handles:
            try:
                handle.close()
            except Exception:
                pass

    out_dir = args.out_dir or (ROOT / "build" / f"refmem_pack_write_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    write_outputs(out_dir, records, payload)
    failed = sum(1 for record in records if record.status != "PASS")
    print(f"summary: passed={failed == 0} failed={failed} out_dir={out_dir}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
