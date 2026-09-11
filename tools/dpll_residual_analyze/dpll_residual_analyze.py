#!/usr/bin/env python3
"""Analyze DPLL residual snapshots and render convergence/anomaly curves.

The input is one or more ``samples.json`` files written by
``tools/dpll_vdc_monitor``.  This is deliberately an offline tool: it adds no
work to the TDMA/Core1 path.  Monitor snapshots may decimate multiple DPLL
updates, so the report records that limitation instead of presenting them as
a full-rate control trace.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[2]
DPLL_LOCKED_STATE = 5
DEFAULT_LOCK_THRESHOLD_NS = 10000
CORRELATION_FLAG_CYCLE_PHASE = 0x00000001
CORRELATION_FLAG_COMMON_TIME = 0x00000002
CORRELATION_FLAG_LOCAL_PHASE_SAME_CLOCK = 0x00000004
CORRELATION_FLAG_LOCAL_PHASE_COMMON_MAPPED = 0x00000008


@dataclass
class ResidualPoint:
    board: str
    elapsed_s: float
    phase_residual_ns: int
    frequency_error_ppb: int
    dco_phase_offset_ns: int
    dco_period_adjust_ppb: int
    dpll_state: int
    gate_reject_code: int
    accepted_count: int
    rejected_count: int
    dpll_update_seq: int
    # A follower records verified peer DCO commands, not a local PI residual.
    capture_kind: str = "master_local_evidence"
    source_slot_id: int = 0
    control_generation: int = 0
    command_seq: int = 0
    effective_vdc_time_ns: int = 0
    # Command-application records carry no local PI residual.  They remain in
    # the trace for auditability but are excluded from residual statistics.
    residual_valid: bool = True
    residual_invalid_reason: str = ""
    centered_residual_ns: float = 0.0
    rolling_mean_ns: float = 0.0
    rolling_centered_mean_ns: float = 0.0
    rolling_rms_ns: float = 0.0
    rolling_jitter_rms_ns: float = 0.0
    anomaly_reasons: tuple[str, ...] = ()
    raw_phase_error_ns: int = 0
    observation_source_slot_id: int = 0
    observation_reference_slot_id: int = 0
    follow_master_slot_id: int = 0
    observation_delay_ns: int = 0
    observation_jitter_ns: int = 0
    observation_delay_generation: int = 0
    observation_bias_generation: int = 0
    observation_generation_metadata_present: bool = False
    observation_metadata_present: bool = False
    observation_metadata_valid: bool = False
    observation_metadata_reason: str = ""
    observation_correlation_flags: int = 0
    # Phase error after removing the directed transport delay.  Keep the raw
    # field above for trace compatibility; all jitter metrics use this value
    # when complete observation metadata is present.
    transport_corrected_phase_residual_ns: float | None = None


def _integer(mapping: dict[str, Any], name: str, default: int = 0) -> int:
    value = mapping.get(name, default)
    return int(value) if value is not None else default


def _correlation_reason(observation: dict[str, Any], source: int,
                        reference: int) -> str:
    """Return the independent time-domain qualification for one sample."""
    if "correlation_flags" not in observation:
        return ""
    try:
        correlation_flags = int(observation["correlation_flags"])
    except (TypeError, ValueError):
        return "correlation_flags_non_integer"
    required_flags = (CORRELATION_FLAG_CYCLE_PHASE |
                      CORRELATION_FLAG_COMMON_TIME)
    if (correlation_flags & required_flags) != required_flags:
        return "common_time_correlation_missing"
    if source != reference and not (
            correlation_flags & CORRELATION_FLAG_LOCAL_PHASE_COMMON_MAPPED):
        return "local_epoch_unaligned"
    return ""


def _append_reason(primary: str, secondary: str) -> str:
    return ";".join(reason for reason in (primary, secondary) if reason)


def _validate_observation_metadata(
        observation: dict[str, Any], follower_metadata: dict[str, Any],
        capture_kind: str) -> tuple[bool, str]:
    """Validate only metadata needed to remove a directed path delay.

    A zero-valued delay is valid when it is explicitly present.  Missing
    fields, malformed values, negative delay/jitter, or a path identity that
    disagrees with the follower evidence make correction unavailable.
    """
    if capture_kind not in ("master_local_evidence", "follower_local_evidence"):
        return False, "capture_kind_has_no_local_observation"
    required = ("source_slot_id", "reference_slot_id", "delay_ns", "jitter_ns")
    if capture_kind == "follower_local_evidence":
        required = required + ("follow_master_slot_id",)
    missing = [field for field in required if field not in observation]
    if missing:
        return False, "missing:" + ",".join(missing)
    try:
        source = int(observation["source_slot_id"])
        reference = int(observation["reference_slot_id"])
        delay = int(observation["delay_ns"])
        jitter = int(observation["jitter_ns"])
    except (TypeError, ValueError):
        return False, "non_integer_field"
    if source < 0 or reference < 0:
        return False, "negative_path_slot"
    if delay < 0 or jitter < 0:
        return False, "negative_delay_or_jitter"
    correlation_reason = _correlation_reason(observation, source, reference)
    generation_fields = ("delay_generation", "bias_generation")
    # A correction is only reproducible against the calibration/bias snapshot
    # that produced it. Legacy records without these fields remain raw-only.
    if any(field not in observation for field in generation_fields):
        return False, _append_reason(
            "generation_metadata_incomplete"
            if any(field in observation for field in generation_fields)
            else "generation_metadata_missing",
            correlation_reason)
    try:
        delay_generation = int(observation["delay_generation"])
        bias_generation = int(observation["bias_generation"])
    except (TypeError, ValueError):
        return False, _append_reason("generation_metadata_non_integer",
                                    correlation_reason)
    if delay_generation <= 0 or bias_generation <= 0:
        return False, _append_reason("generation_metadata_missing",
                                    correlation_reason)
    if capture_kind == "follower_local_evidence":
        for field in ("source_slot_id", "reference_slot_id"):
            if field not in follower_metadata:
                return False, "follower_path_identity_missing"
            try:
                if int(follower_metadata[field]) != int(observation[field]):
                    return False, "follower_path_identity_mismatch"
            except (TypeError, ValueError):
                return False, "follower_path_identity_invalid"
    return (False, correlation_reason) if correlation_reason else (True, "")


def load_monitor_samples(paths: Iterable[Path],
                         board_filter: set[str] | None = None
                         ) -> dict[str, list[ResidualPoint]]:
    series: dict[str, list[ResidualPoint]] = {}
    for path in paths:
        payload = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError(f"monitor samples must be a board object: {path}")
        for raw_board, raw_samples in payload.items():
            board = str(raw_board).upper()
            if board_filter is not None and board not in board_filter:
                continue
            if not isinstance(raw_samples, list):
                raise ValueError(f"{path}: {board} samples must be a list")
            target = series.setdefault(board, [])
            for sample in raw_samples:
                if not isinstance(sample, dict) or sample.get("error"):
                    continue
                vector = sample.get("dpll_vector")
                readiness = sample.get("readiness")
                if not isinstance(vector, dict) or not vector:
                    continue
                if not isinstance(readiness, dict):
                    readiness = {}
                capture_kind = str(sample.get(
                    "capture_kind", "master_local_evidence"))
                follower_command = sample.get("follower_command")
                if not isinstance(follower_command, dict):
                    follower_command = {}
                follower_observation = sample.get("follower_observation")
                if not isinstance(follower_observation, dict):
                    follower_observation = {}
                follower_metadata = (follower_observation if capture_kind ==
                                     "follower_local_evidence" else
                                     follower_command)
                observation = sample.get("observation")
                if not isinstance(observation, dict):
                    observation = {}
                observation_metadata_valid, observation_metadata_reason = (
                    _validate_observation_metadata(
                        observation, follower_metadata, capture_kind))
                target.append(ResidualPoint(
                    board=board,
                    elapsed_s=float(sample.get("elapsed_s", 0.0)),
                    phase_residual_ns=_integer(vector, "last_phase_error_ns"),
                    frequency_error_ppb=_integer(
                        vector, "last_frequency_error_ppb"),
                    dco_phase_offset_ns=_integer(vector, "dco_phase_offset_ns"),
                    dco_period_adjust_ppb=_integer(
                        vector, "dco_period_adjust_ppb"),
                    dpll_state=_integer(vector, "state"),
                    gate_reject_code=_integer(vector, "gate_reject_code"),
                    accepted_count=_integer(readiness, "accepted_count"),
                    rejected_count=_integer(readiness, "rejected_count"),
                    dpll_update_seq=_integer(vector, "dpll_update_seq"),
                    capture_kind=capture_kind,
                    source_slot_id=_integer(follower_metadata, "source_slot_id"),
                    control_generation=_integer(
                        follower_metadata, "control_generation"),
                    command_seq=_integer(follower_metadata, "command_seq"),
                    effective_vdc_time_ns=_integer(
                        follower_metadata, "effective_vdc_time_ns"),
                    residual_valid=capture_kind not in (
                        "follower_applied_command", "follower_state_transition"),
                    residual_invalid_reason=(
                        "follower_command_apply" if capture_kind ==
                        "follower_applied_command" else
                        "follower_state_transition" if capture_kind ==
                        "follower_state_transition" else ""),
                    raw_phase_error_ns=_integer(
                        vector, "raw_phase_error_ns",
                        _integer(vector, "last_phase_error_ns")),
                    observation_source_slot_id=_integer(
                        observation, "source_slot_id"),
                    observation_reference_slot_id=_integer(
                        observation, "reference_slot_id"),
                    follow_master_slot_id=_integer(
                        observation, "follow_master_slot_id"),
                    observation_delay_ns=_integer(observation, "delay_ns"),
                    observation_jitter_ns=_integer(observation, "jitter_ns"),
                    observation_delay_generation=_integer(
                        observation, "delay_generation"),
                    observation_bias_generation=_integer(
                        observation, "bias_generation"),
                    observation_generation_metadata_present=(
                        "delay_generation" in observation or
                        "bias_generation" in observation),
                    observation_metadata_present=bool(observation),
                    observation_metadata_valid=observation_metadata_valid,
                    observation_metadata_reason=observation_metadata_reason,
                    observation_correlation_flags=_integer(
                        observation, "correlation_flags"),
                ))
                if observation_metadata_valid:
                    target[-1].transport_corrected_phase_residual_ns = (
                        target[-1].phase_residual_ns -
                        target[-1].observation_delay_ns)
    for points in series.values():
        points.sort(key=lambda point: point.elapsed_s)
    return {board: points for board, points in series.items() if points}


def _rolling_metrics(points: list[ResidualPoint], window: int) -> None:
    for index, point in enumerate(points):
        window_points = [item for item in points[max(0, index - window + 1):index + 1]
                         if item.residual_valid]
        raw_values = [item.phase_residual_ns for item in window_points]
        centered_values = [item.centered_residual_ns for item in window_points]
        if not raw_values:
            point.rolling_mean_ns = 0.0
            point.rolling_centered_mean_ns = 0.0
            point.rolling_rms_ns = 0.0
            point.rolling_jitter_rms_ns = 0.0
            continue
        point.rolling_mean_ns = sum(raw_values) / len(raw_values)
        point.rolling_centered_mean_ns = sum(centered_values) / len(centered_values)
        point.rolling_rms_ns = math.sqrt(
            sum(value * value for value in raw_values) / len(raw_values))
        point.rolling_jitter_rms_ns = math.sqrt(
            sum(value * value for value in centered_values) / len(centered_values))


def _transport_corrected_value(point: ResidualPoint) -> float:
    """Return corrected residual, or raw residual when correction is unavailable."""
    if point.transport_corrected_phase_residual_ns is not None:
        return float(point.transport_corrected_phase_residual_ns)
    return float(point.phase_residual_ns)


def _linear_slope(xs: list[float], ys: list[float]) -> float:
    if len(xs) < 2:
        return 0.0
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denominator = sum((value - mean_x) ** 2 for value in xs)
    if denominator == 0.0:
        return 0.0
    return sum((x - mean_x) * (y - mean_y)
               for x, y in zip(xs, ys, strict=True)) / denominator


def _linear_fit(xs: list[float], ys: list[float]) -> tuple[float, float] | None:
    """Return the least-squares slope and intercept, when a line is defined."""
    if len(xs) < 2 or len(xs) != len(ys):
        return None
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denominator = sum((value - mean_x) ** 2 for value in xs)
    if denominator == 0.0:
        return None
    slope = sum((x - mean_x) * (y - mean_y)
                for x, y in zip(xs, ys, strict=True)) / denominator
    return slope, mean_y - slope * mean_x


def _rms(values: list[float] | list[int]) -> float:
    if not values:
        return 0.0
    return math.sqrt(sum(value * value for value in values) / len(values))


def _zero_crossings(values: list[int]) -> int:
    signs = [1 if value > 0 else -1 for value in values if value != 0]
    return sum(current != previous
               for previous, current in zip(signs, signs[1:]))


def _trend_classification(zero_crossings: int, rms_ratio: float,
                          sample_count: int) -> str:
    if sample_count < 6:
        return "insufficient_samples"
    if zero_crossings >= 3 and rms_ratio < 0.80:
        return "damped_oscillation_candidate"
    if zero_crossings >= 3 and rms_ratio <= 1.25:
        return "sustained_oscillation_candidate"
    if rms_ratio < 0.80:
        return "converging_candidate"
    if rms_ratio > 1.25:
        return "diverging_candidate"
    return "bounded_or_inconclusive"


def analyze_series(points: list[ResidualPoint], *, rolling_window: int,
                   lock_threshold_ns: int,
                   mad_multiplier: float) -> dict[str, Any]:
    if not points:
        raise ValueError("residual series is empty")
    follower_observation = any(
        point.capture_kind.startswith("follower_") for point in points)
    residual_points = [point for point in points if point.residual_valid]
    residuals = [point.phase_residual_ns for point in residual_points]
    bias = float(statistics.median(residuals)) if residuals else 0.0
    path_values: dict[tuple[int, int], list[int]] = {}
    for point in residual_points:
        if not point.observation_metadata_valid:
            continue
        key = (point.observation_source_slot_id,
               point.observation_reference_slot_id)
        path_values.setdefault(key, []).append(point.phase_residual_ns)
    path_bias = {
        key: float(statistics.median(values))
        for key, values in path_values.items()
    }
    corrected_path_values: dict[tuple[int, int], list[float]] = {}
    for point in residual_points:
        if not point.observation_metadata_valid:
            continue
        key = (point.observation_source_slot_id,
               point.observation_reference_slot_id)
        corrected_path_values.setdefault(key, []).append(
            _transport_corrected_value(point))
    corrected_path_bias = {
        key: float(statistics.median(values))
        for key, values in corrected_path_values.items()
    }
    generation_points = [point for point in residual_points
                         if point.observation_generation_metadata_present]
    generation_keys = {
        (point.observation_delay_generation,
         point.observation_bias_generation)
        for point in generation_points
        if point.observation_metadata_valid
    }
    generation_metadata_consistent = bool(residual_points) and (
        len(generation_points) == len(residual_points) and
        all(point.observation_metadata_valid for point in generation_points) and
        len(generation_keys) == 1)
    delay_correction_available = bool(residuals) and all(
        point.observation_metadata_valid and
        point.transport_corrected_phase_residual_ns is not None
        for point in residual_points)
    delay_correction_available = (
        delay_correction_available and generation_metadata_consistent)
    for point in points:
        key = (point.observation_source_slot_id,
               point.observation_reference_slot_id)
        if not point.residual_valid:
            point.centered_residual_ns = 0.0
        elif delay_correction_available and point.observation_metadata_valid and \
                key in corrected_path_bias:
            point.centered_residual_ns = (
                _transport_corrected_value(point) - corrected_path_bias[key])
        else:
            # Preserve a usable raw-residual diagnostic, but do not label it
            # as transport-corrected when path metadata is incomplete.
            point.centered_residual_ns = point.phase_residual_ns - bias
    _rolling_metrics(points, rolling_window)
    centered_residuals = [point.centered_residual_ns for point in residual_points]
    # Legacy/direct residual fixtures have no transport metadata. A captured
    # observation with metadata is stricter: one unprovable sample means the
    # series may retain raw spread for diagnosis but cannot report true output
    # jitter.
    measurement_untrusted = any(
        point.observation_metadata_present and
        not point.observation_metadata_valid for point in residual_points)
    trusted_centered_residuals = (
        [] if measurement_untrusted else centered_residuals)
    mad = (float(statistics.median(abs(value) for value in centered_residuals))
           if centered_residuals else 0.0)
    trusted_mad = (float(statistics.median(
        abs(value) for value in trusted_centered_residuals))
        if trusted_centered_residuals else None)
    robust_limit = max(float(lock_threshold_ns), mad_multiplier * mad)
    previous_rejected = points[0].rejected_count
    previous_update_seq = points[0].dpll_update_seq
    update_deltas: list[int] = []
    anomaly_count = 0
    for point in points:
        reasons: list[str] = []
        if not point.residual_valid:
            point.anomaly_reasons = ()
            continue
        if abs(point.centered_residual_ns) > robust_limit:
            reasons.append("residual_outlier")
        if point.gate_reject_code != 0:
            reasons.append(f"gate_{point.gate_reject_code}")
        if point.dpll_state != DPLL_LOCKED_STATE:
            reasons.append(f"state_{point.dpll_state}")
        if (point.capture_kind == "follower_local_evidence" and
                not point.observation_metadata_valid):
            reasons.append("observation_metadata_missing")
        if "local_epoch_unaligned" in point.observation_metadata_reason.split(";"):
            reasons.append("local_epoch_unaligned")
        if (point.observation_generation_metadata_present and
                not generation_metadata_consistent):
            reasons.append("generation_metadata_inconsistent")
        if point.rejected_count > previous_rejected:
            reasons.append("reject_counter_advanced")
        if point.dpll_update_seq > previous_update_seq:
            update_deltas.append(point.dpll_update_seq - previous_update_seq)
        previous_rejected = point.rejected_count
        previous_update_seq = point.dpll_update_seq
        point.anomaly_reasons = tuple(reasons)
        anomaly_count += int(bool(reasons))

    split = max(1, len(centered_residuals) // 2)
    first_rms = _rms(centered_residuals[:split])
    second_rms = _rms(centered_residuals[split:])
    rms_ratio = second_rms / first_rms if first_rms > 0.0 else 1.0
    crossings = _zero_crossings(centered_residuals)
    duration_s = max(0.0, points[-1].elapsed_s - points[0].elapsed_s)
    point_intervals = [current.elapsed_s - previous.elapsed_s
                       for previous, current in zip(points, points[1:])
                       if current.elapsed_s > previous.elapsed_s]
    median_interval_s = (float(statistics.median(point_intervals))
                         if point_intervals else 0.0)
    median_updates_per_snapshot = (float(statistics.median(update_deltas))
                                   if update_deltas else 0.0)
    decimated = any(delta > 1 for delta in update_deltas)
    centered_fit = _linear_fit(
        [point.elapsed_s for point in residual_points], centered_residuals)
    analysis = {
        "board": points[0].board,
        "sample_count": len(points),
        "duration_s": duration_s,
        "median_snapshot_interval_s": median_interval_s,
        "median_updates_per_snapshot": median_updates_per_snapshot,
        "full_rate_trace": not decimated,
        "analysis_confidence": ("no_residual_evidence" if not residuals else
                               ("low_observation_metadata" if any(
                                   point.capture_kind ==
                                   "follower_local_evidence" and
                                   not point.observation_metadata_valid
                                   for point in residual_points) else
                               ("low_decimated" if decimated else
                               ("low_sample_count" if len(residuals) < 20 else "normal")))),
        "residual_sample_count": len(residuals),
        "residual_coverage": len(residuals) / len(points),
        "residual_metrics_applicable": bool(residuals),
        "min_phase_residual_ns": min(residuals) if residuals else None,
        "max_phase_residual_ns": max(residuals) if residuals else None,
        "mean_phase_residual_ns": sum(residuals) / len(residuals) if residuals else None,
        "bias_ns": bias if residuals else None,
        "bias_by_path_ns": {
            f"{source}->{reference}": value
            for (source, reference), value in path_bias.items()
        },
        "transport_corrected_bias_ns": (
            float(statistics.median(
                [_transport_corrected_value(point)
                 for point in residual_points
                 if point.observation_metadata_valid]))
            if any(point.observation_metadata_valid
                   for point in residual_points) else None),
        "transport_corrected_bias_by_path_ns": {
            f"{source}->{reference}": value
            for (source, reference), value in corrected_path_bias.items()
        },
        "delay_by_path_ns": {
            f"{source}->{reference}": float(statistics.median(
                [point.observation_delay_ns for point in residual_points
                 if (point.observation_source_slot_id,
                     point.observation_reference_slot_id) ==
                    (source, reference)]))
            for (source, reference) in corrected_path_bias
            if any(point.observation_metadata_valid for point in residual_points
                   if (point.observation_source_slot_id,
                       point.observation_reference_slot_id) ==
                      (source, reference))
        },
        "jitter_mad_ns": trusted_mad,
        "jitter_rms_ns": (_rms(trusted_centered_residuals)
                          if trusted_centered_residuals else None),
        "jitter_peak_to_peak_ns": (
            max(trusted_centered_residuals) - min(trusted_centered_residuals)
            if trusted_centered_residuals else None),
        "raw_jitter_mad_ns": mad if residuals else None,
        "raw_jitter_rms_ns": _rms(centered_residuals) if residuals else None,
        "raw_jitter_peak_to_peak_ns": (
            max(centered_residuals) - min(centered_residuals)
            if residuals else None),
        "jitter_metrics_trusted": bool(trusted_centered_residuals),
        "bias_corrected_min_ns": min(centered_residuals) if residuals else None,
        "bias_corrected_max_ns": max(centered_residuals) if residuals else None,
        "first_half_rms_ns": first_rms,
        "second_half_rms_ns": second_rms,
        "rms_ratio": rms_ratio,
        "absolute_residual_slope_ns_per_s": _linear_slope(
            [point.elapsed_s for point in residual_points],
            [abs(value) for value in centered_residuals]),
        "bias_corrected_linear_slope_ns_per_s": (
            centered_fit[0] if centered_fit is not None else None),
        "bias_corrected_linear_intercept_ns": (
            centered_fit[1] if centered_fit is not None else None),
        "zero_crossings": crossings,
        "trend_classification": _trend_classification(
            crossings, rms_ratio, len(points)),
        "locked_sample_count": sum(
            point.dpll_state == DPLL_LOCKED_STATE for point in residual_points),
        "anomaly_sample_count": anomaly_count,
        "robust_median_ns": bias,
        "robust_mad_ns": mad,
        "robust_outlier_limit_ns": robust_limit,
        "lock_threshold_ns": lock_threshold_ns,
        "source": "dpll_vdc_monitor_scpi_snapshots",
    }
    metadata_missing = any(
        point.capture_kind == "follower_local_evidence" and
        not point.observation_metadata_valid for point in points)
    lock_reasons: list[str] = []
    if follower_observation:
        lock_reasons.append("follower_has_no_local_pi_lock_authority")
    if not residuals:
        lock_reasons.append("no_residual_evidence")
    if len(residuals) < 20:
        lock_reasons.append("insufficient_samples")
    if decimated:
        lock_reasons.append("decimated_trace")
    if metadata_missing:
        lock_reasons.append("observation_metadata_missing")
    if any("local_epoch_unaligned" in
           point.observation_metadata_reason.split(";")
           for point in points):
        lock_reasons.append("local_epoch_unaligned")
    if not generation_metadata_consistent:
        lock_reasons.append("generation_metadata_inconsistent")
    if anomaly_count:
        lock_reasons.append("sample_anomaly")
    if residuals and any(point.dpll_state != DPLL_LOCKED_STATE
                         for point in residual_points):
        lock_reasons.append("not_all_samples_locked")
    analysis["lock_decision"] = (
        "not_local_lock_evidence" if follower_observation else
        ("proven" if not lock_reasons else "not_proven"))
    analysis["lock_decision_reasons"] = lock_reasons
    corrected_points = [point for point in residual_points
                        if delay_correction_available and
                        point.observation_metadata_valid and
                        point.transport_corrected_phase_residual_ns is not None]
    analysis["delay_correction_available"] = delay_correction_available
    analysis["generation_metadata_consistent"] = generation_metadata_consistent
    analysis["observation_generation_count"] = len(generation_points)
    analysis["transport_corrected_sample_count"] = len(corrected_points)
    analysis["transport_delay_removed_for_jitter"] = (
        analysis["delay_correction_available"])
    analysis["delay_correction_rejection_reasons"] = sorted({
        point.observation_metadata_reason for point in residual_points
        if not point.observation_metadata_valid and
        point.observation_metadata_reason
    })
    has_local_evidence = any(
        point.capture_kind == "follower_local_evidence" for point in points)
    if follower_observation and has_local_evidence:
        observed = [point for point in points
                    if point.capture_kind == "follower_local_evidence"]
        sequences = [point.dpll_update_seq for point in observed]
        analysis.update({
            "analysis_kind": "follower_local_evidence",
            "local_pi_lock_evidence": False,
            "local_evidence_sample_count": len(observed),
            "source_slots": sorted({point.source_slot_id for point in observed}),
            "sample_sequence_strict": all(
                current > previous for previous, current in
                zip(sequences, sequences[1:])),
            "multi_point_ready": len(observed) >= 2 and all(
                current > previous for previous, current in
                zip(sequences, sequences[1:])),
            "lock_interpretation": (
                "follower local TDMA evidence is diagnostic; local PI and "
                "lock promotion are disabled"),
            "locked_sample_count": 0,
        })
    elif follower_observation:
        applied = [point for point in points
                   if point.capture_kind == "follower_applied_command"]
        command_sequences = [point.command_seq for point in applied]
        effective_times = [point.effective_vdc_time_ns for point in applied]
        analysis.update({
            "analysis_kind": "follower_validated_command_apply",
            "local_pi_lock_evidence": False,
            "accepted_command_sample_count": len(applied),
            "source_slots": sorted({point.source_slot_id for point in applied}),
            "control_generations": sorted(
                {point.control_generation for point in applied}),
            "command_sequence_strict": all(
                current > previous for previous, current in
                zip(command_sequences, command_sequences[1:])),
            "effective_time_strict": all(
                current > previous for previous, current in
                zip(effective_times, effective_times[1:])),
            "lock_interpretation": (
                "follower command application is not local PI lock evidence"),
            # A follower must not promote a peer lock state into a local lock.
            "locked_sample_count": 0,
        })
    else:
        analysis["analysis_kind"] = "master_local_residual"
        analysis["local_pi_lock_evidence"] = True
    return analysis


def _scale(value: float, source_min: float, source_max: float,
           target_min: float, target_max: float) -> float:
    if source_max <= source_min:
        return (target_min + target_max) / 2.0
    ratio = (value - source_min) / (source_max - source_min)
    return target_min + ratio * (target_max - target_min)


def render_follower_svg(board: str, points: list[ResidualPoint],
                        analysis: dict[str, Any]) -> str:
    """Render a FOLLOWER command-application trace without inventing PI data."""
    width, height = 1600, 860
    left, right = 110.0, 1550.0
    phase_top, phase_bottom = 150.0, 470.0
    rate_top, rate_bottom = 550.0, 740.0
    applied = [point for point in points
               if point.capture_kind == "follower_applied_command"]
    display = applied or points
    start_s, end_s = display[0].elapsed_s, display[-1].elapsed_s
    if end_s <= start_s:
        end_s = start_s + 1.0
    phase_extent = max(max(abs(point.dco_phase_offset_ns) for point in display) * 1.1,
                       1.0)
    rate_extent = max(max(abs(point.dco_period_adjust_ppb) for point in display) * 1.1,
                      1.0)

    def x(value: float) -> float:
        return _scale(value, start_s, end_s, left, right)

    def phase_y(value: float) -> float:
        return _scale(value, -phase_extent, phase_extent,
                      phase_bottom, phase_top)

    def rate_y(value: float) -> float:
        return _scale(value, -rate_extent, rate_extent, rate_bottom, rate_top)

    phase = " ".join(
        f"{x(point.elapsed_s):.1f},{phase_y(point.dco_phase_offset_ns):.1f}"
        for point in display)
    rate = " ".join(
        f"{x(point.elapsed_s):.1f},{rate_y(point.dco_period_adjust_ppb):.1f}"
        for point in display)
    source_slots = ",".join(str(value) for value in analysis["source_slots"]) or "none"
    generations = ",".join(str(value) for value in analysis["control_generations"]) or "none"
    sequence_strict = int(analysis["command_sequence_strict"])
    time_strict = int(analysis["effective_time_strict"])
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<style>text{font-family:ui-monospace,Consolas,monospace;fill:#172033}'
        '.title{font-size:22px;font-weight:700}.label{font-size:14px;font-weight:600}'
        '.small{font-size:12px}.grid{stroke:#d7dce5;stroke-width:1}'
        '.zero{stroke:#667085;stroke-width:1.5}.line{fill:none;stroke-width:2.5}'
        '.point{fill:#2563eb;stroke:#fff;stroke-width:1.5}</style>',
        '<rect width="100%" height="100%" fill="#fff"/>',
        f'<text x="30" y="34" class="title">{escape(board)} FOLLOWER validated command application</text>',
        '<text x="30" y="60" class="small">local PI/integrator bypassed; this is not local PI lock evidence</text>',
        f'<text x="30" y="82" class="small">applied records={analysis["accepted_command_sample_count"]} '
        f'source slots={escape(source_slots)} control generations={escape(generations)} '
        f'strict command sequence={sequence_strict} strict effective time={time_strict}</text>',
        '<text x="30" y="104" class="small">each point is a Core1-validated peer command applied at its common effective VDC time</text>',
        f'<rect x="{left}" y="{phase_top}" width="{right-left}" '
        f'height="{phase_bottom-phase_top}" fill="#fbfcfe" stroke="#c8ced8"/>',
        f'<rect x="{left}" y="{rate_top}" width="{right-left}" '
        f'height="{rate_bottom-rate_top}" fill="#fbfcfe" stroke="#c8ced8"/>',
        f'<text x="30" y="{phase_top + 18}" class="label">applied DCO phase offset (ns)</text>',
        f'<text x="30" y="{rate_top + 18}" class="label">applied DCO period adjustment (ppb)</text>',
        f'<line x1="{left}" y1="{phase_y(0):.1f}" x2="{right}" y2="{phase_y(0):.1f}" class="zero"/>',
        f'<line x1="{left}" y1="{rate_y(0):.1f}" x2="{right}" y2="{rate_y(0):.1f}" class="zero"/>',
        f'<polyline points="{phase}" class="line" stroke="#2563eb"/>',
        f'<polyline points="{rate}" class="line" stroke="#16803c"/>',
    ]
    for fraction in (0.0, 0.25, 0.5, 0.75, 1.0):
        xx = left + (right - left) * fraction
        elapsed = start_s + (end_s - start_s) * fraction
        chunks.append(f'<line x1="{xx:.1f}" y1="{phase_top}" x2="{xx:.1f}" '
                      f'y2="{rate_bottom}" class="grid"/>')
        chunks.append(f'<text x="{xx - 18:.1f}" y="790" '
                      f'class="small">{elapsed:.3f}s</text>')
    for point in applied:
        title = escape(
            f"source={point.source_slot_id} generation={point.control_generation} "
            f"command_seq={point.command_seq} effective_vdc_time_ns="
            f"{point.effective_vdc_time_ns}")
        chunks.extend([
            f'<circle cx="{x(point.elapsed_s):.1f}" cy="{phase_y(point.dco_phase_offset_ns):.1f}" r="4" class="point"><title>{title}</title></circle>',
            f'<circle cx="{x(point.elapsed_s):.1f}" cy="{rate_y(point.dco_period_adjust_ppb):.1f}" r="4" class="point"><title>{title}</title></circle>',
        ])
    chunks.extend([
        '<line x1="110" y1="820" x2="145" y2="820" stroke="#2563eb" stroke-width="3"/>',
        '<text x="152" y="824" class="small">applied phase offset</text>',
        '<line x1="350" y1="820" x2="385" y2="820" stroke="#16803c" stroke-width="3"/>',
        '<text x="392" y="824" class="small">applied period adjustment</text>',
        '<text x="690" y="824" class="small">hover a point for source/generation/sequence/effective time</text>',
        '</svg>',
    ])
    return "\n".join(chunks) + "\n"


def render_follower_evidence_svg(board: str, points: list[ResidualPoint],
                                 analysis: dict[str, Any]) -> str:
    """Render the follower's local send/receive residual observation."""
    width, height = 1600, 860
    left, right = 110.0, 1550.0
    top, bottom = 120.0, 500.0
    start_s, end_s = points[0].elapsed_s, points[-1].elapsed_s
    if end_s <= start_s:
        end_s = start_s + 1.0
    extent = max(10000.0,
                 max((abs(point.phase_residual_ns) for point in points
                      if point.residual_valid), default=1) * 1.1,
                 1.0)

    def x(value: float) -> float:
        return _scale(value, start_s, end_s, left, right)

    def y(value: float) -> float:
        return _scale(value, -extent, extent, bottom, top)

    raw = " ".join(
        f"{x(point.elapsed_s):.1f},{y(point.phase_residual_ns):.1f}"
        for point in points)
    mean = " ".join(
        f"{x(point.elapsed_s):.1f},{y(point.rolling_mean_ns):.1f}"
        for point in points)
    centered = " ".join(
        f"{x(point.elapsed_s):.1f},{y(point.centered_residual_ns):.1f}"
        for point in points if point.residual_valid)
    centered_mean = " ".join(
        f"{x(point.elapsed_s):.1f},{y(point.rolling_centered_mean_ns):.1f}"
        for point in points if point.residual_valid)
    residual_points = [point for point in points if point.residual_valid]
    trend_fit = _linear_fit(
        [point.elapsed_s for point in residual_points],
        [point.centered_residual_ns for point in residual_points])
    trend_line = ""
    trend_label = "linear convergence trend unavailable (fewer than two valid residuals)"
    if trend_fit is not None:
        slope, intercept = trend_fit
        start = residual_points[0].elapsed_s
        end = residual_points[-1].elapsed_s
        trend_line = (
            f'<line x1="{x(start):.1f}" y1="{y(slope * start + intercept):.1f}" '
            f'x2="{x(end):.1f}" y2="{y(slope * end + intercept):.1f}" '
            'class="trendline" stroke="#b54708"/>')
        trend_label = (
            "linear convergence trend (bias-corrected observation only; "
            f"not lock evidence), slope={slope:.3f} ns/s")
    source_slots = ",".join(str(value) for value in analysis["source_slots"]) or "none"
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<style>text{font-family:ui-monospace,Consolas,monospace;fill:#172033}.title{font-size:22px;font-weight:700}.label{font-size:14px;font-weight:600}.small{font-size:12px}.grid{stroke:#d7dce5}.zero{stroke:#667085;stroke-width:1.5}.line{fill:none;stroke-width:2}.mean{fill:none;stroke-width:3}.centered{fill:none;stroke-width:2;stroke-dasharray:7 4}.trendline{stroke-width:3;stroke-dasharray:10 5}</style>',
        '<rect width="100%" height="100%" fill="#fff"/>',
        f'<text x="30" y="34" class="title">{escape(board)} FOLLOWER local TDMA evidence</text>',
        '<text x="30" y="60" class="small">same local send/receive timestamp and path-delay observation as MASTER; PI and lock promotion disabled</text>',
        f'<text x="30" y="82" class="small">samples={len(points)} residuals={analysis["residual_sample_count"]} bias={analysis["bias_ns"]} ns jitter_rms={analysis["jitter_rms_ns"]} ns coverage={analysis["residual_coverage"]:.3f}</text>',
        f'<rect x="{left}" y="{top}" width="{right-left}" height="{bottom-top}" fill="#fbfcfe" stroke="#c8ced8"/>',
        f'<text x="30" y="{top + 18}" class="label">local phase residual (ns)</text>',
        f'<line x1="{left}" y1="{y(0):.1f}" x2="{right}" y2="{y(0):.1f}" class="zero"/>',
        f'<polyline points="{raw}" class="line" stroke="#2563eb"/>',
        f'<polyline points="{mean}" class="mean" stroke="#d97706"/>',
        f'<polyline points="{centered}" class="centered" stroke="#16803c"/>',
        f'<polyline points="{centered_mean}" class="mean" stroke="#7c3aed"/>',
        trend_line,
    ]
    for fraction in (0.0, 0.25, 0.5, 0.75, 1.0):
        xx = left + (right - left) * fraction
        elapsed = start_s + (end_s - start_s) * fraction
        chunks.append(f'<line x1="{xx:.1f}" y1="{top}" x2="{xx:.1f}" y2="{bottom}" class="grid"/>')
        chunks.append(f'<text x="{xx - 18:.1f}" y="530" class="small">{elapsed:.3f}s</text>')
    chunks.extend([
        '<line x1="110" y1="570" x2="145" y2="570" stroke="#2563eb" stroke-width="2"/>',
        '<text x="152" y="574" class="small">raw local residual</text>',
        '<line x1="300" y1="570" x2="335" y2="570" stroke="#d97706" stroke-width="3"/>',
        '<text x="342" y="574" class="small">rolling mean</text>',
        '<line x1="455" y1="570" x2="490" y2="570" stroke="#16803c" stroke-width="2" stroke-dasharray="7 4"/>',
        '<text x="497" y="574" class="small">bias-corrected residual</text>',
        '<line x1="700" y1="570" x2="735" y2="570" stroke="#7c3aed" stroke-width="3"/>',
        '<text x="742" y="574" class="small">centered rolling mean</text>',
        '<line x1="110" y1="600" x2="145" y2="600" stroke="#b54708" stroke-width="3" stroke-dasharray="10 5"/>',
        f'<text x="152" y="604" class="small">{trend_label}</text>',
        '</svg>',
    ])
    return "\n".join(chunks) + "\n"


