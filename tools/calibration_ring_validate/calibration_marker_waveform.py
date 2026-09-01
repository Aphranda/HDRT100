#!/usr/bin/env python3
"""Download and compare TRN-00 marker waveforms saved on node SD cards.

The firmware captures two digital channels every ``tick_resolution_ns``:
channel 0 is the node forward output and channel 1 is its incoming link.  The
alignment scan expands those measured samples with zero-order hold so a 4 ns
capture can be shifted on a 1 ns grid.  This is an offline matching aid; it
does not turn the underlying capture into a 1 ns measurement.
"""

from __future__ import annotations

import argparse
import csv
import html
import json
import sys
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import (  # noqa: E402
    board_command,
    close_persistent_connections,
    discover,
)
from calibration_ring_validate.calibration_clk_codebook_eval import (  # noqa: E402
    BARKER_13,
    crc8_atm,
    marker_raw_waveform,
)


CAPTURE_SCHEMAS = {
    "HAOFV_MARKER_CAPTURE_V1",  # legacy half-chip-base evidence
    "HAOFV_MARKER_CAPTURE_V2",  # explicit per-link base evidence
}
CHANNEL_INDEX = {
    "forward_output": 0,
    "incoming_link": 1,
    "channel_0": 0,
    "channel_1": 1,
}
MARKER_FLAG_NAMES = {
    1 << 0: "SOF_VALID",
    1 << 1: "MANCHESTER_VALID",
    1 << 2: "HEADER_INVERSE_VALID",
    1 << 3: "HEADER_CRC_VALID",
    1 << 4: "HEADER_MATCH",
    1 << 5: "EOF_VALID",
}
MARKER_FLAG_ALL = sum(MARKER_FLAG_NAMES)
CORRELATION_REJECT_NAMES = {
    0: "NONE",
    1: "BAD_ARGUMENT",
    2: "SEARCH_RANGE",
    3: "CAPTURE_TRUNCATED",
    4: "POLARITY",
    5: "SOF",
    6: "MANCHESTER",
    7: "HEADER_INVERSE",
    8: "HEADER_CRC",
    9: "HEADER_MISMATCH",
    10: "EOF",
    11: "DISTANCE",
    12: "MARGIN",
}
CORRELATION_MAX_LAGS = 256
DEFAULT_GLOBAL_SHIFT_LIMIT_NS = 2048
DEFAULT_SVG_WINDOW_NS = 1000


def _csv(raw: str) -> list[str]:
    return [value.strip().strip('"') for value in next(csv.reader([raw]), [])]


def parse_file_read_response(raw: str, expected_offset: int) -> dict[str, object]:
    fields = _csv(raw)
    if len(fields) != 10 or fields[0] != "OK":
        raise ValueError(f"storage read rejected: {raw!r}")
    offset = int(fields[2], 0)
    requested = int(fields[3], 0)
    returned = int(fields[4], 0)
    file_size = int(fields[5], 0)
    eof = int(fields[6], 0) != 0
    error = int(fields[8], 0)
    if offset != expected_offset or error != 0:
        raise ValueError(
            f"storage read mismatch: offset={offset}, expected={expected_offset}, "
            f"error={error}")
    try:
        payload = bytes.fromhex(fields[9])
    except ValueError as exc:
        raise ValueError("storage read returned invalid hex") from exc
    if len(payload) != returned or returned > requested:
        raise ValueError(
            f"storage read length mismatch: hex={len(payload)}, "
            f"returned={returned}, requested={requested}")
    return {
        "job_id": int(fields[1], 0),
        "offset": offset,
        "requested": requested,
        "returned": returned,
        "file_size": file_size,
        "eof": eof,
        "path_hash": int(fields[7], 0),
        "payload": payload,
    }


def download_capture(args: argparse.Namespace) -> dict[str, object]:
    if not args.sd_path.startswith("/") or '"' in args.sd_path:
        raise ValueError("sd-path must be an absolute SD path without quotes")
    args.board_ids = [args.board_id]
    args.keep_open = True
    boards = discover(args)
    if args.board_id not in boards:
        raise RuntimeError(f"node {args.board_id} was not discovered")
    board = boards[args.board_id]
    if args.expected_build is not None and board.build != args.expected_build:
        raise RuntimeError(
            f"{args.board_id}: build {board.build}, expected {args.expected_build}")

    data = bytearray()
    file_size: int | None = None
    path_hash: int | None = None
    try:
        while file_size is None or len(data) < file_size:
            requested = args.chunk_size
            if file_size is not None:
                requested = min(requested, file_size - len(data))
            command = (
                f'SYSTem:STORage:FILE:READ? "{args.sd_path}",'
                f'{len(data)},{requested}')
            page = parse_file_read_response(
                board_command(board, command, args), len(data))
            if file_size is None:
                file_size = int(page["file_size"])
                path_hash = int(page["path_hash"])
            elif int(page["file_size"]) != file_size:
                raise RuntimeError("SD file size changed during download")
            payload = page["payload"]
            assert isinstance(payload, bytes)
            if not payload and not bool(page["eof"]):
                raise RuntimeError("SD read made no progress before EOF")
            data.extend(payload)
            if bool(page["eof"]):
                break
    finally:
        close_persistent_connections()

    if file_size is None or len(data) != file_size:
        raise RuntimeError(
            f"SD download incomplete: received={len(data)}, size={file_size}")
    capture = json.loads(data.decode("utf-8"))
    validate_capture(capture)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    return {
        "schema": "HAOFV_MARKER_CAPTURE_DOWNLOAD_V1",
        "node_id": args.board_id,
        "port": board.port,
        "build_id": board.build,
        "sd_path": args.sd_path,
        "local_path": str(args.out),
        "size": len(data),
        "path_hash": path_hash,
        "capture_node": int(capture["node"]),
        "capture_epoch": int(capture["epoch"]),
        "capture_generation": int(capture["calibration_generation"]),
    }


