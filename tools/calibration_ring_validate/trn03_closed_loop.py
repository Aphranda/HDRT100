#!/usr/bin/env python3
"""Run TRN-03B raw-flight or process-image short-frame closed-loop gates."""

from __future__ import annotations

import argparse
import csv
import json
import math
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
from tdma_field_parse import PHYS_FIELDS, RUNTIME_FIELDS  # noqa: E402
from tdma_service_timing import (  # noqa: E402
    parse_rx_timing,
    parse_service_timing,
)
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
from tdma_board_record import export_frozen, record_status  # noqa: E402


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
        payload_text = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
        # Windows may briefly hold the destination while an IDE or scanner
        # reads it. Use a unique pending file and preserve progress without
        # aborting the realtime gate when the atomic replace is unavailable.
        pending = self.path.with_name(
            f"{self.path.name}.{self.sequence:06d}.tmp")
        pending.write_text(payload_text, encoding="utf-8")
        try:
            pending.replace(self.path)
        except PermissionError as exc:
            fallback = self.path.with_name(
                f"{self.path.stem}.fallback-{self.sequence:06d}.json")
            try:
                fallback.write_text(payload_text, encoding="utf-8")
            except OSError:
                pass
            payload["progress_publish_fallback"] = {
                "path": str(fallback),
                "error": f"{type(exc).__name__}: {exc}",
            }
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
                            load_mask: int,
                            diagnostic_only: bool = False,
                            allow_capture_disturbance: bool = False,
                            spillover_as_feedback: bool = False) -> dict[str, Any]:
    before_phase = before["phases"][phase_id]
    after_phase = after["phases"][phase_id]

    def delta(field: str) -> int:
        return ((int(after_phase[field]) - int(before_phase[field])) &
                0xFFFFFFFF)

    deltas = {field: delta(field) for field in (
        "run_count", "skip_count", "start_miss_count", "overrun_count",
        "deadline_miss_count")}
    phase_errors = []
    spillover_errors = []
    if deltas["run_count"] == 0:
        phase_errors.append(f"{name}_phase_not_serviced")
    # A single overrun inside a multi-thousand-run window is a tail sample (the
    # REFMEM body publishes bounded objects; measured peaks sit at 68-103% of the
    # 29,000-cycle WCET and one sample in ~7,200 crosses it by ~3%).  Treat one
    # excursion as feedback while the DPLL diagnostic load is active, but keep a
    # sustained overrun (>= 2 in the same window) as a hard error.
    single_excursion = spillover_as_feedback and deltas["overrun_count"] == 1
    if deltas["overrun_count"] != 0:
        target = spillover_errors if single_excursion else phase_errors
        target.append(f"{name}_overrun_count_grew")
    if deltas["deadline_miss_count"] != 0:
        # A deadline miss is caused either by this phase's own runtime or by an
        # earlier phase overrunning the cycle.  When the diagnostic DPLL load is
        # the one overrunning (EXE-SAFE-01: DPLL-local WCET/deadline overruns
        # are debug feedback), the spillover is reported, not gated.
        target = spillover_errors if spillover_as_feedback else phase_errors
        target.append(f"{name}_deadline_miss_count_grew")
    if (int(after["quarantined_mask"]) & load_mask) != 0:
        phase_errors.append(f"{name}_load_quarantined")
    if (int(after_phase["max_runtime_cycles"]) >
            int(after_phase["wcet_cycles"])):
        # max_runtime_cycles is a lifetime maximum, so it only proves a current
        # excursion when the overrun counter grew in this window.  With no new
        # overrun the reading is a stale peak from before the last reset and must
        # not fail a clean window.
        if deltas["overrun_count"] != 0:
            target = spillover_errors if single_excursion else phase_errors
            target.append(f"{name}_max_runtime_exceeded_wcet")
    feedback_errors = phase_errors + spillover_errors if diagnostic_only else spillover_errors
    errors = [] if diagnostic_only else phase_errors
    if diagnostic_only:
        feedback_errors = phase_errors
    return {
        "passed": not errors,
        "errors": errors,
        "feedback_errors": feedback_errors,
        "diagnostic_only": diagnostic_only,
        "spillover_errors": spillover_errors,
        "deltas": deltas,
        "before": before_phase,
        "after": after_phase,
        "quarantined_mask": int(after["quarantined_mask"]),
    }


def validate_dpll_schedule(before: dict[str, Any],
                           after: dict[str, Any],
                           allow_capture_disturbance: bool = False,
                           dpll_load_active: bool = False
                           ) -> dict[str, Any]:
    phases = {
        "vdc": validate_realtime_phase(
            before, after, name="vdc", phase_id=APP_REALTIME_PHASE_VDC,
            load_mask=APP_REALTIME_LOAD_VDC_MASK,
            allow_capture_disturbance=allow_capture_disturbance,
            spillover_as_feedback=dpll_load_active),
        "dpll": validate_realtime_phase(
            before, after, name="dpll", phase_id=APP_REALTIME_PHASE_DPLL,
            load_mask=APP_REALTIME_LOAD_DPLL_MASK,
            diagnostic_only=True,
            allow_capture_disturbance=allow_capture_disturbance),
        "refmem": validate_realtime_phase(
            before, after, name="refmem", phase_id=APP_REALTIME_PHASE_REFMEM,
            load_mask=APP_REALTIME_LOAD_REFMEM_MASK,
            allow_capture_disturbance=allow_capture_disturbance,
            spillover_as_feedback=dpll_load_active),
    }
    errors = [
        error for phase in phases.values() for error in phase["errors"]
    ]
    dpll_feedback = phases["dpll"]["feedback_errors"] + [
        error for phase in ("vdc", "refmem")
        for error in phases[phase]["spillover_errors"]]
    return {
        "passed": not errors,
        "errors": errors,
        "dpll_feedback": dpll_feedback,
        "dpll_load_active": dpll_load_active,
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
        "--soak-host-s", type=float, default=0.0,
        help=("sample the short ring page at --soak-interval-s for this many "
              "seconds instead of exporting board records; the window tail "
              "stays observable and no SD storage job is involved"))
    parser.add_argument(
        "--soak-settle-s", type=float, default=2.0,
        help="ignore this many leading seconds before baselining the soak")
    parser.add_argument(
        "--soak-interval-s", type=float, default=1.0,
        help="host soak sampling interval (default 1 s)")
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
        "--wait-ring-running", action="store_true",
        help="poll RING:STATus? until up/down run before sampling")
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
    parser.add_argument("--refmem-cmd-hex", default=None,
                        help="optional 56-byte REFMEM_CMD hex submitted after ring START")
    parser.add_argument("--no-record", action="store_true",
                        help="start the ring and exit without recorder/export")
    parser.add_argument("--no-t2", action="store_true",
                        help="start the ring without seeding T2 flight images")
    parser.add_argument(
        "--cancel-unsaved-record", action="store_true",
        help=("when a frozen record cannot be saved (no storage job left to "
              "persist it), discard it and re-arm instead of failing the round; "
              "the discard is recorded as an action"))
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
        "--allow-capture-disturbance", action="store_true",
        help=("diagnostic capture: retain the capture even when the realtime "
              "schedule deadline-miss counter grows during the latch"))
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
        "--hold-s", type=float, default=0.0,
        help=("keep the process (and the process-image ring) alive "
              "for this many seconds after START; used by relay tests"))
    parser.add_argument(
        "--leave-running", action="store_true",
        help=("leave the matrix-backed ring running only after every closed-"
              "loop gate and a final runtime readback pass; diagnostic mode "
              "records handoff failures without stopping the ring"))
    parser.add_argument(
        "--leave-on-failure", action="store_true",
        help=("diagnostic: leave the ring running even when the closed-loop "
              "gate fails (REFTMEM broadcast HIL on a lab bench)"))
    parser.add_argument(
        "--origin-trial", action="store_true",
        help=("drive the TDMA origin trial entry inside the leave-running "
              "window (TDMA-FLIGHT-002B1): burst 0, trial on the reference "
              "node, release, then read-only observation of the adapter FSM"))
    parser.add_argument("--origin-reference",
                        help="origin reference node serial (default: first board)")
    parser.add_argument("--origin-trial-id", type=int, default=9101)
    parser.add_argument("--origin-rearm-ticks", type=int, default=4096)
    parser.add_argument("--origin-polls", type=int, default=256)
    parser.add_argument(
        "--origin-duration-ticks", type=int, default=15000000000,
        help="trial window in ticks, not ns (250 MHz => 60 s at 1.5e10); the "
             "window must outlast the preparation stage machine")
    parser.add_argument("--origin-observe-seconds", type=float, default=60.0)
    parser.add_argument("--origin-sample-interval-s", type=float, default=0.2)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--origin-cadence", action="store_true",
                        help=("send the optional on-cadence gate argument "
                              "to SYSTem:TDMA:RING:DIAGnostic"))
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