def render_svg(board: str, points: list[ResidualPoint], analysis: dict[str, Any],
               *, lock_threshold_ns: int) -> str:
    if analysis.get("analysis_kind") == "follower_validated_command_apply":
        return render_follower_svg(board, points, analysis)
    if analysis.get("analysis_kind") == "follower_local_evidence":
        return render_follower_evidence_svg(board, points, analysis)
    width, height = 1600, 860
    left, right = 110.0, 1550.0
    phase_top, phase_bottom = 120.0, 500.0
    freq_top, freq_bottom = 590.0, 775.0
    start_s, end_s = points[0].elapsed_s, points[-1].elapsed_s
    if end_s <= start_s:
        end_s = start_s + 1.0
    phase_extent = max(float(lock_threshold_ns) * 1.2,
                       max((abs(point.phase_residual_ns) for point in points
                            if point.residual_valid), default=1) * 1.1,
                       1.0)
    freq_extent = max(max(abs(point.frequency_error_ppb) for point in points),
                      max(abs(point.dco_period_adjust_ppb) for point in points),
                      1)

    def x(value: float) -> float:
        return _scale(value, start_s, end_s, left, right)

    def phase_y(value: float) -> float:
        return _scale(value, -phase_extent, phase_extent,
                      phase_bottom, phase_top)

    def freq_y(value: float) -> float:
        return _scale(value, -freq_extent, freq_extent,
                      freq_bottom, freq_top)

    raw = " ".join(f"{x(point.elapsed_s):.1f},{phase_y(point.phase_residual_ns):.1f}"
                   for point in points)
    mean = " ".join(f"{x(point.elapsed_s):.1f},{phase_y(point.rolling_mean_ns):.1f}"
                    for point in points)
    centered = " ".join(
        f"{x(point.elapsed_s):.1f},{phase_y(point.centered_residual_ns):.1f}"
        for point in points if point.residual_valid)
    centered_mean = " ".join(
        f"{x(point.elapsed_s):.1f},{phase_y(point.rolling_centered_mean_ns):.1f}"
        for point in points if point.residual_valid)
    residual_points = [point for point in points if point.residual_valid]
    trend_fit = _linear_fit(
        [point.elapsed_s for point in residual_points],
        [point.centered_residual_ns for point in residual_points])
    trend_line = ""
    trend_label = "linear convergence trend unavailable (fewer than two valid residuals)"
    if trend_fit is not None:
        slope, intercept = trend_fit
        start = residual_points[0].elapsed_s
        end = residual_points[-1].elapsed_s
        trend_line = (
            f'<line x1="{x(start):.1f}" y1="{phase_y(slope * start + intercept):.1f}" '
            f'x2="{x(end):.1f}" y2="{phase_y(slope * end + intercept):.1f}" '
            'class="trendline" stroke="#b54708"/>')
        trend_label = (
            "linear convergence trend (bias-corrected residual; "
            f"observation only), slope={slope:.3f} ns/s")
    freq = " ".join(f"{x(point.elapsed_s):.1f},{freq_y(point.frequency_error_ppb):.1f}"
                    for point in points)
    dco = " ".join(f"{x(point.elapsed_s):.1f},{freq_y(point.dco_period_adjust_ppb):.1f}"
                   for point in points)
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<style>text{font-family:ui-monospace,Consolas,monospace;fill:#172033}'
        '.title{font-size:22px;font-weight:700}.label{font-size:14px;font-weight:600}'
        '.small{font-size:12px}.grid{stroke:#d7dce5;stroke-width:1}'
        '.zero{stroke:#667085;stroke-width:1.5}.limit{stroke:#b42318;stroke-width:1.5;'
        'stroke-dasharray:8 6}.line{fill:none;stroke-width:2}.mean{fill:none;stroke-width:3}'
        '.centered{fill:none;stroke-width:2;stroke-dasharray:7 4}'
        '.trendline{stroke-width:3;stroke-dasharray:10 5}'
        '.anomaly{fill:#b42318;stroke:#fff;stroke-width:1.5}</style>',
        '<rect width="100%" height="100%" fill="#fff"/>',
        f'<text x="30" y="34" class="title">{escape(board)} DPLL time residual</text>',
        f'<text x="30" y="60" class="small">trend={escape(str(analysis["trend_classification"]))} '
        f'locked={analysis["locked_sample_count"]}/{analysis["sample_count"]} '
        f'RMS ratio={analysis["rms_ratio"]:.3f} zero_crossings={analysis["zero_crossings"]} '
        f'confidence={escape(str(analysis["analysis_confidence"]))}</text>',
        f'<text x="30" y="100" class="small">raw median/bias={analysis["bias_ns"]} ns; '
        f'bias-corrected jitter MAD/RMS/pk-pk={analysis["jitter_mad_ns"]}/'
        f'{analysis["jitter_rms_ns"]}/{analysis["jitter_peak_to_peak_ns"]} ns; '
        f'residual coverage={analysis["residual_coverage"]:.3f}</text>',
        f'<text x="30" y="82" class="small">source=SCPI snapshots; '
        f'median interval={analysis["median_snapshot_interval_s"]:.3f}s; '
        f'median DPLL updates/snapshot={analysis["median_updates_per_snapshot"]:.1f}; '
        f'full_rate={int(analysis["full_rate_trace"])}</text>',
        f'<rect x="{left}" y="{phase_top}" width="{right-left}" '
        f'height="{phase_bottom-phase_top}" fill="#fbfcfe" stroke="#c8ced8"/>',
        f'<text x="30" y="{phase_top + 18}" class="label">phase residual (ns)</text>',
    ]
    for fraction in (0.0, 0.25, 0.5, 0.75, 1.0):
        xx = left + (right - left) * fraction
        elapsed = start_s + (end_s - start_s) * fraction
        chunks.append(f'<line x1="{xx:.1f}" y1="{phase_top}" x2="{xx:.1f}" '
                      f'y2="{freq_bottom}" class="grid"/>')
        chunks.append(f'<text x="{xx - 18:.1f}" y="{height - 32}" '
                      f'class="small">{elapsed:.1f}s</text>')
    for value in (-lock_threshold_ns, 0, lock_threshold_ns):
        css = "zero" if value == 0 else "limit"
        yy = phase_y(float(value))
        chunks.append(f'<line x1="{left}" y1="{yy:.1f}" x2="{right}" '
                      f'y2="{yy:.1f}" class="{css}"/>')
        chunks.append(f'<text x="45" y="{yy + 4:.1f}" class="small">{value}</text>')
    chunks.extend([
        f'<polyline points="{raw}" class="line" stroke="#2563eb"/>',
        f'<polyline points="{mean}" class="mean" stroke="#d97706"/>',
        f'<polyline points="{centered}" class="centered" stroke="#16803c"/>',
        f'<polyline points="{centered_mean}" class="mean" stroke="#7c3aed"/>',
        trend_line,
    ])
    for point in points:
        if point.anomaly_reasons:
            reason = escape(",".join(point.anomaly_reasons))
            chunks.append(f'<circle cx="{x(point.elapsed_s):.1f}" '
                          f'cy="{phase_y(point.phase_residual_ns):.1f}" r="5" '
                          f'class="anomaly"><title>{reason}</title></circle>')
    chunks.extend([
        f'<rect x="{left}" y="{freq_top}" width="{right-left}" '
        f'height="{freq_bottom-freq_top}" fill="#fbfcfe" stroke="#c8ced8"/>',
        f'<line x1="{left}" y1="{freq_y(0):.1f}" x2="{right}" '
        f'y2="{freq_y(0):.1f}" class="zero"/>',
        f'<text x="30" y="{freq_top + 18}" class="label">frequency (ppb)</text>',
        f'<polyline points="{freq}" class="line" stroke="#16803c"/>',
        f'<polyline points="{dco}" class="line" stroke="#7c3aed"/>',
        '<line x1="110" y1="812" x2="145" y2="812" stroke="#2563eb" stroke-width="2"/>',
        '<text x="152" y="816" class="small">raw residual</text>',
        '<line x1="285" y1="812" x2="320" y2="812" stroke="#d97706" stroke-width="3"/>',
        '<text x="327" y="816" class="small">rolling mean</text>',
        '<line x1="430" y1="812" x2="465" y2="812" stroke="#16803c" stroke-width="2" stroke-dasharray="7 4"/>',
        '<text x="472" y="816" class="small">bias-corrected residual</text>',
        '<line x1="700" y1="812" x2="735" y2="812" stroke="#7c3aed" stroke-width="3"/>',
        '<text x="742" y="816" class="small">centered rolling mean</text>',
        '<line x1="110" y1="840" x2="145" y2="840" stroke="#b54708" stroke-width="3" stroke-dasharray="10 5"/>',
        f'<text x="152" y="844" class="small">{trend_label}</text>',
        '<line x1="480" y1="812" x2="515" y2="812" stroke="#16803c" stroke-width="2"/>',
        '<text x="522" y="816" class="small">frequency error</text>',
        '<line x1="690" y1="812" x2="725" y2="812" stroke="#7c3aed" stroke-width="2"/>',
        '<text x="732" y="816" class="small">DCO rate correction</text>',
        '<circle cx="945" cy="812" r="5" class="anomaly"/>',
        '<text x="958" y="816" class="small">gate/state/outlier anomaly</text>',
        '</svg>',
    ])
    return "\n".join(chunks) + "\n"


