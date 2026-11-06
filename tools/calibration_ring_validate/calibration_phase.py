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


def measure_forward_scK(rx_levels: Sequence[int], tx_levels: Sequence[int], *,
                        sample_period_ns: int = 4,
                        bit_period_ns: float | None = None
                        ) -> dict[str, object]:
    """Measure one forwarded SCK period from a two-channel raw capture.

    ``rx_levels`` / ``tx_levels`` are the sampled 0/1 levels of the follower's
    incoming and outgoing SCK on a shared time base (one entry per sample).
    The forwarded edges are paired by polarity, so an inverted probe or a
    capture that lost an edge is rejected instead of silently mis-measured.

    The returned ``rise_delay_ns`` / ``fall_delay_ns`` feed
    :func:`classify_forward_sweep` directly; ``high_ns`` (and ``duty_percent``
    when the bit period is known) shows whether the two edges stayed symmetric.
    """
    if sample_period_ns <= 0:
        raise ValueError("sample period must be positive")
    if len(rx_levels) != len(tx_levels) or len(rx_levels) < 3:
        raise ValueError("both channels need the same capture length")
    for levels in (rx_levels, tx_levels):
        if any(int(level) not in (0, 1) for level in levels):
            raise ValueError("capture levels must be 0 or 1")
    rx_edges = [index for index in range(1, len(rx_levels))
                if int(rx_levels[index]) != int(rx_levels[index - 1])]
    tx_edges = [index for index in range(1, len(tx_levels))
                if int(tx_levels[index]) != int(tx_levels[index - 1])]
    if len(rx_edges) < 2 or len(tx_edges) < 2:
        raise ValueError("capture lost the incoming or forwarded edge")
    if len(rx_edges) != len(tx_edges):
        raise ValueError(
            f"edge count mismatch: rx={len(rx_edges)} tx={len(tx_edges)}")

    def first_edge(levels: Sequence[int], edges: Sequence[int],
                   rising: bool) -> int | None:
        for index in edges:
            if (int(levels[index]) == 1) == rising:
                return index
        return None

    rise_in = first_edge(rx_levels, rx_edges, True)
    fall_in = first_edge(rx_levels, rx_edges, False)
    rise_out = first_edge(tx_levels, tx_edges, True)
    fall_out = first_edge(tx_levels, tx_edges, False)
    if rise_in is None or fall_in is None or rise_out is None or fall_out is None:
        raise ValueError("capture is missing a rising or falling edge")
    if rise_out < rise_in or fall_out < fall_in:
        raise ValueError("forwarded edge precedes its incoming edge")
    rise_delay_ns = (rise_out - rise_in) * sample_period_ns
    fall_delay_ns = (fall_out - fall_in) * sample_period_ns
    if fall_out > rise_out:
        high_ns = (fall_out - rise_out) * sample_period_ns
    else:
        # Capture starts inside the high phase; fall back to the previous
        # falling edge so the width is still the forwarded high pulse.
        previous_fall = [index for index in tx_edges
                         if index < rise_out and int(tx_levels[index]) == 0]
        if not previous_fall:
            raise ValueError("capture does not contain a complete high phase")
        high_ns = (fall_out - previous_fall[-1]) * sample_period_ns
    result: dict[str, object] = {
        "sample_period_ns": sample_period_ns,
        "rise_delay_ns": rise_delay_ns,
        "fall_delay_ns": fall_delay_ns,
        "edge_skew_ns": fall_delay_ns - rise_delay_ns,
        "high_ns": high_ns,
        "rx_edges": len(rx_edges),
        "tx_edges": len(tx_edges),
    }
    if bit_period_ns is not None:
        if bit_period_ns <= 0:
            raise ValueError("bit period must be positive")
        result["duty_percent"] = 100.0 * high_ns / float(bit_period_ns)
    return result


