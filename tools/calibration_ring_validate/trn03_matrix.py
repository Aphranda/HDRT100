#!/usr/bin/env python3
"""Generate a replayable TRN-03 matrix from paired TRN-02 evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

try:
    from .trn02_profile_gate import (
        load_summary,
        node_order,
        validate_profile_pair,
    )
except ImportError:  # Direct execution from this directory.
    from trn02_profile_gate import (  # type: ignore[no-redef]
        load_summary,
        node_order,
        validate_profile_pair,
    )


MATRIX_SCHEMA = "HAOFV_TRN03_REPLAY_MATRIX_V1"
REQUIRED_EVIDENCE_FLAGS = 0x1F
NORMAL_PIO_PERSONA = 1
NORMAL_PIO_BIT_CYCLES = 6
BOARD_SYS_CLOCK_HZ = 250_000_000
PROFILE_VERSION = 1
PROFILE_TRAIN_CYCLES = 4096
PROFILE_FLAGS = 0
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619

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


def build_matrix(level: int, data: dict[str, Any],
                 residence: dict[str, Any], *,
                 data_path: str = "", residence_path: str = ""
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
    links: list[dict[str, Any]] = []
    for index in range(count):
        data_link = data_links[index]
        residence_link = residence_links[index]
        link_trials = trials_by_link[index]
        expected_repeats = int(data_link["trial_count"])
        if len(link_trials) != expected_repeats:
            raise ValueError(f"link{index} DATA trial count mismatch")

        window_starts: list[int] = []
        window_ends: list[int] = []
        marker_to_data_samples: list[int] = []
        codeword_samples: list[int] = []
        guard_samples: list[int] = []
        for trial in link_trials:
            source = trial.get("source")
            if not isinstance(source, dict) or not bool(trial.get("passed")):
                raise ValueError(f"link{index} contains an unaccepted trial")
            window_starts.append(int(source["training_window_start_ns"]))
            window_ends.append(int(source["training_window_end_ns"]))
            marker_to_data_samples.append(int(source["marker_to_data_samples"]))
            codeword_samples.append(int(source["expected_sample_count"]))
            guard_samples.append(int(source["guard_sample_count"]))
        if (min(window_starts) <= 0 or
                any(end < start for start, end in
                    zip(window_starts, window_ends)) or
                len(set(marker_to_data_samples)) != 1 or
                len(set(codeword_samples)) != 1 or
                len(set(guard_samples)) != 1):
            raise ValueError(f"link{index} DATA timing fields are inconsistent")

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

    return {
        "schema": MATRIX_SCHEMA,
        "node_count": count,
        "evidence_flags": REQUIRED_EVIDENCE_FLAGS,
        **{field: identity[field] for field in (
            "calibration_generation", "topology_generation",
            "topology_crc32", "profile_crc32", "schedule_crc32")},
        "links": links,
        "profile_level": level,
        "baud_hz": facts["baud_hz"],
        "cycle_period_ns": facts["cycle_period_ns"],
        "node_ids_in_loop_order": nodes,
        "derivation": {
            "data_summary": data_path,
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
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level", type=int, required=True)
    parser.add_argument("--data", type=Path, required=True,
                        help="TRN-02D repeat-matrix summary")
    parser.add_argument("--residence", type=Path, required=True,
                        help="same-identity TRN-01 residence-matrix summary")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = load_summary(args.data)
    residence = load_summary(args.residence)
    result = build_matrix(
        args.level, data, residence,
        data_path=str(args.data), residence_path=str(args.residence))
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(encoded, encoding="utf-8")
    print(json.dumps({
        "passed": True,
        "level": args.level,
        "node_count": result["node_count"],
        "profile_crc32": result["profile_crc32"],
        "out": str(args.out),
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
