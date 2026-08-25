#!/usr/bin/env python3
"""Run TRN-03B raw-flight or process-image short-frame closed-loop gates."""

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
for tool_path in (ROOT / "tools", ROOT / "tools" / "tdma_ring_monitor",
                  ROOT / "tools" / "calibration_ring_validate"):
    if str(tool_path) not in sys.path:
        sys.path.insert(0, str(tool_path))

from tdma_start_ring import (  # noqa: E402
    Board,
    board_command,
    discover,
    train,
)
from flight_bitmap_validate import (  # noqa: E402
    FIFO_FIELDS,
    PROCESS_FIELDS,
)
from tdma_frequency_sweep import PHYS_FIELDS  # noqa: E402
from trn03_stage import (  # noqa: E402
    checked_action,
    drain_errors,
    error_is_clear,
    load_config,
    stage_board,
)
from trn03_waveform import (  # noqa: E402
    analyze_capture_set,
    download_ring_capture,
    save_ring_capture,
)


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
    parser.add_argument(
        "--offset-row-id", type=int,
        help="TRN-03 logical offset-matrix row; defaults to active_row_id")
    parser.add_argument("--expected-build")
    parser.add_argument("--level", type=int,
                        help="operating-profile level; defaults to config")
    parser.add_argument("--cycles", type=int, default=4096)
    parser.add_argument("--train-chunk-cycles", type=int, default=0)
    parser.add_argument(
        "--clock-train", action="store_true",
        help=("run the optional coarse CLK burst before cyclic START; "
              "default keeps the already armed flight persona intact"))
    parser.add_argument("--window-s", type=float, default=3.0)
    parser.add_argument("--start-wait", type=float, default=1.0)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--capture-timeout", type=float, default=10.0)
    parser.add_argument("--capture-latch-retries", type=int, default=1)
    parser.add_argument("--waveform-window-ns", type=int, default=1000)
    parser.add_argument(
        "--stage", choices=("raw-flight", "process-image"),
        default="process-image",
        help="raw PIO cut-through proof or final FIFO/process-image gate")
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


def arm_with_evidence(board: Board, args: argparse.Namespace) -> dict[str, Any]:
    """ARM one Node and prove that an ACK timeout did not hide rejection."""
    drained = drain_errors(board, args)
    response = board_command(board, "SYSTem:TDMA:RING:ARM", args)
    status_raw = board_command(
        board, "SYSTem:TDMA:RING:ARM:STATus?", args).strip().strip('"')
    error_after = board_command(board, "SYSTem:ERR?", args)
    try:
        arm_result = int(status_raw, 0)
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid ARM status {status_raw!r}") from exc
    evidence = {
        "node": board.address,
        "action": "ARM",
        "response": response,
        "arm_result": arm_result,
        "errors_drained_before": drained,
        "error_after": error_after,
    }
    if arm_result != 1 or not error_is_clear(error_after):
        raise RuntimeError(
            f"{board.address}: ARM rejected, arm_result={arm_result}, "
            f"error={error_after!r}")
    return evidence


def checked_ring_action(board: Board, action: str, command: str,
                        args: argparse.Namespace) -> dict[str, Any]:
    evidence = checked_action(board, command, args)
    return {"node": board.address, "action": action, **evidence}


def runtime_snapshot(board: Board, args: argparse.Namespace,
                     node_index: int) -> dict[str, int]:
    raw = parse_snapshot(
        board_command(board, "SYSTem:TDMA:RING:STATus?", args),
        RUNTIME_FIELDS, board.address)
    selected = dict(raw)
    selected["ring_local_node"] = selected.pop("ring_local_slot_id")
    selected["ring_reference_node"] = selected.pop("ring_reference_slot_id")
    selected["node_index"] = node_index
    return selected


def wait_runtime_started(board: Board, args: argparse.Namespace,
                         node_index: int) -> dict[str, int]:
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    last_error = ""
    while time.monotonic() < deadline:
        try:
            last = runtime_snapshot(board, args, node_index)
        except (OSError, RuntimeError) as exc:
            last_error = str(exc)
            time.sleep(0.1)
            continue
        if last["ring_enabled"] == 1 and last["ring_adapter_started"] == 1:
            return last
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: ARM timeout, last={last}, last_error={last_error}")


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


