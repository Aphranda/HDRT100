#!/usr/bin/env python3
"""Validate fixed NO1..NO4 SMA sources against the four NO5 inputs.

The static 4x4 wire-order matrix is a mandatory preflight.  Dynamic captures
are retained as repeatability diagnostics only: independent source and
validator clocks do not provide the shared phase reference required for a
cable-only SFCW delay fit.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from contextlib import ExitStack
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
HERE = Path(__file__).resolve().parent
TDMA_TOOLS = TOOLS / "tdma_ring_monitor"
for item in (TOOLS, HERE, TDMA_TOOLS):
    if str(item) not in sys.path:
        sys.path.insert(0, str(item))

from scpi_common.scpi_serial import open_serial_port  # noqa: E402
from sma_cable_delay_sweep import parse_frequencies, parse_response  # noqa: E402
from sma_cable_wire_order import (  # noqa: E402
    command_ack_optional,
    query,
    query_ints,
)
from tdma_start_ring import discover  # noqa: E402

SOURCE_BOARD_NOS = (1, 2, 3, 4)
VALIDATOR_BOARD_NO = 5
WIRE_ORDER_TOOL = HERE / "sma_cable_wire_order.py"


def require_wire_order_result(path: Path) -> dict[str, object]:
    result = json.loads(path.read_text(encoding="utf-8"))
    expected = [1, 2, 3, 4]
    if (result.get("schema") != "sma-cable-delay/wire-order-v1" or
            result.get("passed") is not True or
            result.get("inferred_source_to_input") != expected):
        raise RuntimeError(
            "SMA wire-order preflight failed; dynamic validation is blocked")
    return result


def run_wire_order_preflight(args: argparse.Namespace) -> tuple[Path, dict[str, object]]:
    output_dir = args.output_dir / "wire-order"
    command = [
        sys.executable,
        str(WIRE_ORDER_TOOL),
        "--baud", str(args.baud),
        "--timeout", str(args.timeout),
        "--settle", str(args.settle),
        "--signal-settle", str(args.signal_settle),
        "--output-dir", str(output_dir),
    ]
    for board_id in args.board_id:
        command.extend(("--board-id", board_id))
    completed = subprocess.run(command, cwd=ROOT, check=False)
    result_path = output_dir / "sma_cable_wire_order.json"
    if completed.returncode != 0 or not result_path.exists():
        raise RuntimeError(
            f"SMA wire-order preflight exited with {completed.returncode}")
    return result_path, require_wire_order_result(result_path)


def wrapped_phase_delta_mdeg(value: int, reference: int) -> int:
    delta = value - reference
    while delta >= 180_000:
        delta -= 360_000
    while delta < -180_000:
        delta += 360_000
    return delta


def phase_repeat_spread_mdeg(values: list[int]) -> int | None:
    if not values:
        return None
    deltas = [wrapped_phase_delta_mdeg(value, values[0]) for value in values]
    return max(deltas) - min(deltas)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address; repeat for NO1..NO5")
    parser.add_argument("--frequencies-mhz", default="2,4,6,8,10,12,14,16,18")
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--capture-words", type=int, default=256)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.3)
    parser.add_argument("--signal-settle", type=float, default=0.05)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if len(args.board_id) != 5 or len(set(args.board_id)) != 5:
        parser.error("exactly five unique --board-id values are required")
    if args.repeats < 2:
        parser.error("repeats must be at least 2")
    if args.capture_words < 16 or args.capture_words > 512:
        parser.error("capture-words must be within 16..512")
    try:
        frequencies = parse_frequencies(args.frequencies_mhz)
    except ValueError as exc:
        parser.error(str(exc))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    wire_path, wire_result = run_wire_order_preflight(args)

    discovery_args = SimpleNamespace(
        board_ids=list(args.board_id),
        baud=args.baud,
        timeout=args.timeout,
        settle=args.settle,
        expected_build=None,
        short_open=False,
    )
    boards = discover(discovery_args)
    missing = set(args.board_id) - set(boards)
    if missing:
        raise RuntimeError(f"boards not discovered: {sorted(missing)}")

    records: list[dict[str, object]] = []
    identities: dict[int, dict[str, str]] = {}
    with ExitStack() as stack:
        connections = {}
        for board in boards.values():
            ser = stack.enter_context(open_serial_port(
                board.port, args.baud, args.timeout, args.settle))
            board_no = int(query(
                ser, "SYST:BOARD:NO?", args.timeout).strip('"'), 0)
            if board_no in connections:
                raise RuntimeError(f"duplicate BOARD:NO={board_no}")
            connections[board_no] = ser
            identities[board_no] = {
                "address": board.address,
                "port": board.port,
                "build": board.build,
            }
        if sorted(connections) != [1, 2, 3, 4, 5]:
            raise RuntimeError(
                f"BOARD:NO values must be [1,2,3,4,5], got {sorted(connections)}")

        validator = connections[VALIDATOR_BOARD_NO]
        for frequency_hz in frequencies:
            started: list[int] = []
            try:
                source_actual_hz = {}
                for source_no in SOURCE_BOARD_NOS:
                    response = query_ints(
                        connections[source_no],
                        f"CAL:SMA:CABL:SOUR:STAR {frequency_hz},{source_no}",
                        3,
                        args.timeout,
                    )
                    source_actual_hz[source_no] = response[1]
                    started.append(source_no)
                time.sleep(args.signal_settle)
                for repeat in range(args.repeats):
                    response = query_ints(
                        validator,
                        f"READ:CAL:SMA:CABL:VAL? {frequency_hz},{args.capture_words}",
                        17,
                        args.timeout,
                    )
                    parsed = parse_response(",".join(str(value) for value in response))
                    parsed["repeat"] = repeat
                    parsed["source_actual_frequency_hz"] = source_actual_hz
                    records.append(parsed)
            finally:
                for source_no in started:
                    command_ack_optional(
                        connections[source_no],
                        "CAL:SMA:CABL:SOUR:STOP",
                        args.timeout,
                    )

    repeatability = []
    for frequency_hz in frequencies:
        frequency_records = [
            record for record in records
            if int(record["requested_frequency_hz"]) == frequency_hz
        ]
        for channel in SOURCE_BOARD_NOS:
            phase_values = [
                int(record["channels"][channel - 1]["phase_mdeg"])
                for record in frequency_records
                if record["channels"][channel - 1]["valid"]
            ]
            repeatability.append({
                "frequency_hz": frequency_hz,
                "channel": channel,
                "valid_count": len(phase_values),
                "phase_spread_mdeg": phase_repeat_spread_mdeg(phase_values),
            })

    output = {
        "schema": "sma-cable-delay/five-board-diagnostic-v1",
        "wire_order_gate": {
            "passed": True,
            "evidence": str(wire_path),
            "inferred_source_to_input": wire_result["inferred_source_to_input"],
        },
        "identities": identities,
        "fixed_connections": [
            f"NO{channel} OUT{channel} -> NO5 IN{channel}"
            for channel in SOURCE_BOARD_NOS
        ],
        "phase_reference": "independent_source_and_validator_clocks",
        "phase_coherent": False,
        "phase_slope_fit_allowed": False,
        "cable_only_delay_valid": False,
        "measurement_boundary": "source output + cable + NO5 input",
        "coarse_equal_cable_relative_baseline_ps": [0, 0, 0, 0],
        "repeatability": repeatability,
        "records": records,
    }
    result_path = args.output_dir / "sma_cable_five_board_diagnostic.json"
    result_path.write_text(
        json.dumps(output, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(result_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
