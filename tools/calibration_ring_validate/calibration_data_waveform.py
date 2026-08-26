#!/usr/bin/env python3
"""Render a TRN-02 DATA capture against its expected codeword as SVG."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Sequence

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from calibration_ring_validate.calibration_clk_codebook_eval import (  # noqa: E402
    marker_raw_waveform,
)
from calibration_ring_validate.calibration_marker_waveform import (  # noqa: E402
    CORRELATION_REJECT_NAMES,
    DEFAULT_SVG_WINDOW_NS,
    correlation_reject_from_marker_flags,
    expand_zero_order_hold,
    firmware_correlate,
    render_alignment_svg,
    validate_marker_window,
)


DATA_CAPTURE_SCHEMAS = {
    "HAOFV_DATA_TRAIN_CAPTURE_V1",
    "HAOFV_DATA_TRAIN_CAPTURE_V2",
    "HAOFV_DATA_TRAIN_CAPTURE_V3",
}
DIRECTION_CHOICES = ("forward", "reverse")


def unpack_data_capture(capture: object) -> tuple[dict[str, object], list[int]]:
    if (not isinstance(capture, dict) or
            capture.get("schema") not in DATA_CAPTURE_SCHEMAS):
        raise ValueError(
            f"capture schema must be one of {sorted(DATA_CAPTURE_SCHEMAS)}")
    words = capture.get("raw_words")
    word_count = int(capture.get("raw_word_count", -1))
    sample_count = int(capture.get("raw_sample_count", -1))
    sample_period_ns = int(capture.get("sample_period_ns", 0))
    if (not isinstance(words, list) or word_count != len(words) or
            sample_count <= 0 or sample_count > word_count * 32 or
            sample_period_ns <= 0 or
            any(not isinstance(word, int) or word < 0 or word > 0xFFFFFFFF
                for word in words)):
        raise ValueError("invalid DATA capture word/sample metadata")
    samples = [
        (int(words[index >> 5]) >> (index & 31)) & 1
        for index in range(sample_count)
    ]
    return capture, samples


def render_data_waveform(
        capture: dict[str, object], samples: Sequence[int], *,
        codebook_id: int, svg_path: Path, window_start_ns: int | None,
        window_duration_ns: int, marker_direction: str,
        data_direction: str) -> dict[str, object]:
    if (marker_direction not in DIRECTION_CHOICES or
            data_direction not in DIRECTION_CHOICES or
            marker_direction == data_direction):
        raise ValueError(
            "TRN-02 runtime directions must be configured and opposite")
    source_node = int(capture["source_node"])
    destination_node = int(capture["destination_node"])
    epoch = int(capture["epoch"])
    tick_ns = int(capture["sample_period_ns"])
    expected, vector = marker_raw_waveform(
        codebook_id=codebook_id, epoch=epoch,
        source_node=source_node, polarity=0)
    max_best_distance = int(capture.get("max_best_distance", 512))
    min_margin = int(capture.get("min_margin", 0))
    correlation = firmware_correlate(
        expected, samples, max_best_distance=max_best_distance,
        min_margin=min_margin)
    if "best_lag_sample" not in correlation:
        raise ValueError("DATA capture is too short for codeword correlation")
    if correlation.get("detected_polarity") == "normal":
        validation = validate_marker_window(
            samples, int(correlation["best_lag_sample"]),
            half_chip_samples=vector.half_chip_samples,
            expected_header=vector.header)
        reason = correlation_reject_from_marker_flags(
            int(validation["flags"]))
        if reason == 0 and int(
                correlation["best_distance"]) > max_best_distance:
            reason = 11
        if reason == 0 and int(correlation["margin"]) < min_margin:
            reason = 12
        correlation["marker_validation"] = validation
        correlation["reject_reason"] = reason
        correlation["reject_name"] = CORRELATION_REJECT_NAMES[reason]
        correlation["accepted"] = reason == 0
    best_lag_sample = int(correlation["best_lag_sample"])
    best_delay_ns = best_lag_sample * tick_ns
    base_delay_ns = int(capture.get(
        "link_base_delay_ns", capture.get("base_delay_ns", 0)))
    configured_offset_samples = int(
        capture.get("configured_data_offset_sample_count", 0))
    residual_offset_ns = int(
        capture.get(
            "resolved_offset_ns",
            int(capture.get("resolved_offset_sample_count", 0)) * tick_ns))
    alignment_move_ns = -best_delay_ns
    physical_capture_center_ns = (
        base_delay_ns + configured_offset_samples * tick_ns +
        residual_offset_ns)
    calibrated_alignment_delay_ns = base_delay_ns + residual_offset_ns
    reference = expand_zero_order_hold(expected, tick_ns, 1)
    candidate = expand_zero_order_hold(samples, tick_ns, 1)
    svg = render_alignment_svg(
        reference, candidate, step_ns=1, measured_tick_ns=tick_ns,
        best_delay_ns=best_delay_ns,
        window_start_ns=window_start_ns,
        window_duration_ns=window_duration_ns,
        title=(f"DATA {data_direction} node{destination_node}->node{source_node} "
               f"(MARK {marker_direction} "
               f"node{source_node}->node{destination_node}): "
               f"expected vs physical RX; marker offset "
               f"{int(capture.get('marker_offset_sample_count', 0)):+d}, "
               f"DATA offset "
               f"{int(capture.get('resolved_offset_sample_count', 0)):+d}; "
               f"gate {correlation['reject_name']}; "
               f"calibrated delay {calibrated_alignment_delay_ns:+d} ns "
               f"(base {base_delay_ns:+d} ns + residual "
               f"{residual_offset_ns:+d} ns); physical capture center "
               f"{physical_capture_center_ns:+d} ns = base "
               f"{base_delay_ns:+d} ns + configured "
               f"{configured_offset_samples * tick_ns:+d} ns + residual "
               f"{residual_offset_ns:+d} ns"))
    svg_path.parent.mkdir(parents=True, exist_ok=True)
    svg_path.write_text(svg, encoding="utf-8")
    compact_correlation = {
        key: value for key, value in correlation.items() if key != "scan"
    }
    return {
        "schema": "HAOFV_DATA_TRAIN_WAVEFORM_ANALYSIS_V2",
        "capture": str(capture.get("capture_path", "")),
        "svg": str(svg_path),
        "source_node": source_node,
        "destination_node": destination_node,
        "marker_source_node": source_node,
        "marker_destination_node": destination_node,
        "data_source_node": destination_node,
        "data_destination_node": source_node,
        "marker_direction": marker_direction,
        "data_direction": data_direction,
        "measurement_direction": "configured_bidirectional_link",
        "epoch": epoch,
        "calibration_generation": int(capture["calibration_generation"]),
        "codebook_id": codebook_id,
        "sample_period_ns": tick_ns,
        "marker_offset_sample_count": int(
            capture.get("marker_offset_sample_count", 0)),
        "configured_data_offset_sample_count": configured_offset_samples,
        "resolved_offset_sample_count": int(
            capture.get("resolved_offset_sample_count", 0)),
        "expected_sample_count": len(expected),
        "captured_sample_count": len(samples),
        "link_base_delay_ns": base_delay_ns,
        "configured_data_offset_ns": configured_offset_samples * tick_ns,
        "residual_data_offset_ns": residual_offset_ns,
        "calibrated_alignment_delay_ns": calibrated_alignment_delay_ns,
        "physical_capture_center_ns": physical_capture_center_ns,
        "capture_buffer_lag_ns": best_delay_ns,
        "alignment_move_ns": alignment_move_ns,
        "alignment_consistency_error_ns": (
            best_delay_ns - calibrated_alignment_delay_ns),
        "best_candidate_delay_ns": best_delay_ns,
        "move_candidate_by_ns": alignment_move_ns,
        "correlation": compact_correlation,
        "firmware_evidence": {
            key: int(capture[key]) for key in (
                "state", "reject_reason", "flags",
                "correlation_reject_reason",
                "observed_header_fields_valid", "observed_header",
                "observed_header_inverse", "observed_header_crc8",
                "data_crc32", "observed_crc32") if key in capture
        },
        "firmware_correlation_consistent": (
            "correlation_reject_reason" not in capture or
            int(capture["correlation_reject_reason"]) ==
            int(correlation["reject_reason"])),
        "window_start_ns": window_start_ns,
        "window_duration_ns": window_duration_ns,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", required=True, type=Path)
    parser.add_argument("--codebook-id", type=int, choices=range(4))
    parser.add_argument("--svg", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--marker-direction", choices=DIRECTION_CHOICES,
                        required=True)
    parser.add_argument("--data-direction", choices=DIRECTION_CHOICES,
                        required=True)
    parser.add_argument("--window-start-ns", type=int)
    parser.add_argument("--window-duration-ns", type=int,
                        default=DEFAULT_SVG_WINDOW_NS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        capture, samples = unpack_data_capture(json.loads(
            args.capture.read_text(encoding="utf-8")))
        capture["capture_path"] = str(args.capture)
        codebook_id = args.codebook_id
        if codebook_id is None:
            if "data_codebook_id" not in capture:
                raise ValueError(
                    "old capture has no data_codebook_id; pass --codebook-id")
            codebook_id = int(capture["data_codebook_id"])
        result = render_data_waveform(
            capture, samples, codebook_id=codebook_id,
            svg_path=args.svg, window_start_ns=args.window_start_ns,
            window_duration_ns=args.window_duration_ns,
            marker_direction=args.marker_direction,
            data_direction=args.data_direction)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}")
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
