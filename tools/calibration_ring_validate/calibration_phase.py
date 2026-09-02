#!/usr/bin/env python3
"""Shared link-base plus Node-offset phase model for calibration tools."""

from __future__ import annotations

import itertools
import statistics
from collections import Counter
from collections.abc import Sequence


MIN_OFFSET_SAMPLES = -10
MAX_OFFSET_SAMPLES = 10
MAX_PIO_DELAY_SAMPLES = 31
# The training request is intentionally narrow, but a final calibrated offset
# may move outside that search range when the path-delay baseline changes.
# The actual executable phase is still constrained by base + offset <= 31.
MIN_CALIBRATED_OFFSET_SAMPLES = -MAX_PIO_DELAY_SAMPLES
MAX_CALIBRATED_OFFSET_SAMPLES = MAX_PIO_DELAY_SAMPLES
MAX_NODES = 8
MAX_U32 = 0xFFFFFFFF
PHASE_TRAINING_SCHEMA = "HAOFV_UNIFIED_PHASE_TRAINING_V1"
OFFSET_MATRIX_SCHEMA = "HAOFV_UNIFIED_OFFSET_MATRIX_V1"
PHASE_TRAINING_STAGES = (
    "independent_pio_origin",
    "raw_pio_capture",
    "sd_raw_evidence",
    "offline_correlation_and_svg",
    "zero_offset_baseline",
    "full_node_offset_matrix",
    "dynamic_pio_load",
    "residual_repeat_gate",
)


def validate_generation(value: int, label: str = "generation") -> int:
    """Validate a calibration identity before encoding it into a uint32 field."""
    generation = int(value)
    if not 1 <= generation <= MAX_U32:
        raise ValueError(f"{label} must be within 1..{MAX_U32}")
    return generation


