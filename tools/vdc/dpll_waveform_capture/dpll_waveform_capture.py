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
import math
import statistics
import struct
import zlib
from pathlib import Path
from typing import Any, Iterable
from xml.sax.saxutils import escape


MAGIC = 0x57524D53  # SMRW
SCHEMA = 1
SCHEMA_V2 = 2
SCHEMA_V3 = 3
SCHEMA_V4 = 4
HEADER = struct.Struct("<IHHHHIIIIIIIII")
RECORD = struct.Struct("<IIIIQIIIII")
HEADER_V2 = struct.Struct("<IHHHH" + "I" * 12)
RECORD_V2 = struct.Struct("<IIIIQII")
HEADER_V3 = struct.Struct("<IHHHH" + "I" * 12 + "QII")
RECORD_V3 = struct.Struct("<IIIII")
HEADER_V4 = HEADER_V3
RECORD_V4 = struct.Struct("<IIIIIII")
SAMPLES_PER_WORD = 8
CHANNEL_COUNT = 4
DEFAULT_PULSE_PERIOD_NS = 1_000_000
TIMESTAMP_FLAG_DPLL_ELIGIBLE = 0x00000002

WAVEFORM_QUALITY_TIMESTAMP_ELIGIBLE = 1 << 0
WAVEFORM_QUALITY_SEQUENCE_CONTINUOUS = 1 << 1
WAVEFORM_QUALITY_NO_SOURCE_DROP = 1 << 2
WAVEFORM_QUALITY_MATCHED_WINDOW_VALID = 1 << 3
WAVEFORM_QUALITY_RAW_DIAGNOSTIC_ONLY = 1 << 4
WAVEFORM_QUALITY_CORRECTED_ELIGIBLE = 1 << 5
WAVEFORM_QUALITY_GAP_BEFORE = 1 << 6
WAVEFORM_QUALITY_AMBIGUOUS = 1 << 7
WAVEFORM_QUALITY_INCOMPLETE_WINDOW = 1 << 8

_QUALITY_NAMES = (
    (WAVEFORM_QUALITY_TIMESTAMP_ELIGIBLE, "timestamp_eligible"),
    (WAVEFORM_QUALITY_SEQUENCE_CONTINUOUS, "sequence_continuous"),
    (WAVEFORM_QUALITY_NO_SOURCE_DROP, "no_source_drop"),
    (WAVEFORM_QUALITY_MATCHED_WINDOW_VALID, "matched_window_valid"),
    (WAVEFORM_QUALITY_RAW_DIAGNOSTIC_ONLY, "raw_diagnostic_only"),
    (WAVEFORM_QUALITY_CORRECTED_ELIGIBLE, "corrected_eligible"),
    (WAVEFORM_QUALITY_GAP_BEFORE, "gap_before"),
    (WAVEFORM_QUALITY_AMBIGUOUS, "ambiguous"),
    (WAVEFORM_QUALITY_INCOMPLETE_WINDOW, "incomplete_window"),
)


def _quality_names(flags: int) -> list[str]:
    return [name for bit, name in _QUALITY_NAMES if flags & bit]