ORIGIN_COMM_STATES = {
    0: "STOPPED", 1: "ARMED", 2: "RUNNING", 3: "CYCLE_BOUNDARY",
    4: "FAULT", 5: "AUTONOMOUS", 6: "RESIDENT_PREPARING",
}
ORIGIN_AUTONOMOUS_STATE = 5

# Tagged SCPI pages put their tag in values[0], so field N lives at
# values[N + 1]. Name the fields once and parse through these tuples: raw
# indices already caused a release to carry the version constant and two
# mis-labelled counters.
ORIGIN_RUNTIME_FIELDS = (
    "active", "comm_fsm_state", "last_error", "observed_return_count",
    "unobserved_cycle_count", "rejected_observation_count",
    "published_generation", "published_owner_generation",
    "matched_owner_generation", "returned_sequence", "returned_identity",
    "returned_local_generation", "boundary_sequence", "boundary_identity",
    "boundary_local_generation", "boundary_output_remaining",
    "timestamp_flags",
)
ORIGIN_TRIAL_FIELDS = ("version", "epoch", "enabled", "trial_id",
                       "config_seq")
ORIGIN_BOUNDARY_FIELDS = ("boundary_sequence", "boundary_identity",
                         "boundary_local_generation",
                         "boundary_output_remaining")


def origin_page(fields: tuple[str, ...], values: list[str]) -> dict[str, str]:
    """Map a tagged SCPI page to named fields (the tag is values[0])."""
    return dict(zip(fields, (value.strip().strip(chr(34)) for value in values[1:])))


def origin_trial_probe(ordered: list[Board], args: argparse.Namespace,
                       out_dir: Path) -> dict[str, Any]:
    """Drive the origin trial entry on the reference node and observe the FSM.

    The entry contract (TDMA-FLIGHT-002B1) admits the trial only while the ring
    runs in process-image forwarding with the diagnostic-continue flag set and a
    zero burst limit; the preparation stage machine then advances on ring cycles
    up to RESIDENT_INSTALLED, which is what moves the adapter to AUTONOMOUS.
    ``--origin-duration-ticks`` is a tick count (250 MHz), and the window must
    outlast that machine, because an expired trial revokes authorization
    mid-preparation. Every read is observational, and the trial is always
    revoked so no foundation state is left behind.
    """
    reference = getattr(args, "origin_reference", None) or ordered[0].address
    board = next((item for item in ordered if item.address == reference), None)
    if board is None:
        return {"passed": False,
                "error": f"origin reference board not on the bench: {reference}"}
    steps: list[dict[str, Any]] = []

    def send(command: str) -> str:
        try:
            response = board_command(board, command, args)
        except Exception as exc:  # noqa: BLE001 - a missing reply is evidence
            response = f"<{type(exc).__name__}>"
        steps.append({"command": command, "response": response})
        return response

    send("SYSTem:TDMA:RING:BURSt 0")
    burst = send("SYSTem:TDMA:RING:BURSt?")

    def drain_errors() -> None:
        """Drain the SCPI error queue so a queued reply cannot merge into the
        next read (see the merged-reply hazard in tdma_start_ring.py)."""
        for _ in range(8):
            raw = send("SYSTem:ERR?")
            if raw.startswith("0,") or '"No error"' in raw or raw.startswith("<"):
                break

    # The licence freezes the staged ring config, so wait until that config is
    # applied: licensing against a pending config makes the trial stale.
    applied_deadline = time.monotonic() + 10.0
    config_was_applied = False
    while time.monotonic() < applied_deadline:
        opmode = send("SYSTem:TDMA:OPMode?")
        values = [value.strip() for value in opmode.split(",")]
        if len(values) > 13 and values[12] == values[13]:
            config_was_applied = True
            break
        time.sleep(0.2)
    drain_errors()
    trial = ("CALibration:ORIGin:TRIAL:READY {}, {}, {}, {}".replace(", ", ",")
             .format(getattr(args, "origin_trial_id", 9101),
                     getattr(args, "origin_rearm_ticks", 4096),
                     getattr(args, "origin_polls", 256),
                     getattr(args, "origin_duration_ticks", 15000000000)))
    send(trial)
    page = send("READ:CALibration:ORIGin?")
    fields = [value.strip().strip('"') for value in page.split(",")]
    # scpi_calibration_commands.c:305-310 -> tag, version, epoch, enabled,
    # trial_id, config_seq, ... (epoch and config_seq are NOT fields 1 and 4).
    epoch = fields[2] if len(fields) > 5 else "0"
    config_seq = fields[5] if len(fields) > 5 else "0"
    # No live readback exposes the preparation stage: every origin diagnostic
    # page (handoff / release / build probe) requires a stopped ring
    # (tdma_runtime_origin.inc:411-413, 422-424, 437-439). Waiting on those
    # pages while the ring runs can never succeed, so the release is issued
    # with a bounded retry and the trial page is recorded after each attempt.
    release_ready = False
    release_response = "<not-sent>"
    release_attempts: list[dict[str, Any]] = []
    for attempt in range(5):
        drain_errors()
        release_response = send(
            f"CALibration:ORIGin:RELease {epoch},{config_seq}")
        after = send("READ:CALibration:ORIGin?")
        release_attempts.append({"attempt": attempt,
                                 "release_response": release_response,
                                 "trial_page": after})
        if not release_response.startswith("<"):
            release_ready = True
            break
        time.sleep(0.5)

    states: dict[str, int] = {}
    runtime_states: dict[str, int] = {}
    # Emission timing: first/last sampled counters with their wall-clock stamps
    # give the self-excited rate and the boundary cadence.
    timing_samples: list[tuple[float, int, int, int, int]] = []
    # Raw READ:CALibration:ORIGin:LIVE? replies; no stopped-ring precondition, so
    # this is the only live witness of the hardware return interval
    # (rtt_remaining / arm_after / sequence_end) during AUTONOMOUS.
    live_raw: list[tuple[float, str]] = []
    max_observed_returns = 0
    max_rejected = 0
    max_published_generation = 0
    boundary_seen = False
    last_runtime = ""
    deadline = time.monotonic() + float(getattr(args, "origin_observe_seconds", 60.0))
    interval = float(getattr(args, "origin_sample_interval_s", 0.2))
    while time.monotonic() < deadline:
        runtime = send("READ:CALibration:ORIGin:RUNTIme?")
        live = send("READ:CALibration:ORIGin:LIVE?")
        if live:
            live_raw.append((time.monotonic(), live.strip()))
        if runtime.startswith('"ORIGINRUNTIME"'):
            values = runtime.split(",")
            code = values[2].strip() if len(values) > 2 else "?"
            name = ORIGIN_COMM_STATES.get(int(code), code) if code.isdigit() else code
            states[name] = states.get(name, 0) + 1
            key = f"active={values[1].strip()},fsm={name}"
            runtime_states[key] = runtime_states.get(key, 0) + 1
            last_runtime = runtime
            # The SCPI page tag occupies values[0], so field N lives at
            # values[N + 1]; reading raw indices is what made an earlier release
            # carry the version constant instead of the epoch.
            page = origin_page(ORIGIN_RUNTIME_FIELDS, values)
            observed = page.get("observed_return_count", "0")
            if observed.isdigit():
                max_observed_returns = max(max_observed_returns, int(observed))
            rejected = page.get("rejected_observation_count", "0")
            if rejected.isdigit():
                max_rejected = max(max_rejected, int(rejected))
            published = page.get("published_generation", "0")
            if published.isdigit():
                max_published_generation = max(max_published_generation,
                                               int(published))
            if any(page.get(name, "0") not in ("0", "")
                   for name in ORIGIN_BOUNDARY_FIELDS):
                boundary_seen = True
            observed_now = page.get("observed_return_count", "0")
            boundary_now = page.get("boundary_sequence", "0")
            published_now = page.get("published_generation", "0")
            returned_now = page.get("returned_sequence", "0")
            if (observed_now.isdigit() and boundary_now.isdigit() and
                    published_now.isdigit() and returned_now.isdigit()):
                timing_samples.append((time.monotonic(), int(observed_now),
                                       int(boundary_now), int(published_now),
                                       int(returned_now)))
        time.sleep(interval)
    handoff = send("READ:CALibration:ORIGin:HANDoff?")
    record = send("READ:CALibration:ORIGin:RECord? 0")
    send("CALibration:ORIGin:REVOkE")
    return {
        "passed": states.get("AUTONOMOUS", 0) > 0,
        "reference": reference,
        "burst_readback": burst,
        "trial_epoch": epoch,
        "trial_config_seq": config_seq,
        "config_was_applied": config_was_applied,
        "release_ready": release_ready,
        "release_response": release_response,
        "release_attempts": release_attempts,
        "observed_states": states,
        "runtime_states": runtime_states,
        "autonomous_reached": states.get("AUTONOMOUS", 0) > 0,
        "max_observed_return_count": max_observed_returns,
        "max_rejected_observation_count": max_rejected,
        "max_published_generation": max_published_generation,
        "boundary_seen": boundary_seen,
        "origin_live_count": len(live_raw),
        "origin_live_first": [raw for _, raw in live_raw[:3]],
        "origin_live_last": [raw for _, raw in live_raw[-3:]],
        "emission_first_sample": timing_samples[0] if timing_samples else None,
        "emission_last_sample": timing_samples[-1] if timing_samples else None,
        "emission_observed_delta": (
            timing_samples[-1][1] - timing_samples[0][1]
            if len(timing_samples) > 1 else 0),
        "emission_boundary_delta": (
            timing_samples[-1][2] - timing_samples[0][2]
            if len(timing_samples) > 1 else 0),
        # Coarse discriminator for the emission interval: the 1.5 ms and
        # 4 ms hypotheses differ by 2.67x in rate, so a counted delta over
        # the observation window is enough to tell them apart without any
        # sub-millisecond timestamping.
        "emission_interval_ms": (
            round(1000.0 * timing_samples[-1][0] - 1000.0 * timing_samples[0][0], 3)
            / max(1, timing_samples[-1][1] - timing_samples[0][1])
            if len(timing_samples) > 1 else 0.0),
        "emission_interval_class": (
            "no-emission" if len(timing_samples) < 2 or
            timing_samples[-1][1] <= timing_samples[0][1] else
            f"{round(1000.0 * (timing_samples[-1][0] - timing_samples[0][0]) / (timing_samples[-1][1] - timing_samples[0][1]), 2)} ms")
        ,
        "emission_elapsed_s": (
            round(timing_samples[-1][0] - timing_samples[0][0], 3)
            if len(timing_samples) > 1 else 0.0),
        "emission_published_delta": (
            timing_samples[-1][3] - timing_samples[0][3]
            if len(timing_samples) > 1 else 0),
        "emission_returned_sequence_delta": (
            timing_samples[-1][4] - timing_samples[0][4]
            if len(timing_samples) > 1 else 0),
        "last_runtime": last_runtime,
        "handoff": handoff,
        "record": record,
        "steps": steps,
        "out_dir": str(out_dir),
    }


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


