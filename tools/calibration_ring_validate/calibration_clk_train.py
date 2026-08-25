#!/usr/bin/env python3
"""Acquire the Calibration first-stage SPI CLK RTT bracket on a board ring.

Boards are discovered and verified exclusively by ``*IDN?`` unique address.
COM ports are transient transport handles and are never used as board identity.
The tool only orchestrates maintenance state; CLK forwarding and overlap
ordering remain resident in the firmware PIO/core1 TDMA owner path.
"""

from __future__ import annotations

import argparse
import csv
import json
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
    board_command,
    discover,
    status as ring_status,
    wait_started,
)


TRAIN_FIELDS = (
    "tag", "version", "state", "result", "role", "request_seq",
    "service_count", "baud_hz", "requested_cycles", "return_seen",
    "return_before_tx_done", "tx_sck_pin", "rx_sck_pin",
    "timestamp_resolution_ns", "timestamp_flags", "tx_start_ns_lo",
    "tx_start_ns_hi", "tx_done_observed_ns_lo",
    "tx_done_observed_ns_hi", "return_observed_ns_lo",
    "return_observed_ns_hi", "burst_duration_ns_lo",
    "burst_duration_ns_hi", "runtime_train_request_seq",
    "runtime_train_accepted_seq", "runtime_train_start_count",
    "runtime_train_reject_count", "runtime_training_dirty",
)

STATE_FORWARDING = 1
STATE_REFERENCE_COMPLETE = 3
STATE_ERROR = 4
RESULT_FORWARD_ARMED = 1
RESULT_RETURN_OVERLAP = 2
RESULT_NO_OVERLAP = 3


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help=("exact *IDN? address in accepted Calibration ring "
                              "order; repeat for every active board"))
    parser.add_argument("--expected-build")
    parser.add_argument("--level", type=int, default=7,
                        help=("operating profile applied while stopped; "
                              "default level 7 is the 10 MHz / 1 ms baseline"))
    parser.add_argument("--pulse-start", type=int, default=10)
    parser.add_argument("--growth-factor", type=int, default=10)
    parser.add_argument("--pulse-limit", type=int, default=65536)
    parser.add_argument("--binary-refine", action="store_true",
                        help="bisect the final non-overlap/overlap bracket")
    parser.add_argument("--repeats", type=int, default=1,
                        help="repeat every pulse-count decision point")
    parser.add_argument("--gap", type=float, default=0.2,
                        help="quiet gap between training epochs")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--train-timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def u64(values: dict[str, int | str], low: str, high: str) -> int:
    return int(values[low]) | (int(values[high]) << 32)


def train_status(board, args: argparse.Namespace) -> dict[str, int | str]:
    raw = board_command(board, "SYSTem:TDMA:RING:TRAIN:STATus?", args)
    row = next(csv.reader([raw]), [])
    if len(row) != len(TRAIN_FIELDS):
        raise RuntimeError(
            f"{board.address}: CLK train field count {len(row)}, "
            f"expected {len(TRAIN_FIELDS)}: {raw!r}")
    result: dict[str, int | str] = {"tag": row[0].strip().strip('"')}
    if result["tag"] != "CLKTRAIN":
        raise RuntimeError(f"{board.address}: invalid CLK train tag {row[0]!r}")
    for name, value in zip(TRAIN_FIELDS[1:], row[1:]):
        result[name] = int(value.strip().strip('"'), 0)
    result["tx_start_timestamp_ns"] = u64(
        result, "tx_start_ns_lo", "tx_start_ns_hi")
    result["tx_done_observed_timestamp_ns"] = u64(
        result, "tx_done_observed_ns_lo", "tx_done_observed_ns_hi")
    result["return_observed_timestamp_ns"] = u64(
        result, "return_observed_ns_lo", "return_observed_ns_hi")
    result["burst_duration_ns"] = u64(
        result, "burst_duration_ns_lo", "burst_duration_ns_hi")
    return result


