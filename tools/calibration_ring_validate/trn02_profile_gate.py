#!/usr/bin/env python3
"""Aggregate profile-bound TRN-02 DATA and residence evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


IDENTITY_FIELDS = (
    "calibration_generation",
    "topology_generation",
    "topology_crc32",
    "profile_crc32",
    "schedule_crc32",
)


def load_summary(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: summary must be an object")
    return value


def singleton_identity(summary: dict[str, Any], *, residence: bool = False
                       ) -> dict[str, int]:
    matrix = summary.get("matrix", {})
    identity = matrix.get("identity", {})
    result: dict[str, int] = {}
    for field in IDENTITY_FIELDS:
        if field == "topology_generation" and field not in identity:
            per_node = identity.get("topology_generation_by_node")
            expected_nodes = {
                str(node) for node in range(len(node_order(summary)))}
            if (not isinstance(per_node, dict) or
                    set(per_node) != expected_nodes):
                raise ValueError(
                    "topology_generation_by_node: expected every Node")
            node_values: list[int] = []
            for node in sorted(expected_nodes, key=int):
                values = per_node[node]
                if not isinstance(values, list) or len(values) != 1:
                    raise ValueError(
                        f"topology_generation_by_node[{node}]: "
                        "expected one identity value")
                node_values.append(int(values[0]))
            if len(set(node_values)) != 1:
                raise ValueError(
                    "topology_generation_by_node: mixed identity values")
            result[field] = node_values[0]
            continue
        values = identity.get(field, [])
        if not isinstance(values, list) or len(values) != 1:
            raise ValueError(f"{field}: expected one identity value")
        result[field] = int(values[0])
    resolution_field = "tick_resolution_ns" if residence else "sample_period_ns"
    values = identity.get(resolution_field, [])
    if not isinstance(values, list) or len(values) != 1:
        raise ValueError(f"{resolution_field}: expected one identity value")
    result["sample_period_ns"] = int(values[0])
    return result


def node_order(summary: dict[str, Any]) -> list[str]:
    value = summary.get("node_ids_in_loop_order")
    if (not isinstance(value, list) or not 2 <= len(value) <= 8 or
            any(not isinstance(item, str) or not item for item in value) or
            len(set(value)) != len(value)):
        raise ValueError("node_ids_in_loop_order must contain 2..8 unique IDs")
    return list(value)


def validate_data_summary(summary: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    matrix = summary.get("matrix", {})
    try:
        count = len(node_order(summary))
    except ValueError:
        return ["data_node_order"]
    if summary.get("phase") != "TRN-02D_REPEAT_MATRIX":
        errors.append("data_phase")
    if not bool(summary.get("passed")) or not isinstance(matrix, dict) or \
            not bool(matrix.get("passed")):
        errors.append("data_summary_not_passed")
    repeats = int(summary.get("repeats", 0))
    expected = count * repeats
    if repeats < 3:
        errors.append("data_repeat_count")
    if (int(matrix.get("expected_trial_count", -1)) != expected or
            int(matrix.get("trial_count", -1)) != expected or
            int(matrix.get("accepted_count", -1)) != expected):
        errors.append("data_accepted_count")
    if matrix.get("identity_failures"):
        errors.append("data_identity_failures")
    if matrix.get("gate_failures"):
        errors.append("data_gate_failures")
    links = matrix.get("links", [])
    if not isinstance(links, list) or len(links) != count:
        errors.append("data_links")
    else:
        indices = []
        for link in links:
            if not isinstance(link, dict):
                errors.append("data_link_invalid")
                continue
            index = int(link.get("link", -1))
            indices.append(index)
            if (not bool(link.get("passed")) or
                    int(link.get("trial_count", -1)) != repeats or
                    int(link.get("accepted_count", -1)) != repeats or
                    int(link.get("offset_span_sample", 999)) >
                    int(summary.get("max_offset_span_sample", -1)) or
                    link.get("gate_failures")):
                errors.append(f"data_link{index}")
        if sorted(indices) != list(range(count)):
            errors.append("data_link_indices")
    return sorted(set(errors))


def validate_residence_summary(summary: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    matrix = summary.get("matrix", {})
    try:
        count = len(node_order(summary))
    except ValueError:
        return ["residence_node_order"]
    if summary.get("phase") != "TRN-01_RESIDENCE_MATRIX":
        errors.append("residence_phase")
    if not bool(summary.get("passed")) or not isinstance(matrix, dict) or \
            not bool(matrix.get("passed")):
        errors.append("residence_summary_not_passed")
    if int(summary.get("trial_count", -1)) != count:
        errors.append("residence_trial_count")
    if matrix.get("failures"):
        errors.append("residence_failures")
    links = matrix.get("links", [])
    expected_repeats = count - 1
    if not isinstance(links, list) or len(links) != count:
        errors.append("residence_links")
    else:
        indices = []
        for link in links:
            if not isinstance(link, dict):
                errors.append("residence_link_invalid")
                continue
            index = int(link.get("link_index", -1))
            indices.append(index)
            values = link.get("forward_residence_ticks", [])
            if (not bool(link.get("passed")) or
                    not isinstance(values, list) or
                    len(values) != expected_repeats or
                    int(link.get("repeat_count", -1)) != expected_repeats or
                    int(link.get("forward_residence_span_ticks", 999)) > 1 or
                    int(link.get("selected_forward_residence_ticks", -1)) < 0):
                errors.append(f"residence_link{index}")
        if sorted(indices) != list(range(count)):
            errors.append("residence_link_indices")
    loops = matrix.get("loops", [])
    if not isinstance(loops, list) or len(loops) != count:
        errors.append("residence_loops")
    else:
        loop_nodes = []
        for loop in loops:
            if not isinstance(loop, dict):
                errors.append("residence_loop_invalid")
                continue
            node = int(loop.get("node", -1))
            loop_nodes.append(node)
            values = loop.get("loop_delay_ticks", [])
            if (not isinstance(values, list) or not values or
                    any(int(value) <= 0 for value in values)):
                errors.append(f"residence_loop{node}")
        if sorted(loop_nodes) != list(range(count)):
            errors.append("residence_loop_indices")
    return sorted(set(errors))


def validate_profile_pair(data: dict[str, Any], residence: dict[str, Any]
                          ) -> tuple[dict[str, int], list[str]]:
    errors = validate_data_summary(data) + validate_residence_summary(residence)
    data_identity = singleton_identity(data)
    residence_identity = singleton_identity(residence, residence=True)
    for field in (*IDENTITY_FIELDS, "sample_period_ns"):
        if data_identity[field] != residence_identity[field]:
            errors.append(f"residence_{field}_mismatch")
    if node_order(data) != node_order(residence):
        errors.append("residence_node_order_mismatch")
    return data_identity, sorted(set(errors))


def aggregate(profiles: list[tuple[int, Path]],
              residences: list[tuple[int, Path]]) -> dict[str, Any]:
    errors: list[str] = []
    reports: list[dict[str, Any]] = []
    profile_paths = dict(profiles)
    residence_paths = dict(residences)
    if len(profile_paths) != len(profiles):
        errors.append("duplicate_profile_level")
    if len(residence_paths) != len(residences):
        errors.append("duplicate_residence_level")
    if set(profile_paths) != set(residence_paths):
        errors.append("profile_residence_level_mismatch")

    identities: list[dict[str, int]] = []
    for level in sorted(set(profile_paths) & set(residence_paths)):
        data = load_summary(profile_paths[level])
        residence = load_summary(residence_paths[level])
        identity, pair_errors = validate_profile_pair(data, residence)
        identities.append(identity)
        errors.extend(f"level{level}:{error}" for error in pair_errors)
        reports.append({
            "level": level,
            "data_path": str(profile_paths[level]),
            "residence_path": str(residence_paths[level]),
            "identity": identity,
            "node_count": len(node_order(data)),
            "passed": not pair_errors,
            "gate_failures": pair_errors,
        })

    if len(identities) != 3:
        errors.append("profile_count")
    if identities and len({item["profile_crc32"] for item in identities}) != \
            len(identities):
        errors.append("profile_crc_not_distinct")
    for field in ("topology_crc32", "sample_period_ns"):
        if identities and len({item[field] for item in identities}) != 1:
            errors.append(f"mixed_{field}")
    return {
        "phase": "TRN-02_PROFILE_GATE",
        "profile_count": len(reports),
        "profiles": reports,
        "gate_failures": sorted(set(errors)),
        "passed": not errors,
    }


def parse_level_path(items: list[str], option: str) -> list[tuple[int, Path]]:
    result: list[tuple[int, Path]] = []
    for item in items:
        level_text, separator, path_text = item.partition("=")
        if not separator:
            raise ValueError(f"{option} requires LEVEL=SUMMARY_JSON")
        result.append((int(level_text), Path(path_text)))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", action="append", required=True,
                        metavar="LEVEL=SUMMARY_JSON")
    parser.add_argument("--residence", action="append", required=True,
                        metavar="LEVEL=RESIDENCE_SUMMARY_JSON")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        result = aggregate(
            parse_level_path(args.profile, "--profile"),
            parse_level_path(args.residence, "--residence"),
        )
    except ValueError as exc:
        parser.error(str(exc))
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    print(encoded, end="")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encoded, encoding="utf-8")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
