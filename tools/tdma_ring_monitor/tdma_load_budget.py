#!/usr/bin/env python3
"""Calculate cyclic TDMA wire and multi-node forwarding budgets."""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path


DEFAULT_SPI_HZ = (25_000_000,)
DEFAULT_CYCLE_US = (1000.0, 100.0, 10.0)
DEFAULT_NODE_COUNT = (2, 3, 4, 8)
PHYSICAL_HEADER_BYTES = 4
TRANSPORT_HEADER_BYTES = 32
PROCESS_IMAGE_BYTES = 256


@dataclass(frozen=True)
class TdmaLoadBudget:
    spi_hz: int
    cycle_us: float
    node_count: int
    payload_bytes: int
    transport_packet_bytes: int
    wire_bytes: int
    serialization_us: float
    link_utilization_percent: float
    target_utilization_percent: float
    required_spi_hz_at_target: int
    link_budget_pass: bool
    store_forward_round_trip_us: float
    store_forward_pass: bool
    store_forward_per_node_margin_us: float
    cut_through_total_margin_us: float
    cut_through_per_forward_node_budget_us: float


def calculate_budget(
    spi_hz: int,
    cycle_us: float,
    node_count: int,
    *,
    payload_bytes: int = PROCESS_IMAGE_BYTES,
    physical_header_bytes: int = PHYSICAL_HEADER_BYTES,
    transport_header_bytes: int = TRANSPORT_HEADER_BYTES,
    target_utilization: float = 0.8,
) -> TdmaLoadBudget:
    if spi_hz <= 0 or cycle_us <= 0 or not 2 <= node_count <= 8:
        raise ValueError("spi_hz/cycle_us must be positive and node_count must be 2..8")
    if payload_bytes <= 0 or physical_header_bytes < 0 or transport_header_bytes < 0:
        raise ValueError("invalid frame byte count")
    if not 0.0 < target_utilization <= 1.0:
        raise ValueError("target_utilization must be in (0, 1]")

    transport_packet_bytes = transport_header_bytes + payload_bytes
    wire_bytes = physical_header_bytes + transport_packet_bytes
    wire_bits = wire_bytes * 8
    serialization_us = wire_bits * 1_000_000.0 / spi_hz
    utilization = serialization_us / cycle_us
    required_spi_hz = math.ceil(
        wire_bits * 1_000_000.0 / (cycle_us * target_utilization))
    store_forward_us = serialization_us * node_count
    store_forward_margin_us = (cycle_us - store_forward_us) / node_count
    margin_us = cycle_us - serialization_us
    per_node_margin_us = margin_us / (node_count - 1) if margin_us > 0 else margin_us

    return TdmaLoadBudget(
        spi_hz=spi_hz,
        cycle_us=cycle_us,
        node_count=node_count,
        payload_bytes=payload_bytes,
        transport_packet_bytes=transport_packet_bytes,
        wire_bytes=wire_bytes,
        serialization_us=serialization_us,
        link_utilization_percent=utilization * 100.0,
        target_utilization_percent=target_utilization * 100.0,
        required_spi_hz_at_target=required_spi_hz,
        link_budget_pass=utilization <= target_utilization,
        store_forward_round_trip_us=store_forward_us,
        store_forward_pass=store_forward_us <= cycle_us,
        store_forward_per_node_margin_us=store_forward_margin_us,
        cut_through_total_margin_us=margin_us,
        cut_through_per_forward_node_budget_us=per_node_margin_us,
    )


def render_markdown(rows: list[TdmaLoadBudget]) -> str:
    lines = [
        "| SPI | 周期 | 节点 | wire | 串行化 | 链路负载 | 80%门禁 | "
        "store-forward环回 | SF门禁 | SF每节点余量 | cut-through每节点预算 |",
        "|---:|---:|---:|---:|---:|---:|:---:|---:|:---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            f"| {row.spi_hz / 1_000_000:g} MHz | {row.cycle_us:g} us | "
            f"{row.node_count} | {row.wire_bytes} B | "
            f"{row.serialization_us:.3f} us | "
            f"{row.link_utilization_percent:.2f}% | "
            f"{'PASS' if row.link_budget_pass else 'FAIL'} | "
            f"{row.store_forward_round_trip_us:.3f} us | "
            f"{'PASS' if row.store_forward_pass else 'FAIL'} | "
            f"{row.store_forward_per_node_margin_us:.3f} us | "
            f"{row.cut_through_per_forward_node_budget_us:.3f} us |"
        )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spi-hz", action="append", type=int)
    parser.add_argument("--cycle-us", action="append", type=float)
    parser.add_argument("--node-count", action="append", type=int)
    parser.add_argument("--payload-bytes", type=int, default=PROCESS_IMAGE_BYTES)
    parser.add_argument("--target-utilization", type=float, default=0.8)
    parser.add_argument("--format", choices=("markdown", "json"), default="markdown")
    parser.add_argument("--out", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows = [
        calculate_budget(
            spi_hz,
            cycle_us,
            node_count,
            payload_bytes=args.payload_bytes,
            target_utilization=args.target_utilization,
        )
        for spi_hz in (args.spi_hz or DEFAULT_SPI_HZ)
        for cycle_us in (args.cycle_us or DEFAULT_CYCLE_US)
        for node_count in (args.node_count or DEFAULT_NODE_COUNT)
    ]
    if args.format == "json":
        output = json.dumps([asdict(row) for row in rows], indent=2) + "\n"
    else:
        output = render_markdown(rows)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output, encoding="utf-8")
        print(f"out={args.out.resolve()}")
    else:
        print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
