#!/usr/bin/env python3
"""Import a gated TRN-03C candidate into the CalibrationManager owner.

The candidate is converted into the exact board snapshot layout before any
SCPI write.  The tool stages every board and verifies BEGIN/LINK/FINALIZE
readback; activation is a separate explicit operation.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
import time
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Any, Sequence

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import Board, board_command, discover  # noqa: E402
from tools.calibration_ring_validate.trn03_candidate import (  # noqa: E402
    CANDIDATE_SCHEMA,
    candidate_crc32_valid,
)
from tools.calibration_ring_validate.trn03_stage import (  # noqa: E402
    checked_action,
)


PATH_SNAPSHOT_VERSION = 2
PATH_MAX_LINKS = 8
PATH_CANDIDATE_FLAGS = 0x17D
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619

HEADER_FIELDS = (
    "link_count",
    "topology_generation",
    "topology_crc32",
    "bias_generation",
    "profile_crc32",
    "schedule_crc32",
    "calibration_generation",
    "freshness_us",
    "evidence_age_us",
    "ring_round_trip_ns",
    "forwarding_residence_ns",
    "max_residual_ns",
    "max_jitter_ns",
    "max_asymmetry_ns",
    "expected_table_crc32",
)
LINK_FIELDS = (
    "link_index",
    "source_node",
    "destination_node",
    "profile_crc32",
    "topology_generation",
    "bias_generation",
    "sample_count",
    "accepted_count",
    "jitter_ns",
    "asymmetry_ns",
    "residence_ns",
    "raw_path_sum_ns",
    "corrected_path_sum_ns",
    "delay_estimate_ns",
    "clock_rate_error_bound_ns",
    "reject_reason",
    "reference_accepted",
    "active_eligible",
)
CANDIDATE_QUERY_FIELDS = (
    "tag", "import_active", "complete", "candidate_valid", "link_count",
    "valid_link_bitmap", "reject_reason", "topology_generation",
    "topology_crc32", "bias_generation", "profile_crc32", "schedule_crc32",
    "calibration_generation", "freshness_us", "evidence_age_us",
    "ring_round_trip_ns", "forwarding_residence_ns",
    "expected_table_crc32", "calculated_table_crc32", "candidate_table_crc32",
)
LINK_QUERY_FIELDS = (
    "tag", "valid", *LINK_FIELDS,
)
SNAPSHOT_QUERY_FIELDS = (
    "tag", "valid", "link_count", "topology_generation", "topology_crc32",
    "bias_generation", "profile_crc32", "schedule_crc32",
    "calibration_generation", "freshness_us", "cumulative_delay_ns",
    "forwarding_residence_ns", "predicted_ring_round_trip_ns",
    "ring_round_trip_ns", "residual_ns", "table_crc32",
)


def _uint(value: object, label: str, bits: int = 32) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer")
    maximum = (1 << bits) - 1
    if value < 0 or value > maximum:
        raise ValueError(f"{label} is outside uint{bits}")
    return value


def _int64(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer")
    if value < -(1 << 63) or value > (1 << 63) - 1:
        raise ValueError(f"{label} is outside int64")
    return value


def _integral(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be numeric")
    numeric = float(value)
    if not math.isfinite(numeric) or not numeric.is_integer():
        raise ValueError(f"{label} must resolve to whole nanoseconds")
    return int(numeric)


def _nonnegative_number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be numeric")
    numeric = float(value)
    if not math.isfinite(numeric) or numeric < 0:
        raise ValueError(f"{label} must be finite and nonnegative")
    return numeric


def _median_ns(values: object, label: str) -> int:
    if not isinstance(values, list) or not values:
        raise ValueError(f"{label} must contain repeated values")
    return _integral(statistics.median(values), label)


def _hash_u32(state: int, value: int) -> int:
    value &= 0xFFFFFFFF
    for shift in range(0, 32, 8):
        state ^= (value >> shift) & 0xFF
        state = (state * FNV_PRIME) & 0xFFFFFFFF
    return state


def _hash_u64(state: int, value: int) -> int:
    value &= 0xFFFFFFFFFFFFFFFF
    state = _hash_u32(state, value & 0xFFFFFFFF)
    return _hash_u32(state, value >> 32)


def snapshot_crc32(snapshot: dict[str, Any]) -> int:
    state = FNV_OFFSET
    for field in (
            "version", "valid", "active", "flags", "link_count",
            "topology_generation", "topology_crc32", "bias_generation",
            "profile_crc32", "schedule_crc32", "calibration_generation",
            "freshness_us"):
        state = _hash_u32(state, int(snapshot[field]))
    for field in (
            "cumulative_delay_ns", "forwarding_residence_ns",
            "predicted_ring_round_trip_ns", "ring_round_trip_ns",
            "residual_ns"):
        state = _hash_u64(state, int(snapshot[field]))
    links = list(snapshot["links"])
    for index in range(PATH_MAX_LINKS):
        link = links[index] if index < len(links) else {
            field: 0 for field in LINK_FIELDS
        }
        for field in (
                "source_node", "destination_node", "profile_crc32",
                "topology_generation", "bias_generation", "sample_count",
                "accepted_count", "jitter_ns", "asymmetry_ns"):
            state = _hash_u32(state, int(link[field]))
        for field in (
                "residence_ns", "raw_path_sum_ns", "delay_estimate_ns",
                "corrected_path_sum_ns", "clock_rate_error_bound_ns"):
            state = _hash_u64(state, int(link[field]))
        for field in ("reject_reason", "reference_accepted",
                      "active_eligible"):
            state = _hash_u32(state, int(link[field]))
    return state


def _freshness(candidate: dict[str, Any], now: float) -> tuple[int, int]:
    freshness = candidate.get("freshness")
    if not isinstance(freshness, dict):
        raise ValueError("candidate freshness is missing")
    maximum_seconds = freshness.get("maximum_evidence_age_seconds")
    ages = freshness.get("evidence_ages_seconds")
    generated = candidate.get("generated_unix_seconds")
    if not isinstance(ages, list) or not ages:
        raise ValueError("candidate freshness fields are invalid")
    maximum = _nonnegative_number(
        maximum_seconds, "maximum_evidence_age_seconds")
    generated_seconds = _nonnegative_number(
        generated, "generated_unix_seconds")
    now_seconds = _nonnegative_number(now, "current_unix_seconds")
    evidence_ages = [
        _nonnegative_number(value, f"evidence_ages_seconds[{index}]")
        for index, value in enumerate(ages)
    ]
    elapsed = max(0.0, now_seconds - generated_seconds)
    evidence_age = max(evidence_ages) + elapsed
    if maximum <= 0 or evidence_age > maximum:
        raise ValueError("candidate evidence is stale")
    freshness_us = math.floor(maximum * 1_000_000)
    evidence_age_us = math.ceil(evidence_age * 1_000_000)
    if freshness_us > 0xFFFFFFFF or evidence_age_us > 0xFFFFFFFF:
        raise ValueError("candidate freshness exceeds board uint32 range")
    return freshness_us, evidence_age_us


def candidate_to_import(candidate: dict[str, Any], *,
                        now: float | None = None) -> dict[str, Any]:
    if (candidate.get("schema") != CANDIDATE_SCHEMA or
            candidate.get("state") not in ("active_candidate", "staged") or
            not bool(candidate.get("passed")) or
            bool(candidate.get("active")) or
            not candidate_crc32_valid(candidate)):
        raise ValueError("candidate is not an admissible inactive package")
    link_count = _uint(candidate.get("node_count"), "node_count")
    if link_count < 2 or link_count > PATH_MAX_LINKS:
        raise ValueError("node_count must be in [2, 8]")
    board_order = candidate.get("board_ids_in_physical_node_order")
    if (not isinstance(board_order, list) or len(board_order) != link_count or
            len(set(map(str, board_order))) != link_count):
        raise ValueError("candidate board order is invalid")
    raw_links = candidate.get("path_links")
    if not isinstance(raw_links, list) or len(raw_links) != link_count:
        raise ValueError("candidate path_links are incomplete")
    indexed: dict[int, dict[str, Any]] = {}
    for position, link in enumerate(raw_links):
        if not isinstance(link, dict):
            raise ValueError(f"path_links[{position}] is not an object")
        index = _uint(link.get("link_index"),
                      f"path_links[{position}].link_index")
        if index in indexed:
            raise ValueError(f"path link index {index} is duplicated")
        indexed[index] = link
    if sorted(indexed) != list(range(link_count)):
        raise ValueError("candidate path link indices are incomplete")

    identity = {
        field: _uint(candidate.get(field), field)
        for field in ("topology_generation", "topology_crc32",
                      "bias_generation", "profile_crc32", "schedule_crc32",
                      "calibration_generation")
    }
    if any(value == 0 for value in identity.values()):
        raise ValueError("candidate identity contains zero")
    thresholds = candidate.get("thresholds")
    whole_ring = candidate.get("whole_ring_path")
    if not isinstance(thresholds, dict) or not isinstance(whole_ring, dict):
        raise ValueError("candidate activation thresholds are incomplete")
    if whole_ring.get("persona") != "P3_PATH_V1":
        raise ValueError("whole-ring evidence is not from P3_PATH_V1")

    links: list[dict[str, int]] = []
    for index in range(link_count):
        source = indexed[index]
        link = {
            "link_index": index,
            "source_node": _uint(source.get("source_node"),
                                 f"link{index}.source_node"),
            "destination_node": _uint(source.get("destination_node"),
                                      f"link{index}.destination_node"),
            "profile_crc32": identity["profile_crc32"],
            "topology_generation": identity["topology_generation"],
            "bias_generation": identity["bias_generation"],
            "sample_count": _uint(source.get("sample_count"),
                                  f"link{index}.sample_count"),
            "accepted_count": _uint(source.get("accepted_count"),
                                    f"link{index}.accepted_count"),
            "jitter_ns": _uint(
                _integral(source.get("jitter_span_ns"),
                          f"link{index}.jitter_ns"),
                f"link{index}.jitter_ns"),
            "asymmetry_ns": _uint(source.get("asymmetry_ns"),
                                  f"link{index}.asymmetry_ns"),
            "residence_ns": _median_ns(source.get("residence_ns"),
                                       f"link{index}.residence_ns"),
            "raw_path_sum_ns": _median_ns(source.get("raw_path_sum_ns"),
                                          f"link{index}.raw_path_sum_ns"),
            "corrected_path_sum_ns": _median_ns(
                source.get("corrected_path_sum_ns"),
                f"link{index}.corrected_path_sum_ns"),
            "delay_estimate_ns": _integral(source.get("delay_ns"),
                                           f"link{index}.delay_ns"),
            "clock_rate_error_bound_ns": _uint(
                source.get("clock_rate_error_bound_ns"),
                f"link{index}.clock_rate_error_bound_ns", 64),
            "reject_reason": 0,
            "reference_accepted": 1,
            "active_eligible": 1,
        }
        if (link["source_node"] != index or
                link["destination_node"] != (index + 1) % link_count or
                link["sample_count"] == 0 or
                link["accepted_count"] != link["sample_count"] or
                link["residence_ns"] < 0 or link["raw_path_sum_ns"] < 0 or
                link["corrected_path_sum_ns"] < 0 or
                link["delay_estimate_ns"] < 0):
            raise ValueError(f"link{index} is not active eligible")
        links.append(link)

    cumulative_delay_ns = sum(link["delay_estimate_ns"] for link in links)
    forwarding_residence_ns = _uint(
        whole_ring.get("forwarding_residence_ns"),
        "whole_ring.forwarding_residence_ns", 64)
    ring_round_trip_ns = _uint(
        whole_ring.get("ring_round_trip_ns"),
        "whole_ring.ring_round_trip_ns", 64)
    predicted = cumulative_delay_ns + forwarding_residence_ns
    residual = abs(ring_round_trip_ns - predicted)
    if (forwarding_residence_ns == 0 or
            _uint(whole_ring.get("cumulative_delay_ns"),
                  "whole_ring.cumulative_delay_ns", 64) != cumulative_delay_ns or
            _uint(whole_ring.get("predicted_ring_round_trip_ns"),
                  "whole_ring.predicted_ring_round_trip_ns", 64) != predicted or
            _uint(whole_ring.get("residual_ns"),
                  "whole_ring.residual_ns", 64) != residual):
        raise ValueError("whole-ring path replay does not match link evidence")
    max_residual_ns = _uint(
        thresholds.get("maximum_whole_ring_residual_ns"),
        "maximum_whole_ring_residual_ns")
    max_jitter_ns = _uint(
        _integral(thresholds.get("maximum_path_jitter_ns"),
                  "maximum_path_jitter_ns"),
        "maximum_path_jitter_ns")
    max_asymmetry_ns = _uint(
        thresholds.get("maximum_path_asymmetry_ns"),
        "maximum_path_asymmetry_ns")
    if (max_residual_ns == 0 or max_jitter_ns == 0 or
            max_asymmetry_ns == 0 or residual > max_residual_ns or
            any(link["jitter_ns"] > max_jitter_ns or
                link["asymmetry_ns"] > max_asymmetry_ns for link in links)):
        raise ValueError("candidate path quality exceeds activation thresholds")
    freshness_us, evidence_age_us = _freshness(
        candidate, time.time() if now is None else now)

    snapshot = {
        "version": PATH_SNAPSHOT_VERSION,
        "valid": 1,
        "active": 0,
        "flags": PATH_CANDIDATE_FLAGS,
        "link_count": link_count,
        **identity,
        "freshness_us": freshness_us,
        "cumulative_delay_ns": cumulative_delay_ns,
        "forwarding_residence_ns": forwarding_residence_ns,
        "predicted_ring_round_trip_ns": predicted,
        "ring_round_trip_ns": ring_round_trip_ns,
        "residual_ns": residual,
        "links": links,
    }
    expected_crc32 = snapshot_crc32(snapshot)
    header = {
        "link_count": link_count,
        **identity,
        "freshness_us": freshness_us,
        "evidence_age_us": evidence_age_us,
        "ring_round_trip_ns": ring_round_trip_ns,
        "forwarding_residence_ns": forwarding_residence_ns,
        "max_residual_ns": max_residual_ns,
        "max_jitter_ns": max_jitter_ns,
        "max_asymmetry_ns": max_asymmetry_ns,
        "expected_table_crc32": expected_crc32,
    }
    return {
        "schema": "HAOFV_TRN03_BOARD_IMPORT_V1",
        "board_ids_in_physical_node_order": [str(value) for value in board_order],
        "header": header,
        "links": links,
        "expected_snapshot": {**snapshot, "table_crc32": expected_crc32},
        "source_candidate_crc32": int(candidate["candidate_crc32"]),
    }


def begin_command(package: dict[str, Any]) -> str:
    header = package["header"]
    return "CALibration:PATH:CANDidate:BEGin " + ",".join(
        str(header[field]) for field in HEADER_FIELDS)


def link_command(link: dict[str, int]) -> str:
    return "CALibration:PATH:CANDidate:LINK " + ",".join(
        str(link[field]) for field in LINK_FIELDS)


def parse_query(raw: str, fields: Sequence[str], tag: str) -> dict[str, int | str]:
    row = next(csv.reader([raw]), [])
    if len(row) != len(fields) or row[0].strip().strip('"') != tag:
        raise RuntimeError(f"invalid {tag} response: {raw!r}")
    parsed: dict[str, int | str] = {"tag": tag}
    for field, value in zip(fields[1:], row[1:]):
        parsed[field] = int(value.strip().strip('"'), 0)
    return parsed


def _query(board: Board, command: str, fields: Sequence[str], tag: str,
           args: argparse.Namespace) -> dict[str, int | str]:
    return parse_query(board_command(board, command, args), fields, tag)


def stage_board(board: Board, package: dict[str, Any],
                args: argparse.Namespace) -> dict[str, Any]:
    actions: list[dict[str, Any]] = []
    actions.append(checked_action(
        board, "CALibration:PATH:CANDidate:CLEar", args))
    actions.append(checked_action(board, begin_command(package), args))
    begin_readback = _query(
        board, "READ:CALibration:PATH:CANDidate?",
        CANDIDATE_QUERY_FIELDS, "CALPATHCAND", args)
    if (begin_readback["import_active"] != 1 or
            begin_readback["valid_link_bitmap"] != 0):
        raise RuntimeError(f"{board.address}: BEGIN readback mismatch")
    link_readbacks: list[dict[str, int | str]] = []
    for link in package["links"]:
        actions.append(checked_action(board, link_command(link), args))
        readback = _query(
            board, f"READ:CALibration:PATH:CANDidate:LINK? {link['link_index']}",
            LINK_QUERY_FIELDS, "CALPATHLINK", args)
        for field in LINK_FIELDS:
            if int(readback[field]) != int(link[field]):
                raise RuntimeError(
                    f"{board.address}: link{link['link_index']} {field} "
                    "readback mismatch")
        link_readbacks.append(readback)
    before_finalize = _query(
        board, "READ:CALibration:PATH:CANDidate?",
        CANDIDATE_QUERY_FIELDS, "CALPATHCAND", args)
    actions.append(checked_action(
        board, "CALibration:PATH:CANDidate:FINalize", args))
    after_finalize = _query(
        board, "READ:CALibration:PATH:CANDidate?",
        CANDIDATE_QUERY_FIELDS, "CALPATHCAND", args)
    expected_crc = int(package["header"]["expected_table_crc32"])
    expected_bitmap = (1 << int(package["header"]["link_count"])) - 1
    if (before_finalize["complete"] != 1 or
            before_finalize["valid_link_bitmap"] != expected_bitmap or
            after_finalize["import_active"] != 0 or
            after_finalize["complete"] != 1 or
            after_finalize["candidate_valid"] != 1 or
            after_finalize["valid_link_bitmap"] != expected_bitmap or
            after_finalize["calculated_table_crc32"] != expected_crc or
            after_finalize["candidate_table_crc32"] != expected_crc):
        raise RuntimeError(f"{board.address}: FINALIZE readback mismatch")
    return {
        "board_id": board.address,
        "actions": actions,
        "begin_readback": begin_readback,
        "link_readbacks": link_readbacks,
        "before_finalize": before_finalize,
        "after_finalize": after_finalize,
        "passed": True,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--board-id", action="append",
                        help="exact *IDN? address in physical Node order")
    parser.add_argument("--expected-build")
    parser.add_argument("--activate", action="store_true")
    parser.add_argument("--rollback", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    candidate = json.loads(args.candidate.read_text(encoding="utf-8"))
    package = candidate_to_import(candidate)
    board_ids = list(args.board_id or
                     package["board_ids_in_physical_node_order"])
    if board_ids != package["board_ids_in_physical_node_order"]:
        raise SystemExit("--board-id order must match candidate physical Node order")
    if args.rollback and not args.activate:
        raise SystemExit("--rollback requires --activate")
    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn03c_candidate_import_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "board_import.json").write_text(
        json.dumps(package, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    if args.dry_run:
        commands = [begin_command(package),
                    *(link_command(link) for link in package["links"]),
                    "CALibration:PATH:CANDidate:FINalize"]
        (out_dir / "commands.json").write_text(
            json.dumps(commands, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        print(json.dumps({"passed": True, "dry_run": True,
                          "out_dir": str(out_dir)}, ensure_ascii=False))
        return 0
    if not args.expected_build:
        raise SystemExit("--expected-build is required for hardware import")

    args.board_ids = board_ids
    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found: {', '.join(sorted(missing))}")
    ordered = [boards[board_id] for board_id in board_ids]
    wrong_build = {board.address: board.build for board in ordered
                   if board.build != args.expected_build}
    if wrong_build:
        raise SystemExit(f"build mismatch: {wrong_build}")

    results: list[dict[str, Any]] = []
    error = ""
    try:
        for board in ordered:
            results.append(stage_board(board, package, args))
        if args.activate:
            for result, board in zip(results, ordered):
                result["activate"] = checked_action(
                    board, "CALibration:ACTivate", args)
                result["active"] = _query(
                    board, "READ:CALibration:PATH:ACTive?",
                    SNAPSHOT_QUERY_FIELDS, "CALPATHACTIVE", args)
                if (result["active"]["valid"] != 1 or
                        result["active"]["table_crc32"] !=
                        package["header"]["expected_table_crc32"]):
                    raise RuntimeError(
                        f"{board.address}: active snapshot mismatch")
            if args.rollback:
                for result, board in zip(results, ordered):
                    result["rollback"] = checked_action(
                        board, "CALibration:ROLLback", args)
                    result["active_after_rollback"] = _query(
                        board, "READ:CALibration:PATH:ACTive?",
                        SNAPSHOT_QUERY_FIELDS, "CALPATHACTIVE", args)
    except Exception as exc:  # noqa: BLE001 - retain partial board evidence
        error = f"{type(exc).__name__}: {exc}"
    passed = not error and len(results) == len(ordered) and all(
        result.get("passed") for result in results)
    summary = {
        "phase": "TRN-03C_CANDIDATE_IMPORT",
        "passed": passed,
        "error": error,
        "candidate": str(args.candidate),
        "expected_build": args.expected_build,
        "board_ids_in_physical_node_order": board_ids,
        "boards": {board.address: asdict(board) for board in ordered},
        "package": package,
        "results": results,
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(json.dumps({"passed": passed, "error": error,
                      "out_dir": str(out_dir)}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
