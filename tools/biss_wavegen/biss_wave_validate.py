#!/usr/bin/env python3
"""Validate BiSS-C TAP CSV vectors generated for 1 MHz bench bring-up."""

from __future__ import annotations

import argparse
import csv
import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Profile:
    frame_bits: int
    position_offset: int
    position_bits: int
    modulo: int
    target: int
    anchor_offset: int
    anchor_bits: int
    anchor_mask: int
    anchor_value: int
    error_bit: int
    warning_bit: int
    status_gate: str
    crc_offset: int
    crc_bits: int
    crc_cover_offset: int
    crc_cover_bits: int
    crc_polynomial: int
    crc_init: int
    crc_xor: int
    crc_invert: bool
    sample_edge: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=Path("build/biss_wavegen/biss_1mhz.csv"))
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--frame-bits", type=int, default=48)
    parser.add_argument("--position-offset", type=int, default=8)
    parser.add_argument("--position-bits", type=int, default=24)
    parser.add_argument("--position-modulo", type=int, default=16_777_216)
    parser.add_argument("--target", type=int, default=100)
    parser.add_argument("--expect-positions", help="comma-separated expected positions")
    parser.add_argument("--anchor-offset", type=int, default=0)
    parser.add_argument("--anchor-bits", type=int, default=2)
    parser.add_argument("--anchor-mask", type=lambda value: int(value, 0), default=0x3)
    parser.add_argument("--anchor-value", type=lambda value: int(value, 0), default=0x2)
    parser.add_argument("--error-bit", type=int, default=-1)
    parser.add_argument("--warning-bit", type=int, default=-1)
    parser.add_argument("--status-gate", choices=("ignore", "count", "block"), default="ignore")
    parser.add_argument("--crc-offset", type=int, default=-1)
    parser.add_argument("--crc-bits", type=int, default=0)
    parser.add_argument("--crc-cover-offset", type=int, default=0)
    parser.add_argument("--crc-cover-bits", type=int, default=0)
    parser.add_argument("--crc-polynomial", type=lambda value: int(value, 0), default=0x03)
    parser.add_argument("--crc-init", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--crc-xor", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--crc-invert", action="store_true")
    parser.add_argument("--sample-edge", choices=("rising", "falling"), default="rising")
    return parser.parse_args()


def parse_int_list(text: str | None) -> list[int]:
    if not text:
        return []
    return [int(token.strip(), 0) for token in text.split(",") if token.strip()]


def read_summary(path: Path | None) -> dict:
    if path is None or not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def profile_from_args(args: argparse.Namespace, summary: dict) -> Profile:
    return Profile(
        frame_bits=int(summary.get("frame_bits", args.frame_bits)),
        position_offset=int(summary.get("position_offset", args.position_offset)),
        position_bits=int(summary.get("position_bits", args.position_bits)),
        modulo=args.position_modulo,
        target=args.target,
        anchor_offset=int(summary.get("anchor_offset", args.anchor_offset)),
        anchor_bits=int(summary.get("anchor_bits", args.anchor_bits)),
        anchor_mask=args.anchor_mask,
        anchor_value=int(summary.get("anchor_value", args.anchor_value)),
        error_bit=int(summary.get("error_bit", args.error_bit)),
        warning_bit=int(summary.get("warning_bit", args.warning_bit)),
        status_gate=args.status_gate,
        crc_offset=int(summary.get("crc_offset", args.crc_offset)),
        crc_bits=int(summary.get("crc_bits", args.crc_bits)),
        crc_cover_offset=int(summary.get("crc_cover_offset", args.crc_cover_offset)),
        crc_cover_bits=int(summary.get("crc_cover_bits", args.crc_cover_bits)),
        crc_polynomial=int(summary.get("crc_polynomial", args.crc_polynomial)),
        crc_init=int(summary.get("crc_init", args.crc_init)),
        crc_xor=int(summary.get("crc_xor", args.crc_xor)),
        crc_invert=bool(summary.get("crc_invert", args.crc_invert)),
        sample_edge=args.sample_edge,
    )


def read_rows(path: Path) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    with path.open("r", encoding="utf-8", newline="") as input_file:
        reader = csv.DictReader(input_file)
        for row in reader:
            rows.append({
                name: int(row[name])
                for name in ("time_ns", "clk", "data", "frame_index", "bit_index")
            })
    return rows


def is_active_edge(previous: dict[str, int], current: dict[str, int], sample_edge: str) -> bool:
    if sample_edge == "rising":
        return previous["clk"] == 0 and current["clk"] == 1
    return previous["clk"] == 1 and current["clk"] == 0


def decode_frames(rows: list[dict[str, int]], profile: Profile) -> tuple[list[int], list[str]]:
    failures: list[str] = []
    bits_by_frame: dict[int, dict[int, int]] = {}

    for previous, current in zip(rows, rows[1:]):
        if not is_active_edge(previous, current, profile.sample_edge):
            continue
        frame_index = current["frame_index"]
        bit_index = current["bit_index"]
        if frame_index < 0 or bit_index < 0:
            continue
        bits_by_frame.setdefault(frame_index, {})[bit_index] = current["data"] & 1

    frames: list[int] = []
    for frame_index in sorted(bits_by_frame):
        bits = bits_by_frame[frame_index]
        missing = [index for index in range(profile.frame_bits) if index not in bits]
        if missing:
            failures.append(f"frame {frame_index} missing bits: {missing[:8]}")
            continue
        frame = 0
        for bit_index in range(profile.frame_bits):
            frame = (frame << 1) | bits[bit_index]
        frames.append(frame)

    return frames, failures