def submit_train(board, cycles: int, args: argparse.Namespace) -> str:
    response = board_command(
        board, f"SYSTem:TDMA:RING:TRAIN {cycles}", args)
    if response.strip().strip('"') != str(cycles):
        raise RuntimeError(
            f"{board.address}: TRAIN {cycles} rejected: {response!r}")
    return response


def wait_train(board, previous_seq: int, terminal_states: set[int],
               args: argparse.Namespace) -> dict[str, int | str]:
    deadline = time.monotonic() + args.train_timeout
    last: dict[str, int | str] = {}
    while time.monotonic() < deadline:
        last = train_status(board, args)
        if (int(last["request_seq"]) != previous_seq and
                int(last["state"]) in terminal_states):
            return last
        time.sleep(0.02)
    raise RuntimeError(
        f"{board.address}: training state timeout, last={last}")


def arm_training_persona(ordered, reference_node: int,
                         args: argparse.Namespace) -> list[dict[str, object]]:
    actions: list[dict[str, object]] = []
    node_count = len(ordered)
    for board in ordered:
        actions.append({"board": board.address, "command": "STOP",
                        "response": board_command(
                            board, "SYSTem:TDMA:RING:STOP", args)})
    time.sleep(args.gap)
    for node, board in enumerate(ordered):
        command = (
            f"SYSTem:TDMA:RING:TOPology "
            f"{node_count},{node},{reference_node}")
        actions.append({"board": board.address, "command": command,
                        "response": board_command(board, command, args)})

    followers = [board for node, board in enumerate(ordered)
                 if node != reference_node]
    reference = ordered[reference_node]
    for board in followers + [reference]:
        node = ordered.index(board)
        arm_error = ""
        for attempt in range(1, 4):
            response = board_command(
                board, "SYSTem:TDMA:RING:ARM", args)
            try:
                readback = wait_started(board, args)
                if (readback["ring_node_count"] != node_count or
                        # TDMA reports RefMem/TDMA slot IDs at this boundary;
                        # map them explicitly to Calibration node indices.
                        readback["ring_local_slot_id"] != node or
                        readback["ring_reference_slot_id"] != reference_node):
                    raise RuntimeError(f"topology readback mismatch: {readback}")
                actions.append({"board": board.address, "command": "ARM",
                                "attempt": attempt, "response": response,
                                "readback": readback})
                break
            except RuntimeError as exc:
                arm_error = str(exc)
                actions.append({"board": board.address, "command": "ARM",
                                "attempt": attempt, "response": response,
                                "error": arm_error})
                board_command(board, "SYSTem:TDMA:RING:STOP", args)
                time.sleep(args.gap)
                topology = (
                    f"SYSTem:TDMA:RING:TOPology "
                    f"{node_count},{node},{reference_node}")
                board_command(board, topology, args)
                time.sleep(args.gap)
        else:
            raise RuntimeError(
                f"{board.address}: ARM failed after 3 attempts: {arm_error}")

    for board in followers:
        before = train_status(board, args)
        actions.append({"board": board.address, "command": "TRAIN_FORWARD",
                        "response": submit_train(board, 1, args)})
        after = wait_train(board, int(before["request_seq"]),
                           {STATE_FORWARDING, STATE_ERROR}, args)
        if (int(after["state"]) != STATE_FORWARDING or
                int(after["result"]) != RESULT_FORWARD_ARMED):
            raise RuntimeError(
                f"{board.address}: follower forwarding arm failed: {after}")
        actions[-1]["snapshot"] = after
    return actions


def measure_burst(reference, cycles: int,
                  args: argparse.Namespace) -> dict[str, object]:
    trials: list[dict[str, object]] = []
    for repeat_index in range(args.repeats):
        before = train_status(reference, args)
        response = submit_train(reference, cycles, args)
        snapshot = wait_train(reference, int(before["request_seq"]),
                              {STATE_REFERENCE_COMPLETE, STATE_ERROR}, args)
        result = int(snapshot["result"])
        if result not in (RESULT_RETURN_OVERLAP, RESULT_NO_OVERLAP):
            raise RuntimeError(
                f"{reference.address}: burst {cycles} failed: {snapshot}")
        trials.append({
            "repeat_index": repeat_index,
            "command_response": response,
            "overlap": result == RESULT_RETURN_OVERLAP,
            "snapshot": snapshot,
        })
        time.sleep(args.gap)
    overlap_count = sum(1 for trial in trials if bool(trial["overlap"]))
    classification = (
        "ALL_OVERLAP" if overlap_count == len(trials) else
        "ALL_NON_OVERLAP" if overlap_count == 0 else
        "MIXED")
    return {
        "cycles": cycles,
        "classification": classification,
        "overlap_count": overlap_count,
        "repeat_count": len(trials),
        "overlap": classification == "ALL_OVERLAP",
        "snapshot": trials[-1]["snapshot"],
        "trials": trials,
    }


