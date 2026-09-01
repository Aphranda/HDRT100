#!/usr/bin/env python3
"""Decode NO5 PIO0 SMA waveform segments captured on SD.

Serial status is intentionally absent from the analysis path.  Every edge and
phase point emitted here is reconstructed from the raw PIO0 words and timing
metadata stored in the SD segment files.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import math
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
DEFAULT_PULSE_PERIOD_NS = 1_000_000


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


def _circular_delta_ns(value: int, reference: int, period_ns: int) -> int:
    return (value - reference + period_ns // 2) % period_ns - period_ns // 2


def _circular_center_ns(values: list[int], period_ns: int) -> int | None:
    if not values:
        return None
    # A medoid is robust to the false transitions introduced by dropped DMA
    # words and keeps the result on an actually observed pulse phase.
    return min(values, key=lambda candidate: sum(
        abs(_circular_delta_ns(value, candidate, period_ns))
        for value in values))


def _circular_span_ns(values: list[int], period_ns: int) -> int | None:
    if not values:
        return None
    ordered = sorted(value % period_ns for value in values)
    gaps = [ordered[index + 1] - ordered[index]
            for index in range(len(ordered) - 1)]
    gaps.append(period_ns - ordered[-1] + ordered[0])
    return period_ns - max(gaps)


def _half_cycle_oscillation(values: list[int], period_ns: int) -> dict[str, Any]:
    bin_count = 20
    bins = [0] * bin_count
    for value in values:
        bins[(value % period_ns) * bin_count // period_ns] += 1
    primary = max(range(bin_count), key=bins.__getitem__) if values else 0
    candidates = [index for index in range(bin_count)
                  if min((index - primary) % bin_count,
                         (primary - index) % bin_count) >= 3]
    secondary = max(candidates, key=bins.__getitem__) if candidates else primary
    separation_bins = min((secondary - primary) % bin_count,
                          (primary - secondary) % bin_count)
    separation_ns = separation_bins * period_ns // bin_count
    ratio = bins[secondary] / bins[primary] if bins[primary] else 0.0
    detected = (
        len(values) >= 12 and ratio >= 0.20 and
        abs(separation_ns - period_ns // 2) <= period_ns * 0.15)
    return {
        "detected": detected,
        "primary_phase_ns": (primary * 2 + 1) * period_ns // (2 * bin_count),
        "secondary_phase_ns": (
            (secondary * 2 + 1) * period_ns // (2 * bin_count)),
        "secondary_to_primary_ratio": round(ratio, 4),
        "peak_separation_ns": separation_ns,
    }


def _phase_tracking(edges: list[dict[str, Any]], period_ns: int
                    ) -> tuple[list[dict[str, Any]], list[dict[str, Any]],
                               dict[str, Any]]:
    rising = [edge for edge in edges if edge["edge"] == "rising"]
    if not rising:
        return [], [], {"assessment": "NO_RISING_EDGES", "nodes": {}}
    first_ns = min(edge["timestamp_ns"] for edge in rising)
    references = sorted(edge["timestamp_ns"] for edge in rising
                        if edge["channel"] == 0)
    tracking = []
    for edge in rising:
        timestamp_ns = edge["timestamp_ns"]
        phase_ns = timestamp_ns % period_ns
        reference_ns = timestamp_ns
        if references and edge["channel"] != 0:
            position = bisect.bisect_left(references, timestamp_ns)
            nearby = references[max(0, position - 1):position + 1]
            reference_ns = min(nearby, key=lambda value: abs(value - timestamp_ns))
        tracking.append({
            "elapsed_s": (timestamp_ns - first_ns) / 1e9,
            "channel": edge["channel"],
            "node": f"NO{edge['channel'] + 1}",
            "phase_ns": phase_ns,
            "relative_to_no1_ns": (
                0 if edge["channel"] == 0 else
                _circular_delta_ns(phase_ns, reference_ns % period_ns,
                                   period_ns)),
            "reference_age_ms": abs(timestamp_ns - reference_ns) / 1e6,
        })

    duration_s = max(row["elapsed_s"] for row in tracking)
    bin_width_s = 0.25
    trend = []
    bin_total = max(1, math.ceil((duration_s + 1e-12) / bin_width_s))
    for bin_index in range(bin_total):
        start_s = bin_index * bin_width_s
        end_s = start_s + bin_width_s
        for channel in range(CHANNEL_COUNT):
            phases = [row["phase_ns"] for row in tracking
                      if row["channel"] == channel and
                      start_s <= row["elapsed_s"] < end_s]
            center = _circular_center_ns(phases, period_ns)
            if center is not None:
                trend.append({
                    "elapsed_s": start_s + bin_width_s / 2,
                    "channel": channel,
                    "node": f"NO{channel + 1}",
                    "phase_center_ns": center,
                    "sample_count": len(phases),
                })

    nodes: dict[str, Any] = {}
    detected_nodes = []
    final_start_s = duration_s * 0.6
    for channel in range(CHANNEL_COUNT):
        phases = [row["phase_ns"] for row in tracking
                  if row["channel"] == channel]
        final = [row["phase_ns"] for row in tracking
                 if row["channel"] == channel and
                 row["elapsed_s"] >= final_start_s]
        oscillation = _half_cycle_oscillation(final, period_ns)
        if oscillation["detected"]:
            detected_nodes.append(f"NO{channel + 1}")
        nodes[f"NO{channel + 1}"] = {
            "rising_edge_count": len(phases),
            "final_phase_center_ns": _circular_center_ns(final, period_ns),
            "final_circular_span_ns": _circular_span_ns(final, period_ns),
            "half_cycle_oscillation": oscillation,
        }
    assessment = (
        "HALF_CYCLE_LIMIT_CYCLE" if detected_nodes else
        "NO_HALF_CYCLE_LIMIT_CYCLE_DETECTED")
    return tracking, trend, {
        "pulse_period_ns": period_ns,
        "duration_s": duration_s,
        "assessment": assessment,
        "oscillating_nodes": detected_nodes,
        "nodes": nodes,
        "loop_margin_estimation": {
            "available": False,
            "gain_margin_db": None,
            "phase_margin_deg": None,
            "reason": "requires injected frequency response or an identified loop model",
        },
    }


def decode_segments(paths: Iterable[Path], *,
                    pulse_period_ns: int = DEFAULT_PULSE_PERIOD_NS
                    ) -> dict[str, Any]:
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

    tracking, trend, convergence = _phase_tracking(edges, pulse_period_ns)
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
        "phase_tracking": tracking,
        "phase_trend": trend,
        "convergence": convergence,
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


def _convergence_svg(rows: list[dict[str, Any]],
                     trend: list[dict[str, Any]], period_ns: int) -> str:
    width, height = 1400, 760
    left, top, plot_w, plot_h = 90, 55, 1240, 610
    max_x = max((row["elapsed_s"] for row in rows), default=1.0) or 1.0
    colors = ("#2563eb", "#dc2626", "#059669", "#7c3aed")
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<style>text{font-family:Arial,sans-serif;letter-spacing:0;fill:#202124}'
        '.axis{stroke:#5f6368;stroke-width:1}.grid{stroke:#dadce0;stroke-width:1}'
        '.trend{fill:none;stroke-width:2}</style>',
        '<text x="90" y="30" font-size="20">DPLL locking convergence from NO5 PIO0 raw waveform</text>',
    ]
    for step in range(6):
        y = top + plot_h * (1 - step / 5)
        value_us = period_ns * step / 5 / 1000
        chunks.append(
            f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" '
            f'y2="{y:.2f}" class="grid"/>')
        chunks.append(
            f'<text x="{left - 12}" y="{y + 5:.2f}" text-anchor="end" '
            f'font-size="12">{value_us:.0f}</text>')
    chunks.extend([
        f'<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" '
        f'y2="{top + plot_h}" class="axis"/>',
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" '
        f'class="axis"/>',
        f'<text x="{left + plot_w / 2:.2f}" y="{height - 28}" '
        f'text-anchor="middle" font-size="13">Elapsed time (s)</text>',
        f'<text x="20" y="{top + plot_h / 2:.2f}" font-size="13" '
        f'transform="rotate(-90 20 {top + plot_h / 2:.2f})" '
        f'text-anchor="middle">Pulse phase modulo period (us)</text>',
    ])
    for row in rows:
        x = left + plot_w * row["elapsed_s"] / max_x
        y = top + plot_h * (1 - row["phase_ns"] / period_ns)
        chunks.append(
            f'<circle cx="{x:.2f}" cy="{y:.2f}" r="1.7" '
            f'fill="{colors[row["channel"]]}" fill-opacity="0.38"/>')
    for channel, color in enumerate(colors):
        points = " ".join(
            f'{left + plot_w * row["elapsed_s"] / max_x:.2f},'
            f'{top + plot_h * (1 - row["phase_center_ns"] / period_ns):.2f}'
            for row in trend if row["channel"] == channel)
        if points:
            chunks.append(
                f'<polyline points="{escape(points)}" class="trend" '
                f'stroke="{color}"/>')
        chunks.append(
            f'<text x="{left + channel * 105}" y="{height - 58}" '
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
    tracking_fields = ["elapsed_s", "channel", "node", "phase_ns",
                       "relative_to_no1_ns", "reference_age_ms"]
    trend_fields = ["elapsed_s", "channel", "node", "phase_center_ns",
                    "sample_count"]
    _write_csv(out_dir / "dpll_convergence.csv",
               result.get("phase_tracking", []), tracking_fields)
    _write_csv(out_dir / "dpll_convergence_trend.csv",
               result.get("phase_trend", []), trend_fields)
    (out_dir / "phase_curve.svg").write_text(
        _phase_svg(result["phase"]), encoding="utf-8")
    (out_dir / "dpll_convergence.svg").write_text(
        _convergence_svg(
            result.get("phase_tracking", []), result.get("phase_trend", []),
            result.get("convergence", {}).get(
                "pulse_period_ns", DEFAULT_PULSE_PERIOD_NS)),
        encoding="utf-8")
    summary = {key: value for key, value in result.items()
               if key not in {"records", "edges", "phase", "phase_tracking",
                              "phase_trend"}}
    summary["outputs"] = {
        "raw_records": str(out_dir / "raw_records.csv"),
        "edges": str(out_dir / "edges.csv"),
        "phase_curve": str(out_dir / "phase_curve.csv"),
        "phase_svg": str(out_dir / "phase_curve.svg"),
        "dpll_convergence": str(out_dir / "dpll_convergence.csv"),
        "dpll_convergence_trend": str(
            out_dir / "dpll_convergence_trend.csv"),
        "dpll_convergence_svg": str(out_dir / "dpll_convergence.svg"),
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--segment", action="append", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--pulse-period-ns", type=int,
                        default=DEFAULT_PULSE_PERIOD_NS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        result = decode_segments(
            args.segment, pulse_period_ns=args.pulse_period_ns)
        print(json.dumps(write_reports(result, args.out_dir),
                         ensure_ascii=False, indent=2))
    except (OSError, ValueError, struct.error) as exc:
        print(f"FAILED: {exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