def validate_capture(capture: object) -> dict[str, object]:
    if not isinstance(capture, dict) or capture.get("schema") not in CAPTURE_SCHEMAS:
        raise ValueError(
            f"capture schema must be one of {sorted(CAPTURE_SCHEMAS)}")
    words = capture.get("raw_interleaved_words")
    if not isinstance(words, list) or not words:
        raise ValueError("capture has no raw_interleaved_words")
    count = int(capture.get("raw_interleaved_word_count", -1))
    capacity = int(capture.get("raw_interleaved_sample_capacity", -1))
    sample_count = int(capture.get("raw_interleaved_sample_count", capacity))
    tick_ns = int(capture.get("tick_resolution_ns", 0))
    if (count != len(words) or capacity != count * 16 or
            sample_count <= 0 or sample_count > capacity or tick_ns <= 0):
        raise ValueError("capture word count/sample capacity/tick is inconsistent")
    if any(not isinstance(word, int) or word < 0 or word > 0xFFFFFFFF
           for word in words):
        raise ValueError("capture contains a non-u32 raw word")
    return capture


def load_capture(path: Path) -> dict[str, object]:
    return validate_capture(json.loads(path.read_text(encoding="utf-8")))


def unpack_channels(capture: dict[str, object]) -> tuple[list[int], list[int]]:
    channel_0: list[int] = []
    channel_1: list[int] = []
    words = capture["raw_interleaved_words"]
    assert isinstance(words, list)
    for raw_word in words:
        word = int(raw_word)
        for sample in range(16):
            pair = (word >> (sample * 2)) & 0x3
            channel_0.append(pair & 0x1)
            channel_1.append((pair >> 1) & 0x1)
    sample_count = int(capture.get(
        "raw_interleaved_sample_count", len(channel_0)))
    return channel_0[:sample_count], channel_1[:sample_count]


def expand_zero_order_hold(samples: Sequence[int], tick_ns: int,
                           step_ns: int) -> list[int]:
    if step_ns <= 0 or tick_ns <= 0 or tick_ns % step_ns != 0:
        raise ValueError("step-ns must be a positive integer divisor of tick_resolution_ns")
    repeat = tick_ns // step_ns
    return [int(value) for value in samples for _ in range(repeat)]


def transition_positions(samples: Sequence[int], tick_ns: int) -> list[int]:
    return [index * tick_ns for index in range(1, len(samples))
            if samples[index] != samples[index - 1]]


def _ranges(values: Sequence[int], step: int) -> list[list[int]]:
    if not values:
        return []
    ranges: list[list[int]] = []
    start = previous = values[0]
    for value in values[1:]:
        if value != previous + step:
            ranges.append([start, previous])
            start = value
        previous = value
    ranges.append([start, previous])
    return ranges