def acquire_reference_node(ordered, reference_node: int,
                           args: argparse.Namespace) -> dict[str, object]:
    reference = ordered[reference_node]
    actions = arm_training_persona(ordered, reference_node, args)
    samples: list[dict[str, object]] = []
    n_low = 0
    n_high = 0
    mixed_cycles: list[int] = []
    cycles = args.pulse_start
    while cycles <= args.pulse_limit:
        sample = measure_burst(reference, cycles, args)
        samples.append(sample)
        if sample["classification"] == "MIXED":
            mixed_cycles.append(cycles)
        elif sample["classification"] == "ALL_OVERLAP":
            n_high = cycles
            break
        else:
            n_low = cycles
        next_cycles = cycles * args.growth_factor
        cycles = min(next_cycles, args.pulse_limit)
        if cycles == n_low:
            break

    if n_high == 0:
        raise RuntimeError(
            f"{reference.address}: no overlap through {args.pulse_limit} pulses")

    if args.repeats > 1 and n_low + 1 < n_high:
        sampled_cycles = {int(sample["cycles"]) for sample in samples}
        for candidate in range(n_low + 1, n_high):
            if candidate in sampled_cycles:
                sample = next(item for item in samples
                              if int(item["cycles"]) == candidate)
            else:
                sample = measure_burst(reference, candidate, args)
                samples.append(sample)
            if sample["classification"] == "ALL_NON_OVERLAP":
                n_low = candidate
            elif sample["classification"] == "ALL_OVERLAP":
                n_high = candidate
                break
            else:
                mixed_cycles.append(candidate)
    elif args.binary_refine and n_low + 1 < n_high:
        while n_low + 1 < n_high:
            midpoint = (n_low + n_high) // 2
            sample = measure_burst(reference, midpoint, args)
            samples.append(sample)
            if sample["classification"] == "MIXED":
                mixed_cycles.append(midpoint)
                break
            elif sample["classification"] == "ALL_OVERLAP":
                n_high = midpoint
            else:
                n_low = midpoint

    low_duration_ns = 0
    high_duration_ns = 0
    for sample in samples:
        duration = int(sample["snapshot"]["burst_duration_ns"])
        if int(sample["cycles"]) == n_low:
            low_duration_ns = duration
        if int(sample["cycles"]) == n_high:
            high_duration_ns = duration

    return {
        "reference_node": reference_node,
        "reference_node_no": reference_node + 1,
        "reference_node_id": reference.address,
        "baud_hz": int(samples[-1]["snapshot"]["baud_hz"]),
        "n_low": n_low,
        "n_high": n_high,
        "burst_duration_low_ns": low_duration_ns,
        "burst_duration_high_ns": high_duration_ns,
        "mixed_cycles": sorted(set(mixed_cycles)),
        "timestamp_flags": int(samples[-1]["snapshot"]["timestamp_flags"]),
        "dpll_eligible": False,
        "actions": actions,
        "samples": samples,
    }