def extract_bits_msb(frame: int, frame_bits: int, offset: int, bits: int) -> int:
    if bits <= 0:
        return 0
    shift = frame_bits - offset - bits
    mask = (1 << bits) - 1
    return (frame >> shift) & mask


def crc_compute_bits(value: int,
                     bits: int,
                     crc_bits: int,
                     polynomial: int,
                     init: int,
                     xor_value: int,
                     invert: bool) -> int:
    if crc_bits <= 0:
        return 0
    mask = (1 << crc_bits) - 1
    top_bit = 1 << (crc_bits - 1)
    crc = init & mask
    poly = polynomial & mask
    for index in range(bits):
        bit = ((value >> (bits - 1 - index)) & 1) != 0
        feedback = bit ^ ((crc & top_bit) != 0)
        crc = (crc << 1) & mask
        if feedback:
            crc ^= poly
    crc ^= xor_value & mask
    if invert:
        crc = (~crc) & mask
    return crc & mask


def crc_matches(frame: int, profile: Profile) -> bool:
    if profile.crc_offset < 0 or profile.crc_bits <= 0:
        return True
    covered = extract_bits_msb(frame,
                               profile.frame_bits,
                               profile.crc_cover_offset,
                               profile.crc_cover_bits)
    expected = crc_compute_bits(covered,
                                profile.crc_cover_bits,
                                profile.crc_bits,
                                profile.crc_polynomial,
                                profile.crc_init,
                                profile.crc_xor,
                                profile.crc_invert)
    actual = extract_bits_msb(frame, profile.frame_bits, profile.crc_offset, profile.crc_bits)
    return expected == actual


def crossed_position(last: int, current: int, target: int, modulo: int) -> bool:
    if modulo == 0 or target >= modulo or last >= modulo or current >= modulo or last == current:
        return False
    if last < current:
        return last < target <= current
    return target > last or target <= current


def validate_frames(frames: list[int],
                    profile: Profile,
                    expected_positions: list[int],
                    expected_frames: list[int]) -> tuple[dict, list[str]]:
    failures: list[str] = []
    positions: list[int] = []
    status_block_count = 0
    crc_error_count = 0
    crossing_count = 0
    last_position = 0

    if expected_frames and frames != expected_frames:
        failures.append("decoded frames do not match summary frames_hex")

    for index, frame in enumerate(frames):
        if profile.anchor_bits > 0:
            anchor = extract_bits_msb(frame, profile.frame_bits, profile.anchor_offset, profile.anchor_bits)
            if (anchor & profile.anchor_mask) != (profile.anchor_value & profile.anchor_mask):
                failures.append(f"frame {index} anchor mismatch: 0x{anchor:X}")

        position = extract_bits_msb(frame, profile.frame_bits, profile.position_offset, profile.position_bits)
        positions.append(position)

        error_active = profile.error_bit >= 0 and extract_bits_msb(frame, profile.frame_bits, profile.error_bit, 1) == 0
        warning_active = profile.warning_bit >= 0 and extract_bits_msb(frame, profile.frame_bits, profile.warning_bit, 1) == 0
        status_blocked = profile.status_gate == "block" and (error_active or warning_active)
        if error_active or warning_active:
            status_block_count += 1

        crc_ok = crc_matches(frame, profile)
        if not crc_ok:
            crc_error_count += 1
            failures.append(f"frame {index} CRC mismatch")

        if crc_ok and not status_blocked and crossed_position(last_position,
                                                              position,
                                                              profile.target,
                                                              profile.modulo):
            crossing_count += 1
        last_position = position

    if expected_positions and positions != expected_positions:
        failures.append(f"positions mismatch: expected {expected_positions} got {positions}")

    report = {
        "frames_hex": [f"0x{frame:0{(profile.frame_bits + 3) // 4}X}" for frame in frames],
        "positions": positions,
        "frame_count": len(frames),
        "status_block_count": status_block_count,
        "crc_error_count": crc_error_count,
        "crossing_count": crossing_count,
    }
    return report, failures


def main() -> int:
    args = parse_args()
    summary_path = args.summary or args.csv.with_suffix(".json")
    out_path = args.out or args.csv.with_name(f"{args.csv.stem}_validate.json")
    summary = read_summary(summary_path)
    profile = profile_from_args(args, summary)
    rows = read_rows(args.csv)
    frames, failures = decode_frames(rows, profile)

    expected_positions = parse_int_list(args.expect_positions)
    if not expected_positions:
        expected_positions = [int(value) for value in summary.get("positions", [])]
    expected_frames = [int(value, 0) for value in summary.get("frames_hex", [])]

    report, validation_failures = validate_frames(frames, profile, expected_positions, expected_frames)
    failures.extend(validation_failures)

    output = {
        "csv": str(args.csv),
        "summary": str(summary_path) if summary_path.exists() else None,
        "passed": not failures,
        "failures": failures,
        "profile": profile.__dict__,
        "report": report,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(f"out={out_path}")
    if failures:
        print("FAIL")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("PASS")
    print(f"frames={report['frame_count']} crossings={report['crossing_count']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
