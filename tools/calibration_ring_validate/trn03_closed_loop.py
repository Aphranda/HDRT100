#!/usr/bin/env python3
"""Run TRN-03B raw-flight or process-image short-frame closed-loop gates."""

from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
for tool_path in (ROOT / "tools", ROOT / "tools" / "tdma_ring_monitor",
                  ROOT / "tools" / "calibration_ring_validate"):
    if str(tool_path) not in sys.path:
        sys.path.insert(0, str(tool_path))

from tdma_start_ring import (  # noqa: E402
    Board,
    board_command,
    close_persistent_connections,
    discover,
    order_boards_by_board_no,
    train,
)
from flight_bitmap_validate import (  # noqa: E402
    FIFO_FIELDS,
    PROCESS_FIELDS,
)
from tdma_field_parse import PHYS_FIELDS  # noqa: E402
from trn03_stage import (  # noqa: E402
    checked_action,
    drain_errors,
    error_is_clear,
    load_config,
    stage_board,
)
from trn03_waveform import (  # noqa: E402
    analyze_capture_set,
    download_ring_capture,
    read_tdma_load_mask,
    read_tdma_schedule,
    save_ring_capture,
)


RUNTIME_FIELDS = (
    "ring_enabled",
    "ring_node_count",
    "ring_local_slot_id",
    "ring_reference_slot_id",
    "ring_up_running",
    "ring_down_running",
    "ring_seq",
    "ring_last_error",
    "ring_adapter_started",
    "ring_adapter_service_count",
    "ring_up_tx_sequence",
    "ring_down_rx_sequence",
    "ring_up_tx_frame_crc32",
    "ring_down_rx_frame_crc32",
    "ring_idle_beacon_tx_count",
    "ring_idle_beacon_rx_count",
    "ring_feedback_round_trip_ns",
    "ring_timestamp_resolution_ns",
    "ring_timestamp_flags",
    "ring_reference_tx_timestamp_ns_lo",
    "ring_reference_tx_timestamp_ns_hi",
    "ring_feedback_rx_timestamp_ns_lo",
    "ring_feedback_rx_timestamp_ns_hi",
    "ring_adapter_last_error",
    "ring_adapter_tx_count",
    "ring_adapter_rx_count",
    "ring_adapter_rx_bad_count",
    "ring_adapter_rx_transport_bad_count",
    "ring_adapter_rx_schedule_bad_count",
    "ring_adapter_rx_profile_bad_count",
    "ring_adapter_last_bad_transport_result",
    "ring_adapter_last_bad_sequence",
    "ring_adapter_last_bad_schedule_crc32",
    "ring_adapter_last_bad_profile_crc32",
    "ring_adapter_last_bad_header_diff_count",
    "ring_adapter_last_bad_header_first_diff_offset",
    "ring_adapter_last_bad_header_expected_byte",
    "ring_adapter_last_bad_header_observed_byte",
    "ring_config_seq",
    "ring_applied_config_seq",
)

CRC_DIAGNOSTIC_FIELDS = (
    "clock_evidence_enabled",
    "last_bad_sequence",
    "last_bad_transport_result",
    "last_bad_clock_evidence",
    "last_bad_packet_diff_count",
    "last_bad_packet_first_diff_offset",
    "last_bad_packet_expected_byte",
    "last_bad_packet_observed_byte",
    "last_bad_expected_transport_crc32",
    "last_bad_observed_transport_crc32",
    "last_bad_recomputed_transport_crc32",
    "last_bad_expected_payload_crc32",
    "last_bad_observed_payload_crc32",
)

APP_REALTIME_PHASE_VDC = 1
APP_REALTIME_PHASE_DPLL = 2
APP_REALTIME_PHASE_REFMEM = 5
APP_REALTIME_LOAD_VDC_MASK = 1 << 0
APP_REALTIME_LOAD_DPLL_MASK = 1 << 1
APP_REALTIME_LOAD_REFMEM_MASK = 1 << 4


