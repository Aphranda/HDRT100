#!/usr/bin/env python3
"""Build a deterministic index for a directory of SYNC_IO analyzer segments.

The index is an offline SD-export aid.  It verifies each SLAY segment through
the existing decoder, records payload/file CRCs, checks segment continuity per
session, and materializes sequence gaps as explicit drop intervals.  It does
not infer external line health or replace NO5/SMA evidence.
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
from pathlib import Path
from typing import Any, Iterable

from tools.analyzer_trace_decode.analyzer_trace_decode import decode


def _intervals(decoded: dict[str, Any]) -> list[dict[str, Any]]:
    return [dict(interval) for interval in decoded.get("drop_intervals", [])]


def build_index(paths: Iterable[Path], *, tick_hz: int = 0) -> dict[str, Any]:
    unique = sorted({Path(path) for path in paths}, key=lambda path: str(path))
    if not unique:
        raise ValueError("no analyzer segments")
    entries: list[dict[str, Any]] = []
    for path in unique:
        decoded = decode(path, tick_hz=tick_hz)
        header = decoded["header"]
        session = int(header["session"])
        entry = {
            "path": str(path),
            "session": session,
            "header_size": int(header["header_size"]),
            "record_count": int(header["record_count"]),
            "dropped_records": int(header["dropped_records"]),
            "payload_crc32": int(decoded["computed_payload_crc32"]),
            "file_crc32": int(decoded["computed_file_crc32"]),
            "checks": decoded["checks"],
            "discontinuity_count": int(decoded["discontinuity_count"]),
            "drop_intervals": _intervals(decoded),
            "_first_record_sequence": (int(decoded["records"][0]["record_sequence"])
                                       if decoded["records"] else None),
            "_last_record_sequence": (int(decoded["records"][-1]["record_sequence"])
                                      if decoded["records"] else None),
        }
        entries.append(entry)

    sessions: list[dict[str, Any]] = []
    for session in sorted({int(entry["session"]) for entry in entries}):
        ordered = sorted(entries_for_session(entries, session),
                         key=lambda row: (row["path"]))
        previous: int | None = None
        for row in ordered:
            first = row["_first_record_sequence"]
            if previous is not None and first is not None and first != previous + 1:
                row["drop_intervals"].insert(0, {
                    "record_index": 0,
                    "first_missing_sequence": previous + 1,
                    "last_missing_sequence": first - 1,
                    "missing_count": max(0, first - previous - 1),
                    "reason": "cross_segment_sequence_gap",
                })
                row["discontinuity_count"] += 1
            if row["_last_record_sequence"] is not None:
                previous = int(row["_last_record_sequence"])
        sequence_gaps = sum(len(row["drop_intervals"]) for row in ordered)
        sessions.append({
            "session": session,
            "segment_count": len(ordered),
            "record_count": sum(int(row["record_count"]) for row in ordered),
            "dropped_records": sum(int(row["dropped_records"]) for row in ordered),
            "sequence_gap_interval_count": sequence_gaps,
            "paths": [row["path"] for row in ordered],
        })
    for entry in entries:
        entry.pop("_first_record_sequence", None)
        entry.pop("_last_record_sequence", None)
    return {
        "schema": "HAOFV_SYNC_IO_ANALYZER_BATCH_INDEX_V1",
        "source": "SYNC_IO_LOGIC_ANALYZER_SD_EXPORT",
        "segment_count": len(entries),
        "sessions": sessions,
        "segments": entries,
        "evidence_boundaries": {
            "offline_only": True,
            "external_waveform_substitute": False,
            "note": "Index/decoder output describes persisted local pad-visible analyzer data only.",
        },
    }


def entries_for_session(entries: list[dict[str, Any]], session: int) -> list[dict[str, Any]]:
    return [entry for entry in entries if int(entry["session"]) == session]


def write_csv(path: Path, result: dict[str, Any]) -> None:
    fields = ["path", "session", "header_size", "record_count", "dropped_records",
              "payload_crc32", "file_crc32", "discontinuity_count",
              "drop_interval_count", "checks_ok"]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for row in result["segments"]:
            checks = row["checks"]
            writer.writerow({
                **{field: row.get(field) for field in fields[:8]},
                "drop_interval_count": len(row["drop_intervals"]),
                "checks_ok": all(bool(value) for value in checks.values()),
            })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--segment", action="append", type=Path)
    parser.add_argument("--directory", type=Path,
                        help="directory containing *.bin analyzer segments")
    parser.add_argument("--tick-hz", type=int, default=0)
    parser.add_argument("--output", "-o", required=True, type=Path)
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()
    paths = list(args.segment or [])
    if args.directory:
        paths.extend(sorted(args.directory.glob("*.bin")))
    try:
        result = build_index(paths, tick_hz=args.tick_hz)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8", newline="\n")
        if args.csv:
            args.csv.parent.mkdir(parents=True, exist_ok=True)
            write_csv(args.csv, result)
    except (OSError, ValueError, json.JSONDecodeError, struct.error) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
