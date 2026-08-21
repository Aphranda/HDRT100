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


CODED_FIELDS = (
    "version", "state", "reject_reason", "flags", "board_id_lo",
    "board_id_hi", "build_id_lo", "build_id_hi", "logical_slot",
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
    parser.add_argument("--master-slot", type=int, action="append",
                        help="master slot to run; repeat as needed, default all")
    parser.add_argument("--repeats", type=int, default=1,
                        help="independent STOP/ARM/coded/STOP trials per master")
    parser.add_argument("--max-reject-ratio", type=float, default=0.0,
                        help="diagnostic repeat gate, in [0,1]; default requires all trials")
    parser.add_argument("--max-lag-span", type=int,
                        help="optional per-master accepted lag span gate in raw samples")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--action-timeout", type=float, default=0.25,
                        help=("bounded wait for coded START/STOP payload; "
                              "guarded snapshot remains the acceptance gate"))
    parser.add_argument("--coded-timeout", type=float, default=5.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--gap", type=float, default=0.2)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
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


def summarize_trials(trials: list[dict[str, object]],
                     max_reject_ratio: float,
                     max_lag_span: int | None) -> dict[str, object]:
    categories = Counter(trial_reject_category(trial) for trial in trials)
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
    return {
        "trial_count": len(trials),
        "accepted_count": len(accepted),
        "rejected_count": rejected_count,
        "accepted_ratio": len(accepted) / len(trials) if trials else 0.0,
        "reject_ratio": reject_ratio,
        "reject_categories": dict(sorted(categories.items())),
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


def summarize_by_master(trials: list[dict[str, object]],
                        max_reject_ratio: float,
                        max_lag_span: int | None) -> dict[str, object]:
    grouped: dict[int, list[dict[str, object]]] = {}
    for trial in trials:
        grouped.setdefault(int(trial["master_slot"]), []).append(trial)
    return {
        str(master_slot): {
            "master_no": master_slot + 1,
            "master_id": group[0]["master_id"],
            **summarize_trials(group, max_reject_ratio, max_lag_span),
        }
        for master_slot, group in sorted(grouped.items())
    }


def coded_status(board: Board, args: argparse.Namespace) -> dict[str, int]:
    raw = board_command(board, "READ:CALibration:CLOCk:CODEd?", args)
    return parse_csv_u32(raw, len(CODED_FIELDS))


def coded_action_args(args: argparse.Namespace) -> argparse.Namespace:
    fast = argparse.Namespace(**vars(args))
    fast.timeout = min(args.timeout, args.action_timeout)
    fast.settle = min(args.settle, 0.05)
    return fast


def start_coded(board: Board, args: argparse.Namespace) -> str:
    action_args = coded_action_args(args)
    response = board_command(
        board,
        "CALibration:CLOCk:CODEd:STARt "
        f"{args.codebook},{args.min_lag},{args.max_lag},"
        f"{args.max_distance},{args.min_margin}",
        action_args,
    )
    row = next(csv.reader([response]), [])
    expected = [args.codebook, args.min_lag, args.max_lag,
                args.max_distance, args.min_margin]
    try:
        actual = [int(value.strip().strip('"'), 0) for value in row]
    except ValueError:
        actual = []
    if actual != expected:
        # Persona switching may race the lower-priority CDC response flush.
        # Only accept that timeout when the guarded snapshot proves that this
        # exact request reached core1.
        snapshot = coded_status(board, args)
        if (snapshot["state"] == STATE_IDLE or
                snapshot["codebook_id"] != args.codebook or
                snapshot["coarse_min_sample"] != args.min_lag or
                snapshot["coarse_max_sample"] != args.max_lag or
                snapshot["train_sequence"] == 0):
            raise RuntimeError(
                f"{board.address}: coded START rejected: {response!r}, "
                f"snapshot={snapshot}")
        response = f"{response}; accepted_by_snapshot"
    return response


def prepare_ring(ordered: list[Board], master_slot: int,
                 args: argparse.Namespace) -> list[dict[str, object]]:
    actions: list[dict[str, object]] = []
    node_count = len(ordered)
    master = ordered[master_slot]
    start_order = [board for slot, board in enumerate(ordered)
                   if slot != master_slot] + [master]
    for board in ordered:
        actions.append({"board": board.address, "command": "STOP",
                        "response": board_command(board, "SYSTem:TDMA:RING:STOP", args)})
        actions.append({"board": board.address, "command": "OPMODE",
                        "response": board_command(board,
                            f"SYSTem:TDMA:OPMode:STAGe {args.level}", args)})
        actions.append({"board": board.address, "command": "OPMODE_APPLY",
                        "response": board_command(board, "SYSTem:TDMA:OPMode:APPLy", args)})
    for slot, board in enumerate(ordered):
        actions.append({"board": board.address, "command": "TOPOLOGY",
                        "response": board_command(
                            board,
                            f"SYSTem:TDMA:RING:TOPology "
                            f"{node_count},{slot},{master_slot}",
                            args)})
    for board in start_order:
        slot = ordered.index(board)
        last_error = ""
        for attempt in range(1, 4):
            response = board_command(board, "SYSTem:TDMA:RING:ARM", args)
            try:
                readback = wait_started(board, args)
                actions.append({"board": board.address, "command": "ARM",
                                "attempt": attempt, "response": response,
                                "status": readback})
                break
            except RuntimeError as exc:
                last_error = str(exc)
                actions.append({"board": board.address, "command": "ARM",
                                "attempt": attempt, "response": response,
                                "error": last_error})
                board_command(board, "SYSTem:TDMA:RING:STOP", args)
                board_command(
                    board,
                    f"SYSTem:TDMA:RING:TOPology "
                    f"{node_count},{slot},{master_slot}", args)
                time.sleep(args.gap)
        else:
            raise RuntimeError(
                f"{board.address}: ARM failed after retries: {last_error}")
    for board in ordered:
        actions.append({"board": board.address, "command": "STOP_AFTER_ARM",
                        "response": board_command(board, "SYSTem:TDMA:RING:STOP", args)})
    time.sleep(args.gap)
    return actions


def wait_master(board: Board, args: argparse.Namespace) -> dict[str, int]:
    deadline = time.monotonic() + args.coded_timeout
    last = coded_status(board, args)
    while time.monotonic() < deadline:
        if last["state"] in (STATE_ACCEPTED, STATE_REJECTED):
            return last
        time.sleep(0.05)
        last = coded_status(board, args)
    raise RuntimeError(f"{board.address}: coded completion timeout: {last}")


def stop_coded(ordered: list[Board], args: argparse.Namespace) -> None:
    action_args = coded_action_args(args)
    for board in ordered:
        board_command(board, "CALibration:CLOCk:CODEd:STOP", action_args)
    deadline = time.monotonic() + args.coded_timeout
    while time.monotonic() < deadline:
        if all(coded_status(board, args)["state"] == STATE_IDLE for board in ordered):
            return
        time.sleep(0.05)
    raise RuntimeError("coded STOP did not restore IDLE on every board")


def run_master(ordered: list[Board], master_slot: int, repeat_index: int,
               prepare: bool, args: argparse.Namespace) -> dict[str, object]:
    actions = prepare_ring(ordered, master_slot, args) if prepare else []
    master = ordered[master_slot]
    followers = [board for index, board in enumerate(ordered) if index != master_slot]
    started: list[dict[str, object]] = []
    try:
        for board in followers:
            response = start_coded(board, args)
            print(json.dumps({"coded_start": {"board": board.address,
                  "role": "follower", "response": response}}), flush=True)
            started.append({"board": board.address, "role": "follower",
                            "response": response,
                            "snapshot": coded_status(board, args)})
        response = start_coded(master, args)
        print(json.dumps({"coded_start": {"board": master.address,
              "role": "master", "response": response}}), flush=True)
        started.append({"board": master.address, "role": "master",
                        "response": response})
        master_snapshot = wait_master(master, args)
        result = {
            "master_slot": master_slot,
            "master_no": master_slot + 1,
            "master_id": master.address,
            "repeat_index": repeat_index,
            "snapshot": master_snapshot,
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
        writer.writerow(("master_no", "master_id", "repeat_index", "accepted",
                         "reject_category", "state",
                         "reject_reason", "best_lag_sample", "best_distance",
                         "second_distance", "margin", "flags", "error"))
        for item in trials:
            snapshot = item.get("snapshot", {})
            writer.writerow((item["master_no"], item["master_id"],
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
            "ring_prepare_once_per_master": True,
        },
        "boards": {board.address: asdict(board) for board in ordered},
    }
    print(json.dumps(plan, ensure_ascii=False, indent=2))
    if args.dry_run:
        return 0
    master_slots = (list(args.master_slot) if args.master_slot is not None
                    else list(range(len(ordered))))
    if (not master_slots or len(set(master_slots)) != len(master_slots) or
            any(slot < 0 or slot >= len(ordered) for slot in master_slots)):
        raise SystemExit("master-slot must select unique active slots")
    trials: list[dict[str, object]] = []
    for master_slot in master_slots:
        for repeat_index in range(1, args.repeats + 1):
            try:
                result = run_master(
                    ordered, master_slot, repeat_index,
                    prepare=repeat_index == 1, args=args)
            except Exception as exc:  # noqa: BLE001 - retain all repeat evidence
                error = f"{type(exc).__name__}: {exc}"
                result = {
                    "master_slot": master_slot,
                    "master_no": master_slot + 1,
                    "master_id": ordered[master_slot].address,
                    "repeat_index": repeat_index,
                    "accepted": False,
                    "snapshot": {},
                    "started": [],
                    "actions": [],
                    "error": error,
                }
                result["reject_category"] = trial_reject_category(result)
                try:
                    stop_coded(ordered, args)
                except Exception as stop_exc:  # noqa: BLE001
                    result["stop_error"] = (
                        f"{type(stop_exc).__name__}: {stop_exc}")
            trials.append(result)
            print(json.dumps({"trial_complete": {
                "master_no": result["master_no"],
                "master_id": result["master_id"],
                "repeat_index": result["repeat_index"],
                "accepted": result["accepted"],
                "reject_category": result["reject_category"],
                "snapshot": result["snapshot"],
                "error": result.get("error", ""),
            }}, ensure_ascii=False), flush=True)
    by_master = summarize_by_master(
        trials, args.max_reject_ratio, args.max_lag_span)
    overall = summarize_trials(
        trials, args.max_reject_ratio, max_lag_span=None)
    expected_trial_count = len(master_slots) * args.repeats
    passed = (len(trials) == expected_trial_count and
              all(bool(summary["passed"]) for summary in by_master.values()))
    output = {
        **plan,
        "passed": passed,
        "expected_trial_count": expected_trial_count,
        "trials": trials,
        "statistics_by_master": by_master,
        "statistics_overall": overall,
    }
    out_dir = args.out_dir or (ROOT / "build-product-release" /
                               f"calibration_clk_coded_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    write_csv(out_dir / "summary.csv", trials)
    (out_dir / "summary.txt").write_text(
        "\n".join(
            f"NO.{summary['master_no']} {summary['master_id']}: "
            f"accepted={summary['accepted_count']}/{summary['trial_count']} "
            f"reject_ratio={summary['reject_ratio']:.6f} "
            f"lag_histogram={summary['lag_histogram']} "
            f"lag_span={summary['lag_span']} "
            f"distance_mean={summary['best_distance']['mean']} "
            f"margin_min={summary['margin']['min']} "
            f"passed={summary['passed']}"
            for summary in by_master.values()) + "\n",
        encoding="utf-8")
    print(f"passed={passed} out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
