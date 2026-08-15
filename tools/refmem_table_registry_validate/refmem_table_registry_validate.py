#!/usr/bin/env python3
"""Validate RefMem TableRegistry staging CRCs against an RMTP package."""

from __future__ import annotations

import argparse
import binascii
import csv
import json
import struct
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import open_serial_port, read_serial_line_idle


MAGIC = b"RMTP"
FORMAT_VERSION = 1
HEADER_SIZE = 64
TABLE_COUNT = 9
TABLE_MASK_ALL = (1 << TABLE_COUNT) - 1
VALIDATION_OWNER_OK = 3
VALIDATION_ACTIVE = 4
VALIDATION_ROLLBACKABLE = 5
TABLE_FLAG_ACTIVE_PRESENT = 0x00000001
TABLE_FLAG_STAGING_PRESENT = 0x00000002
TABLE_FLAG_CRC_OK = 0x00000004
TABLE_FLAG_OWNER_OK = 0x00000008

TABLE_NAMES = (
    "ApplicationMap",
    "BoardCapability",
    "GenericNode",
    "NodeLoad",
    "FbInstance",
    "EventLink",
    "DataLink",
    "DeploymentGate",
    "ConnectionQuality",
)


@dataclass(frozen=True)
class TableDirectoryEntry:
    table_id: int
    offset: int
    size: int
    crc32: int


@dataclass(frozen=True)
class PackageSummary:
    path: Path
    total_size: int
    payload_crc32: int
    package_crc32: int
    entries: tuple[TableDirectoryEntry, ...]


@dataclass
class Record:
    command: str
    response: str
    status: str
    reason: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="USB CDC serial port, for example COM5")
    parser.add_argument("--visa-resource", help="USBTMC VISA resource")
    parser.add_argument("--package", type=Path, required=True, help="local app_model.rmtp package")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--load-timeout", type=float, default=60.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--load-sd", action="store_true", help="run SYSTem:REFMEM:LOAD:SD before CRC checks")
    parser.add_argument("--activate", action="store_true", help="activate staged package and validate active CRCs")
    return parser.parse_args()


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_package(path: Path) -> PackageSummary:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise ValueError(f"RMTP package too small: {len(data)} bytes")

    magic, version, header_size, total_size, table_count, table_dir_size, payload_crc, package_crc = (
        struct.unpack_from("<4sIIIIIII", data, 0)
    )
    if magic != MAGIC:
        raise ValueError(f"bad RMTP magic: {magic!r}")
    if version != FORMAT_VERSION:
        raise ValueError(f"unsupported RMTP version: {version}")
    if header_size != HEADER_SIZE:
        raise ValueError(f"bad RMTP header size: {header_size}")
    if total_size != len(data):
        raise ValueError(f"RMTP total size mismatch: header={total_size} file={len(data)}")
    if table_count != TABLE_COUNT:
        raise ValueError(f"unexpected table count: {table_count}")
    if table_dir_size != table_count * 16:
        raise ValueError(f"bad table directory size: {table_dir_size}")

    package_for_crc = bytearray(data)
    struct.pack_into("<I", package_for_crc, 28, 0)
    if crc32(package_for_crc) != package_crc:
        raise ValueError("RMTP package CRC mismatch")

    payload_start = header_size + table_dir_size
    if payload_start > len(data):
        raise ValueError("RMTP payload offset is out of range")
    if crc32(data[payload_start:]) != payload_crc:
        raise ValueError("RMTP payload CRC mismatch")

    entries: list[TableDirectoryEntry] = []
    seen_mask = 0
    for index in range(table_count):
        cursor = header_size + index * 16
        table_id, offset, size, table_crc = struct.unpack_from("<IIII", data, cursor)
        if table_id >= TABLE_COUNT:
            raise ValueError(f"table id out of range: {table_id}")
        if (seen_mask & (1 << table_id)) != 0:
            raise ValueError(f"duplicate table id: {table_id}")
        if offset < payload_start or offset + size > len(data):
            raise ValueError(f"table {table_id} payload out of range")
        if crc32(data[offset:offset + size]) != table_crc:
            raise ValueError(f"table {table_id} CRC mismatch")
        seen_mask |= 1 << table_id
        entries.append(TableDirectoryEntry(table_id, offset, size, table_crc))

    if seen_mask != TABLE_MASK_ALL:
        raise ValueError(f"incomplete table mask: 0x{seen_mask:03X}")

    entries.sort(key=lambda entry: entry.table_id)
    return PackageSummary(path=path,
                          total_size=total_size,
                          payload_crc32=payload_crc,
                          package_crc32=package_crc,
                          entries=tuple(entries))


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

        def execute(command: str, timeout_s: float | None = None) -> str:
            inst.timeout = int((timeout_s if timeout_s is not None else args.timeout) * 1000)
            return str(inst.query(command)).strip()

        return execute, [inst, rm]

    serial_context = open_serial_port(args.port, args.baud, args.timeout, args.settle)
    ser = serial_context.__enter__()

    def execute(command: str, timeout_s: float | None = None) -> str:
        ser.reset_input_buffer()
        ser.write((command + "\n").encode("ascii"))
        ser.flush()
        return read_response(ser, timeout_s if timeout_s is not None else args.timeout)

    return execute, [serial_context]


