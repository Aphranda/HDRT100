#!/usr/bin/env python3
"""Measure Calibration link adjacency and ring order with TDMA probes."""

from __future__ import annotations

import argparse
import csv
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
from tdma_field_parse import (  # noqa: E402
    FIELDS as TDMA_FIELDS, PHYS_FIELDS, RUNTIME_FIELDS, parse_status_fields,
)
from calibration_ring_validate.calibration_timeout_config import (  # noqa: E402
    DEFAULT_ACTION_TIMEOUT_S,
    DEFAULT_PHASE_GAP_S,
    DEFAULT_SERIAL_SETTLE_S,
)
from calibration_ring_validate import calibration_clk_train as stopped_profile  # noqa: E402


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


def arm_pair_board(board, args: argparse.Namespace, actions=None) -> None:
    """Submit ARM only after the previous STOP/TOPOLOGY intent is consumed."""
    last_result = None
    for attempt in range(1, 4):
        if attempt > 1:
            time.sleep(args.gap)
        record = {"board": board.address, "attempt": attempt}
        if actions is not None:
            actions.append(record)
        response = board_command(board, "SYSTem:TDMA:RING:ARM", args)
        record["response"] = response
        raw = board_command(board, "SYSTem:TDMA:RING:ARM:STATus?", args)
        record["status_response"] = raw
        try:
            last_result = int(raw.strip().strip('"'), 0)
        except ValueError as exc:
            raise RuntimeError(
                f"{board.address}: invalid ARM status {raw!r}") from exc
        if last_result == 1 and response.strip().strip('"') == "OK":
            return
        if response.strip().strip('"') != "OK":
            raise RuntimeError(f"{board.address}: ARM acknowledgment unknown: {record}")
        # Result 8 is the firmware's bounded transition rejection.  The
        # next attempt is safe after the explicit handoff gap; other results
        # are also retried once so transient CDC/owner races remain diagnosable.
    raise RuntimeError(
        f"{board.address}: ARM rejected with result={last_result}")


def _error_code(raw: str) -> int:
    fields = next(csv.reader([raw], strict=True))
    if len(fields) != 2 or not fields[1].strip():
        raise ValueError(f"invalid error response {raw!r}")
    return int(fields[0].strip(), 10)


def _wait_pair_state(board, args, record, *, started, slot=None):
    """Fresh physical state plus consumed configuration, never a cached ACK."""
    deadline = time.monotonic() + args.arm_wait
    observations = record.setdefault("state_observations", [])
    while time.monotonic() < deadline:
        row = {}
        observations.append(row)
        try:
            raw = board_command(board, "SYSTem:TDMA:RING:STATus?", args)
            row["response"] = raw
            values = [int(field.strip().strip('"'), 0) for field in raw.split(",")]
            if len(values) != len(RUNTIME_FIELDS) or any(v < 0 or v > 0xffffffff for v in values):
                raise ValueError("invalid runtime fields")
            state = dict(zip(RUNTIME_FIELDS, values))
            row["snapshot"] = state
            applied = state["ring_config_seq"] == state["ring_applied_config_seq"]
            matched = slot is None or tuple(state[key] for key in (
                "ring_node_count", "ring_local_slot_id", "ring_reference_slot_id")) == (2, slot, 0)
            if (time.monotonic() < deadline and applied and matched and
                    state["ring_enabled"] == int(started) and
                    state["ring_adapter_started"] == int(started)):
                return state
        except Exception as exc:
            row["error"] = f"{type(exc).__name__}: {exc}"
        time.sleep(min(.02, max(0., deadline - time.monotonic())))
    raise RuntimeError(f"{board.address}: unconfirmed {'ARM' if started else 'STOP'}: {observations}")


def _stop_pair_board(board, args):
    record = {"board": board.address, "passed": False}
    try:
        record["response"] = board_command(board, "SYSTem:TDMA:RING:STOP", args)
    except Exception as exc:
        record["command_error"] = f"{type(exc).__name__}: {exc}"
    try:
        record["stopped"] = _wait_pair_state(board, args, record, started=False)
        record["passed"] = record.get("response", "").strip().strip('"') == "OK"
        if not record["passed"]:
            record["error"] = "STOP acknowledgment refused or unknown despite stopped readback"
    except Exception as exc:
        record["error"] = f"{type(exc).__name__}: {exc}"
    return record


