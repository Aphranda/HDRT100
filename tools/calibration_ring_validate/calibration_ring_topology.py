#!/usr/bin/env python3
"""Measure Calibration link adjacency and ring order with TDMA probes."""

from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
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
    close_persistent_connections,
    discover,
    status,
    train,
    wait_started,
)
from tdma_frequency_sweep import snapshot  # noqa: E402
from calibration_ring_validate.calibration_timeout_config import (  # noqa: E402
    DEFAULT_ACTION_TIMEOUT_S,
    DEFAULT_PHASE_GAP_S,
    DEFAULT_SERIAL_SETTLE_S,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address; repeat for 2..8 boards")
    parser.add_argument("--anchor-id", "--reference-id", dest="anchor_id",
                        help=("*IDN? address used as NO.1 when rendering the "
                              "accepted calibration ring order"))
    parser.add_argument("--expected-build")
    parser.add_argument("--level", type=int, default=7,
                        help=("operating profile applied while stopped; "
                              "default level 7 is the 10 MHz / 1 ms "
                              "Calibration baseline"))
    parser.add_argument("--cycles", type=int, default=512)
    parser.add_argument("--train-chunk-cycles", type=int, default=0,
                        help=("split clock training into bounded chunks; "
                              "0 sends one command"))
    parser.add_argument("--pair-wait", type=float, default=1.5)
    parser.add_argument("--min-rx-frames", type=int, default=10)
    parser.add_argument("--min-rx-words", type=int, default=8)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--action-timeout", type=float,
                        default=DEFAULT_ACTION_TIMEOUT_S,
                        help="maximum wait for an action ACK before readback")
    parser.add_argument("--settle", type=float, default=DEFAULT_SERIAL_SETTLE_S)
    parser.add_argument("--gap", type=float, default=DEFAULT_PHASE_GAP_S,
                        help="bounded Core0/Core1 handoff gap between actions")
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--verbose", action="store_true",
                        help="print full snapshots; summary.json always keeps them")
    parser.add_argument("--assign-no", action="store_true",
                        help=("compatibility alias: assignment is now the "
                              "default after the line-order matrix passes"))
    parser.add_argument("--no-assign", action="store_true",
                        help="diagnostic only: do not commit the measured NO map")
    parser.add_argument("--reboot-verify-no", action="store_true",
                        help=("with --assign-no, reboot all boards and verify "
                              "the persisted Calibration node map"))
    parser.add_argument("--reboot-wait", type=float, default=3.0)
    parser.add_argument("--adjacency-only", "--line-only",
                        dest="adjacency_only", action="store_true",
                        help=("measure directed link adjacency with resident "
                              "TDMA frames only; do not issue clock TRAIN"))
    parser.add_argument("--probe-phase-cycles", type=int, default=10,
                        help=("baseline PIO phase used only by step-1 line "
                        "probing; default 10 samples = 40 ns"))
    parser.add_argument("--short-open", action="store_true",
                        help="open/close CDC for every command (diagnostic fallback)")
    return parser.parse_args()


def counter_delta(before: int, after: int) -> int:
    if after >= before:
        return after - before
    if before >= 0xF0000000 and after <= 0x0FFFFFFF:
        return (after - before) & 0xFFFFFFFF
    return 0


def counter_regressed(before: int, after: int) -> bool:
    return (after < before and
            not (before >= 0xF0000000 and after <= 0x0FFFFFFF))


def arm_pair_board(board, args: argparse.Namespace) -> None:
    """Submit ARM only after the previous STOP/TOPOLOGY intent is consumed."""
    last_result = None
    for attempt in range(1, 4):
        if attempt > 1:
            time.sleep(args.gap)
        board_command(board, "SYSTem:TDMA:RING:ARM", args)
        raw = board_command(board, "SYSTem:TDMA:RING:ARM:STATus?", args)
        try:
            last_result = int(raw.strip().strip('"'), 0)
        except ValueError as exc:
            raise RuntimeError(
                f"{board.address}: invalid ARM status {raw!r}") from exc
        if last_result == 1:
            return
        # Result 8 is the firmware's bounded transition rejection.  The
        # next attempt is safe after the explicit handoff gap; other results
        # are also retried once so transient CDC/owner races remain diagnosable.
    raise RuntimeError(
        f"{board.address}: ARM rejected with result={last_result}")


