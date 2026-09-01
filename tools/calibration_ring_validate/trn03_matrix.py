#!/usr/bin/env python3
"""Generate a replayable TRN-03 matrix from paired TRN-02 evidence."""

from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path
from typing import Any

try:
    from .trn02_profile_gate import (
        IDENTITY_FIELDS,
        load_summary,
        node_order,
        singleton_identity,
        validate_profile_pair,
    )
    from .trn03_stage import sck_replay_phase_margin
except ImportError:  # Direct execution from this directory.
    from trn02_profile_gate import (  # type: ignore[no-redef]
        IDENTITY_FIELDS,
        load_summary,
        node_order,
        singleton_identity,
        validate_profile_pair,
    )
    from trn03_stage import sck_replay_phase_margin  # type: ignore[no-redef]


MATRIX_SCHEMA = "HAOFV_TRN03_REPLAY_MATRIX_V2"
REQUIRED_EVIDENCE_FLAGS = 0x1F
NORMAL_PIO_PERSONA = 1
NORMAL_PIO_BIT_CYCLES = 6
BOARD_SYS_CLOCK_HZ = 250_000_000
PROFILE_VERSION = 1
PROFILE_TRAIN_CYCLES = 4096
PROFILE_FLAGS = 0
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619
SCK_MATRIX_SCHEMA = "HAOFV_SCK_OFFSET_MATRIX_V2"

# Mirrors s_tdma_operating_profiles for the fixed TRN-02 profile ladder.
# tests/python/test_trn03_matrix.py checks these facts against the C sources.
PROFILE_FACTS = {
    7: {"baud_hz": 10_000_000, "cycle_period_ns": 1_000_000},
    8: {"baud_hz": 25_000_000, "cycle_period_ns": 1_000_000},
    9: {"baud_hz": 30_000_000, "cycle_period_ns": 1_000_000},
}


def ceil_div(numerator: int, denominator: int) -> int:
    if numerator < 0 or denominator <= 0:
        raise ValueError("ceil_div requires non-negative numerator")
    return (numerator + denominator - 1) // denominator


def fnv_u32(value: int, hash_value: int) -> int:
    for shift in range(0, 32, 8):
        hash_value ^= (value >> shift) & 0xFF
        hash_value = (hash_value * FNV_PRIME) & 0xFFFFFFFF
    return hash_value


def profile_crc32(level: int) -> int:
    facts = PROFILE_FACTS.get(level)
    if facts is None:
        raise ValueError(f"unsupported TRN-02 profile level {level}")
    value = FNV_OFFSET
    for field in (
        PROFILE_VERSION,
        level,
        facts["baud_hz"],
        facts["cycle_period_ns"],
        PROFILE_TRAIN_CYCLES,
        PROFILE_FLAGS,
    ):
        value = fnv_u32(field, value)
    return value


def ns_to_pio_cycles(value_ns: int, clkdiv_q16: int) -> int:
    return ceil_div(
        value_ns * BOARD_SYS_CLOCK_HZ * 65536,
        1_000_000_000 * clkdiv_q16,
    )


def pio_facts(level: int) -> dict[str, int]:
    profile = PROFILE_FACTS.get(level)
    if profile is None:
        raise ValueError(f"unsupported TRN-02 profile level {level}")
    baud_hz = profile["baud_hz"]
    instruction_hz = baud_hz * NORMAL_PIO_BIT_CYCLES
    # RP2350 PIO programs a 16.8 divider.  Preserve that actual programmed
    # value in the staging field's 16.16 representation.
    clkdiv_q8 = (
        BOARD_SYS_CLOCK_HZ * 256 + instruction_hz // 2
    ) // instruction_hz
    clkdiv_q16 = clkdiv_q8 << 8
    return {
        "pio_persona": NORMAL_PIO_PERSONA,
        "clkdiv_q16": clkdiv_q16,
        "clk_sys_hz": BOARD_SYS_CLOCK_HZ,
        "instruction_period_ns": ceil_div(
            clkdiv_q16 * 1_000_000_000,
            65536 * BOARD_SYS_CLOCK_HZ),
        "bit_cycles": NORMAL_PIO_BIT_CYCLES,
        "baud_hz": baud_hz,
        "cycle_period_ns": profile["cycle_period_ns"],
    }


