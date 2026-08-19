#!/usr/bin/env python3
"""Validate product-board SMA OUT1/IN1 links on two boards.

The tool auto-detects either crossed wiring (A.OUT1->B.IN1 and B.OUT1->A.IN1)
or two local loopbacks (A.OUT1->A.IN1 and B.OUT1->B.IN1).

Boards are discovered and verified by the third field of ``*IDN?``. COM port
numbers are treated only as transient transport endpoints.
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

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.board_identity import parse_idn_response  # noqa: E402
from scpi_common.scpi_serial import open_serial_port, read_scpi_response  # noqa: E402


EXPECTED_INPUT_BASE = 20
EXPECTED_INPUT_COUNT = 4
EXPECTED_OUTPUT_BASE = 16
EXPECTED_OUTPUT_COUNT = 4
OUT1_MASK = 0x1


@dataclass(frozen=True)
class Board:
    port: str
    address: str
    idn: str
    build: str


@dataclass(frozen=True)
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
class Step:
    cycle: int
    source: str
    target: str
    level: int
    source_output_mask: int
    target_input_mask: int
    source_input_mask: int
    observed_receiver: str
    passed: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-a-id", required=True,
                        help="exact *IDN? third-field address for board A")
    parser.add_argument("--board-b-id", required=True,
                        help="exact *IDN? third-field address for board B")
    parser.add_argument("--expected-build")
    parser.add_argument("--cycles", type=int, default=20)
    parser.add_argument("--require-directions", type=int, choices=(1, 2),
                        default=1,
                        help="number of independently wired OUT1->IN1 directions required")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=0.3)
    parser.add_argument("--line-settle", type=float, default=0.02)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def transact(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, command, timeout_s, require_match=True)


def parse_ints(response: str) -> list[int]:
    values: list[int] = []
    for field in response.split(","):
        try:
            values.append(int(field.strip().strip('"'), 0))
        except ValueError:
            return []
    return values


def probe(port: str, wanted: set[str], args: argparse.Namespace) -> Board | None:
    try:
        with open_serial_port(port, args.baud, args.timeout, args.settle) as ser:
            identity = parse_idn_response(transact(ser, "*IDN?", args.timeout))
            if identity.address not in wanted:
                return None
            build = transact(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')
            return Board(port, identity.address, identity.idn, build)
    except (OSError, serial.SerialException, RuntimeError, ValueError):
        return None


def discover(args: argparse.Namespace) -> dict[str, Board]:
    wanted = {args.board_a_id, args.board_b_id}
    found: dict[str, Board] = {}
    for item in list_ports.comports():
        board = probe(item.device, wanted, args)
        if board is not None:
            found[board.address] = board
    return found


def query_values(ser: serial.Serial, command: str, count: int,
                 timeout_s: float) -> list[int]:
    response = "<timeout>"
    for _ in range(4):
        response = transact(ser, command, timeout_s)
        values = parse_ints(response)
        if len(values) >= count:
            return values
        time.sleep(0.02)
    raise RuntimeError(f"{command} returned malformed response: {response!r}")


def read_profile(ser: serial.Serial, timeout_s: float) -> IoProfile:
    return IoProfile(*query_values(
        ser, "REALtime:IO:PROFile?", 8, timeout_s)[:8])


def check_profile(board: Board, profile: IoProfile) -> None:
    actual = (profile.input_base, profile.input_count,
              profile.output_base, profile.output_count)
    expected = (EXPECTED_INPUT_BASE, EXPECTED_INPUT_COUNT,
                EXPECTED_OUTPUT_BASE, EXPECTED_OUTPUT_COUNT)
    if actual != expected:
        raise RuntimeError(
            f"{board.address}: IO profile {actual}, expected {expected}")


def drive(ser: serial.Serial, mask: int, timeout_s: float) -> None:
    # Successful setters return a bare OK which the shared reader filters.
    # The following OUTPut:MASK? query is the authoritative readback.
    transact(ser, f"REALtime:IO:OUTPut:MASK {mask}", min(timeout_s, 0.25))


def read_output(ser: serial.Serial, timeout_s: float) -> int:
    return query_values(ser, "REALtime:IO:OUTPut:MASK?", 3, timeout_s)[2] & 0xF


def read_input(ser: serial.Serial, timeout_s: float) -> int:
    return query_values(ser, "REALtime:IO:INPut:LEVel?", 3, timeout_s)[2] & 0xF


def release(ser: serial.Serial, timeout_s: float) -> None:
    try:
        drive(ser, 0, timeout_s)
    finally:
        ser.reset_input_buffer()
        ser.write(b"REALtime:IO:OUTPut:RELease\n")
        ser.flush()


def exercise_direction(cycle: int, source_board: Board,
                       source: serial.Serial, target_board: Board,
                       target: serial.Serial, args: argparse.Namespace) -> list[Step]:
    steps: list[Step] = []
    for level in (1, 0):
        mask = OUT1_MASK if level else 0
        drive(source, mask, args.timeout)
        time.sleep(args.line_settle)
        source_output = read_output(source, args.timeout)
        target_input = read_input(target, args.timeout)
        source_input = read_input(source, args.timeout)
        # Only one IN1 should follow the source. This both detects crossed vs
        # local wiring and rejects shorts/channel reversal on bits 1..3.
        if level == 0:
            observed_receiver = "none"
            passed = (source_output == 0 and target_input == 0 and
                      source_input == 0)
        elif target_input == OUT1_MASK and source_input == 0:
            observed_receiver = target_board.address
            passed = source_output == OUT1_MASK
        elif source_input == OUT1_MASK and target_input == 0:
            observed_receiver = source_board.address
            passed = source_output == OUT1_MASK
        else:
            observed_receiver = "ambiguous"
            passed = False
        steps.append(Step(cycle, source_board.address, target_board.address,
                          level, source_output, target_input, source_input,
                          observed_receiver, passed))
    return steps


def main() -> int:
    args = parse_args()
    if args.board_a_id == args.board_b_id:
        raise SystemExit("board A and board B IDs must differ")
    if args.cycles <= 0:
        raise SystemExit("cycles must be positive")

    boards = discover(args)
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

    failures: list[str] = []
    steps: list[Step] = []
    with ExitStack() as stack:
        ser_a = stack.enter_context(open_serial_port(
            board_a.port, args.baud, args.timeout, args.settle))
        ser_b = stack.enter_context(open_serial_port(
            board_b.port, args.baud, args.timeout, args.settle))
        try:
            identity_a = parse_idn_response(transact(ser_a, "*IDN?", args.timeout))
            identity_b = parse_idn_response(transact(ser_b, "*IDN?", args.timeout))
            if identity_a.address != board_a.address or identity_b.address != board_b.address:
                raise RuntimeError("board identity changed after opening test ports")
            profile_a = read_profile(ser_a, args.timeout)
            profile_b = read_profile(ser_b, args.timeout)
            check_profile(board_a, profile_a)
            check_profile(board_b, profile_b)
            drive(ser_a, 0, args.timeout)
            drive(ser_b, 0, args.timeout)
            time.sleep(args.line_settle)
            baseline_a = read_input(ser_a, args.timeout)
            baseline_b = read_input(ser_b, args.timeout)
            if baseline_a != 0 or baseline_b != 0:
                failures.append(
                    f"non-zero baseline: A=0x{baseline_a:X} B=0x{baseline_b:X}")

            for cycle in range(1, args.cycles + 1):
                steps.extend(exercise_direction(
                    cycle, board_a, ser_a, board_b, ser_b, args))
                steps.extend(exercise_direction(
                    cycle, board_b, ser_b, board_a, ser_a, args))
        except RuntimeError as exc:
            failures.append(str(exc))
            profile_a = locals().get("profile_a")
            profile_b = locals().get("profile_b")
        finally:
            for ser in (ser_a, ser_b):
                try:
                    release(ser, args.timeout)
                except Exception as exc:  # preserve primary electrical result
                    failures.append(f"output release failed: {exc}")

    failed_steps = [step for step in steps if not step.passed]
    high_receivers: dict[str, set[str]] = {
        board_a.address: set(),
        board_b.address: set(),
    }
    for step in steps:
        if step.level == 1 and step.passed:
            high_receivers[step.source].add(step.observed_receiver)
    topology = "unknown"
    if high_receivers[board_a.address] == {board_b.address} and \
       high_receivers[board_b.address] == {board_a.address}:
        topology = "crossed"
    elif high_receivers[board_a.address] == {board_a.address} and \
         high_receivers[board_b.address] == {board_b.address}:
        topology = "local_loopbacks"
    elif high_receivers[board_a.address] == {board_b.address} and \
         not high_receivers[board_b.address]:
        topology = "single_a_out1_to_b_in1"
    elif high_receivers[board_b.address] == {board_a.address} and \
         not high_receivers[board_a.address]:
        topology = "single_b_out1_to_a_in1"
    elif high_receivers[board_a.address] == {board_a.address} and \
         not high_receivers[board_b.address]:
        topology = "single_a_local_loopback"
    elif high_receivers[board_b.address] == {board_b.address} and \
         not high_receivers[board_a.address]:
        topology = "single_b_local_loopback"
    else:
        failures.append(
            f"inconsistent topology: {board_a.address} receivers="
            f"{sorted(high_receivers[board_a.address])}, "
            f"{board_b.address} receivers="
            f"{sorted(high_receivers[board_b.address])}")
    step_failure_details = [
        f"cycle {step.cycle} {step.source}->{step.target} level={step.level} "
        f"out=0x{step.source_output_mask:X} target_in=0x{step.target_input_mask:X} "
        f"source_in=0x{step.source_input_mask:X}"
        for step in failed_steps
    ]
    direction_a_passed = all(
        step.passed for step in steps if step.source == board_a.address)
    direction_b_passed = all(
        step.passed for step in steps if step.source == board_b.address)
    valid_direction_count = int(direction_a_passed) + int(direction_b_passed)
    if valid_direction_count < args.require_directions:
        failures.append(
            f"valid directions={valid_direction_count}, required={args.require_directions}")
    passed = not failures and len(steps) == args.cycles * 4
    result = {
        "passed": passed,
        "cycles": args.cycles,
        "required_directions": args.require_directions,
        "valid_direction_count": valid_direction_count,
        "direction_results": {
            f"{board_a.address}_OUT1": direction_a_passed,
            f"{board_b.address}_OUT1": direction_b_passed,
        },
        "detected_topology": topology,
        "boards": {board.address: asdict(board) for board in (board_a, board_b)},
        "profiles": {
            board_a.address: asdict(profile_a) if profile_a else None,
            board_b.address: asdict(profile_b) if profile_b else None,
        },
        "step_count": len(steps),
        "failed_step_count": len(failed_steps),
        "failures": failures,
        "step_failure_details": step_failure_details,
        "steps": [asdict(step) for step in steps],
    }
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" / f"sma_two_board_{stamp}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(f"Two-board SMA OUT1/IN1: {'PASS' if passed else 'FAIL'}")
    for board in (board_a, board_b):
        print(f"  {board.address} port={board.port} build={board.build}")
    print(f"  cycles={args.cycles} steps={len(steps)} failed={len(failed_steps)}")
    print(f"  detected_topology={topology}")
    print(f"  valid_directions={valid_direction_count}/{args.require_directions} required")
    print(f"  {board_a.address}.OUT1 direction={'PASS' if direction_a_passed else 'NOT CONNECTED/FAIL'}")
    print(f"  {board_b.address}.OUT1 direction={'PASS' if direction_b_passed else 'NOT CONNECTED/FAIL'}")
    for failure in failures[:20]:
        print(f"  FAIL {failure}")
    print(f"summary={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
