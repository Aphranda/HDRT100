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
    normalized = sorted(value % 360_000 for value in values)
    gaps = [current - previous
            for previous, current in zip(normalized, normalized[1:])]
    gaps.append(normalized[0] + 360_000 - normalized[-1])
    return 360_000 - max(gaps)


def write_diagnostic_svg(path: Path,
                         repeatability: list[dict[str, object]]) -> None:
    width = 960
    height = 560
    left = 90
    top = 75
    plot_width = 820
    plot_height = 390
    frequencies = sorted({int(item["frequency_hz"])
                          for item in repeatability})
    colors = ("#2563eb", "#dc2626", "#16a34a", "#9333ea")

    def x_position(frequency_hz: int) -> float:
        if len(frequencies) == 1:
            return left + plot_width / 2
        return (left + (frequency_hz - frequencies[0]) * plot_width /
                (frequencies[-1] - frequencies[0]))

    def y_position(spread_mdeg: int) -> float:
        return top + plot_height - spread_mdeg * plot_height / 360_000

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="480" y="30" text-anchor="middle" font-size="20">'
        'Five-board SMA phase repeatability</text>',
        '<text x="480" y="54" text-anchor="middle" font-size="13" '
        'fill="#b91c1c">Independent clocks: diagnostic only; '
        'phase-slope delay fit blocked</text>',
    ]
    for degree in range(0, 361, 60):
        y = y_position(degree * 1000)
        parts.extend((
            f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_width}" '
            f'y2="{y:.2f}" stroke="#e2e8f0"/>',
            f'<text x="{left - 12}" y="{y + 4:.2f}" text-anchor="end" '
            f'font-size="12">{degree}°</text>',
        ))
    for frequency_hz in frequencies:
        x = x_position(frequency_hz)
        parts.extend((
            f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" '
            f'y2="{top + plot_height}" stroke="#f1f5f9"/>',
            f'<text x="{x:.2f}" y="{top + plot_height + 24}" '
            f'text-anchor="middle" font-size="12">'
            f'{frequency_hz / 1_000_000:g}</text>',
        ))
    parts.extend((
        f'<line x1="{left}" y1="{top}" x2="{left}" '
        f'y2="{top + plot_height}" stroke="#334155"/>',
        f'<line x1="{left}" y1="{top + plot_height}" '
        f'x2="{left + plot_width}" y2="{top + plot_height}" '
        'stroke="#334155"/>',
        f'<text x="{left + plot_width / 2}" y="{height - 40}" '
        'text-anchor="middle">Frequency (MHz)</text>',
        f'<text x="22" y="{top + plot_height / 2}" '
        'text-anchor="middle" transform="rotate(-90 22 '
        f'{top + plot_height / 2})">Circular phase spread</text>',
    ))
    for channel in SOURCE_BOARD_NOS:
        channel_items = sorted(
            (item for item in repeatability if item["channel"] == channel),
            key=lambda item: int(item["frequency_hz"]),
        )
        points = " ".join(
            f'{x_position(int(item["frequency_hz"])):.2f},'
            f'{y_position(int(item["phase_spread_mdeg"])):.2f}'
            for item in channel_items
            if item["phase_spread_mdeg"] is not None
        )
        parts.append(
            f'<polyline points="{points}" fill="none" '
            f'stroke="{colors[channel - 1]}" stroke-width="2"/>')
        for item in channel_items:
            if item["phase_spread_mdeg"] is None:
                continue
            x = x_position(int(item["frequency_hz"]))
            y = y_position(int(item["phase_spread_mdeg"]))
            parts.append(
                f'<circle cx="{x:.2f}" cy="{y:.2f}" r="3" '
                f'fill="{colors[channel - 1]}"/>')
        legend_x = left + (channel - 1) * 180
        parts.extend((
            f'<line x1="{legend_x}" y1="{height - 12}" '
            f'x2="{legend_x + 25}" y2="{height - 12}" '
            f'stroke="{colors[channel - 1]}" stroke-width="3"/>',
            f'<text x="{legend_x + 32}" y="{height - 8}" '
            f'font-size="12">NO{channel} → IN{channel}</text>',
        ))
    parts.append('</svg>')
    path.write_text("".join(parts), encoding="utf-8")


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

    expected_valid_count = len(frequencies) * len(SOURCE_BOARD_NOS) * args.repeats
    actual_valid_count = sum(int(item["valid_count"])
                             for item in repeatability)
    spreads = [int(item["phase_spread_mdeg"])
               for item in repeatability
               if item["phase_spread_mdeg"] is not None]
    summary = {
        "expected_channel_measurements": expected_valid_count,
        "valid_channel_measurements": actual_valid_count,
        "all_edges_valid": actual_valid_count == expected_valid_count,
        "minimum_phase_spread_mdeg": min(spreads) if spreads else None,
        "maximum_phase_spread_mdeg": max(spreads) if spreads else None,
        "phase_coherence_passed": False,
        "delay_fit_block_reason": "independent_clock_phase_not_coherent",
    }

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
        "summary": summary,
        "repeatability": repeatability,
        "records": records,
    }
    result_path = args.output_dir / "sma_cable_five_board_diagnostic.json"
    result_path.write_text(
        json.dumps(output, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    write_diagnostic_svg(
        args.output_dir / "sma_cable_five_board_phase_repeatability.svg",
        repeatability,
    )
    print(result_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
