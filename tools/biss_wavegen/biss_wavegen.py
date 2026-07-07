#!/usr/bin/env python3
"""Generate simple BiSS-C TAP CLK/DATA CSV vectors for bench bring-up."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("build/biss_wavegen/biss_1mhz.csv"))
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--clock-hz", type=int, default=1_000_000)
    parser.add_argument("--frame-bits", type=int, default=48)
    parser.add_argument("--position-offset", type=int, default=8)
    parser.add_argument("--position-bits", type=int, default=24)
    parser.add_argument("--positions", default="90,110,120", help="comma-separated position values")
    parser.add_argument("--idle-bits", type=int, default=4, help="idle bit periods between frames")
    parser.add_argument("--anchor-offset", type=int, default=0)
    parser.add_argument("--anchor-bits", type=int, default=2)
    parser.add_argument("--anchor-value", type=lambda value: int(value, 0), default=0x2)
    parser.add_argument("--error-bit", type=int, default=-1, help="-1 disables ERR bit")
    parser.add_argument("--warning-bit", type=int, default=-1, help="-1 disables WRN bit")
    parser.add_argument("--crc-offset", type=int, default=-1, help="-1 disables CSV CRC field generation")
    parser.add_argument("--crc-bits", type=int, default=0)
    return parser.parse_args()


def parse_positions(text: str) -> list[int]:
    return [int(token.strip(), 0) for token in text.split(",") if token.strip()]


def put_bits_msb(frame: int, frame_bits: int, offset: int, bits: int, value: int) -> int:
    if bits <= 0:
        return frame
    shift = frame_bits - offset - bits
    mask = (1 << bits) - 1
    frame &= ~(mask << shift)
    frame |= (value & mask) << shift
    return frame


def build_frame(args: argparse.Namespace, position: int) -> int:
    frame = 0
    frame = put_bits_msb(frame,
                         args.frame_bits,
                         args.anchor_offset,
                         args.anchor_bits,
                         args.anchor_value)
    frame = put_bits_msb(frame,
                         args.frame_bits,
                         args.position_offset,
                         args.position_bits,
                         position)
    if args.error_bit >= 0:
        frame = put_bits_msb(frame, args.frame_bits, args.error_bit, 1, 1)
    if args.warning_bit >= 0:
        frame = put_bits_msb(frame, args.frame_bits, args.warning_bit, 1, 1)
    if args.crc_offset >= 0 and args.crc_bits > 0:
        frame = put_bits_msb(frame, args.frame_bits, args.crc_offset, args.crc_bits, 0)
    return frame


def frame_bits_msb(frame: int, frame_bits: int) -> list[int]:
    return [(frame >> (frame_bits - 1 - index)) & 1 for index in range(frame_bits)]


def append_sample(rows: list[dict[str, int]], sample_ns: int, clk: int, data: int, frame_index: int, bit_index: int) -> None:
    if rows and sample_ns <= rows[-1]["time_ns"]:
        sample_ns = rows[-1]["time_ns"] + 1
    rows.append(
        {
            "time_ns": sample_ns,
            "clk": clk,
            "data": data,
            "frame_index": frame_index,
            "bit_index": bit_index,
        }
    )


def generate_rows(args: argparse.Namespace, frames: list[int]) -> list[dict[str, int]]:
    period_ns = 1_000_000_000 // args.clock_hz
    half_ns = period_ns // 2
    rows: list[dict[str, int]] = []
    time_ns = 0
    data = 1

    append_sample(rows, time_ns, 0, data, -1, -1)
    for frame_index, frame in enumerate(frames):
        for _ in range(args.idle_bits):
            time_ns += half_ns
            append_sample(rows, time_ns, 1, data, frame_index, -1)
            time_ns += half_ns
            append_sample(rows, time_ns, 0, data, frame_index, -1)

        for bit_index, bit in enumerate(frame_bits_msb(frame, args.frame_bits)):
            data = bit
            append_sample(rows, time_ns, 0, data, frame_index, bit_index)
            time_ns += half_ns
            append_sample(rows, time_ns, 1, data, frame_index, bit_index)
            time_ns += half_ns
            append_sample(rows, time_ns, 0, data, frame_index, bit_index)

    return rows


def write_csv(path: Path, rows: list[dict[str, int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=("time_ns", "clk", "data", "frame_index", "bit_index"))
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    positions = parse_positions(args.positions)
    frames = [build_frame(args, position) for position in positions]
    rows = generate_rows(args, frames)
    write_csv(args.out, rows)

    summary = {
        "clock_hz": args.clock_hz,
        "frame_bits": args.frame_bits,
        "position_offset": args.position_offset,
        "position_bits": args.position_bits,
        "positions": positions,
        "frames_hex": [f"0x{frame:0{(args.frame_bits + 3) // 4}X}" for frame in frames],
        "csv": str(args.out),
        "rows": len(rows),
    }
    summary_path = args.summary or args.out.with_suffix(".json")
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"csv={args.out}")
    print(f"summary={summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
