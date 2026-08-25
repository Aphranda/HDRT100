#!/usr/bin/env python3
"""Validate TRN-01 marker capture/cut-through evidence exported by firmware.

This tool deliberately does not synthesize edge timestamps.  It accepts only
firmware status rows whose capture/forward ticks are hardware-latched, checks
the common epoch/generation bundle, and verifies the directed ring node order.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import statistics
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
    Board,
    board_command,
    discover,
)


MARKER_FIELDS = (
    "tag", "version", "state", "reject_reason", "flags", "role",
    "board_id_lo", "board_id_hi", "build_id_lo", "build_id_hi",
    "local_node", "reference_node", "predecessor_node", "successor_node",
    "train_epoch", "train_sequence", "marker_id", "marker_codebook_id",
    "marker_crc32", "observed_crc32", "polarity",
    "marker_flags", "correlation_reject_reason", "best_lag_sample",
    "best_distance",
    "calibration_generation", "topology_generation", "topology_crc32",
    "profile_crc32", "schedule_crc32", "tick_resolution_ns",
    "offset_sample_count",
    "marker_capture_tick_lo", "marker_capture_tick_hi",
    "marker_forward_tick_lo", "marker_forward_tick_hi",
    "marker_return_tick_lo", "marker_return_tick_hi",
    "forward_residence_ticks_lo", "forward_residence_ticks_hi",
    "loop_rtt_ticks_lo", "loop_rtt_ticks_hi",
    "dma_capture_count", "dma_overrun_count", "pio_stall_count",
    "timeout_count",
)

STATE_ACCEPTED = 3
STATE_PREPARED = 1
ROLE_ORIGINATOR = 1
ROLE_FOLLOWER = 2
FLAG_DIAGNOSTIC_ONLY = 1 << 0
FLAG_HARDWARE_LATCHED = 1 << 1
FLAG_CAPTURE_VALID = 1 << 2
FLAG_FORWARD_VALID = 1 << 3
FLAG_CRC_VALID = 1 << 4
FLAG_EPOCH_VALID = 1 << 5
FLAG_SEQUENCE_VALID = 1 << 6
FLAG_POLARITY_VALID = 1 << 7
FLAG_DMA_COMPLETE = 1 << 8
REQUIRED_FLAGS = (
    FLAG_HARDWARE_LATCHED | FLAG_CAPTURE_VALID | FLAG_FORWARD_VALID |
    FLAG_CRC_VALID | FLAG_EPOCH_VALID | FLAG_SEQUENCE_VALID |
    FLAG_POLARITY_VALID | FLAG_DMA_COMPLETE
)


def order_boards_by_board_no(
        boards: dict[str, Board], board_ids: list[str],
        args: argparse.Namespace) -> list[Board]:
    """Resolve physical loop order from persistent NO.1..NO.8 identity."""
    numbered: list[tuple[int, Board]] = []
    for address in board_ids:
        board = boards[address]
        raw = board_command(board, "SYSTem:BOARD:NO?", args)
        try:
            board_no = int(raw.strip().strip('"'), 0)
        except ValueError as exc:
            raise RuntimeError(
                f"{board.address}: invalid BOARD:NO response {raw!r}") from exc
        numbered.append((board_no, board))
    expected = list(range(1, len(board_ids) + 1))
    observed = sorted(board_no for board_no, _ in numbered)
    if observed != expected:
        raise RuntimeError(
            "BOARD:NO values must be unique and contiguous from 1 through "
            f"{len(board_ids)}; observed={observed}")
    return [board for _, board in sorted(numbered, key=lambda item: item[0])]
HALF_CHIP_NS_BY_CODEBOOK = {0: 20, 1: 40, 2: 24, 3: 32}
SAMPLE_PERIOD_NS = 4
MIN_OFFSET_SAMPLES = -10
MAX_OFFSET_SAMPLES = 10
MAX_CAPTURE_DELAY_CYCLES = 31
DEFAULT_MATRIX_OFFSET_VALUES = tuple(
    range(MIN_OFFSET_SAMPLES, MAX_OFFSET_SAMPLES + 1))
CORRELATION_REJECT_NAMES = {
    0: "NONE", 1: "BAD_ARGUMENT", 2: "SEARCH_RANGE",
    3: "CAPTURE_TRUNCATED", 4: "POLARITY", 5: "SOF",
    6: "MANCHESTER", 7: "HEADER_INVERSE", 8: "HEADER_CRC",
    9: "HEADER_MISMATCH", 10: "EOF", 11: "DISTANCE", 12: "MARGIN",
}


def capture_phase_delay_cycles(codebook: int, offset_samples: int) -> int:
    if codebook not in HALF_CHIP_NS_BY_CODEBOOK:
        raise ValueError("codebook must be 0..3")
    if not MIN_OFFSET_SAMPLES <= offset_samples <= MAX_OFFSET_SAMPLES:
        raise ValueError(
            f"offset_sample_count is outside [{MIN_OFFSET_SAMPLES}, "
            f"{MAX_OFFSET_SAMPLES}]")
    delay = HALF_CHIP_NS_BY_CODEBOOK[codebook] // SAMPLE_PERIOD_NS + offset_samples
    if not 0 <= delay <= MAX_CAPTURE_DELAY_CYCLES:
        raise ValueError(
            f"codebook {codebook} with offset {offset_samples} requires "
            f"capture delay {delay}, outside PIO delay [0, "
            f"{MAX_CAPTURE_DELAY_CYCLES}]")
    return delay


def derive_link_offset_candidates(
        records: list[dict[str, int | str]]) -> list[dict[str, object]]:
    """Project accepted follower correlation onto base + per-link offset."""
    candidates: list[dict[str, object]] = []
    for record in records:
        if int(record["role"]) != ROLE_FOLLOWER:
            continue
        codebook = int(record["marker_codebook_id"])
        tick_ns = int(record["tick_resolution_ns"])
        accepted = (
            int(record["state"]) == STATE_ACCEPTED and
            int(record["correlation_reject_reason"]) == 0 and
            int(record["marker_flags"]) == 0x3F and
            tick_ns > 0 and codebook in HALF_CHIP_NS_BY_CODEBOOK
        )
        applied_offset_samples = int(record.get("offset_sample_count", 0))
        offset_samples = applied_offset_samples if accepted else None
        offset_ns = offset_samples * tick_ns if offset_samples is not None else None
        base_ns = HALF_CHIP_NS_BY_CODEBOOK.get(codebook)
        reject_reason = int(record["correlation_reject_reason"])
        candidates.append({
            "source_node": int(record["predecessor_node"]),
            "destination_node": int(record["local_node"]),
            "base_half_chip_ns": base_ns,
            "offset_sample_count": offset_samples,
            "offset_ns": offset_ns,
            "sample_anchor_after_marker_ns": (
                base_ns + offset_ns
                if base_ns is not None and offset_ns is not None else None),
            "tick_resolution_ns": tick_ns,
            "correlation_accepted": accepted,
            "observed_best_lag_sample": int(record["best_lag_sample"]),
            "applied_offset_sample_count": applied_offset_samples,
            "observed_best_distance": int(record["best_distance"]),
            "observed_polarity": int(record["polarity"]),
            "marker_flags": int(record["marker_flags"]),
            "correlation_reject_reason": reject_reason,
            "correlation_reject_name": CORRELATION_REJECT_NAMES.get(
                reject_reason, f"UNKNOWN_{reject_reason}"),
            "training_state": int(record["state"]),
            "destination_build_id": int(record["build_id"]),
            "calibration_generation": int(record["calibration_generation"]),
            "topology_generation": int(record["topology_generation"]),
            "topology_crc32": int(record["topology_crc32"]),
            "profile_crc32": int(record["profile_crc32"]),
            "schedule_crc32": int(record["schedule_crc32"]),
            "diagnostic_only": True,
        })
    return candidates


def _u64(row: dict[str, int | str], low: str, high: str) -> int:
    return int(row[low]) | (int(row[high]) << 32)


def parse_marker_status(raw: str) -> dict[str, int | str]:
    values = next(csv.reader([raw]), [])
    if len(values) != len(MARKER_FIELDS):
        raise ValueError(
            f"TRN-01 field count {len(values)}, expected {len(MARKER_FIELDS)}")
    result: dict[str, int | str] = {
        "tag": values[0].strip().strip('"')
    }
    if result["tag"] != "MARKERTRN":
        raise ValueError(f"invalid TRN-01 tag {result['tag']!r}")
    for field, value in zip(MARKER_FIELDS[1:], values[1:]):
        result[field] = int(value.strip().strip('"'), 0)
    result["board_unique_id"] = _u64(
        result, "board_id_lo", "board_id_hi")
    result["build_id"] = _u64(result, "build_id_lo", "build_id_hi")
    result["marker_capture_tick"] = _u64(
        result, "marker_capture_tick_lo", "marker_capture_tick_hi")
    result["marker_forward_tick"] = _u64(
        result, "marker_forward_tick_lo", "marker_forward_tick_hi")
    result["marker_return_tick"] = _u64(
        result, "marker_return_tick_lo", "marker_return_tick_hi")
    result["forward_residence_ticks"] = _u64(
        result, "forward_residence_ticks_lo", "forward_residence_ticks_hi")
    result["loop_rtt_ticks"] = _u64(
        result, "loop_rtt_ticks_lo", "loop_rtt_ticks_hi")
    return result


def validate_ring(records: list[dict[str, int | str]]) -> dict[str, object]:
    errors: list[str] = []
    if not 2 <= len(records) <= 8:
        errors.append("board_count")
    nodes = [int(record["local_node"]) for record in records]
    if sorted(nodes) != list(range(len(records))):
        errors.append("node_set")
    if len({int(record["board_unique_id"]) for record in records}) != len(records):
        errors.append("board_identity")

    common_fields = (
        "reference_node", "train_epoch", "train_sequence", "marker_id",
        "marker_codebook_id", "marker_crc32", "calibration_generation",
        "topology_crc32", "profile_crc32", "schedule_crc32",
        "tick_resolution_ns",
    )
    for field in common_fields:
        if len({int(record[field]) for record in records}) != 1:
            errors.append(field)

    by_node = {int(record["local_node"]): record for record in records}
    reference_nodes = {int(record["reference_node"]) for record in records}
    reference_node = next(iter(reference_nodes)) if len(reference_nodes) == 1 else -1
    originators = [record for record in records
                   if int(record["role"]) == ROLE_ORIGINATOR]
    if len(originators) != 1 or (originators and
            int(originators[0]["local_node"]) != reference_node):
        errors.append("originator")
    for node, record in by_node.items():
        if int(record["state"]) != STATE_ACCEPTED:
            errors.append(f"node_{node}_state")
        if int(record["reject_reason"]) != 0:
            errors.append(f"node_{node}_reject_reason")
        if (int(record["flags"]) & REQUIRED_FLAGS) != REQUIRED_FLAGS:
            errors.append(f"node_{node}_flags")
        if int(record["marker_crc32"]) != int(record["observed_crc32"]):
            errors.append(f"node_{node}_crc")
        if int(record["dma_capture_count"]) == 0:
            errors.append(f"node_{node}_dma_capture")
        for field in ("dma_overrun_count", "pio_stall_count", "timeout_count"):
            if int(record[field]) != 0:
                errors.append(f"node_{node}_{field}")
        capture = int(record["marker_capture_tick"])
        forward = int(record["marker_forward_tick"])
        returned = int(record["marker_return_tick"])
        residence = int(record["forward_residence_ticks"])
        loop_rtt = int(record["loop_rtt_ticks"])
        role = int(record["role"])
        if role == ROLE_ORIGINATOR:
            if (forward == 0 or capture != returned or returned < forward or
                    residence != 0 or loop_rtt != returned - forward):
                errors.append(f"node_{node}_tick_order")
        elif role == ROLE_FOLLOWER:
            if (capture == 0 or forward < capture or
                    residence != forward - capture or returned != 0 or
                    loop_rtt != 0):
                errors.append(f"node_{node}_tick_order")
        else:
            errors.append(f"node_{node}_role")
        if len(records) >= 2:
            if int(record["predecessor_node"]) != (node - 1) % len(records):
                errors.append(f"node_{node}_predecessor")
            if int(record["successor_node"]) != (node + 1) % len(records):
                errors.append(f"node_{node}_successor")
        if (int(record["topology_generation"]) == 0 or
                int(record["topology_crc32"]) == 0):
            errors.append(f"node_{node}_topology")

    diagnostic_only = all(
        (int(record["flags"]) & FLAG_DIAGNOSTIC_ONLY) != 0
        for record in records
    )
    if records and not diagnostic_only:
        errors.append("diagnostic_only")
    return {
        "phase": "TRN-01",
        "passed": not errors,
        "diagnostic_only": diagnostic_only,
        "board_count": len(records),
        "train_epoch": int(records[0]["train_epoch"]) if records else None,
        "train_sequence": int(records[0]["train_sequence"]) if records else None,
        "link_offset_model": "marker_capture + base_half_chip + per_link_offset",
        "link_offset_candidates": derive_link_offset_candidates(records),
        "errors": errors,
        "records": records,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=Path,
                        help="UTF-8 file containing one MARKERTRN CSV row per board")
    source.add_argument("--board-id", action="append",
                        help="exact *IDN? address in physical ring order")
    parser.add_argument("--out", type=Path,
                        help="optional UTF-8 JSON summary path")
    parser.add_argument("--expected-build")
    parser.add_argument("--level", type=int, default=7)
    parser.add_argument("--reference-node", type=int, default=0)
    parser.add_argument(
        "--origin-node", type=int,
        help=("marker injection node; defaults to reference-node but may "
              "rotate without changing the TDMA topology identity"))
    parser.add_argument(
        "--residence-matrix", action="store_true",
        help=("run one marker trial per origin node under one unchanged "
              "topology identity and assemble every physical link residence"))
    parser.add_argument(
        "--reuse-ring-identity", action="store_true",
        help="reuse the already staged stopped ring without topology writes")
    parser.add_argument("--codebook", type=int, default=0)
    parser.add_argument("--epoch", type=int, default=1)
    parser.add_argument("--generation", type=int, default=1)
    parser.add_argument(
        "--node-offset-samples", type=int, action="append",
        help="one -10..+10 marker-window offset per board in physical order")
    parser.add_argument(
        "--offset-matrix", action="store_true",
        help="traverse the configured offset matrix, optionally filtered")
    parser.add_argument(
        "--matrix-offset-value", type=int, action="append",
        help="matrix value to retain; repeat as needed (default: every -10..+10)")
    parser.add_argument(
        "--matrix-filter-node-offset", "--matrix-filter-offset",
        dest="matrix_filter_node_offset", action="append", default=[],
        help="select a current matrix subset as NODE=OFFSET; full matrix is retained")
    parser.add_argument("--matrix-epoch-start", type=int)
    parser.add_argument(
        "--matrix-fixed-epoch", action=argparse.BooleanOptionalAction,
        default=True,
        help=("hold epoch/codeword constant across matrix rows and increment "
              "calibration generation for unique SD captures (default: true)"))
    parser.add_argument(
        "--matrix-repeats", type=int, default=8,
        help="fresh ARM/inject repetitions required for every selected row")
    parser.add_argument("--matrix-generation-start", type=int)
    parser.add_argument("--observed-link-delay-ns", type=float)
    parser.add_argument("--driver-propagation-typ-ns", type=float)
    parser.add_argument("--driver-propagation-max-ns", type=float)
    parser.add_argument("--receiver-propagation-typ-ns", type=float)
    parser.add_argument("--receiver-propagation-max-ns", type=float)
    parser.add_argument("--driver-pwd-max-ns", type=float)
    parser.add_argument("--receiver-pwd-max-ns", type=float)
    parser.add_argument("--driver-enable-max-ns", type=float)
    parser.add_argument("--receiver-enable-max-ns", type=float)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--action-timeout", type=float, default=0.05)
    parser.add_argument("--marker-timeout", type=float, default=5.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--topology-retries", type=int, default=3)
    parser.add_argument("--gap", type=float, default=0.2)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def marker_status(board: Board, args: argparse.Namespace) -> dict[str, int | str]:
    return parse_marker_status(board_command(
        board, "READ:CALibration:MARKer?", args))


def topology_matches(status: dict[str, int], node_count: int, node: int,
                     reference_node: int) -> bool:
    """Map TDMA/RefMem slot-ID readback to Calibration node indices."""
    return (
        status.get("ring_enabled") == 1 and
        status.get("ring_adapter_started") == 1 and
        status.get("ring_node_count") == node_count and
        status.get("ring_local_slot_id") == node and
        status.get("ring_reference_slot_id") == reference_node
    )


def physical_timing_budget(args: argparse.Namespace) -> dict[str, object]:
    names = (
        "driver_propagation_typ_ns", "driver_propagation_max_ns",
        "receiver_propagation_typ_ns", "receiver_propagation_max_ns",
        "driver_pwd_max_ns", "receiver_pwd_max_ns",
        "driver_enable_max_ns", "receiver_enable_max_ns",
    )
    supplied = [getattr(args, name, None) for name in names]
    if not any(value is not None for value in supplied):
        return {"provided": False}
    if any(value is None or value < 0 for value in supplied):
        raise SystemExit(
            "all propagation/PWD/enable timing fields must be non-negative")
    half_chip_ns = HALF_CHIP_NS_BY_CODEBOOK[args.codebook]
    propagation_typ_ns = (args.driver_propagation_typ_ns +
                          args.receiver_propagation_typ_ns)
    propagation_max_ns = (args.driver_propagation_max_ns +
                          args.receiver_propagation_max_ns)
    pwd_max_ns = args.driver_pwd_max_ns + args.receiver_pwd_max_ns
    return {
        "provided": True,
        "codebook_half_chip_ns": half_chip_ns,
        "driver_propagation_typ_ns": args.driver_propagation_typ_ns,
        "driver_propagation_max_ns": args.driver_propagation_max_ns,
        "receiver_propagation_typ_ns": args.receiver_propagation_typ_ns,
        "receiver_propagation_max_ns": args.receiver_propagation_max_ns,
        "transceiver_propagation_typ_ns": propagation_typ_ns,
        "transceiver_propagation_max_ns": propagation_max_ns,
        "driver_pwd_max_ns": args.driver_pwd_max_ns,
        "receiver_pwd_max_ns": args.receiver_pwd_max_ns,
        "transceiver_pwd_max_ns": pwd_max_ns,
        "half_chip_after_transceiver_pwd_ns": half_chip_ns - pwd_max_ns,
        "driver_enable_max_ns": args.driver_enable_max_ns,
        "receiver_enable_max_ns": args.receiver_enable_max_ns,
        "observed_link_delay_ns": args.observed_link_delay_ns,
        "observed_link_delay_includes_driver_line_and_receiver": True,
        "observed_link_delay_is_end_to_end_training_delay": True,
        "do_not_add_component_propagation_to_observed_delay": True,
        "component_propagation_deembedding_is_line_diagnostic_only": True,
        "propagation_is_common_mode_shift_not_half_chip_subtraction": True,
    }


def active_operating_level(board: Board, args: argparse.Namespace) -> int:
    raw = board_command(board, "SYSTem:TDMA:OPMode?", args)
    values = next(csv.reader([raw]), [])
    if not values:
        raise RuntimeError(
            f"{board.address}: empty TDMA operating-profile response")
    try:
        return int(values[0].strip().strip('"'), 0)
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid TDMA operating-profile response "
            f"{raw!r}") from exc


def prepare_ring(ordered: list[Board], args: argparse.Namespace) -> list[dict[str, object]]:
    actions: list[dict[str, object]] = []
    node_count = len(ordered)
    for board in ordered:
        actions.append({"board": board.address, "command": "STOP",
                        "response": board_command(
                            board, "SYSTem:TDMA:RING:STOP", args)})
        active_level = active_operating_level(board, args)
        if active_level == args.level:
            actions.append({"board": board.address,
                            "command": "OPMODE_REUSE",
                            "active_level": active_level})
        else:
            actions.append({"board": board.address, "command": "OPMODE",
                            "response": board_command(
                                board,
                                f"SYSTem:TDMA:OPMode:STAGe {args.level}",
                                args)})
            actions.append({"board": board.address,
                            "command": "OPMODE_APPLY",
                            "response": board_command(
                                board, "SYSTem:TDMA:OPMode:APPLy", args)})
    for node, board in enumerate(ordered):
        actions.append({"board": board.address, "command": "TOPOLOGY",
                        "response": board_command(
                            board, f"SYSTem:TDMA:RING:TOPology "
                            f"{node_count},{node},{args.reference_node}", args),
                        "node": node, "board_no": node + 1})
        board_no_raw = board_command(board, "SYSTem:BOARD:NO?", args)
        board_no = int(board_no_raw.strip().strip('"'), 0)
        if board_no != node + 1:
            raise RuntimeError(
                f"{board.address}: BOARD:NO changed during topology staging: "
                f"expected {node + 1}, observed {board_no}")
        actions.append({"board": board.address,
                        "command": "BOARD_NO_READBACK",
                        "board_no": board_no})
    # Calibration owns PIO/SM/DMA directly.  Do not ARM the flight adapter
    # here: flight runtime requires a complete MARK+SCK+DATA calibration
    # stage, while this preparation path exists to create that stage.
    time.sleep(args.gap)
    return actions


def marker_action_args(args: argparse.Namespace) -> argparse.Namespace:
    action = argparse.Namespace(**vars(args))
    action.timeout = args.action_timeout
    return action


def arm_marker(board: Board, args: argparse.Namespace,
               offset_sample_count: int) -> str:
    command = (f"CALibration:MARKer:ARM {args.codebook},{args.epoch},"
               f"{args.epoch},{args.epoch},{args.generation},"
               f"{offset_sample_count},{args.origin_node}")
    response = board_command(board, command, marker_action_args(args))
    expected = [args.codebook, args.epoch, args.epoch, args.epoch,
                args.generation, offset_sample_count, args.origin_node]
    try:
        actual = [int(value.strip().strip('"'), 0)
                  for value in next(csv.reader([response]), [])]
    except ValueError:
        actual = []
    if actual != expected and not response.startswith("OK(no payload"):
        snapshot = marker_status(board, args)
        if (int(snapshot["state"]) == 0 or
                int(snapshot["train_epoch"]) != args.epoch):
            raise RuntimeError(
                f"{board.address}: marker ARM rejected: {response!r}, "
                f"snapshot={snapshot}")
    return response


def wait_marker_armed(ordered: list[Board], args: argparse.Namespace) -> list[dict[str, int | str]]:
    deadline = time.monotonic() + args.marker_timeout
    last = [marker_status(board, args) for board in ordered]
    while time.monotonic() < deadline:
        if all(int(snapshot["state"]) == STATE_PREPARED and
               int(snapshot["train_epoch"]) == args.epoch
               for snapshot in last):
            return last
        time.sleep(0.05)
        last = [marker_status(board, args) for board in ordered]
    raise RuntimeError(f"marker ARM readiness timeout: {last}")


def inject_marker(board: Board, args: argparse.Namespace) -> str:
    response = board_command(
        board, "CALibration:MARKer:INJect", marker_action_args(args))
    if not response.startswith("OK"):
        raise RuntimeError(
            f"{board.address}: marker INJECT rejected: {response!r}")
    return response


def wait_marker(ordered: list[Board], args: argparse.Namespace) -> list[dict[str, int | str]]:
    deadline = time.monotonic() + args.marker_timeout
    last = [marker_status(board, args) for board in ordered]
    while time.monotonic() < deadline:
        if all(int(snapshot["state"]) in (3, 4) for snapshot in last):
            return last
        time.sleep(0.05)
        last = [marker_status(board, args) for board in ordered]
    raise RuntimeError(f"marker completion timeout: {last}")


def stop_marker(ordered: list[Board], args: argparse.Namespace) -> None:
    action_args = marker_action_args(args)
    for board in ordered:
        board_command(board, "CALibration:MARKer:STOP", action_args)
    deadline = time.monotonic() + args.marker_timeout
    while time.monotonic() < deadline:
        if all(int(marker_status(board, args)["state"]) == 0
               for board in ordered):
            return
        time.sleep(0.05)
    raise RuntimeError("marker STOP did not restore IDLE on every board")


def save_marker_capture(board: Board, args: argparse.Namespace) -> dict[str, object]:
    response = board_command(
        board, "CALibration:MARKer:CAPTure:SAVE", args)
    values = [value.strip().strip('"')
              for value in next(csv.reader([response]), [])]
    if len(values) != 3 or values[0] != "OK":
        raise RuntimeError(
            f"{board.address}: marker capture SD save rejected: {response!r}")
    job_id = int(values[1], 0)
    path = values[2]
    deadline = time.monotonic() + args.marker_timeout
    last = ""
    while time.monotonic() < deadline:
        last = board_command(board, "SYSTem:STORage:JOB?", args)
        job = [value.strip().strip('"')
               for value in next(csv.reader([last]), [])]
        if len(job) >= 8 and int(job[1], 0) == job_id:
            if job[0] == "DONE":
                return {
                    "node_id": board.address,
                    "sd_path": path,
                    "job_id": job_id,
                    "size": int(job[4], 0),
                    "state": job[0],
                }
            if job[0] == "FAILED":
                raise RuntimeError(
                    f"{board.address}: marker capture SD job failed: {last!r}")
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: marker capture SD job timeout: {last!r}")


def run_hil(args: argparse.Namespace) -> dict[str, object]:
    board_ids = list(args.board_id or [])
    if len(board_ids) < 2 or len(board_ids) > 8 or len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must contain 2..8 unique entries")
    if not 0 <= args.reference_node < len(board_ids):
        raise SystemExit("reference-node outside board order")
    if args.origin_node is None:
        args.origin_node = args.reference_node
    if not 0 <= args.origin_node < len(board_ids):
        raise SystemExit("origin-node outside board order")
    if not 0 <= args.codebook <= 3 or not 1 <= args.epoch <= 255:
        raise SystemExit("codebook must be 0..3 and epoch must be 1..255")
    offsets = list(args.node_offset_samples or [0] * len(board_ids))
    if len(offsets) != len(board_ids) or any(
            not MIN_OFFSET_SAMPLES <= offset <= MAX_OFFSET_SAMPLES
            for offset in offsets):
        raise SystemExit(
            "node-offset-samples must provide one -10..+10 value per board")
    half_chip_samples = HALF_CHIP_NS_BY_CODEBOOK[args.codebook] // SAMPLE_PERIOD_NS
    try:
        capture_delays = [capture_phase_delay_cycles(args.codebook, offset)
                          for offset in offsets]
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    timing_budget = physical_timing_budget(args)
    if (timing_budget.get("provided") and
            float(timing_budget["half_chip_after_transceiver_pwd_ns"]) <= 0):
        raise SystemExit("transceiver PWD exhausts the selected half-chip")
    args.board_ids = board_ids
    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    ordered = order_boards_by_board_no(boards, board_ids, args)
    board_ids = [board.address for board in ordered]
    args.board_id = list(board_ids)
    if args.expected_build:
        wrong = {board.address: board.build for board in ordered
                 if board.build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")
    plan: dict[str, object] = {
        "measurement_domain": "calibration",
        "phase": "TRN-01",
        "diagnostic_only": True,
        "node_ids_in_loop_order": board_ids,
        "reference_node": args.reference_node,
        "topology_reference_node": args.reference_node,
        "origin_node": args.origin_node,
        "codebook": args.codebook,
        "epoch": args.epoch,
        "generation": args.generation,
        "node_offset_samples": offsets,
        "node_offset_ns": [offset * SAMPLE_PERIOD_NS for offset in offsets],
        "offset_application": "PIO_CAPTURE_PHASE_DELAY_ONLY_FORWARD_FIXED",
        "base_half_chip_ns": HALF_CHIP_NS_BY_CODEBOOK[args.codebook],
        "pio_forward_phase_delay_cycles": [1 for _ in offsets],
        "pio_capture_phase_delay_cycles": capture_delays,
        "physical_timing_budget": timing_budget,
        "boards": {board.address: asdict(board) for board in ordered},
    }
    if args.dry_run:
        return {**plan, "passed": False, "dry_run": True}
    actions = [] if args.reuse_ring_identity else prepare_ring(ordered, args)
    follower_nodes = [
        (args.origin_node + offset) % len(ordered)
        for offset in range(1, len(ordered))
    ]
    followers = [ordered[node] for node in follower_nodes]
    originator = ordered[args.origin_node]
    arms: list[dict[str, str | int]] = []
    try:
        # Every node first arms RX DMA and its physical rx_csn WAIT gate.  The
        # originator TX SM also remains blocked on an empty PIO TX FIFO.
        for board in followers:
            node = ordered.index(board)
            arms.append({"board": board.address,
                         "offset_sample_count": offsets[node],
                         "response": arm_marker(
                             board, args, offsets[node])})
        arms.append({"board": originator.address,
                     "offset_sample_count": offsets[args.origin_node],
                     "response": arm_marker(
                         originator, args, offsets[args.origin_node])})
        armed = wait_marker_armed(ordered, args)
        injection = {
            "board": originator.address,
            "response": inject_marker(originator, args),
        }
        records = wait_marker(ordered, args)
        validation = validate_ring(records)
        capture_files = [save_marker_capture(board, args) for board in ordered]
    finally:
        stop_marker(ordered, args)
    return {**plan, **validation, "actions": actions, "arms": arms,
            "armed": armed, "injection": injection,
            "capture_files": capture_files}


def build_offset_matrix(
        node_count: int,
        values: tuple[int, ...] = DEFAULT_MATRIX_OFFSET_VALUES,
) -> list[dict[str, object]]:
    return [{
        "row_id": row_id,
        "offset_sample_counts_by_node": list(offsets),
        "offset_ns_by_node": [offset * SAMPLE_PERIOD_NS for offset in offsets],
    } for row_id, offsets in enumerate(
        itertools.product(values, repeat=node_count))]


def parse_matrix_filters(raw_filters: list[str], node_count: int,
                         values: tuple[int, ...]) -> dict[int, int]:
    filters: dict[int, int] = {}
    for raw in raw_filters:
        try:
            node_text, offset_text = raw.split("=", 1)
            node = int(node_text, 0)
            offset = int(offset_text, 0)
        except (ValueError, TypeError) as exc:
            raise SystemExit(
                f"invalid matrix filter {raw!r}; expected NODE=OFFSET") from exc
        if not 0 <= node < node_count or offset not in values:
            raise SystemExit(f"matrix filter outside node/offset range: {raw!r}")
        filters[node] = offset
    return filters


def matrix_trial_identity(*, trial_index: int, epoch_start: int,
                          generation_start: int,
                          fixed_epoch: bool) -> tuple[int, int]:
    if trial_index < 0 or epoch_start < 1 or generation_start < 1:
        raise ValueError("matrix trial identity inputs must be positive")
    return ((epoch_start if fixed_epoch else epoch_start + trial_index),
            generation_start + trial_index if fixed_epoch else generation_start)


def summarize_matrix_row_trials(
        row: dict[str, object], trials: list[dict[str, object]],
        repeats: int) -> dict[str, object]:
    """Require one stable accepted observation per Node for every repeat."""
    offsets = [int(value) for value in row["offset_sample_counts_by_node"]]
    node_count = len(offsets)
    nodes: list[dict[str, object]] = []
    failures: list[str] = []
    if len(trials) != repeats:
        failures.append("repeat_count")
    for node in range(node_count):
        evidence = [
            record
            for trial in trials
            for record in trial.get("nodes", [])
            if int(record["node"]) == node
        ]
        distances = [int(record["best_distance"]) for record in evidence]
        lags = [int(record["best_lag_sample"]) for record in evidence]
        accepted_count = sum(
            int(record["state"]) == STATE_ACCEPTED and
            int(record["correlation_reject_reason"]) == 0
            for record in evidence)
        lag_span = max(lags) - min(lags) if lags else None
        passed = (
            len(evidence) == repeats and accepted_count == repeats and
            lag_span is not None and lag_span <= 1)
        if not passed:
            failures.append(f"node_{node}_repeat_gate")
        nodes.append({
            "node": node,
            "incoming_link": {
                "source_node": (node - 1) % node_count,
                "destination_node": node,
            },
            "capture_offset_sample_count": offsets[node],
            "capture_offset_ns": offsets[node] * SAMPLE_PERIOD_NS,
            "observation_count": len(evidence),
            "accepted_count": accepted_count,
            "best_lag_sample_min": min(lags) if lags else None,
            "best_lag_sample_max": max(lags) if lags else None,
            "best_lag_sample_span": lag_span,
            "best_distance_min": min(distances) if distances else None,
            "best_distance_max": max(distances) if distances else None,
            "best_distance_mean": (
                statistics.fmean(distances) if distances else None),
            "passed": passed,
        })
    if any(not bool(trial.get("passed")) for trial in trials):
        failures.append("ring_repeat_gate")
    return {
        **row,
        "repeat_count": len(trials),
        "required_repeat_count": repeats,
        "nodes": nodes,
        "failures": failures,
        "passed": not failures,
    }


def select_recommended_matrix_row(
        rows: list[dict[str, object]]) -> dict[str, object] | None:
    passed = [row for row in rows if bool(row.get("passed"))]
    if not passed:
        return None

    def score(row: dict[str, object]) -> tuple[float, float, int, int]:
        nodes = list(row["nodes"])
        worst = max(float(node["best_distance_max"]) for node in nodes)
        mean = statistics.fmean(
            float(node["best_distance_mean"]) for node in nodes)
        offset_norm = sum(abs(int(value)) for value in
                          row["offset_sample_counts_by_node"])
        return worst, mean, offset_norm, int(row["row_id"])

    selected = min(passed, key=score)
    return {
        "row_id": int(selected["row_id"]),
        "offset_sample_counts_by_node": list(
            selected["offset_sample_counts_by_node"]),
        "selection_score": {
            "worst_best_distance": score(selected)[0],
            "mean_best_distance": score(selected)[1],
            "offset_l1_norm": score(selected)[2],
        },
    }


def run_offset_matrix(args: argparse.Namespace) -> dict[str, object]:
    board_ids = list(args.board_id or [])
    if len(board_ids) < 2:
        raise SystemExit("offset matrix requires board-id inputs")
    raw_values = args.matrix_offset_value or DEFAULT_MATRIX_OFFSET_VALUES
    matrix_values = tuple(dict.fromkeys(int(value) for value in raw_values))
    if not matrix_values or any(
            not MIN_OFFSET_SAMPLES <= value <= MAX_OFFSET_SAMPLES
            for value in matrix_values):
        raise SystemExit("matrix-offset-value must be within -10..+10")
    full_matrix = build_offset_matrix(len(board_ids), matrix_values)
    filters = parse_matrix_filters(
        args.matrix_filter_node_offset, len(board_ids), matrix_values)
    selected = [row for row in full_matrix if all(
        row["offset_sample_counts_by_node"][node] == offset
        for node, offset in filters.items())]
    try:
        for row in selected:
            for offset in row["offset_sample_counts_by_node"]:
                capture_phase_delay_cycles(args.codebook, int(offset))
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    epoch_start = args.matrix_epoch_start or args.epoch
    generation_start = args.matrix_generation_start or args.generation
    if args.matrix_repeats < 1:
        raise SystemExit("matrix-repeats must be at least 1")
    execution_count = len(selected) * args.matrix_repeats
    if (epoch_start < 1 or
            (not args.matrix_fixed_epoch and
             epoch_start + execution_count - 1 > 255)):
        raise SystemExit("selected matrix rows exceed the marker epoch range")
    if generation_start < 1:
        raise SystemExit("matrix generation start must be positive")
    matrix_out = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn01_marker_matrix_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    matrix_out.mkdir(parents=True, exist_ok=True)
    trial_results: list[dict[str, object]] = []
    row_results: list[dict[str, object]] = []
    execution_index = 0
    for row in selected:
        row_trials: list[dict[str, object]] = []
        for repeat_index in range(1, args.matrix_repeats + 1):
            trial_args = argparse.Namespace(**vars(args))
            trial_args.offset_matrix = False
            trial_args.epoch, trial_args.generation = matrix_trial_identity(
                trial_index=execution_index,
                epoch_start=epoch_start,
                generation_start=generation_start,
                fixed_epoch=args.matrix_fixed_epoch)
            execution_index += 1
            trial_args.node_offset_samples = list(
                row["offset_sample_counts_by_node"])
            trial_args.out_dir = (
                matrix_out / f"row_{int(row['row_id']):06d}" /
                f"repeat_{repeat_index:02d}_g{trial_args.generation}")
            result = run_hil(trial_args)
            encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
            trial_args.out_dir.mkdir(parents=True, exist_ok=True)
            (trial_args.out_dir / "summary.json").write_text(
                encoded, encoding="utf-8")
            records = list(result.get("records", []))
            trial = {
                **row,
                "repeat_index": repeat_index,
                "epoch": trial_args.epoch,
                "generation": trial_args.generation,
                "passed": bool(result.get("passed")),
                "accepted_nodes": [
                    int(record["local_node"]) for record in records
                    if int(record["state"]) == STATE_ACCEPTED],
                "nodes": [{
                    "node": int(record["local_node"]),
                    "incoming_link": {
                        "source_node": int(record["predecessor_node"]),
                        "destination_node": int(record["local_node"]),
                    },
                    "state": int(record["state"]),
                    "correlation_reject_reason": int(
                        record["correlation_reject_reason"]),
                    "best_lag_sample": int(record["best_lag_sample"]),
                    "best_distance": int(record["best_distance"]),
                    "polarity": int(record["polarity"]),
                } for record in records],
                "capture_files": result.get("capture_files", []),
                "summary": str(trial_args.out_dir / "summary.json"),
            }
            row_trials.append(trial)
            trial_results.append(trial)
        row_results.append(summarize_matrix_row_trials(
            row, row_trials, args.matrix_repeats))
    passed_rows = [row["row_id"] for row in row_results if row["passed"]]
    recommended_row = select_recommended_matrix_row(row_results)
    return {
        "phase": "TRN-01_FOUR_NODE_OFFSET_MATRIX",
        "diagnostic_only": True,
        "offset_model": "marker_capture + base_half_chip + per_node_offset_samples*4ns",
        "offset_application": "PIO_CAPTURE_PHASE_DELAY_ONLY_FORWARD_FIXED",
        "base_half_chip_ns": HALF_CHIP_NS_BY_CODEBOOK[args.codebook],
        "pio_capture_phase_delay_mapping": {
            str(value): capture_phase_delay_cycles(args.codebook, value)
            for value in matrix_values
        },
        "pio_forward_phase_delay_cycles": 1,
        "node_ids_in_loop_order": board_ids,
        "matrix_values": list(matrix_values),
        "full_matrix_row_count": len(full_matrix),
        "full_matrix": full_matrix,
        "selection_filters_by_node": {
            str(node): offset for node, offset in filters.items()},
        "selected_row_ids": [row["row_id"] for row in selected],
        "selected_row_count": len(selected),
        "generation": args.generation,
        "matrix_fixed_epoch": bool(args.matrix_fixed_epoch),
        "epoch_start": epoch_start,
        "generation_start": generation_start,
        "repeats_per_row": args.matrix_repeats,
        "expected_trial_count": execution_count,
        "trial_results": trial_results,
        "row_results": row_results,
        "passed_row_ids": passed_rows,
        "recommended_row": recommended_row,
        "passed": bool(passed_rows),
    }


def summarize_residence_matrix(
        trials: list[dict[str, object]],
        node_count: int,
) -> dict[str, object]:
    identity_fields = (
        "calibration_generation", "topology_crc32",
        "profile_crc32", "schedule_crc32", "tick_resolution_ns",
    )
    identity = {field: set() for field in identity_fields}
    topology_generation_by_node: dict[int, set[int]] = {
        node: set() for node in range(node_count)}
    residence_by_node: dict[int, list[int]] = {
        node: [] for node in range(node_count)}
    loop_delay_by_node: dict[int, list[int]] = {
        node: [] for node in range(node_count)}
    failures: list[str] = []
    trial_diagnostics: list[dict[str, object]] = []
    if len(trials) != node_count:
        failures.append("trial_count")
    for trial_index, trial in enumerate(trials):
        trial_diagnostics.append({
            "trial_index": trial_index,
            "origin_node": trial.get("origin_node"),
            "full_ring_marker_passed": bool(trial.get("passed")),
            "error": trial.get("error", ""),
        })
        records = trial.get("records", [])
        if not isinstance(records, list) or len(records) != node_count:
            failures.append(f"trial_{trial_index}_records")
            continue
        for record in records:
            if not isinstance(record, dict):
                failures.append(f"trial_{trial_index}_record_type")
                continue
            for field in identity_fields:
                identity[field].add(int(record[field]))
            node = int(record["local_node"])
            topology_generation_by_node.setdefault(node, set()).add(
                int(record["topology_generation"]))
            if int(record["role"]) == ROLE_FOLLOWER:
                if (int(record["state"]) != STATE_ACCEPTED or
                        int(record["reject_reason"]) != 0 or
                        (int(record["flags"]) & REQUIRED_FLAGS) !=
                        REQUIRED_FLAGS):
                    failures.append(
                        f"trial_{trial_index}_node_{node}_follower")
                residence_by_node.setdefault(node, []).append(
                    int(record["forward_residence_ticks"]))
            elif int(record["role"]) == ROLE_ORIGINATOR:
                loop_delay_by_node.setdefault(node, []).append(
                    int(record["loop_rtt_ticks"]))
    identity_summary = {
        field: sorted(values) for field, values in identity.items()}
    for field, values in identity.items():
        if len(values) != 1 or 0 in values:
            failures.append(field)
    identity_summary["topology_generation_by_node"] = {
        str(node): sorted(topology_generation_by_node.get(node, set()))
        for node in range(node_count)
    }
    for node in range(node_count):
        values = topology_generation_by_node.get(node, set())
        if len(values) != 1 or 0 in values:
            failures.append(f"topology_generation_node{node}")

    links: list[dict[str, object]] = []
    expected_residence_count = max(1, node_count - 1)
    for destination_node in range(node_count):
        values = residence_by_node.get(destination_node, [])
        valid_values = [value for value in values if value > 0]
        span = max(valid_values) - min(valid_values) if valid_values else None
        passed = (
            len(values) == expected_residence_count and
            len(valid_values) == len(values) and span is not None and span <= 1)
        if not passed:
            failures.append(f"node_{destination_node}_forward_residence")
        links.append({
            "link_index": (destination_node - 1) % node_count,
            "source_node": (destination_node - 1) % node_count,
            "destination_node": destination_node,
            "forward_residence_ticks": values,
            "forward_residence_span_ticks": span,
            "selected_forward_residence_ticks": (
                sorted(valid_values)[len(valid_values) // 2]
                if valid_values else None),
            "repeat_count": len(values),
            "passed": passed,
        })
    links.sort(key=lambda row: int(row["link_index"]))
    loops = [{
        "node": node,
        "loop_delay_ticks": loop_delay_by_node.get(node, []),
    } for node in range(node_count)]
    return {
        "identity": identity_summary,
        "links": links,
        "loops": loops,
        "trial_diagnostics": trial_diagnostics,
        "full_ring_marker_passed_count": sum(
            bool(row["full_ring_marker_passed"])
            for row in trial_diagnostics),
        "failures": failures,
        "passed": not failures,
    }


def run_residence_matrix(args: argparse.Namespace) -> dict[str, object]:
    board_ids = list(args.board_id or [])
    node_count = len(board_ids)
    if not 2 <= node_count <= 8 or len(set(board_ids)) != node_count:
        raise SystemExit("board IDs must contain 2..8 unique entries")
    if args.epoch + node_count - 1 > 255:
        raise SystemExit("residence matrix exceeds uint8 training epoch range")
    trials: list[dict[str, object]] = []
    for origin_node in range(node_count):
        trial_args = argparse.Namespace(**vars(args))
        trial_args.residence_matrix = False
        trial_args.offset_matrix = False
        trial_args.origin_node = origin_node
        trial_args.epoch = args.epoch + origin_node
        trial_args.reuse_ring_identity = origin_node != 0
        try:
            trial = run_hil(trial_args)
        except Exception as exc:  # retain failed physical evidence
            trial = {
                "phase": "TRN-01",
                "origin_node": origin_node,
                "epoch": trial_args.epoch,
                "passed": False,
                "error": f"{type(exc).__name__}: {exc}",
            }
        trials.append(trial)
        print(json.dumps({
            "origin_node": origin_node,
            "epoch": trial_args.epoch,
            "passed": bool(trial.get("passed")),
            "error": trial.get("error", ""),
        }, ensure_ascii=False), flush=True)
        time.sleep(args.gap)
    matrix = summarize_residence_matrix(trials, node_count)
    return {
        "measurement_domain": "calibration",
        "phase": "TRN-01_RESIDENCE_MATRIX",
        "diagnostic_only": True,
        "node_ids_in_loop_order": board_ids,
        "topology_reference_node": args.reference_node,
        "calibration_generation": args.generation,
        "initial_epoch": args.epoch,
        "trial_count": len(trials),
        "matrix": matrix,
        "trials": trials,
        "passed": bool(matrix["passed"]),
    }


def main() -> int:
    args = parse_args()
    if args.board_id is not None:
        if args.residence_matrix and args.offset_matrix:
            raise SystemExit("residence-matrix and offset-matrix are exclusive")
        result = (run_residence_matrix(args) if args.residence_matrix else
                  run_offset_matrix(args) if args.offset_matrix else
                  run_hil(args))
        encoded = json.dumps(result, ensure_ascii=False, indent=2)
        print(encoded)
        out_dir = args.out_dir or (
            ROOT / "out" / "training" /
            f"trn01_marker_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "summary.json").write_text(
            encoded + "\n", encoding="utf-8")
        if args.out is not None:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(encoded + "\n", encoding="utf-8")
        return 0 if bool(result.get("passed")) else 1
    records = [
        parse_marker_status(line)
        for line in args.input.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    result = validate_ring(records)
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    print(encoded)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encoded + "\n", encoding="utf-8")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
