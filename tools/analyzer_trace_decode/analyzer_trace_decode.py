#!/usr/bin/env python3
"""Decode persisted SYNC_IO logic-analyzer segments.

The firmware stores a compact little-endian ``SLAY`` header followed by
fixed-size 32-byte capture records.  File CRC is supplied by StorageAO's
write-status response; this decoder verifies the embedded payload CRC and
reports the externally supplied file CRC when provided.
"""

from __future__ import annotations

import argparse
import binascii
import csv
import json
import struct
import sys
from pathlib import Path
from typing import Any

MAGIC = 0x59414C53
SCHEMA = 1
HEADER = struct.Struct("<IHHIIII")
METADATA = struct.Struct("<IIIIII")
RECORD = struct.Struct("<QIIIIII")
RECORD_FLAGS = {1: "diagnostic_only", 2: "trigger", 4: "discontinuity"}


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def decode(path: Path, tick_hz: int = 0, expected_file_crc: int | None = None) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"{path} is too small for an analyzer header")
    magic, schema, header_size, session, record_count, dropped, payload_crc = HEADER.unpack_from(data)
    if header_size < HEADER.size:
        raise ValueError(f"header_size {header_size} is smaller than {HEADER.size}")
    expected_size = header_size + record_count * RECORD.size
    if len(data) != expected_size:
        raise ValueError(f"file size {len(data)} does not match expected {expected_size}")
    payload = data[header_size:]
    metadata = {}
    if header_size >= HEADER.size + METADATA.size:
        (source_mask, profile_generation, persona_generation, hardware_tick_hz,
         timestamp_resolution_ns, capture_sequence) = METADATA.unpack_from(data, HEADER.size)
        metadata = {
            "source_mask": source_mask,
            "profile_generation": profile_generation,
            "persona_generation": persona_generation,
            "hardware_tick_hz": hardware_tick_hz,
            "timestamp_resolution_ns": timestamp_resolution_ns,
            "capture_sequence": capture_sequence,
        }
    records: list[dict[str, Any]] = []
    previous_sequence: int | None = None
    discontinuities = 0
    for index in range(record_count):
        offset = index * RECORD.size
        tick, capture_seq, record_seq, level, edge, flags, reserved = RECORD.unpack_from(payload, offset)
        gap = previous_sequence is not None and record_seq != previous_sequence + 1
        if gap:
            discontinuities += 1
        records.append({
            "index": index,
            "hardware_tick": tick,
            "timestamp_ns": (tick * 1_000_000_000 // tick_hz) if tick_hz else None,
            "capture_sequence": capture_seq,
            "record_sequence": record_seq,
            "level_mask": level,
            "edge_mask": edge,
            "flags": flags,
            "flag_names": [name for bit, name in RECORD_FLAGS.items() if flags & bit],
            "reserved": reserved,
            "sequence_gap": gap,
        })
        previous_sequence = record_seq
    payload_computed = crc32(payload)
    file_computed = crc32(data)
    checks = {
        "magic_ok": magic == MAGIC,
        "schema_ok": schema == SCHEMA,
        "size_ok": len(data) == expected_size,
        "payload_crc_ok": payload_computed == payload_crc,
        "file_crc_ok": expected_file_crc is None or file_computed == expected_file_crc,
    }
    return {
        "source": str(path),
        "format": "SYNC_IO_LOGIC_ANALYZER_SEGMENT",
        "header": {
            "magic": magic,
            "magic_ascii": "SLAY" if magic == MAGIC else None,
            "schema": schema,
            "header_size": header_size,
            "session": session,
            "record_count": record_count,
            "dropped_records": dropped,
            "payload_crc32": payload_crc,
            "metadata": metadata,
        },
        "timebase": {"tick_hz": tick_hz, "known": bool(tick_hz)},
        "computed_payload_crc32": payload_computed,
        "computed_file_crc32": file_computed,
        "discontinuity_count": discontinuities,
        "checks": checks,
        "records": records,
    }


def write_json(decoded: dict[str, Any], output: Path | None) -> None:
    text = json.dumps(decoded, indent=2, ensure_ascii=False) + "\n"
    if output is None:
        sys.stdout.write(text)
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8", newline="\n")


def write_csv(decoded: dict[str, Any], output: Path | None) -> None:
    fields = ["index", "hardware_tick", "timestamp_ns", "capture_sequence", "record_sequence",
              "level_mask", "edge_mask", "flags", "flag_names", "reserved", "sequence_gap"]
    stream = sys.stdout if output is None else output.open("w", encoding="utf-8", newline="")
    try:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for record in decoded["records"]:
            row = dict(record)
            row["flag_names"] = ",".join(row["flag_names"])
            writer.writerow(row)
    finally:
        if output is not None:
            stream.close()


def write_svg(decoded: dict[str, Any], output: Path | None) -> None:
    """Render a deterministic, bounded level-mask waveform as SVG."""
    records = decoded["records"]
    channels = sorted({bit for record in records
                       for bit in range(32)
                       if int(record["level_mask"]) & (1 << bit) or
                       int(record["edge_mask"]) & (1 << bit)})
    if not channels:
        channels = [0]
    max_channels = 32
    channels = channels[:max_channels]
    left, top, row_height, sample_width = 96, 28, 24, 8
    width = left + max(1, len(records)) * sample_width + 16
    height = top + len(channels) * row_height + 24
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
             f'viewBox="0 0 {width} {height}">',
             '<title>SYNC_IO logic analyzer segment</title>',
             '<rect width="100%" height="100%" fill="white"/>']
    for row, channel in enumerate(channels):
        y = top + row * row_height
        parts.append(f'<text x="4" y="{y + 15}" font-family="monospace" font-size="12">GPIO{channel}</text>')
        parts.append(f'<line x1="{left}" y1="{y + 12}" x2="{width - 8}" y2="{y + 12}" stroke="#ddd"/>')
        previous = None
        for index, record in enumerate(records):
            high = bool(int(record["level_mask"]) & (1 << channel))
            x = left + index * sample_width
            level_y = y + (3 if high else 15)
            if previous is not None:
                parts.append(f'<line x1="{x}" y1="{previous}" x2="{x}" y2="{level_y}" stroke="#1769aa"/>')
            parts.append(f'<line x1="{x}" y1="{level_y}" x2="{x + sample_width}" y2="{level_y}" stroke="#1769aa"/>')
            if int(record["edge_mask"]) & (1 << channel):
                parts.append(f'<circle cx="{x + sample_width // 2}" cy="{level_y}" r="2" fill="#d33"/>')
            previous = level_y
    parts.append('</svg>')
    text = "\n".join(parts) + "\n"
    if output is None:
        sys.stdout.write(text)
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8", newline="\n")


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("segment", type=Path)
    parser.add_argument("--tick-hz", type=int, default=0)
    parser.add_argument("--expected-file-crc", type=parse_int)
    parser.add_argument("--csv", action="store_true")
    parser.add_argument("--svg", action="store_true", help="write a level-mask waveform SVG")
    parser.add_argument("--output", "-o", type=Path)
    parser.add_argument("--allow-bad-crc", action="store_true")
    args = parser.parse_args()
    decoded = decode(args.segment, args.tick_hz, args.expected_file_crc)
    if args.csv and args.svg:
        parser.error("--csv and --svg are mutually exclusive")
    if args.csv:
        write_csv(decoded, args.output)
    elif args.svg:
        write_svg(decoded, args.output)
    else:
        write_json(decoded, args.output)
    bad = [name for name, ok in decoded["checks"].items() if not ok]
    if bad and not args.allow_bad_crc:
        print(f"analyzer check failed: {', '.join(bad)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