def render_combined_svg(series: dict[str, list[ResidualPoint]],
                        analyses: dict[str, dict[str, Any]], *,
                        lock_threshold_ns: int) -> str:
    """Render master residuals without presenting follower commands as errors."""
    colors = {"NO1": "#2563eb", "NO2": "#dc2626",
              "NO3": "#059669", "NO4": "#7c3aed"}
    width, height = 1600, 760
    left, right, top, bottom = 110.0, 1510.0, 100.0, 570.0
    boards = [name for name in ("NO1", "NO2", "NO3", "NO4")
              if name in series and analyses.get(name, {}).get(
                  "analysis_kind") != "follower_validated_command_apply"]
    all_points = [point for name in boards for point in series[name]]
    max_x = max((point.elapsed_s for point in all_points), default=1.0) or 1.0
    extent = max(float(lock_threshold_ns) * 1.2,
                 max((abs(point.centered_residual_ns) for point in all_points
                      if point.residual_valid),
                     default=1) * 1.1, 1.0)
    def x(value: float) -> float:
        return left + (right - left) * value / max_x
    def y(value: float) -> float:
        return bottom - (bottom - top) * (value + extent) / (2.0 * extent)
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<style>text{font-family:ui-monospace,Consolas,monospace;fill:#172033}.title{font-size:22px;font-weight:700}.small{font-size:13px}.grid{stroke:#d7dce5}.zero{stroke:#667085}.line{fill:none;stroke-width:2}.trendline{stroke-width:3;stroke-dasharray:10 5}.missing{fill:#b42318;font-weight:700}</style>',
        '<rect width="100%" height="100%" fill="#fff"/>',
        '<text x="30" y="38" class="title">NO1–NO4 DPLL convergence from SD residual captures</text>',
        '<text x="30" y="64" class="small">bias-corrected local residuals; follower command-application evidence is excluded</text>',
        f'<rect x="{left}" y="{top}" width="{right-left}" height="{bottom-top}" fill="#fbfcfe" stroke="#c8ced8"/>',
    ]
    for value in (-extent, -lock_threshold_ns, 0, lock_threshold_ns, extent):
        yy = y(value)
        chunks.append(f'<line x1="{left}" y1="{yy:.1f}" x2="{right}" y2="{yy:.1f}" class="{"zero" if value == 0 else "grid"}"/>')
        chunks.append(f'<text x="{left-12:.1f}" y="{yy+4:.1f}" text-anchor="end" class="small">{value:.0f}</text>')
    for name in boards:
        points = series[name]
        residual_points = [point for point in points if point.residual_valid]
        polyline = " ".join(
            f"{x(point.elapsed_s):.1f},{y(point.centered_residual_ns):.1f}"
            for point in residual_points)
        chunks.append(f'<polyline points="{polyline}" class="line" stroke="{colors[name]}"/>')
        trend_fit = _linear_fit(
            [point.elapsed_s for point in residual_points],
            [point.centered_residual_ns for point in residual_points])
        if trend_fit is not None:
            slope, intercept = trend_fit
            start = residual_points[0].elapsed_s
            end = residual_points[-1].elapsed_s
            chunks.append(
                f'<line x1="{x(start):.1f}" y1="{y(slope * start + intercept):.1f}" '
                f'x2="{x(end):.1f}" y2="{y(slope * end + intercept):.1f}" '
                f'class="trendline" stroke="{colors[name]}"><title>{escape(name)} '
                f'linear convergence trend, bias-corrected observation only, '
                f'slope={slope:.3f} ns/s</title></line>')
    legend_x = left
    for name in ("NO1", "NO2", "NO3", "NO4"):
        analysis = analyses.get(name)
        count = int(analysis.get("sample_count", 0)) if analysis else 0
        confidence = str(analysis.get("analysis_confidence", "missing")) if analysis else "missing"
        color = colors[name]
        if analysis and analysis.get("analysis_kind") == "follower_validated_command_apply":
            count = int(analysis.get("accepted_command_sample_count", 0))
            confidence = "follower_apply_not_local_pi"
        elif analysis and analysis.get("analysis_kind") == "follower_local_evidence":
            count = int(analysis.get("local_evidence_sample_count", 0))
            confidence = "follower_observation_not_local_pi"
        chunks.extend([
            f'<line x1="{legend_x:.1f}" y1="630" x2="{legend_x+34:.1f}" y2="630" stroke="{color}" stroke-width="3"/>',
            f'<text x="{legend_x+42:.1f}" y="635" class="small">{name} samples={count} confidence={escape(confidence)}</text>',
        ])
        legend_x += 340
    chunks.extend([
        '<line x1="110" y1="660" x2="145" y2="660" stroke="#475467" stroke-width="3" stroke-dasharray="10 5"/>',
        '<text x="152" y="665" class="small">dashed: linear convergence trend (bias-corrected observation only; not lock evidence)</text>',
    ])
    missing = [name for name in ("NO1", "NO2", "NO3", "NO4") if name not in series]
    if missing:
        chunks.append(f'<text x="{left}" y="708" class="small missing">MISSING DATA: {escape(", ".join(missing))}</text>')
    chunks.extend([
        f'<text x="{left}" y="{height-14}" class="small">elapsed time (s)</text>',
        f'<text x="30" y="{(top+bottom)/2:.1f}" class="small" transform="rotate(-90 30 {(top+bottom)/2:.1f})" text-anchor="middle">phase residual (ns)</text>',
        '</svg>',
    ])
    return "\n".join(chunks) + "\n"


