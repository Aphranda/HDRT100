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
    "marker_offset_sample_count",
    "sck_offset_sample_count",
    "data_offset_sample_count",
    "sample_period_ns",
    "link_base_delay_ns",
    "marker_phase_delay_cycles",
    "sck_phase_delay_cycles",
    "data_phase_delay_cycles",
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
FLIGHT_SCK_REARM_SAMPLES = 2
FLIGHT_DATA_REARM_SAMPLES = 5
FLIGHT_REFERENCE_NODE = 0


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


def signed_integer_field(source: dict[str, Any], field: str) -> int:
    value = source.get(field)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{field} must be an integer")
    if value < -0x80000000 or value > 0x7FFFFFFF:
        raise ValueError(f"{field} is outside int32")
    return value


def validate_config(raw: object,
                    offset_row_id: int | None = None) -> dict[str, Any]:
    """Validate and resolve exactly one replay row from a full matrix."""
    if not isinstance(raw, dict):
        raise ValueError("config root must be an object")
    header = {field: integer_field(raw, field) for field in HEADER_FIELDS}
    baud_hz = integer_field(raw, "baud_hz")
    if baud_hz == 0:
        raise ValueError("baud_hz must be non-zero")
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
    matrix = raw.get("offset_matrix")
    if not isinstance(matrix, dict):
        raise ValueError("offset_matrix is required for TRN-03")
    rows = matrix.get("rows")
    if (not isinstance(rows, list) or not rows or
            int(matrix.get("full_matrix_row_count", -1)) != len(rows)):
        raise ValueError("offset_matrix rows are incomplete")
    matrix_sample_period_ns = integer_field(matrix, "sample_period_ns")
    row_ids: list[int] = []
    row_signatures: set[tuple[tuple[int, ...], tuple[int, ...],
                              tuple[int, ...]]] = set()
    marker_signature: tuple[int, ...] | None = None
    sck_values_by_node: list[set[int]] = [set() for _ in range(node_count)]
    data_values_by_node: list[set[int]] = [set() for _ in range(node_count)]
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("every offset matrix row must be an object")
        row_id = integer_field(row, "row_id")
        marker_values = row.get("marker_offset_sample_counts_by_node")
        sck_values = row.get("sck_offset_sample_counts_by_node")
        data_values = row.get("data_offset_sample_counts_by_node")
        if (not isinstance(marker_values, list) or
                not isinstance(sck_values, list) or
                not isinstance(data_values, list) or
                len(marker_values) != node_count or
                len(sck_values) != node_count or
                len(data_values) != node_count or
                any(isinstance(value, bool) or not isinstance(value, int) or
                    value < -10 or value > 10
                    for value in marker_values + sck_values + data_values)):
            raise ValueError("offset matrix row dimensions are invalid")
        signature = (tuple(marker_values), tuple(sck_values),
                     tuple(data_values))
        if signature in row_signatures:
            raise ValueError("offset matrix contains a duplicate row")
        if marker_signature is None:
            marker_signature = signature[0]
        elif signature[0] != marker_signature:
            raise ValueError("MARK offsets must remain fixed across TRN-03 rows")
        row_ids.append(row_id)
        row_signatures.add(signature)
        for node in range(node_count):
            sck_values_by_node[node].add(sck_values[node])
            data_values_by_node[node].add(data_values[node])
    if sorted(row_ids) != list(range(len(rows))):
        raise ValueError("offset matrix row_id must cover every row exactly")
    expected_row_count = 1
    for values in (*sck_values_by_node, *data_values_by_node):
        expected_row_count *= len(values)
    if expected_row_count != len(rows):
        raise ValueError("offset matrix is not the full SCK/DATA Cartesian set")
    selected_row_id = (int(matrix.get("active_row_id", -1))
                       if offset_row_id is None else offset_row_id)
    matches = [row for row in rows if isinstance(row, dict) and
               int(row.get("row_id", -1)) == selected_row_id]
    if len(matches) != 1:
        raise ValueError(f"offset matrix row {selected_row_id} is unavailable")
    selected_row = matches[0]
    marker_offsets = selected_row["marker_offset_sample_counts_by_node"]
    sck_offsets = selected_row["sck_offset_sample_counts_by_node"]
    data_offsets = selected_row["data_offset_sample_counts_by_node"]

    links: list[dict[str, int]] = []
    for raw_link in raw_links:
        if not isinstance(raw_link, dict):
            raise ValueError("every link must be an object")
        link = {
            field: (signed_integer_field(raw_link, field)
                    if field in ("marker_offset_sample_count",
                                 "sck_offset_sample_count",
                                 "data_offset_sample_count")
                    else integer_field(raw_link, field))
            for field in LINK_FIELDS
        }
        link_index = link["link_index"]
        if link_index >= node_count:
            raise ValueError("link_index is outside the physical ring")
        marker_source = integer_field(raw_link, "marker_source_node")
        marker_destination = integer_field(
            raw_link, "marker_destination_node")
        data_source = integer_field(raw_link, "data_source_node")
        sck_destination = marker_destination
        data_destination = integer_field(raw_link, "data_destination_node")
        if (marker_source >= node_count or marker_destination >= node_count or
                data_source >= node_count or data_destination >= node_count):
            raise ValueError("offset matrix destination node is invalid")
        expected_next_node = (link_index + 1) % node_count
        if (marker_source != link_index or
                marker_destination != expected_next_node or
                data_source != expected_next_node or
                data_destination != link_index):
            raise ValueError(
                f"link{link_index} Node direction does not match loop order")
        if link["sample_period_ns"] != matrix_sample_period_ns:
            raise ValueError(
                f"link{link_index} sample period does not match offset matrix")
        link["marker_offset_sample_count"] = int(
            marker_offsets[marker_destination])
        link["sck_offset_sample_count"] = int(
            sck_offsets[sck_destination])
        link["data_offset_sample_count"] = int(
            data_offsets[data_destination])
        marker_offset_ns = (
            link["link_base_delay_ns"] +
            link["marker_offset_sample_count"] *
            link["sample_period_ns"])
        sck_offset_ns = (link["link_base_delay_ns"] +
                         link["sck_offset_sample_count"] *
                         link["sample_period_ns"])
        if marker_offset_ns <= 0 or sck_offset_ns <= 0:
            raise ValueError("MARK/SCK phase must be positive")
        link["marker_phase_delay_cycles"] = (
            marker_offset_ns + link["sample_period_ns"] // 2
        ) // link["sample_period_ns"]
        link["sck_phase_delay_cycles"] = (
            sck_offset_ns + link["sample_period_ns"] // 2
        ) // link["sample_period_ns"]
        offset_ns = (link["link_base_delay_ns"] +
                     link["data_offset_sample_count"] *
                     link["sample_period_ns"])
        if offset_ns <= 0:
            raise ValueError(
                "non-positive DATA phase cannot map to delay-only PIO")
        link["data_phase_delay_cycles"] = (
            offset_ns + link["sample_period_ns"] // 2
        ) // link["sample_period_ns"]
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
        if link["sample_period_ns"] == 0 or \
                link["marker_phase_delay_cycles"] > 31 or \
                link["sck_phase_delay_cycles"] > 31 or \
                link["data_phase_delay_cycles"] > 31:
            raise ValueError(
                f"link{link['link_index']} PIO phase mapping is invalid")
        period_samples = 1_000_000_000 // (
            baud_hz * link["sample_period_ns"])
        half_period_samples = period_samples // 2
        if (marker_destination != FLIGHT_REFERENCE_NODE and
                link["sck_phase_delay_cycles"] +
                FLIGHT_SCK_REARM_SAMPLES > half_period_samples):
            raise ValueError(
                f"link{link['link_index']} SCK replay phase cannot re-arm "
                "before the opposite edge")
        if (link["data_phase_delay_cycles"] +
                FLIGHT_DATA_REARM_SAMPLES > period_samples):
            raise ValueError(
                f"link{link['link_index']} DATA replay phase cannot re-arm "
                "before the next symbol")
        links.append(link)
    if sorted(link["link_index"] for link in links) != list(range(node_count)):
        raise ValueError("link_index must cover [0, node_count) exactly")
    ordered_links = sorted(links, key=lambda item: item["link_index"])
    for node in range(node_count):
        marker_link = ordered_links[(node + node_count - 1) % node_count]
        data_link = ordered_links[node]
        node_half_period_samples = 1_000_000_000 // (
            baud_hz * data_link["sample_period_ns"]) // 2
        if (data_link["data_phase_delay_cycles"] <=
                marker_link["sck_phase_delay_cycles"] + 1):
            raise ValueError(
                f"node{node} DATA phase must leave one serial PIO cycle "
                "after its incoming SCK phase")
        if (data_link["data_phase_delay_cycles"] + 2 >
                marker_link["sck_phase_delay_cycles"] +
                node_half_period_samples):
            raise ValueError(
                f"node{node} raw follower cannot preserve SCK duty after "
                "the DATA sample")
    return {
        **header,
        "baud_hz": baud_hz,
        "offset_row_id": selected_row_id,
        "offset_row": selected_row,
        "links": ordered_links,
    }


def load_config(path: Path, offset_row_id: int | None = None) -> dict[str, Any]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    return validate_config(raw, offset_row_id)


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