def scan_alignment(reference: Sequence[int], candidate: Sequence[int], *,
                   shift_min_ns: int, shift_max_ns: int, step_ns: int,
                   window_start: int = 0,
                   window_end: int | None = None) -> dict[str, object]:
    if shift_min_ns > shift_max_ns:
        raise ValueError("shift-min-ns must not exceed shift-max-ns")
    if shift_min_ns % step_ns or shift_max_ns % step_ns:
        raise ValueError("shift limits must be integer multiples of step-ns")
    lag_min = shift_min_ns // step_ns
    lag_max = shift_max_ns // step_ns
    start = max(window_start, 0, -lag_min)
    end = min(
        len(reference) if window_end is None else window_end,
        len(reference), len(candidate) - lag_max)
    if end <= start:
        raise ValueError("no common comparison window across the shift range")

    rows: list[dict[str, object]] = []
    for inverted in (False, True):
        for lag in range(lag_min, lag_max + 1):
            distance = 0
            for index in range(start, end):
                value = int(candidate[index + lag]) ^ int(inverted)
                distance += int(reference[index]) != value
            rows.append({
                "polarity": "inverted" if inverted else "normal",
                "candidate_delay_ns": lag * step_ns,
                "move_candidate_by_ns": -lag * step_ns,
                "distance": distance,
                "mismatch_rate": distance / (end - start),
                "comparison_sample_count": end - start,
            })

    best_by_polarity: dict[str, dict[str, object]] = {}
    for polarity in ("normal", "inverted"):
        polarity_rows = [row for row in rows if row["polarity"] == polarity]
        minimum = min(int(row["distance"]) for row in polarity_rows)
        tied = [int(row["candidate_delay_ns"]) for row in polarity_rows
                if int(row["distance"]) == minimum]
        first = next(row for row in polarity_rows
                     if int(row["distance"]) == minimum)
        best_by_polarity[polarity] = {
            **first,
            "equally_best_candidate_delays_ns": tied,
            "equally_best_candidate_delay_ranges_ns": _ranges(tied, step_ns),
        }
    best = min(best_by_polarity.values(), key=lambda row: (
        int(row["distance"]),
        0 if row["polarity"] == "normal" else 1,
        abs(int(row["candidate_delay_ns"])),
    ))
    return {
        "shift_convention": (
            "positive candidate_delay_ns means candidate occurs later than "
            "reference and must move left by the reported move_candidate_by_ns"),
        "step_ns": step_ns,
        "shift_min_ns": shift_min_ns,
        "shift_max_ns": shift_max_ns,
        "comparison_window_samples": [start, end],
        "comparison_sample_count": end - start,
        "best": best,
        "best_by_polarity": best_by_polarity,
        "scan": rows,
    }


def channel_summary(samples: Sequence[int], tick_ns: int) -> dict[str, object]:
    transitions = transition_positions(samples, tick_ns)
    return {
        "sample_count": len(samples),
        "duration_ns": len(samples) * tick_ns,
        "high_count": sum(samples),
        "low_count": len(samples) - sum(samples),
        "transition_count": len(transitions),
        "transition_positions_ns": transitions,
    }