def classify_forward_sweep(points: Sequence[dict[str, object]], *,
                           sample_period_ns: int = 4,
                           half_period_ns: float | None = None
                           ) -> dict[str, object]:
    """Classify a forwarded-SCK target sweep into the admission branch.

    A bench capture raises the staged SCK target one sample at a time and
    records the measured edge-to-edge delay of the forwarded SCK.  While the
    state machine can still return to the opposite-edge WAIT, the output edge
    tracks the target one-for-one; once the target is too large the edge stops
    tracking (it saturates at the local re-arm point).  The largest target that
    still tracks is the physical ceiling, which decides the 10 MHz fix:

    * ``A``: the ceiling is 11 cycles, so the guarded ``+2`` constant is the
      only thing rejecting the calibrated 44 ns row.
    * ``B``: the ceiling is 10 cycles, so the two edges need separate targets.
    * ``C``: the ceiling is below 10 cycles, so the calibrated row cannot be
      made legal and only a retimed row, a lower baud or a PIO redesign helps.
    """
    if sample_period_ns <= 0:
        raise ValueError("sample period must be positive")
    if len(points) < 2:
        raise ValueError("a sweep needs at least two target points")
    ordered = sorted(
        ({"target_cycles": int(point["target_cycles"]),
          "rise_delay_ns": float(point["rise_delay_ns"]),
          "fall_delay_ns": float(point["fall_delay_ns"])}
         for point in points),
        key=lambda point: point["target_cycles"])
    targets = [point["target_cycles"] for point in ordered]
    if len(set(targets)) != len(targets):
        raise ValueError("sweep targets must be unique")
    step_ns = float(sample_period_ns)
    tolerance_ns = step_ns / 2.0
    evidence: list[dict[str, object]] = [
        {**ordered[0], "tracked": None, "rise_step_ns": None,
         "fall_step_ns": None}]
    tracked_targets = [targets[0]]
    tracking = True
    for previous, current in zip(ordered, ordered[1:]):
        rise_step = current["rise_delay_ns"] - previous["rise_delay_ns"]
        fall_step = current["fall_delay_ns"] - previous["fall_delay_ns"]
        tracked = (abs(rise_step - step_ns) <= tolerance_ns and
                   abs(fall_step - step_ns) <= tolerance_ns)
        evidence.append({**current, "tracked": tracked,
                         "rise_step_ns": rise_step,
                         "fall_step_ns": fall_step})
        if tracking and tracked:
            tracked_targets.append(current["target_cycles"])
        else:
            tracking = False
    ceiling = max(tracked_targets)
    if ceiling >= 11:
        branch = "A"
        reason = "calibrated 44 ns row tracks; only the guarded constant rejects it"
    elif ceiling == 10:
        branch = "B"
        reason = "falling edge saturates above 10 cycles; split rise/fall targets"
    else:
        branch = "C"
        reason = "even 10 cycles does not track; retime, lower baud or redesign"
    result: dict[str, object] = {
        "sample_period_ns": sample_period_ns,
        "target_ceiling_cycles": ceiling,
        "branch": branch,
        "reason": reason,
        "tracked_targets": tracked_targets,
        "points": evidence,
    }
    if half_period_ns is not None:
        result["half_period_ns"] = half_period_ns
        result["ceiling_phase_ns"] = ceiling * sample_period_ns
        result["ceiling_margin_ns"] = half_period_ns - ceiling * sample_period_ns
    return result


def classify_offset_window(points: Sequence[dict[str, object]], *,
                           min_width_samples: int = 2
                           ) -> dict[str, object]:
    """Turn an offset sweep into an eye window and a placement verdict.

    ``points`` is one entry per measured offset::

        {"offset_sample_count": int, "accepted_count": int, "repeats": int}

    A point passes only when every repeat was accepted.  The window is the
    largest contiguous run of passing offsets; its width is the margin the
    current calibration never measured.  The verdict is one of:

    * ``no_window``: nothing passed, so the link is not measurable at this baud.
    * ``non_contiguous``: several disjoint passing regions (aliasing or a mixed
      peak) — do not place a sample point on a guess.
    * ``coverage_incomplete``: the run has holes, so its width is a lower bound.
    * ``too_narrow``: width below ``min_width_samples``; the calibration grid is
      coarser than the eye, so centring is not possible.
    * ``usable``: the eye is wide enough; ``chosen_offset_sample_count`` is the
      nearest representable centre and ``chosen_margin_samples`` what is left on
      the tighter side.
    """
    if min_width_samples < 1:
        raise ValueError("minimum window width must be positive")
    parsed: list[tuple[int, int, int]] = []
    for point in points:
        offset = int(point["offset_sample_count"])
        accepted = int(point["accepted_count"])
        repeats = int(point["repeats"])
        if repeats <= 0 or not 0 <= accepted <= repeats:
            raise ValueError(
                f"offset {offset} has an invalid accepted/repeat count")
        parsed.append((offset, accepted, repeats))
    if not parsed:
        raise ValueError("a window needs at least one measured offset")
    if len({offset for offset, _, _ in parsed}) != len(parsed):
        raise ValueError("window offsets must be unique")
    parsed.sort()
    measured = {offset for offset, _, _ in parsed}
    passing = [offset for offset, accepted, repeats in parsed
               if accepted == repeats]
    runs: list[list[int]] = []
    for offset in passing:
        if runs and offset == runs[-1][-1] + 1:
            runs[-1].append(offset)
        else:
            runs.append([offset])
    base: dict[str, object] = {
        "measured_offsets": sorted(measured),
        "passing_offsets": passing,
        "window_runs": runs,
    }
    if not runs:
        return {**base, "verdict": "no_window", "width_samples": 0,
                "eye_left_sample_count": None, "eye_right_sample_count": None,
                "center_sample_count": None, "center_representable": False,
                "chosen_offset_sample_count": None,
                "chosen_margin_samples": None, "coverage_complete": False}
    # Prefer the widest run; tie-break on the run closest to offset 0 so a
    # symmetric eye does not depend on sweep order.
    best = max(runs, key=lambda run: (len(run),
                                      -min(abs(value) for value in run)))
    left, right = best[0], best[-1]
    width = len(best)
    # The window is only proven when the sweep measured a failing neighbour on
    # both sides; otherwise the eye may extend past the swept range.
    failed = measured - set(passing)
    coverage_complete = (left - 1) in failed and (right + 1) in failed
    center = (left + right) / 2.0
    center_representable = width % 2 == 1
    chosen = int(center)
    chosen_margin = min(chosen - left, right - chosen)
    if len(runs) > 1:
        verdict = "non_contiguous"
    elif width < min_width_samples:
        verdict = "too_narrow"
    elif not coverage_complete:
        verdict = "coverage_incomplete"
    else:
        verdict = "usable"
    return {
        **base,
        "verdict": verdict,
        "eye_left_sample_count": left,
        "eye_right_sample_count": right,
        "width_samples": width,
        "center_sample_count": center,
        "center_representable": center_representable,
        "chosen_offset_sample_count": chosen,
        "chosen_margin_samples": chosen_margin,
        "coverage_complete": coverage_complete,
    }


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