def _start_pair_board(board, args, actions):
    """Exclusive per-board transaction; only a real ACK and clean ERR pass."""
    record = {"board": board.address, "passed": False, "errors_before": []}
    actions.append(record)
    try:
        for _ in range(16):
            raw = board_command(board, "SYSTem:ERRor?", args)
            record["errors_before"].append(raw)
            if _error_code(raw) == 0:
                break
        else:
            raise RuntimeError("error queue did not reach a clean baseline")
        record["attempted"] = True
        try:
            response = board_command(board, "SYSTem:TDMA:RING:START", args)
            record["response"] = response
        finally:
            # A missing or negative reply can occur after data was enabled.
            # Keep the device reason, but never infer START success from it.
            try:
                record["error_after"] = board_command(board, "SYSTem:ERRor?", args)
            except Exception as exc:
                record["error_readback_failure"] = f"{type(exc).__name__}: {exc}"
        if response.strip().strip('"') != "OK":
            raise RuntimeError("START acknowledgment is refused or unknown")
        if "error_after" not in record or _error_code(record["error_after"]) != 0:
            raise RuntimeError("START error queue is not clear")
        record["passed"] = True
    except Exception as exc:
        record["error"] = f"{type(exc).__name__}: {exc}"
        raise RuntimeError(f"{board.address}: START failed: {record}") from exc


def _snapshot_format_errors(candidate):
    errors = {}
    if not isinstance(candidate, dict):
        return {"snapshot": "snapshot is not an object"}
    for plane, fields in (("tdma", TDMA_FIELDS), ("phys", PHYS_FIELDS)):
        try:
            values = candidate[plane]
            raw = candidate["raw"][plane]
            if not isinstance(values, dict) or not isinstance(raw, str):
                raise ValueError("missing object/raw response")
            if plane == "tdma":
                parsed = tuple(parse_status_fields(raw))
            else:
                parts = next(csv.reader([raw], strict=True))
                if len(parts) != len(fields):
                    raise ValueError(f"field count {len(parts)} != {len(fields)}")
                parsed = tuple(int(value.strip().strip('"'), 0) for value in parts)
            if any(value < 0 or value > 0xffffffff for value in parsed):
                raise ValueError("raw field outside uint32 range")
            if any(type(values.get(name)) is not int or values[name] != value
                   for name, value in zip(fields, parsed)):
                raise ValueError("missing, invalid or inconsistent decoded field")
        except (KeyError, TypeError, ValueError, csv.Error, StopIteration) as exc:
            errors[plane] = str(exc)
    return errors


def _pair_snapshot(board, args, armed, observations, *, phase):
    """Retry malformed read-only evidence once; valid lifetime drift is final."""
    for attempt in (1, 2):
        row = {"board": board.address, "phase": phase, "attempt": attempt}
        observations.append(row)
        try:
            close_persistent_connections()
            candidate = snapshot(board, args.timeout)
            row["snapshot"] = candidate
            if not isinstance(candidate, dict) or any(candidate.get(key) != getattr(board, key)
                    for key in ("address", "port", "build")):
                row["classification"] = "IDENTITY_MISMATCH"
                raise RuntimeError("snapshot board identity missing or changed")
            errors = _snapshot_format_errors(candidate)
            changed = {} if "tdma" in errors else {
                key: {"expected": armed[key], "observed": candidate["tdma"][key]}
                for key in ("ring_config_seq", "ring_node_count", "ring_local_slot_id",
                            "ring_reference_slot_id", "ring_enabled", "ring_adapter_started")
                if candidate["tdma"][key] != armed[key]}
            if changed:
                row["classification"] = "LIFETIME_CHANGED"
                row["changed"] = changed
                row["format_errors"] = errors
                raise RuntimeError("snapshot crossed the admitted ARM lifetime")
            if errors:
                row["classification"] = "MALFORMED"
                row["format_errors"] = errors
                if attempt == 1:
                    continue
                raise RuntimeError("snapshot malformed after two read-only observations")
            row["classification"] = "VALID"
            return candidate
        except Exception as exc:
            row["error"] = f"{type(exc).__name__}: {exc}"
            raise
    raise AssertionError("unreachable")


