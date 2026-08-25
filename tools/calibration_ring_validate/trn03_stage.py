#!/usr/bin/env python3
"""Stage and verify a TRN-03 replay matrix on a 2..8 node ring.

The input JSON uses only Calibration node/link/loop terminology.  Existing
TDMA/RefMem slot IDs are not accepted by this tool.  Firmware identity and
budget gates remain authoritative; this host tool only orchestrates SCPI and
persists the evidence.
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
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import (  # noqa: E402
    Board,
    board_command,
    discover,
    status as ring_status,
    wait_started,
)


HEADER_FIELDS = (
    "node_count",
    "evidence_flags",
    "calibration_generation",
    "topology_generation",
    "topology_crc32",
    "profile_crc32",
    "schedule_crc32",
)
LINK_FIELDS = (
    "link_index",
    "evidence_flags",
    "pio_persona",
    "clkdiv_q16",
    "clk_sys_hz",
    "instruction_period_ns",
    "bit_cycles",
    "marker_to_data_cycles",
    "forward_residence_cycles",
    "rx_arm_lead_cycles",
    "codeword_cycles",
    "guard_cycles",
    "link_budget_cycles",
    "loop_delay_cycles",
)
STAGE_QUERY_FIELDS = (
    "tag",
    "enabled",
    "node_count",
    "evidence_flags",
    "complete",
    "calibration_generation",
    "topology_generation",
    "topology_crc32",
    "profile_crc32",
    "schedule_crc32",
    "valid_link_bitmap",
)
LINK_QUERY_FIELDS = (
    "tag",
    "valid",
    "link_index",
    "evidence_flags",
    "calibration_generation",
    "topology_generation",
    "topology_crc32",
    "profile_crc32",
    "schedule_crc32",
    *LINK_FIELDS[2:],
)
REQUIRED_EVIDENCE_FLAGS = 0x1F
DIAGNOSTIC_ONLY_FLAG = 1 << 31


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address in physical node order")
    parser.add_argument("--config", type=Path, required=True,
                        help="TRN-03 replay-matrix JSON")
    parser.add_argument("--expected-build")
    parser.add_argument("--arm-gate", action="store_true",
                        help="ARM every node after the full matrix query")
    parser.add_argument("--clear", action="store_true",
                        help="clear the staged matrix instead of staging")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def integer_field(source: dict[str, Any], field: str) -> int:
    value = source.get(field)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{field} must be an integer")
    if value < 0 or value > 0xFFFFFFFF:
        raise ValueError(f"{field} is outside uint32")
    return value


def load_config(path: Path) -> dict[str, Any]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError("config root must be an object")
    header = {field: integer_field(raw, field) for field in HEADER_FIELDS}
    node_count = header["node_count"]
    if node_count < 2 or node_count > 8:
        raise ValueError("node_count must be in [2, 8]")
    if any(header[field] == 0 for field in HEADER_FIELDS[2:]):
        raise ValueError("all identity fields must be non-zero")
    if ((header["evidence_flags"] & REQUIRED_EVIDENCE_FLAGS) !=
            REQUIRED_EVIDENCE_FLAGS or
            header["evidence_flags"] & DIAGNOSTIC_ONLY_FLAG):
        raise ValueError("stage evidence flags are not TRN-03 eligible")
    raw_links = raw.get("links")
    if not isinstance(raw_links, list) or len(raw_links) != node_count:
        raise ValueError("links must contain exactly node_count entries")
    links: list[dict[str, int]] = []
    for raw_link in raw_links:
        if not isinstance(raw_link, dict):
            raise ValueError("every link must be an object")
        link = {field: integer_field(raw_link, field)
                for field in LINK_FIELDS}
        if ((link["evidence_flags"] & REQUIRED_EVIDENCE_FLAGS) !=
                REQUIRED_EVIDENCE_FLAGS or
                link["evidence_flags"] & DIAGNOSTIC_ONLY_FLAG):
            raise ValueError(
                f"link{link['link_index']} evidence is not TRN-03 eligible")
        required_cycles = sum(link[field] for field in (
            "marker_to_data_cycles",
            "forward_residence_cycles",
            "rx_arm_lead_cycles",
            "codeword_cycles",
            "guard_cycles",
            "loop_delay_cycles",
        ))
        if link["link_budget_cycles"] < required_cycles:
            raise ValueError(
                f"link{link['link_index']} budget expires before replay")
        links.append(link)
    if sorted(link["link_index"] for link in links) != list(range(node_count)):
        raise ValueError("link_index must cover [0, node_count) exactly")
    return {**header, "links": sorted(links, key=lambda item: item["link_index"])}


def stage_begin_command(config: dict[str, Any]) -> str:
    values = ",".join(str(config[field]) for field in HEADER_FIELDS)
    return f"CALibration:TRAINing:STAGe:BEGin {values}"


def stage_link_command(link: dict[str, int]) -> str:
    values = ",".join(str(link[field]) for field in LINK_FIELDS)
    return f"CALibration:TRAINing:STAGe:LINK {values}"


def parse_query(raw: str, fields: tuple[str, ...], tag: str) -> dict[str, Any]:
    row = next(csv.reader([raw]), [])
    if len(row) != len(fields) or row[0].strip().strip('"') != tag:
        raise RuntimeError(f"invalid {tag} response: {raw!r}")
    result: dict[str, Any] = {"tag": tag}
    for field, value in zip(fields[1:], row[1:]):
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


def drain_errors(board: Board, args: argparse.Namespace) -> list[str]:
    errors: list[str] = []
    for _ in range(16):
        raw = board_command(board, "SYSTem:ERR?", args)
        errors.append(raw)
        if error_is_clear(raw):
            return errors
    raise RuntimeError(f"{board.address}: SCPI error queue did not drain")


def checked_action(board: Board, command: str,
                   args: argparse.Namespace) -> dict[str, Any]:
    drained = drain_errors(board, args)
    response = board_command(board, command, args)
    error_after = board_command(board, "SYSTem:ERR?", args)
    evidence = {
        "command": command,
        "response": response,
        "errors_drained_before": drained,
        "error_after": error_after,
    }
    if not error_is_clear(error_after):
        raise RuntimeError(
            f"{board.address}: {command} rejected: {error_after!r}")
    return evidence


def stage_board(board: Board, config: dict[str, Any],
                args: argparse.Namespace) -> dict[str, Any]:
    actions: list[dict[str, Any]] = []
    begin = stage_begin_command(config)
    actions.append(checked_action(board, begin, args))
    for link in config["links"]:
        command = stage_link_command(link)
        actions.append(checked_action(board, command, args))
    stage = parse_query(
        board_command(board, "READ:CALibration:TRAINing:STAGe?", args),
        STAGE_QUERY_FIELDS, "TRN03STG")
    links = []
    for link_index in range(config["node_count"]):
        links.append(parse_query(board_command(
            board,
            f"READ:CALibration:TRAINing:STAGe:LINK? {link_index}",
            args), LINK_QUERY_FIELDS, "TRN03LNK"))
    expected_bitmap = (1 << config["node_count"]) - 1
    stage_matches = all(
        int(stage[field]) == int(config[field]) for field in HEADER_FIELDS)
    links_match = all(
        all(int(observed[field]) == int(expected[field])
            for field in LINK_FIELDS) and
        all(int(observed[field]) == int(config[field])
            for field in HEADER_FIELDS[2:])
        for observed, expected in zip(links, config["links"]))
    return {
        "board_id": board.address,
        "actions": actions,
        "stage": stage,
        "links": links,
        "passed": stage["complete"] == 1 and
                  stage["valid_link_bitmap"] == expected_bitmap and
                  all(link["valid"] == 1 for link in links) and
                  stage_matches and links_match,
    }


def runtime_status_for_node(raw: dict[str, int], node_index: int) -> dict[str, int]:
    """Map the TDMA boundary status into Calibration node terminology."""
    return {
        "node_index": node_index,
        "ring_enabled": raw["ring_enabled"],
        "ring_node_count": raw["ring_node_count"],
        "ring_local_node": raw["ring_local_slot_id"],
        "ring_reference_node": raw["ring_reference_slot_id"],
        "ring_adapter_started": raw["ring_adapter_started"],
        "ring_up_running": raw["ring_up_running"],
        "ring_down_running": raw["ring_down_running"],
    }


def runtime_is_armed(status: dict[str, int], node_count: int,
                     node_index: int) -> bool:
    return (
        status["ring_enabled"] == 1 and
        status["ring_adapter_started"] == 1 and
        status["ring_node_count"] == node_count and
        status["ring_local_node"] == node_index and
        status["ring_reference_node"] == 0
    )


def runtime_is_stopped(status: dict[str, int]) -> bool:
    return status["ring_enabled"] == 0 and \
        status["ring_adapter_started"] == 0


def main() -> int:
    args = parse_args()
    config = load_config(args.config)
    board_ids = list(args.board_id)
    if len(board_ids) != config["node_count"]:
        raise SystemExit("board count must equal config node_count")
    if len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique")
    plan = {
        "phase": "TRN-03A",
        "board_ids_in_physical_node_order": board_ids,
        "config": config,
        "arm_gate": args.arm_gate,
        "clear": args.clear,
    }
    if args.dry_run:
        print(json.dumps(plan, ensure_ascii=False, indent=2))
        return 0

    args.board_ids = board_ids
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

    results: list[dict[str, Any]] = []
    error = ""
    try:
        if args.clear:
            for board in ordered:
                response = board_command(
                    board, "CALibration:TRAINing:STAGe:CLEar", args)
                results.append({"board_id": board.address,
                                "clear_response": response,
                                "passed": True})
        else:
            results = [stage_board(board, config, args) for board in ordered]
            if args.arm_gate and all(result["passed"] for result in results):
                try:
                    for board, result in zip(ordered, results):
                        result["arm_response"] = board_command(
                            board, "SYSTem:TDMA:RING:ARM", args)
                    for node_index, (board, result) in enumerate(
                            zip(ordered, results)):
                        armed = runtime_status_for_node(
                            wait_started(board, args), node_index)
                        result["armed_status"] = armed
                        result["arm_passed"] = runtime_is_armed(
                            armed, len(ordered), node_index)
                finally:
                    for board, result in zip(ordered, results):
                        result["stop_response"] = board_command(
                            board, "SYSTem:TDMA:RING:STOP", args)
                    for node_index, (board, result) in enumerate(
                            zip(ordered, results)):
                        stopped = runtime_status_for_node(
                            ring_status(board, args), node_index)
                        result["stopped_status"] = stopped
                        result["stop_passed"] = runtime_is_stopped(stopped)
    except Exception as exc:  # noqa: BLE001 - persist partial HIL evidence
        error = f"{type(exc).__name__}: {exc}"
    passed = not error and len(results) == len(ordered) and all(
        bool(result.get("passed")) and
        (not args.arm_gate or (
            bool(result.get("arm_passed")) and
            bool(result.get("stop_passed"))))
        for result in results)
    output = {
        **plan,
        "passed": passed,
        "error": error,
        "boards": {board.address: asdict(board) for board in ordered},
        "results": results,
    }
    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn03_stage_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"passed": passed, "error": error,
                      "out_dir": str(out_dir)}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
