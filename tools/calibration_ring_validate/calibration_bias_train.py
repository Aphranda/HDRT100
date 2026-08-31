#!/usr/bin/env python3
"""Train and collect per-node endpoint-bias snapshots for TRN-03C."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict
from pathlib import Path
from threading import Barrier
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
for tool_path in (ROOT / "tools", ROOT / "tools" / "tdma_ring_monitor",
                  ROOT / "tools" / "calibration_ring_validate"):
    if str(tool_path) not in sys.path:
        sys.path.insert(0, str(tool_path))

from tdma_start_ring import Board, board_command, discover  # noqa: E402
from tdma_field_parse import PHYS_FIELDS  # noqa: E402
from trn03_closed_loop import wait_runtime_stopped  # noqa: E402


BIAS_SET_SCHEMA = "HAOFV_CALIBRATION_BIAS_SET_V1"
BIAS_FIELDS = (
    "valid", "flags", "reject_reason", "generation", "sample_count",
    "accepted_count", "rejected_count", "persona_generation",
    "profile_crc32", "topology_generation", "first_epoch", "last_epoch",
    "mean_bias_ns", "spread_ns", "table_crc32",
)
REQUIRED_BIAS_FLAGS = 0x1F
CALIBRATION_LOAD_BIT = 1 << 2
LOOPBACK_FIELDS = (
    "armed", "complete", "sample_hz", "sample_period_ns",
    "produced_words", "edge_mask", "flags", "reject_reason", "epoch",
    "t1_ns", "t2_ns", "t3_ns", "t4_ns", "result_valid",
    "residence_ns", "raw_path_sum_ns", "delay_estimate_ns",
    "active_eligible",
)


def parse_bias_snapshot(raw: str) -> dict[str, int]:
    row = next(csv.reader([raw]), [])
    if len(row) != len(BIAS_FIELDS):
        raise ValueError(
            f"bias field count {len(row)}, expected {len(BIAS_FIELDS)}")
    values = [int(value.strip().strip('"'), 0) for value in row]
    return dict(zip(BIAS_FIELDS, values))


def parse_loopback_snapshot(raw: str) -> dict[str, int]:
    row = next(csv.reader([raw]), [])
    if len(row) != len(LOOPBACK_FIELDS):
        raise ValueError(
            f"loopback field count {len(row)}, expected {len(LOOPBACK_FIELDS)}")
    values = [int(value.strip().strip('"'), 0) for value in row]
    return dict(zip(LOOPBACK_FIELDS, values))


def bias_snapshot_passed(snapshot: dict[str, int]) -> bool:
    return (
        snapshot["valid"] == 1 and
        (snapshot["flags"] & REQUIRED_BIAS_FLAGS) == REQUIRED_BIAS_FLAGS and
        snapshot["reject_reason"] == 0 and
        snapshot["generation"] > 0 and
        snapshot["sample_count"] > 0 and
        snapshot["accepted_count"] == snapshot["sample_count"] and
        snapshot["rejected_count"] == 0 and
        snapshot["table_crc32"] != 0
    )


def parse_physical_snapshot(raw: str) -> dict[str, int]:
    row = next(csv.reader([raw]), [])
    if len(row) != len(PHYS_FIELDS):
        raise ValueError(
            f"physical field count {len(row)}, expected {len(PHYS_FIELDS)}")
    return dict(zip(PHYS_FIELDS,
                    [int(value.strip().strip('"'), 0) for value in row]))


def wait_calibration_idle(board: Board, args: argparse.Namespace) -> dict[str, int]:
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    while time.monotonic() < deadline:
        loopback = parse_loopback_snapshot(
            board_command(board, "READ:CALibration:LOOPback?", args))
        physical = parse_physical_snapshot(
            board_command(board, "SYSTem:SYNC:VDC:TDMA:PHYS?", args))
        last = {
            "loopback_armed": loopback["armed"],
            "program_persona": physical["program_persona"],
        }
        # A stopped ring intentionally retains its loaded flight persona
        # (origin/follower) for deterministic restart.  Only an unconsumed
        # P3 reference STOP is unsafe here; its Core1 cleanup switches away
        # from persona 15 before another START may be published.
        if (loopback["armed"] == 0 and physical["program_persona"] != 15):
            return last
        time.sleep(args.poll_interval)
    raise RuntimeError(
        f"{board.address}: calibration persona did not become idle: {last}")


def read_load_mask(board: Board, args: argparse.Namespace) -> int:
    raw = board_command(board, "SYSTem:TDMA:LOAD:MASK?", args)
    try:
        return int(raw.strip().strip('"'), 0)
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid realtime load mask {raw!r}") from exc


def set_load_mask(board: Board, expected: int,
                  args: argparse.Namespace) -> None:
    board_command(board, f"SYSTem:TDMA:LOAD:MASK {expected}", args)
    observed = read_load_mask(board, args)
    if observed != expected:
        raise RuntimeError(
            f"{board.address}: realtime load mask {observed}, expected {expected}")


def train_board(board: Board, node: int, expected_path_sum_ns: int,
                start_barrier: Barrier,
                args: argparse.Namespace) -> dict[str, Any]:
    original_load_mask = read_load_mask(board, args)
    training_load_mask = original_load_mask | CALIBRATION_LOAD_BIT
    set_load_mask(board, training_load_mask, args)
    try:
        board_command(board, "SYSTem:TDMA:RING:STOP", args)
        board_command(board, "CALibration:BIAS:STOP", args)
        stopped = wait_runtime_stopped(board, args, node)
        calibration_idle = wait_calibration_idle(board, args)
        baseline = parse_bias_snapshot(
            board_command(board, "READ:CALibration:BIAS?", args))
        start_barrier.wait(timeout=args.arm_wait)
        command = (
            f"CALibration:BIAS:STARt {expected_path_sum_ns},"
            f"{args.minimum_samples},{args.maximum_samples},"
            f"{args.maximum_spread_ns},{args.maximum_clock_error_ns}")
        response = board_command(board, command, args)
        deadline = time.monotonic() + args.training_timeout
        snapshots: list[dict[str, int]] = []
        final = baseline
        while time.monotonic() < deadline:
            final = parse_bias_snapshot(
                board_command(board, "READ:CALibration:BIAS?", args))
            snapshots.append(final)
            if (final["generation"] != 0 and
                    final["generation"] != baseline["generation"] and
                    (final["valid"] != 0 or final["reject_reason"] != 0)):
                break
            time.sleep(args.poll_interval)
        board_command(board, "CALibration:BIAS:STOP", args)
        final = parse_bias_snapshot(
            board_command(board, "READ:CALibration:BIAS?", args))
        last_loopback = parse_loopback_snapshot(
            board_command(board, "READ:CALibration:LOOPback?", args))
        passed = bias_snapshot_passed(final)
        save_response = ""
        if args.save and passed:
            save_response = board_command(board, "CALibration:SAVE", args)
        return {
            "node": node,
            "board_id": board.address,
            "port": board.port,
            "build": board.build,
            "expected_path_sum_ns": expected_path_sum_ns,
            "original_load_mask": original_load_mask,
            "training_load_mask": training_load_mask,
            "stopped_readback": stopped,
            "calibration_idle_readback": calibration_idle,
            "start_response": response,
            "poll_count": len(snapshots),
            "last_loopback": last_loopback,
            **final,
            "passed": passed,
            "save_response": save_response,
        }
    finally:
        try:
            board_command(board, "CALibration:BIAS:STOP", args)
            wait_calibration_idle(board, args)
        finally:
            set_load_mask(board, original_load_mask, args)


def aggregate(nodes: list[dict[str, Any]], board_order: list[str]
              ) -> dict[str, Any]:
    generations = {int(node.get("generation", 0)) for node in nodes}
    failures = [f"node{node.get('node')}:bias"
                for node in nodes if not bool(node.get("passed"))]
    if len(generations) != 1 or 0 in generations:
        failures.append("bias_generation")
    return {
        "schema": BIAS_SET_SCHEMA,
        "phase": "TRN-03C_ENDPOINT_BIAS",
        "passed": not failures,
        "gate_failures": failures,
        "board_ids_in_physical_node_order": board_order,
        "nodes": nodes,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True)
    parser.add_argument("--expected-build")
    parser.add_argument("--expected-path-sum-ns", type=int, action="append",
                        required=True,
                        help="one common value or one value per physical node")
    parser.add_argument("--minimum-samples", type=int, default=3)
    parser.add_argument("--maximum-samples", type=int, default=6)
    parser.add_argument("--maximum-spread-ns", type=int, default=4)
    parser.add_argument("--maximum-clock-error-ns", type=int, default=4)
    parser.add_argument("--training-timeout", type=float, default=10.0)
    parser.add_argument("--poll-interval", type=float, default=0.1)
    parser.add_argument("--arm-wait", type=float, default=5.0)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--action-timeout", type=float, default=0.25,
                        help="bounded wait for action acknowledgements")
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--short-open", action="store_true",
                        help="open/close CDC for every command (diagnostic fallback)")
    parser.add_argument("--save", action="store_true")
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.keep_open = not args.short_open
    board_order = list(args.board_id)
    if not 2 <= len(board_order) <= 8 or len(set(board_order)) != len(board_order):
        raise SystemExit("board IDs must be 2..8 unique physical nodes")
    paths = list(args.expected_path_sum_ns)
    if len(paths) == 1:
        paths *= len(board_order)
    if (len(paths) != len(board_order) or any(value < 0 for value in paths) or
            args.minimum_samples <= 0 or
            args.maximum_samples < args.minimum_samples or
            args.training_timeout <= 0 or args.poll_interval <= 0):
        raise SystemExit("bias parameters are invalid")
    args.board_ids = board_order
    boards = discover(args)
    missing = set(board_order) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    ordered = [boards[address] for address in board_order]
    if args.expected_build:
        wrong = {board.address: board.build for board in ordered
                 if board.build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")
    start_barrier = Barrier(len(ordered))

    def run_node(node: int, board: Board,
                 expected_path: int) -> dict[str, Any]:
        try:
            return train_board(
                board, node, expected_path, start_barrier, args)
        except Exception as exc:  # noqa: BLE001 - retain per-node failure
            failure = {
                "node": node,
                "board_id": board.address,
                "port": board.port,
                "build": board.build,
                "expected_path_sum_ns": expected_path,
                "passed": False,
                "error": f"{type(exc).__name__}: {exc}",
            }
            try:
                board_command(board, "CALibration:BIAS:STOP", args)
            except Exception:  # noqa: BLE001 - best-effort STOP evidence
                pass
            return failure

    # P3 reference is a physical ring exchange: every node must expose its
    # responder before its neighbours can collect all four edges.  Running
    # nodes serially makes missing-edge rejection deterministic.  Parallel
    # board sessions also keep the capture windows overlapping; the bounded
    # maximum sample count absorbs normal UART command-start skew.
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        futures = [
            pool.submit(run_node, node, board, expected_path)
            for node, (board, expected_path) in enumerate(zip(ordered, paths))
        ]
        nodes = [future.result() for future in futures]
    result = aggregate(nodes, board_order)
    result["boards"] = {board.address: asdict(board) for board in ordered}
    result["parameters"] = {
        "minimum_samples": args.minimum_samples,
        "maximum_samples": args.maximum_samples,
        "maximum_spread_ns": args.maximum_spread_ns,
        "maximum_clock_error_ns": args.maximum_clock_error_ns,
        "save": args.save,
    }
    args.out_dir.mkdir(parents=True, exist_ok=True)
    output = args.out_dir / "bias_set.json"
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                      encoding="utf-8")
    print(json.dumps({"passed": result["passed"],
                      "gate_failures": result["gate_failures"],
                      "output": str(output)}, ensure_ascii=False))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