def validate_tx_seed(flight_before: dict[str, Any],
                     flight_after: dict[str, Any]
                     ) -> tuple[list[str], dict[str, int]]:
    """Validate the one-shot Core0 TX publication before ring service starts."""
    fields = ("tx_publish_count", "tx_publish_reject_count")
    deltas = counter_deltas(
        flight_before["fifo"], flight_after["fifo"], fields)
    errors: list[str] = []
    if deltas["tx_publish_count"] == 0:
        errors.append("fifo_tx_not_published")
    if deltas["tx_publish_reject_count"] != 0:
        errors.append("fifo_tx_seed_rejected")
    return errors, deltas


def validate_fifo_reset(flight: dict[str, Any]) -> list[str]:
    """Require a fully reclaimed STOPPED-session FIFO before priming."""
    fifo = flight["fifo"]
    checks = (
        (fifo["tx_ready_count"] == 0, "fifo_reset_tx_ready"),
        (fifo["tx_active_buffer"] == 0xFFFFFFFF,
         "fifo_reset_tx_active"),
        (fifo["tx_active_generation"] == 0,
         "fifo_reset_tx_generation"),
        (fifo["rx_queued_count"] == 0, "fifo_reset_rx_queued"),
        (fifo["rx_parse_count"] == 0, "fifo_reset_rx_parse"),
    )
    return [error for passed, error in checks if not passed]


def validate_node(node_index: int, node_count: int,
                   runtime_before: dict[str, int],
                   runtime_after: dict[str, int],
                   flight_before: dict[str, Any],
                   flight_after: dict[str, Any],
                   seed_errors: list[str] | None = None,
                   seed_deltas: dict[str, int] | None = None,
                   *, require_process_image: bool = True,
                   physical_after: dict[str, int] | None = None
                   ) -> tuple[list[str], dict[str, Any]]:
    errors: list[str] = []
    if seed_errors:
        errors.extend(seed_errors)
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
        "rx_acquire_count", "rx_release_count",
    )
    process_deltas = counter_deltas(
        flight_before["process"], flight_after["process"],
        process_delta_fields)
    fifo_deltas = counter_deltas(
        flight_before["fifo"], flight_after["fifo"], fifo_delta_fields)
    feedback_sequence_gap = u32_delta(
        runtime_after["ring_down_rx_sequence"],
        runtime_after["ring_up_tx_sequence"])
    feedback_sequence_ok = (
        feedback_sequence_gap <= 1 if node_index == 0 else
        feedback_sequence_gap == 0)
    feedback_crc_comparable = feedback_sequence_gap == 0
    feedback_crc_match = (
        not feedback_crc_comparable or
        runtime_after["ring_up_tx_frame_crc32"] ==
        runtime_after["ring_down_rx_frame_crc32"])

    checks = [
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
        (feedback_sequence_ok, "feedback_sequence_out_of_window"),
        (feedback_crc_match, "crc_mismatch"),
        (runtime_after["ring_adapter_last_error"] == 0,
         "adapter_error"),
    ]
    if require_process_image:
        checks.extend((
            (flight_after["process"]["configured"] == 1,
             "flight_map_not_configured"),
            (flight_after["process"]["active"] == 1,
             "flight_map_not_active"),
            (flight_after["process"]["local_node"] == node_index,
             "flight_local_node_mismatch"),
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
            (fifo_deltas["tx_acquire_count"] > 0,
             "fifo_tx_not_acquired"),
            (fifo_deltas["rx_publish_count"] > 0,
             "fifo_rx_not_published"),
            (fifo_deltas["rx_acquire_count"] > 0,
             "fifo_rx_not_acquired"),
            (fifo_deltas["rx_release_count"] > 0,
             "fifo_rx_not_released"),
        ))
    if physical_after is not None:
        expected_persona = (
            11 if node_index == 0 else (13 if require_process_image else 12))
        checks.append((
            physical_after.get("program_persona") == expected_persona,
            "physical_flight_persona_mismatch"))
        if require_process_image and node_index != 0:
            checks.extend((
                (physical_after.get("overlay_prepare_count", 0) > 0,
                 "physical_overlay_not_prepared"),
                (physical_after.get("overlay_prepare_fail_count", 0) == 0,
                 "physical_overlay_prepare_failed"),
                (physical_after.get("overlay_replacement_byte_count", 0) > 0,
                 "physical_overlay_not_replaced"),
            ))
    for passed, reason in checks:
        if not passed:
            errors.append(reason)
    if (require_process_image and node_index != 0 and
            process_deltas["map_apply_count"] == 0):
        errors.append("flight_map_not_applied")
    return errors, {
        "runtime": runtime_deltas,
        "process": process_deltas,
        "fifo": fifo_deltas,
        "feedback_identity": {
            "tx_sequence": runtime_after["ring_up_tx_sequence"],
            "rx_sequence": runtime_after["ring_down_rx_sequence"],
            "sequence_gap": feedback_sequence_gap,
            "crc_comparable": feedback_crc_comparable,
            "crc_match": feedback_crc_match,
        },
        "fifo_seed": seed_deltas or {},
        "fifo_gauges": {
            "before": {
                "tx_ready_count": flight_before["fifo"]["tx_ready_count"],
                "tx_active_buffer": flight_before["fifo"]["tx_active_buffer"],
                "tx_active_generation":
                    flight_before["fifo"]["tx_active_generation"],
                "rx_queued_count": flight_before["fifo"]["rx_queued_count"],
                "rx_parse_count": flight_before["fifo"]["rx_parse_count"],
            },
            "after": {
                "tx_ready_count": flight_after["fifo"]["tx_ready_count"],
                "tx_active_buffer": flight_after["fifo"]["tx_active_buffer"],
                "tx_active_generation":
                    flight_after["fifo"]["tx_active_generation"],
                "rx_queued_count": flight_after["fifo"]["rx_queued_count"],
                "rx_parse_count": flight_after["fifo"]["rx_parse_count"],
            },
        },
    }