def wait_started_with_transport_recovery(
        board, args: argparse.Namespace,
        recoveries: list[dict[str, object]], phase: str) -> dict[str, int]:
    """Bound a recoverable persistent-CDC failure with a fresh session.

    The ARM intent has already been accepted by firmware.  A timeout while
    querying its state is diagnostic transport loss, not a hardware-safety
    reason to reject the topology state machine.  Retain the original error,
    close every persistent handle, and perform one short-open readback pass.
    """
    try:
        return wait_started(board, args)
    except RuntimeError as exc:
        if not args.keep_open:
            raise
        recovery = {
            "board_id": board.address,
            "phase": phase,
            "reason": str(exc),
            "action": "BOUNDED_SHORT_OPEN_STATUS_RETRY",
            "recovered": False,
        }
        recoveries.append(recovery)
        close_persistent_connections()
        fallback_args = argparse.Namespace(**vars(args))
        fallback_args.keep_open = False
        fallback_args.short_open = True
        try:
            status_snapshot = wait_started(board, fallback_args)
        except RuntimeError as fallback_exc:
            recovery["fallback_reason"] = str(fallback_exc)
            raise
        recovery["recovered"] = True
        recovery["status_snapshot"] = status_snapshot
        return status_snapshot


def render_ring_order(adjacency: dict[str, list[str]], reference: str,
                      node_count: int) -> list[str]:
    order = [reference]
    current = reference
    for _ in range(node_count - 1):
        next_nodes = adjacency.get(current, [])
        if len(next_nodes) != 1 or next_nodes[0] in order:
            return []
        current = next_nodes[0]
        order.append(current)
    return order if adjacency.get(current) == [reference] else []


