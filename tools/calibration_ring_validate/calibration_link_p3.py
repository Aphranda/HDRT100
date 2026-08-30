#!/usr/bin/env python3
"""Run Calibration P3 per-link bidirectional ranging on a 2..8 board ring.

Board identity is always the exact ``*IDN?`` unique address. COM names are
only transient transport endpoints. Each validation runs the complete
10/25/30 MHz ladder. 30 MHz is bounded diagnostic RX and never a stable
profile; it is still exercised when a lower stable level fails.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
import time
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
)
from calibration_ring_validate.calibration_link_frequency_policy import (  # noqa: E402
    LIMITED_RX_FALLBACK_MHZ,
    LIMITED_RX_FREQUENCY_MHZ,
    validation_frequency_ladder,
)


P3_FIELDS = (
    "state", "role", "signal_group", "flags", "reject_reason", "baud_hz", "epoch",
    "sample_period_ns", "pulse_count", "requested_words", "produced_words",
    "edge_mask", "dma_overrun_count", "pio_stall_count", "clock_high_ns",
    "clock_low_ns", "data_high_ns", "t1_lo", "t1_hi", "t2_lo", "t2_hi",
    "t3_lo", "t3_hi", "t4_lo", "t4_hi", "result_valid",
    "data_pulse_count",
)
P3_V1_FIELDS = (
    "state", "role", "signal_group", "flags", "reject_reason", "baud_hz", "epoch",
    "sample_period_ns", "pulse_count", "requested_words", "produced_words",
    "edge_mask", "dma_overrun_count", "pio_stall_count", "clock_high_ns",
    "clock_low_ns", "data_high_ns", "t1_lo", "t1_hi", "t2_lo", "t2_hi",
    "t3_lo", "t3_hi", "t4_lo", "t4_hi", "result_valid",
)
P3_LEGACY_FIELDS = (
    "state", "role", "flags", "reject_reason", "baud_hz", "epoch",
    "sample_period_ns", "pulse_count", "requested_words", "produced_words",
    "edge_mask", "dma_overrun_count", "pio_stall_count", "clock_high_ns",
    "clock_low_ns", "data_high_ns", "t1_lo", "t1_hi", "t2_lo", "t2_hi",
    "t3_lo", "t3_hi", "t4_lo", "t4_hi", "result_valid",
)
P3_STATE_IDLE = 0
P3_STATE_ARMED = 1
P3_STATE_COMPLETE = 2
P3_STATE_ERROR = 3
P3_ROLE_INITIATOR = 1
P3_ROLE_RESPONDER = 2
P3_FLAGS_REQUIRED = 0x0F
P3_GROUP_CLK_DATA = 0
P3_GROUP_CS_DATA = 1
P3_GROUP_NAMES = {
    "CLK_DATA": P3_GROUP_CLK_DATA,
    "CS_DATA": P3_GROUP_CS_DATA,
}
def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", default=[],
                        help="exact *IDN? address in physical ring order")
    parser.add_argument("--expected-build")
    parser.add_argument("--frequency-mhz", action="append", type=int)
    parser.add_argument("--signal-group", choices=("CLK_DATA", "CS_DATA", "BOTH"),
                        default="BOTH")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--pulse-count", type=int, default=32)
    parser.add_argument("--capture-words", type=int, default=256)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--action-timeout", type=float, default=1.0,
                        help=("bounded wait for P3 action acknowledgement; "
                              "capture completion uses --capture-timeout"))
    parser.add_argument("--capture-timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--gap", type=float, default=0.1)
    parser.add_argument("--frequency-tolerance-percent", type=float,
                        default=5.0)
    parser.add_argument("--duty-tolerance-percent", type=float, default=10.0)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--replay-summary", type=Path,
                        help=("re-evaluate a saved P3 summary with the current "
                              "gate; requires --out-dir and performs no I/O"))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--short-open", action="store_true",
                        help="open/close CDC for every command (diagnostic fallback)")
    return parser.parse_args()


def parse_p3_status(raw: str) -> dict[str, int]:
    row = next(csv.reader([raw]), [])
    fields_by_count = {
        len(P3_FIELDS): P3_FIELDS,
        len(P3_V1_FIELDS): P3_V1_FIELDS,
        len(P3_LEGACY_FIELDS): P3_LEGACY_FIELDS,
    }
    fields = fields_by_count.get(len(row))
    if fields is None:
        raise RuntimeError(
            f"P3 status field count {len(row)} not in "
            f"{tuple(sorted(fields_by_count))}: {raw!r}")
    values = [int(value.strip().strip('"'), 0) for value in row]
    result = dict(zip(fields, values))
    result.setdefault("signal_group", P3_GROUP_CLK_DATA)
    result.setdefault("data_pulse_count", 1)
    for prefix in ("t1", "t2", "t3", "t4"):
        result[prefix + "_ns"] = (
            result[prefix + "_lo"] | (result[prefix + "_hi"] << 32))
    return result


def p3_status(board: Board, args: argparse.Namespace) -> dict[str, int]:
    return parse_p3_status(
        board_command(board, "READ:CALibration:P3?", args))


def action_args(args: argparse.Namespace) -> argparse.Namespace:
    fast = argparse.Namespace(**vars(args))
    fast.timeout = min(args.timeout, args.action_timeout)
    fast.settle = min(args.settle, 0.05)
    return fast


def stop_ring_and_wait(board: Board, args: argparse.Namespace) -> dict[str, int]:
    board_command(board, "SYSTem:TDMA:RING:STOP", args)
    deadline = time.monotonic() + args.capture_timeout
    last: dict[str, int] = {}
    while time.monotonic() < deadline:
        last = ring_status(board, args)
        if (last["ring_enabled"] == 0 and
                last["ring_adapter_started"] == 0):
            return last
        time.sleep(0.03)
    raise RuntimeError(
        f"{board.address}: TDMA ring STOP was not acknowledged: {last}")


def stop_p3(boards: list[Board], args: argparse.Namespace) -> None:
    fast = action_args(args)
    for board in boards:
        board_command(board, "CALibration:P3:STOP", fast)
    # STOP is a core0->core1 intent.  An old IDLE snapshot does not acknowledge
    # that the new STOP sequence has been consumed, so never accept the first
    # stale readback in the same host turn.
    time.sleep(min(0.01, max(0.001, args.gap)))
    deadline = time.monotonic() + args.capture_timeout
    while time.monotonic() < deadline:
        if all(p3_status(board, args)["state"] == P3_STATE_IDLE
               for board in boards):
            return
        time.sleep(0.03)
    raise RuntimeError("P3 STOP did not restore IDLE")


def start_p3(board: Board, role: int, frequency_hz: int, epoch: int,
             signal_group: int, args: argparse.Namespace) -> dict[str, int]:
    fast = action_args(args)
    command = (
        f"CALibration:P3:STARt {role},{frequency_hz},"
        f"{args.pulse_count},{args.capture_words},{epoch},{signal_group}")
    response = board_command(board, command, fast)
    deadline = time.monotonic() + args.capture_timeout
    last: dict[str, int] = {}
    while time.monotonic() < deadline:
        last = p3_status(board, args)
        if (last["epoch"] == epoch and last["role"] == role and
                last["baud_hz"] == frequency_hz and
                last["state"] in (P3_STATE_ARMED, P3_STATE_COMPLETE,
                                  P3_STATE_ERROR)):
            last["start_response"] = response
            return last
        time.sleep(0.03)
    raise RuntimeError(
        f"{board.address}: P3 START not accepted, response={response!r}, "
        f"snapshot={last}")


def wait_complete(board: Board, epoch: int,
                  args: argparse.Namespace) -> dict[str, int]:
    deadline = time.monotonic() + args.capture_timeout
    last = p3_status(board, args)
    while time.monotonic() < deadline:
        if last["epoch"] == epoch and last["state"] != P3_STATE_ARMED:
            return last
        time.sleep(0.03)
        last = p3_status(board, args)
    raise RuntimeError(f"{board.address}: P3 completion timeout: {last}")


def timing_metrics(snapshot: dict[str, int], target_hz: int,
                   frequency_tolerance_percent: float,
                   duty_tolerance_percent: float,
                   signal_group: int = P3_GROUP_CLK_DATA) -> dict[str, object]:
    high_ns = snapshot["clock_high_ns"]
    low_ns = snapshot["clock_low_ns"]
    period_ns = high_ns + low_ns
    actual_hz = 1_000_000_000.0 / period_ns if period_ns > 0 else 0.0
    duty_percent = 100.0 * high_ns / period_ns if period_ns > 0 else 0.0
    frequency_error_percent = (
        100.0 * abs(actual_hz - target_hz) / target_hz)
    sample_period_ns = max(1, snapshot.get("sample_period_ns", 4))
    ideal_data_high_ns = 1_000_000_000.0 / (2.0 * target_hz)
    expected_data_high_ns = round(
        ideal_data_high_ns / sample_period_ns) * sample_period_ns
    data_high_error_ns = abs(
        snapshot["data_high_ns"] - expected_data_high_ns)
    # The selected forward line is clock-like in both personas.  CS_DATA
    # swaps the physical labels and uses CLK only as sync; it still has the
    # same frequency/duty gate as CLK_DATA.
    primary_timing_valid = True
    pulse_count = max(1, snapshot.get("pulse_count", 1))
    if target_hz == LIMITED_RX_FREQUENCY_MHZ * 1_000_000:
        minimum_data_pulse_count = max(4, (pulse_count + 1) // 2)
        data_burst_gate = "LIMITED_RX_HALF_BURST"
    else:
        minimum_data_pulse_count = max(4, (9 * pulse_count + 9) // 10)
        data_burst_gate = "STABLE_NINETY_PERCENT_BURST"
    data_pulse_count = snapshot.get("data_pulse_count", 1)
    return {
        "clock_high_ns": high_ns,
        "clock_low_ns": low_ns,
        "actual_hz": actual_hz,
        "frequency_error_percent": frequency_error_percent,
        "duty_percent": duty_percent,
        "data_high_ns": snapshot["data_high_ns"],
        "data_pulse_count": data_pulse_count,
        "minimum_data_pulse_count": minimum_data_pulse_count,
        "data_burst_gate": data_burst_gate,
        "data_burst_coverage_percent": (
            100.0 * data_pulse_count / pulse_count),
        "expected_data_high_ns": expected_data_high_ns,
        "data_high_error_ns": data_high_error_ns,
        "frequency_ok": (not primary_timing_valid or
                          frequency_error_percent <= frequency_tolerance_percent),
        "duty_ok": (not primary_timing_valid or
                     abs(duty_percent - 50.0) <= duty_tolerance_percent),
        "primary_timing_valid": primary_timing_valid,
        "data_high_ok": data_high_error_ns <= sample_period_ns,
        "data_burst_ok": data_pulse_count >= minimum_data_pulse_count,
    }


def evaluate_pair(initiator: dict[str, int], responder: dict[str, int],
                  target_hz: int, args: argparse.Namespace,
                  signal_group: int = P3_GROUP_CLK_DATA) -> dict[str, object]:
    source_rtt_ns = initiator["t4_ns"] - initiator["t1_ns"]
    residence_ns = responder["t3_ns"] - responder["t2_ns"]
    path_sum_ns = source_rtt_ns - residence_ns
    source_timing = timing_metrics(
        initiator, target_hz, args.frequency_tolerance_percent,
        args.duty_tolerance_percent, signal_group)
    responder_timing = timing_metrics(
        responder, target_hz, args.frequency_tolerance_percent,
        args.duty_tolerance_percent, signal_group)
    programmed_data_high_ns = int(source_timing["expected_data_high_ns"])
    source_timing["programmed_data_high_ns"] = programmed_data_high_ns
    source_timing["expected_data_high_ns"] = responder["data_high_ns"]
    source_timing["data_high_error_ns"] = abs(
        initiator["data_high_ns"] - responder["data_high_ns"])
    source_timing["data_high_ok"] = (
        source_timing["data_high_error_ns"] <=
        max(1, initiator.get("sample_period_ns", 4)))
    source_timing["data_width_reference"] = "responder_local_observed"
    responder_timing["data_width_reference"] = "pio_programmed"
    # edge_mask is the logical t1/t2/t3/t4 mask, independent of GPIO numbers.
    required_initiator_edges = 0x09
    required_responder_edges = 0x06
    failures: list[str] = []
    for name, snapshot, role, edge_mask in (
            ("initiator", initiator, P3_ROLE_INITIATOR,
             required_initiator_edges),
            ("responder", responder, P3_ROLE_RESPONDER,
             required_responder_edges)):
        if snapshot["state"] != P3_STATE_COMPLETE:
            failures.append(name + "_state")
        if snapshot["role"] != role:
            failures.append(name + "_role")
        if snapshot.get("signal_group", P3_GROUP_CLK_DATA) != signal_group:
            failures.append(name + "_signal_group")
        if snapshot["result_valid"] != 1:
            failures.append(name + "_result")
        if (snapshot["flags"] & P3_FLAGS_REQUIRED) != P3_FLAGS_REQUIRED:
            failures.append(name + "_flags")
        if (snapshot["edge_mask"] & edge_mask) != edge_mask:
            failures.append(name + "_edges")
        if snapshot["dma_overrun_count"] != 0:
            failures.append(name + "_dma")
        if snapshot["pio_stall_count"] != 0:
            failures.append(name + "_pio_stall")
    if initiator["epoch"] != responder["epoch"]:
        failures.append("epoch")
    if source_rtt_ns <= 0:
        failures.append("rtt")
    if residence_ns <= 0:
        failures.append("residence")
    if path_sum_ns < 0:
        failures.append("path_sum")
    if not source_timing["frequency_ok"]:
        failures.append("initiator_frequency")
    if not source_timing["duty_ok"]:
        failures.append("initiator_duty")
    if not source_timing["data_high_ok"]:
        failures.append("initiator_data_width")
    if not source_timing["data_burst_ok"]:
        failures.append("initiator_data_burst")
    if not responder_timing["frequency_ok"]:
        failures.append("responder_frequency")
    if not responder_timing["duty_ok"]:
        failures.append("responder_duty")
    if not responder_timing["data_high_ok"]:
        failures.append("responder_data_width")
    if not responder_timing["data_burst_ok"]:
        failures.append("responder_data_burst")
    return {
        "source_rtt_ns": source_rtt_ns,
        "residence_ns": residence_ns,
        "path_sum_ns": path_sum_ns,
        "delay_estimate_ns": path_sum_ns / 2.0,
        "signal_group": signal_group,
        "initiator_timing": source_timing,
        "responder_timing": responder_timing,
        "failures": failures,
        "passed": not failures,
    }


def run_trial(source: Board, destination: Board, frequency_hz: int,
              epoch: int, repeat_index: int, signal_group: int,
              args: argparse.Namespace) -> dict[str, object]:
    stop_p3([source, destination], args)
    try:
        responder_start = start_p3(
            destination, P3_ROLE_RESPONDER, frequency_hz, epoch,
            signal_group, args)
        initiator_start = start_p3(
            source, P3_ROLE_INITIATOR, frequency_hz, epoch,
            signal_group, args)
        initiator = wait_complete(source, epoch, args)
        responder = wait_complete(destination, epoch, args)
        evaluation = evaluate_pair(initiator, responder, frequency_hz, args,
                                   signal_group)
        return {
            "source": source.address,
            "destination": destination.address,
            "frequency_hz": frequency_hz,
            "epoch": epoch,
            "repeat_index": repeat_index,
            "signal_group": signal_group,
            "responder_start": responder_start,
            "initiator_start": initiator_start,
            "initiator": initiator,
            "responder": responder,
            **evaluation,
        }
    finally:
        stop_p3([source, destination], args)


def summarize_trials(trials: list[dict[str, object]]) -> dict[str, object]:
    accepted = [trial for trial in trials if trial.get("passed")]
    delays = [float(trial["delay_estimate_ns"]) for trial in accepted]
    return {
        "trial_count": len(trials),
        "accepted_count": len(accepted),
        "delay_min_ns": min(delays) if delays else None,
        "delay_max_ns": max(delays) if delays else None,
        "delay_mean_ns": statistics.fmean(delays) if delays else None,
        "delay_stddev_ns": statistics.pstdev(delays) if delays else None,
        "passed": bool(trials) and len(accepted) == len(trials),
    }


def apply_frequency_policy(ladder: list[dict[str, object]]) -> dict[str, object]:
    """Classify 30 MHz as bounded diagnostic RX, never a stable profile."""
    stable_rows: list[dict[str, object]] = []
    limited_rows: list[dict[str, object]] = []
    for row in ladder:
        frequency_mhz = int(row["frequency_mhz"])
        if frequency_mhz == LIMITED_RX_FREQUENCY_MHZ:
            row["required_for_stable"] = False
            row["operational_class"] = "LIMITED_RX"
            row["fallback_frequency_mhz"] = LIMITED_RX_FALLBACK_MHZ
            row["operational_status"] = (
                "LIMITED_RX_ACCEPTED" if row.get("passed")
                else "FALLBACK_25MHZ")
            limited_rows.append(row)
        else:
            row["required_for_stable"] = True
            row["operational_class"] = "STABLE_REQUIRED"
            row["fallback_frequency_mhz"] = None
            row["operational_status"] = (
                "STABLE_ACCEPTED" if row.get("passed") else "STABLE_REJECTED")
            stable_rows.append(row)
    stable_passed = bool(stable_rows) and all(
        bool(row.get("passed")) for row in stable_rows)
    stable_frequencies = sorted({
        int(row["frequency_mhz"]) for row in stable_rows
    })
    accepted_stable_frequencies = [
        frequency for frequency in stable_frequencies
        if all(bool(row.get("passed")) for row in stable_rows
               if int(row["frequency_mhz"]) == frequency)
    ]
    limited_rx_executed = bool(limited_rows) and all(
        not bool(row.get("skipped")) for row in limited_rows)
    limited_rx_passed = limited_rx_executed and all(
        bool(row.get("passed")) for row in limited_rows)
    return {
        "stable_profiles_passed": stable_passed,
        "highest_stable_frequency_mhz": (
            max(accepted_stable_frequencies)
            if accepted_stable_frequencies else None),
        "limited_rx_frequency_mhz": LIMITED_RX_FREQUENCY_MHZ,
        "limited_rx_fallback_mhz": LIMITED_RX_FALLBACK_MHZ,
        "limited_rx_executed": limited_rx_executed,
        "limited_rx_all_trials_passed": limited_rx_passed,
        "limited_rx_operational_status": (
            "LIMITED_RX_ACCEPTED" if limited_rx_passed
            else "FALLBACK_25MHZ"),
    }


def replay_saved_summary(source: dict[str, object],
                         args: argparse.Namespace) -> dict[str, object]:
    raw_trials = source.get("trials")
    raw_ladder = source.get("ladder")
    if not isinstance(raw_trials, list) or not isinstance(raw_ladder, list):
        raise ValueError("saved P3 summary is missing trials or ladder")
    trials: list[dict[str, object]] = []
    for raw in raw_trials:
        if not isinstance(raw, dict):
            raise ValueError("saved P3 trial is not an object")
        initiator = raw.get("initiator")
        responder = raw.get("responder")
        if not isinstance(initiator, dict) or not isinstance(responder, dict):
            trials.append({**raw, "passed": False,
                           "failures": ["saved_snapshot_missing"]})
            continue
        evaluation = evaluate_pair(
            initiator, responder, int(raw["frequency_hz"]), args,
            int(raw.get("signal_group", P3_GROUP_CLK_DATA)))
        trials.append({**raw, **evaluation})
    ladder: list[dict[str, object]] = []
    for raw in raw_ladder:
        if not isinstance(raw, dict):
            raise ValueError("saved P3 ladder row is not an object")
        selected = [trial for trial in trials
                    if trial.get("source") == raw.get("source") and
                    trial.get("destination") == raw.get("destination") and
                    int(trial.get("signal_group", P3_GROUP_CLK_DATA)) ==
                    int(raw.get("signal_group", P3_GROUP_CLK_DATA)) and
                    int(trial.get("frequency_hz", 0)) ==
                    int(raw["frequency_mhz"]) * 1_000_000]
        ladder.append({
            "link_index": int(raw["link_index"]),
            "source": raw["source"],
            "destination": raw["destination"],
            "signal_group": int(raw.get("signal_group", P3_GROUP_CLK_DATA)),
            "frequency_mhz": int(raw["frequency_mhz"]),
            **summarize_trials(selected),
        })
    frequency_policy = apply_frequency_policy(ladder)
    plan_fields = (
        "measurement_domain", "phase", "diagnostic_only",
        "board_ids_in_physical_order", "boards", "frequency_ladder_mhz",
        "repeats", "pulse_count", "capture_words", "signal_groups",
    )
    return {
        **{field: source[field] for field in plan_fields if field in source},
        "gate_replay": True,
        "passed": bool(frequency_policy["stable_profiles_passed"]),
        "frequency_policy": frequency_policy,
        "ladder": ladder,
        "trials": trials,
    }


def write_summary(output: dict[str, object], out_dir: Path) -> None:
    ladder = output["ladder"]
    if not isinstance(ladder, list):
        raise ValueError("P3 output ladder is invalid")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(output, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (out_dir / "summary.txt").write_text("\n".join(
        f"{row['source']} -> {row['destination']} "
        f"group={('CLK_DATA' if row.get('signal_group') == P3_GROUP_CLK_DATA else 'CS_DATA')} "
        f"{row['frequency_mhz']}MHz "
        f"accepted={row.get('accepted_count', 0)}/{row.get('trial_count', 0)} "
        f"delay_mean_ns={row.get('delay_mean_ns')} passed={row['passed']} "
        f"class={row.get('operational_class')} "
        f"status={row.get('operational_status')}"
        for row in ladder) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    if args.replay_summary is not None:
        if args.out_dir is None:
            raise SystemExit("--replay-summary requires --out-dir")
        source_path = args.replay_summary.resolve()
        if not source_path.is_file():
            raise SystemExit(f"saved P3 summary not found: {source_path}")
        if args.out_dir.resolve() == source_path.parent:
            raise SystemExit("replay output must not overwrite source evidence")
        source = json.loads(source_path.read_text(encoding="utf-8"))
        try:
            output = replay_saved_summary(source, args)
        except (KeyError, TypeError, ValueError) as exc:
            raise SystemExit(f"invalid saved P3 summary: {exc}") from exc
        output["replayed_from"] = str(source_path)
        write_summary(output, args.out_dir)
        print(f"passed={output['passed']} out_dir={args.out_dir}")
        return 0 if output["passed"] else 1
    try:
        frequencies_mhz = validation_frequency_ladder(args.frequency_mhz)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if not 2 <= len(args.board_id) <= 8:
        raise SystemExit("board count must be in [2, 8]")
    if len(set(args.board_id)) != len(args.board_id):
        raise SystemExit("board IDs must be unique")
    if not 1 <= args.repeats <= 1000:
        raise SystemExit("repeats must be in [1, 1000]")
    if not 4 <= args.pulse_count <= 1024:
        raise SystemExit("pulse-count must be in [4, 1024]")
    if not 1 <= args.capture_words <= 256:
        raise SystemExit("capture-words must be in [1, 256]")
    args.board_ids = list(args.board_id)
    args.keep_open = not args.short_open
    boards = discover(args)
    missing = sorted(set(args.board_id) - set(boards))
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(missing)}")
    ordered = [boards[address] for address in args.board_id]
    wrong_build = {board.address: board.build for board in ordered
                   if args.expected_build and board.build != args.expected_build}
    if wrong_build:
        raise SystemExit(f"build mismatch: {wrong_build}")
    plan = {
        "measurement_domain": "calibration",
        "phase": "p3_per_link_bidirectional",
        "diagnostic_only": True,
        "board_ids_in_physical_order": args.board_id,
        "boards": {board.address: asdict(board) for board in ordered},
        "frequency_ladder_mhz": frequencies_mhz,
        "repeats": args.repeats,
        "pulse_count": args.pulse_count,
        "capture_words": args.capture_words,
        "signal_groups": ([P3_GROUP_CLK_DATA, P3_GROUP_CS_DATA]
                          if args.signal_group == "BOTH"
                          else [P3_GROUP_NAMES[args.signal_group]]),
    }
    if args.dry_run:
        print(json.dumps(plan, ensure_ascii=False, indent=2))
        return 0
    plan["execution_mode"] = "TDMA_STOPPED_OFFLINE_CALIBRATION"
    plan["realtime_load_mask_modified"] = False
    trials: list[dict[str, object]] = []
    ladder: list[dict[str, object]] = []
    try:
        for board in ordered:
            stop_ring_and_wait(board, args)
        # P3 owns an offline Core1 calibration session while TDMA is stopped.
        # It must not depend on, mutate, or unquarantine the realtime load mask.
        time.sleep(args.gap)
        epoch = int(time.time()) & 0xFFFFFFFF
        signal_groups = plan["signal_groups"]
        for link_index, source in enumerate(ordered):
            destination = ordered[(link_index + 1) % len(ordered)]
            for signal_group in signal_groups:
                for frequency_mhz in frequencies_mhz:
                    level_trials: list[dict[str, object]] = []
                    for repeat_index in range(1, args.repeats + 1):
                        epoch = (epoch + 1) & 0xFFFFFFFF
                        try:
                            trial = run_trial(
                                source, destination,
                                frequency_mhz * 1_000_000,
                                epoch, repeat_index, signal_group, args)
                        except Exception as exc:  # retain evidence and continue gate
                            trial = {
                                "source": source.address,
                                "destination": destination.address,
                                "frequency_hz": frequency_mhz * 1_000_000,
                                "epoch": epoch,
                                "repeat_index": repeat_index,
                                "signal_group": signal_group,
                                "passed": False,
                                "error": f"{type(exc).__name__}: {exc}",
                            }
                        trials.append(trial)
                        level_trials.append(trial)
                        print(json.dumps({"p3_trial": trial},
                                         ensure_ascii=False), flush=True)
                        time.sleep(args.gap)
                    summary = summarize_trials(level_trials)
                    ladder.append({
                        "link_index": link_index,
                        "source": source.address,
                        "destination": destination.address,
                        "signal_group": signal_group,
                        "frequency_mhz": frequency_mhz,
                        **summary,
                    })
    finally:
        for board in ordered:
            stop_p3([board], args)
    frequency_policy = apply_frequency_policy(ladder)
    passed = bool(frequency_policy["stable_profiles_passed"])
    output = {
        **plan,
        "passed": passed,
        "frequency_policy": frequency_policy,
        "ladder": ladder,
        "trials": trials,
    }
    out_dir = args.out_dir or (ROOT / "build-product-release" /
        f"calibration_link_p3_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    write_summary(output, out_dir)
    print(f"passed={passed} out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
