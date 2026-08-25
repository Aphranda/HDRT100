#!/usr/bin/env python3
"""Merge TRN-01 evidence into diagnostic per-link offset search plans."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.calibration_ring_validate.calibration_marker_train import (
    MAX_OFFSET_SAMPLES,
    MIN_OFFSET_SAMPLES,
    derive_link_offset_candidates,
)


DEFAULT_OFFSET_SWEEP_SAMPLES = tuple(
    range(MIN_OFFSET_SAMPLES, MAX_OFFSET_SAMPLES + 1))


def derive_link_delay_offset_plan(
        link_delays_ns: list[float], measured_delays_ns: list[float],
        sample_period_ns: float = 4.0) -> dict[str, object]:
    """Quantize per-link (measured_delay - link_delay / 2) for PIO loading."""
    if (not link_delays_ns or
            len(link_delays_ns) != len(measured_delays_ns)):
        raise ValueError(
            "link-delay and measured-delay vectors must have equal non-zero length")
    if sample_period_ns <= 0:
        raise ValueError("sample period must be positive")

    links: list[dict[str, object]] = []
    for link, (link_delay_ns, measured_delay_ns) in enumerate(
            zip(link_delays_ns, measured_delays_ns)):
        if link_delay_ns < 0 or measured_delay_ns < 0:
            raise ValueError("link and measured delays must be non-negative")
        base_ns = link_delay_ns / 2.0
        requested_offset_ns = measured_delay_ns - base_ns
        offset_samples = round(requested_offset_ns / sample_period_ns)
        applied_offset_ns = offset_samples * sample_period_ns
        links.append({
            "link": link,
            "link_delay_ns": link_delay_ns,
            "link_base_delay_ns": base_ns,
            "measured_delay_ns": measured_delay_ns,
            "requested_offset_ns": requested_offset_ns,
            "offset_sample_count": offset_samples,
            "applied_offset_ns": applied_offset_ns,
            "quantization_residual_ns": requested_offset_ns - applied_offset_ns,
            "resolved_window_delay_ns": base_ns + applied_offset_ns,
            "pio_loadable": (
                MIN_OFFSET_SAMPLES <= offset_samples <= MAX_OFFSET_SAMPLES),
        })
    return {
        "phase": "TRN-01_PER_LINK_OFFSET_LOAD_PLAN",
        "diagnostic_only": True,
        "base_model": "link_i_base_delay_ns = link_i_delay_ns / 2",
        "offset_model": (
            "offset_sample_count = round((measured_delay_ns - "
            "link_i_base_delay_ns) / sample_period_ns)"),
        "sample_period_ns": sample_period_ns,
        "pio_offset_sample_range": [MIN_OFFSET_SAMPLES, MAX_OFFSET_SAMPLES],
        "links": links,
        "offset_sample_counts_by_link": [
            int(link["offset_sample_count"]) for link in links],
        "all_offsets_pio_loadable": all(
            bool(link["pio_loadable"]) for link in links),
    }


def aggregate_matrix_summary(summary: dict[str, object]) -> dict[str, object]:
    """Summarize a selected N-dimensional matrix without dropping full-matrix metadata."""
    trials = summary.get("trial_results")
    node_ids = summary.get("node_ids_in_loop_order")
    if not isinstance(trials, list) or not isinstance(node_ids, list):
        raise ValueError("matrix summary requires trial_results and node_ids_in_loop_order")

    grouped: dict[tuple[int, int, int], list[dict[str, int]]] = {}
    rows: list[dict[str, object]] = []
    for trial in trials:
        if not isinstance(trial, dict):
            raise ValueError("matrix trial must be an object")
        offsets = trial.get("offset_sample_counts_by_node")
        nodes = trial.get("nodes")
        if (not isinstance(offsets, list) or len(offsets) != len(node_ids) or
                not isinstance(nodes, list)):
            raise ValueError("matrix trial node/offset dimensions are inconsistent")
        row_nodes: list[dict[str, object]] = []
        for node in nodes:
            if not isinstance(node, dict) or not isinstance(node.get("incoming_link"), dict):
                raise ValueError("matrix trial node has no incoming_link")
            incoming = node["incoming_link"]
            source = int(incoming["source_node"])
            destination = int(incoming["destination_node"])
            capture_offset = int(offsets[destination])
            evidence = {
                "state": int(node["state"]),
                "correlation_reject_reason": int(node["correlation_reject_reason"]),
                "best_lag_sample": int(node["best_lag_sample"]),
                "best_distance": int(node["best_distance"]),
                "polarity": int(node["polarity"]),
            }
            grouped.setdefault(
                (source, destination, capture_offset), []).append(evidence)
            row_nodes.append({
                "node": int(node["node"]),
                "incoming_link": {
                    "source_node": source,
                    "destination_node": destination,
                },
                **evidence,
            })
        rows.append({
            "row_id": int(trial["row_id"]),
            "epoch": int(trial["epoch"]),
            "offset_sample_counts_by_node": [int(value) for value in offsets],
            "accepted_nodes": [int(value) for value in trial.get("accepted_nodes", [])],
            "passed": bool(trial.get("passed", False)),
            "capture_file_count": len(trial.get("capture_files", [])),
            "nodes": row_nodes,
        })

    links: list[dict[str, object]] = []
    for (source, destination, capture_offset), evidence in sorted(grouped.items()):
        distances = [row["best_distance"] for row in evidence]
        lags = [row["best_lag_sample"] for row in evidence]
        reject_histogram: dict[str, int] = {}
        for row in evidence:
            reason = str(row["correlation_reject_reason"])
            reject_histogram[reason] = reject_histogram.get(reason, 0) + 1
        links.append({
            "source_node": source,
            "destination_node": destination,
            "destination_node_capture_offset_sample_count": capture_offset,
            "destination_node_capture_offset_ns": capture_offset * 4,
            "observation_count": len(evidence),
            "accepted_count": sum(row["state"] == 3 and
                                  row["correlation_reject_reason"] == 0
                                  for row in evidence),
            "best_distance_min": min(distances),
            "best_distance_max": max(distances),
            "best_distance_mean": statistics.fmean(distances),
            "best_lag_sample_min": min(lags),
            "best_lag_sample_max": max(lags),
            "reject_reason_histogram": reject_histogram,
        })

    return {
        "phase": "TRN-01_OFFSET_MATRIX_AGGREGATE",
        "diagnostic_only": True,
        "node_ids_in_loop_order": node_ids,
        "full_matrix_row_count": int(summary.get("full_matrix_row_count", 0)),
        "selected_row_count": int(summary.get("selected_row_count", len(rows))),
        "selected_row_ids": [int(value) for value in summary.get("selected_row_ids", [])],
        "selection_filters_by_node": summary.get("selection_filters_by_node", {}),
        "passed_row_ids": [int(value) for value in summary.get("passed_row_ids", [])],
        "all_capture_files_saved": all(
            row["capture_file_count"] == len(node_ids) for row in rows),
        "rows": rows,
        "incoming_link_effects_by_destination_node_capture_offset": links,
    }


def aggregate_summaries(summaries: list[dict[str, object]]) -> dict[str, object]:
    if not summaries:
        raise ValueError("at least one summary is required")
    board_orders = [list(summary.get(
                        "node_ids_in_loop_order",
                        summary.get("board_ids_in_physical_order", [])))
                    for summary in summaries]
    board_order = board_orders[0]
    if len(board_order) < 2 or any(order != board_order for order in board_orders):
        raise ValueError("all summaries must have the same physical board order")

    expected = {(node, (node + 1) % len(board_order))
                for node in range(len(board_order))}
    grouped: dict[tuple[int, int], list[dict[str, object]]] = {
        link: [] for link in expected
    }
    for summary_index, summary in enumerate(summaries):
        records = summary.get("records")
        if not isinstance(records, list):
            raise ValueError(f"summary {summary_index} has no records list")
        for candidate in derive_link_offset_candidates(records):
            link = (int(candidate["source_node"]),
                    int(candidate["destination_node"]))
            if link in grouped:
                grouped[link].append({
                    **candidate,
                    "reference_node": summary.get("reference_node"),
                    "epoch": summary.get("epoch"),
                    "generation": summary.get("generation"),
                    "summary_index": summary_index,
                })

    links: list[dict[str, object]] = []
    for source_node, destination_node in sorted(expected):
        evidence = grouped[(source_node, destination_node)]
        accepted = [row for row in evidence if row["correlation_accepted"]]
        rejected = [row for row in evidence if not row["correlation_accepted"]]
        offsets = [int(row["offset_ns"]) for row in accepted]
        anchors = [int(row["sample_anchor_after_marker_ns"])
                   for row in accepted]
        bases = {int(row["base_half_chip_ns"]) for row in accepted}
        ticks = {int(row["tick_resolution_ns"]) for row in accepted}
        reject_histogram: dict[str, int] = {}
        for row in rejected:
            name = str(row["correlation_reject_name"])
            reject_histogram[name] = reject_histogram.get(name, 0) + 1
        consistent = bool(accepted) and len(bases) == 1 and len(ticks) == 1
        links.append({
            "source_node": source_node,
            "destination_node": destination_node,
            "source_board_id": board_order[source_node],
            "destination_board_id": board_order[destination_node],
            "trial_evidence_count": len(evidence),
            "accepted_evidence_count": len(accepted),
            "rejected_evidence_count": len(rejected),
            "reject_histogram": reject_histogram,
            "base_half_chip_ns": next(iter(bases)) if len(bases) == 1 else None,
            "tick_resolution_ns": next(iter(ticks)) if len(ticks) == 1 else None,
            "offset_min_ns": min(offsets) if offsets else None,
            "offset_max_ns": max(offsets) if offsets else None,
            "offset_mean_ns": statistics.fmean(offsets) if offsets else None,
            "sample_anchor_min_ns": min(anchors) if anchors else None,
            "sample_anchor_max_ns": max(anchors) if anchors else None,
            "consistent": consistent,
            "diagnostic_only": True,
            "accepted_evidence": accepted,
            "rejected_evidence": rejected,
        })
    return {
        "phase": "TRN-02_OFFSET_CANDIDATE",
        "diagnostic_only": True,
        "offset_model": "marker_capture + base_half_chip + per_link_offset",
        "board_ids_in_physical_order": board_order,
        "summary_count": len(summaries),
        "complete_link_count": sum(bool(link["consistent"]) for link in links),
        "expected_link_count": len(expected),
        "passed": all(bool(link["consistent"]) for link in links),
        "links": links,
    }


def aggregate_pair_summaries(
        summaries: list[dict[str, object]],
        ring_board_order: list[str]) -> dict[str, object]:
    """Map two-board directed trials back onto one physical ring.

    A rejected correlation lag remains evidence only.  It must never become an
    offset candidate; the next trial instead receives an explicit per-link
    sweep around the codebook half-chip baseline.
    """
    if not summaries:
        raise ValueError("at least one summary is required")
    if len(ring_board_order) < 2 or len(set(ring_board_order)) != len(ring_board_order):
        raise ValueError("ring board order must contain unique board IDs")

    ring_node = {board_id: node for node, board_id in enumerate(ring_board_order)}
    expected = {
        (ring_board_order[node], ring_board_order[(node + 1) % len(ring_board_order)])
        for node in range(len(ring_board_order))
    }
    grouped: dict[tuple[str, str], list[dict[str, object]]] = {
        link: [] for link in expected
    }

    for summary_index, summary in enumerate(summaries):
        pair_order = list(summary.get(
            "node_ids_in_loop_order",
            summary.get("board_ids_in_physical_order", [])))
        if len(pair_order) != 2:
            raise ValueError(f"summary {summary_index} is not a two-board trial")
        directed_link = (str(pair_order[0]), str(pair_order[1]))
        if directed_link not in grouped:
            raise ValueError(
                f"summary {summary_index} link {directed_link!r} is not adjacent "
                "in the supplied ring order")
        records = summary.get("records")
        if not isinstance(records, list):
            raise ValueError(f"summary {summary_index} has no records list")
        candidates = derive_link_offset_candidates(records)
        destination_candidates = [
            candidate for candidate in candidates
            if int(candidate["source_node"]) == 0
            and int(candidate["destination_node"]) == 1
        ]
        if len(destination_candidates) != 1:
            raise ValueError(
                f"summary {summary_index} must contain exactly one 0->1 candidate")
        candidate = destination_candidates[0]
        grouped[directed_link].append({
            **candidate,
            "source_board_id": directed_link[0],
            "destination_board_id": directed_link[1],
            "source_ring_node": ring_node[directed_link[0]],
            "destination_ring_node": ring_node[directed_link[1]],
            "reference_node": summary.get("reference_node"),
            "epoch": summary.get("epoch"),
            "generation": summary.get("generation"),
            "summary_index": summary_index,
        })

    links: list[dict[str, object]] = []
    for source_node, source_board_id in enumerate(ring_board_order):
        destination_node = (source_node + 1) % len(ring_board_order)
        destination_board_id = ring_board_order[destination_node]
        evidence = grouped[(source_board_id, destination_board_id)]
        accepted = [row for row in evidence if row["correlation_accepted"]]
        rejected = [row for row in evidence if not row["correlation_accepted"]]
        reject_histogram: dict[str, int] = {}
        for row in rejected:
            name = str(row["correlation_reject_name"])
            reject_histogram[name] = reject_histogram.get(name, 0) + 1

        bases = {int(row["base_half_chip_ns"]) for row in evidence}
        ticks = {int(row["tick_resolution_ns"]) for row in evidence}
        base_ns = next(iter(bases)) if len(bases) == 1 else None
        tick_ns = next(iter(ticks)) if len(ticks) == 1 else None
        accepted_offsets = [int(row["offset_sample_count"]) for row in accepted]
        if accepted_offsets:
            center = round(statistics.fmean(accepted_offsets))
            sweep_samples = [center - 1, center, center + 1]
            search_status = "OFFSET_CONFIRMATION_REQUIRED"
        else:
            sweep_samples = list(DEFAULT_OFFSET_SWEEP_SAMPLES)
            search_status = "OFFSET_COARSE_SWEEP_REQUIRED"
        anchor_ns = ([base_ns + sample * tick_ns for sample in sweep_samples]
                     if base_ns is not None and tick_ns is not None else [])

        state_bindings = [{
            "destination_build_id": row.get("destination_build_id"),
            "calibration_generation": row.get("calibration_generation"),
            "topology_generation": row.get("topology_generation"),
            "topology_crc32": row.get("topology_crc32"),
            "profile_crc32": row.get("profile_crc32"),
            "schedule_crc32": row.get("schedule_crc32"),
            "training_state": row.get("training_state"),
        } for row in evidence]
        links.append({
            "source_node": source_node,
            "destination_node": destination_node,
            "source_board_id": source_board_id,
            "destination_board_id": destination_board_id,
            "trial_evidence_count": len(evidence),
            "accepted_evidence_count": len(accepted),
            "rejected_evidence_count": len(rejected),
            "reject_histogram": reject_histogram,
            "base_half_chip_ns": base_ns,
            "tick_resolution_ns": tick_ns,
            "offset_accepted": bool(accepted),
            "accepted_offset_sample_counts": accepted_offsets,
            "search_status": search_status,
            "next_offset_sweep_samples": sweep_samples,
            "next_sample_anchors_after_marker_ns": anchor_ns,
            "failed_correlation_lag_is_not_offset": True,
            "state_bindings": state_bindings,
            "accepted_evidence": accepted,
            "rejected_evidence": rejected,
        })

    return {
        "phase": "TRN-02_DIRECTED_PAIR_OFFSET_SEARCH",
        "diagnostic_only": True,
        "offset_model": "marker_capture + base_half_chip + per_link_offset",
        "offsets_are_independent_per_directed_link_and_node_state": True,
        "failed_trials_are_search_evidence": True,
        "failed_correlation_lag_is_not_offset": True,
        "board_ids_in_physical_order": ring_board_order,
        "summary_count": len(summaries),
        "complete_trial_link_count": sum(bool(link["trial_evidence_count"])
                                         for link in links),
        "accepted_offset_link_count": sum(bool(link["offset_accepted"])
                                          for link in links),
        "expected_link_count": len(ring_board_order),
        "passed": all(bool(link["offset_accepted"]) for link in links),
        "links": links,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", type=Path, action="append")
    parser.add_argument(
        "--ring-board-id", action="append",
        help="physical ring board order; selects directed two-board aggregation")
    parser.add_argument(
        "--link-delay-ns", type=float, action="append",
        help="end-to-end link delay; repeat once per directed link")
    parser.add_argument(
        "--measured-delay-ns", type=float, action="append",
        help="measured node/link delay to compensate; repeat in link order")
    parser.add_argument("--sample-period-ns", type=float, default=4.0)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.link_delay_ns is not None or args.measured_delay_ns is not None:
        if args.summary or args.ring_board_id:
            raise SystemExit(
                "per-link offset planning cannot be combined with summaries")
        result = derive_link_delay_offset_plan(
            list(args.link_delay_ns or []),
            list(args.measured_delay_ns or []),
            args.sample_period_ns)
    else:
        if not args.summary:
            raise SystemExit(
                "provide --summary inputs or per-link delay vectors")
        summaries = [json.loads(path.read_text(encoding="utf-8"))
                     for path in args.summary]
        if (len(summaries) == 1 and
                summaries[0].get("phase") == "TRN-01_FOUR_NODE_OFFSET_MATRIX" and
                not args.ring_board_id):
            result = aggregate_matrix_summary(summaries[0])
        elif args.ring_board_id:
            result = aggregate_pair_summaries(summaries, args.ring_board_id)
        else:
            result = aggregate_summaries(summaries)
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0 if bool(result.get("passed", True)) else 1


if __name__ == "__main__":
    raise SystemExit(main())