def _sck_row_replay_safety(
        offsets_by_node: tuple[int, ...], link_bases_ns: list[int],
        baud_hz: int, sample_period_ns: int,
        reference_node: int = 0) -> tuple[bool, int | None, int, int]:
    """Check one SCK candidate row against every directed-link budget.

    The candidate row remains a raw training result; this helper only decides
    whether the row can be replayed by the selected flight profile.  The
    returned margin is the smallest follower margin, with the reference link
    excluded because it owns the origin rather than a re-arm path.
    """
    if len(offsets_by_node) != len(link_bases_ns):
        return False, -1, len(link_bases_ns), -len(link_bases_ns)
    margins: list[int] = []
    for link_index, base_ns in enumerate(link_bases_ns):
        destination = (link_index + 1) % len(offsets_by_node)
        phase_ns = int(base_ns) + int(offsets_by_node[destination]) * sample_period_ns
        if phase_ns < 0:
            return False, -1, len(link_bases_ns), -len(link_bases_ns)
        phase_cycles = (phase_ns + sample_period_ns // 2) // sample_period_ns
        margin = sck_replay_phase_margin(
            phase_delay_cycles=phase_cycles,
            baud_hz=baud_hz,
            sample_period_ns=sample_period_ns,
            destination_node=destination)
        if margin is None:
            continue
        margins.append(margin)
    minimum = min(margins) if margins else None
    unsafe_count = sum(margin < 0 for margin in margins)
    return (minimum is None or minimum >= 0, minimum, unsafe_count,
            sum(margins))


def _select_sck_replay_row(
        candidates_by_node: list[list[int]], requested: list[int],
        link_bases_ns: list[int], baud_hz: int,
        sample_period_ns: int, *,
        diagnostic_continue: bool = False) -> tuple[list[int], dict[str, Any]]:
    """Select the nearest replay-safe SCK row without dropping candidates."""
    candidate_rows = [tuple(int(value) for value in row)
                      for row in itertools.product(*candidates_by_node)]
    evaluated_rows: list[
        tuple[tuple[int, ...], bool, int | None, int, int]] = []
    for row in candidate_rows:
        safe, margin, unsafe_count, total_margin = _sck_row_replay_safety(
            row, link_bases_ns, baud_hz, sample_period_ns)
        evaluated_rows.append(
            (row, safe, margin, unsafe_count, total_margin))
    safe_rows = [(row, margin) for row, safe, margin, _, _ in evaluated_rows
                 if safe]
    if not safe_rows:
        if not diagnostic_continue:
            raise ValueError(
                "no SCK offset candidate row can satisfy the flight re-arm "
                "budget")
        requested_tuple = tuple(int(value) for value in requested)
        selected, _, selected_margin, selected_unsafe_count, \
            selected_total_margin = min(
            evaluated_rows,
            key=lambda item: (
                -(item[2] if item[2] is not None else 0),
                item[3],
                -item[4],
                sum(abs(value) for value in item[0]),
                sum(abs(value - wanted)
                    for value, wanted in zip(item[0], requested_tuple)),
                item[0]))
        return list(selected), {
            "requested_offset_sample_counts_by_node": list(requested),
            "selected_offset_sample_counts_by_node": list(selected),
            "requested_row_replay_safe": False,
            "selected_row_replay_safe": False,
            "selection_reason": "diagnostic_best_available_unsafe_candidate",
            "selected_min_follower_margin_samples": selected_margin,
            "selected_unsafe_follower_count": selected_unsafe_count,
            "selected_total_follower_margin_samples": selected_total_margin,
            "safe_candidate_row_count": 0,
            "candidate_row_count": len(candidate_rows),
            "diagnostic_continue": True,
            "candidate_replay_evaluation": [
                {"offset_sample_counts_by_node": list(row),
                 "replay_safe": safe,
                 "min_follower_margin_samples": margin,
                 "unsafe_follower_count": unsafe_count,
                 "total_follower_margin_samples": total_margin}
                for row, safe, margin, unsafe_count, total_margin
                in evaluated_rows],
        }
    requested_tuple = tuple(int(value) for value in requested)
    if requested_tuple in {row for row, _ in safe_rows}:
        selected = requested_tuple
        reason = "observed_row_replay_safe"
    else:
        # Prefer the smallest change from the measured recommendation, then
        # the widest remaining follower margin, then lexical order for a
        # stable row ID.  Every rejected row remains in the full matrix.
        selected, selected_margin = min(
            safe_rows,
            key=lambda item: (
                sum(abs(value - wanted)
                    for value, wanted in zip(item[0], requested_tuple)),
                -(item[1] if item[1] is not None else 0),
                item[0]))
        reason = "observed_row_retimed_to_replay_safe_candidate"
    selected_margin = next(margin for row, margin in safe_rows
                           if row == selected)
    return list(selected), {
        "requested_offset_sample_counts_by_node": list(requested),
        "selected_offset_sample_counts_by_node": list(selected),
        "requested_row_replay_safe": requested_tuple in {
            row for row, _ in safe_rows},
        "selected_row_replay_safe": True,
        "selection_reason": reason,
        "selected_min_follower_margin_samples": selected_margin,
        "safe_candidate_row_count": len(safe_rows),
        "candidate_row_count": len(candidate_rows),
        "diagnostic_continue": False,
    }


def indexed(items: object, field: str, count: int, label: str
            ) -> dict[int, dict[str, Any]]:
    if not isinstance(items, list) or len(items) != count:
        raise ValueError(f"{label} must contain exactly {count} entries")
    result: dict[int, dict[str, Any]] = {}
    for item in items:
        if not isinstance(item, dict):
            raise ValueError(f"{label} entry must be an object")
        index = int(item.get(field, -1))
        if index in result or not 0 <= index < count:
            raise ValueError(f"{label} indices must cover [0, {count})")
        result[index] = item
    if sorted(result) != list(range(count)):
        raise ValueError(f"{label} indices must cover [0, {count})")
    return result


def derive_data_refinement_candidates(
        data: dict[str, Any],
        refinements: list[tuple[str, dict[str, Any]]]
        ) -> tuple[dict[int, list[int]], list[dict[str, Any]]]:
    """Derive non-active search rows from accepted selected-link evidence."""
    nodes = node_order(data)
    count = len(nodes)
    base_identity = singleton_identity(data)
    parsed: dict[int, set[int]] = {}
    records: list[dict[str, Any]] = []
    for path, summary in refinements:
        if (not bool(summary.get("passed")) or
                summary.get("phase") != "TRN-02B_SELECTED_LINK_REPEAT"):
            raise ValueError(
                f"DATA refinement evidence is not accepted: {path}")
        if node_order(summary) != nodes:
            raise ValueError(
                f"DATA refinement node order mismatch: {path}")
        selected_links = summary.get("selected_links")
        matrix = summary.get("matrix")
        parameters = summary.get("training_parameters")
        if (not isinstance(selected_links, list) or
                len(selected_links) != 1 or
                not isinstance(matrix, dict) or
                not bool(matrix.get("passed")) or
                matrix.get("identity_failures") or
                matrix.get("gate_failures") or
                not isinstance(parameters, dict)):
            raise ValueError(
                f"DATA refinement selected-link gate failed: {path}")
        link_index = int(selected_links[0])
        links = matrix.get("links")
        offsets = parameters.get("node_data_offset_samples")
        if (not 0 <= link_index < count or
                not isinstance(links, list) or len(links) != 1 or
                int(links[0].get("link", -1)) != link_index or
                not bool(links[0].get("passed")) or
                not isinstance(offsets, list) or len(offsets) != count):
            raise ValueError(
                f"DATA refinement dimensions are invalid: {path}")
        destination_node = int(links[0].get("data_destination_node", -1))
        if not 0 <= destination_node < count:
            raise ValueError(
                f"DATA refinement destination Node is invalid: {path}")
        candidate = int(offsets[destination_node])
        if not -10 <= candidate <= 10:
            raise ValueError(
                f"DATA refinement candidate offset is outside -10..+10: {path}")
        refinement_identity = singleton_identity(summary)
        # Topology generation is a local staging counter and may advance when
        # the selected-link refinement prepares the same physical ring.  The
        # topology CRC and every portable profile identity must remain equal.
        for field in (*IDENTITY_FIELDS, "sample_period_ns"):
            if field == "topology_generation":
                continue
            if int(refinement_identity[field]) != int(base_identity[field]):
                raise ValueError(
                    f"DATA refinement {field} mismatch: {path}")
        parsed.setdefault(destination_node, set()).add(candidate)
        records.append({
            "path": path,
            "link_index": link_index,
            "destination_node": destination_node,
            "configured_offset_sample_count": candidate,
            "repeat_count": int(summary.get("repeats", 0)),
            "accepted_count": int(matrix.get("accepted_count", 0)),
            "calibrated_offset_histogram": links[0].get(
                "calibrated_offset_histogram", {}),
            "topology_generation": int(
                refinement_identity["topology_generation"]),
            "topology_crc32": int(refinement_identity["topology_crc32"]),
        })
    return ({node: sorted(values) for node, values in sorted(parsed.items())},
            records)


def build_matrix(level: int, data: dict[str, Any],
                 residence: dict[str, Any], *,
                 sck: dict[str, Any] | None = None,
                 data_path: str = "", residence_path: str = "",
                 sck_path: str = "",
                 data_refinement_candidates: dict[int, list[int]] | None = None,
                 data_refinement_evidence: list[dict[str, Any]] | None = None,
                 diagnostic_continue: bool = False,
                 ) -> dict[str, Any]:
    identity, failures = validate_profile_pair(data, residence)
    if failures:
        raise ValueError("TRN-02 evidence gate failed: " + ", ".join(failures))
    if identity["profile_crc32"] != profile_crc32(level):
        raise ValueError("profile CRC does not match requested operating level")

    nodes = node_order(data)
    count = len(nodes)
    data_matrix = data["matrix"]
    residence_matrix = residence["matrix"]
    data_links = indexed(data_matrix["links"], "link", count, "DATA links")
    residence_links = indexed(
        residence_matrix["links"], "link_index", count, "residence links")
    loops = indexed(residence_matrix["loops"], "node", count, "loops")
    trials_by_link: dict[int, list[dict[str, Any]]] = {
        index: [] for index in range(count)
    }
    trials = data.get("trials", [])
    if not isinstance(trials, list):
        raise ValueError("DATA trials must be a list")
    for trial in trials:
        if not isinstance(trial, dict):
            raise ValueError("DATA trial must be an object")
        index = int(trial.get("link", -1))
        if index not in trials_by_link:
            raise ValueError("DATA trial link is outside matrix")
        trials_by_link[index].append(trial)

    facts = pio_facts(level)
    clkdiv_q16 = facts["clkdiv_q16"]
    sample_period_ns = identity["sample_period_ns"]
    sck_offset_candidates_by_node: list[list[int]] = [
        [0] for _ in range(count)]
    selected_sck_offsets_by_node = [0] * count
    sck_link_bases: list[int] | None = None
    sck_selection: dict[str, Any] = {
        "requested_offset_sample_counts_by_node":
            list(selected_sck_offsets_by_node),
        "selected_offset_sample_counts_by_node":
            list(selected_sck_offsets_by_node),
        "requested_row_replay_safe": True,
        "selection_reason": "no_independent_sck_evidence",
        "selected_min_follower_margin_samples": None,
        "safe_candidate_row_count": 1,
        "candidate_row_count": 1,
    }
    if sck is not None:
        if (sck.get("phase") != "TRN-01_SCK_OFFSET_MATRIX" or
                not bool(sck.get("passed")) or node_order(sck) != nodes):
            raise ValueError("SCK evidence gate failed")
        sck_matrix_gate = sck.get("matrix")
        if not isinstance(sck_matrix_gate, dict) or not bool(
                sck_matrix_gate.get("passed")):
            raise ValueError("SCK repeat matrix is not accepted")
        sck_identity = singleton_identity(sck)
        for field in (*IDENTITY_FIELDS, "sample_period_ns"):
            if int(sck_identity[field]) != int(identity[field]):
                raise ValueError(f"sck_{field}_mismatch")
        sck_offsets = sck_matrix_gate.get("offset_matrix")
        if not isinstance(sck_offsets, dict):
            raise ValueError("SCK offset matrix is missing")
        if sck_offsets.get("schema") != SCK_MATRIX_SCHEMA:
            raise ValueError("SCK offset matrix is not independently trained V2 evidence")
        raw_candidates = sck_offsets.get("candidate_values_by_node")
        if (not isinstance(raw_candidates, list) or
                len(raw_candidates) != count):
            raise ValueError("SCK candidate dimensions are invalid")
        sck_offset_candidates_by_node = []
        for node, values in enumerate(raw_candidates):
            if (not isinstance(values, list) or not values or
                    any(isinstance(value, bool) or not isinstance(value, int)
                        for value in values)):
                raise ValueError(f"SCK node{node} candidates are invalid")
            sck_offset_candidates_by_node.append(sorted(set(values)))
        sck_rows = sck_offsets.get("rows")
        active_sck_row = int(sck_offsets.get("active_row_id", -1))
        if (not isinstance(sck_rows, list) or
                int(sck_offsets.get("full_matrix_row_count", -1)) !=
                len(sck_rows)):
            raise ValueError("SCK full matrix is incomplete")
        matches = [row for row in sck_rows if isinstance(row, dict) and
                   int(row.get("row_id", -1)) == active_sck_row]
        if len(matches) != 1:
            raise ValueError("SCK active row is unavailable")
        selected_values = matches[0].get(
            "sck_offset_sample_counts_by_node")
        if (not isinstance(selected_values, list) or
                len(selected_values) != count):
            raise ValueError("SCK active row dimensions are invalid")
        selected_sck_offsets_by_node = [int(value)
                                        for value in selected_values]
        training_parameters = sck.get("training_parameters")
        if not isinstance(training_parameters, dict):
            raise ValueError("SCK training parameters are missing")
        raw_sck_bases = training_parameters.get("link_base_delay_ns_by_link")
        if (not isinstance(raw_sck_bases, list) or
                len(raw_sck_bases) != count or
                any(isinstance(value, bool) or not isinstance(value, int) or
                    value <= 0 for value in raw_sck_bases)):
            raise ValueError("SCK per-link bases are invalid")
        sck_link_bases = [int(value) for value in raw_sck_bases]
        (selected_sck_offsets_by_node, sck_selection) = \
            _select_sck_replay_row(
                sck_offset_candidates_by_node,
                selected_sck_offsets_by_node,
                sck_link_bases,
                facts["baud_hz"],
                sample_period_ns,
                diagnostic_continue=diagnostic_continue)
    links: list[dict[str, Any]] = []
    marker_offsets_by_node = [0] * count
    data_offset_candidates_by_node: list[list[int]] = [[] for _ in range(count)]
    selected_data_offsets_by_node = [0] * count
    for index in range(count):
        data_link = data_links[index]
        residence_link = residence_links[index]
        next_node = (index + 1) % count
        if (int(data_link.get("marker_source_node", -1)) != index or
                int(data_link.get("marker_destination_node", -1)) !=
                next_node or
                int(data_link.get("data_source_node", -1)) != next_node or
                int(data_link.get("data_destination_node", -1)) != index or
                int(residence_link.get("source_node", -1)) != index or
                int(residence_link.get("destination_node", -1)) !=
                next_node):
            raise ValueError(
                f"link{index} evidence direction does not match loop order")
        link_trials = trials_by_link[index]
        expected_repeats = int(data_link["trial_count"])
        if len(link_trials) != expected_repeats:
            raise ValueError(f"link{index} DATA trial count mismatch")

        window_starts: list[int] = []
        window_ends: list[int] = []
        marker_to_data_samples: list[int] = []
        codeword_samples: list[int] = []
        guard_samples: list[int] = []
        marker_offsets: list[int] = []
        link_base_delays: list[int] = []
        calibrated_data_offsets: list[int] = []
        for trial in link_trials:
            source = trial.get("source")
            if not isinstance(source, dict) or not bool(trial.get("passed")):
                raise ValueError(f"link{index} contains an unaccepted trial")
            window_starts.append(int(source["training_window_start_ns"]))
            window_ends.append(int(source["training_window_end_ns"]))
            marker_to_data_samples.append(int(source["marker_to_data_samples"]))
            codeword_samples.append(int(source["expected_sample_count"]))
            guard_samples.append(int(source["guard_sample_count"]))
            marker_offsets.append(int(trial["link_marker_offset_sample"]))
            link_base_delays.append(int(source["link_base_delay_ns"]))
            calibrated_data_offsets.append(int(
                trial["calibrated_data_offset_sample_count"]))
        if (min(window_starts) <= 0 or
                any(end < start for start, end in
                    zip(window_starts, window_ends)) or
                len(set(marker_to_data_samples)) != 1 or
                len(set(codeword_samples)) != 1 or
                len(set(guard_samples)) != 1 or
                len(set(marker_offsets)) != 1 or
                len(set(link_base_delays)) != 1):
            raise ValueError(f"link{index} DATA timing fields are inconsistent")
        link_base_delay_ns = link_base_delays[0]
        if link_base_delay_ns <= 0:
            raise ValueError(f"link{index} base delay is invalid")
        if sck_link_bases is not None and \
                sck_link_bases[index] != link_base_delay_ns:
            raise ValueError(f"link{index} SCK/DATA base mismatch")

        # Repeated TRN-02 results may straddle one clk_sys sample bucket.
        # Select the deterministic median while preserving every observed
        # value in source_evidence. Flight MARK/SCK/DATA delay fields execute
        # in clkdiv=1 PIO state machines, so phase cycles use the training
        # sample period, not the slower operating-profile instruction period.
        selected_data_offset = sorted(calibrated_data_offsets)[
            len(calibrated_data_offsets) // 2]
        data_offset_ns = (link_base_delay_ns +
                          selected_data_offset * sample_period_ns)
        if data_offset_ns < 0:
            raise ValueError(f"link{index} DATA phase is below link base")
        data_phase_delay_cycles = (
            data_offset_ns + sample_period_ns // 2
        ) // sample_period_ns
        if data_phase_delay_cycles > 31:
            raise ValueError(f"link{index} DATA phase exceeds PIO delay field")
        marker_destination = int(data_link["marker_destination_node"])
        data_destination = int(data_link["data_destination_node"])
        sck_destination = marker_destination
        selected_sck_offset = selected_sck_offsets_by_node[sck_destination]
        sck_delay_ns = link_base_delay_ns + (
            selected_sck_offset * sample_period_ns)
        if sck_delay_ns < 0:
            raise ValueError(
                f"link{index} SCK phase is below the delay baseline")
        sck_phase_delay_cycles = (
            sck_delay_ns + sample_period_ns // 2
        ) // sample_period_ns
        if sck_phase_delay_cycles > 31:
            raise ValueError(f"link{index} SCK phase exceeds PIO delay field")
        marker_delay_ns = link_base_delay_ns + (
            marker_offsets[0] * sample_period_ns)
        if marker_delay_ns < 0:
            raise ValueError(f"link{index} MARK phase is below link base")
        marker_phase_delay_cycles = (
            marker_delay_ns + sample_period_ns // 2
        ) // sample_period_ns
        if marker_phase_delay_cycles > 31:
            raise ValueError(f"link{index} MARK phase exceeds PIO delay field")
        marker_offsets_by_node[marker_destination] = marker_offsets[0]
        data_offset_candidates_by_node[data_destination] = sorted(
            set(calibrated_data_offsets))
        selected_data_offsets_by_node[data_destination] = selected_data_offset

        residence_ticks = int(
            residence_link["selected_forward_residence_ticks"])
        loop_ticks = [int(value) for value in loops[index]["loop_delay_ticks"]]
        forward_residence_ns = residence_ticks * sample_period_ns
        loop_delay_ns = max(loop_ticks) * sample_period_ns
        marker_to_data_ns = marker_to_data_samples[0] * sample_period_ns
        codeword_ns = codeword_samples[0] * sample_period_ns
        guard_ns = guard_samples[0] * sample_period_ns
        # Arm against the earliest accepted edge of the repeated window.
        rx_arm_lead_ns = min(window_starts)
        cycles = {
            "marker_to_data_cycles": ns_to_pio_cycles(
                marker_to_data_ns, clkdiv_q16),
            "forward_residence_cycles": ns_to_pio_cycles(
                forward_residence_ns, clkdiv_q16),
            "rx_arm_lead_cycles": ns_to_pio_cycles(
                rx_arm_lead_ns, clkdiv_q16),
            "codeword_cycles": ns_to_pio_cycles(codeword_ns, clkdiv_q16),
            "guard_cycles": ns_to_pio_cycles(guard_ns, clkdiv_q16),
            "loop_delay_cycles": ns_to_pio_cycles(
                loop_delay_ns, clkdiv_q16),
        }
        cycles["link_budget_cycles"] = sum(cycles.values())
        links.append({
            "link_index": index,
            "evidence_flags": REQUIRED_EVIDENCE_FLAGS,
            **{key: facts[key] for key in (
                "pio_persona", "clkdiv_q16", "clk_sys_hz",
                "instruction_period_ns", "bit_cycles")},
            **cycles,
            "marker_offset_sample_count": marker_offsets[0],
            "sck_offset_sample_count": selected_sck_offset,
            "data_offset_sample_count": selected_data_offset,
            "sample_period_ns": sample_period_ns,
            "link_base_delay_ns": link_base_delay_ns,
            "marker_phase_delay_cycles": marker_phase_delay_cycles,
            "sck_phase_delay_cycles": sck_phase_delay_cycles,
            "data_phase_delay_cycles": data_phase_delay_cycles,
            "source_node": int(residence_link["source_node"]),
            "destination_node": int(residence_link["destination_node"]),
            # Keep the measured signal directions in the replay matrix.  A
            # later installation may wire DATA differently, so TRN-03B must
            # not infer these endpoints from the marker direction.
            "marker_source_node": int(data_link["marker_source_node"]),
            "marker_destination_node": int(
                data_link["marker_destination_node"]),
            "data_source_node": int(data_link["data_source_node"]),
            "data_destination_node": int(data_link["data_destination_node"]),
            "marker_direction": str(data_link["marker_direction"]),
            "data_direction": str(data_link["data_direction"]),
            "source_evidence": {
                "data_offset_histogram": data_link.get("offset_histogram", {}),
                "marker_offset_sample_counts": marker_offsets,
                "sck_offset_sample_counts":
                    sck_offset_candidates_by_node[sck_destination],
                "calibrated_data_offset_sample_counts":
                    calibrated_data_offsets,
                "data_window_start_ns": window_starts,
                "data_window_end_ns": window_ends,
                "marker_to_data_samples": marker_to_data_samples[0],
                "codeword_samples": codeword_samples[0],
                "guard_samples": guard_samples[0],
                "forward_residence_ticks": residence_link[
                    "forward_residence_ticks"],
                "selected_forward_residence_ticks": residence_ticks,
                "loop_delay_ticks": loop_ticks,
            },
        })

    refinement_candidates = data_refinement_candidates or {}
    normalized_refinement_candidates: dict[str, list[int]] = {}
    for node, requested_values in sorted(refinement_candidates.items()):
        if not 0 <= node < count:
            raise ValueError(
                f"DATA refinement candidate node {node} is outside matrix")
        observed_values = data_offset_candidates_by_node[node]
        if not observed_values:
            raise ValueError(
                f"DATA node{node} has no accepted TRN-02 candidate")
        accepted_refinements: list[int] = []
        for value in sorted(set(requested_values)):
            if not -10 <= value <= 10:
                raise ValueError(
                    f"DATA refinement candidate offset {value} is outside -10..+10")
            if value in observed_values:
                raise ValueError(
                    f"DATA node{node} refinement candidate {value} is already observed")
            if min(abs(value - observed) for observed in observed_values) != 1:
                raise ValueError(
                    f"DATA node{node} refinement candidate {value} must be "
                    "adjacent to accepted TRN-02 evidence")
            accepted_refinements.append(value)
        if accepted_refinements:
            data_offset_candidates_by_node[node] = sorted(
                set(observed_values + accepted_refinements))
            normalized_refinement_candidates[str(node)] = accepted_refinements

    offset_rows = []
    active_row_id = -1
    combined_rows = itertools.product(
        itertools.product(*sck_offset_candidates_by_node),
        itertools.product(*data_offset_candidates_by_node))
    for row_id, (sck_offsets, data_offsets) in enumerate(combined_rows):
        row = {
            "row_id": row_id,
            "marker_offset_sample_counts_by_node":
                list(marker_offsets_by_node),
            "sck_offset_sample_counts_by_node": list(sck_offsets),
            "data_offset_sample_counts_by_node": list(data_offsets),
            "marker_offset_ns_by_node": [
                value * sample_period_ns for value in marker_offsets_by_node],
            "data_offset_ns_by_node": [
                value * sample_period_ns for value in data_offsets],
            "sck_offset_ns_by_node": [
                value * sample_period_ns for value in sck_offsets],
        }
        if (list(sck_offsets) == selected_sck_offsets_by_node and
                list(data_offsets) == selected_data_offsets_by_node):
            active_row_id = row_id
        offset_rows.append(row)
    if active_row_id < 0:
        raise ValueError("selected TRN-03 offset row is absent from full matrix")

    result = {
        "schema": MATRIX_SCHEMA,
        "node_count": count,
        "evidence_flags": REQUIRED_EVIDENCE_FLAGS,
        **{field: identity[field] for field in (
            "calibration_generation", "topology_generation",
            "topology_crc32", "profile_crc32", "schedule_crc32")},
        "links": links,
        "offset_matrix": {
            "sample_period_ns": sample_period_ns,
            "full_matrix_row_count": len(offset_rows),
            "active_row_id": active_row_id,
            "rows": offset_rows,
        },
        "profile_level": level,
        "baud_hz": facts["baud_hz"],
        "cycle_period_ns": facts["cycle_period_ns"],
        "node_ids_in_loop_order": nodes,
        "derivation": {
            "data_summary": data_path,
            "sck_summary": sck_path,
            "residence_summary": residence_path,
            "sample_period_ns": sample_period_ns,
            "pio_fact_anchors": [
                "TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL",
                "tdma_pio_spi_clkdiv_for_baud",
                "BOARD_SYS_CLOCK_HZ",
                "s_tdma_operating_profiles",
            ],
            "repeat_gate": int(data["repeats"]),
            "max_offset_span_sample": int(data["max_offset_span_sample"]),
            "residence_selection": "matrix.selected_forward_residence_ticks",
            "loop_selection": "max(loop_delay_ticks) per source node",
            "sck_replay_selection": sck_selection,
            "data_refinement_candidates_by_node":
                normalized_refinement_candidates,
            "data_refinement_policy": (
                "non_active_adjacent_search_rows_from_accepted_trn02_window"),
            "data_refinement_evidence": data_refinement_evidence or [],
        },
    }
    # The active row is the default row every staging/closed-loop command
    # will load.  Reject an unsafe or structurally inconsistent recommendation
    # while retaining non-active rows as meaningful negative search evidence.
    try:
        from .trn03_stage import validate_config
    except ImportError:  # Direct execution from this directory.
        from trn03_stage import validate_config  # type: ignore[no-redef]
    validate_config(result, allow_unsafe_sck=diagnostic_continue)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level", type=int, required=True)
    parser.add_argument("--data", type=Path, required=True,
                        help="TRN-02D repeat-matrix summary")
    parser.add_argument("--residence", type=Path, required=True,
                        help="same-identity TRN-00 residence-matrix summary")
    parser.add_argument("--sck", type=Path,
                        help="same-identity independently trained SCK matrix summary")
    parser.add_argument(
        "--data-refinement-evidence", type=Path, action="append", default=[],
        help=("accepted TRN-02 selected-link repeat summary whose configured "
              "DATA offset is appended as a non-active adjacent search row; "
              "repeat as needed"))
    parser.add_argument(
        "--diagnostic-continue", action="store_true",
        help=("select the best measured SCK row when none satisfies the "
              "flight re-arm gate; write a non-passing diagnostic matrix"))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = load_summary(args.data)
    residence = load_summary(args.residence)
    sck = load_summary(args.sck) if args.sck is not None else None
    refinement_candidates, refinement_records = \
        derive_data_refinement_candidates(data, [
            (str(path), load_summary(path))
            for path in args.data_refinement_evidence])
    result = build_matrix(
        args.level, data, residence, sck=sck,
        data_path=str(args.data), residence_path=str(args.residence),
        sck_path=str(args.sck) if args.sck is not None else "",
        data_refinement_candidates=refinement_candidates,
        data_refinement_evidence=refinement_records,
        diagnostic_continue=args.diagnostic_continue)
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(encoded, encoding="utf-8")
    replay_safe = bool(result["derivation"]["sck_replay_selection"].get(
        "selected_row_replay_safe", True))
    print(json.dumps({
        "passed": replay_safe,
        "diagnostic_continue": args.diagnostic_continue,
        "level": args.level,
        "node_count": result["node_count"],
        "profile_crc32": result["profile_crc32"],
        "out": str(args.out),
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