def stopped_snapshot(board: Board, args: argparse.Namespace,
                     node_index: int) -> dict[str, int]:
    snapshot = runtime_snapshot(board, args, node_index)
    snapshot["passed"] = int(
        snapshot["ring_enabled"] == 0 and
        snapshot["ring_adapter_started"] == 0)
    return snapshot


def capture_ring_waveforms(
        ordered: list[Board], args: argparse.Namespace, *,
        calibration_generation: int, capture_epoch: int,
        out_dir: Path) -> dict[str, Any]:
    capture_dir = out_dir / "captures"
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        saved = list(pool.map(
            lambda board: save_ring_capture(
                board, args,
                calibration_generation=calibration_generation,
                capture_epoch=capture_epoch),
            ordered))
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        futures = [
            pool.submit(
                download_ring_capture, board, capture_file, args,
                capture_dir / f"node{node_index}_ring_capture.json")
            for node_index, (board, capture_file) in
            enumerate(zip(ordered, saved))
        ]
        downloaded = [future.result() for future in futures]
    return {
        "capture_epoch": capture_epoch,
        "capture_completed": True,
        "saved": saved,
        "downloaded": [
            {key: value for key, value in row.items() if key != "capture"}
            for row in downloaded],
    }


def analyze_ring_waveforms(capture: dict[str, Any], config: dict[str, Any],
                           args: argparse.Namespace, out_dir: Path
                           ) -> dict[str, Any]:
    """Analyze only after every node's raw capture has been downloaded."""
    downloaded = capture.get("downloaded", [])
    if not capture.get("capture_completed") or not isinstance(downloaded, list):
        raise ValueError("raw ring capture is incomplete")
    return analyze_capture_set(
        config,
        [Path(str(row["local_path"])) for row in downloaded],
        out_dir / "analysis", args.waveform_window_ns)