class ProgressReporter:
    """Publish host progress without issuing any additional board query."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.sequence = 0
        self.started = time.monotonic()

    def emit(self, event: str, **details: Any) -> None:
        self.sequence += 1
        payload = {
            "schema": "HAOFV_TRN03_PROGRESS_V1",
            "sequence": self.sequence,
            "event": event,
            "elapsed_s": round(time.monotonic() - self.started, 6),
            "updated_at": datetime.now().astimezone().isoformat(),
            "details": details,
            "query_policy": "REUSE_REQUIRED_CORE0_SNAPSHOTS_ONLY",
        }
        self.path.parent.mkdir(parents=True, exist_ok=True)
        pending = self.path.with_suffix(self.path.suffix + ".tmp")
        pending.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        pending.replace(self.path)
        print("TRN03_PROGRESS " + json.dumps(
            payload, ensure_ascii=False, separators=(",", ":")), flush=True)


class ArmRejectedError(RuntimeError):
    """Preserve the ARM rejection evidence across final cleanup."""

    def __init__(self, message: str, evidence: dict[str, Any]) -> None:
        super().__init__(message)
        self.evidence = evidence


def merge_error(current: str, new_error: str) -> str:
    if not current:
        return new_error
    if not new_error or new_error in current:
        return current
    return current + "; " + new_error


def validate_realtime_phase(before: dict[str, Any], after: dict[str, Any],
                            *, name: str, phase_id: int,
                            load_mask: int) -> dict[str, Any]:
    before_phase = before["phases"][phase_id]
    after_phase = after["phases"][phase_id]

    def delta(field: str) -> int:
        return ((int(after_phase[field]) - int(before_phase[field])) &
                0xFFFFFFFF)

    deltas = {field: delta(field) for field in (
        "run_count", "skip_count", "start_miss_count", "overrun_count",
        "deadline_miss_count")}
    errors = []
    if deltas["run_count"] == 0:
        errors.append(f"{name}_phase_not_serviced")
    if deltas["overrun_count"] != 0:
        errors.append(f"{name}_overrun_count_grew")
    if deltas["deadline_miss_count"] != 0:
        errors.append(f"{name}_deadline_miss_count_grew")
    if (int(after["quarantined_mask"]) & load_mask) != 0:
        errors.append(f"{name}_load_quarantined")
    if (int(after_phase["max_runtime_cycles"]) >
            int(after_phase["wcet_cycles"])):
        errors.append(f"{name}_max_runtime_exceeded_wcet")
    return {
        "passed": not errors,
        "errors": errors,
        "deltas": deltas,
        "before": before_phase,
        "after": after_phase,
        "quarantined_mask": int(after["quarantined_mask"]),
    }


def validate_dpll_schedule(before: dict[str, Any],
                           after: dict[str, Any]) -> dict[str, Any]:
    phases = {
        "vdc": validate_realtime_phase(
            before, after, name="vdc", phase_id=APP_REALTIME_PHASE_VDC,
            load_mask=APP_REALTIME_LOAD_VDC_MASK),
        "dpll": validate_realtime_phase(
            before, after, name="dpll", phase_id=APP_REALTIME_PHASE_DPLL,
            load_mask=APP_REALTIME_LOAD_DPLL_MASK),
        "refmem": validate_realtime_phase(
            before, after, name="refmem", phase_id=APP_REALTIME_PHASE_REFMEM,
            load_mask=APP_REALTIME_LOAD_REFMEM_MASK),
    }
    errors = [
        error for phase in phases.values() for error in phase["errors"]
    ]
    return {
        "passed": not errors,
        "errors": errors,
        "phases": phases,
        "quarantined_mask": int(after["quarantined_mask"]),
    }


def realtime_gate_passes(*, error: str,
                         soak_validation: dict[str, Any],
                         dpll_schedule_gate: dict[str, Any],
                         nodes: dict[str, Any],
                         ordered: list[Board]) -> bool:
    """Return only the deterministic Core1/PIO short-frame gate.

    SD waveform capture and offline SVG analysis intentionally do not appear
    here.  They are evidence for diagnosis and may be unavailable while the
    realtime ring remains healthy enough to hand off to DPLL.
    """
    return (
        not error and
        bool(soak_validation.get("passed")) and
        bool(dpll_schedule_gate.get("passed")) and
        len(nodes) == len(ordered) and
        all(node["passed"] for node in nodes.values())
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address in physical node order")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument(
        "--offset-row-id", type=int,
        help="TRN-03 logical offset-matrix row; defaults to active_row_id")
    parser.add_argument("--expected-build")
    parser.add_argument("--level", type=int,
                        help="operating-profile level; defaults to config")
    parser.add_argument("--cycles", type=int, default=4096)
    parser.add_argument("--train-chunk-cycles", type=int, default=0)
    parser.add_argument(
        "--clock-train", action="store_true",
        help=("run the optional coarse CLK burst before cyclic START; "
               "default keeps the already armed flight persona intact"))
    parser.add_argument(
        "--clock-evidence", choices=("enabled", "disabled"),
        default="enabled",
        help=("enable the fixed process-image DPLL observation trailer or "
              "clear its valid bit for an explicit A/B run; frame type, "
              "length and cadence remain unchanged"))
    parser.add_argument("--window-s", type=float, default=3.0)
    parser.add_argument(
        "--sample-interval-s", type=float, default=1.0,
        help=("periodic soak snapshot interval; every interval is gated, "
              "not only the first and last snapshot"))
    parser.add_argument(
        "--startup-timeout-s", type=float, default=5.0,
        help=("deadline for the explicit ring/process-image startup barrier; "
              "startup pipeline-fill counters are excluded from soak only "
              "after this barrier passes"))
    parser.add_argument(
        "--startup-stable-samples", type=int, default=3,
        help=("consecutive receive opportunities with advancing accepted "
              "sequence and no new reject/incomplete counters"))
    parser.add_argument(
        "--startup-poll-interval-s", type=float, default=0.1,
        help="poll interval for the explicit startup barrier")
    parser.add_argument(
        "--diagnostic-continue", action="store_true",
        help=("record a failed startup/realtime gate but continue soak and "
              "diagnostic collection; the command still exits nonzero"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--read-timeout", type=float, default=0.02)
    parser.add_argument("--action-timeout", type=float, default=0.5)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument(
        "--short-open", action="store_true",
        help="open/close CDC for every command instead of one session per board")
    parser.add_argument(
        "--owner-action-retries", type=int, default=3,
        help=("bounded retries for STOPPED-only profile/topology owner "
              "mutations; every rejected attempt is retained as evidence"))
    parser.add_argument("--capture-timeout", type=float, default=10.0)
    parser.add_argument("--capture-latch-retries", type=int, default=1)
    parser.add_argument(
        "--capture-waveforms", action="store_true",
        help=("run the optional realtime raw-capture request; waveform copy, "
              "SD save and SVG analysis remain outside the short-frame gate"))
    parser.add_argument("--waveform-window-ns", type=int, default=1000)
    parser.add_argument("--sck-frequency-tolerance-percent", type=float,
                        default=5.0)
    parser.add_argument("--sck-duty-tolerance-percent", type=float,
                        default=10.0)
    parser.add_argument(
        "--stage", choices=("raw-flight", "process-image"),
        default="process-image",
        help="raw PIO cut-through proof or final FIFO/process-image gate")
    parser.add_argument(
        "--dpll-provisional", action="store_true",
        help=("while STOPPED, derive the P4-LIVE DIAGNOSTIC_ONLY path matrix "
              "from the frozen TRN-03 link-base matrix before ARM"))
    parser.add_argument(
        "--leave-running", action="store_true",
        help=("leave the matrix-backed ring running only after every closed-"
              "loop gate and a final runtime readback pass; diagnostic mode "
              "records handoff failures without stopping the ring"))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def parse_snapshot(raw: str, fields: tuple[str, ...], label: str
                   ) -> dict[str, int]:
    try:
        values = [int(value.strip().strip('"'), 0) for value in raw.split(",")]
    except ValueError as exc:
        raise RuntimeError(f"{label}: non-integer snapshot {raw!r}") from exc
    if len(values) != len(fields):
        raise RuntimeError(
            f"{label}: field count {len(values)}, expected {len(fields)}")
    return dict(zip(fields, values))


def parse_active_profile(raw: str, label: str) -> dict[str, int]:
    fields = ("level", "baud_hz", "cycle_period_ns", "train_cycles",
              "flags", "profile_crc32")
    try:
        values = [int(value.strip().strip('"'), 0) for value in raw.split(",")]
    except ValueError as exc:
        raise RuntimeError(f"{label}: non-integer profile {raw!r}") from exc
    if len(values) < len(fields):
        raise RuntimeError(
            f"{label}: profile field count {len(values)}, expected at least "
            f"{len(fields)}")
    return dict(zip(fields, values[:len(fields)]))


def resolve_profile_level(config_level: Any,
                          requested_level: int | None) -> int:
    """Keep operating-profile identity inseparable from the frozen matrix."""
    if not isinstance(config_level, int):
        raise ValueError("profile_level is required in config")
    if requested_level is not None and requested_level != config_level:
        raise ValueError(
            f"--level {requested_level} conflicts with frozen config "
            f"profile_level {config_level}; regenerate the staging matrix "
            "for another profile")
    return config_level


def running_handoff_errors(snapshot: dict[str, int], node_index: int,
                           node_count: int) -> list[str]:
    """Validate the final read-only handoff from calibration to DPLL."""
    expected = {
        "ring_enabled": 1,
        "ring_node_count": node_count,
        "ring_local_node": node_index,
        "ring_reference_node": 0,
        "ring_adapter_started": 1,
        "ring_up_running": 1,
        "ring_down_running": 1,
    }
    return [name for name, value in expected.items()
            if snapshot.get(name) != value]


def running_handoff_allows_leave_running(
        handoff: dict[str, dict[str, Any]], *, diagnostic_continue: bool
        ) -> bool:
    """Keep diagnostic runs observational even when the strict gate fails."""
    if not handoff:
        return False
    if diagnostic_continue:
        return True
    return all(bool(item.get("passed")) for item in handoff.values())


def arm_with_evidence(board: Board, args: argparse.Namespace,
                      node_index: int = 0) -> dict[str, Any]:
    """ARM one Node and prove that an ACK timeout did not hide rejection."""
    drained = drain_errors(board, args)
    response = board_command(board, "SYSTem:TDMA:RING:ARM", args)
    status_raw = board_command(
        board, "SYSTem:TDMA:RING:ARM:STATus?", args).strip().strip('"')
    error_after = board_command(board, "SYSTem:ERR?", args)
    try:
        arm_result = int(status_raw, 0)
    except ValueError as exc:
        evidence = {
            "node": board.address,
            "action": "ARM",
            "response": response,
            "arm_status_raw": status_raw,
            "arm_result": None,
            "errors_drained_before": drained,
            "error_after": error_after,
            "passed": False,
        }
        evidence.update(arm_failure_snapshot(board, args, node_index))
        raise ArmRejectedError(
            f"{board.address}: invalid ARM status {status_raw!r}",
            evidence) from exc
    evidence = {
        "node": board.address,
        "action": "ARM",
        "response": response,
        "arm_result": arm_result,
        "errors_drained_before": drained,
        "error_after": error_after,
        "passed": arm_result == 1 and error_is_clear(error_after),
    }
    if not evidence["passed"]:
        evidence.update(arm_failure_snapshot(board, args, node_index))
        raise ArmRejectedError(
            f"{board.address}: ARM rejected, arm_result={arm_result}, "
            f"error={error_after!r}", evidence)
    return evidence


def record_arm_with_evidence(board: Board, args: argparse.Namespace,
                             node_index: int,
                             actions: list[dict[str, Any]]) -> dict[str, Any]:
    """Record both accepted and rejected ARM actions before cleanup."""
    try:
        evidence = arm_with_evidence(board, args, node_index)
    except ArmRejectedError as exc:
        actions.append(exc.evidence)
        raise
    actions.append(evidence)
    return evidence


def checked_ring_action(board: Board, action: str, command: str,
                        args: argparse.Namespace) -> dict[str, Any]:
    evidence = checked_action(board, command, args)
    return {"node": board.address, "action": action, **evidence}


def runtime_snapshot(board: Board, args: argparse.Namespace,
                     node_index: int) -> dict[str, int]:
    raw = parse_snapshot(
        board_command(board, "SYSTem:TDMA:RING:STATus?", args),
        RUNTIME_FIELDS, board.address)
    selected = dict(raw)
    selected["ring_local_node"] = selected.pop("ring_local_slot_id")
    selected["ring_reference_node"] = selected.pop("ring_reference_slot_id")
    selected["node_index"] = node_index
    return selected


def wait_runtime_started(board: Board, args: argparse.Namespace,
                         node_index: int) -> dict[str, int]:
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    last_error = ""
    while time.monotonic() < deadline:
        try:
            last = runtime_snapshot(board, args, node_index)
        except (OSError, RuntimeError) as exc:
            last_error = str(exc)
            time.sleep(0.1)
            continue
        if last["ring_enabled"] == 1 and last["ring_adapter_started"] == 1:
            return last
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: ARM timeout, last={last}, last_error={last_error}")


def wait_runtime_config_applied(board: Board, args: argparse.Namespace,
                                node_index: int) -> dict[str, int]:
    """Wait for Core1 to apply the newest config without requiring RUN yet."""
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    last_error = ""
    while time.monotonic() < deadline:
        try:
            last = runtime_snapshot(board, args, node_index)
        except (OSError, RuntimeError) as exc:
            last_error = str(exc)
            time.sleep(0.1)
            continue
        if last["ring_applied_config_seq"] == last["ring_config_seq"]:
            last["passed"] = 1
            return last
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: config apply timeout, last={last}, "
        f"last_error={last_error}")


def wait_runtime_stopped(board: Board, args: argparse.Namespace,
                         node_index: int) -> dict[str, int]:
    """Wait for Core1 to acknowledge the exact STOP configuration generation."""
    deadline = time.monotonic() + args.arm_wait
    last: dict[str, int] = {}
    last_error = ""
    while time.monotonic() < deadline:
        try:
            last = runtime_snapshot(board, args, node_index)
        except (OSError, RuntimeError) as exc:
            last_error = str(exc)
            time.sleep(0.1)
            continue
        if (last["ring_enabled"] == 0 and
                last["ring_adapter_started"] == 0 and
                last["ring_applied_config_seq"] == last["ring_config_seq"]):
            last["passed"] = 1
            return last
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: STOP acknowledgement timeout, last={last}, "
        f"last_error={last_error}")


def checked_stopped_ring_action(
        board: Board, action: str, command: str, args: argparse.Namespace,
        node_index: int) -> dict[str, Any]:
    """Retry an idempotent owner mutation only after proving STOPPED again."""
    retry_limit = int(getattr(args, "owner_action_retries", 3))
    if retry_limit <= 0:
        raise ValueError("owner action retries must be positive")
    rejected_attempts: list[dict[str, Any]] = []
    last_error = ""
    for attempt in range(1, retry_limit + 1):
        try:
            evidence = checked_ring_action(board, action, command, args)
            evidence["attempt_count"] = attempt
            evidence["rejected_attempts"] = rejected_attempts
            # Owner mutations advance config_seq asynchronously on Core1.
            # Do not let the next ARM observe a stopped ring with an older
            # applied generation; retain the exact acknowledgement as part of
            # the action evidence.
            evidence["stopped_readback"] = wait_runtime_stopped(
                board, args, node_index)
            return evidence
        except RuntimeError as exc:
            last_error = str(exc)
            if attempt == retry_limit:
                break
            stopped = wait_runtime_stopped(board, args, node_index)
            rejected_attempts.append({
                "attempt": attempt,
                "error": last_error,
                "stopped_readback": stopped,
            })
            time.sleep(0.1 * attempt)
    raise RuntimeError(
        f"{board.address}: {action} rejected after {retry_limit} STOPPED "
        f"attempts: {last_error}; rejected_attempts={rejected_attempts}")


def scalar_readback_with_retry(
        board: Board, command: str, args: argparse.Namespace,
        expected: int, label: str) -> dict[str, Any]:
    """Read one scalar with bounded retries and retain invalid responses."""
    retry_limit = int(getattr(args, "owner_action_retries", 3))
    if retry_limit <= 0:
        raise ValueError("owner action retries must be positive")
    rejected_readbacks: list[dict[str, Any]] = []
    for attempt in range(1, retry_limit + 1):
        raw = board_command(board, command, args)
        normalized = raw.strip().strip('"')
        try:
            observed = int(normalized, 0)
        except ValueError:
            rejected_readbacks.append({
                "attempt": attempt,
                "response": raw,
                "error": "non-integer scalar",
            })
        else:
            if observed == expected:
                return {
                    "value": observed,
                    "attempt_count": attempt,
                    "rejected_readbacks": rejected_readbacks,
                }
            rejected_readbacks.append({
                "attempt": attempt,
                "response": raw,
                "observed": observed,
                "expected": expected,
                "error": "readback mismatch",
            })
        if attempt < retry_limit:
            time.sleep(0.1 * attempt)
    raise RuntimeError(
        f"{board.address}: {label} readback did not reach {expected} after "
        f"{retry_limit} attempts; rejected_readbacks={rejected_readbacks}")


def flight_snapshot(board: Board, args: argparse.Namespace) -> dict[str, Any]:
    process = parse_snapshot(
        board_command(board, "SYSTem:TDMA:FLIGHT:PROCess?", args),
        PROCESS_FIELDS, f"{board.address}:process")
    fifo = parse_snapshot(
        board_command(board, "SYSTem:TDMA:FLIGHT:FIFO?", args),
        FIFO_FIELDS, f"{board.address}:fifo")
    process["local_node"] = process.pop("local_slot")
    fifo["tx_active_buffer"] = fifo.pop("tx_active_slot")
    return {"process": process, "fifo": fifo}


def activate_dpll_provisional(board: Board, args: argparse.Namespace,
                              config: dict[str, Any]) -> dict[str, Any]:
    action = checked_action(
        board, "SYSTem:SYNC:VDC:DPLL:PROVisional 1", args)
    raw = board_command(
        board, "SYSTem:SYNC:VDC:DPLL:PROVisional?", args)
    fields = [value.strip().strip('"') for value in raw.split(",")]
    if len(fields) != 7:
        raise RuntimeError(
            f"{board.address}: provisional readback field count "
            f"{len(fields)}, expected 7: {raw!r}")
    enabled_text = fields[0].upper()
    enabled = 1 if enabled_text in ("1", "TRUE") else 0
    try:
        values = [int(value, 0) for value in fields[1:]]
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid provisional readback {raw!r}") from exc
    (flags, entry_count, generation, topology_generation,
     schedule_crc32, table_crc32) = values
    if (enabled != 1 or (flags & (1 << 4)) == 0 or
            entry_count != int(config["node_count"]) or
            generation != int(config["calibration_generation"]) or
            topology_generation != int(config["topology_generation"]) or
            schedule_crc32 != int(config["schedule_crc32"]) or
            table_crc32 == 0):
        raise RuntimeError(
            f"{board.address}: provisional identity mismatch {raw!r}")
    return {
        "node": board.address,
        "action": "DPLL_PROVISIONAL_PATH",
        **action,
        "readback": {
            "enabled": enabled,
            "flags": flags,
            "entry_count": entry_count,
            "calibration_generation": generation,
            "topology_generation": topology_generation,
            "schedule_crc32": schedule_crc32,
            "table_crc32": table_crc32,
        },
    }


def physical_snapshot(board: Board, args: argparse.Namespace) -> dict[str, int]:
    return parse_snapshot(
        board_command(board, "SYSTem:SYNC:VDC:TDMA:PHYS?", args),
        PHYS_FIELDS, f"{board.address}:physical")


def crc_diagnostic_snapshot(board: Board,
                            args: argparse.Namespace) -> dict[str, int]:
    return parse_snapshot(
        board_command(board, "SYSTem:TDMA:FLIGHT:CRC:DIAGnostic?", args),
        CRC_DIAGNOSTIC_FIELDS, f"{board.address}:crc_diagnostic")


def arm_failure_snapshot(board: Board, args: argparse.Namespace,
                         node_index: int) -> dict[str, Any]:
    """Capture independent Core0 snapshots before STOP clears PHY errors."""
    readers = (
        ("physical", lambda: physical_snapshot(board, args)),
        ("runtime", lambda: runtime_snapshot(board, args, node_index)),
        ("flight", lambda: flight_snapshot(board, args)),
        ("crc_diagnostic", lambda: crc_diagnostic_snapshot(board, args)),
    )
    snapshots: dict[str, Any] = {}
    errors: dict[str, str] = {}
    for name, reader in readers:
        try:
            snapshots[name] = reader()
        except Exception as exc:  # noqa: BLE001 - retain partial evidence
            snapshots[name] = None
            errors[name] = f"{type(exc).__name__}: {exc}"
    return {
        "failure_snapshot": snapshots,
        "failure_snapshot_errors": errors,
    }


def sample_node(board: Board, args: argparse.Namespace,
                node_index: int) -> dict[str, Any]:
    return {
        "runtime": runtime_snapshot(board, args, node_index),
        "flight": flight_snapshot(board, args),
        "physical": physical_snapshot(board, args),
        "crc_diagnostic": crc_diagnostic_snapshot(board, args),
    }


def sample_all(ordered: list[Board], args: argparse.Namespace
               ) -> dict[str, dict[str, Any]]:
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        futures = {
            board.address: pool.submit(sample_node, board, args, node_index)
            for node_index, board in enumerate(ordered)
        }
        return {address: future.result()
                for address, future in futures.items()}


def sample_all_with_evidence(
        ordered: list[Board], args: argparse.Namespace
        ) -> tuple[dict[str, dict[str, Any]], dict[str, str]]:
    """Sample all Nodes while retaining a per-Node transport failure."""
    snapshots: dict[str, dict[str, Any]] = {}
    errors: dict[str, str] = {}
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        futures = {
            board.address: pool.submit(sample_node, board, args, node_index)
            for node_index, board in enumerate(ordered)
        }
        for address, future in futures.items():
            try:
                snapshots[address] = future.result()
            except Exception as exc:  # noqa: BLE001 - evidence must survive
                errors[address] = f"{type(exc).__name__}: {exc}"
    return snapshots, errors


def collect_soak_timeline(
        ordered: list[Board], args: argparse.Namespace, *,
        initial: dict[str, dict[str, Any]], window_s: float,
        sample_interval_s: float,
        progress: ProgressReporter | None = None) -> list[dict[str, Any]]:
    """Collect an anchored periodic timeline including both endpoints."""
    started = time.monotonic()
    timeline: list[dict[str, Any]] = [{
        "sample_index": 0,
        "elapsed_s": 0.0,
        "nodes": initial,
        "errors": {},
    }]
    targets: list[float] = []
    target = sample_interval_s
    while target < window_s:
        targets.append(target)
        target += sample_interval_s
    targets.append(window_s)
    for sample_index, target_s in enumerate(targets, start=1):
        remaining = started + target_s - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)
        nodes, errors = sample_all_with_evidence(ordered, args)
        timeline.append({
            "sample_index": sample_index,
            "elapsed_s": round(time.monotonic() - started, 6),
            "nodes": nodes,
            "errors": errors,
        })
        if progress is not None:
            progress.emit(
                "soak_sample", sample_index=sample_index,
                target_s=target_s, observed_board_count=len(nodes),
                transport_errors=errors,
                counters={address: {
                    "ring_enabled": row["runtime"]["ring_enabled"],
                    "ring_up_tx_sequence": row["runtime"][
                        "ring_up_tx_sequence"],
                    "ring_down_rx_sequence": row["runtime"][
                        "ring_down_rx_sequence"],
                    "ring_adapter_rx_bad_count": row["runtime"][
                        "ring_adapter_rx_bad_count"],
                    "receive_rejected_count": row["flight"]["process"][
                        "receive_rejected_count"],
                } for address, row in nodes.items()})
    return timeline


def u32_delta(before: int, after: int) -> int:
    return (after - before) & 0xFFFFFFFF


def counter_deltas(before: dict[str, int], after: dict[str, int],
                   fields: tuple[str, ...]) -> dict[str, int]:
    return {field: u32_delta(before[field], after[field]) for field in fields}


STARTUP_RUNTIME_ERROR_COUNTERS = (
    "ring_adapter_rx_bad_count",
    "ring_adapter_rx_transport_bad_count",
    "ring_adapter_rx_schedule_bad_count",
    "ring_adapter_rx_profile_bad_count",
)

STARTUP_PROCESS_ERROR_COUNTERS = (
    "receive_rejected_count",
    "receive_missing_count",
    "rx_bitmap_incomplete_count",
)


def startup_barrier_interval_errors(
        previous: dict[str, Any], current: dict[str, Any], *,
        node_index: int, node_count: int,
        require_process_image: bool) -> list[str]:
    """Gate one post-start receive opportunity.

    Pipeline fill is allowed before this interval. A stable interval requires
    advancing physical sequences and no new transport/process rejection.
    """
    errors = snapshot_health_errors(
        current, node_index, node_count,
        require_process_image=require_process_image)
    previous_runtime = previous["runtime"]
    current_runtime = current["runtime"]
    for field in ("ring_up_tx_sequence", "ring_down_rx_sequence"):
        if u32_delta(previous_runtime[field], current_runtime[field]) == 0:
            errors.append(f"{field}_not_advancing")
    for field in STARTUP_RUNTIME_ERROR_COUNTERS:
        if u32_delta(previous_runtime[field], current_runtime[field]) != 0:
            errors.append(f"{field}_grew")
    if require_process_image:
        previous_process = previous["flight"]["process"]
        current_process = current["flight"]["process"]
        if u32_delta(
                previous_process["receive_accepted_sequence"],
                current_process["receive_accepted_sequence"]) == 0:
            errors.append("receive_accepted_sequence_not_advancing")
        for field in STARTUP_PROCESS_ERROR_COUNTERS:
            if u32_delta(previous_process[field], current_process[field]) != 0:
                errors.append(f"{field}_grew")
    return errors


def wait_startup_barrier(
        ordered: list[Board], args: argparse.Namespace, *,
        startup_before: dict[str, dict[str, Any]],
        require_process_image: bool,
        continue_on_failure: bool = False,
        progress: ProgressReporter | None = None
        ) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    """Wait for an explicit, replayable transition into steady state."""
    timeout_s = float(args.startup_timeout_s)
    stable_required = int(args.startup_stable_samples)
    poll_interval_s = float(args.startup_poll_interval_s)
    if timeout_s <= 0 or stable_required <= 0 or poll_interval_s <= 0:
        raise ValueError("startup barrier arguments must be positive")

    board_ids = [board.address for board in ordered]
    previous = startup_before
    stable_count = 0
    started = time.monotonic()
    samples: list[dict[str, Any]] = []
    while True:
        remaining = started + timeout_s - time.monotonic()
        if remaining <= 0:
            break
        time.sleep(min(poll_interval_s, remaining))
        current, transport_errors = sample_all_with_evidence(ordered, args)
        node_errors: dict[str, list[str]] = {}
        for node_index, address in enumerate(board_ids):
            if address in transport_errors:
                node_errors[address] = [
                    f"sample_transport:{transport_errors[address]}"]
                continue
            if address not in current or address not in previous:
                node_errors[address] = ["startup_snapshot_missing"]
                continue
            errors = startup_barrier_interval_errors(
                previous[address], current[address], node_index=node_index,
                node_count=len(board_ids),
                require_process_image=require_process_image)
            if errors:
                node_errors[address] = errors
        interval_passed = not node_errors
        stable_count = stable_count + 1 if interval_passed else 0
        samples.append({
            "sample_index": len(samples),
            "elapsed_s": round(time.monotonic() - started, 6),
            "passed": interval_passed,
            "stable_count": stable_count,
            "errors": node_errors,
        })
        if progress is not None:
            progress.emit(
                "startup_sample", sample_index=len(samples) - 1,
                passed=interval_passed, stable_count=stable_count,
                required_stable_count=stable_required,
                observed_board_count=len(current), errors=node_errors,
                transport_errors=transport_errors)
        if not transport_errors and set(current) == set(board_ids):
            previous = current
        if stable_count >= stable_required:
            startup_deltas: dict[str, Any] = {}
            for address in board_ids:
                before = startup_before[address]
                after = current[address]
                startup_deltas[address] = {
                    "runtime_errors": counter_deltas(
                        before["runtime"], after["runtime"],
                        STARTUP_RUNTIME_ERROR_COUNTERS),
                    "process_errors": (counter_deltas(
                        before["flight"]["process"],
                        after["flight"]["process"],
                        STARTUP_PROCESS_ERROR_COUNTERS)
                        if require_process_image else {}),
                    "accepted_sequence_delta": (u32_delta(
                        before["flight"]["process"][
                            "receive_accepted_sequence"],
                        after["flight"]["process"][
                            "receive_accepted_sequence"])
                        if require_process_image else 0),
                }
            return current, {
                "passed": True,
                "timeout_s": timeout_s,
                "stable_samples_required": stable_required,
                "stable_samples_observed": stable_count,
                "startup_pipeline_fill_deltas": startup_deltas,
                "samples": samples,
            }
    evidence = {
        "passed": False,
        "timeout_s": timeout_s,
        "stable_samples_required": stable_required,
        "stable_samples_observed": stable_count,
        "samples": samples,
    }
    if continue_on_failure:
        return previous, evidence
    raise RuntimeError(
        "explicit startup barrier timed out: " +
        json.dumps(evidence, separators=(",", ":")))


def validate_tx_seed(flight_before: dict[str, Any],
                     flight_after: dict[str, Any]
                     ) -> tuple[list[str], dict[str, int]]:
    """Validate the one-shot Core0 TX publication before ring service starts."""
    fields = ("tx_publish_count", "tx_publish_reject_count")
    deltas = counter_deltas(
        flight_before["fifo"], flight_after["fifo"], fields)
    errors: list[str] = []
    if deltas["tx_publish_count"] == 0:
        errors.append("fifo_tx_not_published")
    if deltas["tx_publish_reject_count"] != 0:
        errors.append("fifo_tx_seed_rejected")
    return errors, deltas


def validate_fifo_reset(flight: dict[str, Any]) -> list[str]:
    """Require a fully reclaimed STOPPED-session FIFO before priming."""
    fifo = flight["fifo"]
    checks = (
        (fifo["tx_ready_count"] == 0, "fifo_reset_tx_ready"),
        (fifo["tx_active_buffer"] == 0xFFFFFFFF,
         "fifo_reset_tx_active"),
        (fifo["tx_active_generation"] == 0,
         "fifo_reset_tx_generation"),
        (fifo["rx_queued_count"] == 0, "fifo_reset_rx_queued"),
        (fifo["rx_parse_count"] == 0, "fifo_reset_rx_parse"),
    )
    return [error for passed, error in checks if not passed]


def expected_flight_phase(config: dict[str, Any],
                          node_index: int) -> dict[str, int]:
    """Resolve the exact matrix row fields that one physical Node must load."""
    node_count = int(config["node_count"])
    if not 0 <= node_index < node_count:
        raise ValueError("node index is outside the physical loop")
    links = config.get("links")
    if not isinstance(links, list) or len(links) != node_count:
        raise ValueError("resolved config links are incomplete")
    marker_links = [
        link for link in links
        if int(link["marker_destination_node"]) == node_index
    ]
    data_links = [
        link for link in links
        if int(link["data_destination_node"]) == node_index
    ]
    if len(marker_links) != 1 or len(data_links) != 1:
        raise ValueError(
            "resolved topology must provide exactly one MARK and DATA input "
            "per Node")
    marker_link = marker_links[0]
    data_link = data_links[0]
    return {
        "flight_marker_offset_sample_count":
            int(marker_link["marker_offset_sample_count"]),
        "flight_sck_offset_sample_count":
            int(marker_link["sck_offset_sample_count"]),
        "flight_data_offset_sample_count":
            int(data_link["data_offset_sample_count"]),
        "flight_marker_phase_delay_cycles":
            int(marker_link["marker_phase_delay_cycles"]),
        "flight_sck_phase_delay_cycles":
            int(marker_link["sck_phase_delay_cycles"]),
        "flight_data_phase_delay_cycles":
            int(data_link["data_phase_delay_cycles"]),
    }


def validate_flight_phase_readback(
        physical: dict[str, int], expected: dict[str, int]) -> list[str]:
    """Fail closed if runtime PIO parameters differ from the selected row."""
    errors: list[str] = []
    for field, value in expected.items():
        if physical.get(field) != value:
            errors.append(field.removeprefix("flight_") + "_mismatch")
    return errors


def validate_node(node_index: int, node_count: int,
                   runtime_before: dict[str, int],
                   runtime_after: dict[str, int],
                   flight_before: dict[str, Any],
                   flight_after: dict[str, Any],
                   seed_errors: list[str] | None = None,
                   seed_deltas: dict[str, int] | None = None,
                   *, require_process_image: bool = True,
                   physical_after: dict[str, int] | None = None,
                   expected_physical: dict[str, int] | None = None
                   ) -> tuple[list[str], dict[str, Any]]:
    errors: list[str] = []
    if seed_errors:
        errors.extend(seed_errors)
    runtime_delta_fields = (
        "ring_seq", "ring_adapter_service_count", "ring_up_tx_sequence",
        "ring_down_rx_sequence", "ring_idle_beacon_tx_count",
        "ring_idle_beacon_rx_count", "ring_adapter_tx_count",
        "ring_adapter_rx_count", "ring_adapter_rx_bad_count",
        "ring_adapter_rx_transport_bad_count",
        "ring_adapter_rx_schedule_bad_count",
        "ring_adapter_rx_profile_bad_count",
    )
    runtime_deltas = counter_deltas(
        runtime_before, runtime_after, runtime_delta_fields)
    process_delta_fields = (
        "map_apply_count", "input_bytes", "output_bytes",
        "map_reject_count", "length_reject_count",
        "tx_unavailable_count", "rx_bitmap_scan_count",
        "rx_bitmap_hit_count", "rx_bitmap_duplicate_count",
        "rx_bitmap_present_count", "rx_bitmap_incomplete_count",
        "receive_accepted_count", "receive_rejected_count",
        "receive_missing_count",
    )
    fifo_delta_fields = (
        "tx_publish_count", "tx_publish_reject_count", "tx_acquire_count",
        "tx_image_stale_count", "tx_reuse_count", "tx_release_count",
        "rx_publish_count", "rx_mirror_drop_count", "rx_publish_drop_count",
        "rx_acquire_count", "rx_release_count",
    )
    process_deltas = counter_deltas(
        flight_before["process"], flight_after["process"],
        process_delta_fields)
    fifo_deltas = counter_deltas(
        flight_before["fifo"], flight_after["fifo"], fifo_delta_fields)
    feedback_sequence_gap = u32_delta(
        runtime_after["ring_down_rx_sequence"],
        runtime_after["ring_up_tx_sequence"])
    feedback_sequence_ok = (
        feedback_sequence_gap <= 1 if node_index == 0 else
        feedback_sequence_gap == 0)
    feedback_crc_comparable = feedback_sequence_gap == 0
    feedback_crc_match = (
        not feedback_crc_comparable or
        runtime_after["ring_up_tx_frame_crc32"] ==
        runtime_after["ring_down_rx_frame_crc32"])

    checks = [
        (runtime_after["ring_enabled"] == 1, "ring_not_enabled"),
        (runtime_after["ring_adapter_started"] == 1,
         "adapter_not_started"),
        (runtime_after["ring_node_count"] == node_count,
         "node_count_mismatch"),
        (runtime_after["ring_local_node"] == node_index,
         "local_node_mismatch"),
        (runtime_after["ring_reference_node"] == 0,
         "reference_node_mismatch"),
        (runtime_after["ring_up_running"] == 1, "up_not_running"),
        (runtime_after["ring_down_running"] == 1, "down_not_running"),
        (runtime_deltas["ring_seq"] > 0, "ring_sequence_not_growing"),
        (runtime_deltas["ring_adapter_service_count"] > 0,
         "adapter_service_not_growing"),
        (runtime_deltas["ring_up_tx_sequence"] > 0,
         "tx_sequence_not_growing"),
        (runtime_deltas["ring_down_rx_sequence"] > 0,
         "rx_sequence_not_growing"),
        (runtime_deltas["ring_adapter_tx_count"] > 0,
         "adapter_tx_not_growing"),
        (runtime_deltas["ring_adapter_rx_count"] > 0,
         "adapter_rx_not_growing"),
        (runtime_deltas["ring_adapter_rx_bad_count"] == 0,
         "adapter_rx_bad_grew"),
        (runtime_after["ring_up_tx_frame_crc32"] != 0,
         "tx_crc_missing"),
        (runtime_after["ring_down_rx_frame_crc32"] != 0,
         "rx_crc_missing"),
        (feedback_sequence_ok, "feedback_sequence_out_of_window"),
        (feedback_crc_match, "crc_mismatch"),
        (runtime_after["ring_adapter_last_error"] == 0,
         "adapter_error"),
    ]
    if require_process_image:
        checks.extend((
            (flight_after["process"]["configured"] == 1,
             "flight_map_not_configured"),
            (flight_after["process"]["active"] == 1,
             "flight_map_not_active"),
            (flight_after["process"]["local_node"] == node_index,
             "flight_local_node_mismatch"),
            (flight_after["process"]["receive_version"] >= 1,
             "receive_health_version_invalid"),
            (flight_after["process"]["receive_configured"] == 1,
             "receive_health_not_configured"),
            (flight_after["process"]["receive_state"] == 1,
             "receive_image_not_valid"),
            (flight_after["process"]["receive_consecutive_failure_count"] == 0,
             "receive_failure_streak_active"),
            (flight_after["process"]["receive_accepted_segment_mask"] ==
             flight_after["process"]["receive_expected_segment_mask"],
             "receive_segment_bitmap_incomplete"),
            (flight_after["process"]["receive_accepted_wkc"] ==
             flight_after["process"]["receive_expected_wkc"],
             "receive_wkc_incomplete"),
            (flight_after["process"]["receive_accepted_map_generation"] ==
             flight_after["process"]["map_generation"],
             "receive_map_generation_mismatch"),
            (flight_after["process"]["receive_accepted_payload_size"] ==
             flight_after["process"]["payload_size"],
             "receive_payload_size_mismatch"),
            (process_deltas["receive_accepted_count"] > 0,
             "receive_image_not_accepted"),
            (process_deltas["receive_rejected_count"] == 0,
             "receive_reject_grew"),
            (process_deltas["receive_missing_count"] == 0,
             "receive_missing_grew"),
            (process_deltas["rx_bitmap_incomplete_count"] == 0,
             "receive_bitmap_incomplete_grew"),
            (fifo_deltas["tx_publish_reject_count"] == 0,
             "fifo_tx_reject_grew"),
            (fifo_deltas["rx_mirror_drop_count"] == 0,
             "fifo_rx_mirror_drop_grew"),
            (fifo_deltas["rx_publish_drop_count"] == 0,
             "fifo_rx_publish_drop_grew"),
            (process_deltas["map_reject_count"] == 0,
             "flight_map_reject_grew"),
            (process_deltas["length_reject_count"] == 0,
             "flight_length_reject_grew"),
            (fifo_deltas["tx_acquire_count"] > 0,
             "fifo_tx_not_acquired"),
            (fifo_deltas["rx_publish_count"] > 0,
             "fifo_rx_not_published"),
            (fifo_deltas["rx_acquire_count"] > 0,
             "fifo_rx_not_acquired"),
            (fifo_deltas["rx_release_count"] > 0,
             "fifo_rx_not_released"),
        ))
    if physical_after is not None:
        expected_persona = (
            11 if node_index == 0 else (13 if require_process_image else 12))
        checks.append((
            physical_after.get("program_persona") == expected_persona,
            "physical_flight_persona_mismatch"))
        if expected_physical is not None:
            errors.extend(validate_flight_phase_readback(
                physical_after, expected_physical))
        if require_process_image and node_index != 0:
            checks.extend((
                (physical_after.get("overlay_prepare_count", 0) > 0,
                 "physical_overlay_not_prepared"),
                (physical_after.get("overlay_prepare_fail_count", 0) == 0,
                 "physical_overlay_prepare_failed"),
                (physical_after.get("overlay_replacement_byte_count", 0) > 0,
                 "physical_overlay_not_replaced"),
            ))
    for passed, reason in checks:
        if not passed:
            errors.append(reason)
    if (require_process_image and node_index != 0 and
            process_deltas["map_apply_count"] == 0):
        errors.append("flight_map_not_applied")
    return errors, {
        "runtime": runtime_deltas,
        "process": process_deltas,
        "fifo": fifo_deltas,
        "feedback_identity": {
            "tx_sequence": runtime_after["ring_up_tx_sequence"],
            "rx_sequence": runtime_after["ring_down_rx_sequence"],
            "sequence_gap": feedback_sequence_gap,
            "crc_comparable": feedback_crc_comparable,
            "crc_match": feedback_crc_match,
        },
        "fifo_seed": seed_deltas or {},
        "fifo_gauges": {
            "before": {
                "tx_ready_count": flight_before["fifo"]["tx_ready_count"],
                "tx_active_buffer": flight_before["fifo"]["tx_active_buffer"],
                "tx_active_generation":
                    flight_before["fifo"]["tx_active_generation"],
                "rx_queued_count": flight_before["fifo"]["rx_queued_count"],
                "rx_parse_count": flight_before["fifo"]["rx_parse_count"],
            },
            "after": {
                "tx_ready_count": flight_after["fifo"]["tx_ready_count"],
                "tx_active_buffer": flight_after["fifo"]["tx_active_buffer"],
                "tx_active_generation":
                    flight_after["fifo"]["tx_active_generation"],
                "rx_queued_count": flight_after["fifo"]["rx_queued_count"],
                "rx_parse_count": flight_after["fifo"]["rx_parse_count"],
            },
        },
    }


def snapshot_health_errors(
        snapshot: dict[str, Any], node_index: int, node_count: int, *,
        require_process_image: bool) -> list[str]:
    """Classify a single periodic sample without relying on later recovery."""
    runtime = snapshot["runtime"]
    flight = snapshot["flight"]
    physical = snapshot["physical"]
    checks = (
        (runtime["ring_enabled"] == 1, "ring_not_enabled"),
        (runtime["ring_adapter_started"] == 1, "adapter_not_started"),
        (runtime["ring_node_count"] == node_count, "node_count_mismatch"),
        (runtime["ring_local_node"] == node_index, "local_node_mismatch"),
        (runtime["ring_reference_node"] == 0, "reference_node_mismatch"),
        (runtime["ring_up_running"] == 1, "up_not_running"),
        (runtime["ring_down_running"] == 1, "down_not_running"),
        (runtime["ring_adapter_last_error"] == 0, "adapter_error"),
        (physical["last_error"] == 0, "physical_error"),
        (physical["overlay_last_error"] == 0, "overlay_error"),
    )
    errors = [reason for passed, reason in checks if not passed]
    if require_process_image:
        process = flight["process"]
        if process["configured"] != 1:
            errors.append("flight_map_not_configured")
        if process["active"] != 1:
            errors.append("flight_map_not_active")
        if process["local_node"] != node_index:
            errors.append("flight_local_node_mismatch")
        if process["receive_configured"] != 1:
            errors.append("receive_health_not_configured")
        if process["receive_state"] != 1:
            errors.append("receive_image_not_valid")
        if process["receive_consecutive_failure_count"] != 0:
            errors.append("receive_failure_streak_active")
        if (process["receive_accepted_segment_mask"] !=
                process["receive_expected_segment_mask"]):
            errors.append("receive_segment_bitmap_incomplete")
        if process["receive_accepted_wkc"] != process["receive_expected_wkc"]:
            errors.append("receive_wkc_incomplete")
    return errors


def _counter_regressions(
        before: dict[str, int], after: dict[str, int],
        fields: tuple[str, ...], group: str) -> list[str]:
    """Reject a counter reset while still allowing a natural uint32 wrap."""
    regressions: list[str] = []
    for field in fields:
        if after[field] < before[field] and u32_delta(
                before[field], after[field]) > 0x7FFFFFFF:
            regressions.append(f"{group}.{field}")
    return regressions


def validate_soak_timeline(
        timeline: list[dict[str, Any]], board_ids: list[str],
        config: dict[str, Any], *, require_process_image: bool
        ) -> dict[str, Any]:
    """Gate every soak interval and retain down/recovery/error evidence."""
    if len(timeline) < 2:
        return {"passed": False, "errors": ["timeline_too_short"],
                "nodes": {}}
    runtime_counters = (
        "ring_seq", "ring_adapter_service_count", "ring_up_tx_sequence",
        "ring_down_rx_sequence", "ring_idle_beacon_tx_count",
        "ring_idle_beacon_rx_count", "ring_adapter_tx_count",
        "ring_adapter_rx_count", "ring_adapter_rx_bad_count",
        "ring_adapter_rx_transport_bad_count",
        "ring_adapter_rx_schedule_bad_count",
        "ring_adapter_rx_profile_bad_count",
    )
    process_counters = (
        "map_apply_count", "input_bytes", "output_bytes",
        "map_reject_count", "length_reject_count", "tx_unavailable_count",
        "rx_bitmap_scan_count", "rx_bitmap_hit_count",
        "rx_bitmap_duplicate_count", "rx_bitmap_present_count",
        "rx_bitmap_incomplete_count", "receive_accepted_count",
        "receive_rejected_count", "receive_missing_count",
    )
    fifo_counters = (
        "tx_publish_count", "tx_publish_reject_count", "tx_acquire_count",
        "tx_image_stale_count", "tx_reuse_count", "tx_release_count",
        "rx_publish_count", "rx_mirror_drop_count", "rx_publish_drop_count",
        "rx_acquire_count", "rx_release_count",
    )
    physical_fault_counters = (
        "rx_bad_count", "rx_partial_count", "rx_stall_count",
        "tx_timeout_count", "program_switch_fail_count",
        "origin_clock_timeout_count", "origin_data_timeout_count",
        "origin_recovery_count", "overlay_prepare_fail_count",
    )
    node_results: dict[str, Any] = {}
    all_errors: list[str] = []
    for node_index, address in enumerate(board_ids):
        samples: list[dict[str, Any]] = []
        unhealthy_sample_count = 0
        observation_failure_count = 0
        runtime_unhealthy_sample_count = 0
        down_event_count = 0
        recovery_count = 0
        previous_runtime_healthy: bool | None = None
        for entry in timeline:
            transport_error = entry.get("errors", {}).get(address, "")
            snapshot = entry.get("nodes", {}).get(address)
            observation_missing = bool(transport_error or snapshot is None)
            if observation_missing:
                health_errors = [
                    f"sample_transport:{transport_error or 'snapshot_missing'}"]
                observation_failure_count += 1
            else:
                health_errors = snapshot_health_errors(
                    snapshot, node_index, len(board_ids),
                    require_process_image=require_process_image)
                runtime_healthy = not health_errors
                if not runtime_healthy:
                    runtime_unhealthy_sample_count += 1
                if (previous_runtime_healthy is True and
                        not runtime_healthy):
                    down_event_count += 1
                if (previous_runtime_healthy is False and
                        runtime_healthy):
                    recovery_count += 1
                previous_runtime_healthy = runtime_healthy
            sample_passed = not health_errors
            if not sample_passed:
                unhealthy_sample_count += 1
            samples.append({
                "sample_index": entry["sample_index"],
                "elapsed_s": entry["elapsed_s"],
                "passed": sample_passed,
                "observation_missing": observation_missing,
                "errors": health_errors,
            })

        intervals: list[dict[str, Any]] = []
        counter_regressions: list[dict[str, Any]] = []
        physical_fault_growth: list[dict[str, Any]] = []
        for before_entry, after_entry in zip(timeline, timeline[1:]):
            before = before_entry.get("nodes", {}).get(address)
            after = after_entry.get("nodes", {}).get(address)
            interval_errors: list[str] = []
            deltas: dict[str, Any] = {}
            regressions: list[str] = []
            fault_deltas: dict[str, int] = {}
            if before is None or after is None:
                interval_errors.append("sample_missing")
            else:
                interval_errors, deltas = validate_node(
                    node_index, len(board_ids), before["runtime"],
                    after["runtime"], before["flight"], after["flight"],
                    require_process_image=require_process_image,
                    physical_after=after["physical"],
                    expected_physical=expected_flight_phase(
                        config, node_index))
                regressions.extend(_counter_regressions(
                    before["runtime"], after["runtime"], runtime_counters,
                    "runtime"))
                regressions.extend(_counter_regressions(
                    before["flight"]["process"],
                    after["flight"]["process"], process_counters,
                    "process"))
                regressions.extend(_counter_regressions(
                    before["flight"]["fifo"], after["flight"]["fifo"],
                    fifo_counters, "fifo"))
                fault_deltas = counter_deltas(
                    before["physical"], after["physical"],
                    physical_fault_counters)
                if any(fault_deltas.values()):
                    interval_errors.append("physical_fault_counter_grew")
                    physical_fault_growth.append({
                        "from_sample": before_entry["sample_index"],
                        "to_sample": after_entry["sample_index"],
                        "deltas": fault_deltas,
                    })
                if regressions:
                    interval_errors.append("counter_regression")
                    counter_regressions.append({
                        "from_sample": before_entry["sample_index"],
                        "to_sample": after_entry["sample_index"],
                        "fields": regressions,
                    })
            intervals.append({
                "from_sample": before_entry["sample_index"],
                "to_sample": after_entry["sample_index"],
                "duration_s": round(
                    after_entry["elapsed_s"] - before_entry["elapsed_s"], 6),
                "passed": not interval_errors,
                "errors": interval_errors,
                "deltas": deltas,
                "physical_fault_deltas": fault_deltas,
            })
        node_errors: list[str] = []
        if observation_failure_count:
            node_errors.append("periodic_observation_missing")
        if runtime_unhealthy_sample_count:
            node_errors.append("unhealthy_periodic_sample")
        if any(not interval["passed"] for interval in intervals):
            node_errors.append("periodic_interval_gate_failed")
        if down_event_count:
            node_errors.append("runtime_down_event")
        if recovery_count:
            node_errors.append("runtime_recovery_observed")
        first_snapshot = timeline[0].get("nodes", {}).get(address)
        last_snapshot = timeline[-1].get("nodes", {}).get(address)
        receive_quality: dict[str, Any] = {}
        if first_snapshot is not None and last_snapshot is not None:
            runtime_delta = counter_deltas(
                first_snapshot["runtime"], last_snapshot["runtime"],
                ("ring_adapter_rx_count", "ring_adapter_rx_bad_count",
                 "ring_adapter_rx_transport_bad_count",
                 "ring_adapter_rx_schedule_bad_count",
                 "ring_adapter_rx_profile_bad_count"))
            good_frames = runtime_delta["ring_adapter_rx_count"]
            bad_frames = runtime_delta["ring_adapter_rx_bad_count"]
            total_frames = good_frames + bad_frames
            observed_rate = bad_frames / total_frames if total_frames else None
            receive_quality = {
                "rx_good_frame_count": good_frames,
                "rx_bad_frame_count": bad_frames,
                "rx_total_frame_count": total_frames,
                "rx_transport_bad_frame_count": runtime_delta[
                    "ring_adapter_rx_transport_bad_count"],
                "rx_schedule_bad_frame_count": runtime_delta[
                    "ring_adapter_rx_schedule_bad_count"],
                "rx_profile_bad_frame_count": runtime_delta[
                    "ring_adapter_rx_profile_bad_count"],
                "observed_frame_error_rate": observed_rate,
                "observed_frame_error_ppm": (
                    observed_rate * 1_000_000.0
                    if observed_rate is not None else None),
                "mean_frames_per_error": (
                    total_frames / bad_frames if bad_frames else None),
                "zero_error_95_upper_bound_frame_error_rate": (
                    min(1.0, 3.0 / total_frames)
                    if total_frames and bad_frames == 0 else None),
            }
        node_results[address] = {
            "node_index": node_index,
            "passed": not node_errors,
            "errors": node_errors,
            "sample_count": len(samples),
            "interval_count": len(intervals),
            "unhealthy_sample_count": unhealthy_sample_count,
            "observation_failure_count": observation_failure_count,
            "runtime_unhealthy_sample_count":
                runtime_unhealthy_sample_count,
            "down_event_count": down_event_count,
            "recovery_count": recovery_count,
            "counter_regressions": counter_regressions,
            "physical_fault_growth": physical_fault_growth,
            "receive_quality": receive_quality,
            "samples": samples,
            "intervals": intervals,
        }
        all_errors.extend(f"{address}:{error}" for error in node_errors)
    quality_rows = [
        (address, node["receive_quality"])
        for address, node in node_results.items()
        if node["receive_quality"].get("observed_frame_error_rate") is not None
    ]
    worst_receive_quality = {}
    if quality_rows:
        worst_address, worst_quality = max(
            quality_rows,
            key=lambda row: row[1]["observed_frame_error_rate"])
        worst_receive_quality = {
            "board_id": worst_address,
            "node_index": node_results[worst_address]["node_index"],
            **worst_quality,
        }
    return {
        "passed": not all_errors,
        "errors": all_errors,
        "timeline_sample_count": len(timeline),
        "worst_receive_quality": worst_receive_quality,
        "nodes": node_results,
    }


def stopped_snapshot(board: Board, args: argparse.Namespace,
                     node_index: int) -> dict[str, int]:
    snapshot = runtime_snapshot(board, args, node_index)
    snapshot["passed"] = int(
        snapshot["ring_enabled"] == 0 and
        snapshot["ring_adapter_started"] == 0)
    return snapshot


def capture_ring_waveforms(
        ordered: list[Board], args: argparse.Namespace, *,
        calibration_generation: int, capture_epoch: int,
        out_dir: Path,
        original_load_masks: dict[str, int] | None = None
        ) -> dict[str, Any]:
    capture_dir = out_dir / "captures"
    load_masks = original_load_masks or {}
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        saved = list(pool.map(
            lambda board: save_ring_capture(
                board, args,
                calibration_generation=calibration_generation,
                capture_epoch=capture_epoch,
                original_load_mask=load_masks.get(board.address)),
            ordered))
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        futures = [
            pool.submit(
                download_ring_capture, board, capture_file, args,
                capture_dir / f"node{node_index}_ring_capture.json")
            for node_index, (board, capture_file) in
            enumerate(zip(ordered, saved))
        ]
        downloaded = [future.result() for future in futures]
    return {
        "capture_epoch": capture_epoch,
        "capture_completed": True,
        "saved": saved,
        "downloaded": [
            {key: value for key, value in row.items() if key != "capture"}
            for row in downloaded],
    }


def analyze_ring_waveforms(capture: dict[str, Any], config: dict[str, Any],
                           args: argparse.Namespace, out_dir: Path
                           ) -> dict[str, Any]:
    """Analyze only after every node's raw capture has been downloaded."""
    downloaded = capture.get("downloaded", [])
    if not capture.get("capture_completed") or not isinstance(downloaded, list):
        raise ValueError("raw ring capture is incomplete")
    return analyze_capture_set(
        config,
        [Path(str(row["local_path"])) for row in downloaded],
        out_dir / "analysis", args.waveform_window_ns,
        frequency_tolerance_percent=args.sck_frequency_tolerance_percent,
        duty_tolerance_percent=args.sck_duty_tolerance_percent)