def service_timing_snapshot(board: Board, args: argparse.Namespace,
                            cycle_ticks: int | None = None) -> dict[str, Any]:
    """Attribute the TDMA phase wall clock to its recorded stages.

    The app-level phase counter reports the whole phase body (transport,
    observer, VDC run output, SYNC_IO reference, analyzer and accounting) as
    one number, so it cannot say which service consumed the window.  Every
    record is captured: STEADY is the largest phase that ran no STOP/ARM/config
    transition, PEAK the largest complete phase, RUN/OTHer separate autonomous
    from other phases, and LAST is the final phase.  Every nested stage
    interval is inclusive: subtract recorded children before adding siblings.
    With ``cycle_ticks`` each record also reports how much of the TDMA period
    is waiting (period minus TX minus RX).  Read-only evidence; it adds no gate.
    """
    result: dict[str, Any] = {}
    errors: dict[str, str] = {}
    for key, text, parser in (
            ("last", "SYSTem:TDMA:PROFile?", parse_service_timing),
            ("peak", "SYSTem:TDMA:PROFile:PEAK?", parse_service_timing),
            ("run", "SYSTem:TDMA:PROFile:RUN?", parse_service_timing),
            ("other", "SYSTem:TDMA:PROFile:OTHer?", parse_service_timing),
            ("steady", "SYSTem:TDMA:PROFile:STEady?", parse_service_timing),
            ("rx", "SYSTem:TDMA:PROFile:RX?", parse_rx_timing)):
        try:
            raw = board_command(board, text, args)
            result[f"{key}_raw"] = raw
            result[key] = (parser(raw, cycle_ticks=cycle_ticks)
                           if parser is parse_service_timing else parser(raw))
        except Exception as exc:  # noqa: BLE001 - retain partial evidence
            result[key] = None
            errors[key] = f"{type(exc).__name__}: {exc}"
    result["cycle_ticks"] = cycle_ticks
    result["errors"] = errors
    return result


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


def parse_dpll_debug_admission_status(
        raw: str, label: str) -> dict[str, Any]:
    """Parse the mailbox-backed DPLL debug-admission status exactly."""
    fields = [value.strip().strip('"') for value in raw.split(",")]
    if len(fields) != 9:
        raise RuntimeError(
            f"{label}: debug-admission readback field count {len(fields)}, "
            f"expected 9: {raw!r}")
    enabled_text, mode, state = fields[:3]
    if enabled_text not in ("0", "1"):
        raise RuntimeError(
            f"{label}: invalid debug-admission enabled value {raw!r}")
    if mode != "DEBUG_ADMISSION" or state not in ("IDLE", "PENDING", "ACTIVE"):
        raise RuntimeError(
            f"{label}: invalid debug-admission state {raw!r}")
    try:
        (continued_count, last_gate_code, last_gate_slot,
         last_gate_evidence, requested_generation,
         applied_generation) = [int(value, 0) for value in fields[3:]]
    except ValueError as exc:
        raise RuntimeError(
            f"{label}: invalid debug-admission readback {raw!r}") from exc
    return {
        "raw": raw,
        "enabled": int(enabled_text, 0),
        "mode": mode,
        "state": state,
        "continued_count": continued_count,
        "last_gate_code": last_gate_code,
        "last_gate_slot": last_gate_slot,
        "last_gate_evidence": last_gate_evidence,
        "requested_generation": requested_generation,
        "applied_generation": applied_generation,
    }