def link_base_delay_ns(link_delay_ns: int,
                       path_delay_baseline_divisor: int = 2) -> int:
    if link_delay_ns <= 0 or link_delay_ns % 2:
        raise ValueError("link delay must be a positive even number of ns")
    if (isinstance(path_delay_baseline_divisor, bool) or
            path_delay_baseline_divisor <= 0):
        raise ValueError("path delay baseline divisor must be positive")
    base = (link_delay_ns + path_delay_baseline_divisor // 2) \
        // path_delay_baseline_divisor
    if base <= 0:
        raise ValueError("path delay baseline rounds to zero")
    return base


def phase_delay_samples(*, link_base_ns: int, sample_period_ns: int,
                        node_offset_samples: int,
                        max_delay_samples: int =
                        MAX_PIO_DELAY_SAMPLES) -> int:
    if link_base_ns <= 0 or sample_period_ns <= 0:
        raise ValueError("link base and sample period must be positive")
    if not MIN_OFFSET_SAMPLES <= node_offset_samples <= MAX_OFFSET_SAMPLES:
        raise ValueError(
            f"Node offset must be within {MIN_OFFSET_SAMPLES}.."
            f"{MAX_OFFSET_SAMPLES} samples")
    base_samples = (link_base_ns + sample_period_ns // 2) // sample_period_ns
    delay = base_samples + node_offset_samples
    if not 0 <= delay <= max_delay_samples:
        raise ValueError(
            f"base + offset requires {delay} samples, outside PIO delay "
            f"0..{max_delay_samples}")
    return delay


def build_offset_rows(*, node_count: int, values_by_node: Sequence[Sequence[int]],
                      sample_period_ns: int,
                      min_offset_samples: int = MIN_OFFSET_SAMPLES,
                      max_offset_samples: int = MAX_OFFSET_SAMPLES
                      ) -> list[dict[str, object]]:
    """Build the same full Cartesian Node matrix for every trained signal."""
    if not 2 <= node_count <= MAX_NODES:
        raise ValueError(f"node count must be within 2..{MAX_NODES}")
    if (len(values_by_node) != node_count or sample_period_ns <= 0 or
            min_offset_samples > max_offset_samples):
        raise ValueError("matrix dimensions and sample period are invalid")
    normalized: list[tuple[int, ...]] = []
    for values in values_by_node:
        unique = tuple(sorted({int(value) for value in values}))
        if not unique or any(
                not min_offset_samples <= value <= max_offset_samples
                for value in unique):
            raise ValueError("matrix values must be non-empty bounded offsets")
        normalized.append(unique)
    return [{
        "row_id": row_id,
        "offset_sample_counts_by_node": list(offsets),
        "offset_ns_by_node": [offset * sample_period_ns for offset in offsets],
    } for row_id, offsets in enumerate(itertools.product(*normalized))]


def build_observed_offset_matrix(
        *, signal: str, values_by_node: Sequence[Sequence[int]],
        sample_period_ns: int,
        min_offset_samples: int = MIN_OFFSET_SAMPLES,
        max_offset_samples: int = MAX_OFFSET_SAMPLES) -> dict[str, object]:
    """Reduce repeats, then retain the complete Cartesian candidate matrix."""
    normalized_signal = signal.upper()
    if normalized_signal not in {"MARK", "SCK", "DATA"}:
        raise ValueError("signal must be MARK, SCK, or DATA")
    node_count = len(values_by_node)
    missing = [node for node, values in enumerate(values_by_node) if not values]
    if missing:
        return {
            "schema": OFFSET_MATRIX_SCHEMA,
            "signal": normalized_signal,
            "sample_period_ns": sample_period_ns,
            "candidate_values_by_node": [
                sorted({int(value) for value in values})
                for values in values_by_node],
            "full_matrix_row_count": 0,
            "active_row_id": -1,
            "rows": [],
            "missing_nodes": missing,
        }
    selected: list[int] = []
    histograms: list[dict[str, int]] = []
    for raw_values in values_by_node:
        values = [int(value) for value in raw_values]
        counts = Counter(values)
        highest = max(counts.values())
        modes = [value for value, count in counts.items() if count == highest]
        median = statistics.median(values)
        selected.append(min(modes, key=lambda value: (abs(value - median), value)))
        histograms.append({str(key): value for key, value in sorted(counts.items())})
    candidates = [sorted({int(value) for value in values})
                  for values in values_by_node]
    rows = build_offset_rows(
        node_count=node_count, values_by_node=candidates,
        sample_period_ns=sample_period_ns,
        min_offset_samples=min_offset_samples,
        max_offset_samples=max_offset_samples)
    active_row_id = next(
        int(row["row_id"]) for row in rows
        if row["offset_sample_counts_by_node"] == selected)
    return {
        "schema": OFFSET_MATRIX_SCHEMA,
        "signal": normalized_signal,
        "sample_period_ns": sample_period_ns,
        "candidate_values_by_node": candidates,
        "accepted_histogram_by_node": histograms,
        "recommended_offset_sample_counts_by_node": selected,
        "full_matrix_row_count": len(rows),
        "active_row_id": active_row_id,
        "rows": rows,
        "missing_nodes": [],
    }


def build_phase_training_contract(
        *, signal: str, link_delay_ns_by_link: Sequence[int],
        node_offset_samples: Sequence[int], sample_period_ns: int,
        destination_node_by_link: Sequence[int] | None = None,
        capture_origin: str | None = None,
        path_delay_baseline_divisor: int = 2,
        max_delay_samples: int = MAX_PIO_DELAY_SAMPLES) -> dict[str, object]:
    """Create the canonical plan/evidence header shared by all phase training."""
    normalized_signal = signal.upper()
    if normalized_signal not in {"MARK", "SCK", "DATA"}:
        raise ValueError("signal must be MARK, SCK, or DATA")
    node_count = len(node_offset_samples)
    if not 2 <= node_count <= MAX_NODES:
        raise ValueError(f"node count must be within 2..{MAX_NODES}")
    if len(link_delay_ns_by_link) != node_count:
        raise ValueError("one measured delay is required per physical link")
    destinations = (list(destination_node_by_link)
                    if destination_node_by_link is not None else
                    [(link + 1) % node_count for link in range(node_count)])
    if (len(destinations) != node_count or
            sorted(destinations) != list(range(node_count))):
        raise ValueError("directed links must cover every destination Node once")
    bases = [link_base_delay_ns(
        int(delay), path_delay_baseline_divisor)
             for delay in link_delay_ns_by_link]
    links = []
    for link, (base_ns, destination) in enumerate(zip(bases, destinations)):
        offset = int(node_offset_samples[destination])
        links.append({
            "link": link,
            "destination_node": destination,
            "link_delay_ns": int(link_delay_ns_by_link[link]),
            "link_base_delay_ns": base_ns,
            "node_offset_sample_count": offset,
            "effective_phase_delay_samples": phase_delay_samples(
                link_base_ns=base_ns,
                sample_period_ns=sample_period_ns,
                node_offset_samples=offset,
                max_delay_samples=max_delay_samples),
        })
    return {
        "schema": PHASE_TRAINING_SCHEMA,
        "signal": normalized_signal,
        "capture_origin": capture_origin or f"{normalized_signal.lower()}_pio_edge",
        "path_delay_baseline_divisor": path_delay_baseline_divisor,
        "formula": (
            "round((link_delay_ns / path_delay_baseline_divisor) / "
            "sample_period_ns) + node_offset_samples"),
        "stage_order": list(PHASE_TRAINING_STAGES),
        "sample_period_ns": sample_period_ns,
        "node_offset_sample_counts": [int(value) for value in node_offset_samples],
        "links": links,
    }
