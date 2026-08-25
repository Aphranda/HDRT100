#!/usr/bin/env python3
"""Aggregate TRN-02 profile evidence without manually interpreting reports."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def _load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: summary must be an object")
    return value


def _identity(summary: dict[str, Any]) -> dict[str, int]:
    matrix = summary.get("matrix", {})
    identity = matrix.get("identity", {})
    result: dict[str, int] = {}
    for field in ("calibration_generation", "topology_generation",
                  "topology_crc32", "profile_crc32", "schedule_crc32",
                  "sample_period_ns"):
        values = identity.get(field, [])
        if not isinstance(values, list) or len(values) != 1:
            raise ValueError(f"{field}: expected one identity value")
        result[field] = int(values[0])
    return result


def _check_residence(path: Path, expected: dict[str, int]) -> list[str]:
    summary = _load(path)
    errors: list[str] = []
    records = summary.get("records", [])
    if not isinstance(records, list) or len(records) == 0:
        return ["residence_records_missing"]
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            errors.append(f"residence_record_{index}_invalid")
            continue
        for field in ("topology_generation", "topology_crc32", "profile_crc32",
                      "schedule_crc32", "calibration_generation"):
            if field in expected and int(record.get(field, -1)) != expected[field]:
                errors.append(f"residence_{field}_mismatch")
        if int(record.get("forward_residence_ticks", 0)) < 0:
            errors.append("residence_negative")
    return sorted(set(errors))


def aggregate(profiles: list[tuple[int, Path]], residence: Path | None) -> dict[str, Any]:
    errors: list[str] = []
    reports: list[dict[str, Any]] = []
    identities: list[dict[str, int]] = []
    for level, path in profiles:
        summary = _load(path)
        matrix = summary.get("matrix", {})
        if summary.get("phase") != "TRN-02D_REPEAT_MATRIX":
            errors.append(f"level{level}:phase")
        if not bool(summary.get("passed")) or not bool(matrix.get("passed")):
            errors.append(f"level{level}:summary_not_passed")
        if int(matrix.get("expected_trial_count", -1)) != 12 or \
                int(matrix.get("accepted_count", -1)) != 12:
            errors.append(f"level{level}:accepted_count")
        identity = _identity(summary)
        identities.append(identity)
        reports.append({"level": level, "path": str(path), "identity": identity,
                        "matrix": matrix})

    if len({item["profile_crc32"] for item in identities}) != len(identities):
        errors.append("profile_crc_not_distinct")
    for field in ("topology_crc32", "sample_period_ns"):
        if len({item[field] for item in identities}) != 1:
            errors.append(f"mixed_{field}")

    residence_result: dict[str, Any]
    if residence is None:
        errors.append("forward_residence_evidence_missing")
        residence_result = {"path": None, "passed": False, "errors":
                            ["forward_residence_evidence_missing"]}
    else:
        residence_errors: list[str] = []
        for identity in identities:
            residence_errors.extend(_check_residence(residence, identity))
        residence_result = {"path": str(residence), "passed": not residence_errors,
                            "errors": sorted(set(residence_errors))}
        errors.extend(residence_result["errors"])

    return {"phase": "TRN-02_PROFILE_GATE", "profile_count": len(profiles),
            "profiles": reports, "residence": residence_result,
            "gate_failures": sorted(set(errors)), "passed": not errors}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", action="append", required=True,
                        metavar="LEVEL=SUMMARY_JSON")
    parser.add_argument("--residence", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    profiles: list[tuple[int, Path]] = []
    for item in args.profile:
        level_text, separator, path_text = item.partition("=")
        if not separator:
            parser.error("--profile requires LEVEL=SUMMARY_JSON")
        profiles.append((int(level_text), Path(path_text)))
    result = aggregate(profiles, args.residence)
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    print(encoded, end="")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encoded, encoding="utf-8")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