def activate_dpll_debug_admission(
        board: Board, args: argparse.Namespace) -> dict[str, Any]:
    """Enable debug continuation and prove Core1 has applied its mailbox."""
    action = checked_action(
        board, "SYSTem:SYNC:VDC:DPLL:OVERRide 1", args)
    fields = [value.strip().strip('"') for value in
              str(action["response"]).split(",")]
    if len(fields) != 3 or fields[0].upper() != "OK" or fields[1] != "1":
        raise RuntimeError(
            f"{board.address}: invalid debug-admission action response "
            f"{action['response']!r}")
    try:
        requested_generation = int(fields[2], 0)
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid debug-admission generation "
            f"{action['response']!r}") from exc
    if requested_generation <= 0:
        raise RuntimeError(
            f"{board.address}: invalid debug-admission generation "
            f"{requested_generation}")

    deadline = time.monotonic() + float(args.arm_wait)
    rejected_readbacks: list[dict[str, Any]] = []
    last: dict[str, Any] | None = None
    while time.monotonic() < deadline:
        raw = board_command(board, "SYSTem:SYNC:VDC:DPLL:OVERRide?", args)
        try:
            status = parse_dpll_debug_admission_status(raw, board.address)
        except RuntimeError as exc:
            rejected_readbacks.append({"response": raw, "error": str(exc)})
        else:
            last = status
            if (status["enabled"] == 1 and status["state"] == "ACTIVE" and
                    status["requested_generation"] == requested_generation and
                    status["applied_generation"] == requested_generation):
                return {
                    "node": board.address,
                    "action": "DPLL_DEBUG_ADMISSION",
                    **action,
                    "requested_generation": requested_generation,
                    "readback_attempt_count": len(rejected_readbacks) + 1,
                    "rejected_readbacks": rejected_readbacks,
                    "readback": status,
                }
            rejected_readbacks.append({
                "response": raw,
                "readback": status,
                "error": "mailbox not active at requested generation",
            })
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: debug-admission mailbox did not become ACTIVE "
        f"at generation {requested_generation}; last={last}; "
        f"rejected_readbacks={rejected_readbacks}")


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
        "service_timing": service_timing_snapshot(board, args),
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
            delta = u32_delta(previous_process[field], current_process[field])
            # The firmware marks its stale-feedback timeout as a V1 diagnostic
            # bring-up gate, so a single freshness miss right after the ring
            # starts stays a warning; two or more still fail the barrier.
            # Measured after the RX windows were aligned with the evidence-mode
            # cadence: per 78-frame interval the worst observed increment is 3
            # (3.8%), typical 0-2.  Three stays a warning; four fails.  This is
            # a bound on the diagnostic bring-up gate, not a blanket relaxation.
            allowed = 3 if field == "receive_missing_count" else 0
            if delta > allowed:
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
    # Clock-evidence mode roughly halves the ring cadence (measured ~200 frames/s
    # without it versus ~130 with it), so the same number of receive
    # opportunities needs more wall time.  Scale the barrier instead of demanding
    # steady state inside a window the link cannot fill yet.
    if getattr(args, "clock_evidence", "disabled") == "enabled":
        timeout_s *= 2.0
    stable_required = int(args.startup_stable_samples)
    poll_interval_s = float(args.startup_poll_interval_s)
    if timeout_s <= 0 or stable_required <= 0 or poll_interval_s <= 0:
        raise ValueError("startup barrier arguments must be positive")

    board_ids = [board.address for board in ordered]
    previous = startup_before
    stable_count = 0
    started = time.monotonic()
    deadline = started + timeout_s
    samples: list[dict[str, Any]] = []
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        time.sleep(min(poll_interval_s, remaining))
        sample_started = time.monotonic()
        if sample_started >= deadline:
            break
        current, transport_errors = sample_all_with_evidence(ordered, args)
        # Serial collection is bounded separately and can finish after the
        # startup deadline. Keep its evidence, but never promote a late healthy
        # observation to a stable interval inside the requested startup window.
        sample_completed = time.monotonic()
        completed_within_deadline = sample_completed <= deadline
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
        interval_passed = not node_errors and completed_within_deadline
        stable_count = stable_count + 1 if interval_passed else 0
        samples.append({
            "sample_index": len(samples),
            "sample_started_elapsed_s": round(sample_started - started, 6),
            "elapsed_s": round(sample_completed - started, 6),
            "completed_within_deadline": completed_within_deadline,
            "deadline_error": (None if completed_within_deadline else
                               "startup_sample_completed_after_deadline"),
            "health_passed": not node_errors,
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
                completed_within_deadline=completed_within_deadline,
                transport_errors=transport_errors)
        if not transport_errors and set(current) == set(board_ids):
            previous = current
        if not completed_within_deadline:
            break
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


def evaluate_board_records(records: dict[str, Any], ordered: list[Board],
                           args: argparse.Namespace, config: dict[str, Any]):
    """Apply the existing health predicates to each actual board-clock sample."""
    ids = [board.address for board in ordered]
    decoded = {address: records[address]["decoded"] for address in ids}
    baseline = {address: decoded[address]["baseline"]["snapshot"] for address in ids}
    by_slot = {address: {row["slot"]: row for row in decoded[address]["samples"]} for address in ids}
    collection_errors = {address: decoded[address]["errors"] for address in ids if decoded[address]["errors"]}
    requested = {decoded[address]["terminal"]["requested"] for address in ids}
    periods = {decoded[address]["interval_us"] for address in ids}
    if len(requested) != 1 or len(periods) != 1:
        raise ValueError("board recording configurations differ")
    period = periods.pop() / 1e6
    previous = baseline
    stable = 0
    barrier_rows = []
    anchor = None
    require_image = args.stage == "process-image"
    # The recording is triggered by RING:START itself, so slot 0 is the
    # pre-enable fill state and can never be healthy.  Use it only as the
    # baseline and start evaluating from the first slot where every board
    # reports the ring running.
    sample_count = requested.pop()
    first_slot = 0
    for slot in range(sample_count):
        if all(slot in by_slot[address] and
               int(by_slot[address][slot]["snapshot"]["runtime"]["ring_enabled"]) == 1 and
               int(by_slot[address][slot]["snapshot"]["runtime"]["ring_up_running"]) == 1 and
               int(by_slot[address][slot]["snapshot"]["runtime"]["ring_down_running"]) == 1
               for address in ids):
            first_slot = slot
            break
    for slot in range(first_slot, sample_count):
        rows = {address: by_slot[address][slot] for address in ids if slot in by_slot[address]}
        elapsed = {address: (row["completed_us"] - decoded[address]["terminal"]["trigger_us"]) / 1e6
                   for address, row in rows.items()}
        errors = {}
        current = {address: row["snapshot"] for address, row in rows.items()}
        for index, address in enumerate(ids):
            if address not in rows:
                errors[address] = ["board_sample_missing"]
                continue
            row = rows[address]
            node_errors = startup_barrier_interval_errors(previous[address], current[address],
                node_index=index, node_count=len(ids), require_process_image=require_image)
            if row["valid_mask"] != 0x3F or row["skipped_before"]:
                node_errors.append("board_sample_invalid_or_missed")
            if node_errors:
                errors[address] = node_errors
        # The record grid is quantised to one interval, so a sample taken just
        # before the deadline legitimately lands a few milliseconds late.
        deadline_s = args.startup_timeout_s + period
        timely = len(elapsed) == len(ids) and all(value <= deadline_s for value in elapsed.values())
        passed = not errors and timely
        stable = stable + 1 if passed else 0
        barrier_rows.append(dict(sample_index=slot, elapsed_s=max(elapsed.values(), default=slot * period),
            board_elapsed_s=elapsed, completed_within_deadline=timely,
            health_passed=not errors, passed=passed, stable_count=stable, errors=errors))
        if len(current) == len(ids):
            previous = current
        if stable >= args.startup_stable_samples:
            anchor = slot
            break
        if slot * period >= args.startup_timeout_s:
            break
    barrier = dict(passed=anchor is not None and not collection_errors,
        timeout_s=args.startup_timeout_s, stable_samples_required=args.startup_stable_samples,
        stable_samples_observed=stable, samples=barrier_rows,
        clock_source="board trigger and snapshot timestamps", collection_errors=collection_errors,
        startup_pipeline_fill_deltas={address: {
            "runtime_errors": counter_deltas(baseline[address]["runtime"], previous[address]["runtime"], STARTUP_RUNTIME_ERROR_COUNTERS),
            "process_errors": counter_deltas(baseline[address]["flight"]["process"], previous[address]["flight"]["process"], STARTUP_PROCESS_ERROR_COUNTERS) if require_image else {},
            "accepted_sequence_delta": u32_delta(baseline[address]["flight"]["process"]["receive_accepted_sequence"], previous[address]["flight"]["process"]["receive_accepted_sequence"]) if require_image else 0,
        } for address in ids})
    # A rejected startup still gets a bounded diagnostic soak with all failures
    # retained. It cannot be promoted by a later recovery.
    if anchor is None:
        common = set.intersection(*(set(by_slot[address]) for address in ids))
        candidates = [slot for slot in common if slot * period >= args.startup_timeout_s]
        if not candidates:
            raise RuntimeError("no complete board record available after failed startup")
        anchor = min(candidates)
    last_slot = anchor + math.ceil(args.window_s / period)
    # Keep the actual observed duration, including release jitter, at least
    # the requested soak window on every board. An extra existing record may
    # be needed; never fabricate an endpoint at the nominal target time.
    common_slots = set.intersection(*(set(by_slot[address]) for address in ids))
    endpoints = [slot for slot in sorted(common_slots) if slot >= last_slot and
        all(by_slot[address][slot]["completed_us"] - by_slot[address][anchor]["completed_us"] >=
            args.window_s * 1e6 for address in ids)]
    duration_complete = bool(endpoints)
    if endpoints:
        last_slot = endpoints[0]
    timeline = []
    for slot in range(anchor, last_slot + 1):
        rows = {address: by_slot[address][slot] for address in ids if slot in by_slot[address]}
        errors = {address: "board_sample_missing" for address in ids if address not in rows}
        for address, row in rows.items():
            if row["valid_mask"] != 0x3F or row["skipped_before"]:
                errors[address] = "board_sample_invalid_or_missed"
        elapsed = {address: (row["completed_us"] - by_slot[address][anchor]["completed_us"]) / 1e6
                   for address, row in rows.items()}
        timeline.append(dict(sample_index=len(timeline), elapsed_s=max(elapsed.values(), default=(slot - anchor) * period),
            board_elapsed_s=elapsed, target_elapsed_s=(slot - anchor) * period,
            nodes={address: row["snapshot"] for address, row in rows.items()}, errors=errors))
    validation = validate_soak_timeline(timeline, ids, config, require_process_image=require_image)
    if not duration_complete:
        validation["passed"] = False
        validation.setdefault("errors", []).append("board_soak_duration_incomplete")
    if collection_errors:
        validation["passed"] = False
        validation.setdefault("errors", []).append("board_record_collection_failed")
    return timeline[0]["nodes"], timeline[-1]["nodes"], barrier, timeline, validation


SOAK_GROWTH_FIELDS = (
    "ring_adapter_rx_bad_count",
    "ring_adapter_rx_transport_bad_count",
    "ring_adapter_rx_schedule_bad_count",
    "ring_adapter_rx_profile_bad_count",
)


def host_soak(ordered: list[Board], args: argparse.Namespace, progress,
              out_dir: Path) -> dict[str, Any]:
    """Sample the short ring page at a fixed cadence instead of board records.

    The board recorder freezes a fixed sample window and needs an SD storage job
    to release it; a host soak costs one short-page round-trip per board per
    interval (about 62 ms on the current USB CDC link), so a 1 Hz cadence leaves
    most of each second free and keeps the window tail observable.
    """
    window_s = float(args.soak_host_s)
    interval_s = max(0.2, float(args.soak_interval_s))
    samples: list[dict[str, Any]] = []
    started = time.monotonic()
    try:
        index = 0
        while time.monotonic() - started <= window_s:
            elapsed = time.monotonic() - started
            row = {board.address: runtime_snapshot(board, args, node)
                   for node, board in enumerate(ordered)}
            samples.append({"index": index, "elapsed_s": round(elapsed, 3),
                            "boards": row})
            index += 1
            progress.emit("host_soak_sample", index=index,
                          elapsed_s=round(elapsed, 3))
            time.sleep(interval_s)
    finally:
        for board in ordered:
            try:
                board_command(board, "SYSTem:TDMA:RING:STOP", args)
            except RuntimeError:
                pass
    settle_s = float(getattr(args, "soak_settle_s", 2.0) or 0.0)
    stable = [sample for sample in samples if sample["elapsed_s"] >= settle_s] or samples
    baseline = stable[0]["boards"]
    final = stable[-1]["boards"]
    verdict: dict[str, Any] = {}
    passed = True
    for board in ordered:
        first, last = baseline[board.address], final[board.address]
        grew = [name for name in SOAK_GROWTH_FIELDS if last[name] > first[name]]
        down = any(sample["boards"][board.address]["ring_up_running"] == 0 or
                   sample["boards"][board.address]["ring_down_running"] == 0
                   for sample in stable[1:])
        healthy = not grew and not down and last["ring_enabled"] == 1
        passed = passed and healthy
        verdict[board.address] = {
            "healthy": healthy,
            "grew": grew,
            "went_down": down,
            "tx_delta": last["ring_adapter_tx_count"] - first["ring_adapter_tx_count"],
            "rx_delta": last["ring_adapter_rx_count"] - first["ring_adapter_rx_count"],
            "loop_time_ns": first.get("ring_loop_time_last_ns"),
            "sample_count": len(stable),
        }
    result = {
        "window_s": window_s,
        "interval_s": interval_s,
        "passed": passed,
        "boards": verdict,
        "samples": samples,
    }
    summary_path = Path(out_dir) / "soak-host.json"
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(result, ensure_ascii=False), encoding="utf-8")
    return result


