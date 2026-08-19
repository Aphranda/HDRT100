#!/usr/bin/env python3
"""Sweep the product SMA OUT1/IN1 link frequency in both directions.

Boards are discovered by the unique address in the third field of ``*IDN?``;
COM port numbers are only transient transport endpoints. The firmware hardware
generator drives OUT1 and the peer hardware edge counter measures IN1.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from contextlib import ExitStack
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from scpi_common.board_identity import parse_idn_response  # noqa: E402
from scpi_common.scpi_serial import (  # noqa: E402
    is_scpi_log_line,
    open_serial_port,
    read_serial_line_idle,
    strip_scpi_ack_prefix,
    trim_embedded_scpi_log,
)
from sma_two_board_in1_out1 import (  # noqa: E402
    Board,
    check_profile,
    discover,
    read_profile,
    release,
    transact,
)


DEFAULT_FREQUENCIES_MHZ = "1,2,5,10,15,20,25,30,35,40,45,50"


@dataclass(frozen=True)
class SweepStep:
    direction: str
    source_address: str
    target_address: str
    requested_hz: int
    generated_hz: int
    measured_hz: int
    edge_count: int
    gate_us: int
    elapsed_us: int
    error_ppm: int
    passed: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-a-id", required=True)
    parser.add_argument("--board-b-id", required=True)
    parser.add_argument("--expected-build")
    parser.add_argument("--frequencies-mhz", default=DEFAULT_FREQUENCIES_MHZ)
    parser.add_argument("--gate-us", type=int, default=1000)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--max-error-ppm", type=int, default=10000)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.3)
    parser.add_argument("--line-settle", type=float, default=0.02)
    parser.add_argument("--one-way", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def parse_frequencies(text: str) -> list[int]:
    values: list[int] = []
    for field in text.split(","):
        mhz = int(field.strip(), 0)
        if mhz < 1 or mhz > 50:
            raise SystemExit(f"frequency outside 1..50 MHz: {mhz}")
        values.append(mhz * 1_000_000)
    if not values:
        raise SystemExit("frequency list must not be empty")
    return values


def parse_uints(response: str, count: int) -> list[int]:
    try:
        values = [int(field.strip().strip('"'), 0)
                  for field in response.split(",")]
    except ValueError as exc:
        raise RuntimeError(f"malformed integer response: {response!r}") from exc
    if len(values) != count:
        raise RuntimeError(
            f"response field count {len(values)} != {count}: {response!r}")
    return values


def transact_uints(ser, command: str, count: int, timeout_s: float) -> list[int]:
    """Read a numeric CSV response even when RTOS scheduling splits CDC output."""
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    deadline = time.monotonic() + timeout_s
    response = ""
    while time.monotonic() < deadline:
        fragment = read_serial_line_idle(ser, deadline, idle_gap_s=0.2)
        if fragment is None or is_scpi_log_line(fragment):
            continue
        fragment = strip_scpi_ack_prefix(trim_embedded_scpi_log(fragment))
        if not fragment:
            continue
        response += fragment
        if response.count(",") >= count - 1:
            return parse_uints(response, count)
    raise RuntimeError(f"incomplete response to {command}: {response!r}")


def stop(ser, timeout_s: float) -> None:
    try:
        transact(ser, "REALtime:IO:SMA:FREQuency:STOP", timeout_s)
    finally:
        release(ser, timeout_s)


def run_direction(source_name: str,
                  source_board: Board,
                  source,
                  target_name: str,
                  target_board: Board,
                  target,
                  frequencies: list[int],
                  args: argparse.Namespace) -> list[SweepStep]:
    results: list[SweepStep] = []
    for requested_hz in frequencies:
        actual_response = transact(
            source,
            f"REALtime:IO:SMA:FREQuency:TX 1,{requested_hz}",
            args.timeout)
        generated_hz = int(actual_response.strip().strip('"'), 0)
        time.sleep(args.line_settle)
        for _ in range(args.repeats):
            values = transact_uints(
                target,
                f"REALtime:IO:SMA:FREQuency:RX? 1,{args.gate_us}",
                6,
                args.timeout)
            measured_hz = values[5]
            error_ppm = round(
                (measured_hz - generated_hz) * 1_000_000 / generated_hz)
            passed = (values[0] == 1 and values[1] == 23 and
                      values[4] > 0 and
                      abs(error_ppm) <= args.max_error_ppm)
            results.append(SweepStep(
                direction=f"{source_name}->{target_name}",
                source_address=source_board.address,
                target_address=target_board.address,
                requested_hz=requested_hz,
                generated_hz=generated_hz,
                measured_hz=measured_hz,
                edge_count=values[4],
                gate_us=values[2],
                elapsed_us=values[3],
                error_ppm=error_ppm,
                passed=passed,
            ))
        stop(source, args.timeout)
    return results


def main() -> int:
    args = parse_args()
    if args.board_a_id == args.board_b_id:
        raise SystemExit("board A and board B IDs must differ")
    if args.repeats <= 0:
        raise SystemExit("repeats must be positive")
    if args.gate_us < 100 or args.gate_us > 1000:
        raise SystemExit("gate-us must be within 100..1000")
    frequencies = parse_frequencies(args.frequencies_mhz)

    boards = discover(argparse.Namespace(**vars(args)))
    missing = {args.board_a_id, args.board_b_id} - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    board_a = boards[args.board_a_id]
    board_b = boards[args.board_b_id]
    if args.expected_build:
        for board in (board_a, board_b):
            if board.build != args.expected_build:
                raise SystemExit(
                    f"{board.address}: build {board.build} != {args.expected_build}")

    steps: list[SweepStep] = []
    failures: list[str] = []
    with ExitStack() as stack:
        ser_a = stack.enter_context(open_serial_port(
            board_a.port, args.baud, args.timeout, args.settle))
        ser_b = stack.enter_context(open_serial_port(
            board_b.port, args.baud, args.timeout, args.settle))
        try:
            identity_a = parse_idn_response(transact(ser_a, "*IDN?", args.timeout))
            identity_b = parse_idn_response(transact(ser_b, "*IDN?", args.timeout))
            if identity_a.address != board_a.address or identity_b.address != board_b.address:
                raise RuntimeError("board identity changed after opening sweep ports")
            profile_a = read_profile(ser_a, args.timeout)
            profile_b = read_profile(ser_b, args.timeout)
            check_profile(board_a, profile_a)
            check_profile(board_b, profile_b)
            stop(ser_a, args.timeout)
            stop(ser_b, args.timeout)
            steps.extend(run_direction(
                "A", board_a, ser_a, "B", board_b, ser_b, frequencies, args))
            if not args.one_way:
                steps.extend(run_direction(
                    "B", board_b, ser_b, "A", board_a, ser_a, frequencies, args))
        except (RuntimeError, ValueError) as exc:
            failures.append(str(exc))
        finally:
            for ser in (ser_a, ser_b):
                try:
                    stop(ser, args.timeout)
                except Exception as exc:
                    failures.append(f"cleanup failed: {exc}")

    failed_steps = [step for step in steps if not step.passed]
    failures.extend(
        f"{step.direction} {step.requested_hz}Hz generated={step.generated_hz} "
        f"measured={step.measured_hz} error_ppm={step.error_ppm}"
        for step in failed_steps)
    directions = ["A->B"] if args.one_way else ["A->B", "B->A"]
    expected_steps = len(directions) * len(frequencies) * args.repeats
    passed = not failures and len(steps) == expected_steps

    last_pass_hz: dict[str, int] = {}
    for direction in directions:
        stable = []
        for frequency in frequencies:
            group = [step for step in steps
                     if step.direction == direction and
                     step.requested_hz == frequency]
            if len(group) == args.repeats and all(step.passed for step in group):
                stable.append(frequency)
        last_pass_hz[direction] = max(stable, default=0)

    result = {
        "passed": passed,
        "boards": {"A": asdict(board_a), "B": asdict(board_b)},
        "frequencies_hz": frequencies,
        "gate_us": args.gate_us,
        "repeats": args.repeats,
        "max_error_ppm": args.max_error_ppm,
        "last_pass_hz": last_pass_hz,
        "failures": failures,
        "steps": [asdict(step) for step in steps],
    }
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" / f"sma_frequency_{stamp}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")

    print(f"Two-board SMA frequency sweep: {'PASS' if passed else 'FAIL'}")
    print(f"A={board_a.address} port={board_a.port} build={board_a.build}")
    print(f"B={board_b.address} port={board_b.port} build={board_b.build}")
    print(f"{'Direction':>9} {'Req MHz':>8} {'Gen Hz':>10} "
          f"{'Min Hz':>10} {'Max Hz':>10} {'Worst ppm':>10} Result")
    for direction in directions:
        for frequency in frequencies:
            group = [step for step in steps
                     if step.direction == direction and
                     step.requested_hz == frequency]
            if not group:
                print(f"{direction:>9} {frequency / 1e6:>8.3f} "
                      f"{'-':>10} {'-':>10} {'-':>10} {'-':>10} MISSING")
                continue
            worst = max(abs(step.error_ppm) for step in group)
            status = "PASS" if all(step.passed for step in group) else "FAIL"
            print(f"{direction:>9} {frequency / 1e6:>8.3f} "
                  f"{group[0].generated_hz:>10} "
                  f"{min(step.measured_hz for step in group):>10} "
                  f"{max(step.measured_hz for step in group):>10} "
                  f"{worst:>10} {status}")
    for direction in directions:
        print(f"last_pass_{direction}={last_pass_hz[direction]}Hz")
    for failure in failures[:20]:
        print(f"FAIL {failure}")
    print(f"summary={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