def compact_pair_results(pair_results: list[dict[str, object]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for item in pair_results:
        phys = item.get("receiver_phys", {})
        rows.append({
            "driver": item["driver"],
            "receiver": item["receiver"],
            "detected": item["detected"],
            "rx_frames": item["rx_delta"],
            "rx_counter_regressed": item.get("rx_counter_regressed", False),
            "rx_words": item["rx_words_delta"],
            "rx_edges": item["rx_edges_delta"],
            "magic_fail": item["magic_fail_delta"],
            "bad_header": [
                phys.get("last_bad_header0", 0),
                phys.get("last_bad_header1", 0),
                phys.get("last_bad_header2", 0),
                phys.get("last_bad_header3", 0),
            ],
        })
    return rows


def main() -> int:
    args = parse_args()
    args.keep_open = not args.short_open
    board_ids = list(args.board_id)
    if len(board_ids) < 2 or len(board_ids) > 8:
        raise SystemExit("board count must be in [2, 8]")
    if len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique")
    if args.anchor_id and args.anchor_id not in board_ids:
        raise SystemExit("anchor-id must be one of the board IDs")
    if args.reboot_verify_no and args.no_assign:
        raise SystemExit("--reboot-verify-no cannot be combined with --no-assign")
    if args.cycles <= 0 or args.cycles > 65536 or args.cycles % 8:
        raise SystemExit("cycles must be an 8-cycle multiple in [8, 65536]")
    if (args.train_chunk_cycles < 0 or
            (args.train_chunk_cycles != 0 and
             (args.train_chunk_cycles > args.cycles or
              args.train_chunk_cycles % 8 != 0))):
        raise SystemExit(
            "train-chunk-cycles must be 0 or an 8-cycle multiple not greater "
            "than cycles")
    if not 1 <= args.probe_phase_cycles <= 31:
        raise SystemExit("probe-phase-cycles must be in [1, 31]")
    args.board_ids = board_ids

    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    if args.expected_build:
        wrong = {address: boards[address].build for address in board_ids
                 if boards[address].build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")

    def apply_profile(address: str) -> dict[str, object]:
        board = boards[address]
        _ = board_command(board, "SYSTem:TDMA:RING:STOP", args)
        stage_response = board_command(
            board, f"SYSTem:TDMA:OPMode:STAGe {args.level}", args)
        apply_response = board_command(
            board, "SYSTem:TDMA:OPMode:APPLy", args)
        probe_response = board_command(
            board,
            f"CALibration:TOPology:PROBe 1,{args.probe_phase_cycles}", args)
        active_response = board_command(
            board, "SYSTem:TDMA:OPMode?", args)
        active_level = int(active_response.split(",", 1)[0].strip().strip('"'), 0)
        return {
            "address": address,
            "requested_level": args.level,
            "active_level": active_level,
            "stage_response": stage_response,
            "apply_response": apply_response,
            "probe_response": probe_response,
            "passed": active_level == args.level,
        }

    with ThreadPoolExecutor(max_workers=len(board_ids)) as executor:
        profile_apply = list(executor.map(apply_profile, board_ids))
    if not all(item["passed"] for item in profile_apply):
        raise RuntimeError(f"Calibration profile apply failed: {profile_apply}")

    pair_results: list[dict[str, object]] = []
    transport_recoveries: list[dict[str, object]] = []
    adjacency = {address: [] for address in board_ids}
    try:
        # P0T uses temporary two-node topologies. Clear any persisted formal
        # training stage once before the matrix scan; later pairs do not add a
        # stage, so repeating this action only adds serial timeout latency.
        with ThreadPoolExecutor(max_workers=len(board_ids)) as executor:
            list(executor.map(
                lambda address: board_command(
                    boards[address], "CALibration:TRAINing:STAGe:CLEar", args),
                board_ids))
        for driver_id in board_ids:
            for receiver_id in board_ids:
                if receiver_id == driver_id:
                    continue
                with ThreadPoolExecutor(max_workers=len(board_ids)) as executor:
                    list(executor.map(
                        lambda address: board_command(
                            boards[address], "SYSTem:TDMA:RING:STOP", args),
                        board_ids))
                time.sleep(args.gap)

                driver = boards[driver_id]
                receiver = boards[receiver_id]
                with ThreadPoolExecutor(max_workers=2) as executor:
                    list(executor.map(
                        lambda item: board_command(item[0], item[1], args),
                        ((driver, "SYSTem:TDMA:RING:TOPology 2,0,0"),
                         (receiver, "SYSTem:TDMA:RING:TOPology 2,1,0"))))
                time.sleep(args.gap)
                for board in (receiver, driver):
                    arm_pair_board(board, args)
                    _ = wait_started_with_transport_recovery(
                        board, args, transport_recoveries,
                        f"{driver_id}->{receiver_id}")
                if not args.adjacency_only:
                    for board in (receiver, driver):
                        _ = train(board, args)

                # ``board_command`` uses a persistent CDC session by default,
                # while the frequency-sweep snapshot helper owns a separate
                # serial handle. Release the former before opening the
                # latter; Windows rejects concurrent opens of the same COM
                # port and turns a valid probe into a false failure.
                close_persistent_connections()
                before = snapshot(receiver, args.timeout)
                _ = board_command(receiver, "SYSTem:TDMA:RING:START", args)
                _ = board_command(driver, "SYSTem:TDMA:RING:START", args)
                # START is an intent.  Use the receiver's counters as the
                # completion query and return as soon as activity is visible;
                # pair_wait is only the bounded failure timeout.
                deadline = time.monotonic() + args.pair_wait
                after = None
                while time.monotonic() < deadline:
                    close_persistent_connections()
                    candidate = snapshot(receiver, args.timeout)
                    rx_delta = counter_delta(
                        before["tdma"]["ring_adapter_rx_count"],
                        candidate["tdma"]["ring_adapter_rx_count"])
                    rx_words_delta = counter_delta(
                        before["phys"]["rx_dma_produced_words"],
                        candidate["phys"]["rx_dma_produced_words"])
                    rx_edges_delta = counter_delta(
                        before["phys"]["rx_edge_count"],
                        candidate["phys"]["rx_edge_count"])
                    after = candidate
                    if (rx_delta >= args.min_rx_frames or
                            rx_words_delta >= args.min_rx_words or
                            rx_edges_delta > 0):
                        break
                    time.sleep(min(args.gap, 0.02))
                if after is None:
                    raise RuntimeError(
                        f"{receiver.address}: pair activity query produced no snapshot")
                close_persistent_connections()
                # ``after`` is the queried completion snapshot above.
                rx_before = before["tdma"]["ring_adapter_rx_count"]
                rx_after = after["tdma"]["ring_adapter_rx_count"]
                rx_delta = counter_delta(rx_before, rx_after)
                rx_counter_regressed = counter_regressed(rx_before, rx_after)
                tx_delta = counter_delta(
                    before["tdma"]["ring_adapter_tx_count"],
                    after["tdma"]["ring_adapter_tx_count"])
                rx_words_delta = counter_delta(
                    before["phys"]["rx_dma_produced_words"],
                    after["phys"]["rx_dma_produced_words"])
                rx_edges_delta = counter_delta(
                    before["phys"]["rx_edge_count"],
                    after["phys"]["rx_edge_count"])
                magic_fail_delta = counter_delta(
                    before["phys"]["rx_magic_fail_count"],
                    after["phys"]["rx_magic_fail_count"])
                detected = (
                    rx_delta >= args.min_rx_frames
                    or rx_words_delta >= args.min_rx_words
                    or rx_edges_delta > 0
                )
                if detected:
                    adjacency[driver_id].append(receiver_id)
                pair_results.append({
                    "driver": driver_id,
                    "receiver": receiver_id,
                    "detected": detected,
                    "rx_delta": rx_delta,
                    "rx_counter_regressed": rx_counter_regressed,
                    "tx_delta": tx_delta,
                    "rx_words_delta": rx_words_delta,
                    "rx_edges_delta": rx_edges_delta,
                    "magic_fail_delta": magic_fail_delta,
                    "receiver_status": after["tdma"],
                    "receiver_phys": after["phys"],
                })
    finally:
        def cleanup(address: str) -> None:
            try:
                _ = board_command(
                    boards[address], "SYSTem:TDMA:RING:STOP", args)
                _ = board_command(
                    boards[address], "CALibration:TOPology:PROBe 0", args)
            except Exception:  # pragma: no cover - best effort bench cleanup
                pass
        with ThreadPoolExecutor(max_workers=len(board_ids)) as executor:
            list(executor.map(cleanup, board_ids))
        # The topology process is a phase boundary. Do not leave persistent
        # CDC handles alive for the next calibration subprocess.
        close_persistent_connections()

    anchor = args.anchor_id or board_ids[0]
    ring_order = render_ring_order(adjacency, anchor, len(board_ids))
    passed = len(ring_order) == len(board_ids)
    assignments: list[dict[str, object]] = []
    reboot_readback: list[dict[str, object]] = []
    if passed and not args.no_assign:
        for index, address in enumerate(ring_order):
            write_response = board_command(
                boards[address], f"SYSTem:BOARD:NO {index + 1}", args)
            readback = board_command(
                boards[address], "SYSTem:BOARD:NO?", args).strip().strip('"')
            assignment_passed = readback == str(index + 1)
            assignments.append({
                "no": index + 1,
                "address": address,
                "write_response": write_response,
                "readback": readback,
                "passed": assignment_passed,
            })
            passed = passed and assignment_passed
        if passed and args.reboot_verify_no:
            for address in ring_order:
                _ = board_command(
                    boards[address], "SYSTem:BOOT:RESet", args)
            time.sleep(max(args.reboot_wait, 3.0))
            rebooted = discover(args)
            missing_after_reboot = set(board_ids) - set(rebooted)
            if missing_after_reboot:
                raise RuntimeError(
                    "boards missing after reboot: " +
                    ", ".join(sorted(missing_after_reboot)))
            for index, address in enumerate(ring_order):
                readback = board_command(
                    rebooted[address], "SYSTem:BOARD:NO?", args
                ).strip().strip('"')
                readback_passed = readback == str(index + 1)
                reboot_readback.append({
                    "no": index + 1,
                    "address": address,
                    "readback": readback,
                    "passed": readback_passed,
                })
                passed = passed and readback_passed
    result = {
        "measurement_domain": "calibration",
        "measurement_phase": "link_adjacency_and_ring_topology",
        "operating_level": args.level,
        "profile_apply": profile_apply,
        "passed": passed,
        "anchor_id": anchor,
        "ring_order": ring_order,
        "node_map": [{"node": index, "no": index + 1,
                      "address": address}
                     for index, address in enumerate(ring_order)],
        "assignments": assignments,
        "node_discovery": {
            "step": "line_order_matrix",
            "committed": bool(assignments),
            "numbering": "NO.1..NO.N along measured directed ring",
        },
        "reboot_readback": reboot_readback,
        "adjacency": adjacency,
        "boards": {address: asdict(boards[address]) for address in board_ids},
        "pair_results": pair_results,
        "transport_recoveries": transport_recoveries,
        "adjacency_only": args.adjacency_only,
        "probe_phase_cycles": args.probe_phase_cycles,
    }
    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"calibration_ring_topology_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8")
    console_result = result if args.verbose else {
        "passed": passed,
        "anchor_id": anchor,
        "ring_order": ring_order,
        "adjacency": adjacency,
        "pair_results": compact_pair_results(pair_results),
    }
    print(json.dumps(console_result, indent=2))
    print(f"out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