def _legacy_quality_flags(record: dict[str, Any]) -> int:
    """Infer only the fields available in pre-v4 segments.

    The result remains explicitly tagged as legacy-derived by the caller; it
    is never treated as stronger provenance than the stored timestamp fields.
    """
    flags = 0
    eligible = bool(int(record.get("timestamp_flags", 0)) &
                   TIMESTAMP_FLAG_DPLL_ELIGIBLE)
    if eligible:
        flags |= WAVEFORM_QUALITY_TIMESTAMP_ELIGIBLE
    if int(record.get("sample_period_ns", 0)) > 0 and eligible:
        flags |= WAVEFORM_QUALITY_MATCHED_WINDOW_VALID
    if int(record.get("dropped_before", 0)) == 0:
        flags |= WAVEFORM_QUALITY_NO_SOURCE_DROP
    if eligible and not int(record.get("dropped_before", 0)):
        flags |= (WAVEFORM_QUALITY_SEQUENCE_CONTINUOUS |
                  WAVEFORM_QUALITY_CORRECTED_ELIGIBLE)
    if not flags & WAVEFORM_QUALITY_CORRECTED_ELIGIBLE:
        flags |= WAVEFORM_QUALITY_RAW_DIAGNOSTIC_ONLY
    return flags


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
    magic, schema, header_size, record_size, reserved = struct.unpack_from(
        "<IHHHH", data)
    if magic != MAGIC:
        raise ValueError(f"unexpected waveform magic 0x{magic:08X}")
    if schema == SCHEMA:
        values = HEADER.unpack_from(data)
        (_, _, _, _, _, session_id, segment_index, first_record_index,
         record_count, dropped_count, start_ms, end_ms, observed_mask,
         payload_crc32) = values
        expected_header_size = HEADER.size
        record_layout = RECORD
        common_metadata: dict[str, int] = {}
    elif schema == SCHEMA_V2:
        if len(data) < HEADER_V2.size:
            raise ValueError(f"waveform v2 segment is shorter than header: {path}")
        values = HEADER_V2.unpack_from(data)
        (_, _, _, _, _, session_id, segment_index, first_record_index,
         record_count, dropped_count, start_ms, end_ms, observed_mask,
         sample_period_ns, timestamp_source, timestamp_resolution_ns,
         payload_crc32) = values
        expected_header_size = HEADER_V2.size
        record_layout = RECORD_V2
        common_metadata = {
            "sample_period_ns": sample_period_ns,
            "timestamp_source": timestamp_source,
            "timestamp_resolution_ns": timestamp_resolution_ns,
        }
    elif schema in (SCHEMA_V3, SCHEMA_V4):
        if len(data) < HEADER_V3.size:
            raise ValueError(
                f"waveform v{schema} segment is shorter than header: {path}")
        values = HEADER_V3.unpack_from(data)
        (_, _, _, _, _, session_id, segment_index, first_record_index,
         record_count, dropped_count, start_ms, end_ms, observed_mask,
         sample_period_ns, timestamp_source, timestamp_resolution_ns,
         first_sample_seq, first_matched_window_start_ns, timestamp_flags,
         payload_crc32) = values
        expected_header_size = HEADER_V3.size
        record_layout = RECORD_V4 if schema == SCHEMA_V4 else RECORD_V3
        common_metadata = {
            "sample_period_ns": sample_period_ns,
            "timestamp_source": timestamp_source,
            "timestamp_resolution_ns": timestamp_resolution_ns,
            "first_sample_seq": first_sample_seq,
            "first_matched_window_start_ns": first_matched_window_start_ns,
            "timestamp_flags": timestamp_flags,
        }
    else:
        raise ValueError(f"unsupported waveform schema {schema}")
    if header_size != expected_header_size or record_size != record_layout.size:
        raise ValueError(
            f"waveform layout {header_size}/{record_size} != "
            f"{expected_header_size}/{record_layout.size}")
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
    previous_window_start_ns = common_metadata.get(
        "first_matched_window_start_ns", 0)
    first_source_dropped = None
    for index in range(record_count):
        record = record_layout.unpack_from(payload, index * record_size)
        if schema == SCHEMA:
            decoded = dict(zip((
                "raw_word", "sample_seq", "previous_sample_mask",
                "base_time_l32_ns", "matched_window_start_ns",
                "sample_period_ns", "timestamp_source",
                "timestamp_resolution_ns", "timestamp_flags",
                "dropped_before",
            ), record, strict=True))
        elif schema == SCHEMA_V2:
            decoded = dict(zip((
                "raw_word", "sample_seq", "previous_sample_mask",
                "base_time_l32_ns", "matched_window_start_ns",
                "timestamp_flags", "dropped_before",
            ), record, strict=True))
            decoded.update(common_metadata)
        elif schema == SCHEMA_V3:
            (raw_word, previous_sample_mask, base_time_l32_ns,
             matched_window_start_l32_ns, dropped_before) = record
            if first_source_dropped is None:
                first_source_dropped = dropped_before
            sample_seq = (common_metadata["first_sample_seq"] + index +
                          dropped_before - first_source_dropped)
            candidate = (
                (previous_window_start_ns & ~0xFFFFFFFF) |
                matched_window_start_l32_ns)
            if candidate + (1 << 31) < previous_window_start_ns:
                candidate += 1 << 32
            elif candidate > previous_window_start_ns + (1 << 31):
                candidate -= 1 << 32
            previous_window_start_ns = candidate
            decoded = {
                "raw_word": raw_word,
                "sample_seq": sample_seq,
                "previous_sample_mask": previous_sample_mask,
                "base_time_l32_ns": base_time_l32_ns,
                "matched_window_start_ns": candidate,
                "timestamp_flags": common_metadata["timestamp_flags"],
                "dropped_before": dropped_before,
                "sample_period_ns": common_metadata["sample_period_ns"],
                "timestamp_source": common_metadata["timestamp_source"],
                "timestamp_resolution_ns": common_metadata[
                    "timestamp_resolution_ns"],
            }
        else:
            (raw_word, previous_sample_mask, base_time_l32_ns,
             matched_window_start_l32_ns, dropped_before, sample_seq,
             quality_flags) = record
            if first_source_dropped is None:
                first_source_dropped = dropped_before
            candidate = (
                (previous_window_start_ns & ~0xFFFFFFFF) |
                matched_window_start_l32_ns)
            if candidate + (1 << 31) < previous_window_start_ns:
                candidate += 1 << 32
            elif candidate > previous_window_start_ns + (1 << 31):
                candidate -= 1 << 32
            previous_window_start_ns = candidate
            decoded = {
                "raw_word": raw_word,
                "sample_seq": sample_seq,
                "previous_sample_mask": previous_sample_mask,
                "base_time_l32_ns": base_time_l32_ns,
                "matched_window_start_ns": candidate,
                "timestamp_flags": common_metadata["timestamp_flags"],
                "dropped_before": dropped_before,
                "sample_period_ns": common_metadata["sample_period_ns"],
                "timestamp_source": common_metadata["timestamp_source"],
                "timestamp_resolution_ns": common_metadata[
                    "timestamp_resolution_ns"],
                "quality_flags": quality_flags,
            }
        decoded.setdefault("quality_flags", _legacy_quality_flags(decoded))
        decoded["quality_names"] = _quality_names(
            int(decoded["quality_flags"]))
        records.append(decoded)
    return {
        "path": str(path),
        "schema": schema,
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


def _four_node_span_trend(trend: list[dict[str, Any]],
                          period_ns: int) -> list[dict[str, Any]]:
    phases_by_time: dict[float, list[int]] = {}
    for row in trend:
        phases_by_time.setdefault(row["elapsed_s"], []).append(
            row["phase_center_ns"])
    return [
        {
            "elapsed_s": elapsed_s,
            "span_ns": _circular_span_ns(phases, period_ns),
            "node_count": len(phases),
        }
        for elapsed_s, phases in sorted(phases_by_time.items())
        if len(phases) == CHANNEL_COUNT
    ]


def _span_convergence_summary(span_trend: list[dict[str, Any]],
                              period_ns: int) -> dict[str, Any]:
    if not span_trend:
        return {
            "available": False,
            "direction": "UNAVAILABLE",
            "early_median_ns": None,
            "late_median_ns": None,
            "change_ns": None,
        }
    window_count = max(1, len(span_trend) // 5)
    early = [row["span_ns"] for row in span_trend[:window_count]]
    late = [row["span_ns"] for row in span_trend[-window_count:]]
    early_median = int(statistics.median(early))
    late_median = int(statistics.median(late))
    change_ns = late_median - early_median
    threshold_ns = max(1, period_ns // 20)
    direction = (
        "CONVERGING" if change_ns < -threshold_ns else
        "DIVERGING" if change_ns > threshold_ns else
        "STABLE_OR_INCONCLUSIVE")
    return {
        "available": True,
        "direction": direction,
        "early_median_ns": early_median,
        "late_median_ns": late_median,
        "change_ns": change_ns,
        "decision_threshold_ns": threshold_ns,
        "window_count": window_count,
    }


def _annotate_record_gaps(records: list[dict[str, Any]]) -> tuple[int, list[dict[str, Any]]]:
    """Annotate discontinuities before edge reconstruction.

    A dropped raw word makes the previous-sample mask unsafe only for the
    first sample of the next retained word.  Keep the record for auditability,
    but mark that boundary so no synthetic transition crosses the gap.
    """
    gap_count = 0
    previous_seq: int | None = None
    previous_dropped: int | None = None
    previous_capture_dropped: int | None = None
    for index, record in enumerate(records):
        reasons: list[str] = []
        sample_seq = int(record.get("sample_seq", 0))
        dropped_before = int(record.get("dropped_before", 0))
        capture_dropped = int(record.get("capture_dropped_count", 0))
        if previous_seq is None:
            if dropped_before > 0:
                reasons.append("initial_source_drop")
            if capture_dropped > 0:
                reasons.append("initial_capture_drop")
        else:
            sequence_delta = sample_seq - previous_seq
            if sequence_delta <= 0:
                reasons.append("sample_sequence_non_monotonic")
            elif sequence_delta > 1:
                reasons.append("sample_sequence_gap")
            if previous_dropped is not None and dropped_before > previous_dropped:
                reasons.append("source_drop")
            if (previous_capture_dropped is not None and
                    capture_dropped > previous_capture_dropped):
                reasons.append("capture_drop")
            if reasons:
                gap_count += 1
        record["gap_before"] = bool(reasons)
        record["gap_reasons"] = reasons
        quality_flags = int(record.get("quality_flags", 0))
        if reasons:
            quality_flags |= WAVEFORM_QUALITY_GAP_BEFORE
            quality_flags &= ~WAVEFORM_QUALITY_CORRECTED_ELIGIBLE
            quality_flags |= WAVEFORM_QUALITY_RAW_DIAGNOSTIC_ONLY
        record["quality_flags"] = quality_flags
        record["quality_names"] = _quality_names(quality_flags)
        record["record_index"] = index
        previous_seq = sample_seq
        previous_dropped = dropped_before
        previous_capture_dropped = capture_dropped
    return gap_count, records


def _phase_tracking(edges: list[dict[str, Any]], period_ns: int,
                    channel_delays_ns: dict[int, int] | None = None,
                    channel_phase_ns: dict[int, int] | None = None
                    ) -> tuple[list[dict[str, Any]], list[dict[str, Any]],
                               list[dict[str, Any]], dict[str, Any]]:
    rising = [edge for edge in edges if edge["edge"] == "rising"]
    if not rising:
        return [], [], [], {"assessment": "NO_RISING_EDGES", "nodes": {}}
    delays = {channel: int((channel_delays_ns or {}).get(channel, 0))
              for channel in range(CHANNEL_COUNT)}
    for edge in rising:
        edge["corrected_timestamp_ns"] = edge["timestamp_ns"] - delays[
            edge["channel"]]

    references = sorted(edge["corrected_timestamp_ns"] for edge in rising
                        if edge["channel"] == 0)
    if not references:
        return [], [], [], {"assessment": "NO_REFERENCE_NO1_EDGES", "nodes": {}}
    epoch_ns = references[0]
    # Estimate each channel's phase, then assign edges to an integer pulse
    # cycle.  A nearest edge in absolute time is unsafe when a channel is
    # sparse: it can silently pair with the adjacent pulse.  When a TDMA/SMA
    # schedule is supplied, it is the authoritative phase contract; inferred
    # phase is retained only as a diagnostic fallback.
    phase_centers: dict[int, int] = {}
    schedule_phase = {channel: int((channel_phase_ns or {}).get(channel, 0))
                      for channel in range(CHANNEL_COUNT)}
    schedule_phase[0] = 0
    schedule_supplied = channel_phase_ns is not None
    schedule_complete = schedule_supplied and all(
        channel in (channel_phase_ns or {}) for channel in range(1, CHANNEL_COUNT))
    window_metadata_present = all("window_start_ns" in edge for edge in rising)
    window_groups: dict[int, dict[int, list[dict[str, Any]]]] = {}
    if window_metadata_present:
        for edge in rising:
            window_groups.setdefault(int(edge["window_start_ns"]), {}).\
                setdefault(edge["channel"], []).append(edge)
    for channel in range(CHANNEL_COUNT):
        if schedule_supplied:
            phase_centers[channel] = _circular_delta_ns(
                schedule_phase[channel], 0, period_ns)
        else:
            values = [(_edge["corrected_timestamp_ns"] - epoch_ns) % period_ns
                      for _edge in rising if _edge["channel"] == channel]
            phase_center = _circular_center_ns(values, period_ns)
            phase_centers[channel] = (_circular_delta_ns(phase_center, 0,
                                                         period_ns)
                                      if phase_center is not None else 0)
    by_cycle: dict[int, dict[int, list[dict[str, Any]]]] = {}
    if window_metadata_present and not schedule_complete:
        # A matched TDMA window is an evidence boundary, not proof that the
        # other channels belong to that round.  Without a complete schedule
        # contract, only edges in the same explicit window may be paired.
        for cycle, (window_start, channels) in enumerate(
                sorted(window_groups.items())):
            for channel, candidates in channels.items():
                for edge in candidates:
                    edge["cycle_index"] = cycle
            by_cycle[cycle] = channels
    else:
        for edge in rising:
            relative_ns = edge["corrected_timestamp_ns"] - epoch_ns
            # Anchor cycle identity to NO1's epoch. Signed phase centers allow a
            # follower edge just before NO1 to remain in the same physical cycle
            # instead of wrapping to an adjacent pulse.
            cycle = math.floor(
                (relative_ns - phase_centers[edge["channel"]]) /
                period_ns + 0.5)
            edge["cycle_index"] = cycle
            by_cycle.setdefault(cycle, {}).setdefault(edge["channel"], []).append(edge)

    tracking = []
    pair_tolerance_ns = max(1, period_ns // 4)
    matched_by_cycle: dict[int, dict[int, dict[str, Any]]] = {}
    for cycle, channels in by_cycle.items():
        selected: dict[int, dict[str, Any]] = {}
        for channel, candidates in channels.items():
            if window_metadata_present and not schedule_complete:
                selected[channel] = min(
                    candidates,
                    key=lambda item: item["corrected_timestamp_ns"])
            else:
                expected = (epoch_ns + cycle * period_ns +
                            phase_centers[channel])
                selected[channel] = min(
                    candidates,
                    key=lambda item: abs(item["corrected_timestamp_ns"] - expected))
        if len(selected) == CHANNEL_COUNT:
            same_window = (
                not window_metadata_present or
                len({int(item["window_start_ns"])
                     for item in selected.values()}) == 1)
            within_period = all(
                abs(item["corrected_timestamp_ns"] -
                    (epoch_ns + cycle * period_ns + phase_centers[channel]))
                <= pair_tolerance_ns
                for channel, item in selected.items())
            if ((window_metadata_present and not schedule_complete and
                 same_window) or
                    (not window_metadata_present or schedule_complete) and
                    within_period):
                matched_by_cycle[cycle] = selected

    for edge in rising:
        corrected_ns = edge["corrected_timestamp_ns"]
        phase_ns = (corrected_ns - epoch_ns) % period_ns
        pair = matched_by_cycle.get(edge["cycle_index"], {})
        reference = pair.get(0)
        tracking.append({
            "elapsed_s": (corrected_ns - epoch_ns) / 1e9,
            "channel": edge["channel"],
            "node": f"NO{edge['channel'] + 1}",
            "timestamp_ns": edge["timestamp_ns"],
            "corrected_timestamp_ns": corrected_ns,
            "cycle_index": edge["cycle_index"],
            "window_start_ns": edge.get("window_start_ns"),
            "quality_flags": edge.get("quality_flags", 0),
            "quality_names": list(edge.get("quality_names", [])),
            "corrected_eligible": edge.get("corrected_eligible", False),
            "matched_common_cycle": edge["cycle_index"] in matched_by_cycle,
            "phase_ns": phase_ns,
            "relative_to_no1_ns": (
                _circular_delta_ns(phase_ns,
                                   (reference["corrected_timestamp_ns"] -
                                    epoch_ns) % period_ns, period_ns)
                if reference is not None else None),
            "phase_innovation_ns": _circular_delta_ns(
                phase_ns, phase_centers[edge["channel"]], period_ns),
            "reference_age_ms": (
                abs(corrected_ns - reference["corrected_timestamp_ns"]) / 1e6
                if reference is not None else None),
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
                      row["matched_common_cycle"] and
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
        all_phases = [row["phase_ns"] for row in tracking
                      if row["channel"] == channel]
        phases = [row["phase_ns"] for row in tracking
                  if row["channel"] == channel and
                  row["matched_common_cycle"]]
        final = [row["phase_ns"] for row in tracking
                 if row["channel"] == channel and
                 row["matched_common_cycle"] and
                 row["elapsed_s"] >= final_start_s]
        oscillation = _half_cycle_oscillation(final, period_ns)
        if oscillation["detected"]:
            detected_nodes.append(f"NO{channel + 1}")
        matched_offsets = [
            int(row["phase_innovation_ns"])
            for row in tracking
            if row["channel"] == channel and
            row["matched_common_cycle"] and
            row["relative_to_no1_ns"] is not None]
        channel_bias = (float(statistics.median(matched_offsets))
                        if matched_offsets else None)
        centered = ([_circular_delta_ns(value, int(channel_bias), period_ns)
                     for value in matched_offsets]
                    if channel_bias is not None else [])
        channel_mad = (float(statistics.median(abs(value) for value in centered))
                       if centered else None)
        channel_rms = (math.sqrt(sum(value * value for value in centered) /
                                 len(centered))
                       if centered else None)
        nodes[f"NO{channel + 1}"] = {
            "rising_edge_count": len(all_phases),
            "common_cycle_edge_count": len(phases),
            "final_phase_center_ns": _circular_center_ns(final, period_ns),
            "final_circular_span_ns": _circular_span_ns(final, period_ns),
            "half_cycle_oscillation": oscillation,
            "common_cycle_sample_count": len(matched_offsets),
            "fixed_bias_relative_to_no1_ns": channel_bias,
            "bias_corrected_jitter_mad_ns": channel_mad,
            "bias_corrected_jitter_rms_ns": channel_rms,
            "bias_corrected_jitter_peak_to_peak_ns": (
                max(centered) - min(centered) if centered else None),
        }
    assessment = (
        "HALF_CYCLE_LIMIT_CYCLE" if detected_nodes else
        "NO_HALF_CYCLE_LIMIT_CYCLE_DETECTED")
    span_trend = _four_node_span_trend(trend, period_ns)
    span_convergence = _span_convergence_summary(span_trend, period_ns)
    return tracking, trend, span_trend, {
        "pulse_period_ns": period_ns,
        "channel_delays_ns": delays,
        "channel_phase_ns": phase_centers,
        "window_metadata_present": window_metadata_present,
        "window_group_count": len(window_groups),
        "complete_window_count": sum(
            len(channels) == CHANNEL_COUNT for channels in window_groups.values()),
        "window_channel_counts": {
            str(channel): sum(channel in channels
                              for channels in window_groups.values())
            for channel in range(CHANNEL_COUNT)
        },
        "pairing_mode": (
            "explicit_window_cooccurrence" if window_metadata_present and
            not schedule_complete else
            "schedule_phase_cycle" if schedule_complete else
            "inferred_period_cycle"),
        "alignment_mode": ("schedule_contract" if schedule_complete else
                            "incomplete_schedule_contract" if schedule_supplied
                            else "inferred_channel_phase"),
        "schedule_phase_supplied": schedule_supplied,
        "schedule_phase_complete": schedule_complete,
        "common_cycle_count": len(matched_by_cycle),
        "common_cycle_coverage": (
            len(matched_by_cycle) / len(by_cycle) if by_cycle else 0.0),
        "pair_tolerance_ns": pair_tolerance_ns,
        "duration_s": duration_s,
        "assessment": assessment,
        "oscillating_nodes": detected_nodes,
        "four_node_span": {
            "available": bool(span_trend),
            "start_ns": span_trend[0]["span_ns"] if span_trend else None,
            "final_ns": span_trend[-1]["span_ns"] if span_trend else None,
            "minimum_ns": min((row["span_ns"] for row in span_trend),
                              default=None),
            "maximum_ns": max((row["span_ns"] for row in span_trend),
                              default=None),
            "convergence": span_convergence,
        },
        "nodes": nodes,
        "loop_margin_estimation": {
            "available": False,
            "gain_margin_db": None,
            "phase_margin_deg": None,
            "reason": "requires injected frequency response or an identified loop model",
        },
    }


def decode_segments(paths: Iterable[Path], *,
                    pulse_period_ns: int = DEFAULT_PULSE_PERIOD_NS,
                    channel_delays_ns: dict[int, int] | None = None,
                    channel_phase_ns: dict[int, int] | None = None
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
        for record in segment["records"]:
            # Segment ``dropped_count`` is cumulative. Keep it attached to
            # each record so a capture-buffer loss invalidates the first
            # reconstructed edge after the boundary.
            record["segment_index"] = segment["segment_index"]
            record["capture_dropped_count"] = segment["dropped_count"]
            records.append(record)
        expected_first += segment["record_count"]

    gap_count, records = _annotate_record_gaps(records)
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
                    invalid_reasons = list(record["gap_reasons"]
                                           if record["gap_before"] and sample_index == 0
                                           else [])
                    if (int(record.get("sample_period_ns", 0)) <= 0 or
                            int(record.get("timestamp_resolution_ns", 0)) <= 0):
                        invalid_reasons.append("invalid_timing_metadata")
                    edges.append({
                        "record_index": record_index,
                        "sample_seq": record["sample_seq"],
                        "sample_index": sample_index,
                        "channel": channel,
                        "edge": "rising" if current & bit else "falling",
                        "timestamp_ns": timestamp_ns,
                        "window_start_ns": record["matched_window_start_ns"],
                        "timestamp_flags": record["timestamp_flags"],
                        "eligible": bool(record["timestamp_flags"] &
                                          TIMESTAMP_FLAG_DPLL_ELIGIBLE),
                        "quality_flags": int(record.get("quality_flags", 0)),
                        "quality_names": list(record.get("quality_names", [])),
                        "corrected_eligible": bool(
                            int(record.get("quality_flags", 0)) &
                            WAVEFORM_QUALITY_CORRECTED_ELIGIBLE),
                        "valid": not invalid_reasons,
                        "invalid_reason": ";".join(invalid_reasons),
                    })
            previous = current

    valid_edges = [edge for edge in edges if edge["valid"]]
    eligible_edges = [edge for edge in valid_edges if edge["eligible"]]
    window_channels: dict[int, set[int]] = {}
    for edge in eligible_edges:
        if edge["edge"] == "rising":
            window_channels.setdefault(int(edge["window_start_ns"]), set()).add(
                int(edge["channel"]))
    required_channels = {
        channel for channel in range(CHANNEL_COUNT)
        if observed_mask & (1 << channel)
    }
    incomplete_windows = {
        window for window, channels in window_channels.items()
        if channels != required_channels
    }
    for record in records:
        if (int(record.get("matched_window_start_ns", 0)) in
                incomplete_windows):
            record["quality_flags"] = (
                int(record.get("quality_flags", 0)) |
                WAVEFORM_QUALITY_INCOMPLETE_WINDOW |
                WAVEFORM_QUALITY_RAW_DIAGNOSTIC_ONLY)
            record["quality_flags"] &= ~WAVEFORM_QUALITY_CORRECTED_ELIGIBLE
            record["quality_names"] = _quality_names(record["quality_flags"])
    for edge in edges:
        if (edge["edge"] == "rising" and edge["eligible"] and
                int(edge["window_start_ns"]) in incomplete_windows):
            edge["quality_flags"] |= WAVEFORM_QUALITY_INCOMPLETE_WINDOW
            edge["quality_flags"] &= ~WAVEFORM_QUALITY_CORRECTED_ELIGIBLE
            edge["quality_flags"] |= WAVEFORM_QUALITY_RAW_DIAGNOSTIC_ONLY
            edge["quality_names"] = _quality_names(edge["quality_flags"])
            edge["corrected_eligible"] = False
    corrected_edges = [edge for edge in eligible_edges
                       if edge["corrected_eligible"]]
    tracking, trend, span_trend, convergence = _phase_tracking(
        eligible_edges, pulse_period_ns, channel_delays_ns, channel_phase_ns)
    (corrected_tracking, corrected_trend, corrected_span_trend,
     corrected_convergence) = _phase_tracking(
        corrected_edges, pulse_period_ns, channel_delays_ns, channel_phase_ns)
    # The phase table is derived from the same common-cycle matcher as the
    # convergence trace.  Window co-occurrence alone can mix adjacent pulses.
    phase = []
    matched_rows: dict[int, dict[int, dict[str, Any]]] = {}
    for row in corrected_tracking:
        if row["matched_common_cycle"]:
            matched_rows.setdefault(row["cycle_index"], {})[
                row["channel"]] = row
    for cycle, channels in sorted(matched_rows.items()):
        if len(channels) != CHANNEL_COUNT:
            continue
        corrected = [channels[channel]["corrected_timestamp_ns"]
                     for channel in range(CHANNEL_COUNT)]
        phase_row: dict[str, Any] = {
            "window_start_ns": min(
                channels[channel]["timestamp_ns"] for channel in channels),
            "cycle_index": cycle,
            "elapsed_s": min(channels[channel]["elapsed_s"]
                              for channel in channels),
            "span_ns": _circular_span_ns(corrected, pulse_period_ns),
        }
        reference_corrected = corrected[0]
        for channel in range(CHANNEL_COUNT):
            phase_row[f"edge{channel}_ns"] = channels[channel]["timestamp_ns"]
            phase_row[f"corrected_edge{channel}_ns"] = corrected[channel]
            phase_row[f"offset{channel}_ns"] = _circular_delta_ns(
                corrected[channel], reference_corrected, pulse_period_ns)
        phase.append(phase_row)
    valid_edge_count = len(valid_edges)
    invalid_edge_count = len(edges) - valid_edge_count
    edge_coverage = (valid_edge_count / len(edges)) if edges else 0.0
    valid_channels = sorted({edge["channel"] for edge in valid_edges})
    channel_coverage = len(valid_channels) / CHANNEL_COUNT
    rising_counts = {
        channel: sum(1 for edge in valid_edges
                     if edge["channel"] == channel and
                     edge["edge"] == "rising")
        for channel in range(CHANNEL_COUNT)
    }
    common_cycle_count = int(corrected_convergence.get("common_cycle_count", 0))
    confidence = (
        "no_valid_edges" if valid_edge_count == 0 else
        "no_eligible_edges" if not eligible_edges else
        "degraded_gap" if gap_count else
        "low_channel_coverage" if channel_coverage < 1.0 else
        "incomplete_window" if incomplete_windows else
        "insufficient_common_cycles" if common_cycle_count < 2 else
        "low_edge_coverage" if edge_coverage < 0.9 else
        "incomplete_schedule_contract" if convergence.get(
            "schedule_phase_supplied", False) and not convergence.get(
                "schedule_phase_complete", False) else
        "inferred_alignment" if not convergence.get(
            "schedule_phase_supplied", False) else
        "normal")
    return {
        "schema": "HAOFV_NO5_PIO0_WAVEFORM_V1",
        "source": "NO5_SD_PIO0_RAW_WAVEFORM",
        "session_id": session_id,
        "segment_count": len(segments),
        "record_count": len(records),
        "edge_count": len(edges),
        "valid_edge_count": valid_edge_count,
        "invalid_edge_count": invalid_edge_count,
        "edge_coverage": edge_coverage,
        "eligible_edge_count": len(eligible_edges),
        "corrected_eligible_edge_count": len(corrected_edges),
        "valid_channels": valid_channels,
        "channel_coverage": channel_coverage,
        "rising_edge_counts": rising_counts,
        "gap_count": gap_count,
        "incomplete_window_count": len(incomplete_windows),
        "quality_counts": {
            name: sum(1 for record in records
                      if int(record.get("quality_flags", 0)) & bit)
            for bit, name in _QUALITY_NAMES
        },
        "observation_confidence": confidence,
        "phase_round_count": len(phase),
        "common_cycle_count": common_cycle_count,
        "common_cycle_coverage": corrected_convergence.get(
            "common_cycle_coverage", 0.0),
        "channel_delays_ns": corrected_convergence.get("channel_delays_ns", {}),
        "channel_phase_ns": corrected_convergence.get("channel_phase_ns", {}),
        "alignment_mode": corrected_convergence.get("alignment_mode", "unknown"),
        "schedule_phase_complete": corrected_convergence.get(
            "schedule_phase_complete", False),
        "dropped_count": max(segment["dropped_count"] for segment in segments),
        "source_dropped_count": max(
            (record["dropped_before"] for record in records), default=0),
        "capture_dropped_count": max(
            (record.get("capture_dropped_count", 0) for record in records),
            default=0),
        "records": records,
        "edges": edges,
        "phase": phase,
        "phase_tracking": tracking,
        "phase_trend": trend,
        "phase_span_trend": span_trend,
        "corrected_phase_tracking": corrected_tracking,
        "corrected_phase_trend": corrected_trend,
        "corrected_phase_span_trend": corrected_span_trend,
        "raw_convergence": convergence,
        "convergence": convergence,
        "corrected_convergence": corrected_convergence,
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
    max_y = max((abs(row[f"offset{channel}_ns"]) for row in rows
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
                     trend: list[dict[str, Any]],
                     span_trend: list[dict[str, Any]], period_ns: int,
                     raw_rows: list[dict[str, Any]] | None = None) -> str:
    width, height = 1400, 900
    left, top, plot_w, plot_h = 90, 55, 1240, 540
    span_top, span_h = 655, 140
    all_display_rows = list(rows) + list(raw_rows or [])
    max_x = max((row["elapsed_s"] for row in all_display_rows),
                default=1.0) or 1.0
    colors = ("#2563eb", "#dc2626", "#059669", "#7c3aed")
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<style>text{font-family:Arial,sans-serif;letter-spacing:0;fill:#202124}'
        '.axis{stroke:#5f6368;stroke-width:1}.grid{stroke:#dadce0;stroke-width:1}'
        '.trend{fill:none;stroke-width:2}.span{fill:none;stroke:#202124;stroke-width:2}'
        '.outlier{fill:#ffffff;stroke:#d97706;stroke-width:2}'
        '.raw-only{fill:#9ca3af;fill-opacity:.28}.corrected{stroke-width:2}'
        '.incomplete{fill:#f59e0b;fill-opacity:.8;stroke:#b45309;stroke-width:1}</style>',
        '<text x="90" y="30" font-size="20">DPLL locking convergence from NO5 PIO0 raw waveform</text>',
        '<text x="90" y="47" font-size="12">Gray: raw diagnostic only; colored curves and span: corrected-eligible samples</text>',
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
        f'<text x="20" y="{top + plot_h / 2:.2f}" font-size="13" '
        f'transform="rotate(-90 20 {top + plot_h / 2:.2f})" '
        f'text-anchor="middle">Pulse phase modulo period (us)</text>',
        f'<text x="{left}" y="{span_top - 18}" font-size="14">'
        'Four-node circular phase span (smaller means convergence)</text>',
    ])
    for step in range(6):
        x = left + plot_w * step / 5
        elapsed_s = max_x * step / 5
        label = (f"{elapsed_s:.2f}" if max_x < 10 else
                 f"{elapsed_s:.1f}" if max_x < 100 else
                 f"{elapsed_s:.0f}")
        chunks.extend([
            f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" '
            f'y2="{top + plot_h}" class="grid"/>',
            f'<line x1="{x:.2f}" y1="{span_top}" x2="{x:.2f}" '
            f'y2="{span_top + span_h}" class="grid"/>',
            f'<text x="{x:.2f}" y="{span_top + span_h + 22}" '
            f'text-anchor="middle" font-size="12" '
            f'class="x-tick-label">{label}</text>',
        ])
    for step in range(3):
        y = span_top + span_h * (1 - step / 2)
        value_us = period_ns * step / 2 / 1000
        chunks.extend([
            f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" '
            f'y2="{y:.2f}" class="grid"/>',
            f'<text x="{left - 12}" y="{y + 5:.2f}" text-anchor="end" '
            f'font-size="12">{value_us:.0f}</text>',
        ])
    chunks.extend([
        f'<line x1="{left}" y1="{span_top + span_h}" '
        f'x2="{left + plot_w}" y2="{span_top + span_h}" class="axis"/>',
        f'<line x1="{left}" y1="{span_top}" x2="{left}" '
        f'y2="{span_top + span_h}" class="axis"/>',
        f'<text x="{left + plot_w / 2:.2f}" y="{height - 28}" '
        f'text-anchor="middle" font-size="13">Elapsed time (s)</text>',
    ])
    # Keep the raw layer visible for diagnosis, while the trend and span above
    # remain derived exclusively from corrected-eligible rows.
    display_rows = raw_rows if raw_rows is not None else rows
    for row in display_rows:
        x = left + plot_w * row["elapsed_s"] / max_x
        y = top + plot_h * (1 - row["phase_ns"] / period_ns)
        flags = int(row.get("quality_flags", 0))
        corrected = bool(row.get("corrected_eligible", True))
        incomplete = bool(flags & WAVEFORM_QUALITY_INCOMPLETE_WINDOW)
        marker_class = "incomplete" if incomplete else (
            "corrected" if corrected else "raw-only")
        title = ("incomplete TDMA window; diagnostic only" if incomplete else
                 "corrected-eligible phase sample" if corrected else
                 "raw diagnostic only; excluded from corrected convergence")
        chunks.append(
            f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{2.4 if not corrected else 1.7:.1f}" '
            f'class="{marker_class}" data-node="NO{row["channel"] + 1}" '
            f'data-quality-flags="{flags}" '
            f'data-corrected-eligible="{str(corrected).lower()}">'
            f'<title>{escape(title)}</title></circle>')
    for channel, color in enumerate(colors):
        channel_rows = sorted(
            (row for row in trend if row["channel"] == channel),
            key=lambda row: row["elapsed_s"])
        segments: list[list[dict[str, Any]]] = [[]]
        outliers: list[dict[str, Any]] = []
        previous = None
        for row in channel_rows:
            if previous is not None:
                direct_jump = abs(row["phase_center_ns"] -
                                  previous["phase_center_ns"])
                circular_jump = abs(_circular_delta_ns(
                    row["phase_center_ns"], previous["phase_center_ns"],
                    period_ns))
                wrapped = direct_jump > period_ns // 2
                abnormal = circular_jump > period_ns // 4
                if wrapped or abnormal:
                    segments.append([])
                if abnormal:
                    outliers.append(row)
            segments[-1].append(row)
            previous = row
        for segment_index, segment in enumerate(segments):
            points = " ".join(
                f'{left + plot_w * row["elapsed_s"] / max_x:.2f},'
                f'{top + plot_h * (1 - row["phase_center_ns"] / period_ns):.2f}'
                for row in segment)
            if not points:
                continue
            chunks.append(
                f'<polyline data-node="NO{channel + 1}" '
                f'data-segment="{segment_index}" points="{escape(points)}" '
                f'class="trend" '
                f'stroke="{color}"/>')
        for row in outliers:
            x = left + plot_w * row["elapsed_s"] / max_x
            y = top + plot_h * (1 - row["phase_center_ns"] / period_ns)
            chunks.append(
                f'<circle cx="{x:.2f}" cy="{y:.2f}" r="5" '
                f'class="outlier" data-node="NO{channel + 1}">'
                '<title>Phase innovation exceeds one quarter period</title>'
                '</circle>')
        chunks.append(
            f'<text x="{left + channel * 105}" y="{height - 58}" '
            f'font-size="13" fill="{color}">NO{channel + 1}</text>')
    span_points = " ".join(
        f'{left + plot_w * row["elapsed_s"] / max_x:.2f},'
        f'{span_top + span_h * (1 - row["span_ns"] / period_ns):.2f}'
        for row in span_trend)
    if span_points:
        chunks.append(
            f'<polyline points="{escape(span_points)}" class="span" '
            'data-series="four-node-circular-span"/>')
    chunks.append('</svg>')
    return "\n".join(chunks) + "\n"


def write_reports(result: dict[str, Any], out_dir: Path) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    record_fields = list(result["records"][0]) if result["records"] else [
        "raw_word", "sample_seq", "previous_sample_mask", "base_time_l32_ns",
        "matched_window_start_ns", "sample_period_ns", "timestamp_source",
        "timestamp_resolution_ns", "timestamp_flags", "dropped_before"]
    edge_fields = ["record_index", "sample_seq", "sample_index", "channel",
                   "edge", "timestamp_ns", "corrected_timestamp_ns",
                   "cycle_index", "window_start_ns", "timestamp_flags",
                   "quality_flags", "quality_names", "corrected_eligible",
                   "eligible", "valid",
                   "invalid_reason"]
    phase_fields = ["window_start_ns", "cycle_index", "elapsed_s", "span_ns"] + [
        field for channel in range(CHANNEL_COUNT)
        for field in (f"edge{channel}_ns", f"corrected_edge{channel}_ns",
                      f"offset{channel}_ns")]
    _write_csv(out_dir / "raw_records.csv", result["records"], record_fields)
    _write_csv(out_dir / "edges.csv", result["edges"], edge_fields)
    _write_csv(out_dir / "phase_curve.csv", result["phase"], phase_fields)
    tracking_fields = ["elapsed_s", "channel", "node", "timestamp_ns",
                       "corrected_timestamp_ns", "cycle_index",
                       "window_start_ns",
                       "matched_common_cycle", "phase_ns",
                       "relative_to_no1_ns", "phase_innovation_ns",
                       "reference_age_ms", "quality_flags",
                       "quality_names", "corrected_eligible"]
    trend_fields = ["elapsed_s", "channel", "node", "phase_center_ns",
                    "sample_count"]
    span_fields = ["elapsed_s", "span_ns", "node_count"]
    _write_csv(out_dir / "dpll_convergence.csv",
               result.get("corrected_phase_tracking", []), tracking_fields)
    _write_csv(out_dir / "dpll_convergence_trend.csv",
               result.get("corrected_phase_trend", []), trend_fields)
    _write_csv(out_dir / "dpll_convergence_span.csv",
               result.get("corrected_phase_span_trend", []), span_fields)
    (out_dir / "phase_curve.svg").write_text(
        _phase_svg(result["phase"]), encoding="utf-8")
    (out_dir / "dpll_convergence.svg").write_text(
        _convergence_svg(
            result.get("corrected_phase_tracking", []),
            result.get("corrected_phase_trend", []),
            result.get("corrected_phase_span_trend", []),
            result.get("corrected_convergence", {}).get(
                "pulse_period_ns", DEFAULT_PULSE_PERIOD_NS),
            result.get("phase_tracking", [])),
        encoding="utf-8")
    summary = {key: value for key, value in result.items()
               if key not in {"records", "edges", "phase", "phase_tracking",
                              "phase_trend", "phase_span_trend"}}
    summary["outputs"] = {
        "raw_records": str(out_dir / "raw_records.csv"),
        "edges": str(out_dir / "edges.csv"),
        "phase_curve": str(out_dir / "phase_curve.csv"),
        "phase_svg": str(out_dir / "phase_curve.svg"),
        "dpll_convergence": str(out_dir / "dpll_convergence.csv"),
        "dpll_convergence_trend": str(
            out_dir / "dpll_convergence_trend.csv"),
        "dpll_convergence_span": str(
            out_dir / "dpll_convergence_span.csv"),
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
    parser.add_argument(
        "--channel-delay-ns", action="append", default=[], metavar="CH=NS",
        help="subtract calibrated delay from channel timestamps (repeatable)")
    parser.add_argument(
        "--channel-phase-ns", action="append", default=[], metavar="CH=NS",
        help="authoritative TDMA/SMA phase of channel relative to NO1 "
             "(repeatable; required for strong cross-channel evidence)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        delays: dict[int, int] = {}
        for item in args.channel_delay_ns:
            channel_text, delay_text = item.split("=", 1)
            channel = int(channel_text, 0)
            if channel < 0 or channel >= CHANNEL_COUNT:
                raise ValueError(f"channel out of range: {channel}")
            delays[channel] = int(delay_text, 0)
        phases: dict[int, int] = {}
        for item in args.channel_phase_ns:
            channel_text, phase_text = item.split("=", 1)
            channel = int(channel_text, 0)
            if channel < 0 or channel >= CHANNEL_COUNT:
                raise ValueError(f"channel out of range: {channel}")
            phases[channel] = int(phase_text, 0)
        result = decode_segments(
            args.segment, pulse_period_ns=args.pulse_period_ns,
            channel_delays_ns=delays,
            channel_phase_ns=phases if phases else None)
        print(json.dumps(write_reports(result, args.out_dir),
                         ensure_ascii=False, indent=2))
    except (OSError, ValueError, struct.error) as exc:
        print(f"FAILED: {exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
