#!/usr/bin/env python3
"""Run the TRN-03B NORMAL-persona short-frame/FIFO closed-loop gate."""

from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
for tool_path in (ROOT / "tools", ROOT / "tools" / "tdma_ring_monitor"):
    if str(tool_path) not in sys.path:
        sys.path.insert(0, str(tool_path))

from tdma_start_ring import (  # noqa: E402
    Board,
    board_command,
    discover,
    train,
    wait_started,
)
from tdma_field_parse import FIELDS as TDMA_FIELDS  # noqa: E402
from flight_bitmap_validate import (  # noqa: E402
    FIFO_FIELDS,
    PROCESS_FIELDS,
)
from tdma_frequency_sweep import PHYS_FIELDS  # noqa: E402
from trn03_stage import load_config, stage_board  # noqa: E402


RUNTIME_FIELDS = (
    "ring_enabled",
    "ring_node_count",
    "ring_local_slot_id",
    "ring_reference_slot_id",
    "ring_up_running",
    "ring_down_running",
    "ring_seq",
    "ring_last_error",
    "ring_adapter_started",
    "ring_adapter_service_count",
    "ring_up_tx_sequence",
    "ring_down_rx_sequence",
    "ring_up_tx_frame_crc32",
    "ring_down_rx_frame_crc32",
    "ring_idle_beacon_tx_count",
    "ring_idle_beacon_rx_count",
    "ring_adapter_last_error",
    "ring_adapter_tx_count",
    "ring_adapter_rx_count",
    "ring_adapter_rx_bad_count",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address in physical node order")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--expected-build")
    parser.add_argument("--level", type=int,
                        help="operating-profile level; defaults to config")
    parser.add_argument("--cycles", type=int, default=4096)
    parser.add_argument("--train-chunk-cycles", type=int, default=0)
    parser.add_argument("--window-s", type=float, default=3.0)
    parser.add_argument("--start-wait", type=float, default=1.0)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def parse_snapshot(raw: str, fields: tuple[str, ...], label: str
                   ) -> dict[str, int]:
    try:
        values = [int(value.strip().strip('"'), 0) for value in raw.split(",")]
    except ValueError as exc:
        raise RuntimeError(f"{label}: non-integer snapshot {raw!r}") from exc
    if len(values) != len(fields):
        raise RuntimeError(
            f"{label}: field count {len(values)}, expected {len(fields)}")
    return dict(zip(fields, values))


def parse_active_profile(raw: str, label: str) -> dict[str, int]:
    fields = ("level", "baud_hz", "cycle_period_ns", "train_cycles",
              "flags", "profile_crc32")
    try:
        values = [int(value.strip().strip('"'), 0) for value in raw.split(",")]
    except ValueError as exc:
        raise RuntimeError(f"{label}: non-integer profile {raw!r}") from exc
    if len(values) < len(fields):
        raise RuntimeError(
            f"{label}: profile field count {len(values)}, expected at least "
            f"{len(fields)}")
    return dict(zip(fields, values[:len(fields)]))


def runtime_snapshot(board: Board, args: argparse.Namespace,
                     node_index: int) -> dict[str, int]:
    raw = parse_snapshot(
        board_command(board, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args),
        tuple(TDMA_FIELDS), board.address)
    selected = {field: raw[field] for field in RUNTIME_FIELDS}
    selected["ring_local_node"] = selected.pop("ring_local_slot_id")
    selected["ring_reference_node"] = selected.pop("ring_reference_slot_id")
    selected["node_index"] = node_index
    return selected


def flight_snapshot(board: Board, args: argparse.Namespace) -> dict[str, Any]:
    process = parse_snapshot(
        board_command(board, "SYSTem:TDMA:FLIGHT:PROCess?", args),
        PROCESS_FIELDS, f"{board.address}:process")
    fifo = parse_snapshot(
        board_command(board, "SYSTem:TDMA:FLIGHT:FIFO?", args),
        FIFO_FIELDS, f"{board.address}:fifo")
    process["local_node"] = process.pop("local_slot")
    fifo["tx_active_buffer"] = fifo.pop("tx_active_slot")
    return {"process": process, "fifo": fifo}