def start_pair(driver, receiver, args, actions, recoveries):
    """At most two ARM lifetimes; no naked retry of an unknown START."""
    for attempt in (1, 2):
        record = {"driver": driver.address, "receiver": receiver.address,
                  "attempt": attempt, "passed": False, "arm": [], "start": []}
        actions.append(record)
        try:
            for board, slot in ((receiver, 1), (driver, 0)):
                arm_pair_board(board, args, record["arm"])
                state_record = {"board": board.address}
                record.setdefault("armed", []).append(state_record)
                state_record["snapshot"] = _wait_pair_state(
                    board, args, state_record, started=True, slot=slot)
            if not args.adjacency_only:
                for board in (receiver, driver):
                    train_record = {"board": board.address}
                    record.setdefault("training", []).append(train_record)
                    train_record["result"] = train(board, args)
            # Snapshot is a separate serial owner; do not reuse an old
            # lifetime's counter baseline if preparation has changed it.
            armed = record["armed"][0]["snapshot"]
            before = _pair_snapshot(receiver, args, armed,
                record.setdefault("baseline_observations", []), phase="baseline")
            record["before"] = before
            for board in (receiver, driver):
                _start_pair_board(board, args, record["start"])
            record["passed"] = True
            return before
        except Exception as exc:
            record["error"] = f"{type(exc).__name__}: {exc}"
            if not any(row.get("attempted") for row in record["start"]):
                raise
            # Attempt every STOP even if one transport fails. Re-ARM is
            # forbidden until both fresh stopped/config barriers are proven.
            record["recovery_stop"] = [_stop_pair_board(board, args)
                                       for board in (receiver, driver)]
            if not all(row["passed"] for row in record["recovery_stop"]):
                raise RuntimeError(f"pair recovery STOP unconfirmed: {record}") from exc
            if attempt == 2:
                raise RuntimeError(f"pair START failed after two lifetimes: {record}") from exc
            recoveries.append({"phase": f"{driver.address}->{receiver.address}",
                "action": "STOP_CONFIRMED_REARM_PAIR", "failed_attempt": attempt,
                "reason": record["error"]})
    raise AssertionError("unreachable")


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


def configure_pair_topology(board, slot, args, actions):
    """Reuse the calibration owner's bounded stopped configuration handoff.

    A completed STOP query is not a lease on the next control operation.
    Keep explicit -200 refusals and require a fresh ACK plus applied generation
    before ARM. Never clear a DPLL session to make topology admission pass.
    """
    row = {"board": board.address, "slot": slot, "actions": [],
           "errors_before": [], "passed": False}
    actions.append(row)
    config_args = argparse.Namespace(**vars(args))
    config_args.idle_poll_interval = .02
    try:
        for _ in range(16):
            raw = board_command(board, "SYSTem:ERRor?", args)
            row["errors_before"].append(raw)
            if _error_code(raw) == 0:
                break
        else:
            raise RuntimeError("error queue did not reach a clean topology baseline")
        raw = board_command(board, "SYSTem:VDC:FEEDback:SESSion?", args)
        row["session_before"] = raw
        if raw.strip().strip('"') != "0":
            raise RuntimeError("topology requires an inactive feedback session")
        values = stopped_profile._set_stopped_topology(
            board, (2, slot, 0), config_args, row["actions"])
        row["response"] = ','.join(map(str, values))
        row["error_after"] = board_command(board, "SYSTem:ERRor?", args)
        if _error_code(row["error_after"]) != 0:
            raise RuntimeError("topology error queue is not clear")
        row["passed"] = True
    except Exception as exc:
        row["error"] = f"{type(exc).__name__}: {exc}"
        raise
    return row


