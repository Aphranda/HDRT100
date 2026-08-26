#!/usr/bin/env python3
"""Exercise TRN-03 staging rejection gates on a physical 2..8 node ring.

This is a diagnostic-only HIL tool.  It discovers the active profile and
schedule identity from each board, deliberately submits incomplete or invalid
replay evidence, verifies that ARM remains closed, and persists the transcript.
It never emits a replay matrix that can be reused as TRN-03 admission evidence.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
for import_path in (ROOT / "tools", ROOT / "tools" / "tdma_ring_monitor"):
    if str(import_path) not in sys.path:
        sys.path.insert(0, str(import_path))

from tdma_field_parse import FIELDS as TDMA_FIELDS  # noqa: E402
from tdma_start_ring import Board, board_command, discover  # noqa: E402


REQUIRED_EVIDENCE_FLAGS = 0x1F
DIAGNOSTIC_ONLY_FLAG = 1 << 31
STAGE_QUERY_FIELDS = (
    "tag", "enabled", "node_count", "evidence_flags", "complete",
    "calibration_generation", "topology_generation", "topology_crc32",
    "profile_crc32", "schedule_crc32", "valid_link_bitmap",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address in physical node order")
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def parse_u32_csv(raw: str) -> list[int]:
    return [int(value.strip().strip('"'), 0)
            for value in next(csv.reader([raw]), [])]


def parse_stage(raw: str) -> dict[str, int | str]:
    row = next(csv.reader([raw]), [])
    if (len(row) != len(STAGE_QUERY_FIELDS) or
            row[0].strip().strip('"') != "TRN03STG"):
        raise RuntimeError(f"invalid TRN03STG response: {raw!r}")
    result: dict[str, int | str] = {"tag": "TRN03STG"}
    for field, value in zip(STAGE_QUERY_FIELDS[1:], row[1:]):
        result[field] = int(value.strip().strip('"'), 0)
    return result


def error_is_clear(raw: str) -> bool:
    row = next(csv.reader([raw]), [])
    if not row:
        return False
    try:
        return int(row[0].strip().strip('"'), 0) == 0
    except ValueError:
        return False


def error_is_rejection(raw: str) -> bool:
    return not error_is_clear(raw)


def drain_errors(board: Board, args: argparse.Namespace) -> list[str]:
    errors: list[str] = []
    for _ in range(16):
        raw = board_command(board, "SYSTem:ERR?", args)
        errors.append(raw)
        if error_is_clear(raw):
            return errors
    raise RuntimeError(f"{board.address}: SCPI error queue did not drain")


def action_with_error(board: Board, command: str,
                      args: argparse.Namespace) -> dict[str, Any]:
    drained = drain_errors(board, args)
    response = board_command(board, command, args)
    error = board_command(board, "SYSTem:ERR?", args)
    return {
        "command": command,
        "response": response,
        "errors_drained_before": drained,
        "error_after": error,
    }


def stage_begin_command(node_count: int, evidence_flags: int,
                        identity: dict[str, int]) -> str:
    values = (
        node_count,
        evidence_flags,
        identity["calibration_generation"],
        identity["topology_generation"],
        identity["topology_crc32"],
        identity["profile_crc32"],
        identity["schedule_crc32"],
    )
    return "CALibration:TRAINing:STAGe:BEGin " + ",".join(map(str, values))


def stage_link_command(link_index: int, evidence_flags: int,
                       expired: bool = False,
                       offset_phase_mismatch: bool = False) -> str:
    # Diagnostic constants describe a synthetic replay workload only.  The
    # deliberate incomplete/invalid cases below prevent it from becoming an
    # admissible matrix.
    marker_to_data_cycles = 10
    forward_residence_cycles = 5
    rx_arm_lead_cycles = 2
    codeword_cycles = 20
    guard_cycles = 2
    loop_delay_cycles = 8
    required_cycles = sum((marker_to_data_cycles,
                           forward_residence_cycles,
                           rx_arm_lead_cycles,
                           codeword_cycles,
                           guard_cycles,
                           loop_delay_cycles))
    link_budget_cycles = required_cycles - 1 if expired else required_cycles
    data_offset_sample_count = 4 if offset_phase_mismatch else 5
    values = (
        link_index, evidence_flags, 1, 65536, 150_000_000, 4, 25,
        marker_to_data_cycles, forward_residence_cycles, rx_arm_lead_cycles,
        codeword_cycles, guard_cycles, link_budget_cycles, loop_delay_cycles,
        0, 0, data_offset_sample_count, 4, 40, 10, 10, 15,
    )
    return "CALibration:TRAINing:STAGe:LINK " + ",".join(map(str, values))


def stage_snapshot(board: Board, args: argparse.Namespace) -> dict[str, Any]:
    raw = board_command(board, "READ:CALibration:TRAINing:STAGe?", args)
    if raw.strip().strip('"') == "EMPTY":
        return {"tag": "EMPTY"}
    return parse_stage(raw)


def ring_values(board: Board, args: argparse.Namespace) -> dict[str, int]:
    raw = board_command(board, "SYSTem:REFMEM:SYNC:TDMA:STATus?", args)
    values = parse_u32_csv(raw)
    if len(values) != len(TDMA_FIELDS):
        raise RuntimeError(
            f"{board.address}: TDMA status has {len(values)} fields, "
            f"expected {len(TDMA_FIELDS)}")
    return {field: values[index] for index, field in enumerate(TDMA_FIELDS)}


def discover_runtime_identity(board: Board, node_count: int, node_index: int,
                              args: argparse.Namespace) -> tuple[dict[str, int],
                                                                  list[dict[str, Any]]]:
    transcript: list[dict[str, Any]] = []
    for command in (
            "SYSTem:TDMA:RING:STOP",
            "CALibration:TRAINing:STAGe:CLEar",
            f"SYSTem:TDMA:RING:TOPology {node_count},{node_index},0",
            "SYSTem:TDMA:RING:ARM"):
        transcript.append(action_with_error(board, command, args))
        if not error_is_clear(transcript[-1]["error_after"]):
            raise RuntimeError(
                f"{board.address}: setup command failed: {command}: "
                f"{transcript[-1]['error_after']}")
    profile = parse_u32_csv(board_command(board, "SYSTem:TDMA:OPMode?", args))
    if len(profile) < 6:
        raise RuntimeError(f"{board.address}: invalid operating profile query")
    status = ring_values(board, args)
    identity = {
        "calibration_generation": 1,
        "topology_generation": 1,
        "topology_crc32": 1,
        "profile_crc32": profile[5],
        "schedule_crc32": status["ring_schedule_crc32"],
    }
    if (status["ring_enabled"] != 1 or
            status["ring_node_count"] != node_count or
            status["ring_local_slot_id"] != node_index or
            identity["profile_crc32"] == 0 or
            identity["schedule_crc32"] == 0):
        raise RuntimeError(
            f"{board.address}: runtime identity exposure failed: "
            f"node={node_index}, status={status}, identity={identity}")
    transcript.append(action_with_error(
        board, "SYSTem:TDMA:RING:STOP", args))
    if not error_is_clear(transcript[-1]["error_after"]):
        raise RuntimeError(f"{board.address}: STOP after identity failed")
    return identity, transcript


def arm_must_reject(board: Board, args: argparse.Namespace) -> dict[str, Any]:
    action = action_with_error(board, "SYSTem:TDMA:RING:ARM", args)
    status = ring_values(board, args)
    action["ring_enabled_after"] = status["ring_enabled"]
    action["passed"] = (
        error_is_rejection(action["error_after"]) and
        status["ring_enabled"] == 0)
    return action


def run_board(board: Board, node_count: int, node_index: int,
              args: argparse.Namespace) -> dict[str, Any]:
    identity, setup = discover_runtime_identity(
        board, node_count, node_index, args)
    cases: list[dict[str, Any]] = []

    # The header itself is explicitly diagnostic and must be rejected.
    clear = action_with_error(board, "CALibration:TRAINing:STAGe:CLEar", args)
    diagnostic = action_with_error(board, stage_begin_command(
        node_count, REQUIRED_EVIDENCE_FLAGS | DIAGNOSTIC_ONLY_FLAG, identity),
        args)
    cases.append({
        "name": "diagnostic_header_rejected",
        "clear": clear,
        "action": diagnostic,
        "stage": stage_snapshot(board, args),
        "passed": (error_is_rejection(diagnostic["error_after"]) and
                   stage_snapshot(board, args)["tag"] == "EMPTY"),
    })

    # BEGIN is accepted, but zero links means ARM must remain closed.
    clear = action_with_error(board, "CALibration:TRAINing:STAGe:CLEar", args)
    begin = action_with_error(board, stage_begin_command(
        node_count, REQUIRED_EVIDENCE_FLAGS, identity), args)
    snapshot = stage_snapshot(board, args)
    arm = arm_must_reject(board, args)
    cases.append({
        "name": "empty_matrix_arm_rejected",
        "clear": clear, "begin": begin, "stage": snapshot, "arm": arm,
        "passed": (error_is_clear(begin["error_after"]) and
                   snapshot.get("complete") == 0 and
                   snapshot.get("valid_link_bitmap") == 0 and arm["passed"]),
    })

    # Fill every physical link except the final one.
    clear = action_with_error(board, "CALibration:TRAINing:STAGe:CLEar", args)
    begin = action_with_error(board, stage_begin_command(
        node_count, REQUIRED_EVIDENCE_FLAGS, identity), args)
    links = [action_with_error(board, stage_link_command(
        link_index, REQUIRED_EVIDENCE_FLAGS), args)
        for link_index in range(node_count - 1)]
    snapshot = stage_snapshot(board, args)
    arm = arm_must_reject(board, args)
    expected_bitmap = (1 << (node_count - 1)) - 1
    cases.append({
        "name": "missing_link_arm_rejected",
        "clear": clear, "begin": begin, "links": links,
        "stage": snapshot, "arm": arm,
        "passed": (all(error_is_clear(link["error_after"])
                       for link in links) and
                   snapshot.get("complete") == 0 and
                   snapshot.get("valid_link_bitmap") == expected_bitmap and
                   arm["passed"]),
    })

    # A diagnostic-only link may not enter the matrix.
    clear = action_with_error(board, "CALibration:TRAINing:STAGe:CLEar", args)
    begin = action_with_error(board, stage_begin_command(
        node_count, REQUIRED_EVIDENCE_FLAGS, identity), args)
    link = action_with_error(board, stage_link_command(
        0, REQUIRED_EVIDENCE_FLAGS | DIAGNOSTIC_ONLY_FLAG), args)
    snapshot = stage_snapshot(board, args)
    cases.append({
        "name": "diagnostic_link_rejected",
        "clear": clear, "begin": begin, "link": link, "stage": snapshot,
        "passed": (error_is_rejection(link["error_after"]) and
                   snapshot.get("valid_link_bitmap") == 0),
    })

    # A replay workload whose required cycles exceed its link budget fails.
    clear = action_with_error(board, "CALibration:TRAINing:STAGe:CLEar", args)
    begin = action_with_error(board, stage_begin_command(
        node_count, REQUIRED_EVIDENCE_FLAGS, identity), args)
    link = action_with_error(board, stage_link_command(
        0, REQUIRED_EVIDENCE_FLAGS, expired=True), args)
    snapshot = stage_snapshot(board, args)
    arm = arm_must_reject(board, args)
    cases.append({
        "name": "expired_link_budget_rejected",
        "clear": clear, "begin": begin, "link": link,
        "stage": snapshot, "arm": arm,
        "passed": (error_is_rejection(link["error_after"]) and
                   snapshot.get("valid_link_bitmap") == 0 and arm["passed"]),
    })

    # The reported Node offset and the phase actually patched into PIO must
    # remain the same base+offset value.  A mismatched pair must not enter the
    # staged bitmap even when every identity and budget field is otherwise
    # valid.
    clear = action_with_error(board, "CALibration:TRAINing:STAGe:CLEar", args)
    begin = action_with_error(board, stage_begin_command(
        node_count, REQUIRED_EVIDENCE_FLAGS, identity), args)
    link = action_with_error(board, stage_link_command(
        0, REQUIRED_EVIDENCE_FLAGS, offset_phase_mismatch=True), args)
    snapshot = stage_snapshot(board, args)
    arm = arm_must_reject(board, args)
    cases.append({
        "name": "offset_phase_mismatch_rejected",
        "clear": clear, "begin": begin, "link": link,
        "stage": snapshot, "arm": arm,
        "passed": (error_is_rejection(link["error_after"]) and
                   snapshot.get("valid_link_bitmap") == 0 and arm["passed"]),
    })

    final_clear = action_with_error(
        board, "CALibration:TRAINing:STAGe:CLEar", args)
    return {
        "board_id": board.address,
        "physical_node_index": node_index,
        "runtime_identity": identity,
        "setup": setup,
        "cases": cases,
        "final_clear": final_clear,
        "passed": all(case["passed"] for case in cases),
    }


def main() -> int:
    args = parse_args()
    board_ids = list(args.board_id)
    if len(board_ids) < 2 or len(board_ids) > 8:
        raise SystemExit("board count must be in [2, 8]")
    if len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique")
    args.board_ids = board_ids
    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    ordered = [boards[board_id] for board_id in board_ids]
    wrong_build = {board.address: board.build for board in ordered
                   if board.build != args.expected_build}
    if wrong_build:
        raise SystemExit(f"build mismatch: {wrong_build}")

    results: list[dict[str, Any]] = []
    error = ""
    try:
        for node_index, board in enumerate(ordered):
            results.append(run_board(
                board, len(ordered), node_index, args))
    except Exception as exc:  # noqa: BLE001 - preserve partial HIL evidence
        error = f"{type(exc).__name__}: {exc}"
    passed = (not error and len(results) == len(ordered) and
              all(result["passed"] for result in results))
    output = {
        "phase": "TRN-03A-negative-gates",
        "diagnostic_only": True,
        "reusable_as_admission_evidence": False,
        "expected_build": args.expected_build,
        "board_ids_in_physical_node_order": board_ids,
        "passed": passed,
        "error": error,
        "boards": {board.address: asdict(board) for board in ordered},
        "results": results,
    }
    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn03_negative_gates_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"passed": passed, "error": error,
                      "out_dir": str(out_dir)}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
