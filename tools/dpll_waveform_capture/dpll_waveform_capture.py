#!/usr/bin/env python3
"""Decode NO5 PIO0 SMA waveform segments captured on SD.

Serial status is intentionally absent from the analysis path.  Every edge and
phase point emitted here is reconstructed from the raw PIO0 words and timing
metadata stored in the SD segment files.
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
import zlib
from pathlib import Path
from typing import Any, Iterable
from xml.sax.saxutils import escape


MAGIC = 0x57524D53  # SMRW
SCHEMA = 1
HEADER = struct.Struct("<IHHHHIIIIIIIII")
RECORD = struct.Struct("<IIIIQIIIII")
SAMPLES_PER_WORD = 8
CHANNEL_COUNT = 4


def _absolute_base_time(base_l32: int, window_start_ns: int) -> int:
    base = (window_start_ns & ~0xFFFFFFFF) | base_l32
    if base + (1 << 31) < window_start_ns:
        base += 1 << 32
    elif base > window_start_ns + (1 << 31):
        base -= 1 << 32
    return base


def decode_segment(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"waveform segment is shorter than header: {path}")
    values = HEADER.unpack_from(data)
    (magic, schema, header_size, record_size, reserved, session_id,
     segment_index, first_record_index, record_count, dropped_count,
     start_ms, end_ms, observed_mask, payload_crc32) = values
    if magic != MAGIC:
        raise ValueError(f"unexpected waveform magic 0x{magic:08X}")
    if schema != SCHEMA:
        raise ValueError(f"unsupported waveform schema {schema}")
    if header_size != HEADER.size or record_size != RECORD.size:
        raise ValueError(
            f"waveform layout {header_size}/{record_size} != "
            f"{HEADER.size}/{RECORD.size}")
    if reserved != 0:
        raise ValueError(f"unsupported waveform flags 0x{reserved:04X}")
    expected_size = header_size + record_count * record_size
    if len(data) != expected_size:
        raise ValueError(f"waveform size {len(data)} != expected {expected_size}")
    payload = data[header_size:]
    actual_crc = zlib.crc32(payload) & 0xFFFFFFFF
    if actual_crc != payload_crc32:
        raise ValueError(
            f"waveform payload CRC 0x{actual_crc:08X} != "
            f"0x{payload_crc32:08X}")

    records = []
    for index in range(record_count):
        record = RECORD.unpack_from(payload, index * record_size)
        records.append(dict(zip((
            "raw_word", "sample_seq", "previous_sample_mask",
            "base_time_l32_ns", "matched_window_start_ns",
            "sample_period_ns", "timestamp_source",
            "timestamp_resolution_ns", "timestamp_flags",
            "dropped_before",
        ), record, strict=True)))
    return {
        "path": str(path),
        "session_id": session_id,
        "segment_index": segment_index,
        "first_record_index": first_record_index,
        "record_count": record_count,
        "dropped_count": dropped_count,
        "start_ms": start_ms,
        "end_ms": end_ms,
        "observed_mask": observed_mask,
        "payload_crc32": payload_crc32,
        "records": records,
    }


def decode_segments(paths: Iterable[Path]) -> dict[str, Any]:
    segments = sorted(
        (decode_segment(path) for path in paths),
        key=lambda segment: segment["segment_index"])
    if not segments:
        raise ValueError("no waveform segments")
    session_id = segments[0]["session_id"]
    records: list[dict[str, int]] = []
    expected_first = 0
    for expected_segment, segment in enumerate(segments):
        if segment["session_id"] != session_id:
            raise ValueError("waveform segments belong to different sessions")
        if segment["segment_index"] != expected_segment:
            raise ValueError(
                f"missing waveform segment {expected_segment}: "
                f"found {segment['segment_index']}")
        if segment["first_record_index"] != expected_first:
            raise ValueError(
                f"record discontinuity {segment['first_record_index']} != "
                f"{expected_first}")
        records.extend(segment["records"])
        expected_first += segment["record_count"]

    edges: list[dict[str, Any]] = []
    observed_mask = segments[0]["observed_mask"] & 0x0F
    for record_index, record in enumerate(records):
        previous = record["previous_sample_mask"] & observed_mask
        base_ns = _absolute_base_time(
            record["base_time_l32_ns"], record["matched_window_start_ns"])
        for sample_index in range(SAMPLES_PER_WORD):
            # Firmware phase-only capture sets sample0_lsb=false.
            shift = (SAMPLES_PER_WORD - 1 - sample_index) * CHANNEL_COUNT
            current = (record["raw_word"] >> shift) & observed_mask
            changed = previous ^ current
            for channel in range(CHANNEL_COUNT):
                bit = 1 << channel
                if changed & bit:
                    timestamp_ns = (
                        base_ns + sample_index * record["sample_period_ns"])
                    edges.append({
                        "record_index": record_index,
                        "sample_seq": record["sample_seq"],
                        "sample_index": sample_index,
                        "channel": channel,
                        "edge": "rising" if current & bit else "falling",
                        "timestamp_ns": timestamp_ns,
                        "window_start_ns": record["matched_window_start_ns"],
                    })
            previous = current

    rising_by_window: dict[int, dict[int, int]] = {}
    for edge in edges:
        if edge["edge"] != "rising" or edge["window_start_ns"] == 0:
            continue
        channels = rising_by_window.setdefault(edge["window_start_ns"], {})
        channels.setdefault(edge["channel"], edge["timestamp_ns"])
    phase = []
    for window_start, channels in sorted(rising_by_window.items()):
        if len(channels) != CHANNEL_COUNT:
            continue
        earliest = min(channels.values())
        latest = max(channels.values())
        row: dict[str, Any] = {
            "window_start_ns": window_start,
            "elapsed_s": (window_start - min(rising_by_window)) / 1e9,
            "span_ns": latest - earliest,
        }
        for channel in range(CHANNEL_COUNT):
            row[f"edge{channel}_ns"] = channels[channel]
            row[f"offset{channel}_ns"] = channels[channel] - earliest
        phase.append(row)

    return {
        "schema": "HAOFV_NO5_PIO0_WAVEFORM_V1",
        "source": "NO5_SD_PIO0_RAW_WAVEFORM",
        "session_id": session_id,
        "segment_count": len(segments),
        "record_count": len(records),
        "edge_count": len(edges),
        "phase_round_count": len(phase),
        "dropped_count": max(segment["dropped_count"] for segment in segments),
        "source_dropped_count": max(
            (record["dropped_before"] for record in records), default=0),
        "records": records,
        "edges": edges,
        "phase": phase,
        "segments": [{key: value for key, value in segment.items()
                      if key != "records"} for segment in segments],
    }


def _write_csv(path: Path, rows: list[dict[str, Any]], fields: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def _phase_svg(rows: list[dict[str, Any]]) -> str:
    width, height = 1200, 620
    left, top, plot_w, plot_h = 80, 45, 1080, 500
    max_x = max((row["elapsed_s"] for row in rows), default=1.0) or 1.0
    max_y = max((row[f"offset{channel}_ns"] for row in rows
                 for channel in range(CHANNEL_COUNT)), default=1) or 1
    colors = ("#2563eb", "#dc2626", "#059669", "#7c3aed")
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<style>text{font-family:Arial,sans-serif;letter-spacing:0;fill:#202124}'
        '.axis{stroke:#5f6368;stroke-width:1}.line{fill:none;stroke-width:1.5}</style>',
        '<text x="80" y="27" font-size="18">NO5 PIO0 raw waveform phase offsets</text>',
        f'<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" '
        f'y2="{top + plot_h}" class="axis"/>',
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" class="axis"/>',
    ]
    for channel, color in enumerate(colors):
        points = " ".join(
            f'{left + plot_w * row["elapsed_s"] / max_x:.2f},'
            f'{top + plot_h * (1 - row[f"offset{channel}_ns"] / max_y):.2f}'
            for row in rows)
        chunks.append(
            f'<polyline points="{escape(points)}" class="line" stroke="{color}"/>')
        chunks.append(
            f'<text x="{left + channel * 110}" y="{height - 28}" '
            f'font-size="13" fill="{color}">NO{channel + 1}</text>')
    chunks.append('</svg>')
    return "\n".join(chunks) + "\n"


def write_reports(result: dict[str, Any], out_dir: Path) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    record_fields = list(result["records"][0]) if result["records"] else [
        "raw_word", "sample_seq", "previous_sample_mask", "base_time_l32_ns",
        "matched_window_start_ns", "sample_period_ns", "timestamp_source",
        "timestamp_resolution_ns", "timestamp_flags", "dropped_before"]
    edge_fields = ["record_index", "sample_seq", "sample_index", "channel",
                   "edge", "timestamp_ns", "window_start_ns"]
    phase_fields = ["window_start_ns", "elapsed_s", "span_ns"] + [
        field for channel in range(CHANNEL_COUNT)
        for field in (f"edge{channel}_ns", f"offset{channel}_ns")]
    _write_csv(out_dir / "raw_records.csv", result["records"], record_fields)
    _write_csv(out_dir / "edges.csv", result["edges"], edge_fields)
    _write_csv(out_dir / "phase_curve.csv", result["phase"], phase_fields)
    (out_dir / "phase_curve.svg").write_text(
        _phase_svg(result["phase"]), encoding="utf-8")
    summary = {key: value for key, value in result.items()
               if key not in {"records", "edges", "phase"}}
    summary["outputs"] = {
        "raw_records": str(out_dir / "raw_records.csv"),
        "edges": str(out_dir / "edges.csv"),
        "phase_curve": str(out_dir / "phase_curve.csv"),
        "phase_svg": str(out_dir / "phase_curve.svg"),
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--segment", action="append", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        result = decode_segments(args.segment)
        print(json.dumps(write_reports(result, args.out_dir),
                         ensure_ascii=False, indent=2))
    except (OSError, ValueError, struct.error) as exc:
        print(f"FAILED: {exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
