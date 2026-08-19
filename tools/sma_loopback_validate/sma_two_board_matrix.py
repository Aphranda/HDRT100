#!/usr/bin/env python3
"""Scan the complete two-board product SMA output-to-input wiring matrix.

Rows are A.OUT1..OUT4 and B.OUT1..OUT4. Columns are A.IN1..IN4 and
B.IN1..IN4. One output is driven at a time; boards are identified by *IDN?.
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
from scpi_common.scpi_serial import open_serial_port  # noqa: E402
from sma_two_board_in1_out1 import (  # noqa: E402
    Board,
    check_profile,
    discover,
    drive,
    read_input,
    read_output,
    read_profile,
    release,
    transact,
)


@dataclass(frozen=True)
class MatrixStep:
    cycle: int
    source_board: str
    output_channel: int
    output_mask_readback: int
    board_a_input_mask: int
    board_b_input_mask: int
    low_a_input_mask: int
    low_b_input_mask: int
    passed: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-a-id", required=True)
    parser.add_argument("--board-b-id", required=True)
    parser.add_argument("--expected-build")
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=0.3)
    parser.add_argument("--line-settle", type=float, default=0.03)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def labels(prefix: str, kind: str) -> list[str]:
    return [f"{prefix}.{kind}{index}" for index in range(1, 5)]


def main() -> int:
    args = parse_args()
    if args.board_a_id == args.board_b_id:
        raise SystemExit("board A and board B IDs must differ")
    if args.cycles <= 0:
        raise SystemExit("cycles must be positive")

    # Reuse the identity-first discovery contract from the directional tool.
    discovery_args = argparse.Namespace(**vars(args))
    boards = discover(discovery_args)
    missing = {args.board_a_id, args.board_b_id} - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    board_a: Board = boards[args.board_a_id]
    board_b: Board = boards[args.board_b_id]
    if args.expected_build:
        for board in (board_a, board_b):
            if board.build != args.expected_build:
                raise SystemExit(
                    f"{board.address}: build {board.build} != {args.expected_build}")

    rows = labels("A", "OUT") + labels("B", "OUT")
    columns = labels("A", "IN") + labels("B", "IN")
    hit_counts = [[0 for _ in columns] for _ in rows]
    steps: list[MatrixStep] = []
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
                raise RuntimeError("board identity changed after opening matrix test")
            profile_a = read_profile(ser_a, args.timeout)
            profile_b = read_profile(ser_b, args.timeout)
            check_profile(board_a, profile_a)
            check_profile(board_b, profile_b)

            for cycle in range(1, args.cycles + 1):
                for source_index, (source, other) in enumerate(
                    ((ser_a, ser_b), (ser_b, ser_a))
                ):
                    for output_index in range(4):
                        drive(ser_a, 0, args.timeout)
                        drive(ser_b, 0, args.timeout)
                        drive(source, 1 << output_index, args.timeout)
                        time.sleep(args.line_settle)

                        output_readback = read_output(source, args.timeout)
                        input_a = read_input(ser_a, args.timeout)
                        input_b = read_input(ser_b, args.timeout)
                        row = source_index * 4 + output_index
                        for bit in range(4):
                            if input_a & (1 << bit):
                                hit_counts[row][bit] += 1
                            if input_b & (1 << bit):
                                hit_counts[row][4 + bit] += 1

                        drive(source, 0, args.timeout)
                        time.sleep(args.line_settle)
                        low_a = read_input(ser_a, args.timeout)
                        low_b = read_input(ser_b, args.timeout)
                        passed = (output_readback == (1 << output_index) and
                                  low_a == 0 and low_b == 0)
                        steps.append(MatrixStep(
                            cycle=cycle,
                            source_board=board_a.address if source_index == 0
                                         else board_b.address,
                            output_channel=output_index + 1,
                            output_mask_readback=output_readback,
                            board_a_input_mask=input_a,
                            board_b_input_mask=input_b,
                            low_a_input_mask=low_a,
                            low_b_input_mask=low_b,
                            passed=passed,
                        ))
        except RuntimeError as exc:
            failures.append(str(exc))
            profile_a = locals().get("profile_a")
            profile_b = locals().get("profile_b")
        finally:
            for ser in (ser_a, ser_b):
                try:
                    release(ser, args.timeout)
                except Exception as exc:
                    failures.append(f"output release failed: {exc}")

    detected: dict[str, list[str]] = {}
    unstable: list[str] = []
    for row_index, row_name in enumerate(rows):
        detected[row_name] = []
        for column_index, column_name in enumerate(columns):
            hits = hit_counts[row_index][column_index]
            if hits == args.cycles:
                detected[row_name].append(column_name)
            elif hits != 0:
                unstable.append(
                    f"{row_name}->{column_name} observed {hits}/{args.cycles}")
    if unstable:
        failures.extend(unstable)
    failed_steps = [step for step in steps if not step.passed]
    failures.extend(
        f"cycle {step.cycle} {step.source_board}.OUT{step.output_channel} "
        f"readback=0x{step.output_mask_readback:X} low="
        f"0x{step.low_a_input_mask:X}/0x{step.low_b_input_mask:X}"
        for step in failed_steps
    )
    passed = not failures and len(steps) == args.cycles * 8

    result = {
        "passed": passed,
        "cycles": args.cycles,
        "boards": {
            "A": asdict(board_a),
            "B": asdict(board_b),
        },
        "profiles": {
            "A": asdict(profile_a) if profile_a else None,
            "B": asdict(profile_b) if profile_b else None,
        },
        "rows": rows,
        "columns": columns,
        "hit_counts": hit_counts,
        "detected_connections": detected,
        "failures": failures,
        "steps": [asdict(step) for step in steps],
    }
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" / f"sma_matrix_{stamp}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    cell_width = 7
    print(f"Two-board SMA wiring matrix: {'PASS' if passed else 'FAIL'}")
    print(f"A={board_a.address} port={board_a.port} build={board_a.build}")
    print(f"B={board_b.address} port={board_b.port} build={board_b.build}")
    print(" " * 9 + "".join(f"{name:>{cell_width}}" for name in columns))
    for row_index, row_name in enumerate(rows):
        cells = "".join(
            f"{hit_counts[row_index][column_index]:>{cell_width}}"
            for column_index in range(len(columns))
        )
        print(f"{row_name:>8} {cells}")
    print("Detected stable connections:")
    any_connection = False
    for row_name in rows:
        for column_name in detected[row_name]:
            any_connection = True
            print(f"  {row_name} -> {column_name}")
    if not any_connection:
        print("  none")
    for failure in failures[:20]:
        print(f"FAIL {failure}")
    print(f"summary={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
