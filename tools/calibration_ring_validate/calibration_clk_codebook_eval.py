#!/usr/bin/env python3
"""Evaluate Calibration CLK timing markers at raw PIO sample resolution.

The evaluator is deliberately hardware-agnostic: it compares known binary
waveforms under integer sample lag and reports how strongly adjacent lag
hypotheses can be separated.  It does not claim sub-sample hardware accuracy.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import asdict, dataclass
from io import StringIO


BARKER_13 = (1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 1)
CALIBRATION_CLK_MARKER_CANDIDATE_VERSION = 0
CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20 = 0
CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_40 = 1
CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_24 = 2
CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32 = 3
CALIBRATION_CLK_MARKER_LFSR_MASK = 0x8E
CALIBRATION_CLK_MARKER_LFSR_SEED = 0x01
CALIBRATION_CLK_MARKER_LOGICAL_BITS = 321


@dataclass(frozen=True)
class MarkerVector:
    version: int
    codebook_id: int
    epoch: int
    master_slot: int
    polarity: int
    header: int
    header_inverse: int
    header_crc8: int
    half_chip_samples: int
    logical_bits: int
    raw_samples: int
    raw_words: int
    timing_origin_sample: int
    timing_samples: int
    raw_sample_fnv1a32: int


@dataclass(frozen=True)
class Evaluation:
    name: str
    encoding: str
    code_bits: int
    lfsr_width: int
    lfsr_mask: int
    sample_ns: int
    half_chip_samples: int
    half_chip_ns: int
    marker_samples: int
    marker_ns: int
    transition_count: int
    min_level_samples: int
    max_level_samples: int
    adjacent_shift_distance: int
    min_wrong_lag_distance: int
    min_wrong_lag_samples: int
    guaranteed_sample_flip_correction: int
    search_radius_samples: int


def marker_header(version: int, codebook_id: int, epoch: int,
                  master_slot: int, polarity: int) -> int:
    if not 0 <= version <= 3:
        raise ValueError("version must fit two bits")
    if codebook_id not in (
        CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20,
            CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_40,
            CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_24,
            CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32):
        raise ValueError("unsupported candidate codebook")
    if not 0 <= epoch <= 0xFF:
        raise ValueError("epoch must fit eight bits")
    if not 0 <= master_slot <= 7:
        raise ValueError("master_slot must fit three bits")
    if polarity not in (0, 1):
        raise ValueError("polarity must fit one bit")
    return ((version & 0x3) << 14) | ((codebook_id & 0x3) << 12) | \
        (epoch << 4) | ((master_slot & 0x7) << 1) | polarity


def crc8_atm(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF \
                if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def _msb_bits(value: int, width: int) -> list[int]:
    return [(value >> bit) & 1 for bit in range(width - 1, -1, -1)]


def marker_logical_bits(version: int = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
                        codebook_id: int =
                        CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20,
                        epoch: int = 0, master_slot: int = 0,
                        polarity: int = 0) -> tuple[list[int], int, int]:
    header = marker_header(
        version, codebook_id, epoch, master_slot, polarity)
    header_inverse = header ^ 0xFFFF
    header_bytes = bytes((header >> 8, header & 0xFF,
                          header_inverse >> 8, header_inverse & 0xFF))
    header_crc8 = crc8_atm(header_bytes)
    timing, selected_mask = msequence(
        8, CALIBRATION_CLK_MARKER_LFSR_MASK,
        CALIBRATION_CLK_MARKER_LFSR_SEED)
    assert selected_mask == CALIBRATION_CLK_MARKER_LFSR_MASK
    bits = list(BARKER_13)
    bits.extend(_msb_bits(header, 16))
    bits.extend(_msb_bits(header_inverse, 16))
    bits.extend(_msb_bits(header_crc8, 8))
    bits.extend(timing)
    bits.extend(1 - bit for bit in BARKER_13)
    if len(bits) != CALIBRATION_CLK_MARKER_LOGICAL_BITS:
        raise AssertionError("candidate marker length drift")
    return bits, header, header_crc8


def marker_raw_waveform(
        version: int = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        codebook_id: int = CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20,
        epoch: int = 0, master_slot: int = 0,
        polarity: int = 0) -> tuple[list[int], MarkerVector]:
    half_chip_samples = {
        CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20: 5,
        CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_40: 10,
        CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_24: 6,
        CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32: 8,
    }.get(codebook_id)
    if half_chip_samples is None:
        raise ValueError("unsupported candidate codebook")
    bits, header, header_crc8 = marker_logical_bits(
        version, codebook_id, epoch, master_slot, polarity)
    raw = encode(bits, "manchester", half_chip_samples)
    digest = 0x811C9DC5
    for sample in raw:
        digest = ((digest ^ sample) * 0x01000193) & 0xFFFFFFFF
    vector = MarkerVector(
        version=version,
        codebook_id=codebook_id,
        epoch=epoch,
        master_slot=master_slot,
        polarity=polarity,
        header=header,
        header_inverse=header ^ 0xFFFF,
        header_crc8=header_crc8,
        half_chip_samples=half_chip_samples,
        logical_bits=len(bits),
        raw_samples=len(raw),
        raw_words=(len(raw) + 31) // 32,
        timing_origin_sample=(13 + 16 + 16 + 8) *
        2 * half_chip_samples,
        timing_samples=255 * 2 * half_chip_samples,
        raw_sample_fnv1a32=digest,
    )
    return raw, vector


def parse_csv_ints(value: str) -> list[int]:
    result = [int(item.strip(), 0) for item in value.split(",") if item.strip()]
    if not result:
        raise argparse.ArgumentTypeError("at least one integer is required")
    return result


def galois_period(width: int, mask: int, seed: int = 1) -> int:
    if width < 2 or seed <= 0 or seed >= (1 << width):
        return 0
    state = seed
    limit = (1 << width) - 1
    for period in range(1, limit + 1):
        lsb = state & 1
        state >>= 1
        if lsb:
            state ^= mask
        if state == 0:
            return 0
        if state == seed:
            return period
    return 0


def first_primitive_mask(width: int) -> int:
    expected = (1 << width) - 1
    for mask in range(1 << (width - 1), 1 << width):
        if galois_period(width, mask) == expected:
            return mask
    raise ValueError(f"no maximal-length Galois mask found for width={width}")


def msequence(width: int, mask: int | None = None,
              seed: int = 1) -> tuple[list[int], int]:
    selected_mask = first_primitive_mask(width) if mask is None else mask
    expected = (1 << width) - 1
    if galois_period(width, selected_mask, seed) != expected:
        raise ValueError(
            f"mask 0x{selected_mask:X} is not maximal length for width={width}")
    state = seed
    bits: list[int] = []
    for _ in range(expected):
        lsb = state & 1
        bits.append(lsb)
        state >>= 1
        if lsb:
            state ^= selected_mask
    return bits, selected_mask


def encode(bits: list[int] | tuple[int, ...], encoding: str,
           half_chip_samples: int) -> list[int]:
    if half_chip_samples < 1:
        raise ValueError("half_chip_samples must be positive")
    waveform: list[int] = []
    if encoding == "nrz":
        for bit in bits:
            waveform.extend([int(bit)] * (2 * half_chip_samples))
        return waveform
    if encoding == "manchester":
        for bit in bits:
            first = int(bit)
            waveform.extend([first] * half_chip_samples)
            waveform.extend([1 - first] * half_chip_samples)
        return waveform
    if encoding == "differential_manchester":
        level = 0
        for bit in bits:
            if int(bit) == 0:
                level ^= 1
            waveform.extend([level] * half_chip_samples)
            level ^= 1
            waveform.extend([level] * half_chip_samples)
        return waveform
    raise ValueError(f"unsupported encoding: {encoding}")


def run_lengths(waveform: list[int]) -> list[int]:
    if not waveform:
        return []
    lengths: list[int] = []
    start = 0
    for index in range(1, len(waveform)):
        if waveform[index] != waveform[index - 1]:
            lengths.append(index - start)
            start = index
    lengths.append(len(waveform) - start)
    return lengths


def shifted_hamming(waveform: list[int], shift: int,
                    idle_level: int = 0) -> int:
    distance = 0
    for index, expected in enumerate(waveform):
        source = index - shift
        observed = waveform[source] if 0 <= source < len(waveform) else idle_level
        distance += int(expected != observed)
    return distance


def evaluate(name: str, bits: list[int] | tuple[int, ...], encoding: str,
             sample_ns: int, half_chip_samples: int, window_ns: int,
             lfsr_width: int = 0, lfsr_mask: int = 0) -> Evaluation:
    waveform = encode(bits, encoding, half_chip_samples)
    lengths = run_lengths(waveform)
    transitions = max(0, len(lengths) - 1)
    radius = (window_ns + sample_ns - 1) // sample_ns
    wrong = [
        (shifted_hamming(waveform, shift), shift)
        for shift in range(-radius, radius + 1)
        if shift != 0
    ]
    min_distance, min_shift = min(wrong)
    adjacent = min(shifted_hamming(waveform, -1),
                   shifted_hamming(waveform, 1))
    return Evaluation(
        name=name,
        encoding=encoding,
        code_bits=len(bits),
        lfsr_width=lfsr_width,
        lfsr_mask=lfsr_mask,
        sample_ns=sample_ns,
        half_chip_samples=half_chip_samples,
        half_chip_ns=half_chip_samples * sample_ns,
        marker_samples=len(waveform),
        marker_ns=len(waveform) * sample_ns,
        transition_count=transitions,
        min_level_samples=min(lengths),
        max_level_samples=max(lengths),
        adjacent_shift_distance=adjacent,
        min_wrong_lag_distance=min_distance,
        min_wrong_lag_samples=min_shift,
        guaranteed_sample_flip_correction=max(0, (min_distance - 1) // 2),
        search_radius_samples=radius,
    )


def default_evaluations(sample_ns: int, half_chip_samples: list[int],
                        widths: list[int], window_ns: int) -> list[Evaluation]:
    codes: list[tuple[str, list[int] | tuple[int, ...], int, int]] = [
        ("barker13", BARKER_13, 0, 0),
    ]
    for width in widths:
        bits, mask = msequence(width)
        codes.append((f"mseq{len(bits)}", bits, width, mask))

    results: list[Evaluation] = []
    for name, bits, width, mask in codes:
        for half_samples in half_chip_samples:
            for encoding in ("nrz", "manchester", "differential_manchester"):
                results.append(evaluate(
                    name, bits, encoding, sample_ns, half_samples, window_ns,
                    lfsr_width=width, lfsr_mask=mask))
    return results


def csv_text(results: list[Evaluation]) -> str:
    output = StringIO()
    fields = list(Evaluation.__dataclass_fields__)
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    for result in results:
        writer.writerow(asdict(result))
    return output.getvalue()


def table_text(results: list[Evaluation]) -> str:
    header = (
        "name      encoding                  half_ns len_ns trans adj "
        "min_wrong wrong_shift correctable max_run_ns mask")
    lines = [header]
    for item in results:
        lines.append(
            f"{item.name:<9} {item.encoding:<25} "
            f"{item.half_chip_ns:>7} {item.marker_ns:>6} "
            f"{item.transition_count:>5} {item.adjacent_shift_distance:>3} "
            f"{item.min_wrong_lag_distance:>9} "
            f"{item.min_wrong_lag_samples:>11} "
            f"{item.guaranteed_sample_flip_correction:>11} "
            f"{item.max_level_samples * item.sample_ns:>10} "
            f"0x{item.lfsr_mask:X}")
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample-ns", type=int, default=4)
    parser.add_argument("--window-ns", type=int, default=52,
                        help="coarse one-sided lag search radius")
    parser.add_argument("--half-chip-samples", type=parse_csv_ints,
                        default=[5, 10], help="comma-separated PIO samples")
    parser.add_argument("--widths", type=parse_csv_ints, default=[5, 6, 7, 8],
                        help="comma-separated maximal LFSR widths")
    parser.add_argument("--format", choices=("table", "json", "csv"),
                        default="table")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.sample_ns < 1 or args.window_ns < args.sample_ns:
        raise SystemExit("sample-ns must be positive and window-ns >= sample-ns")
    if any(value < 1 for value in args.half_chip_samples):
        raise SystemExit("half-chip-samples must all be positive")
    if any(value < 2 or value > 12 for value in args.widths):
        raise SystemExit("LFSR widths must be in [2,12]")
    results = default_evaluations(
        args.sample_ns, args.half_chip_samples, args.widths, args.window_ns)
    if args.format == "json":
        print(json.dumps([asdict(item) for item in results], indent=2))
    elif args.format == "csv":
        sys.stdout.write(csv_text(results))
    else:
        sys.stdout.write(table_text(results))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