def write_reports(series: dict[str, list[ResidualPoint]], out_dir: Path, *,
                  input_paths: list[Path], rolling_window: int,
                  lock_threshold_ns: int,
                  mad_multiplier: float) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    analyses: dict[str, dict[str, Any]] = {}
    svg_paths: dict[str, str] = {}
    for board, points in sorted(series.items()):
        analysis = analyze_series(
            points, rolling_window=rolling_window,
            lock_threshold_ns=lock_threshold_ns,
            mad_multiplier=mad_multiplier)
        analyses[board] = analysis
        svg_path = out_dir / f"{board.lower()}_dpll_residual.svg"
        svg_path.write_text(render_svg(
            board, points, analysis, lock_threshold_ns=lock_threshold_ns),
            encoding="utf-8")
        svg_paths[board] = str(svg_path)

    with (out_dir / "dpll_residual_samples.csv").open(
            "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow([
            "board", "elapsed_s", "phase_residual_ns", "residual_valid",
            "residual_invalid_reason", "centered_residual_ns",
            "rolling_mean_ns", "rolling_centered_mean_ns", "rolling_rms_ns",
            "rolling_jitter_rms_ns", "frequency_error_ppb", "dco_phase_offset_ns",
            "dco_period_adjust_ppb", "dpll_state", "gate_reject_code",
            "accepted_count", "rejected_count", "dpll_update_seq",
            "capture_kind", "source_slot_id", "control_generation",
            "command_seq", "effective_vdc_time_ns", "observation_delay_ns",
            "observation_delay_generation", "observation_bias_generation",
            "observation_generation_metadata_present",
            "transport_corrected_phase_residual_ns",
            "observation_metadata_valid", "observation_metadata_reason",
            "anomaly_reasons",
        ])
        for board, points in sorted(series.items()):
            for point in points:
                writer.writerow([
                    board, f"{point.elapsed_s:.6f}", point.phase_residual_ns,
                    int(point.residual_valid), point.residual_invalid_reason,
                    point.centered_residual_ns,
                    f"{point.rolling_mean_ns:.3f}",
                    f"{point.rolling_centered_mean_ns:.3f}",
                    f"{point.rolling_rms_ns:.3f}",
                    f"{point.rolling_jitter_rms_ns:.3f}",
                    point.frequency_error_ppb,
                    point.dco_phase_offset_ns, point.dco_period_adjust_ppb,
                    point.dpll_state, point.gate_reject_code,
                    point.accepted_count, point.rejected_count,
                    point.dpll_update_seq, point.capture_kind,
                    point.source_slot_id, point.control_generation,
                    point.command_seq, point.effective_vdc_time_ns,
                    point.observation_delay_ns,
                    point.observation_delay_generation,
                    point.observation_bias_generation,
                    int(point.observation_generation_metadata_present),
                    _transport_corrected_value(point),
                    int(point.observation_metadata_valid),
                    point.observation_metadata_reason,
                    ";".join(point.anomaly_reasons),
                ])
    result = {
        "schema": "HAOFV_DPLL_RESIDUAL_ANALYSIS_V1",
        "inputs": [str(path) for path in input_paths],
        "rolling_window": rolling_window,
        "lock_threshold_ns": lock_threshold_ns,
        "mad_multiplier": mad_multiplier,
        "nodes": analyses,
        "svg": svg_paths,
        "combined_svg": str(out_dir / "no1_no4_dpll_convergence.svg"),
        "full_rate_warning": (
            "SCPI snapshots can decimate multiple DPLL updates; use the "
            "reported confidence and updates-per-snapshot before transfer-"
            "function fitting."),
    }
    (out_dir / "no1_no4_dpll_convergence.svg").write_text(
        render_combined_svg(series, analyses,
                            lock_threshold_ns=lock_threshold_ns),
        encoding="utf-8")
    (out_dir / "dpll_residual_analysis.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", type=Path, required=True,
                        help="dpll_vdc_monitor samples.json; repeatable")
    parser.add_argument("--board", action="append",
                        help="optional NO1..NO8 filter; repeatable")
    parser.add_argument("--rolling-window", type=int, default=5)
    parser.add_argument("--lock-threshold-ns", type=int,
                        default=DEFAULT_LOCK_THRESHOLD_NS)
    parser.add_argument("--mad-multiplier", type=float, default=6.0)
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "out" / "dpll-residual-analysis")
    return parser.parse_args()


def run(args: argparse.Namespace) -> dict[str, Any]:
    if args.rolling_window <= 0 or args.lock_threshold_ns <= 0:
        raise ValueError("rolling window and lock threshold must be positive")
    if args.mad_multiplier <= 0:
        raise ValueError("MAD multiplier must be positive")
    board_filter = ({name.upper() for name in args.board}
                    if args.board else None)
    if board_filter is not None and any(
            not name.startswith("NO") or not name[2:].isdigit() or
            not 1 <= int(name[2:]) <= 8 for name in board_filter):
        raise ValueError("board filter must use NO1..NO8")
    series = load_monitor_samples(args.input, board_filter)
    if not series:
        raise ValueError("no valid DPLL residual samples found")
    return write_reports(
        series, args.out_dir, input_paths=args.input,
        rolling_window=args.rolling_window,
        lock_threshold_ns=args.lock_threshold_ns,
        mad_multiplier=args.mad_multiplier)


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}")
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