def load_status_fields(response: str) -> list[str]:
    fields = parse_csv_response(response)
    if len(fields) != 24:
        raise AssertionError(f"LOAD:STATus field_count={len(fields)} expected=24")
    return fields


def table_fields(response: str) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != 18:
        raise AssertionError(f"TABle field_count={len(fields)} expected=18")
    try:
        return [int(field.strip().strip('"'), 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"TABle response contains non-integer field: {response!r}") from exc


def check_load_sd_response(response: str, package: PackageSummary) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 25:
        raise AssertionError(f"LOAD:SD field_count={len(fields)} expected=25")
    if fields[0] != "STAGED":
        raise AssertionError(f"LOAD:SD did not stage: {response!r}")
    if int(fields[12], 0) != package.package_crc32:
        raise AssertionError(f"staging package CRC mismatch: {fields[12]} != 0x{package.package_crc32:08X}")
    if fields[24] != "/refmem/app_model.rmtp":
        raise AssertionError(f"unexpected LOAD:SD path: {fields[24]!r}")


def check_load_status(response: str, package: PackageSummary) -> None:
    fields = load_status_fields(response)
    if int(fields[3], 0) != 0:
        raise AssertionError(f"RefMem load mode is not IDLE: {fields[3]}")
    if int(fields[4], 0) != 2:
        raise AssertionError(f"RefMem staging is not VALIDATED: {fields[4]}")
    if int(fields[11], 0) != package.package_crc32:
        raise AssertionError(f"staging package CRC mismatch: {fields[11]} != 0x{package.package_crc32:08X}")
    if int(fields[12], 0) != 0:
        raise AssertionError(f"staging lint error count is non-zero: {fields[12]}")
    if int(fields[21], 0) != 0:
        raise AssertionError(f"last error is non-zero: {fields[21]}")


def check_active_load_status(response: str, package: PackageSummary) -> None:
    fields = load_status_fields(response)
    if int(fields[3], 0) != 0:
        raise AssertionError(f"RefMem load mode is not IDLE after activation: {fields[3]}")
    if int(fields[10], 0) != package.package_crc32:
        raise AssertionError(
            f"active package CRC mismatch after activation: {fields[10]} != 0x{package.package_crc32:08X}"
        )
    if int(fields[21], 0) != 0:
        raise AssertionError(f"last error is non-zero after activation: {fields[21]}")


def check_table_response(response: str, expected: TableDirectoryEntry, *, activated: bool = False) -> None:
    fields = table_fields(response)
    if activated:
        checks = {
            "version": fields[0] == 1,
            "table_count": fields[1] == TABLE_COUNT,
            "active_mask": fields[2] == TABLE_MASK_ALL,
            "staging_mask": fields[3] == 0,
            "registry_error": fields[5] == 0,
            "table_id": fields[6] == expected.table_id,
            "image_size": fields[10] == expected.size,
            "active_crc": fields[11] == expected.crc32,
            "staging_crc": fields[12] == 0,
            "validation_state": fields[13] == VALIDATION_ACTIVE,
            "last_result": fields[15] == 0,
            "flags": (fields[17] & (TABLE_FLAG_ACTIVE_PRESENT | TABLE_FLAG_CRC_OK | TABLE_FLAG_OWNER_OK)) ==
                     (TABLE_FLAG_ACTIVE_PRESENT | TABLE_FLAG_CRC_OK | TABLE_FLAG_OWNER_OK),
        }
        bad = [name for name, ok in checks.items() if not ok]
        if bad:
            table_name = TABLE_NAMES[expected.table_id]
            raise AssertionError(f"{table_name} active registry mismatch {bad}: {fields}")
        return

    checks = {
        "version": fields[0] == 1,
        "table_count": fields[1] == TABLE_COUNT,
        "active_mask": fields[2] == TABLE_MASK_ALL,
        "staging_mask": fields[3] == TABLE_MASK_ALL,
        "registry_error": fields[5] == 0,
        "table_id": fields[6] == expected.table_id,
        "image_size": fields[10] == expected.size,
        "staging_crc": fields[12] == expected.crc32,
        "validation_state": fields[13] == VALIDATION_OWNER_OK,
        "last_result": fields[15] == 0,
        "flags": (fields[17] & (TABLE_FLAG_STAGING_PRESENT | TABLE_FLAG_CRC_OK | TABLE_FLAG_OWNER_OK)) ==
                 (TABLE_FLAG_STAGING_PRESENT | TABLE_FLAG_CRC_OK | TABLE_FLAG_OWNER_OK),
    }
    bad = [name for name, ok in checks.items() if not ok]
    if bad:
        table_name = TABLE_NAMES[expected.table_id]
        raise AssertionError(f"{table_name} registry mismatch {bad}: {fields}")


def image_fields(response: str) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != 9:
        raise AssertionError(f"TABle:IMAGe field_count={len(fields)} expected=9")
    try:
        return [int(field.strip().strip('"'), 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"TABle:IMAGe response contains non-integer field: {response!r}") from exc


def check_image_response(response: str, *, role: int, state: int, table_mask: int, package: PackageSummary) -> None:
    fields = image_fields(response)
    checks = {
        "version": fields[0] == 1,
        "role": fields[1] == role,
        "state": fields[2] == state,
        "table_mask": fields[3] == table_mask,
        "package_crc32": fields[4] == (package.package_crc32 if table_mask != 0 else 0),
        "last_result": fields[7] == 0,
    }
    bad = [name for name, ok in checks.items() if not ok]
    if bad:
        raise AssertionError(f"image descriptor mismatch {bad}: {fields}")


def check_table_view_response(response: str, expected: TableDirectoryEntry, package: PackageSummary) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 9:
        raise AssertionError(f"TABle:VIEW field_count={len(fields)} expected=9")
    try:
        values = [int(field.strip().strip('"'), 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"TABle:VIEW response contains non-integer field: {response!r}") from exc
    checks = {
        "version": values[0] == 1,
        "role": values[1] == 0,
        "table_id": values[2] == expected.table_id,
        "package_crc32": values[4] == package.package_crc32,
        "table_crc32": values[5] == expected.crc32,
        "offset": values[6] == expected.offset,
        "size": values[7] == expected.size,
        "first_u32": values[8] == FORMAT_VERSION,
    }
    bad = [name for name, ok in checks.items() if not ok]
    if bad:
        raise AssertionError(f"{TABLE_NAMES[expected.table_id]} table view mismatch {bad}: {values}")


def check_activate_response(response: str, package: PackageSummary) -> None:
    fields = parse_csv_response(response)
    if len(fields) != 34:
        raise AssertionError(f"LOAD:ACTivate field_count={len(fields)} expected=34")
    if fields[0] != "ACTIVE":
        raise AssertionError(f"LOAD:ACTivate did not activate: {response!r}")
    values = [int(field.strip().strip('"'), 0) for field in fields[1:]]
    if values[0] != 1 or values[1] != TABLE_COUNT:
        raise AssertionError(f"unexpected registry header: {values[:6]}")
    if values[2] != TABLE_MASK_ALL or values[3] != 0 or values[5] != 0:
        raise AssertionError(f"unexpected registry masks/error: {values[:6]}")
    active = values[6:15]
    staging = values[15:24]
    rollbackable = values[24:33]
    if active[1] != 0 or active[2] != VALIDATION_ACTIVE:
        raise AssertionError(f"active image descriptor invalid: {active}")
    if active[3] != TABLE_MASK_ALL or active[4] != package.package_crc32:
        raise AssertionError(f"active image package mismatch: {active}")
    if staging[1] != 1 or staging[2] != 0 or staging[3] != 0:
        raise AssertionError(f"staging image descriptor was not cleared: {staging}")
    if rollbackable[1] != 2:
        raise AssertionError(f"rollbackable image role mismatch: {rollbackable}")


def run_validation(execute,
                   package: PackageSummary,
                   *,
                   load_sd: bool,
                   activate: bool,
                   load_timeout_s: float) -> list[Record]:
    records: list[Record] = []

    def run(command: str, checker, timeout_s: float | None = None) -> None:
        response = execute(command, timeout_s)
        try:
            checker(response)
            record = Record(command, response, "PASS", "ok")
        except AssertionError as exc:
            record = Record(command, response, "FAIL", str(exc))
        records.append(record)
        print(f"{record.status} {command} => {response}")
        if record.status != "PASS":
            raise SystemExit(1)
        time.sleep(0.05)

    if load_sd:
        run("SYSTem:REFMEM:LOAD:SD", lambda response: check_load_sd_response(response, package), load_timeout_s)
        run("SYSTem:COMMand:ACK?",
            lambda response: check_command_ack(response, command_type=16, payload_size=76))

    run("SYSTem:REFMEM:LOAD:STATus?", lambda response: check_load_status(response, package))
    if activate:
        run("SYSTem:REFMEM:LOAD:ACTivate", lambda response: check_activate_response(response, package))
        run("SYSTem:COMMand:ACK?",
            lambda response: check_command_ack(response, command_type=17, payload_size=16))
        run("SYSTem:REFMEM:LOAD:STATus?", lambda response: check_active_load_status(response, package))
        run("SYSTem:REFMEM:TABle:IMAGe? 0",
            lambda response: check_image_response(response,
                                                  role=0,
                                                  state=VALIDATION_ACTIVE,
                                                  table_mask=TABLE_MASK_ALL,
                                                  package=package))
        run("SYSTem:REFMEM:TABle:IMAGe? 1",
            lambda response: check_image_response(response,
                                                  role=1,
                                                  state=0,
                                                  table_mask=0,
                                                  package=package))
        for table_id in (0, 3):
            entry = package.entries[table_id]
            run(f"SYSTem:REFMEM:TABle:VIEW? 0,{table_id}",
                lambda response, expected=entry: check_table_view_response(response,
                                                                           expected,
                                                                           package))

    for entry in package.entries:
        run(f"SYSTem:REFMEM:TABle? {entry.table_id}",
            lambda response, expected=entry: check_table_response(response,
                                                                  expected,
                                                                  activated=activate))
    run("SYSTem:ERRor?", check_no_scpi_error)
    return records


def check_command_ack(response: str, *, command_type: int, payload_size: int) -> None:
    fields = parse_csv_response(response)
    if len(fields) < 28:
        raise AssertionError(f"COMMand:ACK field_count={len(fields)} expected>=28")
    values = [int(field.strip().strip('"'), 0) for field in fields]
    if values[1] != 4:
        raise AssertionError(f"command was not ACKED: {response!r}")
    if values[7] != command_type or values[11] != payload_size:
        raise AssertionError(f"unexpected command type/size: {values[7]}/{values[11]}")
    if values[18] == 0 or values[19] != 0 or values[22] != 0:
        raise AssertionError(f"unexpected ACK/NACK fields: {response!r}")


def check_no_scpi_error(response: str) -> None:
    fields = parse_csv_response(response)
    if len(fields) < 2 or fields[0] != "0":
        raise AssertionError(f"SCPI error queue is not clean: {response!r}")


def write_outputs(out_dir: Path, records: list[Record], package: PackageSummary) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    transcript = out_dir / "refmem_table_registry_validate_transcript.txt"
    with transcript.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# RefMem TableRegistry validation {datetime.now().isoformat(timespec='seconds')}\n")
        handle.write(f"# package={package.path}\n")
        handle.write(f"# package_crc32=0x{package.package_crc32:08X}\n")
        for entry in package.entries:
            handle.write(
                f"# table {entry.table_id} {TABLE_NAMES[entry.table_id]} "
                f"offset={entry.offset} size={entry.size} crc32=0x{entry.crc32:08X}\n"
            )
        for record in records:
            handle.write(f"> {record.command}\n")
            handle.write(f"< {record.response}\n")
            handle.write(f"# {record.status} {record.reason}\n")

    summary = {
        "passed": all(record.status == "PASS" for record in records),
        "total": len(records),
        "failed": sum(1 for record in records if record.status != "PASS"),
        "package": str(package.path),
        "package_size": package.total_size,
        "package_crc32": f"{package.package_crc32:08X}",
        "payload_crc32": f"{package.payload_crc32:08X}",
        "table_crc32": {
            TABLE_NAMES[entry.table_id]: f"{entry.crc32:08X}" for entry in package.entries
        },
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")


def main() -> int:
    args = parse_args()
    if not args.package.exists():
        raise SystemExit(f"package not found: {args.package}")
    package = parse_package(args.package)

    execute, close_handles = make_execute(args)
    records: list[Record] = []
    try:
        records = run_validation(execute,
                                 package,
                                 load_sd=args.load_sd,
                                 activate=args.activate,
                                 load_timeout_s=args.load_timeout)
    finally:
        for handle in close_handles:
            try:
                handle.close() if hasattr(handle, "close") else handle.__exit__(None, None, None)
            except Exception:
                pass

    out_dir = args.out_dir or (ROOT / "build" / f"refmem_table_registry_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    write_outputs(out_dir, records, package)
    failed = sum(1 for record in records if record.status != "PASS")
    print(f"summary: passed={failed == 0} failed={failed} out_dir={out_dir}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
