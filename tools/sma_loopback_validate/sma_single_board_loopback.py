#!/usr/bin/env python3
"""Validate product SMA OUT1..4 to IN1..4 loopback on one board.

Before running, connect OUT1->IN1, OUT2->IN2, OUT3->IN3 and OUT4->IN4.
The firmware reverses the physical GPIO20..23 input order at its boundary, so
the SCPI-visible input mask must equal the driven output mask.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import (  # noqa: E402
    open_serial_port,
    read_scpi_response,
)


EXPECTED_INPUT_BASE = 20
EXPECTED_INPUT_COUNT = 4
EXPECTED_OUTPUT_BASE = 16
EXPECTED_OUTPUT_COUNT = 4
TEST_MASKS = (0x0, 0x1, 0x2, 0x4, 0x8, 0xF, 0x0)


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
class LoopbackStep:
    driven_mask: int
    reported_output_mask: int
    observed_input_mask: int
    passed: bool


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
            return []
    return values


def transact(ser, command: str, timeout_s: float, *, expect_response: bool = True) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    if not expect_response:
        return "<sent>"
    return read_scpi_response(ser, command, timeout_s, require_match=False)


def query_ints(ser, command: str, timeout_s: float, expected_count: int) -> list[int]:
    response = transact(ser, command, timeout_s)
    values = parse_csv_ints(response)
    if len(values) < expected_count:
        raise RuntimeError(f"{command} returned malformed response: {response!r}")
    return values


def drive_mask(ser, mask: int, timeout_s: float) -> None:
    # Existing validation firmware may not emit a payload for successful writes;
    # verify the requested value through the following OUTPut:MASK? query.
    transact(ser, f"REALtime:IO:OUTPut:MASK {mask}", min(timeout_s, 0.35))


def release_outputs(ser, timeout_s: float) -> None:
    try:
        drive_mask(ser, 0, timeout_s)
    finally:
        transact(ser,
                 "REALtime:IO:OUTPut:RELease",
                 min(timeout_s, 0.35),
                 expect_response=False)


def read_profile(ser, timeout_s: float) -> IoProfile:
    values = query_ints(ser, "REALtime:IO:PROFile?", timeout_s, 8)
    return IoProfile(*values[:8])


def check_product_profile(profile: IoProfile) -> list[str]:
    failures: list[str] = []
    expected = {
        "input_base": EXPECTED_INPUT_BASE,
        "input_count": EXPECTED_INPUT_COUNT,
        "output_base": EXPECTED_OUTPUT_BASE,
        "output_count": EXPECTED_OUTPUT_COUNT,
    }
    for name, value in expected.items():
        actual = getattr(profile, name)
        if actual != value:
            failures.append(f"profile {name}={actual}, expected {value}")
    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="product-board USB CDC port, normally COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--line-settle", type=float, default=0.05)
    parser.add_argument("--expected-build")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.timeout <= 0.0 or args.settle < 0.0 or args.line_settle < 0.0:
        raise SystemExit("timeout must be positive; settle values must be non-negative")

    started = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" / f"sma_loopback_{started}"
    )
    failures: list[str] = []
    steps: list[LoopbackStep] = []
    build = ""
    profile: IoProfile | None = None

    with open_serial_port(args.port, args.baud, args.timeout, args.settle) as ser:
        try:
            build = transact(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')
            if args.expected_build and build != args.expected_build:
                failures.append(f"build={build}, expected {args.expected_build}")

            profile = read_profile(ser, args.timeout)
            failures.extend(check_product_profile(profile))
            if failures:
                raise RuntimeError("product profile/build preflight failed")

            for mask in TEST_MASKS:
                drive_mask(ser, mask, args.timeout)
                time.sleep(args.line_settle)

                output_values = query_ints(
                    ser, "REALtime:IO:OUTPut:MASK?", args.timeout, 3
                )
                input_values = query_ints(
                    ser, "REALtime:IO:INPut:LEVel?", args.timeout, 3
                )
                reported_mask = output_values[2] & 0xF
                observed_mask = input_values[2] & 0xF
                passed = reported_mask == mask and observed_mask == mask
                steps.append(
                    LoopbackStep(
                        driven_mask=mask,
                        reported_output_mask=reported_mask,
                        observed_input_mask=observed_mask,
                        passed=passed,
                    )
                )
                if not passed:
                    failures.append(
                        f"mask 0x{mask:X}: output reported 0x{reported_mask:X}, "
                        f"input observed 0x{observed_mask:X}"
                    )
        except RuntimeError as exc:
            if str(exc) != "product profile/build preflight failed":
                failures.append(str(exc))
        finally:
            try:
                release_outputs(ser, args.timeout)
            except Exception as exc:  # Keep the primary electrical result visible.
                failures.append(f"output release failed: {exc}")

    passed = not failures and len(steps) == len(TEST_MASKS)
    summary = {
        "started": started,
        "port": args.port,
        "build": build,
        "wiring": ["OUT1->IN1", "OUT2->IN2", "OUT3->IN3", "OUT4->IN4"],
        "profile": asdict(profile) if profile is not None else None,
        "passed": passed,
        "steps": [asdict(step) for step in steps],
        "failures": failures,
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    lines = [
        f"Single-board SMA loopback: {'PASS' if passed else 'FAIL'}",
        f"port={args.port} build={build or '-'}",
        "wiring=OUT1->IN1 OUT2->IN2 OUT3->IN3 OUT4->IN4",
    ]
    if profile is not None:
        lines.append(
            f"profile=OUT GPIO{profile.output_base}.."
            f"{profile.output_base + profile.output_count - 1}, "
            f"IN physical GPIO{profile.input_base}.."
            f"{profile.input_base + profile.input_count - 1} (logical bit-reversed)"
        )
    for step in steps:
        state = "PASS" if step.passed else "FAIL"
        lines.append(
            f"{state} drive=0x{step.driven_mask:X} "
            f"reported=0x{step.reported_output_mask:X} "
            f"observed=0x{step.observed_input_mask:X}"
        )
    lines.extend(f"FAIL {failure}" for failure in failures)
    lines.append(f"summary={out_dir}")
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    for line in lines:
        print(line)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
