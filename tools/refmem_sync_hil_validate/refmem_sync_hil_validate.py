#!/usr/bin/env python3
"""Validate two-board RefMem Sync HELLO/EPOCH exchange over SCPI transport."""

from __future__ import annotations

import argparse
import csv
import json
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    serial = None
    SERIAL_IMPORT_ERROR = exc
else:
    SERIAL_IMPORT_ERROR = None


ROOT = Path(__file__).resolve().parents[2]

HELLO_TYPE = 1
EPOCH_TYPE = 2

HELLO_FIELDS = (
    "status",
    "frame_size",
    "source_slot",
    "target_mask",
    "epoch_id",
    "run_id",
    "seq32",
    "payload_crc32",
    "hex",
)

RX_FIELDS = (
    "status",
    "accepted",
    "result",
    "frame_result",
    "frame_type",
    "source_slot",
    "target_mask",
    "epoch_id",
    "run_id",
    "seq32",
    "payload_size",
    "payload_crc32",
)

PEER_FIELDS = (
    "source_slot",
    "seen",
    "hello_seen",
    "epoch_seen",
    "frame_count",
    "duplicate_count",
    "stale_count",
    "drop_count",
    "last_seq32",
    "expected_seq32",
    "last_frame_type",
    "last_compact_time",
    "last_payload_crc32",
)

QUALITY_FIELDS = (
    "local_slot",
    "epoch_id",
    "run_id",
    "frame_rx_count",
    "accepted_count",
    "bad_frame_count",
    "header_error_count",
    "crc_error_count",
    "source_error_count",
    "target_mismatch_count",
    "epoch_mismatch_count",
    "duplicate_count",
    "stale_count",
    "drop_count",
)


@dataclass
class Record:
    board: str
    command: str
    response: str
    status: str
    reason: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-a", help="board A USB CDC serial port, for example COM3")
    parser.add_argument("--port-b", help="board B USB CDC serial port, for example COM4")
    parser.add_argument("--visa-a", help="board A USBTMC VISA resource")
    parser.add_argument("--visa-b", help="board B USBTMC VISA resource")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--slot-a", type=int, default=0)
    parser.add_argument("--slot-b", type=int, default=1)
    parser.add_argument("--epoch", type=int, default=1)
    parser.add_argument("--run", type=int, default=1)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def fields_dict(names: tuple[str, ...], response: str) -> dict[str, str]:
    fields = parse_csv_response(response)
    return {name: fields[index] if index < len(fields) else "" for index, name in enumerate(names)}


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def read_serial_line(ser, deadline: float) -> str | None:
    raw = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(1)
        if not chunk:
            continue
        raw.extend(chunk)
        if chunk == b"\n":
            break
    if not raw:
        return None
    return bytes(raw).decode("utf-8", errors="replace").strip()


def read_serial_response(ser, timeout_s: float) -> str:
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
    except ImportError as exc:  # pragma: no cover - bench dependency
        raise SystemExit("pyvisa is required: python -m pip install pyvisa") from exc

    rm = pyvisa.ResourceManager()
    inst = rm.open_resource(resource)
    inst.timeout = int(timeout_s * 1000)
    inst.write_termination = "\n"
    inst.read_termination = "\n"
    return rm, inst


def make_serial_execute(port: str, args: argparse.Namespace):
    if serial is None:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from SERIAL_IMPORT_ERROR

    ser = serial.Serial(port, args.baud, timeout=0.1, write_timeout=args.timeout)
    time.sleep(args.settle)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    def execute(command: str) -> str:
        ser.reset_input_buffer()
        ser.write((command + "\n").encode("ascii"))
        ser.flush()
        return read_serial_response(ser, args.timeout)

    return execute, [ser]


def make_visa_execute(resource: str, args: argparse.Namespace):
    rm, inst = open_visa_resource(resource, args.timeout)

    def execute(command: str) -> str:
        return str(inst.query(command)).strip()

    return execute, [inst, rm]


def make_execute(name: str, port: str | None, visa: str | None, args: argparse.Namespace):
    if port and visa:
        raise SystemExit(f"use either --port-{name} or --visa-{name}, not both")
    if not port and not visa:
        raise SystemExit(f"--port-{name} or --visa-{name} is required")
    if visa:
        return make_visa_execute(visa, args)
    return make_serial_execute(str(port), args)


def run_checked(records: list[Record],
                board: str,
                execute: Callable[[str], str],
                command: str,
                checker: Callable[[str], None]) -> str:
    response = execute(command)
    try:
        checker(response)
        records.append(Record(board, command, response, "PASS", "ok"))
    except AssertionError as exc:
        records.append(Record(board, command, response, "FAIL", str(exc)))
        print(f"FAIL {board} {command} => {response} ({exc})")
        raise
    print(f"PASS {board} {command} => {response}")
    return response


