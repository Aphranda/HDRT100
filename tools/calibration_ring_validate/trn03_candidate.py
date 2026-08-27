#!/usr/bin/env python3
"""Build the TRN-03C active candidate from replayable hardware evidence.

The tool never activates calibration.  It accepts only a complete TRN-03
matrix, repeated process-image/SD waveform gates, P3 hardware timestamps and
an accepted per-node endpoint-bias set.  A failed run is retained as rejected
staging evidence.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import statistics
import sys
import time
import zlib
from pathlib import Path
from typing import Any, Sequence

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.calibration_ring_validate.trn03_stage import load_config  # noqa: E402


CANDIDATE_SCHEMA = "HAOFV_TRN03_ACTIVE_CANDIDATE_V1"
LIFECYCLE_SCHEMA = "HAOFV_TRN03_CANDIDATE_LIFECYCLE_V1"
BIAS_SET_SCHEMA = "HAOFV_CALIBRATION_BIAS_SET_V1"
REQUIRED_BIAS_FLAGS = 0x1F
P3_GROUP_CLK_DATA = 0
P3_STATE_COMPLETE = 2
P3_ROLE_INITIATOR = 1
P3_ROLE_RESPONDER = 2
P3_REQUIRED_FLAGS = 0x0F
P3_INITIATOR_EDGE_MASK = 0x09
P3_RESPONDER_EDGE_MASK = 0x06


def _integer(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer")
    return value


def _indexed(items: object, field: str, count: int,
             label: str) -> dict[int, dict[str, Any]]:
    if not isinstance(items, list) or len(items) != count:
        raise ValueError(f"{label} must contain {count} entries")
    result: dict[int, dict[str, Any]] = {}
    for item in items:
        if not isinstance(item, dict):
            raise ValueError(f"{label} entry must be an object")
        index = _integer(item.get(field), f"{label}.{field}")
        if index in result or not 0 <= index < count:
            raise ValueError(f"{label} indices must cover [0, {count})")
        result[index] = item
    if sorted(result) != list(range(count)):
        raise ValueError(f"{label} indices must cover [0, {count})")
    return result


def _selected_row(config: dict[str, Any],
                  offset_row_id: int | None = None) -> dict[str, Any]:
    matrix = config.get("offset_matrix")
    if not isinstance(matrix, dict):
        raise ValueError("offset_matrix is missing")
    active_row = (_integer(matrix.get("active_row_id"), "active_row_id")
                  if offset_row_id is None else int(offset_row_id))
    rows = matrix.get("rows")
    if not isinstance(rows, list):
        raise ValueError("offset rows are missing")
    matches = [row for row in rows if isinstance(row, dict) and
               row.get("row_id") == active_row]
    if len(matches) != 1:
        raise ValueError("active offset row is unavailable")
    return matches[0]


def replay_matrix(config: dict[str, Any], offset_row_id: int | None = None
                  ) -> tuple[list[dict[str, Any]], list[str]]:
    count = _integer(config.get("node_count"), "node_count")
    links = _indexed(config.get("links"), "link_index", count, "links")
    row = _selected_row(config, offset_row_id)
    failures: list[str] = []
    replay: list[dict[str, Any]] = []
    for index in range(count):
        link = links[index]
        components = [
            _integer(link.get(field), f"link{index}.{field}")
            for field in (
                "marker_to_data_cycles", "forward_residence_cycles",
                "rx_arm_lead_cycles", "codeword_cycles", "guard_cycles",
                "loop_delay_cycles")]
        replayed_budget = sum(components)
        staged_budget = _integer(
            link.get("link_budget_cycles"), f"link{index}.link_budget_cycles")
        if replayed_budget != staged_budget:
            failures.append(f"link{index}:cycle_budget")

        sample_ns = _integer(link.get("sample_period_ns"),
                             f"link{index}.sample_period_ns")
        base_ns = _integer(link.get("link_base_delay_ns"),
                           f"link{index}.link_base_delay_ns")
        destination_fields = (
            ("marker", "marker_destination_node"),
            ("sck", "marker_destination_node"),
            ("data", "data_destination_node"),
        )
        phase_replay: dict[str, int] = {}
        for phase, destination_field in destination_fields:
            destination = _integer(link.get(destination_field),
                                   f"link{index}.{destination_field}")
            offsets = row.get(f"{phase}_offset_sample_counts_by_node")
            if not isinstance(offsets, list) or len(offsets) != count:
                raise ValueError(f"{phase} offset row dimensions are invalid")
            offset = _integer(offsets[destination],
                              f"row.{phase}.node{destination}")
            phase_ns = base_ns + offset * sample_ns
            if phase_ns < 0:
                failures.append(f"link{index}:{phase}_negative")
                replayed = -1
            else:
                replayed = (phase_ns + sample_ns // 2) // sample_ns
            phase_replay[phase] = replayed
            if replayed != _integer(
                    link.get(f"{phase}_phase_delay_cycles"),
                    f"link{index}.{phase}_phase_delay_cycles"):
                failures.append(f"link{index}:{phase}_phase")
        replay.append({
            "link_index": index,
            "staged_link_budget_cycles": staged_budget,
            "replayed_link_budget_cycles": replayed_budget,
            "phase_delay_cycles": phase_replay,
        })
    return replay, failures


def validate_closed_loops(
        summaries: Sequence[dict[str, Any]], config: dict[str, Any],
        minimum_repeats: int, offset_row_id: int | None = None
        ) -> tuple[list[str], list[int], list[str]]:
    failures: list[str] = []
    rtt_values: list[int] = []
    board_order: list[str] = []
    matrix = config.get("offset_matrix")
    if not isinstance(matrix, dict):
        raise ValueError("offset_matrix must be an object")
    expected_row_id = (_integer(matrix.get("active_row_id"),
                                "offset_matrix.active_row_id")
                       if offset_row_id is None else int(offset_row_id))
    expected_row = _selected_row(config, expected_row_id)
    if len(summaries) < minimum_repeats:
        failures.append("closed_loop_repeat_count")
    for repeat, summary in enumerate(summaries):
        prefix = f"closed_loop{repeat}"
        current_order = summary.get("board_ids_in_physical_node_order")
        if not isinstance(current_order, list):
            failures.append(prefix + ":node_order")
            continue
        current_order = [str(value) for value in current_order]
        if repeat == 0:
            board_order = current_order
        elif current_order != board_order:
            failures.append(prefix + ":node_order_mismatch")
        if not bool(summary.get("passed")):
            failures.append(prefix + ":gate")
        if summary.get("stage") != "process-image":
            failures.append(prefix + ":stage")
        if int(summary.get("offset_row_id", -1)) != expected_row_id:
            failures.append(prefix + ":offset_row_id")
        if summary.get("offset_row") != expected_row:
            failures.append(prefix + ":offset_row")
        if _integer(summary.get("calibration_generation"),
                    prefix + ".generation") != _integer(
                        config.get("calibration_generation"),
                        "config.calibration_generation"):
            failures.append(prefix + ":generation")
        capture = summary.get("ring_capture")
        analysis = summary.get("ring_analysis")
        if not isinstance(capture, dict) or not bool(
                capture.get("capture_completed")):
            failures.append(prefix + ":sd_capture")
        if not isinstance(analysis, dict) or not bool(analysis.get("passed")):
            failures.append(prefix + ":waveform")
        nodes = summary.get("nodes")
        if not isinstance(nodes, dict) or len(nodes) != len(current_order):
            failures.append(prefix + ":nodes")
            continue
        reference = next((node for node in nodes.values()
                          if isinstance(node, dict) and
                          int(node.get("node_index", -1)) == 0), None)
        runtime = reference.get("runtime_after") if isinstance(reference, dict) else None
        rtt = (int(runtime.get("ring_feedback_round_trip_ns", 0))
               if isinstance(runtime, dict) else 0)
        if rtt <= 0:
            failures.append(prefix + ":loop_rtt")
        else:
            rtt_values.append(rtt)
    return failures, rtt_values, board_order


def validate_bias_set(
        bias_set: dict[str, Any], board_order: Sequence[str]
        ) -> tuple[dict[int, dict[str, Any]], list[str]]:
    failures: list[str] = []
    if bias_set.get("schema") != BIAS_SET_SCHEMA:
        return {}, ["bias_schema"]
    if not bool(bias_set.get("passed")):
        failures.append("bias_set_gate")
    if bias_set.get("board_ids_in_physical_node_order") != list(board_order):
        failures.append("bias_node_order")
    try:
        nodes = _indexed(bias_set.get("nodes"), "node", len(board_order),
                         "bias nodes")
    except ValueError as exc:
        return {}, failures + [str(exc)]
    generations: set[int] = set()
    for node, snapshot in nodes.items():
        generation = int(snapshot.get("generation", 0))
        generations.add(generation)
        if (not bool(snapshot.get("valid")) or generation <= 0 or
                (int(snapshot.get("flags", 0)) & REQUIRED_BIAS_FLAGS) !=
                REQUIRED_BIAS_FLAGS or
                int(snapshot.get("sample_count", 0)) <= 0 or
                int(snapshot.get("accepted_count", 0)) !=
                int(snapshot.get("sample_count", -1)) or
                int(snapshot.get("table_crc32", 0)) == 0):
            failures.append(f"node{node}:bias")
    if len(generations) != 1 or 0 in generations:
        failures.append("bias_generation")
    return nodes, failures


def validate_paths(
        p3: dict[str, Any], config: dict[str, Any],
        board_order: Sequence[str], bias_nodes: dict[int, dict[str, Any]],
        minimum_repeats: int, maximum_residual_ns: float
        ) -> tuple[list[dict[str, Any]], list[str]]:
    failures: list[str] = []
    count = len(board_order)
    links = _indexed(config.get("links"), "link_index", count, "links")
    if p3.get("board_ids_in_physical_order") != list(board_order):
        failures.append("p3_node_order")
    if not bool(p3.get("passed")):
        failures.append("p3_gate")
    frequency_hz = _integer(config.get("baud_hz"), "config.baud_hz")
    trials = p3.get("trials")
    if not isinstance(trials, list):
        return [], failures + ["p3_trials"]
    results: list[dict[str, Any]] = []
    for index in range(count):
        source = board_order[index]
        destination = board_order[(index + 1) % count]
        matching = [trial for trial in trials if isinstance(trial, dict) and
                    trial.get("source") == source and
                    trial.get("destination") == destination and
                    int(trial.get("frequency_hz", 0)) == frequency_hz and
                    int(trial.get("signal_group", -1)) == P3_GROUP_CLK_DATA]
        accepted: list[dict[str, Any]] = []
        for repeat, trial in enumerate(matching):
            initiator = trial.get("initiator")
            responder = trial.get("responder")
            hardware_ok = (
                isinstance(initiator, dict) and
                isinstance(responder, dict) and
                int(initiator.get("state", -1)) == P3_STATE_COMPLETE and
                int(responder.get("state", -1)) == P3_STATE_COMPLETE and
                int(initiator.get("role", -1)) == P3_ROLE_INITIATOR and
                int(responder.get("role", -1)) == P3_ROLE_RESPONDER and
                (int(initiator.get("flags", 0)) & P3_REQUIRED_FLAGS) ==
                P3_REQUIRED_FLAGS and
                (int(responder.get("flags", 0)) & P3_REQUIRED_FLAGS) ==
                P3_REQUIRED_FLAGS and
                (int(initiator.get("edge_mask", 0)) &
                 P3_INITIATOR_EDGE_MASK) == P3_INITIATOR_EDGE_MASK and
                (int(responder.get("edge_mask", 0)) &
                 P3_RESPONDER_EDGE_MASK) == P3_RESPONDER_EDGE_MASK and
                int(initiator.get("result_valid", 0)) == 1 and
                int(responder.get("result_valid", 0)) == 1 and
                int(initiator.get("dma_overrun_count", -1)) == 0 and
                int(responder.get("dma_overrun_count", -1)) == 0 and
                int(initiator.get("pio_stall_count", -1)) == 0 and
                int(responder.get("pio_stall_count", -1)) == 0 and
                int(initiator.get("epoch", -1)) ==
                int(responder.get("epoch", -2)) ==
                int(trial.get("epoch", -3)))
            if not hardware_ok:
                failures.append(f"link{index}:path_hardware_repeat{repeat}")
            elif bool(trial.get("passed")):
                accepted.append(trial)
        if matching and len(accepted) != len(matching):
            failures.append(f"link{index}:path_repeat_gate")
        if len(accepted) < minimum_repeats:
            failures.append(f"link{index}:path_repeats")
            continue
        source_bias = int(bias_nodes.get(index, {}).get("mean_bias_ns", 0))
        destination_bias = int(
            bias_nodes.get((index + 1) % count, {}).get("mean_bias_ns", 0))
        endpoint_bias = source_bias + destination_bias
        corrected_path_sums = [float(trial["path_sum_ns"]) - endpoint_bias
                               for trial in accepted]
        if min(corrected_path_sums) < 0:
            failures.append(f"link{index}:negative_corrected_path")
            continue
        delays = [value / 2.0 for value in corrected_path_sums]
        delay_ns = statistics.median(delays)
        base_delay_ns = int(links[index]["link_base_delay_ns"])
        residual_ns = delay_ns - 2.0 * base_delay_ns
        if abs(residual_ns) > maximum_residual_ns:
            failures.append(f"link{index}:path_base_residual")
        results.append({
            "link_index": index,
            "source_node": index,
            "destination_node": (index + 1) % count,
            "source_board_id": source,
            "destination_board_id": destination,
            "sample_count": len(accepted),
            "accepted_count": len(accepted),
            "raw_path_sum_ns": [float(trial["path_sum_ns"])
                                for trial in accepted],
            "endpoint_bias_ns": endpoint_bias,
            "corrected_path_sum_ns": corrected_path_sums,
            "delay_ns": delay_ns,
            "jitter_span_ns": max(delays) - min(delays),
            "link_base_delay_ns": base_delay_ns,
            "path_base_residual_ns": residual_ns,
            "forward_residence_cycles":
                int(links[index]["forward_residence_cycles"]),
            "loop_delay_cycles": int(links[index]["loop_delay_cycles"]),
            "link_budget_cycles": int(links[index]["link_budget_cycles"]),
        })
    return results, failures


def build_candidate(
        config: dict[str, Any], closed_loops: Sequence[dict[str, Any]],
        p3: dict[str, Any], bias_set: dict[str, Any], *,
        minimum_closed_loop_repeats: int = 2,
        minimum_path_repeats: int = 3,
        maximum_path_base_residual_ns: float = 4.0,
        evidence_ages_seconds: Sequence[float] = (),
        maximum_evidence_age_seconds: float = 3600.0,
        offset_row_id: int | None = None
        ) -> dict[str, Any]:
    failures: list[str] = []
    replay, replay_failures = replay_matrix(config, offset_row_id)
    failures.extend(replay_failures)
    loop_failures, rtt_values, board_order = validate_closed_loops(
        closed_loops, config, minimum_closed_loop_repeats, offset_row_id)
    failures.extend(loop_failures)
    bias_nodes, bias_failures = validate_bias_set(bias_set, board_order)
    failures.extend(bias_failures)
    paths, path_failures = validate_paths(
        p3, config, board_order, bias_nodes, minimum_path_repeats,
        maximum_path_base_residual_ns)
    failures.extend(path_failures)
    ages = [float(age) for age in evidence_ages_seconds]
    if any(age < 0 or age > maximum_evidence_age_seconds for age in ages):
        failures.append("freshness")
    result: dict[str, Any] = {
        "schema": CANDIDATE_SCHEMA,
        "state": "active_candidate" if not failures else "rejected_staging",
        "active": False,
        "passed": not failures,
        "gate_failures": sorted(set(failures)),
        "node_count": int(config["node_count"]),
        "board_ids_in_physical_node_order": board_order,
        "calibration_generation": int(config["calibration_generation"]),
        "topology_generation": int(config["topology_generation"]),
        "topology_crc32": int(config["topology_crc32"]),
        "profile_crc32": int(config["profile_crc32"]),
        "schedule_crc32": int(config["schedule_crc32"]),
        "offset_row_id": (int(config["offset_matrix"]["active_row_id"])
                          if offset_row_id is None else int(offset_row_id)),
        "source_active_offset_row_id":
            int(config["offset_matrix"]["active_row_id"]),
        "matrix_replay": replay,
        "closed_loop_repeat_count": len(closed_loops),
        "loop_round_trip_ns": rtt_values,
        "path_links": paths,
        "bias_generation": (int(next(iter(bias_nodes.values()))[
            "generation"]) if bias_nodes else 0),
        "freshness": {
            "evidence_ages_seconds": ages,
            "maximum_evidence_age_seconds": maximum_evidence_age_seconds,
        },
        "thresholds": {
            "minimum_closed_loop_repeats": minimum_closed_loop_repeats,
            "minimum_path_repeats": minimum_path_repeats,
            "maximum_path_base_residual_ns":
                maximum_path_base_residual_ns,
        },
    }
    encoded = json.dumps(result, sort_keys=True, separators=(",", ":")).encode()
    result["candidate_crc32"] = zlib.crc32(encoded) & 0xFFFFFFFF
    return result


def refresh_candidate_crc32(candidate: dict[str, Any]) -> int:
    encoded_value = dict(candidate)
    encoded_value.pop("candidate_crc32", None)
    encoded = json.dumps(
        encoded_value, sort_keys=True, separators=(",", ":")).encode()
    candidate["candidate_crc32"] = zlib.crc32(encoded) & 0xFFFFFFFF
    return int(candidate["candidate_crc32"])


def candidate_crc32_valid(candidate: dict[str, Any]) -> bool:
    """Verify the immutable evidence checksum before a lifecycle transition."""
    if not isinstance(candidate, dict) or not isinstance(
            candidate.get("candidate_crc32"), int):
        return False
    expected = int(candidate["candidate_crc32"])
    checked = copy.deepcopy(candidate)
    return refresh_candidate_crc32(checked) == expected


def _validate_lifecycle_candidate(candidate: dict[str, Any]) -> None:
    if (candidate.get("schema") != CANDIDATE_SCHEMA or
            not bool(candidate.get("passed")) or
            candidate.get("state") not in ("active_candidate", "staged") or
            bool(candidate.get("active")) or
            not candidate_crc32_valid(candidate)):
        raise ValueError("candidate is not a valid inactive package")


def stage_candidate(candidate: dict[str, Any]) -> dict[str, Any]:
    """Freeze a validated evidence package as an inactive staged candidate."""
    _validate_lifecycle_candidate(candidate)
    staged = copy.deepcopy(candidate)
    staged["state"] = "staged"
    staged["active"] = False
    refresh_candidate_crc32(staged)
    return staged


def activate_candidate(
        candidate: dict[str, Any],
        current_active: dict[str, Any] | None = None
        ) -> tuple[dict[str, Any], dict[str, Any] | None]:
    """Promote a validated candidate and return (active, rollbackable).

    Activation is a pure host-side state transition.  The board owner must
    still enforce its current topology/profile/schedule gate before applying
    the returned active snapshot.
    """
    _validate_lifecycle_candidate(candidate)
    if current_active is not None:
        if (current_active.get("schema") != LIFECYCLE_SCHEMA or
                not bool(current_active.get("active")) or
                not candidate_crc32_valid(current_active)):
            raise ValueError("current active package is invalid")
        if int(candidate["calibration_generation"]) <= int(
                current_active["calibration_generation"]):
            raise ValueError("candidate generation is not newer than active")
    active = copy.deepcopy(candidate)
    active["state"] = "active"
    active["active"] = True
    active["source_state"] = candidate.get("state")
    refresh_candidate_crc32(active)
    rollback = copy.deepcopy(current_active) if current_active is not None else None
    if rollback is not None:
        rollback["state"] = "rollbackable"
        rollback["active"] = False
        refresh_candidate_crc32(rollback)
    active["schema"] = LIFECYCLE_SCHEMA
    refresh_candidate_crc32(active)
    return active, rollback


def rollback_candidate(
        current_active: dict[str, Any],
        rollbackable: dict[str, Any]
        ) -> tuple[dict[str, Any], dict[str, Any]]:
    """Restore the previous package and retain the displaced active package."""
    if (current_active.get("schema") != LIFECYCLE_SCHEMA or
            not bool(current_active.get("active")) or
            not candidate_crc32_valid(current_active) or
            rollbackable.get("schema") != LIFECYCLE_SCHEMA or
            bool(rollbackable.get("active")) or
            rollbackable.get("state") != "rollbackable" or
            not candidate_crc32_valid(rollbackable)):
        raise ValueError("active/rollbackable package is invalid")
    restored = copy.deepcopy(rollbackable)
    restored["state"] = "active"
    restored["active"] = True
    displaced = copy.deepcopy(current_active)
    displaced["state"] = "rollbackable"
    displaced["active"] = False
    refresh_candidate_crc32(restored)
    refresh_candidate_crc32(displaced)
    return restored, displaced


def _load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def _source(path: Path, *, freshness_checked: bool) -> dict[str, Any]:
    data = path.read_bytes()
    return {
        "path": str(path),
        "sha256": hashlib.sha256(data).hexdigest(),
        "age_seconds": max(0.0, time.time() - path.stat().st_mtime),
        "freshness_checked": freshness_checked,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--closed-loop", type=Path, action="append",
                        required=True)
    parser.add_argument("--p3", type=Path, required=True)
    parser.add_argument("--bias-set", type=Path, required=True)
    parser.add_argument("--minimum-closed-loop-repeats", type=int, default=2)
    parser.add_argument("--minimum-path-repeats", type=int, default=3)
    parser.add_argument("--maximum-path-base-residual-ns", type=float,
                        default=4.0)
    parser.add_argument("--maximum-evidence-age-seconds", type=float,
                        default=3600.0)
    parser.add_argument(
        "--offset-row-id", type=int,
        help="tested full-matrix row to promote as the active candidate")
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if (args.minimum_closed_loop_repeats < 2 or
                args.minimum_path_repeats < 2 or
                args.maximum_path_base_residual_ns < 0 or
                args.maximum_evidence_age_seconds <= 0):
            raise ValueError("candidate thresholds are outside allowed range")
        # load_config performs the same matrix gate used by board staging.
        resolved_config = load_config(args.config, args.offset_row_id)
        config = _load(args.config)
        # trn03_stage resolves the selected row into the per-link phase
        # fields.  Reuse that canonical replay rather than the source
        # matrix's previously selected row, while retaining the host-side
        # physical source/destination mapping that is not sent over SCPI.
        source_links = _indexed(
            config.get("links"), "link_index", int(config["node_count"]),
            "links")
        config["links"] = [
            {**source_links[int(link["link_index"])], **link}
            for link in resolved_config["links"]]
        paths = [args.config, *args.closed_loop, args.p3, args.bias_set]
        sources = [_source(path, freshness_checked=index != 0)
                   for index, path in enumerate(paths)]
        candidate = build_candidate(
            config, [_load(path) for path in args.closed_loop],
            _load(args.p3), _load(args.bias_set),
            minimum_closed_loop_repeats=args.minimum_closed_loop_repeats,
            minimum_path_repeats=args.minimum_path_repeats,
            maximum_path_base_residual_ns=
                args.maximum_path_base_residual_ns,
            evidence_ages_seconds=[source["age_seconds"] for source in sources
                                   if source["freshness_checked"]],
            maximum_evidence_age_seconds=args.maximum_evidence_age_seconds,
            offset_row_id=args.offset_row_id)
        candidate["sources"] = sources
        refresh_candidate_crc32(candidate)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    args.out_dir.mkdir(parents=True, exist_ok=True)
    output = args.out_dir / "active_candidate.json"
    output.write_text(json.dumps(candidate, ensure_ascii=False, indent=2) + "\n",
                      encoding="utf-8")
    print(json.dumps({"passed": candidate["passed"],
                      "state": candidate["state"],
                      "gate_failures": candidate["gate_failures"],
                      "output": str(output)}, ensure_ascii=False))
    return 0 if candidate["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
