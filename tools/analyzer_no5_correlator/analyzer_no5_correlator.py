#!/usr/bin/env python3
"""Correlate a local SYNC_IO analyzer segment with a NO5 SMA capture.

This is an offline evidence join.  It never talks to a board and never treats
the two captures as interchangeable: the analyzer proves pad-visible local
levels while the NO5 capture proves the external SMA waveform.  Correlation is
based on the common hardware-time domain when available; sequence identities
are retained separately because an analyzer capture sequence and a NO5 sample
sequence have different owners and meanings.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import sys
from pathlib import Path
from typing import Any, Iterable


def _read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def _analyzer_times(analyzer: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    header = analyzer.get("header")
    if not isinstance(header, dict):
        raise ValueError("analyzer JSON is missing header")
    metadata = header.get("metadata")
    if not isinstance(metadata, dict):
        metadata = {}
    tick_hz = int(metadata.get("hardware_tick_hz") or
                  analyzer.get("timebase", {}).get("tick_hz") or 0)
    records = analyzer.get("records")
    if not isinstance(records, list):
        raise ValueError("analyzer JSON is missing records")
    result: list[dict[str, Any]] = []
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise ValueError(f"analyzer record {index} is not an object")
        timestamp = record.get("timestamp_ns")
        if timestamp is None and tick_hz:
            timestamp = int(record.get("hardware_tick", 0)) * 1_000_000_000 // tick_hz
        result.append({
            "index": index,
            "timestamp_ns": int(timestamp) if timestamp is not None else None,
            "capture_sequence": int(record.get("capture_sequence", 0)),
            "record_sequence": int(record.get("record_sequence", 0)),
        })
    return result, {
        "capture_sequence": int(metadata.get("capture_sequence") or
                                  header.get("session", 0)),
        "profile_generation": int(metadata.get("profile_generation", 0)),
        "persona_generation": int(metadata.get("persona_generation", 0)),
        "source_mask": int(metadata.get("source_mask", 0)),
        "hardware_tick_hz": tick_hz,
        "timestamp_resolution_ns": int(metadata.get("timestamp_resolution_ns", 0)),
    }


def _no5_times(no5: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    segments = no5.get("segments", [])
    if not isinstance(segments, list):
        raise ValueError("NO5 JSON segments must be a list")
    records = no5.get("records", [])
    if not isinstance(records, list):
        raise ValueError("NO5 JSON records must be a list")
    common: dict[str, Any] = {}
    if segments and isinstance(segments[0], dict):
        common.update(segments[0].get("common_metadata", {}))
        for key in ("sample_period_ns", "timestamp_source",
                    "timestamp_resolution_ns", "timestamp_flags",
                    "first_sample_seq", "first_matched_window_start_ns"):
            if key in segments[0]:
                common[key] = segments[0][key]
    if not common and records and isinstance(records[0], dict):
        for key in ("sample_period_ns", "timestamp_source",
                    "timestamp_resolution_ns", "timestamp_flags"):
            if key in records[0]:
                common[key] = records[0][key]
    period_ns = int(common.get("sample_period_ns", 0) or 0)
    result: list[dict[str, Any]] = []
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise ValueError(f"NO5 record {index} is not an object")
        timestamp = record.get("timestamp_ns")
        if timestamp is None:
            timestamp = record.get("matched_window_start_ns")
        if timestamp is None and period_ns:
            timestamp = (int(record.get("sample_seq", index)) * period_ns)
        result.append({
            "index": index,
            "timestamp_ns": int(timestamp) if timestamp is not None else None,
            "sample_seq": int(record.get("sample_seq", index)),
            "session_id": int(no5.get("session_id", 0)),
        })
    return result, {
        "session_id": int(no5.get("session_id", 0)),
        "sample_period_ns": period_ns,
        "timestamp_source": int(common.get("timestamp_source", 0) or 0),
        "timestamp_resolution_ns": int(common.get("timestamp_resolution_ns", 0) or 0),
        "timestamp_flags": int(common.get("timestamp_flags", 0) or 0),
        "first_sample_seq": int(common.get("first_sample_seq", result[0]["sample_seq"] if result else 0)),
    }


def _monotonic(values: Iterable[int]) -> bool:
    previous: int | None = None
    for value in values:
        if previous is not None and value <= previous:
            return False
        previous = value
    return True


def correlate(analyzer: dict[str, Any], no5: dict[str, Any], *,
              tolerance_ns: int = 0,
              sequence_anchor: dict[str, Any] | None = None) -> dict[str, Any]:
    """Return a deterministic, read-only association report."""
    if tolerance_ns < 0:
        raise ValueError("tolerance_ns must be non-negative")
    analyzer_records, analyzer_meta = _analyzer_times(analyzer)
    no5_records, no5_meta = _no5_times(no5)
    a = [row for row in analyzer_records if row["timestamp_ns"] is not None]
    n = [row for row in no5_records if row["timestamp_ns"] is not None]
    n_times = [int(row["timestamp_ns"]) for row in n]
    pairs: list[dict[str, Any]] = []
    for row in a:
        timestamp = int(row["timestamp_ns"])
        position = bisect.bisect_left(n_times, timestamp)
        candidates = []
        if position < len(n):
            candidates.append(n[position])
        if position:
            candidates.append(n[position - 1])
        if not candidates:
            continue
        nearest = min(candidates, key=lambda item: abs(int(item["timestamp_ns"]) - timestamp))
        delta = int(nearest["timestamp_ns"]) - timestamp
        if tolerance_ns and abs(delta) > tolerance_ns:
            continue
        pairs.append({
            "analyzer_index": row["index"],
            "analyzer_record_sequence": row["record_sequence"],
            "analyzer_capture_sequence": row["capture_sequence"],
            "no5_index": nearest["index"],
            "no5_sample_seq": nearest["sample_seq"],
            "timestamp_ns": timestamp,
            "no5_timestamp_ns": nearest["timestamp_ns"],
            "delta_ns": delta,
        })
    analyzer_sequences = sorted({row["capture_sequence"] for row in analyzer_records})
    no5_sequences = [row["sample_seq"] for row in no5_records]
    a_times = [int(row["timestamp_ns"]) for row in a]
    overlap = bool(a_times and n_times and max(min(a_times), min(n_times)) <=
                   min(max(a_times), max(n_times)))
    timebase_compatible = bool(
        analyzer_meta["hardware_tick_hz"] and
        no5_meta["timestamp_resolution_ns"] and a_times and n_times)
    report: dict[str, Any] = {
        "schema": "HAOFV_SYNC_IO_NO5_CORRELATION_V1",
        "correlation": {
            "timebase_compatible": timebase_compatible,
            "timestamp_overlap": overlap,
            "pair_count": len(pairs),
            "tolerance_ns": tolerance_ns,
            "offset_median_ns": (sorted(p["delta_ns"] for p in pairs)[len(pairs) // 2]
                                  if pairs else None),
        },
        "analyzer": {
            "source": analyzer.get("source"),
            "metadata": analyzer_meta,
            "record_count": len(analyzer_records),
            "capture_sequences": analyzer_sequences,
            "timestamps_available": bool(a),
        },
        "no5": {
            "source": no5.get("source"),
            "metadata": no5_meta,
            "record_count": len(no5_records),
            "sample_sequence_range": ([min(no5_sequences), max(no5_sequences)]
                                       if no5_sequences else None),
            "sample_sequence_monotonic": _monotonic(no5_sequences),
            "timestamps_available": bool(n),
        },
        "sequence_alignment": {
            "status": "explicit_anchor" if sequence_anchor else "separate_domains",
            "analyzer_capture_sequence": analyzer_meta["capture_sequence"],
            "no5_session_id": no5_meta["session_id"],
            "note": ("capture_sequence identifies the local analyzer epoch; "
                     "sample_seq identifies the NO5 raw-word stream. They are "
                     "not numerically comparable without an explicit anchor."),
            "anchor": sequence_anchor,
        },
        "evidence_boundaries": {
            "mutually_substitutable": False,
            "analyzer": "local pad-visible GPIO levels and analyzer metadata only",
            "no5": "external SMA/line waveform and NO5 timebase only",
            "required_for_external_claim": "both captures plus this association report",
        },
        "pairs": pairs,
    }
    return report


def _write_csv(path: Path, pairs: list[dict[str, Any]]) -> None:
    fields = ["analyzer_index", "analyzer_record_sequence", "analyzer_capture_sequence",
              "no5_index", "no5_sample_seq", "timestamp_ns", "no5_timestamp_ns", "delta_ns"]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(pairs)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analyzer", required=True, type=Path,
                        help="decoded analyzer JSON from analyzer_trace_decode")
    parser.add_argument("--no5", required=True, type=Path,
                        help="decoded NO5 summary JSON from dpll_waveform_capture")
    parser.add_argument("--output", "-o", required=True, type=Path)
    parser.add_argument("--csv", type=Path,
                        help="optional matched-pair CSV output")
    parser.add_argument("--tolerance-ns", type=int, default=0)
    parser.add_argument("--sequence-anchor", type=Path,
                        help="optional JSON object tying both epochs to a TDMA cycle")
    args = parser.parse_args()
    try:
        anchor = _read_json(args.sequence_anchor) if args.sequence_anchor else None
        result = correlate(_read_json(args.analyzer), _read_json(args.no5),
                           tolerance_ns=args.tolerance_ns,
                           sequence_anchor=anchor)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8", newline="\n")
        if args.csv:
            args.csv.parent.mkdir(parents=True, exist_ok=True)
            _write_csv(args.csv, result["pairs"])
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
