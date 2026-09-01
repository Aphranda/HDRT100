#!/usr/bin/env python3
"""Verify fixed NO1..NO4 SMA_OUT1 to NO5 SMA_IN1..4 wiring."""

from __future__ import annotations

import argparse
import json
import sys
import time
from contextlib import ExitStack
from dataclasses import asdict, dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
TDMA_TOOLS = TOOLS / "tdma_ring_monitor"
for item in (TOOLS, TDMA_TOOLS):
    if str(item) not in sys.path:
        sys.path.insert(0, str(item))

from scpi_common.scpi_serial import (  # noqa: E402
    is_scpi_log_line,
    open_serial_port,
    read_scpi_response,
    read_serial_line_idle,
    strip_scpi_ack_prefix,
    trim_embedded_scpi_log,
)
from tdma_start_ring import discover  # noqa: E402

SOURCE_COUNT = 4
VALIDATOR_BOARD_NO = 5
MATRIX_SIZE = 4


@dataclass(frozen=True)
class WireMeasurement:
    source_board_no: int
    source_output_channel: int
    validator_input_channel: int
    driven_output_mask: int
    observed_high_input_mask: int
    observed_low_input_mask: int
    detected: bool


def parse_ints(response: str, expected_count: int) -> list[int]:
    try:
        values = [int(part.strip().strip('"'), 0)
                  for part in response.split(",")]
    except ValueError as exc:
        raise ValueError(f"malformed integer response: {response!r}") from exc
    if len(values) != expected_count:
        raise ValueError(
            f"response has {len(values)} fields, expected {expected_count}: {response!r}")
    return values


def infer_wire_order(measurements: list[WireMeasurement]) -> tuple[list[int], bool]:
    inferred: list[int] = []
    for source_no in range(1, SOURCE_COUNT + 1):
        detected = [item.validator_input_channel for item in measurements
                    if item.source_board_no == source_no and item.detected]
        inferred.append(detected[0] if len(detected) == 1 else 0)
    return inferred, inferred == [1, 2, 3, 4]


def query(ser, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    response = read_scpi_response(ser, command, timeout_s, require_match=True)
    if response == "<timeout>":
        raise RuntimeError(f"timeout waiting for {command}")
    return response


def query_ints(ser, command: str, expected_count: int,
               timeout_s: float) -> list[int]:
    """Collect a numeric response split by RTOS scheduling or CDC packets."""
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
        if response.count(",") >= expected_count - 1:
            return parse_ints(response, expected_count)
    raise RuntimeError(f"incomplete response to {command}: {response!r}")


def command_ack_optional(ser, command: str, timeout_s: float) -> None:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    (void_response := read_scpi_response(
        ser, command, min(timeout_s, 0.3), require_match=False))
    del void_response


def write_matrix_svg(path: Path,
                     measurements: list[WireMeasurement],
                     passed: bool) -> None:
    cell = 92
    left = 150
    top = 85
    width = left + cell * 4 + 40
    height = top + cell * 4 + 70
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{width / 2}" y="30" text-anchor="middle" font-size="20">'
        f'SMA wire order: {"PASS" if passed else "FAIL"}</text>',
    ]
    by_pair = {(item.source_board_no, item.validator_input_channel): item
               for item in measurements}
    for input_channel in range(1, 5):
        parts.append(
            f'<text x="{left + (input_channel - 0.5) * cell}" y="64" '
            f'text-anchor="middle">NO5 IN{input_channel}</text>')
    for source_no in range(1, 5):
        y = top + (source_no - 1) * cell
        parts.append(
            f'<text x="135" y="{y + cell / 2}" text-anchor="end">'
            f'NO{source_no} OUT1</text>')
        for input_channel in range(1, 5):
            item = by_pair[(source_no, input_channel)]
            expected = source_no == input_channel
            good = item.detected == expected
            color = "#bbf7d0" if good else "#fecaca"
            label = "HIGH" if item.detected else "LOW"
            parts.extend((
                f'<rect x="{left + (input_channel - 1) * cell}" y="{y}" '
                f'width="{cell - 4}" height="{cell - 4}" fill="{color}" '
                'stroke="#64748b"/>',
                f'<text x="{left + (input_channel - 0.5) * cell - 2}" '
                f'y="{y + cell / 2}" text-anchor="middle" font-size="12">'
                f'{label}</text>',
            ))
    parts.append('</svg>')
    path.write_text("".join(parts), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address; repeat for NO1..NO5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.3)
    parser.add_argument("--signal-settle", type=float, default=0.05)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if len(args.board_id) != 5 or len(set(args.board_id)) != 5:
        parser.error("exactly five unique --board-id values are required")
    args.board_ids = list(args.board_id)
    args.expected_build = None
    args.short_open = False

    boards = discover(args)
    missing = set(args.board_ids) - set(boards)
    if missing:
        raise RuntimeError(f"boards not discovered: {sorted(missing)}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    measurements: list[WireMeasurement] = []
    identities: dict[int, dict[str, str]] = {}
    with ExitStack() as stack:
        connections = {}
        for board in boards.values():
            ser = stack.enter_context(open_serial_port(
                board.port, args.baud, args.timeout, args.settle))
            board_no = int(query(ser, "SYST:BOARD:NO?", args.timeout).strip('"'), 0)
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
        for source_no in range(1, SOURCE_COUNT + 1):
            command_ack_optional(connections[source_no],
                                 "REALtime:IO:OUTPut:RELease",
                                 args.timeout)
        for source_no in range(1, SOURCE_COUNT + 1):
            source = connections[source_no]
            output_channel = 1
            output_mask = 1 << (output_channel - 1)
            command_ack_optional(source,
                                 f"REALtime:IO:OUTPut:MASK {output_mask}",
                                 args.timeout)
            try:
                time.sleep(args.signal_settle)
                high_values = query_ints(
                    validator, "REALtime:IO:INPut:LEVel?", 3, args.timeout)
                high_mask = high_values[2] & 0xF
                command_ack_optional(source,
                                     "REALtime:IO:OUTPut:MASK 0",
                                     args.timeout)
                time.sleep(args.signal_settle)
                low_values = query_ints(
                    validator, "REALtime:IO:INPut:LEVel?", 3, args.timeout)
                low_mask = low_values[2] & 0xF
                for input_channel in range(1, MATRIX_SIZE + 1):
                    input_mask = 1 << (input_channel - 1)
                    detected = ((high_mask & input_mask) != 0 and
                                (low_mask & input_mask) == 0)
                    measurements.append(WireMeasurement(
                        source_no,
                        output_channel,
                        input_channel,
                        output_mask,
                        high_mask,
                        low_mask,
                        detected,
                    ))
            finally:
                command_ack_optional(
                    source, "REALtime:IO:OUTPut:RELease", args.timeout)

    inferred, passed = infer_wire_order(measurements)
    output = {
        "schema": "sma-cable-delay/wire-order-v1",
        "passed": passed,
        "expected_source_to_input": [1, 2, 3, 4],
        "inferred_source_to_input": inferred,
        "identities": identities,
        "measurements": [asdict(item) for item in measurements],
    }
    result_path = args.output_dir / "sma_cable_wire_order.json"
    result_path.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n",
                           encoding="utf-8")
    write_matrix_svg(args.output_dir / "sma_cable_wire_order.svg",
                     measurements, passed)
    print(result_path)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