def physical_snapshot(board: Board, args: argparse.Namespace) -> dict[str, int]:
    return parse_snapshot(
        board_command(board, "SYSTem:SYNC:VDC:TDMA:PHYS?", args),
        PHYS_FIELDS, f"{board.address}:physical")


def sample_node(board: Board, args: argparse.Namespace,
                node_index: int) -> dict[str, Any]:
    return {
        "runtime": runtime_snapshot(board, args, node_index),
        "flight": flight_snapshot(board, args),
        "physical": physical_snapshot(board, args),
    }


def sample_all(ordered: list[Board], args: argparse.Namespace
               ) -> dict[str, dict[str, Any]]:
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        futures = {
            board.address: pool.submit(sample_node, board, args, node_index)
            for node_index, board in enumerate(ordered)
        }
        return {address: future.result()
                for address, future in futures.items()}


def u32_delta(before: int, after: int) -> int:
    return (after - before) & 0xFFFFFFFF


def counter_deltas(before: dict[str, int], after: dict[str, int],
                   fields: tuple[str, ...]) -> dict[str, int]:
    return {field: u32_delta(before[field], after[field]) for field in fields}


def validate_node(node_index: int, node_count: int,
                  runtime_before: dict[str, int],
                  runtime_after: dict[str, int],
                  flight_before: dict[str, Any],
                  flight_after: dict[str, Any]) -> tuple[list[str], dict[str, Any]]:
    errors: list[str] = []
    runtime_delta_fields = (
        "ring_seq", "ring_adapter_service_count", "ring_up_tx_sequence",
        "ring_down_rx_sequence", "ring_idle_beacon_tx_count",
        "ring_idle_beacon_rx_count", "ring_adapter_tx_count",
        "ring_adapter_rx_count", "ring_adapter_rx_bad_count",
    )
    runtime_deltas = counter_deltas(
        runtime_before, runtime_after, runtime_delta_fields)
    process_delta_fields = (
        "map_apply_count", "input_bytes", "output_bytes",
        "map_reject_count", "length_reject_count",
        "tx_unavailable_count", "rx_bitmap_scan_count",
        "rx_bitmap_hit_count", "rx_bitmap_duplicate_count",
    )
    fifo_delta_fields = (
        "tx_publish_count", "tx_publish_reject_count", "tx_acquire_count",
        "tx_image_stale_count", "tx_reuse_count", "tx_release_count",
        "rx_publish_count", "rx_mirror_drop_count", "rx_publish_drop_count",
        "rx_acquire_count", "rx_release_count", "rx_parse_count",
    )
    process_deltas = counter_deltas(
        flight_before["process"], flight_after["process"],
        process_delta_fields)
    fifo_deltas = counter_deltas(
        flight_before["fifo"], flight_after["fifo"], fifo_delta_fields)

    checks = (
        (runtime_after["ring_enabled"] == 1, "ring_not_enabled"),
        (runtime_after["ring_adapter_started"] == 1,
         "adapter_not_started"),
        (runtime_after["ring_node_count"] == node_count,
         "node_count_mismatch"),
        (runtime_after["ring_local_node"] == node_index,
         "local_node_mismatch"),
        (runtime_after["ring_reference_node"] == 0,
         "reference_node_mismatch"),
        (runtime_after["ring_up_running"] == 1, "up_not_running"),
        (runtime_after["ring_down_running"] == 1, "down_not_running"),
        (runtime_deltas["ring_seq"] > 0, "ring_sequence_not_growing"),
        (runtime_deltas["ring_adapter_service_count"] > 0,
         "adapter_service_not_growing"),
        (runtime_deltas["ring_up_tx_sequence"] > 0,
         "tx_sequence_not_growing"),
        (runtime_deltas["ring_down_rx_sequence"] > 0,
         "rx_sequence_not_growing"),
        (runtime_deltas["ring_adapter_tx_count"] > 0,
         "adapter_tx_not_growing"),
        (runtime_deltas["ring_adapter_rx_count"] > 0,
         "adapter_rx_not_growing"),
        (runtime_deltas["ring_adapter_rx_bad_count"] == 0,
         "adapter_rx_bad_grew"),
        (runtime_after["ring_up_tx_frame_crc32"] != 0,
         "tx_crc_missing"),
        (runtime_after["ring_down_rx_frame_crc32"] != 0,
         "rx_crc_missing"),
        (runtime_after["ring_up_tx_frame_crc32"] ==
         runtime_after["ring_down_rx_frame_crc32"], "crc_mismatch"),
        (runtime_after["ring_adapter_last_error"] == 0,
         "adapter_error"),
        (flight_after["process"]["configured"] == 1,
         "flight_map_not_configured"),
        (flight_after["process"]["active"] == 1,
         "flight_map_not_active"),
        (flight_after["process"]["local_node"] == node_index,
         "flight_local_node_mismatch"),
        (fifo_deltas["tx_publish_count"] > 0,
         "fifo_tx_not_published"),
        (fifo_deltas["tx_acquire_count"] > 0,
         "fifo_tx_not_acquired"),
        (fifo_deltas["tx_release_count"] > 0,
         "fifo_tx_not_released"),
        (fifo_deltas["rx_publish_count"] > 0,
         "fifo_rx_not_published"),
        (fifo_deltas["rx_parse_count"] > 0,
         "fifo_rx_not_parsed"),
        (fifo_deltas["tx_publish_reject_count"] == 0,
         "fifo_tx_reject_grew"),
        (fifo_deltas["rx_mirror_drop_count"] == 0,
         "fifo_rx_mirror_drop_grew"),
        (fifo_deltas["rx_publish_drop_count"] == 0,
         "fifo_rx_publish_drop_grew"),
        (process_deltas["map_reject_count"] == 0,
         "flight_map_reject_grew"),
        (process_deltas["length_reject_count"] == 0,
         "flight_length_reject_grew"),
    )
    for passed, reason in checks:
        if not passed:
            errors.append(reason)
    if node_index != 0 and process_deltas["map_apply_count"] == 0:
        errors.append("flight_map_not_applied")
    return errors, {
        "runtime": runtime_deltas,
        "process": process_deltas,
        "fifo": fifo_deltas,
    }


