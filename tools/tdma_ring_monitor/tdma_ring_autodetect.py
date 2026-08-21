#!/usr/bin/env python3
"""Detect the physical 2..8-board TDMA ring order by isolated frame probes."""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import (  # noqa: E402
    board_command,
    discover,
    status,
    train,
    wait_started,
)
from tdma_frequency_sweep import snapshot  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address; repeat for 2..8 boards")
    parser.add_argument("--reference-id",
                        help="address used as slot0 when rendering ring order")
    parser.add_argument("--expected-build")
    parser.add_argument("--cycles", type=int, default=512)
    parser.add_argument("--train-chunk-cycles", type=int, default=0,
                        help=("split clock training into bounded chunks; "
                              "0 sends one command"))
    parser.add_argument("--pair-wait", type=float, default=1.5)
    parser.add_argument("--min-rx-frames", type=int, default=10)
    parser.add_argument("--min-rx-words", type=int, default=8)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--verbose", action="store_true",
                        help="print full snapshots; summary.json always keeps them")
    parser.add_argument("--assign-no", action="store_true",
                        help="write NO.1..NO.8 to boards after a valid ring is detected")
    parser.add_argument("--line-only", action="store_true",
                        help=("detect physical adjacency with resident TDMA "
                              "frames only; do not issue clock TRAIN"))
    return parser.parse_args()


def counter_delta(before: int, after: int) -> int:
    return (after - before) & 0xFFFFFFFF


def render_ring_order(adjacency: dict[str, list[str]], reference: str,
                      node_count: int) -> list[str]:
    order = [reference]
    current = reference
    for _ in range(node_count - 1):
        next_nodes = adjacency.get(current, [])
        if len(next_nodes) != 1 or next_nodes[0] in order:
            return []
        current = next_nodes[0]
        order.append(current)
    return order if adjacency.get(current) == [reference] else []


def compact_pair_results(pair_results: list[dict[str, object]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for item in pair_results:
        phys = item.get("receiver_phys", {})
        rows.append({
            "driver": item["driver"],
            "receiver": item["receiver"],
            "detected": item["detected"],
            "rx_frames": item["rx_delta"],
            "rx_words": item["rx_words_delta"],
            "rx_edges": item["rx_edges_delta"],
            "magic_fail": item["magic_fail_delta"],
            "bad_header": [
                phys.get("last_bad_header0", 0),
                phys.get("last_bad_header1", 0),
                phys.get("last_bad_header2", 0),
                phys.get("last_bad_header3", 0),
            ],
        })
    return rows


def main() -> int:
    args = parse_args()
    board_ids = list(args.board_id)
    if len(board_ids) < 2 or len(board_ids) > 8:
        raise SystemExit("board count must be in [2, 8]")
    if len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique")
    if args.reference_id and args.reference_id not in board_ids:
        raise SystemExit("reference-id must be one of the board IDs")
    if args.cycles <= 0 or args.cycles > 65536 or args.cycles % 8:
        raise SystemExit("cycles must be an 8-cycle multiple in [8, 65536]")
    if (args.train_chunk_cycles < 0 or
            (args.train_chunk_cycles != 0 and
             (args.train_chunk_cycles > args.cycles or
              args.train_chunk_cycles % 8 != 0))):
        raise SystemExit(
            "train-chunk-cycles must be 0 or an 8-cycle multiple not greater "
            "than cycles")
    args.board_ids = board_ids

    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    if args.expected_build:
        wrong = {address: boards[address].build for address in board_ids
                 if boards[address].build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")

    pair_results: list[dict[str, object]] = []
    adjacency = {address: [] for address in board_ids}
    try:
        for driver_id in board_ids:
            for receiver_id in board_ids:
                if receiver_id == driver_id:
                    continue
                for address in board_ids:
                    _ = board_command(
                        boards[address], "SYSTem:TDMA:RING:STOP", args)

                driver = boards[driver_id]
                receiver = boards[receiver_id]
                _ = board_command(
                    driver, "SYSTem:TDMA:RING:TOPology 2,0,0", args)
                _ = board_command(
                    receiver, "SYSTem:TDMA:RING:TOPology 2,1,0", args)
                for board in (receiver, driver):
                    _ = board_command(board, "SYSTem:TDMA:RING:ARM", args)
                    _ = wait_started(board, args)
                if not args.line_only:
                    for board in (receiver, driver):
                        _ = train(board, args)

                before = snapshot(receiver, args.timeout)
                _ = board_command(receiver, "SYSTem:TDMA:RING:START", args)
                _ = board_command(driver, "SYSTem:TDMA:RING:START", args)
                time.sleep(args.pair_wait)
                after = snapshot(receiver, args.timeout)
                rx_delta = counter_delta(
                    before["tdma"]["ring_adapter_rx_count"],
                    after["tdma"]["ring_adapter_rx_count"])
                tx_delta = counter_delta(
                    before["tdma"]["ring_adapter_tx_count"],
                    after["tdma"]["ring_adapter_tx_count"])
                rx_words_delta = counter_delta(
                    before["phys"]["rx_dma_produced_words"],
                    after["phys"]["rx_dma_produced_words"])
                rx_edges_delta = counter_delta(
                    before["phys"]["rx_edge_count"],
                    after["phys"]["rx_edge_count"])
                magic_fail_delta = counter_delta(
                    before["phys"]["rx_magic_fail_count"],
                    after["phys"]["rx_magic_fail_count"])
                detected = (
                    rx_delta >= args.min_rx_frames
                    or rx_words_delta >= args.min_rx_words
                    or rx_edges_delta > 0
                )
                if detected:
                    adjacency[driver_id].append(receiver_id)
                pair_results.append({
                    "driver": driver_id,
                    "receiver": receiver_id,
                    "detected": detected,
                    "rx_delta": rx_delta,
                    "tx_delta": tx_delta,
                    "rx_words_delta": rx_words_delta,
                    "rx_edges_delta": rx_edges_delta,
                    "magic_fail_delta": magic_fail_delta,
                    "receiver_status": after["tdma"],
                    "receiver_phys": after["phys"],
                })
    finally:
        for address in board_ids:
            try:
                _ = board_command(
                    boards[address], "SYSTem:TDMA:RING:STOP", args)
            except Exception:  # pragma: no cover - best effort bench cleanup
                pass

    reference = args.reference_id or board_ids[0]
    ring_order = render_ring_order(adjacency, reference, len(board_ids))
    passed = len(ring_order) == len(board_ids)
    assignments: list[dict[str, object]] = []
    if passed and args.assign_no:
        for index, address in enumerate(ring_order):
            response = board_command(
                boards[address], f"SYSTem:BOARD:NO {index + 1}", args)
            assignments.append({"no": index + 1, "address": address,
                                "response": response})
    result = {
        "passed": passed,
        "reference_id": reference,
        "ring_order": ring_order,
        "slot_map": [{"no": index + 1, "address": address}
                     for index, address in enumerate(ring_order)],
        "assignments": assignments,
        "adjacency": adjacency,
        "boards": {address: asdict(boards[address]) for address in board_ids},
        "pair_results": pair_results,
        "line_only": args.line_only,
    }
    out_dir = args.out_dir or (
        ROOT / "build" /
        f"tdma_ring_autodetect_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8")
    console_result = result if args.verbose else {
        "passed": passed,
        "reference_id": reference,
        "ring_order": ring_order,
        "adjacency": adjacency,
        "pair_results": compact_pair_results(pair_results),
    }
    print(json.dumps(console_result, indent=2))
    print(f"out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