def apply_profile(board, args: argparse.Namespace) -> dict[str, object]:
    """Keep every attempt and reuse the stopped APPLY attribution barrier."""
    profile_args = argparse.Namespace(**vars(args))
    profile_args.idle_poll_interval = getattr(args, "idle_poll_interval", .02)
    actions = []
    result = {"address": board.address, "requested_level": args.level,
              "active_level": None, "passed": False, "actions": actions}
    try:
        stopped_profile._control_command(
            board, "SYSTem:TDMA:RING:STOP", profile_args, actions, ack=True)
        result["stopped"] = stopped_profile.wait_ring_stopped(board, profile_args)
        staged = stopped_profile._control_command(board,
            f"SYSTem:TDMA:OPMode:STAGe {args.level}", profile_args, actions, fields=6)
        result["stage_response"] = actions[-1]["response"]
        if staged[0] != args.level:
            raise RuntimeError(f"staged level mismatch: {staged}")
        applied = stopped_profile._apply_stopped_opmode(
            board, staged, profile_args, actions)
        result["active_level"] = applied[0]
        stopped_profile._control_command(board,
            f"CALibration:TOPology:PROBe 1,{args.probe_phase_cycles}",
            profile_args, actions, expected=(1, args.probe_phase_cycles))
        result["probe_response"] = actions[-1]["response"]
        result["passed"] = True
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        attempts = [row for row in actions
                    if row["command"] == "SYSTem:TDMA:OPMode:APPLy"]
        if attempts:
            result["apply_response"] = attempts[-1].get("response")
            observed = attempts[-1].get("opmode_after")
            if observed is not None:
                result["active_level"] = observed[0]
    return result


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

    profile_apply: list[dict[str, object]] = []
    pair_results: list[dict[str, object]] = []
    pair_actions: list[dict[str, object]] = []
    pair_preparation: list[dict[str, object]] = []
    transport_recoveries: list[dict[str, object]] = []
    cleanup_results: list[dict[str, object]] = []
    error = ""
    adjacency = {address: [] for address in board_ids}
    try:
        with ThreadPoolExecutor(max_workers=len(board_ids)) as executor:
            # Retain completed peers even if another profile action fails.
            futures = [executor.submit(apply_profile, boards[address], args)
                       for address in board_ids]
            for future in futures:
                try:
                    profile_apply.append(future.result())
                except Exception as exc:
                    profile_apply.append({"passed": False, "error": f"{type(exc).__name__}: {exc}"})
        if not all(item["passed"] for item in profile_apply):
            raise RuntimeError(f"Calibration profile apply failed: {profile_apply}")
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
                preparation = {"driver": driver_id, "receiver": receiver_id, "topology": []}
                pair_preparation.append(preparation)
                with ThreadPoolExecutor(max_workers=len(board_ids)) as executor:
                    preparation["stop"] = list(executor.map(
                        lambda address: _stop_pair_board(boards[address], args),
                        board_ids))
                if not all(row["passed"] for row in preparation["stop"]):
                    raise RuntimeError(f"pair preparation STOP unconfirmed: {preparation}")
                time.sleep(args.gap)

                driver = boards[driver_id]
                receiver = boards[receiver_id]
                for board, slot in ((driver, 0), (receiver, 1)):
                    configure_pair_topology(board, slot, args, preparation["topology"])
                time.sleep(args.gap)
                before = start_pair(driver, receiver, args, pair_actions, transport_recoveries)
                # START is an intent.  Use the receiver's counters as the
                # completion query and return as soon as activity is visible;
                # pair_wait is only the bounded failure timeout.
                deadline = time.monotonic() + args.pair_wait
                after = None
                while time.monotonic() < deadline:
                    candidate = _pair_snapshot(receiver, args, before["tdma"],
                        preparation.setdefault("activity_observations", []), phase="activity")
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
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        def cleanup(address: str):
            record = _stop_pair_board(boards[address], args)
            try:
                record["probe_response"] = board_command(
                    boards[address], "CALibration:TOPology:PROBe 0", args)
                if tuple(int(v.strip().strip('"'), 0) for v in record["probe_response"].split(",")) != (0, 0):
                    raise RuntimeError("probe cleanup acknowledgment unknown")
            except Exception as exc:
                record["probe_error"] = f"{type(exc).__name__}: {exc}"
                record["passed"] = False
            return record
        with ThreadPoolExecutor(max_workers=len(board_ids)) as executor:
            cleanup_results = list(executor.map(cleanup, board_ids))
        close_persistent_connections()

    anchor = args.anchor_id or board_ids[0]
    ring_order = render_ring_order(adjacency, anchor, len(board_ids))
    passed = (not error and len(ring_order) == len(board_ids) and
              all(row["passed"] for row in cleanup_results))
    assignments: list[dict[str, object]] = []
    reboot_readback: list[dict[str, object]] = []
    try:
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
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
        passed = False
    finally:
        close_persistent_connections()
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
        "pair_actions": pair_actions,
        "pair_preparation": pair_preparation,
        "cleanup": cleanup_results,
        "error": error,
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
