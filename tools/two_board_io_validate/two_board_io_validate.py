#!/usr/bin/env python3
"""Check two-board SYNC_IO wiring order with static output masks.

The tool opens both SCPI serial ports, queries each active IO profile, drives
one output bit high at a time, and reads the opposite board input mask. It is
intended for the minimum-system two-board bring-up where the boards are wired
output-group to input-group.
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
class IoProfile:
    input_base: int
    input_count: int
    output_base: int
    output_count: int
    trig_in_pin: int
    rj45_in_pin: int
    trig_out_pin: int
    rj45_out_pin: int


@dataclass
class WireStep:
    source: str
    target: str
    output_index: int
    output_pin: int
    observed_mask: int
    observed_inputs: list[int]
    expected_mask: int
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
        try:
            values.append(int(field.strip().strip('"'), 0))
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


def read_profile(ser: serial.Serial, timeout_s: float) -> IoProfile:
    values = parse_csv_ints(query(ser, "REALtime:IO:PROFile?", timeout_s))
    if len(values) < 8:
        raise RuntimeError(f"REALtime:IO:PROFile? returned malformed response: {values}")
    return IoProfile(*values[:8])


def read_input_mask(ser: serial.Serial, timeout_s: float) -> tuple[int, int, int]:
    values = parse_csv_ints(query(ser, "REALtime:IO:INPut:LEVel?", timeout_s))
    if len(values) < 3:
        raise RuntimeError(f"REALtime:IO:INPut:LEVel? returned malformed response: {values}")
    return values[0], values[1], values[2]


def bit_indices(mask: int, count: int) -> list[int]:
    return [index for index in range(count) if (mask & (1 << index)) != 0]


def release_outputs(ser: serial.Serial, timeout_s: float) -> None:
    write_command(ser, "REALtime:IO:OUTPut:MASK 0", timeout_s)
    write_command(ser, "REALtime:IO:OUTPut:RELease", timeout_s)


def drive_mask(ser: serial.Serial, mask: int, timeout_s: float) -> None:
    response = write_command(ser, f"REALtime:IO:OUTPut:MASK {mask}", timeout_s)
    if response not in ('"OK"', "<sent>"):
        raise RuntimeError(f"OUTPut:MASK {mask} returned {response!r}")


def check_direction(source_name: str,
                    source: serial.Serial,
                    source_profile: IoProfile,
                    target_name: str,
                    target: serial.Serial,
                    target_profile: IoProfile,
                    timeout_s: float,
                    settle_s: float) -> list[WireStep]:
    steps: list[WireStep] = []
    count = min(source_profile.output_count, target_profile.input_count)
    drive_mask(source, 0, timeout_s)
    time.sleep(settle_s)

    for output_index in range(count):
        expected_mask = 1 << output_index
        drive_mask(source, expected_mask, timeout_s)
        time.sleep(settle_s)
        _, _, observed_mask = read_input_mask(target, timeout_s)
        observed_mask &= (1 << target_profile.input_count) - 1
        steps.append(WireStep(
            source=source_name,
            target=target_name,
            output_index=output_index,
            output_pin=source_profile.output_base + output_index,
            observed_mask=observed_mask,
            observed_inputs=bit_indices(observed_mask, target_profile.input_count),
            expected_mask=expected_mask,
            passed=observed_mask == expected_mask,
        ))
        drive_mask(source, 0, timeout_s)
        time.sleep(settle_s)

    return steps


def format_step(step: WireStep, target_profile: IoProfile) -> str:
    if step.observed_inputs:
        targets = ",".join(
            f"IN{index}/GPIO{target_profile.input_base + index}"
            for index in step.observed_inputs
        )
    else:
        targets = "none"
    expected_pin = target_profile.input_base + step.output_index
    status = "PASS" if step.passed else "FAIL"
    return (
        f"{status} {step.source}.OUT{step.output_index}/GPIO{step.output_pin} -> "
        f"{step.target}.{targets} expected IN{step.output_index}/GPIO{expected_pin} "
        f"mask=0x{step.observed_mask:X}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-a", required=True, help="first board serial port, for example COM6")
    parser.add_argument("--port-b", required=True, help="second board serial port, for example COM7")
    parser.add_argument("--name-a", default="B0")
    parser.add_argument("--name-b", default="B1")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0, help="seconds after opening each port")
    parser.add_argument("--line-settle", type=float, default=0.05, help="seconds after changing output mask")
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    if args.port_a == args.port_b:
        raise SystemExit("--port-a and --port-b must be different serial ports")

    started = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or ROOT / "build-rtos-multicore-smoke" / f"two_board_io_{started}"
    out_dir.mkdir(parents=True, exist_ok=True)

    with ExitStack() as stack:
        ser_a = stack.enter_context(open_board(args.port_a, args.baud, args.timeout, args.settle))
        ser_b = stack.enter_context(open_board(args.port_b, args.baud, args.timeout, args.settle))

        profile_a = read_profile(ser_a, args.timeout)
        profile_b = read_profile(ser_b, args.timeout)

        release_outputs(ser_a, args.timeout)
        release_outputs(ser_b, args.timeout)

        steps_ab = check_direction(args.name_a,
                                   ser_a,
                                   profile_a,
                                   args.name_b,
                                   ser_b,
                                   profile_b,
                                   args.timeout,
                                   args.line_settle)
        steps_ba = check_direction(args.name_b,
                                   ser_b,
                                   profile_b,
                                   args.name_a,
                                   ser_a,
                                   profile_a,
                                   args.timeout,
                                   args.line_settle)

        release_outputs(ser_a, args.timeout)
        release_outputs(ser_b, args.timeout)

    steps = steps_ab + steps_ba
    passed = all(step.passed for step in steps)
    summary = {
        "started": started,
        "passed": passed,
        "ports": {args.name_a: args.port_a, args.name_b: args.port_b},
        "profiles": {args.name_a: asdict(profile_a), args.name_b: asdict(profile_b)},
        "steps": [asdict(step) for step in steps],
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    lines = [
        f"Two-board IO wiring validation: {'PASS' if passed else 'FAIL'}",
        f"{args.name_a}: IN GPIO{profile_a.input_base}..{profile_a.input_base + profile_a.input_count - 1}, "
        f"OUT GPIO{profile_a.output_base}..{profile_a.output_base + profile_a.output_count - 1}",
        f"{args.name_b}: IN GPIO{profile_b.input_base}..{profile_b.input_base + profile_b.input_count - 1}, "
        f"OUT GPIO{profile_b.output_base}..{profile_b.output_base + profile_b.output_count - 1}",
    ]
    lines.extend(format_step(step, profile_b if step.target == args.name_b else profile_a) for step in steps)
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    for line in lines:
        print(line)
    print(f"summary={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
