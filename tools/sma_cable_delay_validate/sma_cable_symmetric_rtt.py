#!/usr/bin/env python3
"""Discover five-board bidirectional SMA wiring and measure symmetric RTT."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
from dataclasses import asdict, dataclass
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
from sma_cable_wire_order import (  # noqa: E402
    command_ack_optional,
    query,
    query_ints,
)
from tdma_start_ring import discover  # noqa: E402

NODE_NOS = (1, 2, 3, 4)
VALIDATOR_NO = 5
CHANNELS = (1, 2, 3, 4)
RTT_FIELD_COUNT = 12
RESPONDER_FIELD_COUNT = 4


@dataclass(frozen=True)
class Route:
    node_no: int
    node_output_channel: int
    validator_input_channel: int
    validator_output_channel: int
    node_input_channel: int


def _drive(ser, mask: int, timeout: float) -> None:
    command_ack_optional(ser, f"REALtime:IO:OUTPut:MASK {mask}", timeout)


def _input_mask(ser, timeout: float) -> int:
    return query_ints(ser, "REALtime:IO:INPut:LEVel?", 3, timeout)[2] & 0xF


def _detected_channels(high_mask: int, low_mask: int) -> list[int]:
    changed = high_mask & ~low_mask & 0xF
    return [channel for channel in CHANNELS
            if changed & (1 << (channel - 1))]


def scan_direction(source, target, timeout: float,
                   settle: float) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for output_channel in CHANNELS:
        _drive(source, 1 << (output_channel - 1), timeout)
        time.sleep(settle)
        high_mask = _input_mask(target, timeout)
        _drive(source, 0, timeout)
        time.sleep(settle)
        low_mask = _input_mask(target, timeout)
        records.append({
            "output_channel": output_channel,
            "high_input_mask": high_mask,
            "low_input_mask": low_mask,
            "detected_input_channels": _detected_channels(high_mask, low_mask),
        })
    return records


def infer_routes(scans: dict[int, dict[str, list[dict[str, object]]]]) \
        -> tuple[list[Route], bool]:
    routes: list[Route] = []
    for node_no in NODE_NOS:
        forward = [
            (int(record["output_channel"]), int(inputs[0]))
            for record in scans[node_no]["node_to_validator"]
            if len(inputs := record["detected_input_channels"]) == 1
        ]
        reverse = [
            (int(record["output_channel"]), int(inputs[0]))
            for record in scans[node_no]["validator_to_node"]
            if len(inputs := record["detected_input_channels"]) == 1
        ]
        if len(forward) != 1 or len(reverse) != 1:
            continue
        routes.append(Route(
            node_no=node_no,
            node_output_channel=forward[0][0],
            validator_input_channel=forward[0][1],
            validator_output_channel=reverse[0][0],
            node_input_channel=reverse[0][1],
        ))
    passed = (
        [route.node_no for route in routes] == list(NODE_NOS) and
        sorted(route.validator_input_channel for route in routes) ==
        list(CHANNELS) and
        sorted(route.validator_output_channel for route in routes) ==
        list(CHANNELS)
    )
    return routes, passed


def parse_rtt_response(values: list[int]) -> dict[str, int | bool]:
    if len(values) != RTT_FIELD_COUNT:
        raise ValueError(f"expected {RTT_FIELD_COUNT} RTT fields, got {len(values)}")
    path_sum_ps = values[7] | (values[8] << 32)
    mean_leg_delay_ps = values[9] | (values[10] << 32)
    return {
        "initiator_output_channel": values[0],
        "initiator_input_channel": values[1],
        "sample_rate_hz": values[2],
        "sample_period_ps": values[3],
        "response_sample_index": values[4],
        "raw_round_trip_cycles": values[5],
        "responder_turnaround_cycles": values[6],
        "path_sum_ps": path_sum_ps,
        "mean_leg_delay_ps": mean_leg_delay_ps,
        "valid": values[11] != 0,
    }


def summarize_records(records: list[dict[str, object]],
                      repeats: int) -> dict[str, object]:
    nodes = []
    for node_no in NODE_NOS:
        node_records = [record for record in records
                        if record["node_no"] == node_no and record["valid"]]
        delays = [int(record["mean_leg_delay_ps"]) for record in node_records]
        histogram: dict[str, int] = {}
        for delay in delays:
            key = str(delay)
            histogram[key] = histogram.get(key, 0) + 1
        nodes.append({
            "node_no": node_no,
            "valid_count": len(delays),
            "expected_count": repeats,
            "minimum_mean_leg_delay_ps": min(delays) if delays else None,
            "median_mean_leg_delay_ps": (
                int(statistics.median(delays)) if delays else None),
            "maximum_mean_leg_delay_ps": max(delays) if delays else None,
            "quantized_delay_histogram": histogram,
        })
    return {
        "passed": all(item["valid_count"] == repeats for item in nodes),
        "nodes": nodes,
    }


def write_svg(path: Path, summary: dict[str, object]) -> None:
    width, height = 760, 390
    left, top, plot_width, plot_height = 90, 60, 600, 250
    values = [int(item["median_mean_leg_delay_ps"] or 0)
              for item in summary["nodes"]]
    maximum = max(values + [1])
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#fff"/>',
        '<text x="380" y="30" text-anchor="middle" font-size="20">'
        'SMA symmetric two-way mean leg delay</text>',
        f'<line x1="{left}" y1="{top + plot_height}" x2="{left + plot_width}" '
        f'y2="{top + plot_height}" stroke="#334155"/>',
    ]
    bar_width = plot_width / 8
    for index, value in enumerate(values):
        x = left + (index * 2 + 0.5) * bar_width
        bar_height = value * (plot_height - 20) / maximum
        y = top + plot_height - bar_height
        parts.extend((
            f'<rect x="{x}" y="{y}" width="{bar_width}" height="{bar_height}" '
            'fill="#2563eb"/>',
            f'<text x="{x + bar_width / 2}" y="{top + plot_height + 24}" '
            f'text-anchor="middle">NO{index + 1}</text>',
            f'<text x="{x + bar_width / 2}" y="{max(48, y - 8)}" '
            f'text-anchor="middle" font-size="12">{value / 1000:.3f} ns</text>',
        ))
    parts.extend((
        '<text x="380" y="370" text-anchor="middle" font-size="12">'
        'Includes endpoint driver/receiver delays; cable-only delay is not yet valid</text>',
        '</svg>',
    ))
    path.write_text("".join(parts), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True)
    parser.add_argument("--repeats", type=int, default=64)
    parser.add_argument("--capture-words", type=int, default=16)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.3)
    parser.add_argument("--line-settle", type=float, default=0.05)
    parser.add_argument("--arm-settle", type=float, default=0.02)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if len(args.board_id) != 5 or len(set(args.board_id)) != 5:
        parser.error("exactly five unique --board-id values are required")
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    if not 16 <= args.capture_words <= 512:
        parser.error("--capture-words must be within 16..512")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    discovery_args = SimpleNamespace(
        board_ids=list(args.board_id), baud=args.baud, timeout=args.timeout,
        settle=args.settle, expected_build=None, short_open=False)
    boards = discover(discovery_args)
    missing = set(args.board_id) - set(boards)
    if missing:
        raise RuntimeError(f"boards not discovered: {sorted(missing)}")

    identities: dict[int, dict[str, str]] = {}
    scans: dict[int, dict[str, list[dict[str, object]]]] = {}
    records: list[dict[str, object]] = []
    with ExitStack() as stack:
        connections = {}
        for board in boards.values():
            ser = stack.enter_context(open_serial_port(
                board.port, args.baud, args.timeout, args.settle))
            board_no = int(query(ser, "SYST:BOARD:NO?", args.timeout).strip('"'), 0)
            connections[board_no] = ser
            identities[board_no] = {
                "address": board.address, "port": board.port, "build": board.build}
        if sorted(connections) != [1, 2, 3, 4, 5]:
            raise RuntimeError(f"BOARD:NO values must be [1,2,3,4,5], got {sorted(connections)}")

        for ser in connections.values():
            command_ack_optional(ser, "REALtime:IO:OUTPut:RELease", args.timeout)
        validator = connections[VALIDATOR_NO]
        for node_no in NODE_NOS:
            scans[node_no] = {
                "node_to_validator": scan_direction(
                    connections[node_no], validator, args.timeout, args.line_settle),
                "validator_to_node": scan_direction(
                    validator, connections[node_no], args.timeout, args.line_settle),
            }
        routes, topology_passed = infer_routes(scans)
        if not topology_passed:
            raise RuntimeError(f"bidirectional wire-order preflight failed: {routes}")

        with ThreadPoolExecutor(max_workers=1) as executor:
            for repeat in range(args.repeats):
                for route in routes:
                    response_future = executor.submit(
                        query_ints,
                        validator,
                        "READ:CAL:SMA:CABL:RTT:RESP? "
                        f"{route.validator_input_channel},"
                        f"{route.validator_output_channel}",
                        RESPONDER_FIELD_COUNT,
                        args.timeout,
                    )
                    time.sleep(args.arm_settle)
                    values = query_ints(
                        connections[route.node_no],
                        "READ:CAL:SMA:CABL:RTT? "
                        f"{route.node_output_channel},{route.node_input_channel},"
                        f"{args.capture_words}",
                        RTT_FIELD_COUNT,
                        args.timeout,
                    )
                    responder = response_future.result(timeout=args.timeout + 1.0)
                    parsed = parse_rtt_response(values)
                    parsed.update({
                        "repeat": repeat,
                        "node_no": route.node_no,
                        "responder_input_channel": responder[0],
                        "responder_output_channel": responder[1],
                    })
                    records.append(parsed)

    summary = summarize_records(records, args.repeats)
    result = {
        "schema": "sma-cable-delay/symmetric-rtt-v1",
        "passed": bool(topology_passed and summary["passed"]),
        "measurement_boundary": (
            "node driver + forward SMA path + NO5 receiver + fixed PIO turnaround + "
            "NO5 driver + reverse SMA path + node receiver"),
        "reciprocal_path_assumption": True,
        "cable_only_delay_valid": False,
        "identities": identities,
        "wire_order": {
            "passed": topology_passed,
            "routes": [asdict(route) for route in routes],
            "full_scans": scans,
        },
        "summary": summary,
        "records": records,
    }
    result_path = args.output_dir / "sma_cable_symmetric_rtt.json"
    result_path.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    write_svg(args.output_dir / "sma_cable_symmetric_rtt.svg", summary)
    print(result_path)
    for item in summary["nodes"]:
        print(
            f"NO{item['node_no']}: valid={item['valid_count']}/{item['expected_count']} "
            f"median={item['median_mean_leg_delay_ps']} ps "
            f"range={item['minimum_mean_leg_delay_ps']}.."
            f"{item['maximum_mean_leg_delay_ps']} ps")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
