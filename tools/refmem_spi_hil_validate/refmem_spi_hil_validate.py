#!/usr/bin/env python3
"""Validate two-board RefMem Sync over the physical SPI adapter.

This is the P4.5 bridge-away test: the PC no longer moves hex frames between
boards. It only starts RX on the receiver and starts TX on the sender; the
RefMem frame itself crosses the physical SPI wires.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]

ROLE_MASTER = 1
ROLE_SLAVE = 2
HELLO_TYPE = 1
EPOCH_TYPE = 2
DELTA_TYPE = 3
ACK_NACK_TYPE = 5
FENCE_TYPE = 6
QUALITY_TYPE = 7
RAW_BYTE_COUNT = 32
RAW_SEED = 0xA5


@dataclass
class ExchangeResult:
    name: str
    sender: str
    receiver: str
    tx_command: str
    tx_response: str
    rx_response: str
    passed: bool
    reason: str


@dataclass
class IoProfile:
    input_base: int
    input_count: int
    output_base: int
    output_count: int
    trig_in_pin: int
    rj45_in_pin: int
    trig_out_pin: int
    rj45_out_pin: int


@dataclass(frozen=True)
class SpiPins:
    rx: int
    csn: int
    sck: int
    tx: int


def read_serial_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = bytearray()
        while time.monotonic() < deadline:
            ch = ser.read(1)
            if not ch:
                continue
            raw.extend(ch)
            if ch == b"\n":
                break
        if not raw:
            continue
        line = bytes(raw).decode("utf-8", errors="replace").strip()
        maybe_log = line[1:] if line.startswith('"[') else line
        if not line or maybe_log.startswith("[") or maybe_log.startswith("log:"):
            continue
        if line in {'"OK"', "OK", 'OK"'} or line.startswith('"OK[') or line.startswith("OK["):
            return '"OK"'
        return re.sub(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+.*$', "", line).strip()
    return "<timeout>"


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_serial_line(ser, timeout_s)


def parse_csv(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except Exception:
        return []


def parse_ints(response: str) -> list[int]:
    values: list[int] = []
    for part in parse_csv(response):
        try:
            values.append(int(part.strip().strip('"')))
        except ValueError:
            pass
    return values


def expect_response_frame(response: str, expected_type: int) -> tuple[bool, str]:
    fields = parse_csv(response)
    if not fields or fields[0].strip('"') != "OK":
        return False, f"tx not OK: {response}"
    ints = parse_ints(response)
    if len(ints) < 2:
        return False, f"tx response too short: {response}"
    frame_type = ints[1]
    if frame_type != expected_type:
        return False, f"tx frame type {frame_type} != {expected_type}: {response}"
    return True, "OK"


def expect_tdma_tx(response: str) -> tuple[bool, str]:
    fields = parse_csv(response)
    if not fields or fields[0].strip('"') != "ACCEPTED":
        return False, f"tdma tx not accepted: {response}"
    ints = parse_ints(response)
    if len(ints) < 2 or ints[0] == 0 or ints[1] == 0:
        return False, f"tdma tx response too short: {response}"
    return True, "OK"


def tdma_intent_seq(response: str) -> int:
    values = parse_ints(response)
    return values[0] if values else 0


def wait_tdma_rx_armed(ser: serial.Serial,
                       intent_seq: int,
                       timeout_s: float) -> tuple[bool, str]:
    deadline = time.monotonic() + timeout_s
    last_status = "<not-polled>"
    while time.monotonic() < deadline:
        last_status = query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", timeout_s)
        values = parse_ints(last_status)
        if len(values) >= 6:
            state = values[0]
            armed = values[2]
            active_intent = values[4]
            completed = values[5]
            if active_intent >= intent_seq and armed == 1 and completed < active_intent:
                return True, last_status
            if active_intent >= intent_seq and state == 5 and completed >= active_intent:
                return False, last_status
        time.sleep(0.01)
    return False, last_status


def expect_rx(response: str, expected_type: int, expected_source: int) -> tuple[bool, str]:
    fields = parse_csv(response)
    if not fields or fields[0].strip('"') != "ACCEPTED":
        return False, f"rx not accepted: {response}"
    ints = parse_ints(response)
    if len(ints) < 6:
        return False, f"rx response too short: {response}"
    accepted = ints[0]
    result = ints[1]
    frame_result = ints[2]
    frame_type = ints[3]
    source_slot = ints[4]
    if accepted != 1 or result != 0 or frame_result != 0:
        return False, f"rx result accepted={accepted} result={result} frame={frame_result}"
    if frame_type != expected_type:
        return False, f"rx frame type {frame_type} != {expected_type}"
    if source_slot != expected_source:
        return False, f"rx source {source_slot} != {expected_source}"
    return True, "OK"


def extract_frame_hex(response: str) -> str:
    fields = parse_csv(response)
    if not fields or fields[0].strip('"') != "OK":
        raise RuntimeError(f"frame builder did not return OK: {response}")
    frame_hex = fields[-1].strip().strip('"')
    if not frame_hex or not re.fullmatch(r"[0-9A-Fa-f]+", frame_hex):
        raise RuntimeError(f"frame builder response has no frame hex: {response}")
    return frame_hex


def spi_command_to_frame_builder(command: str) -> str:
    replacements = {
        "SYSTem:REFMEM:SYNC:SPI:HELLo": "SYSTem:REFMEM:SYNC:HELLo?",
        "SYSTem:REFMEM:SYNC:SPI:EPOCh": "SYSTem:REFMEM:SYNC:EPOCh?",
        "SYSTem:REFMEM:SYNC:SPI:DELTa": "SYSTem:REFMEM:SYNC:DELTa?",
        "SYSTem:REFMEM:SYNC:SPI:ACK": "SYSTem:REFMEM:SYNC:ACK?",
        "SYSTem:REFMEM:SYNC:SPI:FENCe": "SYSTem:REFMEM:SYNC:FENCe?",
        "SYSTem:REFMEM:SYNC:SPI:QUALity:FRAMe": "SYSTem:REFMEM:SYNC:QUALity:FRAMe?",
    }
    for old, new in replacements.items():
        if command.startswith(old):
            return new + command[len(old):]
    raise RuntimeError(f"cannot map SPI command to frame builder: {command}")


def raw_checksum(seed: int, count: int) -> int:
    return sum((seed + i) & 0xFF for i in range(count))


def expect_raw_tx(response: str, count: int, seed: int) -> tuple[bool, str]:
    fields = parse_csv(response)
    if not fields or fields[0].strip('"') != "OK":
        return False, f"raw tx not OK: {response}"
    ints = parse_ints(response)
    expected = [count, raw_checksum(seed, count), seed & 0xFF, (seed + count - 1) & 0xFF]
    if ints[:4] != expected:
        return False, f"raw tx fields {ints[:4]} != {expected}: {response}"
    return True, "OK"


def expect_raw_rx(response: str, count: int, seed: int) -> tuple[bool, str]:
    fields = parse_csv(response)
    if not fields or fields[0].strip('"') != "RAW":
        return False, f"raw rx not RAW: {response}"
    ints = parse_ints(response)
    expected = [count, raw_checksum(seed, count), seed & 0xFF, (seed + count - 1) & 0xFF, 0]
    if ints[:5] != expected:
        return False, f"raw rx fields {ints[:5]} != {expected}: {response}"
    return True, "OK"


def parse_line_map(text: str) -> list[int]:
    values = [int(part.strip(), 0) for part in text.split(",")]
    if len(values) != 4 or sorted(values) != [0, 1, 2, 3]:
        raise SystemExit(f"line remap must be a permutation of 0..3: {text!r}")
    return values


def explicit_line_map(text: str) -> list[int] | None:
    if text.strip().lower() in {"auto", "detect"}:
        return None
    return parse_line_map(text)


def output_index_for_target_input(remap: list[int], target_input_index: int) -> int:
    return remap.index(target_input_index)


def read_io_profile(ser: serial.Serial, timeout_s: float) -> IoProfile:
    values = parse_ints(query(ser, "REALtime:IO:PROFile?", timeout_s))
    if len(values) < 8:
        raise RuntimeError(f"REALtime:IO:PROFile? returned malformed response: {values}")
    return IoProfile(*values[:8])


def master_pins(sender_profile: IoProfile, sender_to_receiver: list[int]) -> SpiPins:
    return SpiPins(
        rx=sender_profile.input_base,
        csn=sender_profile.output_base + output_index_for_target_input(sender_to_receiver, 1),
        sck=sender_profile.output_base + output_index_for_target_input(sender_to_receiver, 2),
        tx=sender_profile.output_base + output_index_for_target_input(sender_to_receiver, 0),
    )


def slave_pins(receiver_profile: IoProfile, receiver_to_sender: list[int]) -> SpiPins:
    return SpiPins(
        rx=receiver_profile.input_base,
        csn=receiver_profile.input_base + 1,
        sck=receiver_profile.input_base + 2,
        tx=receiver_profile.output_base + output_index_for_target_input(receiver_to_sender, 0),
    )


def arm_spi(ser: serial.Serial, role: int, baud: int, pins: SpiPins, timeout_s: float) -> str:
    return query(
        ser,
        f"SYSTem:REFMEM:SYNC:SPI:ARM {role},{baud},{pins.rx},{pins.csn},{pins.sck},{pins.tx}",
        timeout_s,
    )


def run_io_preflight(args: argparse.Namespace) -> dict[str, object]:
    preflight_dir = args.out_dir / "io_preflight"
    command = [
        sys.executable,
        str(ROOT / "tools" / "two_board_io_validate" / "two_board_io_validate.py"),
        "--port-a", args.port_a,
        "--port-b", args.port_b,
        "--name-a", "A",
        "--name-b", "B",
        "--expect-a-to-b", args.line_remap_a_to_b,
        "--expect-b-to-a", args.line_remap_b_to_a,
        "--out-dir", str(preflight_dir),
        "--timeout", str(args.timeout_s),
    ]
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    summary_path = preflight_dir / "summary.json"
    summary: dict[str, object] = {}
    if summary_path.exists():
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    return {
        "command": command,
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "summary_path": str(summary_path),
        "summary": summary,
    }


def read_observed_map(io_preflight: dict[str, object],
                      source: str,
                      target: str) -> list[int]:
    summary = io_preflight.get("summary")
    if not isinstance(summary, dict):
        raise RuntimeError("IO preflight summary is missing")
    observed_maps = summary.get("observed_maps")
    if not isinstance(observed_maps, dict):
        raise RuntimeError("IO preflight observed_maps is missing")
    key = f"{source}_to_{target}"
    values = observed_maps.get(key)
    if not isinstance(values, list):
        raise RuntimeError(f"IO preflight observed map {key} is missing")
    text = ",".join(str(value) for value in values)
    return parse_line_map(text)


def exchange(name: str,
             sender_name: str,
             sender: serial.Serial,
             sender_pins: SpiPins,
             receiver_name: str,
             receiver: serial.Serial,
             receiver_pins: SpiPins,
             tx_command: str,
             expected_type: int,
             expected_source: int,
             baud: int,
             rx_timeout_ms: int,
             timeout_s: float) -> ExchangeResult:
    arm_rx = arm_spi(receiver, ROLE_SLAVE, baud, receiver_pins, timeout_s)
    arm_tx = arm_spi(sender, ROLE_MASTER, baud, sender_pins, timeout_s)
    if not arm_rx.startswith('"OK"') or not arm_tx.startswith('"OK"'):
        return ExchangeResult(name, sender_name, receiver_name, tx_command,
                              arm_tx, arm_rx, False, "SPI arm failed")

    rx_holder: dict[str, str] = {}

    def rx_worker() -> None:
        rx_holder["response"] = query(receiver,
                                      f"SYSTem:REFMEM:SYNC:SPI:RX? {rx_timeout_ms}",
                                      timeout_s + (rx_timeout_ms / 1000.0) + 2.0)

    thread = threading.Thread(target=rx_worker, daemon=True)
    thread.start()
    time.sleep(0.15)
    tx_response = query(sender, tx_command, timeout_s)
    thread.join(timeout_s + (rx_timeout_ms / 1000.0) + 3.0)
    rx_response = rx_holder.get("response", "<rx-thread-timeout>")

    tx_ok, tx_reason = expect_response_frame(tx_response, expected_type)
    rx_ok, rx_reason = expect_rx(rx_response, expected_type, expected_source)
    passed = tx_ok and rx_ok
    reason = "OK" if passed else f"{tx_reason}; {rx_reason}"
    return ExchangeResult(name, sender_name, receiver_name, tx_command,
                          tx_response, rx_response, passed, reason)


def tdma_exchange(name: str,
                  sender_name: str,
                  sender: serial.Serial,
                  sender_pins: SpiPins,
                  receiver_name: str,
                  receiver: serial.Serial,
                  receiver_pins: SpiPins,
                  tx_command: str,
                  expected_type: int,
                  expected_source: int,
                  baud: int,
                  rx_timeout_ms: int,
                  timeout_s: float) -> ExchangeResult:
    query(sender, "SYSTem:REFMEM:SYNC:TDMA:ABORt", timeout_s)
    query(receiver, "SYSTem:REFMEM:SYNC:TDMA:ABORt", timeout_s)

    frame_builder = spi_command_to_frame_builder(tx_command)
    frame_response = query(sender, frame_builder, timeout_s)
    try:
        frame_hex = extract_frame_hex(frame_response)
    except RuntimeError as exc:
        return ExchangeResult(name, sender_name, receiver_name, frame_builder,
                              frame_response, "<not-started>", False, str(exc))

    rx_deadline_us = rx_timeout_ms * 1000
    rx_command = (
        "SYSTem:REFMEM:SYNC:TDMA:RX "
        f"{rx_deadline_us},{baud},{receiver_pins.rx},{receiver_pins.csn},"
        f"{receiver_pins.sck},{receiver_pins.tx}"
    )
    rx_start = query(receiver, rx_command, timeout_s)
    if not rx_start.startswith('"ACCEPTED"'):
        return ExchangeResult(name, sender_name, receiver_name, rx_command,
                              frame_response, rx_start, False, "TDMA RX start failed")

    rx_intent_seq = tdma_intent_seq(rx_start)
    rx_armed, rx_status = wait_tdma_rx_armed(receiver, rx_intent_seq, timeout_s)
    if not rx_armed:
        return ExchangeResult(name, sender_name, receiver_name, rx_command,
                              frame_response, rx_status, False, "TDMA RX arm gate failed")

    tdma_tx_command = (
        "SYSTem:REFMEM:SYNC:TDMA:TX "
        f"\"{frame_hex}\",{baud},{rx_deadline_us},{sender_pins.rx},{sender_pins.csn},"
        f"{sender_pins.sck},{sender_pins.tx}"
    )
    tx_response = query(sender, tdma_tx_command, timeout_s)

    deadline = time.monotonic() + timeout_s + (rx_timeout_ms / 1000.0) + 2.0
    rx_response = "<tdma-frame-timeout>"
    while time.monotonic() < deadline:
        candidate = query(receiver, "SYSTem:REFMEM:SYNC:TDMA:FRAMe?", timeout_s)
        if candidate.startswith('"ACCEPTED"') or candidate.startswith('"REJECTED"'):
            rx_response = candidate
            break
        time.sleep(0.05)

    tx_ok, tx_reason = expect_tdma_tx(tx_response)
    rx_ok, rx_reason = expect_rx(rx_response, expected_type, expected_source)
    passed = tx_ok and rx_ok
    reason = "OK" if passed else f"{tx_reason}; {rx_reason}"
    return ExchangeResult(name, sender_name, receiver_name, tdma_tx_command,
                          tx_response, rx_response, passed, reason)


def raw_exchange(name: str,
                 sender_name: str,
                 sender: serial.Serial,
                 sender_pins: SpiPins,
                 receiver_name: str,
                 receiver: serial.Serial,
                 receiver_pins: SpiPins,
                 baud: int,
                 rx_timeout_ms: int,
                 timeout_s: float) -> ExchangeResult:
    tx_command = f"SYSTem:REFMEM:SYNC:SPI:RAW:TX {RAW_BYTE_COUNT},{RAW_SEED}"
    rx_command = f"SYSTem:REFMEM:SYNC:SPI:RAW:RX? {RAW_BYTE_COUNT},{RAW_SEED},{rx_timeout_ms}"
    arm_rx = arm_spi(receiver, ROLE_SLAVE, baud, receiver_pins, timeout_s)
    arm_tx = arm_spi(sender, ROLE_MASTER, baud, sender_pins, timeout_s)
    if not arm_rx.startswith('"OK"') or not arm_tx.startswith('"OK"'):
        return ExchangeResult(name, sender_name, receiver_name, tx_command,
                              arm_tx, arm_rx, False, "SPI raw arm failed")

    rx_holder: dict[str, str] = {}

    def rx_worker() -> None:
        rx_holder["response"] = query(receiver,
                                      rx_command,
                                      timeout_s + (rx_timeout_ms / 1000.0) + 2.0)

    thread = threading.Thread(target=rx_worker, daemon=True)
    thread.start()
    time.sleep(0.15)
    tx_response = query(sender, tx_command, timeout_s)
    thread.join(timeout_s + (rx_timeout_ms / 1000.0) + 3.0)
    rx_response = rx_holder.get("response", "<rx-thread-timeout>")

    tx_ok, tx_reason = expect_raw_tx(tx_response, RAW_BYTE_COUNT, RAW_SEED)
    rx_ok, rx_reason = expect_raw_rx(rx_response, RAW_BYTE_COUNT, RAW_SEED)
    passed = tx_ok and rx_ok
    reason = "OK" if passed else f"{tx_reason}; {rx_reason}"
    return ExchangeResult(name, sender_name, receiver_name, tx_command,
                          tx_response, rx_response, passed, reason)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", default="COM5")
    parser.add_argument("--port-b", default="COM6")
    parser.add_argument("--baud", type=int, default=25000000)
    parser.add_argument("--transport", choices=["spi", "tdma"], default="spi")
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--timeout-s", type=float, default=2.0)
    parser.add_argument("--rx-timeout-ms", type=int, default=3000)
    parser.add_argument("--line-remap-a-to-b", default="auto")
    parser.add_argument("--line-remap-b-to-a", default="auto")
    parser.add_argument("--skip-io-preflight", action="store_true")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build-rtos-multicore-smoke" / "refmem_spi_hil")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    results: list[ExchangeResult] = []
    failures: list[str] = []
    io_preflight: dict[str, object] | None = None
    if not args.skip_io_preflight:
        io_preflight = run_io_preflight(args)
        if io_preflight["returncode"] != 0:
            failures.append("IO preflight failed")

    remap_a_to_b = explicit_line_map(args.line_remap_a_to_b)
    remap_b_to_a = explicit_line_map(args.line_remap_b_to_a)
    if remap_a_to_b is None or remap_b_to_a is None:
        if io_preflight is None or io_preflight["returncode"] != 0:
            failures.append("IO preflight is required for auto line remap")
            remap_a_to_b = remap_a_to_b or [0, 1, 2, 3]
            remap_b_to_a = remap_b_to_a or [0, 1, 2, 3]
        else:
            if remap_a_to_b is None:
                remap_a_to_b = read_observed_map(io_preflight, "A", "B")
            if remap_b_to_a is None:
                remap_b_to_a = read_observed_map(io_preflight, "B", "A")

    with serial.Serial(args.port_a, args.serial_baud, timeout=0.1, write_timeout=args.timeout_s) as ser_a, \
            serial.Serial(args.port_b, args.serial_baud, timeout=0.1, write_timeout=args.timeout_s) as ser_b:
        time.sleep(0.5)
        for ser in (ser_a, ser_b):
            ser.reset_input_buffer()
            ser.reset_output_buffer()

        build_a = query(ser_a, "SYST:FW:BUILD?", args.timeout_s)
        build_b = query(ser_b, "SYST:FW:BUILD?", args.timeout_s)
        init_a = query(ser_a, "SYSTem:REFMEM:SYNC:INITialize 0,1,1", args.timeout_s)
        init_b = query(ser_b, "SYSTem:REFMEM:SYNC:INITialize 1,1,1", args.timeout_s)
        if not init_a.startswith('"OK"'):
            failures.append(f"A init failed: {init_a}")
        if not init_b.startswith('"OK"'):
            failures.append(f"B init failed: {init_b}")

        profile_a = read_io_profile(ser_a, args.timeout_s)
        profile_b = read_io_profile(ser_b, args.timeout_s)
        a_master_to_b = master_pins(profile_a, remap_a_to_b)
        b_slave_from_a = slave_pins(profile_b, remap_b_to_a)
        b_master_to_a = master_pins(profile_b, remap_b_to_a)
        a_slave_from_b = slave_pins(profile_a, remap_a_to_b)

        raw_plan = [
            ("A_RAW_B", "A", ser_a, a_master_to_b, "B", ser_b, b_slave_from_a),
            ("B_RAW_A", "B", ser_b, b_master_to_a, "A", ser_a, a_slave_from_b),
        ]
        if not failures:
            for item in raw_plan:
                result = raw_exchange(*item,
                                      baud=args.baud,
                                      rx_timeout_ms=args.rx_timeout_ms,
                                      timeout_s=args.timeout_s)
                results.append(result)
                if not result.passed:
                    failures.append(f"{result.name}: {result.reason}")
                    break

        plan = [
            ("A_HELLO_B", "A", ser_a, a_master_to_b, "B", ser_b, b_slave_from_a,
             "SYSTem:REFMEM:SYNC:SPI:HELLo 0,2,0", HELLO_TYPE, 0),
            ("B_HELLO_A", "B", ser_b, b_master_to_a, "A", ser_a, a_slave_from_b,
             "SYSTem:REFMEM:SYNC:SPI:HELLo 1,1,0", HELLO_TYPE, 1),
            ("A_EPOCH_B", "A", ser_a, a_master_to_b, "B", ser_b, b_slave_from_a,
             "SYSTem:REFMEM:SYNC:SPI:EPOCh 0,2,0", EPOCH_TYPE, 0),
            ("B_EPOCH_A", "B", ser_b, b_master_to_a, "A", ser_a, a_slave_from_b,
             "SYSTem:REFMEM:SYNC:SPI:EPOCh 1,1,0", EPOCH_TYPE, 1),
            ("A_DELTA_B", "A", ser_a, a_master_to_b, "B", ser_b, b_slave_from_a,
             "SYSTem:REFMEM:SYNC:SPI:DELTa 0,2,0,0,3,1,2768240641,1", DELTA_TYPE, 0),
            ("B_ACK_A", "B", ser_b, b_master_to_a, "A", ser_a, a_slave_from_b,
             "SYSTem:REFMEM:SYNC:SPI:ACK 1,1,0", ACK_NACK_TYPE, 1),
            ("B_DELTA_A", "B", ser_b, b_master_to_a, "A", ser_a, a_slave_from_b,
             "SYSTem:REFMEM:SYNC:SPI:DELTa 1,1,0,1,3,1,3053453314,1", DELTA_TYPE, 1),
            ("A_ACK_B", "A", ser_a, a_master_to_b, "B", ser_b, b_slave_from_a,
             "SYSTem:REFMEM:SYNC:SPI:ACK 0,2,0", ACK_NACK_TYPE, 0),
            ("A_FENCE_B", "A", ser_a, a_master_to_b, "B", ser_b, b_slave_from_a,
             "SYSTem:REFMEM:SYNC:SPI:FENCe 0,2,0,1,1,2,3,1000", FENCE_TYPE, 0),
            ("B_FENCE_A", "B", ser_b, b_master_to_a, "A", ser_a, a_slave_from_b,
             "SYSTem:REFMEM:SYNC:SPI:FENCe 1,1,0,1,1,1,3,1000", FENCE_TYPE, 1),
            ("A_QUALITY_B", "A", ser_a, a_master_to_b, "B", ser_b, b_slave_from_a,
             "SYSTem:REFMEM:SYNC:SPI:QUALity:FRAMe 0,2,0,1,1,1", QUALITY_TYPE, 0),
            ("B_QUALITY_A", "B", ser_b, b_master_to_a, "A", ser_a, a_slave_from_b,
             "SYSTem:REFMEM:SYNC:SPI:QUALity:FRAMe 1,1,0,1,1,0", QUALITY_TYPE, 1),
        ]

        if not failures:
            for item in plan:
                exchange_fn = tdma_exchange if args.transport == "tdma" else exchange
                result = exchange_fn(*item,
                                     baud=args.baud,
                                     rx_timeout_ms=args.rx_timeout_ms,
                                     timeout_s=args.timeout_s)
                results.append(result)
                if not result.passed:
                    failures.append(f"{result.name}: {result.reason}")
                    break

        mirror_a = query(ser_a, "SYSTem:REFMEM:SYNC:MIRRor? 1", args.timeout_s)
        mirror_b = query(ser_b, "SYSTem:REFMEM:SYNC:MIRRor? 0", args.timeout_s)
        ack_a = query(ser_a, "SYSTem:REFMEM:SYNC:ACK:STATus? 1", args.timeout_s)
        ack_b = query(ser_b, "SYSTem:REFMEM:SYNC:ACK:STATus? 0", args.timeout_s)
        fence_a = query(ser_a, "SYSTem:REFMEM:SYNC:FENCe:STATus? 1", args.timeout_s)
        fence_b = query(ser_b, "SYSTem:REFMEM:SYNC:FENCe:STATus? 0", args.timeout_s)
        spi_a = query(ser_a, "SYSTem:REFMEM:SYNC:SPI:STATus?", args.timeout_s)
        spi_b = query(ser_b, "SYSTem:REFMEM:SYNC:SPI:STATus?", args.timeout_s)
        tdma_a = query(ser_a, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args.timeout_s)
        tdma_b = query(ser_b, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args.timeout_s)
        profiles = {"A": asdict(profile_a), "B": asdict(profile_b)}
        spi_pin_plan = {
            "a_master_to_b": asdict(a_master_to_b),
            "b_slave_from_a": asdict(b_slave_from_a),
            "b_master_to_a": asdict(b_master_to_a),
            "a_slave_from_b": asdict(a_slave_from_b),
        }

    report = {
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "port_a": args.port_a,
        "port_b": args.port_b,
        "transport": args.transport,
        "build_a": build_a,
        "build_b": build_b,
        "init_a": init_a,
        "init_b": init_b,
        "wiring": "active OUT group to peer IN group, remap derived by two_board_io_validate",
        "line_remap_a_to_b": remap_a_to_b,
        "line_remap_b_to_a": remap_b_to_a,
        "io_preflight": io_preflight,
        "profiles": profiles,
        "spi_pin_plan": spi_pin_plan,
        "exchanges": [asdict(r) for r in results],
        "mirror_a_source_1": mirror_a,
        "mirror_b_source_0": mirror_b,
        "ack_a_source_1": ack_a,
        "ack_b_source_0": ack_b,
        "fence_a_source_1": fence_a,
        "fence_b_source_0": fence_b,
        "spi_a": spi_a,
        "spi_b": spi_b,
        "tdma_a": tdma_a,
        "tdma_b": tdma_b,
        "failures": failures,
        "passed": not failures,
    }
    (args.out_dir / "refmem_spi_hil_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    (args.out_dir / "summary.txt").write_text(
        "PASS\n" if not failures else "FAIL\n" + "\n".join(failures) + "\n",
        encoding="utf-8",
    )

    print("PASS" if not failures else "FAIL")
    print(args.out_dir / "refmem_spi_hil_report.json")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
