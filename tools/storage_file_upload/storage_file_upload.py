#!/usr/bin/env python3
"""Upload one or more local files through generic Storage SCPI."""

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


@dataclass
class Record:
    command: str
    response: str
    status: str
    reason: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--chunk", type=int, default=256)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--file", nargs=2, action="append", metavar=("LOCAL", "REMOTE"),
                        required=True, help="upload LOCAL file to REMOTE absolute SD path")
    return parser.parse_args()


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def read_response(ser, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_serial_line_idle(ser, deadline)
        if line is None or is_log_line(line):
            continue
        return line
    return "<timeout>"


def execute(ser, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_response(ser, timeout_s)


def wait_storage_job_idle(ser, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    last_response = ""
    while time.monotonic() < deadline:
        last_response = execute(ser, "SYSTem:STORage:JOB?", min(1.0, timeout_s))
        fields = parse_csv_response(last_response)
        if fields and fields[0] not in ("QUEUED", "RUNNING"):
            return last_response
        time.sleep(0.1)
    raise TimeoutError(f"storage job did not finish: {last_response!r}")


def parent_dirs(remote_path: str) -> list[str]:
    parts = [part for part in remote_path.strip("/").split("/")[:-1] if part]
    dirs: list[str] = []
    current = ""
    for part in parts:
        current += "/" + part
        dirs.append(current)
    return dirs


def append_record(records: list[Record],
                  command: str,
                  response: str,
                  checker,
                  *,
                  allow_fail: bool = False) -> None:
    status = "PASS"
    reason = "ok"
    try:
        checker(response)
    except Exception as exc:
        if allow_fail:
            status = "INFO"
            reason = str(exc)
        else:
            status = "FAIL"
            reason = str(exc)
    records.append(Record(command, response, status, reason))
    print(f"{status} {command[:96]} => {response}")
    if status == "FAIL":
        raise AssertionError(reason)


def expect_prefix(response: str, expected: str) -> None:
    fields = parse_csv_response(response)
    if not fields or fields[0] != expected:
        raise AssertionError(f"expected prefix {expected!r}, got {response!r}")


def expect_begin(response: str) -> int:
    fields = parse_csv_response(response)
    if len(fields) < 2 or fields[0] != "OK":
        raise AssertionError(f"bad write begin {response!r}")
    return int(fields[1], 0)


def expect_data(response: str, txn_id: int, written: int) -> None:
    fields = parse_csv_response(response)
    if len(fields) < 3 or fields[0] != "OK":
        raise AssertionError(f"bad write data {response!r}")
    if int(fields[1], 0) != txn_id or int(fields[2], 0) != written:
        raise AssertionError(f"write progress mismatch {response!r}")


def expect_info(response: str, remote_path: str, size: int) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 8 or fields[0] != "OK" or fields[3] != "FILE":
        raise AssertionError(f"bad file info {response!r}")
    if int(fields[4], 0) != size:
        raise AssertionError(f"size mismatch {fields[4]} != {size}")
    if fields[7] != remote_path:
        raise AssertionError(f"path mismatch {fields[7]!r} != {remote_path!r}")


def abort_active_write_transaction(ser, records: list[Record], timeout_s: float) -> None:
    command = "SYSTem:STORage:FILE:WRITe:STATus?"
    response = execute(ser, command, timeout_s)
    fields = parse_csv_response(response)
    records.append(Record(command, response, "PASS" if fields else "INFO", "preflight"))
    print(f"{records[-1].status} {command} => {response}")
    if len(fields) < 2 or int(fields[0], 0) == 0:
        return

    txn_id = int(fields[1], 0)
    command = f"SYSTem:STORage:FILE:WRITe:ABORt {txn_id}"
    response = execute(ser, command, timeout_s)
    append_record(records, command, response, lambda r: expect_prefix(r, "OK"), allow_fail=True)


def upload_one(ser,
               records: list[Record],
               local_path: Path,
               remote_path: str,
               chunk_size: int,
               timeout_s: float) -> None:
    payload = local_path.read_bytes()
    crc = binascii.crc32(payload) & 0xFFFFFFFF

    for directory in parent_dirs(remote_path):
        command = f'SYSTem:STORage:DIRectory:CREate "{directory}"'
        response = execute(ser, command, timeout_s)
        append_record(records, command, response, lambda r: expect_prefix(r, "CREATED"), allow_fail=True)
        job_response = wait_storage_job_idle(ser, timeout_s)
        records.append(Record("SYSTem:STORage:JOB?", job_response, "PASS", "idle"))
        print(f"PASS SYSTem:STORage:JOB? => {job_response}")

    command = f'SYSTem:STORage:FILE:DELete "{remote_path}"'
    response = execute(ser, command, timeout_s)
    append_record(records, command, response, lambda r: expect_prefix(r, "DELETED"), allow_fail=True)
    job_response = wait_storage_job_idle(ser, timeout_s)
    records.append(Record("SYSTem:STORage:JOB?", job_response, "PASS", "idle"))
    print(f"PASS SYSTem:STORage:JOB? => {job_response}")

    command = f'SYSTem:STORage:FILE:WRITe:BEGIN "{remote_path}",{len(payload)},{crc}'
    response = execute(ser, command, timeout_s)
    txn_id = expect_begin(response)
    records.append(Record(command, response, "PASS", "ok"))
    print(f"PASS {command[:96]} => {response}")

    for offset in range(0, len(payload), chunk_size):
        chunk = payload[offset:offset + chunk_size]
        command = f'SYSTem:STORage:FILE:WRITe:DATA {txn_id},{offset},"{chunk.hex().upper()}"'
        response = execute(ser, command, timeout_s)
        written = offset + len(chunk)
        try:
            expect_data(response, txn_id, written)
        except Exception as exc:
            records.append(Record(command, response, "FAIL", str(exc)))
            print(f"FAIL {command[:96]} => {response}")
            raise
        records.append(Record(command, response, "PASS", "ok"))
        if written == len(payload) or (offset // chunk_size) % 64 == 0:
            print(f"PASS DATA txn={txn_id} written={written}/{len(payload)} => {response}")

    command = f"SYSTem:STORage:FILE:WRITe:END {txn_id}"
    response = execute(ser, command, timeout_s)
    append_record(records, command, response, lambda r: expect_prefix(r, "WRITTEN"))

    command = f'SYSTem:STORage:FILE:INFO? "{remote_path}"'
    response = execute(ser, command, timeout_s)
    append_record(records, command, response, lambda r: expect_info(r, remote_path, len(payload)))


def write_outputs(out_dir: Path, records: list[Record]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    transcript = out_dir / "storage_file_upload_transcript.txt"
    with transcript.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# Storage file upload {datetime.now().isoformat(timespec='seconds')}\n")
        for record in records:
            handle.write(f"> {record.command}\n")
            handle.write(f"< {record.response}\n")
            handle.write(f"# {record.status} {record.reason}\n")
    summary = {
        "passed": all(record.status != "FAIL" for record in records),
        "total": len(records),
        "failed": sum(1 for record in records if record.status == "FAIL"),
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")


def main() -> int:
    args = parse_args()
    if args.chunk <= 0 or args.chunk > 320:
        raise SystemExit("--chunk must be in 1..320 to fit the SCPI input buffer")

    records: list[Record] = []
    with open_serial_port(args.port, args.baud, args.timeout, args.settle) as ser:
        abort_active_write_transaction(ser, records, args.timeout)
        for local, remote in args.file:
            local_path = Path(local)
            if not local_path.exists():
                raise SystemExit(f"local file not found: {local_path}")
            if not remote.startswith("/"):
                raise SystemExit(f"remote path must be absolute: {remote}")
            upload_one(ser, records, local_path, remote, args.chunk, args.timeout)

    out_dir = args.out_dir or (ROOT / "build" / f"storage_file_upload_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    write_outputs(out_dir, records)
    failed = sum(1 for record in records if record.status == "FAIL")
    print(f"summary: passed={failed == 0} failed={failed} out_dir={out_dir}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