def feedback_sequence_ok(gap: int, node_index: int) -> bool:
    """Whether an instantaneous ``up_tx - down_rx`` depth is still healthy.

    The origin both generates and forwards frames, so its TX sequence runs
    ahead of its own RX: one frame generated before the first capture, one in
    the ring and one in the accept pipeline.  Measured runs show 1..3 with
    ``ring_adapter_rx_bad_count`` unchanged, so the origin bound is 3; a
    follower has no such offset and must stay exactly 0.
    """
    return gap <= 3 if node_index == 0 else gap == 0


def feedback_sequence_drift_ok(up_delta: int, down_delta: int,
                               node_index: int) -> bool:
    """Whether one interval's TX/RX advance stayed loss-free.

    The instantaneous depth (``feedback_sequence_ok``) is a phase snapshot: the
    origin's TX runs ahead of its own RX by 1..3 frames by design (generated
    frame + ring + accept pipeline) and jitters by one frame.  A lost frame is
    what breaks the *advance*: the two counters stop moving together, so each
    interval must advance by the same count (origin at most one apart).
    """
    return abs(up_delta - down_delta) <= 2 if node_index == 0 else up_delta == down_delta


def save_previous_record(board, args, actions):
    """Finish a record the previous run left FROZEN (state 5).

    The host exports the RAM lease of every run without spending a storage job,
    so the next ARM is refused until that frozen record is SAVE'd.  A SAVE is
    only accepted while the ring is stopped *and* ``config_seq`` still equals
    ``applied_config_seq``; once profile/matrix staging advances the config
    generation the board refuses the SAVE and a FROZEN record has no CANCEL
    path, which wedged every second consecutive run until a BOOT:RESet.
    """
    status = record_status(board_command(
        board, "SYSTem:TDMA:RECord:STATus?", args))
    if status is None or status["state"] in (0, 8, 9):
        return status
    if status["state"] != 5:
        raise RuntimeError(f"{board.address}: recorder is not reusable: {status}")
    response = board_command(board, "SYSTem:TDMA:RECord:SAVE", args)
    actions.append(dict(node=board.address, action="SAVE_PREVIOUS_RECORD",
                        epoch=status["epoch"], response=response))
    if response.strip().strip('"') != "OK":
        raise RuntimeError(f"{board.address}: previous recorder SAVE rejected")
    deadline = time.monotonic() + args.arm_wait
    while time.monotonic() < deadline:
        status = record_status(board_command(
            board, "SYSTem:TDMA:RECord:STATus?", args))
        if status is not None and status["state"] in (8, 9):
            break
        time.sleep(0.05)
    if status is None or status["state"] not in (8, 9):
        raise RuntimeError(
            f"{board.address}: previous recorder SAVE did not complete: {status}")
    return status