def write_csv(path: Path,
              reference_nodes: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.writer(handle)
        writer.writerow(("reference_node_no", "reference_node",
                         "reference_node_id", "baud_hz",
                         "n_low", "n_high", "burst_duration_low_ns",
                         "burst_duration_high_ns", "mixed_cycles",
                         "dpll_eligible"))
        for item in reference_nodes:
            writer.writerow((item["reference_node_no"],
                             item["reference_node"],
                             item["reference_node_id"], item["baud_hz"],
                             item["n_low"], item["n_high"],
                             item["burst_duration_low_ns"],
                             item["burst_duration_high_ns"],
                             ";".join(str(value)
                                      for value in item["mixed_cycles"]),
                             int(bool(item["dpll_eligible"]))))


def main() -> int:
    args = parse_args()
    board_ids = list(args.board_id)
    if len(board_ids) < 2 or len(board_ids) > 8:
        raise SystemExit("board count must be in [2, 8]")
    if len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique")
    if (args.pulse_start < 1 or args.pulse_start > args.pulse_limit or
            args.pulse_limit > 65536 or args.growth_factor < 2 or
            args.repeats < 1):
        raise SystemExit(
            "require 1 <= pulse-start <= pulse-limit <= 65536, "
            "growth-factor >= 2, and repeats >= 1")
    args.board_ids = board_ids

    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(
            f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    ordered = [boards[address] for address in board_ids]
    if args.expected_build:
        wrong = {board.address: board.build for board in ordered
                 if board.build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")

    plan = {
        "board_ids_in_physical_order": board_ids,
        "level": args.level,
        "pulse_start": args.pulse_start,
        "growth_factor": args.growth_factor,
        "pulse_limit": args.pulse_limit,
        "binary_refine": args.binary_refine,
        "repeats": args.repeats,
        "boards": {board.address: asdict(board) for board in ordered},
    }
    print(json.dumps(plan, ensure_ascii=False, indent=2))
    if args.dry_run:
        return 0

    for board in ordered:
        board_command(board, "SYSTem:TDMA:RING:STOP", args)
        board_command(board,
                      f"SYSTem:TDMA:OPMode:STAGe {args.level}", args)
        board_command(board, "SYSTem:TDMA:OPMode:APPLy", args)

    reference_nodes: list[dict[str, object]] = []
    passed = False
    error = ""
    try:
        for reference_node in range(len(ordered)):
            result = acquire_reference_node(ordered, reference_node, args)
            reference_nodes.append(result)
            print(json.dumps({"reference_node_complete": {
                key: result[key] for key in (
                    "reference_node_no", "reference_node_id", "baud_hz",
                    "n_low", "n_high",
                    "burst_duration_low_ns", "burst_duration_high_ns",
                    "mixed_cycles")
            }}, ensure_ascii=False))
        passed = len(reference_nodes) == len(ordered)
    except Exception as exc:  # noqa: BLE001 - persist partial HIL evidence
        error = f"{type(exc).__name__}: {exc}"
    finally:
        for board in ordered:
            try:
                board_command(board, "SYSTem:TDMA:RING:STOP", args)
            except Exception:  # noqa: BLE001 - best-effort bench cleanup
                pass

    output = {
        "measurement_domain": "calibration",
        "passed": passed,
        "phase": "spi_clk_round_trip_acquisition_bracket",
        "measurement_semantics": "diagnostic overlap bracket, not DPLL eligible",
        "plan": plan,
        "reference_nodes": reference_nodes,
        "error": error,
    }
    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"calibration_clk_train_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    write_csv(out_dir / "summary.csv", reference_nodes)
    (out_dir / "summary.txt").write_text(
        "\n".join(
            f"Node{item['reference_node_no'] - 1} "
            f"{item['reference_node_id']}: "
            f"N_low={item['n_low']} N_high={item['n_high']} "
            f"duration_ns=[{item['burst_duration_low_ns']},"
            f"{item['burst_duration_high_ns']})"
            for item in reference_nodes) + "\n",
        encoding="utf-8")
    print(json.dumps({
        "passed": passed,
        "error": error,
        "reference_node_count": len(reference_nodes),
        "summary": [
            {key: item[key] for key in (
                "reference_node_no", "reference_node_id", "baud_hz",
                "n_low", "n_high",
                "burst_duration_low_ns", "burst_duration_high_ns",
                "mixed_cycles")}
            for item in reference_nodes
        ],
    }, ensure_ascii=False, indent=2))
    print(f"out_dir={out_dir}")
    if error:
        print(error, file=sys.stderr)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
