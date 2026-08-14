#!/usr/bin/env python3
"""Validate two-board RefMem Sync HELLO/EPOCH/DELTA/ACK exchange over SCPI."""

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
DELTA_TYPE = 3
ACK_NACK_TYPE = 5

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

MIRROR_FIELDS = (
    "query_source_slot",
    "visible",
    "source_slot",
    "slot_id",
    "payload_kind",
    "slot_seq",
    "field_id",
    "field_offset",
    "field_width",
    "dirty_mask",
    "value_u32",
    "value_crc32",
    "last_frame_seq32",
    "committed_count",
    "visible_count",
)

ACK_FRAME_FIELDS = (
    "status",
    "frame_size",
    "source_slot",
    "target_mask",
    "epoch_id",
    "run_id",
    "seq32",
    "payload_crc32",
    "ack_seq32",
    "accepted",
    "rx_result",
    "rx_frame_result",
    "hex",
)

ACK_STATUS_FIELDS = (
    "query_source_slot",
    "seen",
    "source_slot",
    "command_seq",
    "delta_seq32",
    "taken_flags",
    "ack_flags",
    "nack_flags",
    "busy_flags",
    "timeout_flags",
    "last_reason",
    "last_reason_slot",
    "evidence_index",
    "last_frame_seq32",
    "received_count",
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
    parser.add_argument("--delta-a", type=int, default=0xA5000001)
    parser.add_argument("--delta-b", type=int, default=0xB6000002)
    parser.add_argument("--expected-build")
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


def expect_build(response: str, *, expected: str | None) -> None:
    fields = parse_csv_response(response)
    if not fields or not fields[0]:
        raise AssertionError(f"bad build response {response!r}")
    if expected is not None and fields[0] != expected:
        raise AssertionError(f"build {fields[0]!r} expected {expected!r}")


def expect_claim(response: str) -> None:
    fields = parse_csv_response(response)
    if len(fields) < 9:
        raise AssertionError(f"claim response too short {response!r}")
    if int(fields[4], 0) == 0:
        raise AssertionError("claim assigned_count is zero")
    if int(fields[8], 0) == 0:
        raise AssertionError("claim map_crc32 is zero")


def expect_adapter(response: str) -> None:
    fields = parse_csv_response(response)
    if len(fields) < 18:
        raise AssertionError(f"adapter response too short {response!r}")
    if int(fields[0], 0) != 1:
        raise AssertionError(f"adapter_id={fields[0]} expected 1")
    if int(fields[3], 0) < 64:
        raise AssertionError(f"adapter max_payload too small: {fields[3]}")


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


def expect_rx_rejected(response: str,
                       *,
                       frame_type: int,
                       source: int,
                       target: int,
                       epoch: int,
                       run: int,
                       result: int,
                       frame_result: int) -> None:
    data = fields_dict(RX_FIELDS, response)
    if data["status"] != "REJECTED" or int(data["accepted"], 0) != 0:
        raise AssertionError(f"RX not rejected: {response!r}")
    if int(data["result"], 0) != result:
        raise AssertionError(f"result={data['result']} expected {result}")
    if int(data["frame_result"], 0) != frame_result:
        raise AssertionError(f"frame_result={data['frame_result']} expected {frame_result}")
    if int(data["frame_type"], 0) != frame_type:
        raise AssertionError(f"frame_type={data['frame_type']} expected {frame_type}")
    if int(data["source_slot"], 0) != source or int(data["target_mask"], 0) != target:
        raise AssertionError("source/target mismatch")
    if int(data["epoch_id"], 0) != epoch or int(data["run_id"], 0) != run:
        raise AssertionError("epoch/run mismatch")


def expect_ack_frame(response: str,
                     *,
                     source: int,
                     target: int,
                     epoch: int,
                     run: int,
                     seq: int,
                     ack_seq: int,
                     accepted: int,
                     rx_result: int,
                     rx_frame_result: int) -> None:
    data = fields_dict(ACK_FRAME_FIELDS, response)
    if data["status"] != "OK":
        raise AssertionError(f"ack frame status is {data['status']!r}")
    if int(data["source_slot"], 0) != source or int(data["target_mask"], 0) != target:
        raise AssertionError("ack source/target mismatch")
    if int(data["epoch_id"], 0) != epoch or int(data["run_id"], 0) != run:
        raise AssertionError("ack epoch/run mismatch")
    if int(data["seq32"], 0) != seq or int(data["ack_seq32"], 0) != ack_seq:
        raise AssertionError("ack seq mismatch")
    if int(data["accepted"], 0) != accepted:
        raise AssertionError(f"ack accepted={data['accepted']} expected {accepted}")
    if int(data["rx_result"], 0) != rx_result:
        raise AssertionError(f"ack rx_result={data['rx_result']} expected {rx_result}")
    if int(data["rx_frame_result"], 0) != rx_frame_result:
        raise AssertionError(f"ack rx_frame_result={data['rx_frame_result']} expected {rx_frame_result}")
    if not data["hex"] or len(data["hex"]) != int(data["frame_size"], 0) * 2:
        raise AssertionError("ack hex length does not match frame_size")


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


def expect_quality(response: str, *, local: int, epoch: int, run: int, accepted_min: int) -> None:
    data = fields_dict(QUALITY_FIELDS, response)
    if int(data["local_slot"], 0) != local:
        raise AssertionError(f"quality local={data['local_slot']} expected {local}")
    if int(data["epoch_id"], 0) != epoch or int(data["run_id"], 0) != run:
        raise AssertionError("quality epoch/run mismatch")
    if int(data["accepted_count"], 0) < accepted_min:
        raise AssertionError(f"quality accepted_count did not reach {accepted_min}")
    for field in ("bad_frame_count", "crc_error_count", "target_mismatch_count", "epoch_mismatch_count"):
        if int(data[field], 0) != 0:
            raise AssertionError(f"quality {field}={data[field]}")


def expect_mirror(response: str, *, source: int, slot: int, slot_seq: int, field: int, value: int) -> None:
    data = fields_dict(MIRROR_FIELDS, response)
    if int(data["query_source_slot"], 0) != source or int(data["source_slot"], 0) != source:
        raise AssertionError(f"mirror source mismatch: {response!r}")
    if int(data["visible"], 0) != 1:
        raise AssertionError("mirror not visible")
    if int(data["slot_id"], 0) != slot:
        raise AssertionError(f"mirror slot={data['slot_id']} expected {slot}")
    if int(data["slot_seq"], 0) != slot_seq:
        raise AssertionError(f"mirror slot_seq={data['slot_seq']} expected {slot_seq}")
    if int(data["field_id"], 0) != field:
        raise AssertionError(f"mirror field={data['field_id']} expected {field}")
    if int(data["field_width"], 0) != 4:
        raise AssertionError(f"mirror width={data['field_width']} expected 4")
    if int(data["value_u32"], 0) != value:
        raise AssertionError(f"mirror value={data['value_u32']} expected {value}")
    if int(data["committed_count"], 0) < 1 or int(data["visible_count"], 0) < 1:
        raise AssertionError("mirror commit/visible count did not advance")


def expect_ack_status(response: str,
                      *,
                      source: int,
                      delta_seq: int,
                      taken_flags: int,
                      ack_flags: int,
                      nack_flags: int,
                      reason: int,
                      reason_slot: int,
                      frame_seq: int) -> None:
    data = fields_dict(ACK_STATUS_FIELDS, response)
    if int(data["query_source_slot"], 0) != source or int(data["source_slot"], 0) != source:
        raise AssertionError(f"ack status source mismatch: {response!r}")
    if int(data["seen"], 0) != 1:
        raise AssertionError("ack status not seen")
    if int(data["delta_seq32"], 0) != delta_seq:
        raise AssertionError(f"ack delta_seq32={data['delta_seq32']} expected {delta_seq}")
    if int(data["taken_flags"], 0) != taken_flags:
        raise AssertionError(f"ack taken_flags={data['taken_flags']} expected {taken_flags}")
    if int(data["ack_flags"], 0) != ack_flags:
        raise AssertionError(f"ack_flags={data['ack_flags']} expected {ack_flags}")
    if int(data["nack_flags"], 0) != nack_flags:
        raise AssertionError(f"nack_flags={data['nack_flags']} expected {nack_flags}")
    if int(data["last_reason"], 0) != reason:
        raise AssertionError(f"ack reason={data['last_reason']} expected {reason}")
    if int(data["last_reason_slot"], 0) != reason_slot:
        raise AssertionError(f"ack reason_slot={data['last_reason_slot']} expected {reason_slot}")
    if int(data["last_frame_seq32"], 0) != frame_seq:
        raise AssertionError(f"ack frame_seq={data['last_frame_seq32']} expected {frame_seq}")
    if int(data["received_count"], 0) < 1:
        raise AssertionError("ack received_count did not advance")


def quote_hex(hex_text: str) -> str:
    return '"' + hex_text.replace('"', "") + '"'


def corrupt_last_payload_byte(hex_text: str) -> str:
    data = bytearray.fromhex(hex_text)
    if len(data) <= 36:
        raise AssertionError("frame too short to corrupt payload")
    data[-1] ^= 0x55
    return data.hex().upper()


def run_exchange(args: argparse.Namespace,
                 execute_a: Callable[[str], str],
                 execute_b: Callable[[str], str]) -> list[Record]:
    records: list[Record] = []
    mask_a = 1 << args.slot_a
    mask_b = 1 << args.slot_b

    run_checked(records,
                "A",
                execute_a,
                "SYST:FW:BUILD?",
                lambda r: expect_build(r, expected=args.expected_build))
    run_checked(records,
                "B",
                execute_b,
                "SYST:FW:BUILD?",
                lambda r: expect_build(r, expected=args.expected_build))
    run_checked(records,
                "A",
                execute_a,
                "SYSTem:REFMEM:CLAIM? 0",
                expect_claim)
    run_checked(records,
                "B",
                execute_b,
                "SYSTem:REFMEM:CLAIM? 0",
                expect_claim)
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
    run_checked(records,
                "A",
                execute_a,
                "SYSTem:REFMEM:SYNC:ADAPter?",
                expect_adapter)
    run_checked(records,
                "B",
                execute_b,
                "SYSTem:REFMEM:SYNC:ADAPter?",
                expect_adapter)

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

    delta_a = run_checked(records,
                          "A",
                          execute_a,
                          f"SYSTem:REFMEM:SYNC:DELTa? {args.slot_a},{mask_b},3,{args.slot_a},1,1,{args.delta_a},1",
                          lambda r: expect_frame(r,
                                                 source=args.slot_a,
                                                 target=mask_b,
                                                 epoch=args.epoch,
                                                 run=args.run))
    delta_a_hex = fields_dict(HELLO_FIELDS, delta_a)["hex"]
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(delta_a_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=DELTA_TYPE,
                                    source=args.slot_a,
                                    target=mask_b,
                                    epoch=args.epoch,
                                    run=args.run))

    delta_b = run_checked(records,
                          "B",
                          execute_b,
                          f"SYSTem:REFMEM:SYNC:DELTa? {args.slot_b},{mask_a},3,{args.slot_b},1,1,{args.delta_b},1",
                          lambda r: expect_frame(r,
                                                 source=args.slot_b,
                                                 target=mask_a,
                                                 epoch=args.epoch,
                                                 run=args.run))
    delta_b_hex = fields_dict(HELLO_FIELDS, delta_b)["hex"]
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(delta_b_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=DELTA_TYPE,
                                    source=args.slot_b,
                                    target=mask_a,
                                    epoch=args.epoch,
                                    run=args.run))

    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:MIRRor? {args.slot_b}",
                lambda r: expect_mirror(r,
                                        source=args.slot_b,
                                        slot=args.slot_b,
                                        slot_seq=1,
                                        field=1,
                                        value=args.delta_b))
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:MIRRor? {args.slot_a}",
                lambda r: expect_mirror(r,
                                        source=args.slot_a,
                                        slot=args.slot_a,
                                        slot_seq=1,
                                        field=1,
                                        value=args.delta_a))

    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:PEER? {args.slot_b}",
                lambda r: expect_peer(r, source=args.slot_b, hello_seen=1, epoch_seen=1, last_type=DELTA_TYPE))
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:PEER? {args.slot_a}",
                lambda r: expect_peer(r, source=args.slot_a, hello_seen=1, epoch_seen=1, last_type=DELTA_TYPE))
    run_checked(records,
                "A",
                execute_a,
                "SYSTem:REFMEM:SYNC:QUALity?",
                lambda r: expect_quality(r, local=args.slot_a, epoch=args.epoch, run=args.run, accepted_min=3))
    run_checked(records,
                "B",
                execute_b,
                "SYSTem:REFMEM:SYNC:QUALity?",
                lambda r: expect_quality(r, local=args.slot_b, epoch=args.epoch, run=args.run, accepted_min=3))

    ack_b = run_checked(records,
                        "B",
                        execute_b,
                        f"SYSTem:REFMEM:SYNC:ACK? {args.slot_b},{mask_a},4",
                        lambda r: expect_ack_frame(r,
                                                   source=args.slot_b,
                                                   target=mask_a,
                                                   epoch=args.epoch,
                                                   run=args.run,
                                                   seq=4,
                                                   ack_seq=3,
                                                   accepted=1,
                                                   rx_result=0,
                                                   rx_frame_result=0))
    ack_b_hex = fields_dict(ACK_FRAME_FIELDS, ack_b)["hex"]
    ack_a = run_checked(records,
                        "A",
                        execute_a,
                        f"SYSTem:REFMEM:SYNC:ACK? {args.slot_a},{mask_b},4",
                        lambda r: expect_ack_frame(r,
                                                   source=args.slot_a,
                                                   target=mask_b,
                                                   epoch=args.epoch,
                                                   run=args.run,
                                                   seq=4,
                                                   ack_seq=3,
                                                   accepted=1,
                                                   rx_result=0,
                                                   rx_frame_result=0))
    ack_a_hex = fields_dict(ACK_FRAME_FIELDS, ack_a)["hex"]
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(ack_b_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=ACK_NACK_TYPE,
                                    source=args.slot_b,
                                    target=mask_a,
                                    epoch=args.epoch,
                                    run=args.run))
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:ACK:STATus? {args.slot_b}",
                lambda r: expect_ack_status(r,
                                            source=args.slot_b,
                                            delta_seq=3,
                                            taken_flags=mask_b,
                                            ack_flags=mask_b,
                                            nack_flags=0,
                                            reason=0,
                                            reason_slot=args.slot_b,
                                            frame_seq=4))

    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(delta_a_hex)}",
                lambda r: expect_rx_rejected(r,
                                             frame_type=DELTA_TYPE,
                                             source=args.slot_a,
                                             target=mask_b,
                                             epoch=args.epoch,
                                             run=args.run,
                                             result=6,
                                             frame_result=0))
    duplicate_ack = run_checked(records,
                                "B",
                                execute_b,
                                f"SYSTem:REFMEM:SYNC:ACK? {args.slot_b},{mask_a},5",
                                lambda r: expect_ack_frame(r,
                                                           source=args.slot_b,
                                                           target=mask_a,
                                                           epoch=args.epoch,
                                                           run=args.run,
                                                           seq=5,
                                                           ack_seq=3,
                                                           accepted=0,
                                                           rx_result=6,
                                                           rx_frame_result=0))
    duplicate_ack_hex = fields_dict(ACK_FRAME_FIELDS, duplicate_ack)["hex"]
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(duplicate_ack_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=ACK_NACK_TYPE,
                                    source=args.slot_b,
                                    target=mask_a,
                                    epoch=args.epoch,
                                    run=args.run))
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:ACK:STATus? {args.slot_b}",
                lambda r: expect_ack_status(r,
                                            source=args.slot_b,
                                            delta_seq=3,
                                            taken_flags=mask_b,
                                            ack_flags=0,
                                            nack_flags=mask_b,
                                            reason=6,
                                            reason_slot=args.slot_b,
                                            frame_seq=5))

    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(ack_a_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=ACK_NACK_TYPE,
                                    source=args.slot_a,
                                    target=mask_b,
                                    epoch=args.epoch,
                                    run=args.run))
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:ACK:STATus? {args.slot_a}",
                lambda r: expect_ack_status(r,
                                            source=args.slot_a,
                                            delta_seq=3,
                                            taken_flags=mask_a,
                                            ack_flags=mask_a,
                                            nack_flags=0,
                                            reason=0,
                                            reason_slot=args.slot_a,
                                            frame_seq=4))

    target_bad = run_checked(records,
                             "A",
                             execute_a,
                             f"SYSTem:REFMEM:SYNC:DELTa? {args.slot_a},{mask_a},5,{args.slot_a},2,1,{args.delta_a + 1},1",
                             lambda r: expect_frame(r,
                                                    source=args.slot_a,
                                                    target=mask_a,
                                                    epoch=args.epoch,
                                                    run=args.run))
    target_bad_hex = fields_dict(HELLO_FIELDS, target_bad)["hex"]
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(target_bad_hex)}",
                lambda r: expect_rx_rejected(r,
                                             frame_type=DELTA_TYPE,
                                             source=args.slot_a,
                                             target=mask_a,
                                             epoch=args.epoch,
                                             run=args.run,
                                             result=4,
                                             frame_result=0))
    target_ack = run_checked(records,
                             "B",
                             execute_b,
                             f"SYSTem:REFMEM:SYNC:ACK? {args.slot_b},{mask_a},6",
                             lambda r: expect_ack_frame(r,
                                                        source=args.slot_b,
                                                        target=mask_a,
                                                        epoch=args.epoch,
                                                        run=args.run,
                                                        seq=6,
                                                        ack_seq=5,
                                                        accepted=0,
                                                        rx_result=4,
                                                        rx_frame_result=0))
    target_ack_hex = fields_dict(ACK_FRAME_FIELDS, target_ack)["hex"]
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(target_ack_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=ACK_NACK_TYPE,
                                    source=args.slot_b,
                                    target=mask_a,
                                    epoch=args.epoch,
                                    run=args.run))
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:ACK:STATus? {args.slot_b}",
                lambda r: expect_ack_status(r,
                                            source=args.slot_b,
                                            delta_seq=5,
                                            taken_flags=mask_b,
                                            ack_flags=0,
                                            nack_flags=mask_b,
                                            reason=4,
                                            reason_slot=args.slot_b,
                                            frame_seq=6))

    crc_bad = run_checked(records,
                          "A",
                          execute_a,
                          f"SYSTem:REFMEM:SYNC:DELTa? {args.slot_a},{mask_b},6,{args.slot_a},3,1,{args.delta_a + 2},1",
                          lambda r: expect_frame(r,
                                                 source=args.slot_a,
                                                 target=mask_b,
                                                 epoch=args.epoch,
                                                 run=args.run))
    crc_bad_hex = corrupt_last_payload_byte(fields_dict(HELLO_FIELDS, crc_bad)["hex"])
    run_checked(records,
                "B",
                execute_b,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(crc_bad_hex)}",
                lambda r: expect_rx_rejected(r,
                                             frame_type=DELTA_TYPE,
                                             source=args.slot_a,
                                             target=mask_b,
                                             epoch=args.epoch,
                                             run=args.run,
                                             result=2,
                                             frame_result=9))
    crc_ack = run_checked(records,
                          "B",
                          execute_b,
                          f"SYSTem:REFMEM:SYNC:ACK? {args.slot_b},{mask_a},7",
                          lambda r: expect_ack_frame(r,
                                                     source=args.slot_b,
                                                     target=mask_a,
                                                     epoch=args.epoch,
                                                     run=args.run,
                                                     seq=7,
                                                     ack_seq=6,
                                                     accepted=0,
                                                     rx_result=2,
                                                     rx_frame_result=9))
    crc_ack_hex = fields_dict(ACK_FRAME_FIELDS, crc_ack)["hex"]
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:RX {quote_hex(crc_ack_hex)}",
                lambda r: expect_rx(r,
                                    frame_type=ACK_NACK_TYPE,
                                    source=args.slot_b,
                                    target=mask_a,
                                    epoch=args.epoch,
                                    run=args.run))
    run_checked(records,
                "A",
                execute_a,
                f"SYSTem:REFMEM:SYNC:ACK:STATus? {args.slot_b}",
                lambda r: expect_ack_status(r,
                                            source=args.slot_b,
                                            delta_seq=6,
                                            taken_flags=mask_b,
                                            ack_flags=0,
                                            nack_flags=mask_b,
                                            reason=9,
                                            reason_slot=args.slot_b,
                                            frame_seq=7))
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
        "delta_a": args.delta_a,
        "delta_b": args.delta_b,
        "expected_build": args.expected_build,
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
    passed = all(record.status == "PASS" for record in records) and len(records) == 46
    print(f"summary: passed={passed} records={len(records)} out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
