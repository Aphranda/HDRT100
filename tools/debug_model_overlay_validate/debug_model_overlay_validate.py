#!/usr/bin/env python3
"""Validate the GPIO4..7 minimum-system distributed model overlay.

The tool controls only REALtime:IO:MODel:* maintenance commands. It opens two
serial ports, releases both boards, then drives one owner line at a time:

- X GPIO4 -> Y GPIO4: TURN_POS_PULSE
- X GPIO5 -> Y GPIO5: VNA_READY
- Y GPIO6 -> X GPIO6: VNA_TRIG
- X GPIO7 -> Y GPIO7: LINK_SWITCH
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from contextlib import ExitStack, contextmanager
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterator

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]


@dataclass
class ModelProfile:
    base_pin: int
    pin_count: int
    uart_conflict_mask: int
    uart_enabled: int


@dataclass
class ModelStep:
    signal: str
    source: str
    target: str
    gpio: int
    bit_index: int
    expected_mask: int
    observed_mask: int
    source_enable_mask: int
    source_value_mask: int
    passed: bool


def read_serial_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = bytearray()
        while time.monotonic() < deadline:
            ch = ser.read(1)
            if not ch:
                continue
            raw.extend(ch)
            if ch in (b"\n", b"\r"):
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


def parse_csv_ints(response: str) -> list[int]:
    try:
        fields = next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []
    values: list[int] = []
    for field in fields:
        text = field.strip().strip('"')
        if text.upper() == "TRUE":
            values.append(1)
            continue
        if text.upper() == "FALSE":
            values.append(0)
            continue
        try:
            values.append(int(text, 0))
        except ValueError:
            pass
    return values


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_serial_line(ser, timeout_s)


def write_command(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    deadline = time.monotonic() + min(timeout_s, 0.5)
    response = "<sent>"
    while time.monotonic() < deadline:
        if ser.in_waiting <= 0:
            time.sleep(0.005)
            continue
        line = read_serial_line(ser, 0.1)
        if line != "<timeout>":
            response = line
            break
    return response


@contextmanager
def open_board(port: str, baud: int, timeout_s: float, settle_s: float) -> Iterator[serial.Serial]:
    ser = serial.Serial(port, baud, timeout=0.1, write_timeout=timeout_s)
    try:
        time.sleep(settle_s)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        yield ser
    finally:
        try:
            ser.flush()
        finally:
            ser.close()


def read_profile(ser: serial.Serial, timeout_s: float) -> ModelProfile:
    values = parse_csv_ints(query(ser, "REALtime:IO:MODel:PROFile?", timeout_s))
    if len(values) < 4:
        raise RuntimeError(f"REALtime:IO:MODel:PROFile? returned malformed response: {values}")
    return ModelProfile(*values[:4])


def read_input_mask(ser: serial.Serial, timeout_s: float) -> tuple[int, int, int]:
    values = parse_csv_ints(query(ser, "REALtime:IO:MODel:INPut:LEVel?", timeout_s))
    if len(values) < 3:
        raise RuntimeError(f"REALtime:IO:MODel:INPut:LEVel? returned malformed response: {values}")
    return values[0], values[1], values[2]


def read_output_mask(ser: serial.Serial, timeout_s: float) -> tuple[int, int, int, int]:
    values = parse_csv_ints(query(ser, "REALtime:IO:MODel:OUTPut:MASK?", timeout_s))
    if len(values) < 4:
        raise RuntimeError(f"REALtime:IO:MODel:OUTPut:MASK? returned malformed response: {values}")
    return values[0], values[1], values[2], values[3]


def release_model(ser: serial.Serial, timeout_s: float) -> None:
    write_command(ser, "REALtime:IO:MODel:OUTPut:MASK 0,0", timeout_s)
    response = write_command(ser, "REALtime:IO:MODel:OUTPut:RELease", timeout_s)
    if response not in ('"OK"', "<sent>"):
        raise RuntimeError(f"MODel:OUTPut:RELease returned {response!r}")


def drive_model(ser: serial.Serial, enable_mask: int, value_mask: int, timeout_s: float) -> None:
    response = write_command(
        ser,
        f"REALtime:IO:MODel:OUTPut:MASK {enable_mask},{value_mask}",
        timeout_s,
    )
    if response not in ('"OK"', "<sent>"):
        raise RuntimeError(
            f"MODel:OUTPut:MASK {enable_mask},{value_mask} returned {response!r}"
        )


def validate_profiles(profile_x: ModelProfile, profile_y: ModelProfile) -> None:
    for name, profile in (("X", profile_x), ("Y", profile_y)):
        if profile.base_pin != 4 or profile.pin_count != 4:
            raise RuntimeError(f"{name} model profile must be GPIO4..7, got {profile}")
        if profile.uart_enabled != 0:
            raise RuntimeError(f"{name} UART stdio is enabled; GPIO4/5 overlay is unsafe")


def check_step(signal: str,
               source_name: str,
               source: serial.Serial,
               target_name: str,
               target: serial.Serial,
               gpio: int,
               timeout_s: float,
               settle_s: float) -> ModelStep:
    bit_index = gpio - 4
    expected_mask = 1 << bit_index
    drive_model(source, expected_mask, expected_mask, timeout_s)
    time.sleep(settle_s)
    _, _, observed_mask = read_input_mask(target, timeout_s)
    observed_mask &= 0x0F
    _, _, source_enable_mask, source_value_mask = read_output_mask(source, timeout_s)
    drive_model(source, 0, 0, timeout_s)
    time.sleep(settle_s)
    return ModelStep(
        signal=signal,
        source=source_name,
        target=target_name,
        gpio=gpio,
        bit_index=bit_index,
        expected_mask=expected_mask,
        observed_mask=observed_mask,
        source_enable_mask=source_enable_mask,
        source_value_mask=source_value_mask,
        passed=observed_mask == expected_mask,
    )


def format_step(step: ModelStep) -> str:
    status = "PASS" if step.passed else "FAIL"
    return (
        f"{status} {step.signal}: {step.source}.GPIO{step.gpio} -> "
        f"{step.target}.GPIO{step.gpio} expected=0x{step.expected_mask:X} "
        f"observed=0x{step.observed_mask:X} "
        f"source_enable=0x{step.source_enable_mask:X} source_value=0x{step.source_value_mask:X}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-x", required=True, help="X board serial port, for example COM3")
    parser.add_argument("--port-y", required=True, help="Y board serial port, for example COM4")
    parser.add_argument("--name-x", default="X")
    parser.add_argument("--name-y", default="Y")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--line-settle", type=float, default=0.05)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    if args.port_x == args.port_y:
        raise SystemExit("--port-x and --port-y must be different serial ports")

    started = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or ROOT / "build-rtos-multicore-smoke" / f"debug_model_overlay_{started}"
    out_dir.mkdir(parents=True, exist_ok=True)

    profile_x: ModelProfile | None = None
    profile_y: ModelProfile | None = None
    steps: list[ModelStep] = []
    passed = False

    with ExitStack() as stack:
        ser_x = stack.enter_context(open_board(args.port_x, args.baud, args.timeout, args.settle))
        ser_y = stack.enter_context(open_board(args.port_y, args.baud, args.timeout, args.settle))

        try:
            profile_x = read_profile(ser_x, args.timeout)
            profile_y = read_profile(ser_y, args.timeout)
            validate_profiles(profile_x, profile_y)

            release_model(ser_x, args.timeout)
            release_model(ser_y, args.timeout)

            steps.append(check_step("TURN_POS_PULSE",
                                    args.name_x,
                                    ser_x,
                                    args.name_y,
                                    ser_y,
                                    4,
                                    args.timeout,
                                    args.line_settle))
            steps.append(check_step("VNA_READY",
                                    args.name_x,
                                    ser_x,
                                    args.name_y,
                                    ser_y,
                                    5,
                                    args.timeout,
                                    args.line_settle))
            steps.append(check_step("VNA_TRIG",
                                    args.name_y,
                                    ser_y,
                                    args.name_x,
                                    ser_x,
                                    6,
                                    args.timeout,
                                    args.line_settle))
            steps.append(check_step("LINK_SWITCH",
                                    args.name_x,
                                    ser_x,
                                    args.name_y,
                                    ser_y,
                                    7,
                                    args.timeout,
                                    args.line_settle))
            passed = all(step.passed for step in steps)
        finally:
            try:
                release_model(ser_x, args.timeout)
            finally:
                release_model(ser_y, args.timeout)

    summary = {
        "started": started,
        "passed": passed,
        "ports": {args.name_x: args.port_x, args.name_y: args.port_y},
        "profiles": {
            args.name_x: asdict(profile_x) if profile_x else None,
            args.name_y: asdict(profile_y) if profile_y else None,
        },
        "steps": [asdict(step) for step in steps],
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    lines = [f"Debug model overlay validation: {'PASS' if passed else 'FAIL'}"]
    if profile_x and profile_y:
        lines.append(f"{args.name_x}: GPIO{profile_x.base_pin}..{profile_x.base_pin + profile_x.pin_count - 1}, uart_enabled={profile_x.uart_enabled}")
        lines.append(f"{args.name_y}: GPIO{profile_y.base_pin}..{profile_y.base_pin + profile_y.pin_count - 1}, uart_enabled={profile_y.uart_enabled}")
    lines.extend(format_step(step) for step in steps)
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    for line in lines:
        print(line)
    print(f"summary={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