def stopped_snapshot(board: Board, args: argparse.Namespace,
                     node_index: int) -> dict[str, int]:
    snapshot = runtime_snapshot(board, args, node_index)
    snapshot["passed"] = int(
        snapshot["ring_enabled"] == 0 and
        snapshot["ring_adapter_started"] == 0)
    return snapshot


def main() -> int:
    args = parse_args()
    raw_config = json.loads(args.config.read_text(encoding="utf-8"))
    config = load_config(args.config)
    board_ids = list(args.board_id)
    if len(board_ids) != config["node_count"] or len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique and match config node_count")
    level = args.level if args.level is not None else raw_config.get("profile_level")
    if not isinstance(level, int):
        raise SystemExit("profile level is required in config or --level")
    if args.cycles <= 0 or args.cycles > 65536 or args.cycles % 8:
        raise SystemExit("cycles must be an 8-cycle multiple in [8, 65536]")
    if args.window_s <= 0 or args.start_wait < 0:
        raise SystemExit("window-s must be positive and start-wait non-negative")
    args.board_ids = board_ids
    plan = {
        "phase": "TRN-03B",
        "board_ids_in_physical_node_order": board_ids,
        "profile_level": level,
        "config": str(args.config),
        "calibration_generation": config["calibration_generation"],
        "cycles": args.cycles,
        "window_s": args.window_s,
    }
    if args.dry_run:
        print(json.dumps(plan, ensure_ascii=False, indent=2))
        return 0

    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    ordered = [boards[board_id] for board_id in board_ids]
    if args.expected_build:
        wrong = {board.address: board.build for board in ordered
                 if board.build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")
    start_order = ordered[1:] + ordered[:1]
    actions: list[dict[str, Any]] = []
    stage_results: list[dict[str, Any]] = []
    nodes: dict[str, Any] = {}
    stopped: dict[str, Any] = {}
    error = ""
    try:
        for board in ordered:
            actions.append({"node": board.address, "action": "STOP",
                            "response": board_command(
                                board, "SYSTem:TDMA:RING:STOP", args)})
        for node_index, board in enumerate(ordered):
            actions.append({"node": board.address, "action": "PROFILE_STAGE",
                            "response": board_command(
                                board, f"SYSTem:TDMA:OPMode:STAGe {level}", args)})
            actions.append({"node": board.address, "action": "PROFILE_APPLY",
                            "response": board_command(
                                board, "SYSTem:TDMA:OPMode:APPLy", args)})
            active = parse_active_profile(
                board_command(board, "SYSTem:TDMA:OPMode?", args),
                f"{board.address}:profile")
            if active["level"] != level or active["profile_crc32"] != config["profile_crc32"]:
                raise RuntimeError(f"{board.address}: profile mismatch {active}")
            actions.append({"node": board.address, "action": "TOPOLOGY",
                            "response": board_command(
                                board,
                                f"SYSTem:TDMA:RING:TOPology {len(ordered)},{node_index},0",
                                args)})
        stage_results = [stage_board(board, config, args) for board in ordered]
        if not all(result["passed"] for result in stage_results):
            raise RuntimeError("matrix write/readback failed")
        for board in start_order:
            actions.append({"node": board.address, "action": "ARM",
                            "response": board_command(
                                board, "SYSTem:TDMA:RING:ARM", args)})
        for board in start_order:
            wait_started(board, args)
        for board in start_order:
            actions.append({"node": board.address, "action": "CLOCK_TRAIN",
                            "response": train(board, args)})
        for node_index, board in enumerate(ordered):
            seed = 0x40 + node_index * 0x10
            actions.append({
                "node": board.address,
                "action": "FIFO_TX",
                "response": board_command(
                    board,
                    f"SYSTem:TDMA:FLIGHT:TX 32,{seed},{config['calibration_generation']},{node_index + 1},1",
                    args),
            })
        for board in start_order:
            actions.append({"node": board.address, "action": "START",
                            "response": board_command(
                                board, "SYSTem:TDMA:RING:START", args)})
        time.sleep(args.start_wait)
        before = sample_all(ordered, args)
        time.sleep(args.window_s)
        after = sample_all(ordered, args)
        for node_index, board in enumerate(ordered):
            node_errors, deltas = validate_node(
                node_index, len(ordered), before[board.address]["runtime"],
                after[board.address]["runtime"],
                before[board.address]["flight"],
                after[board.address]["flight"])
            nodes[board.address] = {
                "node_index": node_index,
                "passed": not node_errors,
                "errors": node_errors,
                "deltas": deltas,
                "runtime_before": before[board.address]["runtime"],
                "runtime_after": after[board.address]["runtime"],
                "flight_before": before[board.address]["flight"],
                "flight_after": after[board.address]["flight"],
                "physical_before": before[board.address]["physical"],
                "physical_after": after[board.address]["physical"],
            }
    except Exception as exc:  # noqa: BLE001 - preserve partial HIL evidence
        error = f"{type(exc).__name__}: {exc}"
    finally:
        for board in ordered:
            try:
                actions.append({"node": board.address, "action": "STOP_FINAL",
                                "response": board_command(
                                    board, "SYSTem:TDMA:RING:STOP", args)})
                stopped[board.address] = stopped_snapshot(
                    board, args, board_ids.index(board.address))
            except Exception as exc:  # noqa: BLE001
                stopped[board.address] = {
                    "passed": 0,
                    "error": f"{type(exc).__name__}: {exc}",
                }
    passed = (
        not error and len(nodes) == len(ordered) and
        all(node["passed"] for node in nodes.values()) and
        all(bool(item.get("passed")) for item in stopped.values())
    )
    result = {
        **plan,
        "passed": passed,
        "error": error,
        "boards": {board.address: asdict(board) for board in ordered},
        "stage_results": stage_results,
        "nodes": nodes,
        "stopped": stopped,
        "actions": actions,
    }
    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn03b_closed_loop_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"passed": passed, "error": error,
                      "out_dir": str(out_dir)}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