def main() -> int:
    args = parse_args()
    raw_config = json.loads(args.config.read_text(encoding="utf-8"))
    config = load_config(args.config, args.offset_row_id)
    board_ids = list(args.board_id)
    if len(board_ids) != config["node_count"] or len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique and match config node_count")
    level = args.level if args.level is not None else raw_config.get("profile_level")
    if not isinstance(level, int):
        raise SystemExit("profile level is required in config or --level")
    if args.cycles <= 0 or args.cycles > 65536 or args.cycles % 8:
        raise SystemExit("cycles must be an 8-cycle multiple in [8, 65536]")
    if (args.window_s <= 0 or args.start_wait < 0 or
            args.capture_timeout <= 0 or args.capture_latch_retries < 0 or
            args.waveform_window_ns <= 0):
        raise SystemExit("window-s must be positive and start-wait non-negative")
    args.board_ids = board_ids
    plan = {
        "phase": "TRN-03B",
        "board_ids_in_physical_node_order": board_ids,
        "profile_level": level,
        "config": str(args.config),
        "calibration_generation": config["calibration_generation"],
        "offset_row_id": config["offset_row_id"],
        "offset_row": config["offset_row"],
        "cycles": args.cycles,
        "clock_train": args.clock_train,
        "window_s": args.window_s,
        "waveform_window_ns": args.waveform_window_ns,
        "stage": args.stage,
    }
    if args.dry_run:
        print(json.dumps(plan, ensure_ascii=False, indent=2))
        return 0

    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn03b_closed_loop_{datetime.now().strftime('%Y%m%d_%H%M%S')}")

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
    fifo_reset: dict[str, Any] = {}
    fifo_seed: dict[str, Any] = {}
    stopped: dict[str, Any] = {}
    ring_capture: dict[str, Any] = {}
    ring_analysis: dict[str, Any] = {}
    capture_error = ""
    analysis_error = ""
    capture_attempted = False
    error = ""
    try:
        for board in ordered:
            actions.append({"node": board.address, "action": "STOP",
                            "response": board_command(
                                board, "SYSTem:TDMA:RING:STOP", args)})
        expected_mode = 2 if args.stage == "process-image" else 1
        for board in ordered:
            response = board_command(
                board,
                f"SYSTem:TDMA:FLIGHT:MODE {1 if args.stage == 'process-image' else 0}",
                args)
            mode_raw = board_command(
                board, "SYSTem:TDMA:FLIGHT:MODE?", args).strip().strip('"')
            try:
                mode = int(mode_raw, 0)
            except ValueError as exc:
                raise RuntimeError(
                    f"{board.address}: invalid flight mode {mode_raw!r}") from exc
            actions.append({
                "node": board.address,
                "action": "FLIGHT_MODE",
                "response": response,
                "mode": mode,
            })
            if mode != expected_mode:
                raise RuntimeError(
                    f"{board.address}: flight mode {mode}, expected {expected_mode}")
        if args.stage == "process-image":
            for board in ordered:
                response = board_command(
                    board, "SYSTem:TDMA:FLIGHT:FIFO:RESet", args)
                readback = flight_snapshot(board, args)
                reset_errors = validate_fifo_reset(readback)
                fifo_reset[board.address] = {
                    "passed": not reset_errors,
                    "errors": reset_errors,
                    "readback": readback,
                }
                actions.append({
                    "node": board.address,
                    "action": "FIFO_RESET",
                    "response": response,
                    "errors": reset_errors,
                })
            if not all(item["passed"] for item in fifo_reset.values()):
                raise RuntimeError("FIFO STOPPED-session reset failed")
        for node_index, board in enumerate(ordered):
            actions.append(checked_ring_action(
                board, "PROFILE_STAGE",
                f"SYSTem:TDMA:OPMode:STAGe {level}", args))
            actions.append(checked_ring_action(
                board, "PROFILE_APPLY", "SYSTem:TDMA:OPMode:APPLy", args))
            active = parse_active_profile(
                board_command(board, "SYSTem:TDMA:OPMode?", args),
                f"{board.address}:profile")
            if active["level"] != level or active["profile_crc32"] != config["profile_crc32"]:
                raise RuntimeError(f"{board.address}: profile mismatch {active}")
            actions.append(checked_ring_action(
                board, "TOPOLOGY",
                f"SYSTem:TDMA:RING:TOPology {len(ordered)},{node_index},0",
                args))
        stage_results = [stage_board(board, config, args) for board in ordered]
        if not all(result["passed"] for result in stage_results):
            raise RuntimeError("matrix write/readback failed")
        for node_index, board in enumerate(ordered):
            if args.stage == "process-image":
                seed = 0x40 + node_index * 0x10
                seed_before = flight_snapshot(board, args)
                actions.append({
                    "node": board.address,
                    "action": "FIFO_TX",
                    "response": board_command(
                        board,
                        f"SYSTem:TDMA:FLIGHT:TX 32,{seed},{config['calibration_generation']},{node_index + 1},1",
                        args),
                })
                seed_after = flight_snapshot(board, args)
                seed_errors, seed_deltas = validate_tx_seed(
                    seed_before, seed_after)
                fifo_seed[board.address] = {
                    "passed": not seed_errors,
                    "errors": seed_errors,
                    "deltas": seed_deltas,
                    "before": seed_before,
                    "after": seed_after,
                }
            else:
                fifo_seed[board.address] = {
                    "passed": True, "errors": [], "deltas": {}}
        for board in start_order:
            actions.append(arm_with_evidence(board, args))
        for board in start_order:
            wait_runtime_started(board, args, board_ids.index(board.address))
        if args.clock_train:
            for board in start_order:
                actions.append({
                    "node": board.address,
                    "action": "CLOCK_TRAIN",
                    "response": train(board, args),
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
                after[board.address]["flight"],
                fifo_seed[board.address]["errors"],
                fifo_seed[board.address]["deltas"],
                require_process_image=args.stage == "process-image",
                physical_after=after[board.address]["physical"])
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
        capture_attempted = True
        try:
            ring_capture = capture_ring_waveforms(
                ordered, args,
                calibration_generation=config["calibration_generation"],
                capture_epoch=int(time.time()) & 0xFFFFFFFF,
                out_dir=out_dir)
        except Exception as exc:  # noqa: BLE001 - retain gate evidence
            capture_error = f"{type(exc).__name__}: {exc}"
        if ring_capture.get("capture_completed"):
            try:
                ring_analysis = analyze_ring_waveforms(
                    ring_capture, raw_config, args, out_dir)
            except Exception as exc:  # noqa: BLE001 - capture stays valid
                analysis_error = f"{type(exc).__name__}: {exc}"
    except Exception as exc:  # noqa: BLE001 - preserve partial HIL evidence
        error = f"{type(exc).__name__}: {exc}"
    finally:
        if not capture_attempted:
            try:
                ring_capture = capture_ring_waveforms(
                    ordered, args,
                    calibration_generation=config["calibration_generation"],
                    capture_epoch=int(time.time()) & 0xFFFFFFFF,
                    out_dir=out_dir)
            except Exception as exc:  # noqa: BLE001 - STOP still mandatory
                capture_error = f"{type(exc).__name__}: {exc}"
            if ring_capture.get("capture_completed"):
                try:
                    ring_analysis = analyze_ring_waveforms(
                        ring_capture, raw_config, args, out_dir)
                except Exception as exc:  # noqa: BLE001 - capture stays valid
                    analysis_error = f"{type(exc).__name__}: {exc}"
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
        not error and not capture_error and len(nodes) == len(ordered) and
        all(node["passed"] for node in nodes.values()) and
        all(bool(item.get("passed")) for item in stopped.values())
    )
    result = {
        **plan,
        "passed": passed,
        "error": error,
        "boards": {board.address: asdict(board) for board in ordered},
        "stage_results": stage_results,
        "fifo_reset": fifo_reset,
        "fifo_seed": fifo_seed,
        "nodes": nodes,
        "ring_capture": ring_capture,
        "ring_capture_error": capture_error,
        "ring_analysis": ring_analysis,
        "ring_analysis_error": analysis_error,
        "stopped": stopped,
        "actions": actions,
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"passed": passed, "error": error,
                      "out_dir": str(out_dir)}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