def firmware_rx_samples(capture: dict[str, object], channel: str, *,
                        half_chip_samples: int, role: str) -> tuple[list[int], int]:
    """Reproduce calibration_manager_marker_extract_rx on saved raw samples."""
    samples = unpack_channels(capture)[CHANNEL_INDEX[channel]]
    if role not in ("origin", "follower"):
        raise ValueError("role must be origin or follower")
    if (role == "origin" and
            capture.get("capture_anchor") != "physical_rx_csn_falling_edge"):
        return samples, 0
    offset = int(capture.get("offset_sample_count", 0))
    if not -10 <= offset <= 10:
        raise ValueError("capture offset_sample_count is outside [-10, 10]")
    tick_ns = int(capture.get("tick_resolution_ns", 0))
    if tick_ns <= 0:
        raise ValueError("capture tick_resolution_ns must be positive")
    if "link_base_delay_ns" in capture:
        link_base_ns = int(capture["link_base_delay_ns"])
        if link_base_ns <= 0:
            raise ValueError("capture link base must be positive")
        phase_base_samples = (link_base_ns + tick_ns // 2) // tick_ns
    else:
        # V1 evidence predates explicit per-link bases.
        phase_base_samples = half_chip_samples
    phase_delay_cycles = phase_base_samples + offset
    if not 0 <= phase_delay_cycles <= 31:
        raise ValueError("capture phase delay is outside the PIO [0, 31] range")
    prefix = half_chip_samples + 1 + phase_delay_cycles
    return ([1] * half_chip_samples +
            [0] * (prefix - half_chip_samples) + samples), prefix


def _decode_manchester_bit(samples: Sequence[int], marker_start: int,
                           logical_index: int,
                           half_chip_samples: int) -> int | None:
    raw_start = marker_start + logical_index * 2 * half_chip_samples
    first = samples[raw_start:raw_start + half_chip_samples]
    second = samples[raw_start + half_chip_samples:
                     raw_start + 2 * half_chip_samples]
    if len(first) != half_chip_samples or len(second) != half_chip_samples:
        return None
    first_ones = sum(first)
    second_ones = sum(second)
    if (first_ones * 2 == half_chip_samples or
            second_ones * 2 == half_chip_samples or
            (first_ones > half_chip_samples // 2) ==
            (second_ones > half_chip_samples // 2)):
        return None
    return int(first_ones > half_chip_samples // 2)


def _decode_msb_field(samples: Sequence[int], marker_start: int,
                      logical_offset: int, width: int,
                      half_chip_samples: int) -> int | None:
    value = 0
    for bit in range(width):
        decoded = _decode_manchester_bit(
            samples, marker_start, logical_offset + bit, half_chip_samples)
        if decoded is None:
            return None
        value = (value << 1) | decoded
    return value


def validate_marker_window(samples: Sequence[int], marker_start: int, *,
                           half_chip_samples: int,
                           expected_header: int) -> dict[str, object]:
    """Reproduce calibration_clk_marker_validate_capture and expose fields."""
    sof_offset = 0
    header_offset = len(BARKER_13)
    header_inverse_offset = header_offset + 16
    crc_offset = header_inverse_offset + 16
    eof_offset = crc_offset + 8 + 255
    flags = 1 << 1
    sof_valid = True
    eof_valid = True
    for index, expected_sof in enumerate(BARKER_13):
        sof = _decode_manchester_bit(
            samples, marker_start, sof_offset + index, half_chip_samples)
        eof = _decode_manchester_bit(
            samples, marker_start, eof_offset + index, half_chip_samples)
        if sof is None or eof is None:
            flags &= ~(1 << 1)
            sof_valid = False
            eof_valid = False
            break
        sof_valid = sof_valid and sof == expected_sof
        eof_valid = eof_valid and eof == 1 - expected_sof
    if sof_valid:
        flags |= 1 << 0
    if eof_valid:
        flags |= 1 << 5

    header = _decode_msb_field(
        samples, marker_start, header_offset, 16, half_chip_samples)
    header_inverse = _decode_msb_field(
        samples, marker_start, header_inverse_offset, 16, half_chip_samples)
    crc = _decode_msb_field(
        samples, marker_start, crc_offset, 8, half_chip_samples)
    if header is None or header_inverse is None or crc is None:
        flags &= ~(1 << 1)
    else:
        if (header ^ header_inverse) == 0xFFFF:
            flags |= 1 << 2
        header_bytes = bytes((header >> 8, header & 0xFF,
                              header_inverse >> 8, header_inverse & 0xFF))
        if crc == crc8_atm(header_bytes):
            flags |= 1 << 3
        if header == expected_header:
            flags |= 1 << 4
    return {
        "flags": flags,
        "flag_names": [name for bit, name in MARKER_FLAG_NAMES.items()
                       if flags & bit],
        "all_flags_valid": flags == MARKER_FLAG_ALL,
        "decoded_header": header,
        "decoded_header_inverse": header_inverse,
        "decoded_crc8": crc,
    }


def correlation_reject_from_marker_flags(flags: int) -> int:
    for bit, reason in ((1 << 1, 6), (1 << 0, 5), (1 << 2, 7),
                        (1 << 3, 8), (1 << 4, 9), (1 << 5, 10)):
        if flags & bit == 0:
            return reason
    return 0


def firmware_correlate(reference: Sequence[int], capture: Sequence[int], *,
                       max_best_distance: int = 512,
                       min_margin: int = 0) -> dict[str, object]:
    """Reproduce calibration_clk_marker_correlate at measured sample resolution."""
    available_lag = len(capture) - len(reference)
    max_lag = min(available_lag, CORRELATION_MAX_LAGS - 1)
    if max_lag < 1:
        reason = 3 if available_lag < 0 else 2
        return {
            "accepted": False,
            "reject_reason": reason,
            "reject_name": CORRELATION_REJECT_NAMES[reason],
            "candidate_count": max(0, max_lag + 1),
            "scan": [],
        }
    rows: list[dict[str, int]] = []
    for lag in range(max_lag + 1):
        observed = capture[lag:lag + len(reference)]
        normal = sum(int(expected) != int(actual)
                     for expected, actual in zip(reference, observed))
        inverted = len(reference) - normal
        rows.append({"lag_sample": lag, "normal_distance": normal,
                     "inverted_distance": inverted})
    ranked_normal = sorted(rows, key=lambda row: (
        row["normal_distance"], row["lag_sample"]))
    best = ranked_normal[0]
    second = ranked_normal[1]
    inverted_best = min(rows, key=lambda row: (
        row["inverted_distance"], row["lag_sample"]))
    detected_polarity = (
        "inverted" if inverted_best["inverted_distance"] <
        best["normal_distance"] else "normal")
    result: dict[str, object] = {
        "accepted": False,
        "candidate_count": len(rows),
        "best_lag_sample": best["lag_sample"],
        "best_distance": best["normal_distance"],
        "second_lag_sample": second["lag_sample"],
        "second_distance": second["normal_distance"],
        "margin": second["normal_distance"] - best["normal_distance"],
        "inverted_best_lag_sample": inverted_best["lag_sample"],
        "inverted_best_distance": inverted_best["inverted_distance"],
        "detected_polarity": detected_polarity,
        "scan": rows,
    }
    reason = 4 if detected_polarity == "inverted" else 0
    result["reject_reason"] = reason
    result["reject_name"] = CORRELATION_REJECT_NAMES[reason]
    return result


def correlate_capture(args: argparse.Namespace) -> dict[str, object]:
    capture = load_capture(args.capture)
    epoch = int(capture["epoch"]) if args.epoch is None else args.epoch
    node = int(capture["node"])
    role = args.role
    if role == "auto":
        role = "origin" if node == args.master_node else "follower"
    expected, vector = marker_raw_waveform(
        codebook_id=args.codebook_id, epoch=epoch,
        source_node=args.master_node, polarity=0)
    sample_count_source = "capture"
    replay_capture = capture
    if "raw_interleaved_sample_count" not in capture:
        inferred_sample_count = len(expected) + (256 if role == "origin" else 8)
        capacity = int(capture["raw_interleaved_sample_capacity"])
        if inferred_sample_count > capacity:
            raise ValueError("inferred firmware sample count exceeds capture capacity")
        replay_capture = {**capture,
                          "raw_interleaved_sample_count": inferred_sample_count}
        sample_count_source = "inferred_from_firmware_request"
    observed, prefix = firmware_rx_samples(
        replay_capture, args.channel, half_chip_samples=vector.half_chip_samples,
        role=role)
    correlation = firmware_correlate(
        expected, observed, max_best_distance=args.max_best_distance,
        min_margin=args.min_margin)
    if correlation.get("detected_polarity") == "normal":
        validation = validate_marker_window(
            observed, int(correlation["best_lag_sample"]),
            half_chip_samples=vector.half_chip_samples,
            expected_header=vector.header)
        reason = correlation_reject_from_marker_flags(
            int(validation["flags"]))
        if reason == 0 and int(correlation["best_distance"]) > args.max_best_distance:
            reason = 11
        if reason == 0 and int(correlation["margin"]) < args.min_margin:
            reason = 12
        correlation["marker_validation"] = validation
        correlation["reject_reason"] = reason
        correlation["reject_name"] = CORRELATION_REJECT_NAMES[reason]
        correlation["accepted"] = reason == 0

    tick_ns = int(capture["tick_resolution_ns"])
    requested_global_limit_ns = int(getattr(
        args, "global_shift_limit_ns", DEFAULT_GLOBAL_SHIFT_LIMIT_NS))
    if requested_global_limit_ns <= 0:
        raise ValueError("global-shift-limit-ns must be positive")
    global_limit_ns = min(
        requested_global_limit_ns,
        ((min(len(expected), len(observed)) - 1) // 2) * tick_ns)
    global_limit_ns -= global_limit_ns % tick_ns
    if global_limit_ns <= 0:
        raise ValueError("capture is too short for signed global alignment")
    global_alignment = scan_alignment(
        expected, observed,
        shift_min_ns=-global_limit_ns,
        shift_max_ns=global_limit_ns,
        step_ns=tick_ns)
    global_best = global_alignment["best"]
    assert isinstance(global_best, dict)
    global_best["waveform_move_candidate_by_ns"] = int(
        global_best["move_candidate_by_ns"])
    global_best["waveform_move_candidate_by_samples"] = (
        int(global_best["move_candidate_by_ns"]) // tick_ns)
    current_offset = int(capture.get("offset_sample_count", 0))
    if role == "follower":
        offset_delta = int(global_best["move_candidate_by_ns"]) // tick_ns
        recommended_offset = current_offset + offset_delta
        global_best["recommended_offset_delta_samples"] = offset_delta
        global_best["recommended_offset_sample_count"] = recommended_offset
        global_best["recommended_offset_within_current_range"] = (
            -10 <= recommended_offset <= 10)
        global_best["offset_direction_model"] = (
            "increasing offset adds reconstructed prefix samples and moves "
            "the candidate waveform right")
    else:
        global_best["recommended_offset_delta_samples"] = None
        global_best["recommended_offset_sample_count"] = None
        global_best["recommended_offset_within_current_range"] = None

    best_lag = correlation.get("best_lag_sample")
    marker_start_raw = best_lag - prefix if isinstance(best_lag, int) else None
    result: dict[str, object] = {
        "schema": "HAOFV_MARKER_FIRMWARE_REPLAY_V1",
        "capture": str(args.capture),
        "capture_node": node,
        "channel": args.channel,
        "role": role,
        "epoch": epoch,
        "codebook_id": args.codebook_id,
        "master_node": args.master_node,
        "measured_tick_resolution_ns": tick_ns,
        "offset_sample_count": current_offset,
        "raw_interleaved_sample_count": int(
            replay_capture["raw_interleaved_sample_count"]),
        "raw_interleaved_sample_count_source": sample_count_source,
        "firmware_prefix_samples": prefix,
        "firmware_prefix_ns": prefix * tick_ns,
        "expected_marker_samples": len(expected),
        "expected_marker_ns": len(expected) * tick_ns,
        "marker_window": {
            "start_in_firmware_samples": best_lag,
            "start_in_raw_capture_samples": marker_start_raw,
            "start_in_raw_capture_ns": (
                marker_start_raw * tick_ns
                if marker_start_raw is not None else None),
            "end_in_firmware_samples": (
                best_lag + len(expected) if isinstance(best_lag, int) else None),
        },
        "gate": {
            "max_best_distance": args.max_best_distance,
            "min_margin": args.min_margin,
        },
        "correlation": correlation,
        "global_alignment": global_alignment,
    }
    if args.svg is not None:
        reference_1ns = expand_zero_order_hold(expected, tick_ns, 1)
        observed_1ns = expand_zero_order_hold(observed, tick_ns, 1)
        best_delay_ns = int(global_best["candidate_delay_ns"])
        svg = render_alignment_svg(
            reference_1ns, observed_1ns,
            step_ns=1,
            measured_tick_ns=tick_ns,
            best_delay_ns=best_delay_ns,
            window_start_ns=args.svg_window_start_ns,
            window_duration_ns=args.svg_window_duration_ns,
            title=(f"node{node} capture replay: expected marker vs "
                   f"reconstructed {args.channel}; offset "
                   f"{int(capture.get('offset_sample_count', 0)):+d} samples"))
        args.svg.parent.mkdir(parents=True, exist_ok=True)
        args.svg.write_text(svg, encoding="utf-8")
        result["svg"] = {
            "path": str(args.svg),
            "comparison": "expected_marker_vs_firmware_reconstructed_capture",
            "offset_sample_count": int(capture.get("offset_sample_count", 0)),
            "firmware_best_lag_sample": int(correlation["best_lag_sample"]),
            "firmware_best_delay_ns": int(correlation["best_lag_sample"]) * tick_ns,
            "global_best_candidate_delay_ns": best_delay_ns,
            "global_waveform_move_candidate_by_ns": int(
                global_best["move_candidate_by_ns"]),
            "window_start_ns": args.svg_window_start_ns,
            "window_duration_ns": args.svg_window_duration_ns,
        }
    return result


def correlation_console_summary(result: dict[str, object], out: Path) -> dict[str, object]:
    correlation = result["correlation"]
    assert isinstance(correlation, dict)
    compact = {key: value for key, value in correlation.items() if key != "scan"}
    summary = {
        "schema": result["schema"],
        "out": str(out),
        "capture_node": result["capture_node"],
        "role": result["role"],
        "marker_window": result["marker_window"],
        "correlation": compact,
        "global_best": result["global_alignment"]["best"],
    }
    if "svg" in result:
        summary["svg"] = result["svg"]
    return summary


def analyze_capture(args: argparse.Namespace) -> dict[str, object]:
    candidate_capture = load_capture(args.candidate)
    reference_capture = (
        load_capture(args.reference) if args.reference is not None
        else candidate_capture)
    candidate_channels = unpack_channels(candidate_capture)
    reference_channels = unpack_channels(reference_capture)
    candidate_samples = candidate_channels[CHANNEL_INDEX[args.candidate_channel]]
    reference_samples = reference_channels[CHANNEL_INDEX[args.reference_channel]]
    candidate_tick = int(candidate_capture["tick_resolution_ns"])
    reference_tick = int(reference_capture["tick_resolution_ns"])
    if candidate_tick != reference_tick:
        raise ValueError("reference and candidate tick_resolution_ns differ")

    reference_1ns = expand_zero_order_hold(
        reference_samples, reference_tick, args.step_ns)
    candidate_1ns = expand_zero_order_hold(
        candidate_samples, candidate_tick, args.step_ns)
    window_start = 0
    window_end: int | None = None
    if args.window == "active":
        transitions = transition_positions(reference_samples, 1)
        if transitions:
            margin_samples = max(1, args.active_margin_ns // reference_tick)
            first = transitions[0]
            last = transitions[-1]
            window_start = max(0, first - margin_samples) * (
                reference_tick // args.step_ns)
            window_end = min(len(reference_samples), last + margin_samples + 1) * (
                reference_tick // args.step_ns)

    alignment = scan_alignment(
        reference_1ns, candidate_1ns,
        shift_min_ns=args.shift_min_ns,
        shift_max_ns=args.shift_max_ns,
        step_ns=args.step_ns,
        window_start=window_start,
        window_end=window_end)
    result: dict[str, object] = {
        "schema": "HAOFV_MARKER_WAVEFORM_ANALYSIS_V1",
        "reference": {
            "path": str(args.reference or args.candidate),
            "channel": args.reference_channel,
            "capture_node": int(reference_capture["node"]),
            "capture_epoch": int(reference_capture["epoch"]),
            "capture_generation": int(reference_capture["calibration_generation"]),
            **channel_summary(reference_samples, reference_tick),
        },
        "candidate": {
            "path": str(args.candidate),
            "channel": args.candidate_channel,
            "capture_node": int(candidate_capture["node"]),
            "capture_epoch": int(candidate_capture["epoch"]),
            "capture_generation": int(candidate_capture["calibration_generation"]),
            **channel_summary(candidate_samples, candidate_tick),
        },
        "measured_tick_resolution_ns": reference_tick,
        "scan_step_ns": args.step_ns,
        "resampling_model": "zero_order_hold",
        "one_ns_scan_is_not_one_ns_measurement_resolution": True,
        "window": args.window,
        "alignment": alignment,
    }
    if args.svg is not None:
        best = alignment["best"]
        assert isinstance(best, dict)
        svg = render_alignment_svg(
            reference_1ns, candidate_1ns,
            step_ns=args.step_ns,
            measured_tick_ns=reference_tick,
            best_delay_ns=int(best["candidate_delay_ns"]),
            window_start_ns=args.svg_window_start_ns,
            window_duration_ns=args.svg_window_duration_ns,
            title=(f"node{int(candidate_capture['node'])} marker waveform: "
                   f"{args.reference_channel} vs "
                   f"{args.candidate_channel}"))
        args.svg.parent.mkdir(parents=True, exist_ok=True)
        args.svg.write_text(svg, encoding="utf-8")
        result["svg"] = {
            "path": str(args.svg),
            "best_candidate_delay_ns": int(best["candidate_delay_ns"]),
            "window_start_ns": args.svg_window_start_ns,
            "window_duration_ns": args.svg_window_duration_ns,
        }
    return result


def analysis_console_summary(result: dict[str, object], out: Path) -> dict[str, object]:
    alignment = result["alignment"]
    assert isinstance(alignment, dict)
    best = alignment["best"]
    assert isinstance(best, dict)
    summary = {
        "schema": result["schema"],
        "out": str(out),
        "measured_tick_resolution_ns": result["measured_tick_resolution_ns"],
        "scan_step_ns": result["scan_step_ns"],
        "one_ns_scan_is_not_one_ns_measurement_resolution": result[
            "one_ns_scan_is_not_one_ns_measurement_resolution"],
        "best": best,
    }
    if "svg" in result:
        summary["svg"] = result["svg"]
    return summary


def _svg_step_path(samples: Sequence[int], *, start: int, end: int,
                   lag: int, x0: float, width: float,
                   y_high: float, y_low: float) -> str:
    valid = [(index, int(samples[index + lag]))
             for index in range(start, end)
             if 0 <= index + lag < len(samples)]
    if not valid:
        return ""
    scale = width / (end - start)
    first_index, first_value = valid[0]
    y = y_high if first_value else y_low
    commands = [f"M {x0 + (first_index - start) * scale:.2f} {y:.2f}"]
    previous = first_value
    for index, value in valid[1:]:
        x = x0 + (index - start) * scale
        if value != previous:
            old_y = y_high if previous else y_low
            new_y = y_high if value else y_low
            commands.append(f"H {x:.2f} V {new_y:.2f}")
            previous = value
    commands.append(f"H {x0 + (valid[-1][0] + 1 - start) * scale:.2f}")
    return " ".join(commands)


def _svg_mismatch_rects(reference: Sequence[int], candidate: Sequence[int], *,
                        start: int, end: int, lag: int,
                        x0: float, width: float,
                        y: float, height: float) -> str:
    scale = width / (end - start)
    mismatch = []
    for index in range(start, min(end, len(reference))):
        candidate_index = index + lag
        if (0 <= candidate_index < len(candidate) and
                int(reference[index]) != int(candidate[candidate_index])):
            mismatch.append(index)
    ranges: list[tuple[int, int]] = []
    if mismatch:
        range_start = previous = mismatch[0]
        for index in mismatch[1:]:
            if index != previous + 1:
                ranges.append((range_start, previous + 1))
                range_start = index
            previous = index
        ranges.append((range_start, previous + 1))
    return "".join(
        f'<rect x="{x0 + (left - start) * scale:.2f}" y="{y:.2f}" '
        f'width="{(right - left) * scale:.2f}" height="{height:.2f}" '
        'class="mismatch"/>'
        for left, right in ranges)


def render_alignment_svg(reference: Sequence[int], candidate: Sequence[int], *,
                         step_ns: int, measured_tick_ns: int,
                         best_delay_ns: int,
                         window_start_ns: int | None,
                         window_duration_ns: int,
                         title: str) -> str:
    if (window_duration_ns <= 0 or window_duration_ns % step_ns or
            best_delay_ns % step_ns):
        raise ValueError("SVG duration and delays must be multiples of step-ns")
    duration_samples = window_duration_ns // step_ns
    if window_start_ns is None:
        transitions = [index for index in range(1, len(reference))
                       if reference[index] != reference[index - 1]]
        anchor = transitions[0] if transitions else 0
        start = max(0, anchor - duration_samples // 8)
    else:
        if window_start_ns < 0 or window_start_ns % step_ns:
            raise ValueError("SVG window start must be a non-negative step multiple")
        start = window_start_ns // step_ns
    end = min(len(reference), start + duration_samples)
    if end <= start:
        raise ValueError("SVG window is outside the reference capture")
    best_lag = best_delay_ns // step_ns
    x0 = 150.0
    width = 1200.0
    rows = (
        ("reference", reference, 0, 120.0, "reference"),
        ("candidate comparison (raw)", candidate,
         0, 250.0, "comparison"),
        (f"candidate moved {-best_delay_ns:+d} ns", candidate,
         best_lag, 380.0, "best"),
    )
    paths = []
    for label, samples, lag, center, css_class in rows:
        if css_class != "reference":
            paths.append(_svg_mismatch_rects(
                reference, samples, start=start, end=end, lag=lag,
                x0=x0, width=width, y=center - 42, height=84))
        path = _svg_step_path(
            samples, start=start, end=end, lag=lag, x0=x0, width=width,
            y_high=center - 25, y_low=center + 25)
        paths.append(
            f'<text x="20" y="{center + 5:.2f}" class="row-label">'
            f'{html.escape(label)}</text><path d="{path}" class="wave {css_class}"/>')
    ticks = []
    for index in range(9):
        x = x0 + width * index / 8
        ns = (start + (end - start) * index / 8) * step_ns
        ticks.append(
            f'<line x1="{x:.2f}" y1="75" x2="{x:.2f}" y2="430" class="grid"/>'
            f'<text x="{x:.2f}" y="455" class="tick">{ns:.0f} ns</text>')
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="500" '
        'viewBox="0 0 1400 500">\n'
        '<style>text{font-family:Consolas,monospace;fill:#172033}'
        '.title{font-size:20px;font-weight:700}.note{font-size:13px;fill:#53627a}'
        '.row-label{font-size:14px}.tick{font-size:12px;text-anchor:middle}'
        '.grid{stroke:#d9e0ea;stroke-width:1}.wave{fill:none;stroke-width:2.5}'
        '.reference{stroke:#1f5fbf}.comparison{stroke:#c2410c}'
        '.best{stroke:#15803d}.mismatch{fill:#ef4444;fill-opacity:.16}</style>\n'
        '<rect width="1400" height="500" fill="#ffffff"/>\n'
        f'<text x="20" y="30" class="title">{html.escape(title)}</text>\n'
        f'<text x="20" y="55" class="note">measured tick {measured_tick_ns} ns; '
        f'display grid {step_ns} ns via zero-order hold; window '
        f'{window_duration_ns / 1000:g} us; red = mismatch</text>\n'
        + "".join(ticks) + "".join(paths) + '\n</svg>\n')


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    download = sub.add_parser("download", help="download one capture JSON from node SD")
    download.add_argument("--board-id", required=True)
    download.add_argument("--sd-path", required=True)
    download.add_argument("--out", required=True, type=Path)
    download.add_argument("--expected-build")
    download.add_argument("--chunk-size", type=int, default=128,
                          choices=range(1, 129), metavar="1..128")
    download.add_argument("--baud", type=int, default=115200)
    download.add_argument("--timeout", type=float, default=5.0)
    download.add_argument("--settle", type=float, default=0.2)

    analyze = sub.add_parser("analyze", help="scan waveform alignment")
    analyze.add_argument("--candidate", required=True, type=Path)
    analyze.add_argument("--reference", type=Path,
                         help="defaults to the candidate capture")
    analyze.add_argument("--candidate-channel", choices=CHANNEL_INDEX,
                         default="incoming_link")
    analyze.add_argument("--reference-channel", choices=CHANNEL_INDEX,
                         default="forward_output")
    analyze.add_argument("--shift-min-ns", type=int, default=-200)
    analyze.add_argument("--shift-max-ns", type=int, default=200)
    analyze.add_argument("--step-ns", type=int, default=1)
    analyze.add_argument("--window", choices=("active", "full"),
                         default="active")
    analyze.add_argument("--active-margin-ns", type=int, default=40)
    analyze.add_argument("--svg", type=Path,
                         help="write a waveform comparison SVG")
    analyze.add_argument("--svg-window-start-ns", type=int)
    analyze.add_argument("--svg-window-duration-ns", type=int,
                         default=DEFAULT_SVG_WINDOW_NS)
    analyze.add_argument("--out", required=True, type=Path)

    correlate = sub.add_parser(
        "correlate", help="replay the firmware marker correlation gate")
    correlate.add_argument("--capture", required=True, type=Path)
    correlate.add_argument("--channel", choices=CHANNEL_INDEX,
                           default="incoming_link")
    correlate.add_argument("--codebook-id", required=True, type=int,
                           choices=range(4), metavar="0..3")
    correlate.add_argument("--master-node", type=int, default=0)
    correlate.add_argument("--epoch", type=int)
    correlate.add_argument("--role", choices=("auto", "origin", "follower"),
                           default="auto")
    correlate.add_argument("--max-best-distance", type=int, default=512)
    correlate.add_argument("--min-margin", type=int, default=0)
    correlate.add_argument(
        "--global-shift-limit-ns", type=int,
        default=DEFAULT_GLOBAL_SHIFT_LIMIT_NS,
        help="signed offline overlap search limit (default: 2048 ns)")
    correlate.add_argument(
        "--svg", type=Path,
        help="write expected-marker versus reconstructed-capture SVG")
    correlate.add_argument("--svg-window-start-ns", type=int)
    correlate.add_argument("--svg-window-duration-ns", type=int,
                           default=DEFAULT_SVG_WINDOW_NS)
    correlate.add_argument("--out", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "download":
            result = download_capture(args)
        elif args.command == "analyze":
            result = analyze_capture(args)
        else:
            result = correlate_capture(args)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    if args.command in ("analyze", "correlate"):
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        result = (analysis_console_summary(result, args.out)
                  if args.command == "analyze"
                  else correlation_console_summary(result, args.out))
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