def acquire_board_records(ordered, start_order, args, actions, progress, out_dir):
    """Trigger once; make no SCPI data queries until the board window has ended."""
    epoch = int(time.time()) & 0xFFFFFFFF
    period_us = round(args.sample_interval_s * 1e6)
    count = math.ceil((args.startup_timeout_s + args.window_s) * 1e6 / period_us) + 2
    for board in ordered:
        previous = record_status(board_command(
            board, "SYSTem:TDMA:RECord:STATus?", args))
        if previous is None:
            raise RuntimeError(f"{board.address}: recorder status unavailable before ARM")
        if previous["state"] == 5:
            response = board_command(board, "SYSTem:TDMA:RECord:SAVE", args)
            actions.append(dict(node=board.address, action="SAVE_PREVIOUS_RECORD",
                                epoch=previous["epoch"], response=response))
            if response.strip().strip('"') != "OK":
                raise RuntimeError(f"{board.address}: previous recorder SAVE rejected")
            deadline = time.monotonic() + args.arm_wait
            while time.monotonic() < deadline:
                previous = record_status(board_command(
                    board, "SYSTem:TDMA:RECord:STATus?", args))
                if previous is not None and previous["state"] in (8, 9):
                    break
                time.sleep(0.05)
            if previous is None or previous["state"] != 8:
                if not getattr(args, "cancel_unsaved_record", False):
                    raise RuntimeError(
                        f"{board.address}: previous recorder SAVE did not complete: {previous}")
                # The frozen record belongs to the run that produced it and its
                # storage job is gone, so it can never be written now. Discard it
                # explicitly instead of failing every following round.
                actions.append(dict(
                    node=board.address, action="RECORD_CANCEL_UNSAVED",
                    epoch=previous["epoch"] if previous is not None else 0,
                    response=board_command(
                        board, "SYSTem:TDMA:RECord:CANCel", args)))
                deadline = time.monotonic() + args.arm_wait
                while time.monotonic() < deadline:
                    previous = record_status(board_command(
                        board, "SYSTem:TDMA:RECord:STATus?", args))
                    if previous is not None and previous["state"] in (0, 8, 9):
                        break
                    time.sleep(0.05)
                if previous is None or previous["state"] not in (0, 8, 9):
                    raise RuntimeError(
                        f"{board.address}: recorder cancel did not complete: {previous}")
        elif previous["state"] not in (0, 8, 9):
            raise RuntimeError(
                f"{board.address}: recorder is not reusable: {previous}")
        actions.append(dict(node=board.address, action="RECORD_ARM", epoch=epoch,
            response=board_command(board, f"SYSTem:TDMA:RECord:ARM {epoch},{period_us},{count}", args)))
        deadline = time.monotonic() + args.arm_wait
        status = None
        while time.monotonic() < deadline:
            status = record_status(board_command(board, "SYSTem:TDMA:RECord:STATus?", args))
            if status is not None and status["state"] == 3 and status["epoch"] == epoch:
                break
            if status is not None and status["state"] == 9:
                raise RuntimeError(f"recorder ARM rejected: {status}")
            time.sleep(0.05)
        else:
            raise RuntimeError(f"recorder ARM acknowledgement timeout: {status}")
    try:
        cmd_hex = getattr(args, "refmem_cmd_hex", None)
        if cmd_hex and not getattr(args, "_refmem_submitted", False):
            args._refmem_submitted = True
            ref = next((b for b in start_order
                        if b.address == "0010071E65B5CB38"), start_order[0])
            response = board_command(
                ref, f'SYSTem:REFMEM:CMD:SUBmit "{cmd_hex}"', args)
            actions.append(dict(node=ref.address, action="REFMEM_CMD_SUBMIT",
                                response=response))
            progress.emit("refmem_cmd_submitted", board_id=ref.address,
                          response=response)
        for board in start_order:
            # RING:START is idempotent (it only raises data_enabled), and the
            # firmware rejects it when the core0 owner try-lock or the runtime
            # config guard is momentarily busy.  Retry briefly and, if it still
            # fails, fail here with the ring status instead of exporting an empty
            # board record seven seconds later.
            row = dict(node=board.address, action="START")
            for _attempt in range(3):
                row["response"] = board_command(
                    board, "SYSTem:TDMA:RING:START", args)
                if row["response"].strip().strip('"') == "OK":
                    break
                time.sleep(0.05)
            actions.append(row)
            if row["response"].strip().strip('"') != "OK":
                raise RuntimeError(
                    f"{board.address}: RING:START rejected "
                    f"{row['response']!r} "
                    f"ring={board_command(board, 'SYSTem:TDMA:RING:STATus?', args)} "
                    f"err={board_command(board, 'SYSTem:ERR?', args)}")
        # RING:START only requests the enable; the down/up paths come up a beat
        # later.  Wait for the ring to actually report running before opening the
        # sample window, otherwise the first samples see down_not_running and the
        # startup barrier fails on a link that is merely still filling.
        ring_deadline = (time.monotonic() + 3.0
                         if getattr(args, "wait_ring_running", False)
                         else time.monotonic())
        while time.monotonic() < ring_deadline:
            pending = []
            for board in start_order:
                raw = board_command(board, "SYSTem:TDMA:RING:STATus?", args)
                fields = [int(x) for x in raw.strip().strip('"').split(",")
                          if x.strip().lstrip("-").isdigit()]
                if len(fields) < 6 or fields[0] != 1 or fields[4] != 1 or fields[5] != 1:
                    pending.append(board.address)
            if not pending:
                break
            time.sleep(0.05)
        progress.emit("board_recording_started", epoch=epoch, samples=count,
                      interval_us=period_us, query_policy="TRIGGER_ONLY_DURING_ACQUISITION")
        # Every sample is produced by the board clock, independently of this wait.
        deadline = time.monotonic() + count * period_us / 1e6 + 0.5
        while time.monotonic() < deadline:
            time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))
    finally:
        if not getattr(args, "leave_running", False) and not getattr(
                args, "capture_waveforms", False):
            # Stop every board before any frozen export or status query. Attempt
            # all STOPs even if START or another STOP failed; retain each failure.
            stop_errors = {}
            for board in ordered:
                row = dict(node=board.address, action="STOP_BEFORE_EXPORT")
                actions.append(row)
                try:
                    row["response"] = board_command(board, "SYSTem:TDMA:RING:STOP", args)
                    normalized = row["response"].strip().strip('"')
                    if normalized != "OK":
                        if normalized != "OK(no payload; verified by state readback)":
                            raise RuntimeError(f"STOP rejected: {row['response']}")
                        # USB CDC can lose the STOP reply after the firmware
                        # consumed the intent.  The placeholder only promotes
                        # with a fresh stopped readback; a real refusal leaves
                        # the ring enabled (or config != applied) and still fails.
                        stopped = stopped_snapshot(board, args, ordered.index(board))
                        row["ack_disposition"] = normalized
                        row["stopped_readback"] = stopped
                        if not stopped.get("passed"):
                            raise RuntimeError(f"STOP rejected: {row['response']}")
                except Exception as exc:
                    row["error"] = f"{type(exc).__name__}: {exc}"
                    stop_errors[board.address] = row["error"]
            if stop_errors:
                raise RuntimeError(f"STOP before export failed: {stop_errors}")
    with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
        futures = {board.address: pool.submit(export_frozen, board, args, board_command,
            epoch=epoch, path=out_dir / f"node{index}_board_record.bin")
            for index, board in enumerate(ordered)}
        records = {}
        export_errors = {}
        for address, future in futures.items():
            try:
                records[address] = future.result()
            except Exception as exc:
                export_errors[address] = f"{type(exc).__name__}: {exc}"
    (out_dir / "board-records.json").write_text(json.dumps(dict(records=records,
        errors=export_errors, epoch=epoch), indent=2), encoding="utf-8")
    if export_errors:
        raise RuntimeError(f"board frozen export failed: {export_errors}")
    progress.emit("board_recording_exported", epoch=epoch, board_count=len(records))
    return records


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
    expected = {
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
    if "origin_capture_phase_delay_cycles" in data_link:
        # TRN-03's anchor is Node zero; followers retain DATA sampling/replay.
        expected["flight_origin_capture_phase_delay_cycles"] = int(
            data_link["origin_capture_phase_delay_cycles"] or data_link["data_phase_delay_cycles"]
            if node_index == 0 else data_link["data_phase_delay_cycles"])
    return expected


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
                   allow_capture_disturbance: bool = False,
                   missing_allowance: int = 3,
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
    feedback_drift_ok = feedback_sequence_drift_ok(
        runtime_deltas["ring_up_tx_sequence"],
        runtime_deltas["ring_down_rx_sequence"], node_index)
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
    # The gap is the instantaneous DEPTH of frames still in flight: down_rx is a
    # transport sequence, not a received count, so the origin legitimately holds
    # one frame in the ring plus one in the accept pipeline.  Deeper than that is
    # a real loss and stays an error.
    feedback_ok = feedback_sequence_ok(feedback_sequence_gap, node_index)
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
        (feedback_ok, "feedback_sequence_out_of_window"),
        (feedback_drift_ok, "feedback_sequence_drift"),
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
            # The firmware marks its stale-feedback timeout as a V1 diagnostic
            # bring-up gate (tdma_pio_spi_ring_adapter_tx.c: stale_timeout_ns),
            # and image integrity is already covered by the bitmap/WKC/reject
            # checks below.  A single freshness miss in a window with zero
            # rejects stays a warning; two or more remain a hard error.
            (process_deltas["receive_missing_count"] <= missing_allowance,
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
        window_up = sum(
            (interval["deltas"].get("runtime") or {}).get(
                "ring_up_tx_sequence", 0) for interval in intervals)
        window_down = sum(
            (interval["deltas"].get("runtime") or {}).get(
                "ring_down_rx_sequence", 0) for interval in intervals)
        node_errors: list[str] = []
        # Per-interval advance jitters by bursts (TX re-emits on a deadline while
        # RX catches up), so the loss evidence is the net drift over the window;
        # a sustained leak grows it every interval.
        window_drift = abs(window_up - window_down)
        if (window_drift > 2 if node_index == 0 else window_drift != 0):
            node_errors.append("feedback_sequence_window_drift")
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


def save_latched_capture(board, args, latched, calibration_generation,
                         capture_epoch):
    """SAVE a capture that was already latched while the ring was running."""
    # A recorder export can leave a storage transaction in RECEIVING; that
    # blocks every new begin_file_write and makes SAVE return -200. Abort the
    # stale transaction before saving the capture.
    write_status = board_command(
        board, "SYSTem:STORage:FILE:WRITe:STATus?", args)
    write_values = [value.strip().strip('"')
                    for value in next(csv.reader([write_status]), [])]
    if len(write_values) >= 2 and write_values[0] not in {"0", "IDLE"}:
        board_command(
            board,
            f"SYSTem:STORage:FILE:WRITe:ABORt {write_values[1]}", args)

    response = board_command(
        board,
        f"CALibration:RING:CAPTure:SAVE "
        f"{calibration_generation},{capture_epoch}", args)
    values = [value.strip().strip('"')
              for value in next(csv.reader([response]), [])]
    if len(values) != 3 or values[0] != "OK":
        try:
            import subprocess
            root = Path(__file__).resolve().parents[2]
            result = subprocess.run(
                [sys.executable,
                 str(root / "tools/interface/scpi_query/scpi_query.py"),
                 board.port,
                 f"CALibration:RING:CAPTure:SAVE "
                 f"{calibration_generation},{capture_epoch}",
                 "--timeout", "30", "--settle", "0.5"],
                capture_output=True, text=True, timeout=40, cwd=root)
            out = (result.stdout or "").strip()
            response = (out.split("=>", 1)[1].strip().strip('"')
                        if "=>" in out else response)
            values = [value.strip().strip('"')
                      for value in next(csv.reader([response]), [])]
        except Exception:                                                 # noqa: BLE001
            pass
    if len(values) != 3 or values[0] != "OK":
        raise RuntimeError(
            f"{board.address}: ring capture SD save rejected: {response!r}")
    job_id = int(values[1], 0)
    path = values[2]
    deadline = time.monotonic() + args.capture_timeout
    last = ""
    while time.monotonic() < deadline:
        last = board_command(board, "SYSTem:STORage:JOB?", args)
        job = [value.strip().strip('"')
               for value in next(csv.reader([last]), [])]
        if len(job) >= 8 and int(job[1], 0) == job_id:
            if job[0] == "DONE":
                return {"node_id": board.address, "sd_path": path,
                        "job_id": job_id, "size": int(job[4], 0),
                        "latch_attempts": latched["latch_attempts"],
                        "latch_status": latched["ready_status"],
                        "load_mask_before": latched["load_mask"],
                        "load_mask_during_capture": latched["load_mask"],
                        "load_mask_restored": latched["load_mask"],
                        "schedule_before": latched["schedule_before"],
                        "schedule_after": latched["schedule_after"],
                        "schedule_validation": latched["schedule_validation"],
                        "capture_debug": {}}
            if job[0] in {"FAIL", "ERROR"}:
                raise RuntimeError(
                    f"{board.address}: ring capture SD job failed: {job!r}")
        time.sleep(0.02)
    raise RuntimeError(
        f"{board.address}: ring capture SD save timeout: {last!r}")


def capture_ring_waveforms(
        ordered: list[Board], args: argparse.Namespace, *,
        calibration_generation: int, capture_epoch: int,
        out_dir: Path,
        original_load_masks: dict[str, int] | None = None
        ) -> dict[str, Any]:
    capture_dir = out_dir / "captures"
    load_masks = original_load_masks or {}
    # Latch while the ring is running, then stop every board before SAVE.
    args.capture_latch_only = True
    try:
        latched = [
            save_ring_capture(
                board, args,
                calibration_generation=calibration_generation,
                capture_epoch=capture_epoch,
                original_load_mask=load_masks.get(board.address))
            for board in ordered]
    finally:
        args.capture_latch_only = False
    for board in ordered:
        board_command(board, "SYSTem:TDMA:RING:STOP", args)
    time.sleep(1.0)
    saved = [
        save_latched_capture(
            board, args, latched_row,
            calibration_generation, capture_epoch)
        for board, latched_row in zip(ordered, latched)]
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
        "acquisition_policy": "SCPI_TRIGGER_BOARD_CLOCK_FROZEN_EXPORT",
        "board_record_interval_s": args.sample_interval_s,
        "diagnostic_continue": args.diagnostic_continue,
        "waveform_window_ns": args.waveform_window_ns,
        "sck_frequency_tolerance_percent":
            args.sck_frequency_tolerance_percent,
        "sck_duty_tolerance_percent": args.sck_duty_tolerance_percent,
        "stage": args.stage,
        "dpll_provisional": args.dpll_provisional,
        "dpll_debug_admission": bool(
            args.dpll_provisional and args.diagnostic_continue),
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
    board_records: dict[str, Any] = {}
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
        # Clear the previous run's frozen record while the ring is still stopped
        # on its applied generation; staging below advances ring_config_seq and
        # the board would then refuse the SAVE, leaving FROZEN un-clearable.
        for board in ordered:
            save_previous_record(board, args, actions)
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
        # Optional second argument selects the reference node's on-cadence
        # publish gate (gate policy only, not the physical emission cadence
        # budget).  Omitted by default so existing callers keep sending the
        # historic single-argument form unchanged.
        diagnostic_command = f"SYSTem:TDMA:RING:DIAGnostic {diagnostic_mode}"
        if getattr(args, "origin_cadence", False):
            diagnostic_command += ",1"
        for node_index, board in enumerate(ordered):
            actions.append(checked_stopped_ring_action(
                board, "DIAGNOSTIC_MODE",
                diagnostic_command,
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
        if args.dpll_provisional and args.diagnostic_continue:
            for board in ordered:
                actions.append(activate_dpll_debug_admission(board, args))
        if dpll_schedule_required:
            dpll_schedule_before = {
                board.address: read_tdma_schedule(board, args)
                for board in ordered
            }
        for node_index, board in enumerate(ordered):
            if getattr(args, "no_t2", False):
                fifo_seed[board.address] = {
                    "passed": True, "errors": [], "deltas": {}}
                continue
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
        if getattr(args, "no_record", False):
            for board in start_order:
                board_command(board, "SYSTem:TDMA:RING:ARM", args)
            for board in start_order:
                actions.append(dict(node=board.address, action="START",
                    response=board_command(board, "SYSTem:TDMA:RING:START", args)))
            for board in start_order:
                wait_runtime_config_applied(
                    board, args, board_ids.index(board.address))
            for board in start_order:
                wait_runtime_started(
                    board, args, board_ids.index(board.address))
            progress.emit("board_recording_started", epoch=0, samples=0,
                          interval_us=0, query_policy="NO_RECORD")
            progress.emit("complete", passed=True, left_running=True,
                          error="", summary="")
            hold_s = float(getattr(args, "hold_s", 0.0) or 0.0)
            if hold_s > 0.0:
                progress.emit("hold", hold_s=hold_s)
                # release the persistent serial handles so an external step
                # (relay test) can use the ports while the ring keeps running
                close_persistent_connections()
                time.sleep(hold_s)
            return 0
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
        if float(getattr(args, "soak_host_s", 0.0) or 0.0) > 0.0:
            # RING:START is only issued inside the board-record acquisition, so
            # the host soak has to raise data_enabled itself, in the same
            # follower-first order and with the same transient-retry policy.
            for board in start_order:
                response = board_command(
                    board, "SYSTem:TDMA:RING:START", args)
                for _attempt in range(3):
                    if response.strip().strip('"') == "OK":
                        break
                    time.sleep(0.05)
                    response = board_command(
                        board, "SYSTem:TDMA:RING:START", args)
                if response.strip().strip('"') != "OK":
                    raise RuntimeError(
                        f"{board.address}: RING:START rejected {response!r} "
                        f"ring={board_command(board, 'SYSTem:TDMA:RING:STATus?', args)} "
                        f"err={board_command(board, 'SYSTem:ERR?', args)}")
            soak = host_soak(ordered, args, progress, out_dir)
            progress.emit(
                "complete", passed=soak["passed"], left_running=False,
                error="",
                summary=str(Path(out_dir) / "soak-host.json"),
                boards=soak["boards"])
            return 0 if soak["passed"] else 1
        board_records = acquire_board_records(
            ordered, start_order, args, actions, progress, out_dir)
        before, after, startup_barrier, soak_timeline, soak_validation = evaluate_board_records(
            board_records, ordered, args, config)
        if not startup_barrier.get("passed"):
            startup_gate_error = "explicit startup barrier timed out"
        final_nodes = soak_timeline[-1]["nodes"]
        missing_final = set(board_ids) - set(final_nodes)
        if missing_final:
            raise RuntimeError(
                "final periodic samples missing: " +
                ", ".join(sorted(missing_final)))
        after = final_nodes
        if dpll_schedule_required:
            for board in ordered:
                after_schedule = after[board.address]["schedule"]
                dpll_schedule_gate["nodes"][board.address] = (
                    validate_dpll_schedule(
                        dpll_schedule_before[board.address], after_schedule,
                        allow_capture_disturbance=getattr(
                            args, "allow_capture_disturbance", False),
                        dpll_load_active=bool(
                            getattr(args, "dpll_provisional", False))))
            dpll_schedule_gate["passed"] = all(
                item["passed"]
                for item in dpll_schedule_gate["nodes"].values())
        for node_index, board in enumerate(ordered):
            # The node-level pair spans the whole run, so the per-interval
            # freshness allowance has to scale with the number of intervals.
            node_intervals = max(
                1, int(round(float(getattr(args, "window_s", 3.0)) /
                             float(getattr(args, "sample_interval_s", 0.5)))))
            node_errors, deltas = validate_node(
                node_index, len(ordered), before[board.address]["runtime"],
                after[board.address]["runtime"],
                before[board.address]["flight"],
                after[board.address]["flight"],
                fifo_seed[board.address]["errors"],
                fifo_seed[board.address]["deltas"],
                require_process_image=args.stage == "process-image",
                allow_capture_disturbance=getattr(
                    args, "allow_capture_disturbance", False),
                physical_after=after[board.address]["physical"],
                expected_physical=expected_flight_phase(config, node_index),
                missing_allowance=3 * node_intervals)
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
                "service_timing": service_timing_snapshot(
                    board, args,
                    cycle_ticks=int(
                        (after[board.address].get("schedule") or {}).get(
                            "cycle_cycles", 0) or 0) or None),
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
        if getattr(args, "capture_waveforms", False) and not getattr(
                args, "leave_running", False):
            # Capture needs the ring running; the recorder finally deliberately
            # kept it alive for this block. Stop it now so the handoff/STOP
            # evidence and later stages see a stopped ring.
            for board in ordered:
                actions.append(dict(
                    node=board.address, action="STOP_AFTER_CAPTURE",
                    response=board_command(
                        board, "SYSTem:TDMA:RING:STOP", args)))
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
        if getattr(args, "leave_on_failure", False) and args.leave_running \
                and not closed_loop_passed:
            # Diagnostic bench handoff: the ring is still the only 10 MHz
            # maintenance carrier available for the REFMEM broadcast HIL.
            leave_running = True
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
                diagnostic_continue=(args.diagnostic_continue or
                                     getattr(args, "leave_on_failure", False)))
            if not strict_handoff_passed and not args.diagnostic_continue \
                    and not getattr(args, "leave_on_failure", False):
                error = merge_error(
                    error, "leave-running final runtime readback failed")
        # The origin entry (TDMA-FLIGHT-002B1) may only be licensed while the
        # ring is running: the preparation stage machine advances on ring
        # cycles, not on host polling. Bind the result before the branch so the
        # summary can report it even when the trial was not requested.
        origin_trial: dict[str, Any] = {}
        if leave_running:
            if args.origin_trial:
                origin_trial = origin_trial_probe(ordered, args, out_dir)
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
        "board_records": board_records,
        "ring_capture": ring_capture,
        "ring_capture_error": capture_error,
        "ring_analysis": ring_analysis,
        "ring_analysis_error": analysis_error,
        "capture_load_masks": capture_load_masks,
        "running_handoff": running_handoff,
        "left_running": leave_running,
        "origin_trial": origin_trial,
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

