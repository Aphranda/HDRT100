#!/usr/bin/env python3
"""Run the Calibration P2 coded-marker HIL on a 2..8 board ring.

Board identity is always the exact ``*IDN?`` unique address.  COM ports are
only transient transport handles.  This tool performs maintenance-state
orchestration; marker generation, PIO forwarding, capture and correlation are
owned by the firmware core1 Calibration/TDMA path.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
from dataclasses import asdict
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import (  # noqa: E402
    Board,
    board_command,
    discover,
    status as ring_status,
    wait_started,
)
from calibration_ring_validate.calibration_load_guard import (  # noqa: E402
    CalibrationLoadGuard,
)
from calibration_ring_validate.calibration_timeout_config import (  # noqa: E402
    DEFAULT_ACTION_TIMEOUT_S,
    DEFAULT_PHASE_GAP_S,
    DEFAULT_SERIAL_SETTLE_S,
)


CODED_FIELDS = (
    "version", "state", "reject_reason", "flags", "board_id_lo",
    "board_id_hi", "build_id_lo", "build_id_hi", "local_node",
    "train_epoch", "train_sequence", "calibration_generation",
    "topology_generation", "topology_crc32", "profile_crc32",
    "schedule_crc32", "baud_hz", "codebook_id", "sample_period_ns",
    "coarse_min_sample", "coarse_max_sample", "capture_origin_lo",
    "capture_origin_hi", "capture_sample_count",
    "timing_field_tx_origin_sample", "best_lag_sample", "best_distance",
    "second_lag_sample", "second_distance", "margin", "polarity",
    "marker_flags", "tx_dma_count", "rx_dma_count", "dma_overrun_count",
    "pio_stall_count",
)
STATE_IDLE = 0
STATE_ACCEPTED = 3
STATE_REJECTED = 4
MARKER_FLAGS_ALL = 0x3F
CODEBOOK_HALF_CHIP_NS = {0: 20, 1: 40, 2: 24, 3: 32}

CODED_REJECT_NAMES = {
    0: "none",
    1: "bad_argument",
    2: "bad_state",
    3: "generation",
    4: "coarse_bracket",
    5: "dma",
    6: "pio_stall",
}
CORRELATION_REJECT_NAMES = {
    1: "bad_argument",
    2: "search_range",
    3: "capture_truncated",
    4: "polarity",
    5: "sof",
    6: "manchester",
    7: "header_inverse",
    8: "header_crc",
    9: "header_mismatch",
    10: "eof",
    11: "distance",
    12: "margin",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address in physical ring order")
    parser.add_argument("--expected-build")
    parser.add_argument("--level", type=int, default=7)
    parser.add_argument("--codebook", type=int, default=1,
                        help="candidate codebook: 0=20ns, 1=40ns, 2=24ns, 3=32ns")
    parser.add_argument("--min-lag", type=int, default=80)
    parser.add_argument("--max-lag", type=int, default=120)
    parser.add_argument("--max-distance", type=int, default=512,
                        help="diagnostic scan gate; tighten only from repeats")
    parser.add_argument("--min-margin", type=int, default=0,
                        help="diagnostic scan gate; tighten only from repeats")
    parser.add_argument("--reference-node", type=int, action="append",
                        help=("reference node to run; repeat as needed, "
                              "default all nodes"))
    parser.add_argument("--repeats", type=int, default=1,
                        help=("independent STOP/ARM/coded/STOP trials per "
                              "reference node"))
    parser.add_argument("--max-reject-ratio", type=float, default=0.0,
                        help="diagnostic repeat gate, in [0,1]; default requires all trials")
    parser.add_argument("--max-lag-span", type=int,
                        help=("optional per-reference-node accepted lag span "
                              "gate in raw samples"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--action-timeout", type=float,
                        default=DEFAULT_ACTION_TIMEOUT_S,
                        help=("bounded wait for coded START/STOP payload; "
                              "guarded snapshot remains the acceptance gate"))
    parser.add_argument("--coded-timeout", type=float, default=5.0)
    parser.add_argument("--settle", type=float, default=DEFAULT_SERIAL_SETTLE_S)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--probe-phase-cycles", type=int, action="append",
                        help=("stopped raw-link topology-probe phase candidate; "
                              "repeat to scan candidates, default 10"))
    parser.add_argument("--gap", type=float, default=DEFAULT_PHASE_GAP_S)
    parser.add_argument("--poll-interval", type=float, default=0.02)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--short-open", action="store_true",
                        help="open/close CDC for every command (diagnostic fallback)")
    return parser.parse_args()


def parse_csv_u32(raw: str, expected: int) -> dict[str, int]:
    row = next(csv.reader([raw]), [])
    if len(row) != expected:
        raise RuntimeError(f"coded status field count {len(row)} != {expected}: {raw!r}")
    return {
        name: int(value.strip().strip('"'), 0)
        for name, value in zip(CODED_FIELDS, row)
    }


def reject_reason_name(reason: int) -> str:
    if reason >= 0x100:
        correlation_reason = reason - 0x100
        return "correlation_" + CORRELATION_REJECT_NAMES.get(
            correlation_reason, f"unknown_{correlation_reason}")
    return CODED_REJECT_NAMES.get(reason, f"unknown_{reason}")


def metric_statistics(values: list[int]) -> dict[str, int | float | None]:
    """Return deterministic descriptive statistics for accepted trials.

    P99 uses the nearest-rank definition.  Population standard deviation is
    used because the captured repeats are the complete HIL run being scored,
    not a sample-size correction for a larger population.
    """
    if not values:
        return {
            "count": 0,
            "min": None,
            "max": None,
            "mean": None,
            "p99": None,
            "stddev": None,
        }
    ordered = sorted(values)
    p99_index = max(0, math.ceil(0.99 * len(ordered)) - 1)
    return {
        "count": len(ordered),
        "min": ordered[0],
        "max": ordered[-1],
        "mean": statistics.fmean(ordered),
        "p99": ordered[p99_index],
        "stddev": statistics.pstdev(ordered),
    }


def trial_reject_category(trial: dict[str, object]) -> str:
    error = str(trial.get("error", ""))
    if error:
        return "host_error:" + error.split(":", maxsplit=1)[0]
    snapshot = trial.get("snapshot")
    if not isinstance(snapshot, dict):
        return "missing_snapshot"
    if int(snapshot.get("state", -1)) != STATE_ACCEPTED:
        return reject_reason_name(int(snapshot.get("reject_reason", -1)))
    if int(snapshot.get("marker_flags", 0)) != MARKER_FLAGS_ALL:
        return "marker_flags"
    if int(snapshot.get("dma_overrun_count", 0)) != 0:
        return "dma_overrun"
    if int(snapshot.get("pio_stall_count", 0)) != 0:
        return "pio_stall"
    if int(snapshot.get("capture_origin_lo", 0)) == 0 and int(
            snapshot.get("capture_origin_hi", 0)) == 0:
        return "capture_origin_missing"
    return "accepted"


def trial_integrity_category(trial: dict[str, object]) -> str:
    """Classify whether one trial produced trustworthy calibration evidence."""
    error = str(trial.get("error", ""))
    if error:
        return "host_error:" + error.split(":", maxsplit=1)[0]
    snapshot = trial.get("snapshot")
    if not isinstance(snapshot, dict) or not snapshot:
        return "missing_snapshot"
    state = int(snapshot.get("state", -1))
    if state not in (STATE_ACCEPTED, STATE_REJECTED):
        return "incomplete_state"
    expected_board_id = int(str(trial["reference_node_id"]), 16)
    actual_board_id = (
        int(snapshot.get("board_id_hi", -1)) << 32 |
        int(snapshot.get("board_id_lo", -1)))
    if actual_board_id != expected_board_id:
        return "board_identity"
    if int(snapshot.get("local_node", -1)) != int(trial["reference_node"]):
        return "local_node_identity"
    expected_build = trial.get("reference_build_id")
    if expected_build is not None:
        actual_build = (
            int(snapshot.get("build_id_hi", -1)) << 32 |
            int(snapshot.get("build_id_lo", -1)))
        if actual_build != int(str(expected_build)):
            return "build_identity"
    reason = int(snapshot.get("reject_reason", -1))
    if state == STATE_REJECTED and reason < 0x100:
        return "control_reject:" + reject_reason_name(reason)
    if int(snapshot.get("marker_flags", 0)) != MARKER_FLAGS_ALL:
        return "marker_flags"
    if int(snapshot.get("dma_overrun_count", 0)) != 0:
        return "dma_overrun"
    if int(snapshot.get("pio_stall_count", 0)) != 0:
        return "pio_stall"
    if (int(snapshot.get("capture_origin_lo", 0)) == 0 and
            int(snapshot.get("capture_origin_hi", 0)) == 0):
        return "capture_origin_missing"
    return "complete"


def summarize_trials(trials: list[dict[str, object]],
                     max_reject_ratio: float,
                     max_lag_span: int | None) -> dict[str, object]:
    categories = Counter(trial_reject_category(trial) for trial in trials)
    integrity_categories = Counter(
        trial_integrity_category(trial) for trial in trials)
    accepted = [trial for trial in trials
                if trial_reject_category(trial) == "accepted"]
    rejected_count = len(trials) - len(accepted)
    reject_ratio = rejected_count / len(trials) if trials else 1.0

    def values(field: str) -> list[int]:
        return [int(trial["snapshot"][field]) for trial in accepted]

    lag_values = values("best_lag_sample")
    lag_histogram = dict(sorted(Counter(lag_values).items()))
    sequence_values = values("train_sequence")
    sequence_unique = len(sequence_values) == len(set(sequence_values))
    sequence_monotonic = all(
        current > previous
        for previous, current in zip(sequence_values, sequence_values[1:]))
    lag_span = max(lag_values) - min(lag_values) if lag_values else None
    mixed_peak_count = sum(
        int(trial["snapshot"]["margin"]) == 0 or
        int(trial["snapshot"]["second_distance"]) ==
        int(trial["snapshot"]["best_distance"])
        for trial in accepted)
    gate_failures: list[str] = []
    if reject_ratio > max_reject_ratio:
        gate_failures.append("reject_ratio")
    if not sequence_unique:
        gate_failures.append("duplicate_sequence")
    if not sequence_monotonic:
        gate_failures.append("non_monotonic_sequence")
    if max_lag_span is not None and (lag_span is None or lag_span > max_lag_span):
        gate_failures.append("lag_span")
    if mixed_peak_count != 0:
        gate_failures.append("mixed_peak")
    integrity_complete = (
        bool(trials) and integrity_categories == {"complete": len(trials)})
    if not integrity_complete:
        gate_failures.append("integrity")
    return {
        "trial_count": len(trials),
        "accepted_count": len(accepted),
        "rejected_count": rejected_count,
        "accepted_ratio": len(accepted) / len(trials) if trials else 0.0,
        "reject_ratio": reject_ratio,
        "reject_categories": dict(sorted(categories.items())),
        "integrity_categories": dict(sorted(integrity_categories.items())),
        "integrity_complete": integrity_complete,
        "mixed_peak_count": mixed_peak_count,
        "mixed_peak_ratio": mixed_peak_count / len(accepted) if accepted else 0.0,
        "sequence_unique": sequence_unique,
        "sequence_monotonic": sequence_monotonic,
        "lag_span": lag_span,
        "lag_histogram": {str(key): value for key, value in lag_histogram.items()},
        "best_lag_sample": metric_statistics(lag_values),
        "best_distance": metric_statistics(values("best_distance")),
        "second_distance": metric_statistics(values("second_distance")),
        "margin": metric_statistics(values("margin")),
        "gate_failures": gate_failures,
        "passed": not gate_failures and bool(trials),
    }


def phase_selection_key(summary: dict[str, object],
                        phase_cycles: int) -> tuple[float, float, int] | None:
    """Rank one eligible phase by worst margin, then worst distance."""
    if not bool(summary.get("passed")):
        return None
    margin = summary.get("margin")
    distance = summary.get("best_distance")
    if not isinstance(margin, dict) or not isinstance(distance, dict):
        return None
    margin_min = margin.get("min")
    distance_max = distance.get("max")
    if margin_min is None or distance_max is None:
        return None
    return (-float(margin_min), float(distance_max), phase_cycles)


def summarize_phase_matrix(
        trials: list[dict[str, object]], max_reject_ratio: float,
        max_lag_span: int | None) -> dict[str, object]:
    """Select one phase per reference node without promoting candidate gates."""
    grouped: dict[int, dict[int, list[dict[str, object]]]] = {}
    for trial in trials:
        reference_node = int(trial["reference_node"])
        phase_cycles = int(trial["probe_phase_cycles"])
        grouped.setdefault(reference_node, {}).setdefault(
            phase_cycles, []).append(trial)

    result: dict[str, object] = {}
    for reference_node, phase_groups in sorted(grouped.items()):
        candidates = {
            str(phase): summarize_trials(
                group, max_reject_ratio, max_lag_span)
            for phase, group in sorted(phase_groups.items())
        }
        ranked: list[tuple[tuple[float, float, int], int]] = []
        for phase_text, summary in candidates.items():
            phase = int(phase_text)
            key = phase_selection_key(summary, phase)
            if key is not None:
                ranked.append((key, phase))
        selected_phase = min(ranked)[1] if ranked else None
        selected = (candidates[str(selected_phase)]
                    if selected_phase is not None else None)
        first_group = next(iter(phase_groups.values()))
        result[str(reference_node)] = {
            "reference_node_no": reference_node + 1,
            "reference_node_id": first_group[0]["reference_node_id"],
            "phase_candidates": candidates,
            "selected_probe_phase_cycles": selected_phase,
            "selection_policy": [
                "maximize_margin_min",
                "minimize_best_distance_max",
                "minimize_phase_cycles",
            ],
            "selected_statistics": selected,
            "passed": selected is not None,
        }
    return result


def summarize_by_reference_node(trials: list[dict[str, object]],
                                max_reject_ratio: float,
                                max_lag_span: int | None) -> dict[str, object]:
    grouped: dict[int, list[dict[str, object]]] = {}
    for trial in trials:
        grouped.setdefault(int(trial["reference_node"]), []).append(trial)
    return {
        str(reference_node): {
            "reference_node_no": reference_node + 1,
            "reference_node_id": group[0]["reference_node_id"],
            **summarize_trials(group, max_reject_ratio, max_lag_span),
        }
        for reference_node, group in sorted(grouped.items())
    }


def coded_status(board: Board, args: argparse.Namespace) -> dict[str, int]:
    raw = board_command(board, "READ:CALibration:CLOCk:CODEd?", args)
    return parse_csv_u32(raw, len(CODED_FIELDS))


def coded_action_args(args: argparse.Namespace) -> argparse.Namespace:
    fast = argparse.Namespace(**vars(args))
    fast.timeout = min(args.timeout, args.action_timeout)
    fast.settle = min(args.settle, 0.05)
    return fast


def wait_ring_stopped(board: Board,
                      args: argparse.Namespace) -> dict[str, int]:
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    while time.monotonic() < deadline:
        last = ring_status(board, args)
        if (last["ring_enabled"] == 0 and
                last["ring_adapter_started"] == 0):
            return last
        time.sleep(args.poll_interval)
    raise RuntimeError(f"{board.address}: STOP apply timeout: {last}")


def start_coded(board: Board, args: argparse.Namespace) -> str:
    action_args = coded_action_args(args)
    expected = [args.codebook, args.min_lag, args.max_lag,
                args.max_distance, args.min_margin]
    command = (
        "CALibration:CLOCk:CODEd:STARt "
        f"{args.codebook},{args.min_lag},{args.max_lag},"
        f"{args.max_distance},{args.min_margin}")
    response = ""
    attempt_errors: list[list[str]] = []
    for attempt in range(1, 4):
        response = board_command(board, command, action_args)
        row = next(csv.reader([response]), [])
        try:
            actual = [int(value.strip().strip('"'), 0) for value in row]
        except ValueError:
            actual = []
        if actual == expected:
            return response
        # Persona switching may race the lower-priority CDC response flush.
        # Wait for the asynchronous Core0->Core1 mailbox to leave IDLE, then
        # accept the timeout only when the guarded snapshot proves that this
        # exact request reached core1.
        deadline = time.monotonic() + min(args.coded_timeout, 1.0)
        snapshot = coded_status(board, args)
        while (time.monotonic() < deadline and
               snapshot["state"] == STATE_IDLE):
            time.sleep(args.poll_interval)
            snapshot = coded_status(board, args)
        if (snapshot["state"] != STATE_IDLE and
                snapshot["codebook_id"] == args.codebook and
                snapshot["coarse_min_sample"] == args.min_lag and
                snapshot["coarse_max_sample"] == args.max_lag and
                snapshot["train_sequence"] != 0):
            return f"{response}; accepted_by_snapshot_attempt={attempt}"
        # A prior STOP intent may still be crossing the Core0/Core1 mailbox.
        # Drain the explicit rejection before one bounded retry.
        errors: list[str] = []
        for _ in range(16):
            error = board_command(board, "SYSTem:ERR?", args)
            errors.append(error)
            if error.startswith("0,") or error.startswith('+0,'):
                break
        attempt_errors.append(errors)
        time.sleep(args.gap)
    raise RuntimeError(
        f"{board.address}: coded START rejected after 3 attempts: "
        f"{response!r}, errors={attempt_errors}, snapshot={snapshot}")


def prepare_ring(ordered: list[Board], reference_node: int,
                 args: argparse.Namespace) -> list[dict[str, object]]:
    actions: list[dict[str, object]] = []
    node_count = len(ordered)
    reference = ordered[reference_node]
    start_order = [board for node, board in enumerate(ordered)
                   if node != reference_node] + [reference]
    def prepare_board(board: Board):
        stop = board_command(board, "SYSTem:TDMA:RING:STOP", args)
        clear = board_command(board, "CALibration:TRAINing:STAGe:CLEar", args)
        probe = board_command(
            board, f"CALibration:TOPology:PROBe 1,{args.probe_phase_cycles}", args)
        stage = board_command(board, f"SYSTem:TDMA:OPMode:STAGe {args.level}", args)
        apply = board_command(board, "SYSTem:TDMA:OPMode:APPLy", args)
        return (
            {"board": board.address, "command": "STOP", "response": stop},
            {"board": board.address, "command": "CLEAR_TRAINING_STAGE", "response": clear},
            {"board": board.address, "command": "TOPOLOGY_PROBE_ENABLE", "response": probe},
            {"board": board.address, "command": "OPMODE", "response": stage},
            {"board": board.address, "command": "OPMODE_APPLY", "response": apply},
        )

    with ThreadPoolExecutor(max_workers=node_count) as executor:
        for result in executor.map(prepare_board, ordered):
            actions.extend(result)

    def set_topology(item):
        node, board = item
        command = (f"SYSTem:TDMA:RING:TOPology "
                   f"{node_count},{node},{reference_node}")
        return {"board": board.address, "command": command,
                "response": board_command(board, command, args)}

    with ThreadPoolExecutor(max_workers=node_count) as executor:
        actions.extend(executor.map(set_topology, enumerate(ordered)))

    def arm_one(board: Board):
        response = board_command(board, "SYSTem:TDMA:RING:ARM", args)
        arm_result = int(board_command(
            board, "SYSTem:TDMA:RING:ARM:STATus?", args
        ).strip().strip('"'), 0)
        if arm_result != 1:
            raise RuntimeError(
                f"{board.address}: ARM rejected with result={arm_result}")
        return board, response, arm_result

    with ThreadPoolExecutor(max_workers=len(start_order) - 1) as executor:
        armed = list(executor.map(arm_one, start_order[:-1]))
    armed.append(arm_one(start_order[-1]))
    for board, response, arm_result in armed:
        actions.append({"board": board.address, "command": "ARM_SUBMIT",
                        "response": response, "arm_result": arm_result})

    with ThreadPoolExecutor(max_workers=node_count) as executor:
        started = list(executor.map(
            lambda board: (board, wait_started(board, args)), ordered))
    for board, readback in started:
        arm = next(item for item in armed if item[0] is board)
        actions.append({"board": board.address, "command": "ARM_STARTED",
                        "response": arm[1], "arm_result": arm[2],
                        "status": readback})

    with ThreadPoolExecutor(max_workers=node_count) as executor:
        stopped = list(executor.map(
            lambda board: (board, board_command(
                board, "SYSTem:TDMA:RING:STOP", args)), ordered))
    for board, response in stopped:
        actions.append({"board": board.address, "command": "STOP_AFTER_ARM",
                        "response": response})
    with ThreadPoolExecutor(max_workers=node_count) as executor:
        stopped_status = list(executor.map(
            lambda board: (board, wait_ring_stopped(board, args)), ordered))
    for board, status in stopped_status:
        actions.append({"board": board.address, "command": "STOP_APPLIED",
                        "status": status})
    with ThreadPoolExecutor(max_workers=node_count) as executor:
        disabled = list(executor.map(
            lambda board: (board, board_command(
                board, "CALibration:TOPology:PROBe 0", args)), ordered))
    for board, response in disabled:
        actions.append({"board": board.address,
                        "command": "TOPOLOGY_PROBE_DISABLE",
                        "response": response})
    time.sleep(args.gap)
    return actions


def wait_reference_node(board: Board,
                        args: argparse.Namespace) -> dict[str, int]:
    deadline = time.monotonic() + args.coded_timeout
    last = coded_status(board, args)
    while time.monotonic() < deadline:
        if last["state"] in (STATE_ACCEPTED, STATE_REJECTED):
            return last
        time.sleep(args.poll_interval)
        last = coded_status(board, args)
    raise RuntimeError(f"{board.address}: coded completion timeout: {last}")


def stop_coded(ordered: list[Board], args: argparse.Namespace) -> None:
    action_args = coded_action_args(args)
    with ThreadPoolExecutor(max_workers=len(ordered)) as executor:
        list(executor.map(
            lambda board: board_command(
                board, "CALibration:CLOCk:CODEd:STOP", action_args), ordered))
    deadline = time.monotonic() + args.coded_timeout
    while time.monotonic() < deadline:
        with ThreadPoolExecutor(max_workers=len(ordered)) as executor:
            states = list(executor.map(
                lambda board: coded_status(board, args)["state"], ordered))
        if all(state == STATE_IDLE for state in states):
            return
        time.sleep(args.poll_interval)
    raise RuntimeError("coded STOP did not restore IDLE on every board")


def run_reference_node(ordered: list[Board], reference_node: int,
                       repeat_index: int, prepare: bool,
                       args: argparse.Namespace) -> dict[str, object]:
    actions = prepare_ring(ordered, reference_node, args) if prepare else []
    reference = ordered[reference_node]
    followers = [board for node, board in enumerate(ordered)
                 if node != reference_node]
    started: list[dict[str, object]] = []
    try:
        def start_follower(board: Board):
            response = start_coded(board, args)
            return board, response, coded_status(board, args)

        with ThreadPoolExecutor(max_workers=len(followers)) as executor:
            follower_results = list(executor.map(start_follower, followers))
        for board, response, follower_snapshot in follower_results:
            print(json.dumps({"coded_start": {"board": board.address,
                  "role": "follower", "response": response}}), flush=True)
            started.append({"board": board.address, "role": "follower",
                            "response": response,
                            "snapshot": follower_snapshot})
        response = start_coded(reference, args)
        print(json.dumps({"coded_start": {"board": reference.address,
              "role": "reference", "response": response}}), flush=True)
        started.append({"board": reference.address, "role": "reference",
                        "response": response})
        reference_snapshot = wait_reference_node(reference, args)
        result = {
            "reference_node": reference_node,
            "reference_node_no": reference_node + 1,
            "reference_node_id": reference.address,
            "reference_build_id": reference.build,
            "repeat_index": repeat_index,
            "probe_phase_cycles": args.probe_phase_cycles,
            "snapshot": reference_snapshot,
            "started": started,
            "actions": actions,
        }
        result["reject_category"] = trial_reject_category(result)
        result["accepted"] = result["reject_category"] == "accepted"
    finally:
        stop_coded(ordered, args)
    return result


def write_csv(path: Path, trials: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.writer(handle)
        writer.writerow(("reference_node_no", "reference_node_id",
                         "probe_phase_cycles",
                         "repeat_index", "accepted",
                         "reject_category", "state",
                         "reject_reason", "best_lag_sample", "best_distance",
                         "second_distance", "margin", "flags", "error"))
        for item in trials:
            snapshot = item.get("snapshot", {})
            writer.writerow((item["reference_node_no"],
                             item["reference_node_id"],
                             item["probe_phase_cycles"],
                             item["repeat_index"], int(bool(item["accepted"])),
                             item["reject_category"], snapshot.get("state", ""),
                             snapshot.get("reject_reason", ""),
                             snapshot.get("best_lag_sample", ""),
                             snapshot.get("best_distance", ""),
                             snapshot.get("second_distance", ""),
                             snapshot.get("margin", ""), snapshot.get("flags", ""),
                             item.get("error", "")))


def main() -> int:
    args = parse_args()
    args.keep_open = not args.short_open
    if len(args.board_id) < 2 or len(args.board_id) > 8:
        raise SystemExit("board count must be in [2, 8]")
    if len(set(args.board_id)) != len(args.board_id):
        raise SystemExit("board IDs must be unique")
    if (args.min_lag < 0 or args.min_lag >= args.max_lag or
            args.max_lag - args.min_lag + 1 > 256):
        raise SystemExit("invalid bounded coded lag window")
    if args.codebook < 0 or args.codebook > 3:
        raise SystemExit("candidate codebook must be in [0, 3]")
    if args.repeats < 1 or args.repeats > 1000:
        raise SystemExit("repeats must be in [1, 1000]")
    if not 0.0 <= args.max_reject_ratio <= 1.0:
        raise SystemExit("max-reject-ratio must be in [0, 1]")
    if args.action_timeout <= 0.0 or args.action_timeout > args.timeout:
        raise SystemExit("action-timeout must be > 0 and <= timeout")
    if args.max_lag_span is not None and args.max_lag_span < 0:
        raise SystemExit("max-lag-span must be >= 0")
    phase_candidates = (args.probe_phase_cycles
                        if args.probe_phase_cycles is not None else [10])
    if (len(set(phase_candidates)) != len(phase_candidates) or
            any(phase < 1 or phase > 31 for phase in phase_candidates)):
        raise SystemExit("probe phase candidates must be unique and in [1, 31]")
    args.board_ids = list(args.board_id)
    boards = discover(args)
    missing = set(args.board_id) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    ordered = [boards[address] for address in args.board_id]
    if args.expected_build:
        wrong = {board.address: board.build for board in ordered
                 if board.build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")
    plan = {
        "measurement_domain": "calibration",
        "phase": "coded_marker_correlated_rtt",
        "diagnostic_only": True,
        "board_ids_in_physical_order": args.board_id,
        "codebook": args.codebook,
        "half_chip_ns": CODEBOOK_HALF_CHIP_NS[args.codebook],
        "min_lag": args.min_lag,
        "max_lag": args.max_lag,
        "max_distance": args.max_distance,
        "min_margin": args.min_margin,
        "repeats": args.repeats,
        "probe_phase_candidates": phase_candidates,
        "repeat_gate": {
            "max_reject_ratio": args.max_reject_ratio,
            "max_lag_span": args.max_lag_span,
            "marker_flags_all": MARKER_FLAGS_ALL,
        },
        "host_orchestration": {
            "timeout_s": args.timeout,
            "coded_action_timeout_s": args.action_timeout,
            "settle_s": args.settle,
            "independent_coded_stop_each_trial": True,
            "ring_prepare_once_per_reference_node_and_phase": True,
        },
        "boards": {board.address: asdict(board) for board in ordered},
    }
    print(json.dumps(plan, ensure_ascii=False, indent=2))
    if args.dry_run:
        return 0
    reference_nodes = (list(args.reference_node)
                       if args.reference_node is not None
                       else list(range(len(ordered))))
    if (not reference_nodes or
            len(set(reference_nodes)) != len(reference_nodes) or
            any(node < 0 or node >= len(ordered)
                for node in reference_nodes)):
        raise SystemExit("reference-node must select unique active nodes")
    trials: list[dict[str, object]] = []
    load_guard = CalibrationLoadGuard(ordered, args)
    with load_guard:
        for reference_node in reference_nodes:
            for phase_cycles in phase_candidates:
                phase_args = argparse.Namespace(**vars(args))
                phase_args.probe_phase_cycles = phase_cycles
                phase_prepared = False
                for repeat_index in range(1, args.repeats + 1):
                    try:
                        result = run_reference_node(
                            ordered, reference_node, repeat_index,
                            prepare=not phase_prepared, args=phase_args)
                        phase_prepared = True
                    except Exception as exc:  # noqa: BLE001 - retain all repeat evidence
                        error = f"{type(exc).__name__}: {exc}"
                        result = {
                            "reference_node": reference_node,
                            "reference_node_no": reference_node + 1,
                            "reference_node_id": ordered[reference_node].address,
                            "reference_build_id": ordered[reference_node].build,
                            "probe_phase_cycles": phase_cycles,
                            "repeat_index": repeat_index,
                            "accepted": False,
                            "snapshot": {},
                            "started": [],
                            "actions": [],
                            "error": error,
                        }
                        result["reject_category"] = trial_reject_category(result)
                        try:
                            stop_coded(ordered, phase_args)
                        except Exception as stop_exc:  # noqa: BLE001
                            result["stop_error"] = (
                                f"{type(stop_exc).__name__}: {stop_exc}")
                    trials.append(result)
                    print(json.dumps({"trial_complete": {
                        "reference_node_no": result["reference_node_no"],
                        "reference_node_id": result["reference_node_id"],
                        "probe_phase_cycles": result["probe_phase_cycles"],
                        "repeat_index": result["repeat_index"],
                        "accepted": result["accepted"],
                        "reject_category": result["reject_category"],
                        "snapshot": result["snapshot"],
                        "error": result.get("error", ""),
                    }}, ensure_ascii=False), flush=True)
    by_reference_node = summarize_phase_matrix(
        trials, args.max_reject_ratio, args.max_lag_span)
    overall = summarize_trials(
        trials, args.max_reject_ratio, max_lag_span=None)
    expected_trial_count = (
        len(reference_nodes) * len(phase_candidates) * args.repeats)
    integrity_failures = [
        {"reference_node_no": trial["reference_node_no"],
         "probe_phase_cycles": trial["probe_phase_cycles"],
         "repeat_index": trial["repeat_index"],
         "category": category}
        for trial in trials
        if (category := trial_integrity_category(trial)) != "complete"
    ]
    passed = (len(trials) == expected_trial_count and
              all(bool(summary["passed"])
                  for summary in by_reference_node.values()))
    output = {
        **plan,
        "realtime_calibration_load": load_guard.evidence(),
        "passed": passed,
        "expected_trial_count": expected_trial_count,
        "integrity_failures": integrity_failures,
        "trials": trials,
        "statistics_by_reference_node": by_reference_node,
        "statistics_overall": overall,
    }
    out_dir = args.out_dir or (ROOT / "out" / "training" /
                               f"calibration_clk_coded_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    write_csv(out_dir / "summary.csv", trials)
    (out_dir / "summary.txt").write_text(
        "\n".join(
            f"Node{summary['reference_node_no'] - 1} "
            f"{summary['reference_node_id']}: "
            f"selected_phase={summary['selected_probe_phase_cycles']} "
            f"candidates={list(summary['phase_candidates'])} "
            f"passed={summary['passed']}"
            for summary in by_reference_node.values()) + "\n",
        encoding="utf-8")
    print(f"passed={passed} out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