def expect_field(response: str, names: tuple[str, ...], field: str, expected: int | str) -> None:
    data = fields_dict(names, response)
    value = data.get(field, "")
    if isinstance(expected, int):
        if int(value, 0) != expected:
            raise AssertionError(f"{field}={value!r}, expected {expected}")
    elif value != expected:
        raise AssertionError(f"{field}={value!r}, expected {expected!r}")


def expect_init(response: str, *, slot: int, epoch: int, run: int) -> None:
    fields = parse_csv_response(response)
    if len(fields) < 7 or fields[0] != "OK":
        raise AssertionError(f"bad INIT response {response!r}")
    if int(fields[1], 0) != slot or int(fields[2], 0) != epoch or int(fields[3], 0) != run:
        raise AssertionError(f"INIT state mismatch {response!r}")


def expect_frame(response: str, *, source: int, target: int, epoch: int, run: int) -> None:
    data = fields_dict(HELLO_FIELDS, response)
    if data["status"] != "OK":
        raise AssertionError(f"frame status is {data['status']!r}")
    if int(data["source_slot"], 0) != source:
        raise AssertionError(f"source mismatch {data['source_slot']} != {source}")
    if int(data["target_mask"], 0) != target:
        raise AssertionError(f"target mismatch {data['target_mask']} != {target}")
    if int(data["epoch_id"], 0) != epoch or int(data["run_id"], 0) != run:
        raise AssertionError("epoch/run mismatch")
    if not data["hex"] or len(data["hex"]) != int(data["frame_size"], 0) * 2:
        raise AssertionError("hex length does not match frame_size")


def expect_rx(response: str, *, frame_type: int, source: int, target: int, epoch: int, run: int) -> None:
    data = fields_dict(RX_FIELDS, response)
    if data["status"] != "ACCEPTED" or int(data["accepted"], 0) != 1:
        raise AssertionError(f"RX not accepted: {response!r}")
    for field in ("result", "frame_result"):
        if int(data[field], 0) != 0:
            raise AssertionError(f"{field}={data[field]}")
    if int(data["frame_type"], 0) != frame_type:
        raise AssertionError(f"frame_type={data['frame_type']} expected {frame_type}")
    if int(data["source_slot"], 0) != source or int(data["target_mask"], 0) != target:
        raise AssertionError("source/target mismatch")
    if int(data["epoch_id"], 0) != epoch or int(data["run_id"], 0) != run:
        raise AssertionError("epoch/run mismatch")


def expect_peer(response: str, *, source: int, hello_seen: int, epoch_seen: int, last_type: int) -> None:
    data = fields_dict(PEER_FIELDS, response)
    if int(data["source_slot"], 0) != source:
        raise AssertionError(f"peer source={data['source_slot']} expected {source}")
    if int(data["seen"], 0) != 1:
        raise AssertionError("peer not seen")
    if int(data["hello_seen"], 0) < hello_seen:
        raise AssertionError("peer hello not seen")
    if int(data["epoch_seen"], 0) < epoch_seen:
        raise AssertionError("peer epoch not seen")
    if int(data["last_frame_type"], 0) != last_type:
        raise AssertionError(f"last_frame_type={data['last_frame_type']} expected {last_type}")


def expect_quality(response: str, *, local: int, epoch: int, run: int) -> None:
    data = fields_dict(QUALITY_FIELDS, response)
    if int(data["local_slot"], 0) != local:
        raise AssertionError(f"quality local={data['local_slot']} expected {local}")
    if int(data["epoch_id"], 0) != epoch or int(data["run_id"], 0) != run:
        raise AssertionError("quality epoch/run mismatch")
    if int(data["accepted_count"], 0) < 2:
        raise AssertionError("quality accepted_count did not reach HELLO+EPOCH")
    for field in ("bad_frame_count", "crc_error_count", "target_mismatch_count", "epoch_mismatch_count"):
        if int(data[field], 0) != 0:
            raise AssertionError(f"quality {field}={data[field]}")


def quote_hex(hex_text: str) -> str:
    return '"' + hex_text.replace('"', "") + '"'