def main() -> int:
    args = parse_args()
    # TRN-03 owns the complete four-board transaction. Keep exactly one
    # shared SerialSession per board until summary handoff; --short-open is a
    # diagnostic fallback for CDC recovery only.
    args.keep_open = not args.short_open
    raw_config = json.loads(args.config.read_text(encoding="utf-8"))
    config = load_config(
        args.config, args.offset_row_id,
        allow_unsafe_sck=args.diagnostic_continue,
        allow_unsafe_data=args.diagnostic_continue)
    board_ids = list(args.board_id)
    if len(board_ids) != config["node_count"] or len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique and match config node_count")
    try:
        level = resolve_profile_level(
            raw_config.get("profile_level"), args.level)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if args.cycles <= 0 or args.cycles > 65536 or args.cycles % 8:
        raise SystemExit("cycles must be an 8-cycle multiple in [8, 65536]")
    if args.dpll_provisional and args.stage != "process-image":
        raise SystemExit("--dpll-provisional requires --stage process-image")
    if (args.window_s <= 0 or args.sample_interval_s <= 0 or
            args.startup_timeout_s <= 0 or
            args.startup_stable_samples <= 0 or
            args.startup_poll_interval_s <= 0 or
            args.read_timeout <= 0 or args.action_timeout <= 0 or
            args.read_timeout > args.timeout or
            args.action_timeout > args.timeout or
            args.capture_timeout <= 0 or args.capture_latch_retries < 0 or
            args.waveform_window_ns <= 0 or
            args.sck_frequency_tolerance_percent < 0 or
            args.sck_duty_tolerance_percent < 0):
        raise SystemExit("window/soak/startup arguments must be positive")
    args.board_ids = board_ids
    plan = {
        "phase": "TRN-03B",
        "board_ids_requested": board_ids,
        "profile_level": level,
        "config": str(args.config),
        "calibration_generation": config["calibration_generation"],
        "offset_row_id": config["offset_row_id"],
        "offset_row": config["offset_row"],
        "cycles": args.cycles,
        "clock_train": args.clock_train,
        "clock_evidence": args.clock_evidence,
        "window_s": args.window_s,
        "sample_interval_s": args.sample_interval_s,
        "startup_timeout_s": args.startup_timeout_s,
        "startup_stable_samples": args.startup_stable_samples,
        "startup_poll_interval_s": args.startup_poll_interval_s,
        "diagnostic_continue": args.diagnostic_continue,
        "waveform_window_ns": args.waveform_window_ns,
        "sck_frequency_tolerance_percent":
            args.sck_frequency_tolerance_percent,
        "sck_duty_tolerance_percent": args.sck_duty_tolerance_percent,
        "stage": args.stage,
        "dpll_provisional": args.dpll_provisional,
        "leave_running_requested": args.leave_running,
    }
    if args.dry_run:
        print(json.dumps(plan, ensure_ascii=False, indent=2))
        return 0

    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn03b_closed_loop_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    progress = ProgressReporter(out_dir / "progress.json")
    progress.emit(
        "initializing", board_ids=board_ids, stage=args.stage,
        diagnostic_continue=args.diagnostic_continue)

    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    ordered = order_boards_by_board_no(boards, board_ids, args)
    progress.emit(
        "boards_discovered",
        boards=[{"board_id": board.address, "port": board.port}
                for board in ordered])
    board_ids = [board.address for board in ordered]
    plan["board_ids_in_physical_node_order"] = board_ids
    expected_order = config.get("node_ids_in_loop_order")
    if expected_order is not None and list(expected_order) != board_ids:
        raise SystemExit(
            "matrix topology identity does not match calibrated BOARD:NO order")
    if args.expected_build:
        wrong = {board.address: board.build for board in ordered
                 if board.build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")
    start_order = ordered[1:] + ordered[:1]
    actions: list[dict[str, Any]] = []
    stage_results: list[dict[str, Any]] = []
    nodes: dict[str, Any] = {}
    fifo_reset: dict[str, Any] = {}
    fifo_seed: dict[str, Any] = {}
    stopped: dict[str, Any] = {}
    running_handoff: dict[str, Any] = {}
    ring_capture: dict[str, Any] = {}
    ring_analysis: dict[str, Any] = {}
    capture_load_masks: dict[str, int] = {}
    soak_timeline: list[dict[str, Any]] = []
    soak_validation: dict[str, Any] = {}
    startup_barrier: dict[str, Any] = {}
    dpll_schedule_before: dict[str, Any] = {}
    dpll_schedule_required = bool(
        args.stage == "process-image" and args.dpll_provisional)
    dpll_schedule_gate: dict[str, Any] = {
        "required": dpll_schedule_required,
        "passed": not dpll_schedule_required,
        "nodes": {},
    }
    capture_error = ""
    analysis_error = ""
    startup_gate_error = ""
    # Raw SD capture and its offline waveform analysis are diagnostic evidence.
    # They must never become part of the Core1/PIO realtime gate: a capture
    # latch timeout is recorded below, but it cannot stop a healthy short-frame
    # ring from being handed to the next stage.
    diagnostic_passed = True
    capture_attempted = False
    error = ""
    try:
        for board in ordered:
            actions.append({
                "node": board.address,
                "action": "CALIBRATION_BIAS_STOP",
                "response": board_command(
                    board, "CALibration:BIAS:STOP", args),
            })
            actions.append({
                "node": board.address,
                "action": "CALIBRATION_LOOPBACK_STOP",
                "response": board_command(
                    board, "CALibration:LOOPback:STOP", args),
            })
            actions.append({"node": board.address, "action": "STOP",
                            "response": board_command(
                                board, "SYSTem:TDMA:RING:STOP", args)})
        stop_ack = {
            board.address: wait_runtime_stopped(board, args, node_index)
            for node_index, board in enumerate(ordered)
        }
        actions.extend({
            "node": address,
            "action": "STOP_ACK",
            "config_seq": snapshot["ring_config_seq"],
            "applied_config_seq": snapshot["ring_applied_config_seq"],
        } for address, snapshot in stop_ack.items())
        time.sleep(args.settle)
        capture_load_masks = {
            board.address: read_tdma_load_mask(
                board, args, args.capture_latch_retries)
            for board in ordered
        }
        actions.extend({
            "node": address,
            "action": "CAPTURE_LOAD_MASK_FREEZE",
            "enabled_mask": enabled_mask,
        } for address, enabled_mask in capture_load_masks.items())
        expected_mode = 2 if args.stage == "process-image" else 1
        for node_index, board in enumerate(ordered):
            evidence_enabled = 1 if args.clock_evidence == "enabled" else 0
            evidence_action = checked_stopped_ring_action(
                board, "CLOCK_EVIDENCE",
                f"SYSTem:TDMA:FLIGHT:CLOCK:EVIDence {evidence_enabled}",
                args, node_index)
            evidence_readback = scalar_readback_with_retry(
                board, "SYSTem:TDMA:FLIGHT:CLOCK:EVIDence?", args,
                evidence_enabled, "clock evidence")
            evidence_action["enabled"] = evidence_readback["value"]
            evidence_action["readback"] = evidence_readback
            actions.append(evidence_action)
            response = board_command(
                board,
                f"SYSTem:TDMA:FLIGHT:MODE {1 if args.stage == 'process-image' else 0}",
                args)
            mode_raw = board_command(
                board, "SYSTem:TDMA:FLIGHT:MODE?", args).strip().strip('"')
            try:
                mode = int(mode_raw, 0)
            except ValueError as exc:
                raise RuntimeError(
                    f"{board.address}: invalid flight mode {mode_raw!r}") from exc
            actions.append({
                "node": board.address,
                "action": "FLIGHT_MODE",
                "response": response,
                "mode": mode,
            })
            if mode != expected_mode:
                raise RuntimeError(
                    f"{board.address}: flight mode {mode}, expected {expected_mode}")
        if args.stage == "process-image":
            for board in ordered:
                response = board_command(
                    board, "SYSTem:TDMA:FLIGHT:FIFO:RESet", args)
                readback = flight_snapshot(board, args)
                reset_errors = validate_fifo_reset(readback)
                fifo_reset[board.address] = {
                    "passed": not reset_errors,
                    "errors": reset_errors,
                    "readback": readback,
                }
                actions.append({
                    "node": board.address,
                    "action": "FIFO_RESET",
                    "response": response,
                    "errors": reset_errors,
                })
            if not all(item["passed"] for item in fifo_reset.values()):
                raise RuntimeError("FIFO STOPPED-session reset failed")
        for node_index, board in enumerate(ordered):
            actions.append(checked_ring_action(
                board, "PROFILE_STAGE",
                f"SYSTem:TDMA:OPMode:STAGe {level}", args))
            actions.append(checked_stopped_ring_action(
                board, "PROFILE_APPLY", "SYSTem:TDMA:OPMode:APPLy", args,
                node_index))
            active = parse_active_profile(
                board_command(board, "SYSTem:TDMA:OPMode?", args),
                f"{board.address}:profile")
            if active["level"] != level or active["profile_crc32"] != config["profile_crc32"]:
                raise RuntimeError(f"{board.address}: profile mismatch {active}")
            actions.append(checked_stopped_ring_action(
                board, "TOPOLOGY",
                f"SYSTem:TDMA:RING:TOPology {len(ordered)},{node_index},0",
                args, node_index))
        stage_results = [stage_board(board, config, args) for board in ordered]
        if not all(result["passed"] for result in stage_results):
            raise RuntimeError("matrix write/readback failed")
        progress.emit(
            "matrix_staged", completed=len(stage_results),
            total=len(ordered), offset_row_id=config["offset_row_id"])
        # Matrix staging is also an asynchronous owner mutation.  It may
        # advance ring_config_seq after the profile/topology acknowledgements,
        # so require Core1 to apply that generation before any FIFO seed or
        # ARM operation is attempted.
        matrix_apply_ack = {}
        for node_index, board in enumerate(ordered):
            matrix_apply_ack[board.address] = wait_runtime_stopped(
                board, args, node_index)
            actions.append({
                "node": board.address,
                "action": "MATRIX_APPLIED_ACK",
                "config_seq": matrix_apply_ack[board.address][
                    "ring_config_seq"],
                "applied_config_seq": matrix_apply_ack[board.address][
                    "ring_applied_config_seq"],
            })
        diagnostic_mode = 1 if args.diagnostic_continue else 0
        for node_index, board in enumerate(ordered):
            actions.append(checked_stopped_ring_action(
                board, "DIAGNOSTIC_MODE",
                f"SYSTem:TDMA:RING:DIAGnostic {diagnostic_mode}",
                args, node_index))
            diagnostic_readback = board_command(
                board, "SYSTem:TDMA:RING:DIAGnostic?", args).strip().strip('"')
            if diagnostic_readback != str(diagnostic_mode):
                raise RuntimeError(
                    f"{board.address}: diagnostic mode mismatch "
                    f"{diagnostic_readback!r}, expected {diagnostic_mode}")
            actions[-1]["readback"] = int(diagnostic_readback, 0)
        if args.dpll_provisional:
            for board in ordered:
                actions.append(
                    activate_dpll_provisional(board, args, config))
        if dpll_schedule_required:
            dpll_schedule_before = {
                board.address: read_tdma_schedule(board, args)
                for board in ordered
            }
        for node_index, board in enumerate(ordered):
            if args.stage == "process-image":
                seed = 0x40 + node_index * 0x10
                seed_before = flight_snapshot(board, args)
                actions.append({
                    "node": board.address,
                    "action": "FIFO_TX",
                    "response": board_command(
                        board,
                        f"SYSTem:TDMA:FLIGHT:TX 32,{seed},{config['calibration_generation']},{node_index + 1},1",
                        args),
                })
                seed_after = flight_snapshot(board, args)
                seed_errors, seed_deltas = validate_tx_seed(
                    seed_before, seed_after)
                fifo_seed[board.address] = {
                    "passed": not seed_errors,
                    "errors": seed_errors,
                    "deltas": seed_deltas,
                    "before": seed_before,
                    "after": seed_after,
                }
            else:
                fifo_seed[board.address] = {
                    "passed": True, "errors": [], "deltas": {}}
        for board in start_order:
            record_arm_with_evidence(
                board, args, board_ids.index(board.address), actions)
            progress.emit(
                "board_armed", board_id=board.address,
                completed=len([action for action in actions
                               if action.get("action") == "ARM"]),
                total=len(start_order))
        for board in start_order:
            applied = wait_runtime_config_applied(
                board, args, board_ids.index(board.address))
            actions.append({
                "node": board.address,
                "action": "ARM_CONFIG_APPLIED_ACK",
                "config_seq": applied["ring_config_seq"],
                "applied_config_seq": applied["ring_applied_config_seq"],
            })
        for board in start_order:
            wait_runtime_started(board, args, board_ids.index(board.address))
        if args.clock_train:
            for board in start_order:
                actions.append({
                    "node": board.address,
                    "action": "CLOCK_TRAIN",
                    "response": train(board, args),
                })
        startup_before = sample_all(ordered, args)
        for board in start_order:
            actions.append({"node": board.address, "action": "START",
                            "response": board_command(
                                board, "SYSTem:TDMA:RING:START", args)})
        progress.emit("ring_started", board_count=len(start_order))
        before, startup_barrier = wait_startup_barrier(
            ordered, args, startup_before=startup_before,
            require_process_image=args.stage == "process-image",
            continue_on_failure=args.diagnostic_continue,
            progress=progress)
        if not startup_barrier.get("passed"):
            startup_gate_error = "explicit startup barrier timed out"
        soak_timeline = collect_soak_timeline(
            ordered, args, initial=before, window_s=args.window_s,
            sample_interval_s=args.sample_interval_s, progress=progress)
        final_nodes = soak_timeline[-1]["nodes"]
        missing_final = set(board_ids) - set(final_nodes)
        if missing_final:
            raise RuntimeError(
                "final periodic samples missing: " +
                ", ".join(sorted(missing_final)))
        after = final_nodes
        soak_validation = validate_soak_timeline(
            soak_timeline, board_ids, config,
            require_process_image=args.stage == "process-image")
        if dpll_schedule_required:
            for board in ordered:
                after_schedule = read_tdma_schedule(board, args)
                dpll_schedule_gate["nodes"][board.address] = (
                    validate_dpll_schedule(
                        dpll_schedule_before[board.address], after_schedule))
            dpll_schedule_gate["passed"] = all(
                item["passed"]
                for item in dpll_schedule_gate["nodes"].values())
        for node_index, board in enumerate(ordered):
            node_errors, deltas = validate_node(
                node_index, len(ordered), before[board.address]["runtime"],
                after[board.address]["runtime"],
                before[board.address]["flight"],
                after[board.address]["flight"],
                fifo_seed[board.address]["errors"],
                fifo_seed[board.address]["deltas"],
                require_process_image=args.stage == "process-image",
                physical_after=after[board.address]["physical"],
                expected_physical=expected_flight_phase(config, node_index))
            nodes[board.address] = {
                "node_index": node_index,
                "passed": not node_errors,
                "errors": node_errors,
                "deltas": deltas,
                "runtime_before": before[board.address]["runtime"],
                "runtime_after": after[board.address]["runtime"],
                "flight_before": before[board.address]["flight"],
                "flight_after": after[board.address]["flight"],
                "physical_before": before[board.address]["physical"],
                "physical_after": after[board.address]["physical"],
                "crc_diagnostic_before":
                    before[board.address]["crc_diagnostic"],
                "crc_diagnostic_after":
                    after[board.address]["crc_diagnostic"],
            }
        if getattr(args, "capture_waveforms", False):
            capture_attempted = True
            try:
                ring_capture = capture_ring_waveforms(
                    ordered, args,
                    calibration_generation=config["calibration_generation"],
                    capture_epoch=int(time.time()) & 0xFFFFFFFF,
                    out_dir=out_dir,
                    original_load_masks=capture_load_masks)
            except Exception as exc:  # noqa: BLE001 - retain gate evidence
                capture_error = f"{type(exc).__name__}: {exc}"
                diagnostic_passed = False
            if ring_capture.get("capture_completed"):
                try:
                    ring_analysis = analyze_ring_waveforms(
                        ring_capture, raw_config, args, out_dir)
                except Exception as exc:  # noqa: BLE001 - capture stays valid
                    analysis_error = f"{type(exc).__name__}: {exc}"
                    diagnostic_passed = False
                else:
                    diagnostic_passed = bool(ring_analysis.get("passed"))
            else:
                diagnostic_passed = False
        else:
            ring_capture = {
                "capture_skipped": True,
                "reason": "short_frame_gate_keeps_diagnostics_off_realtime_path",
            }
            ring_analysis = {"passed": True, "skipped": True}
        if startup_gate_error:
            error = startup_gate_error
    except Exception as exc:  # noqa: BLE001 - preserve partial HIL evidence
        error = merge_error(error, f"{type(exc).__name__}: {exc}")
        progress.emit("runtime_error", error=error)
    finally:
        if getattr(args, "capture_waveforms", False) and not capture_attempted:
            try:
                ring_capture = capture_ring_waveforms(
                    ordered, args,
                    calibration_generation=config["calibration_generation"],
                    capture_epoch=int(time.time()) & 0xFFFFFFFF,
                    out_dir=out_dir,
                    original_load_masks=capture_load_masks)
            except Exception as exc:  # noqa: BLE001 - STOP still mandatory
                capture_error = f"{type(exc).__name__}: {exc}"
                diagnostic_passed = False
            if ring_capture.get("capture_completed"):
                try:
                    ring_analysis = analyze_ring_waveforms(
                        ring_capture, raw_config, args, out_dir)
                except Exception as exc:  # noqa: BLE001 - capture stays valid
                    analysis_error = f"{type(exc).__name__}: {exc}"
                    diagnostic_passed = False
                else:
                    diagnostic_passed = bool(ring_analysis.get("passed"))
            else:
                diagnostic_passed = False
        realtime_gate_passed = realtime_gate_passes(
            error=error, soak_validation=soak_validation,
            dpll_schedule_gate=dpll_schedule_gate, nodes=nodes,
            ordered=ordered)
        # Keep the historical name as an alias for consumers which use it to
        # decide whether the running ring may be handed to DPLL.
        closed_loop_passed = realtime_gate_passed
        leave_running = bool(
            args.leave_running and
            (closed_loop_passed or
             (args.diagnostic_continue and bool(soak_timeline))))
        if leave_running:
            for node_index, board in enumerate(ordered):
                try:
                    snapshot = runtime_snapshot(board, args, node_index)
                    snapshot_errors = running_handoff_errors(
                        snapshot, node_index, len(ordered))
                    running_handoff[board.address] = {
                        "passed": not snapshot_errors,
                        "errors": snapshot_errors,
                        "runtime": snapshot,
                    }
                except Exception as exc:  # noqa: BLE001
                    running_handoff[board.address] = {
                        "passed": False,
                        "errors": [f"{type(exc).__name__}: {exc}"],
                    }
            strict_handoff_passed = all(
                item["passed"] for item in running_handoff.values())
            leave_running = running_handoff_allows_leave_running(
                running_handoff,
                diagnostic_continue=args.diagnostic_continue)
            if not strict_handoff_passed and not args.diagnostic_continue:
                error = merge_error(
                    error, "leave-running final runtime readback failed")
        if leave_running:
            for board in ordered:
                stopped[board.address] = {
                    "passed": 1,
                    "skipped": 1,
                    "reason": (
                        "matrix_backed_ring_handed_to_dpll_observer"
                        if closed_loop_passed else
                        "diagnostic_continue_after_failed_gate"),
                }
                actions.append({
                    "node": board.address,
                    "action": "LEAVE_RUNNING",
                })
        else:
            for board in ordered:
                try:
                    actions.append({
                        "node": board.address,
                        "action": "STOP_FINAL",
                        "response": board_command(
                            board, "SYSTem:TDMA:RING:STOP", args),
                    })
                    stopped[board.address] = wait_runtime_stopped(
                        board, args, board_ids.index(board.address))
                except Exception as exc:  # noqa: BLE001
                    stopped[board.address] = {
                        "passed": 0,
                        "error": f"{type(exc).__name__}: {exc}",
                    }
    realtime_gate_passed = realtime_gate_passes(
        error=error, soak_validation=soak_validation,
        dpll_schedule_gate=dpll_schedule_gate, nodes=nodes,
        ordered=ordered)
    passed = (
        realtime_gate_passed and
        all(bool(item.get("passed")) for item in stopped.values())
    )
    result = {
        **plan,
        "passed": passed,
        "realtime_gate_passed": realtime_gate_passed,
        "closed_loop_passed": realtime_gate_passed,
        "diagnostic_passed": diagnostic_passed,
        "diagnostic_continue": args.diagnostic_continue,
        "error": error,
        "boards": {board.address: asdict(board) for board in ordered},
        "stage_results": stage_results,
        "fifo_reset": fifo_reset,
        "fifo_seed": fifo_seed,
        "nodes": nodes,
        "soak_timeline": soak_timeline,
        "soak_validation": soak_validation,
        "dpll_schedule_gate": dpll_schedule_gate,
        "startup_barrier": startup_barrier,
        "ring_capture": ring_capture,
        "ring_capture_error": capture_error,
        "ring_analysis": ring_analysis,
        "ring_analysis_error": analysis_error,
        "capture_load_masks": capture_load_masks,
        "running_handoff": running_handoff,
        "left_running": leave_running,
        "stopped": stopped,
        "actions": actions,
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    progress.emit(
        "complete", passed=passed, left_running=result["left_running"],
        error=error, summary=str(out_dir / "summary.json"))
    close_persistent_connections()
    print(json.dumps({"passed": passed, "error": error,
                      "out_dir": str(out_dir)}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