def run_exchange(args: argparse.Namespace,
                 execute_a: Callable[[str], str],
                 execute_b: Callable[[str], str]) -> list[Record]:
    records: list[Record] = []
    mask_a = 1 << args.slot_a
    mask_b = 1 << args.slot_b

    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:INITialize {args.slot_a},{args.epoch},{args.run}",
                lambda r: expect_init(r, slot=args.slot_a, epoch=args.epoch, run=args.run))
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:INITialize {args.slot_b},{args.epoch},{args.run}",
                lambda r: expect_init(r, slot=args.slot_b, epoch=args.epoch, run=args.run))

    hello_a = run_checked(records,
                          "A",
                          execute_a,
                          f"SYSTem:REFMEM:SYNC:HELLo? {args.slot_a},{mask_b},1",
                          lambda r: expect_frame(r,
                                                 source=args.slot_a,
                                                 target=mask_b,
                                                 epoch=args.epoch,
                                                 run=args.run))
    hello_a_hex = fields_dict(HELLO_FIELDS, hello_a)["hex"]
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(hello_a_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=HELLO_TYPE,
                                    source=args.slot_a,
                                    target=mask_b,
                                    epoch=args.epoch,
                                    run=args.run))

    hello_b = run_checked(records,
                          "B",
                          execute_b,
                          f"SYSTem:REFMEM:SYNC:HELLo? {args.slot_b},{mask_a},1",
                          lambda r: expect_frame(r,
                                                 source=args.slot_b,
                                                 target=mask_a,
                                                 epoch=args.epoch,
                                                 run=args.run))
    hello_b_hex = fields_dict(HELLO_FIELDS, hello_b)["hex"]
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(hello_b_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=HELLO_TYPE,
                                    source=args.slot_b,
                                    target=mask_a,
                                    epoch=args.epoch,
                                    run=args.run))

    epoch_a = run_checked(records,
                          "A",
                          execute_a,
                          f"SYSTem:REFMEM:SYNC:EPOCh? {args.slot_a},{mask_b},2",
                          lambda r: expect_frame(r,
                                                 source=args.slot_a,
                                                 target=mask_b,
                                                 epoch=args.epoch,
                                                 run=args.run))
    epoch_a_hex = fields_dict(HELLO_FIELDS, epoch_a)["hex"]
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(epoch_a_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=EPOCH_TYPE,
                                    source=args.slot_a,
                                    target=mask_b,
                                    epoch=args.epoch,
                                    run=args.run))

    epoch_b = run_checked(records,
                          "B",
                          execute_b,
                          f"SYSTem:REFMEM:SYNC:EPOCh? {args.slot_b},{mask_a},2",
                          lambda r: expect_frame(r,
                                                 source=args.slot_b,
                                                 target=mask_a,
                                                 epoch=args.epoch,
                                                 run=args.run))
    epoch_b_hex = fields_dict(HELLO_FIELDS, epoch_b)["hex"]
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(epoch_b_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=EPOCH_TYPE,
                                    source=args.slot_b,
                                    target=mask_a,
                                    epoch=args.epoch,
                                    run=args.run))

    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:PEER? {args.slot_b}",
                lambda r: expect_peer(r, source=args.slot_b, hello_seen=1, epoch_seen=1, last_type=EPOCH_TYPE))
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:PEER? {args.slot_a}",
                lambda r: expect_peer(r, source=args.slot_a, hello_seen=1, epoch_seen=1, last_type=EPOCH_TYPE))
    run_checked(records,
                "A",
                execute_a,
                "SYSTem:REFMEM:SYNC:QUALity?",
                lambda r: expect_quality(r, local=args.slot_a, epoch=args.epoch, run=args.run))
    run_checked(records,
                "B",
                execute_b,
                "SYSTem:REFMEM:SYNC:QUALity?",
                lambda r: expect_quality(r, local=args.slot_b, epoch=args.epoch, run=args.run))
    return records


def write_outputs(out_dir: Path, args: argparse.Namespace, records: list[Record]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    passed = all(record.status == "PASS" for record in records)
    with (out_dir / "transcript.txt").open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# RefMem Sync HIL validation {datetime.now().isoformat(timespec='seconds')}\n")
        handle.write(f"# slot_a={args.slot_a} slot_b={args.slot_b} epoch={args.epoch} run={args.run}\n")
        for record in records:
            handle.write(f"# {record.board} {record.status} {record.reason}\n")
            handle.write(f"> {record.command}\n")
            handle.write(f"< {record.response}\n")
    summary = {
        "passed": passed,
        "slot_a": args.slot_a,
        "slot_b": args.slot_b,
        "epoch": args.epoch,
        "run": args.run,
        "records": [record.__dict__ for record in records],
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
    (out_dir / "summary.txt").write_text("PASS\n" if passed else "FAIL\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    handles_a = []
    handles_b = []
    records: list[Record] = []
    try:
        execute_a, handles_a = make_execute("a", args.port_a, args.visa_a, args)
        execute_b, handles_b = make_execute("b", args.port_b, args.visa_b, args)
        records = run_exchange(args, execute_a, execute_b)
    except Exception as exc:
        records.append(Record("HOST", "open/run", f"{type(exc).__name__}: {exc}", "FAIL", "exception"))
        print(f"FAIL HOST open/run => {type(exc).__name__}: {exc}")
    finally:
        for handle in handles_a + handles_b:
            try:
                handle.close()
            except Exception:
                pass

    out_dir = args.out_dir or (ROOT / "build-rtos-multicore-smoke" /
                               f"refmem_sync_hil_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    write_outputs(out_dir, args, records)
    passed = all(record.status == "PASS" for record in records) and len(records) == 14
    print(f"summary: passed={passed} records={len(records)} out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
