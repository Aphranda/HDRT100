#!/usr/bin/env python3
"""Capture the bounded internal VDC summary trace without runtime polling.

Core1 updates bounded SRAM counters on service/success and commits one record
per selected interval. No runtime host polling or SD writes are used. This is a
bounded internal observer, not a zero-cost probe or physical GPIO measurement.
Prepare a fresh session with TDMA stopped before --start-ring;
do not TRAIN or change topology after trace ARM. --skip-arm only recovers an
existing capture and never claims that its entire runtime was host-silent.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import math
from contextlib import ExitStack
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import open_serial_port, read_serial_line_idle  # noqa: E402
from tools.vdc_priority_trace.vdc_priority_trace import (  # noqa: E402
    decode,
    download_capture,
    parse_status,
    validate_summary_interval_ms,
)
from tools.tdma_ring_monitor.tdma_field_parse import FIELDS as TDMA_FIELDS


TRACE_ROOT = "SYSTem:VDC:PRIORity:TRACe"
STATE_FROZEN = 3
STATE_IDLE = 0
REASON_STOP = 1
REASON_TARGET_COMPLETE = 8
REASON_RELEASED = 6
MAX_QUIET_SECONDS = 60.0  # Legacy capacity; explicit intervals scale this window.


@dataclass(frozen=True)
class BoardSpec:
    name: str
    port: str


@dataclass
class BoardResult:
    name: str
    port: str
    capture_id: int
    arm_response: str = ""
    arm_attempted: bool = False
    stop_response: str = ""
    release_response: str = ""
    status_before_stop: dict[str, int] | None = None
    status_after_stop: dict[str, int] | None = None
    status_after_release: dict[str, int] | None = None
    binary: str | None = None
    decoded: str | None = None
    records: int = 0
    coverage_incomplete: bool = True
    passed: bool = False
    error: str | None = None
    assessment: dict[str, Any] | None = None


def timestamp_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def parse_board(value: str) -> BoardSpec:
    name, separator, port = value.partition("=")
    if not separator or not name.strip() or not port.strip():
        raise argparse.ArgumentTypeError("board must be NAME=PORT")
    return BoardSpec(name.strip(), port.strip())


def arm_command(capture_id: int, *, origin: bool, summary_interval_ms: int | None = None) -> str:
    if not 0 < capture_id <= 0xFFFFFFFF:
        raise ValueError("capture_id must be in 1..0xffffffff")
    suffix = "" if summary_interval_ms is None else f",{validate_summary_interval_ms(summary_interval_ms)}"
    return f"{TRACE_ROOT}:SUMMary:{'ORIGin' if origin else 'PHASe'} {capture_id}{suffix}"


def validate_duration(duration_s: float, summary_interval_ms: int | None = None) -> float:
    duration_s = float(duration_s)
    interval = 1000 if summary_interval_ms is None else validate_summary_interval_ms(summary_interval_ms)
    limit = min(600.0, MAX_QUIET_SECONDS * interval / 1000)
    if not math.isfinite(duration_s) or not 0 < duration_s <= limit:
        raise ValueError(f"current summary interval supports a quiet window of 0..{limit:g} seconds")
    return float(duration_s)


def summary_schema(origin: bool, summary_interval_ms: int | None) -> int:
    return (8 if origin else 7) if summary_interval_ms is None else (10 if origin else 9)


def quiet_wait(duration_s: float) -> None:
    """Bound each host wait while issuing no device commands between waits."""
    remaining = duration_s
    while remaining > 0:
        chunk = min(60.0, remaining)
        time.sleep(chunk)
        remaining -= chunk


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def query(ser: Any, command: str, timeout_s: float) -> str:
    """Send one maintenance command and return its first non-log response."""
    deadline = time.monotonic() + timeout_s
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    while time.monotonic() < deadline:
        line = read_serial_line_idle(ser, deadline)
        if line is None or is_log_line(line):
            continue
        line = line.strip()
        if line == '"OK"':
            return "OK"
        if line.startswith('"OK"'):
            line = line[4:].lstrip(", ")
        if line:
            return line
    return "<timeout>"


def status_query(ser: Any, timeout_s: float) -> dict[str, int]:
    return parse_status(query(ser, f"{TRACE_ROOT}:STATus?", timeout_s))


def wait_frozen(ser: Any, timeout_s: float, poll_s: float) -> dict[str, int]:
    deadline = time.monotonic() + timeout_s
    last: dict[str, int] | None = None
    while time.monotonic() < deadline:
        last = status_query(ser, min(timeout_s, max(0.2, deadline - time.monotonic())))
        if (last["state"] == STATE_FROZEN and
                last["request_seq"] == last["ack_seq"]):
            return last
        time.sleep(min(poll_s, max(0.0, deadline - time.monotonic())))
    raise TimeoutError(f"trace did not freeze: {last}")


def wait_armed(ser: Any, timeout_s: float, poll_s: float) -> dict[str, int]:
    deadline = time.monotonic() + timeout_s
    last: dict[str, int] | None = None
    while time.monotonic() < deadline:
        last = status_query(ser, min(timeout_s, max(0.2, deadline - time.monotonic())))
        if last["request_seq"] == last["ack_seq"] and last["state"] in (1, 2):
            return last
        time.sleep(min(poll_s, max(0.0, deadline - time.monotonic())))
    raise TimeoutError(f"trace did not acknowledge ARM: {last}")


def freeze_trace(ser: Any, result: BoardResult, args: argparse.Namespace) -> None:
    """Freeze one trace after the caller has stopped every TDMA ring."""
    result.stop_response = query(ser, f"{TRACE_ROOT}:STOP", args.timeout)
    if result.stop_response != "1":
        raise RuntimeError(f"{result.name}: summary STOP rejected: {result.stop_response}")
    result.status_before_stop = wait_frozen(ser, args.stop_timeout, args.poll_interval_s)
    # FULL/BINDING/etc. are valuable failed captures. Preserve and decode them.


def release_trace(ser: Any, result: BoardResult, args: argparse.Namespace) -> None:
    result.release_response = query(ser, f"{TRACE_ROOT}:RELease", args.timeout)
    if result.release_response != "1":
        raise RuntimeError(f"{result.name}: summary RELEASE rejected: {result.release_response}")
    result.status_after_release = wait_released(ser, args.timeout, args.poll_interval_s)


def wait_released(ser: Any, timeout_s: float, poll_s: float) -> dict[str, int]:
    deadline = time.monotonic() + timeout_s
    last: dict[str, int] | None = None
    while time.monotonic() < deadline:
        last = status_query(ser, min(timeout_s, max(0.2, deadline - time.monotonic())))
        if (last["state"] == STATE_IDLE and last["reason"] == REASON_RELEASED and
                last["request_seq"] == last["ack_seq"]):
            return last
        time.sleep(min(poll_s, max(0.0, deadline - time.monotonic())))
    raise TimeoutError(f"trace did not acknowledge RELEASE: {last}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", action="append", type=parse_board, required=True,
                        help="NAME=PORT; repeat once per board")
    parser.add_argument("--duration-s", type=float, required=True,
                        help="quiet internal observation duration; no runtime polling")
    parser.add_argument("--summary-interval-ms", type=int,
                        help="explicit 1000..10000 ms in 1000 ms steps; selects schema 9/10")
    parser.add_argument("--capture-id", type=int, default=1)
    parser.add_argument("--origin", action="store_true",
                        help="arm origin schema 8 (10 with explicit interval); default is phase 7/9")
    parser.add_argument("--origin-board", action="append", default=[],
                        help="board name using origin schema; repeat for mixed-role captures")
    parser.add_argument("--skip-arm", action="store_true",
                        help="recover an existing capture; full-run silence is unproven")
    parser.add_argument("--start-ring", action="store_true",
                        help="ARM/start preconfigured stopped TDMA after trace ARM, no TRAIN")
    parser.add_argument("--trial-epoch", type=int,
                        help="origin diagnostic live-timestamp grant, required for START")
    parser.add_argument("--max-success-gap-s", type=float, default=1.0,
                        help="diagnostic input continuity bound; not a per-cycle guarantee")
    parser.add_argument("--stop-ring", action="store_true",
                        help="STOP every TDMA ring before freezing/reading traces")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--stop-timeout", type=float, default=8.0)
    parser.add_argument("--poll-interval-s", type=float, default=0.1)
    parser.add_argument("--expected-build")
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        args.duration_s = validate_duration(args.duration_s, args.summary_interval_ms)
    except ValueError as exc:
        parser.error(str(exc))
    return args


def ring_status(ser: Any, timeout_s: float) -> dict[str, int]:
    raw = query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", timeout_s)
    values = [int(word.strip().strip('"'), 0) for word in raw.split(",")]
    if len(values) != len(TDMA_FIELDS):
        raise ValueError("invalid TDMA status field count")
    return dict(zip(TDMA_FIELDS, values))


def stop_all(handles: list[Any], args: argparse.Namespace,
             events: list[dict[str, Any]]) -> None:
    """Send every STOP before any readback; only confirmed STOP admits READ."""
    failures = []
    for ser in handles:
        try:
            response = query(ser, "SYSTem:TDMA:RING:STOP", args.timeout)
            events.append(dict(port=ser.port, command="TDMA STOP", response=response))
            if response != "OK":
                failures.append(f"{ser.port}: STOP response {response}")
        except Exception as exc:
            failures.append(f"{ser.port}: STOP {exc}")
    deadline = time.monotonic() + args.stop_timeout
    pending = list(handles)
    while pending and time.monotonic() < deadline:
        for ser in list(pending):
            try:
                status = ring_status(ser, args.timeout)
                if not any(status[k] for k in ("ring_enabled", "ring_adapter_started",
                                               "ring_up_running", "ring_down_running")):
                    events.append(dict(port=ser.port, stopped=True))
                    pending.remove(ser)
            except Exception as exc:
                events.append(dict(port=ser.port, readback_error=str(exc)))
        if pending:
            time.sleep(args.poll_interval_s)
    if pending:
        failures.append(f"STOP not confirmed: {[s.port for s in pending]}")
    if failures:
        raise RuntimeError("; ".join(failures))


def assess_capture(decoded: dict[str, Any], requested_s: float,
                   origin: bool, max_success_gap_s: float = 1.0,
                   summary_interval_ms: int | None = None) -> dict[str, Any]:
    """Separate actual coverage from successful follow observations."""
    status, rows = decoded["status"], decoded["records"]
    hz = status["tick_hz"]
    schema_ok = status["schema"] == summary_schema(origin, summary_interval_ms)
    interval_ok = status["sample_interval_ms"] == (summary_interval_ms or 1000)
    first = rows[0]["observed_start_raw"] if rows else None
    last = rows[-1]["observed_end_raw"] if rows else None
    observed = (last - first) / hz if rows and hz else 0.0
    terminal = bool(rows and "TERMINAL" in rows[-1]["flag_names"])
    flags = {name: sum(name in r["flag_names"] for r in rows)
             for name in ("TERMINAL", "SERVICE_GAP", "COUNTER_RESET", "CLOCK_INVALID",
                          "UNBOUND", "NO_SUCCESS", "FIELD_SATURATED", "COUNTER_SATURATED")}
    incomplete = not rows or any(r["coverage_incomplete"] for r in rows)
    empty = [r["bin_index"] for r in rows if r["observed_end_raw"] > r["observed_start_raw"]
             and not r["success_count"]]
    # A sealed summary proves record coverage, not the separate GUARD verdict
    # or physical output precision. Bench acceptance must bind those receipts.
    completed = status["reason"] == REASON_STOP or (
        status["reason"] == REASON_TARGET_COMPLETE and status["schema"] in (9, 10)
        and status["state"] == 3)
    coverage = bool(schema_ok and interval_ok and terminal and completed
                    and not incomplete and observed >= requested_s)
    success = [r for r in rows if r["success_count"]]
    success_first = (success[0]["observed_start_raw"] + success[0]["first_success_offset_ticks"]
                     if success else None)
    success_last = (success[-1]["observed_start_raw"] + success[-1]["last_success_offset_ticks"]
                    if success else None)
    gap = max((r["max_success_gap_ticks"] / hz for r in rows), default=0)
    return dict(schema_ok=schema_ok, interval_ok=interval_ok,
                summary_interval_ms=status["sample_interval_ms"], first_raw_tick=first, last_raw_tick=last,
                observed_s=observed, requested_s=requested_s, terminal=terminal,
                freeze_reason=status["reason"], flags=flags, crc_verified=True,
                coverage_complete=coverage, empty_nonzero_bins=empty,
                all_bins_have_success=bool(success) and not empty,
                first_success_raw_tick=success_first, last_success_raw_tick=success_last,
                success_span_s=(success_last-success_first)/hz if success else 0,
                initial_wait_s=(success_first-first)/hz if success else None,
                success_gap_basis=('after_first_success' if rows and
                    'STEADY_SUCCESS_GAP' in rows[0]['flag_names'] else 'start_inclusive'),
                success_gap_limit_s=max_success_gap_s,
                success_gap_passed=bool(success) and gap <= max_success_gap_s,
                success_count=sum(r["success_count"] for r in rows),
                frequency_applied_count=sum(r["frequency_applied_count"] for r in rows),
                phase_applied_count=sum(r["phase_applied_count"] for r in rows),
                residual_range_ns=([min(r["residual_min_ns"] for r in success),
                                    max(r["residual_max_ns"] for r in success)]
                                   if success and not origin else None),
                frequency_range_ppb=([min(r["min_ppb"] for r in success),
                                      max(r["max_ppb"] for r in success)] if success else None),
                max_service_gap_s=max((r["max_service_gap_ticks"] / hz for r in rows), default=0),
                max_success_gap_s=gap,
                requested_window_proven=False,
                physical_lock_qualified=False)


def run(args: argparse.Namespace, opener: Callable[..., Any] = open_serial_port) -> dict[str, Any]:
    summary_interval_ms = getattr(args, "summary_interval_ms", None)
    duration = validate_duration(args.duration_s, summary_interval_ms)
    gap_limit = getattr(args, "max_success_gap_s", 1.0)
    if not math.isfinite(gap_limit) or gap_limit <= 0:
        raise ValueError("max-success-gap-s must be finite and positive")
    if not args.board or len({b.name for b in args.board}) != len(args.board) or len({b.port for b in args.board}) != len(args.board):
        raise ValueError("distinct board names and ports required")
    if args.capture_id <= 0 or args.capture_id + len(args.board) - 1 > 0xFFFFFFFF:
        raise ValueError("capture-id range invalid")
    recovery = bool(getattr(args, "skip_arm", False))
    start_ring = bool(getattr(args, "start_ring", False))
    if recovery == start_ring:
        raise ValueError("select exactly one of --start-ring or --skip-arm")
    origins = set(getattr(args, "origin_board", ()))
    if getattr(args, "origin", False):
        origins.update(b.name for b in args.board)
    if origins - {b.name for b in args.board}:
        raise ValueError("unknown origin board")
    if start_ring and (len(origins) != 1 or not 0 < (getattr(args, "trial_epoch", None) or 0) <= 0xFFFFFFFF):
        raise ValueError("START requires one origin and a nonzero trial epoch")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    if (args.out_dir / "summary.json").exists():
        raise ValueError("output already contains a summary")
    results = [BoardResult(b.name, b.port, args.capture_id + i) for i,b in enumerate(args.board)]
    errors, events, handles = [], [], []
    quiet_elapsed, stopped = 0.0, False
    with ExitStack() as stack:
        try:
            for spec in args.board:
                opened = opener(spec.port, args.baud, args.timeout, args.settle)
                if hasattr(opened, "__enter__"):
                    handles.append(stack.enter_context(opened))
                else:
                    handles.append(opened)
                    stack.callback(opened.close)
            if recovery:
                for result in results:
                    result.arm_response, result.arm_attempted = "prearmed", True
            else:
                for spec, ser in zip(args.board, handles):
                    status = ring_status(ser, args.timeout)
                    if any(status[k] for k in ("ring_adapter_started", "ring_enabled",
                                               "ring_up_running", "ring_down_running")):
                        raise RuntimeError(f"{spec.name}: prepare stopped TDMA before capture")
                    build = query(ser, "SYST:FW:BUILD?", args.timeout).strip('"')
                    events.append(dict(port=ser.port, build=build))
                    if args.expected_build and build != args.expected_build:
                        raise RuntimeError(f"{spec.name}: build mismatch {build}")
                for result, ser in zip(results, handles):
                    result.arm_attempted = True
                    result.arm_response = query(ser, arm_command(result.capture_id,
                        origin=result.name in origins, summary_interval_ms=summary_interval_ms), args.timeout)
                    if result.arm_response != str(result.capture_id):
                        raise RuntimeError(f"{result.name}: ARM rejected {result.arm_response}")
                    status = wait_armed(ser, args.timeout, args.poll_interval_s)
                    if (status["capture_id"] != result.capture_id or
                            status["schema"] != summary_schema(result.name in origins, summary_interval_ms) or
                            status["sample_interval_ms"] != (summary_interval_ms or 1000)):
                        raise RuntimeError(f"{result.name}: ARM identity mismatch")
                order = sorted(range(len(results)), key=lambda i: results[i].name in origins)
                for i in order:
                    response = query(handles[i], "SYSTem:TDMA:RING:ARM", args.timeout)
                    events.append(dict(port=handles[i].port, command="TDMA ARM", response=response))
                    if response != "OK":
                        raise RuntimeError(f"{results[i].name}: TDMA ARM rejected {response}")
                for i in order:
                    deadline = time.monotonic() + args.stop_timeout
                    while True:
                        status = ring_status(handles[i], args.timeout)
                        if status["ring_adapter_started"] and status["ring_enabled"]:
                            break
                        if time.monotonic() >= deadline:
                            raise TimeoutError(f"{results[i].name}: TDMA ARM not acknowledged")
                        time.sleep(args.poll_interval_s)
                # Independent ports, one bounded START per board. Join every
                # transaction before issuing trial or failure cleanup commands.
                with ThreadPoolExecutor(max_workers=len(handles)) as executor:
                    pending = {i: executor.submit(query, handles[i],
                        "SYSTem:TDMA:RING:START", args.timeout) for i in order}
                    starts = {}
                    for i, future in pending.items():
                        try:
                            starts[i] = future.result()
                        except Exception as exc:
                            starts[i] = f"<error: {exc}>"
                for i in order:
                    response = starts[i]
                    events.append(dict(port=handles[i].port, command="TDMA START", response=response))
                failed_starts = {results[i].name: r for i,r in starts.items() if r != "OK"}
                if failed_starts:
                    raise RuntimeError(f"START failed {failed_starts}")
                i = next(i for i,r in enumerate(results) if r.name in origins)
                command = f"CALibration:ORIGin:TRIAL {args.trial_epoch},8192,256,0"
                response = query(handles[i], command, args.timeout)
                events.append(dict(port=handles[i].port, command=command, response=response))
                grant = int(response)
                if not grant or grant & 1:
                    raise RuntimeError("origin trial grant rejected")
                begin = time.monotonic()
                quiet_wait(duration)
                quiet_elapsed = time.monotonic() - begin
        except Exception as exc:
            errors.append(str(exc))
        finally:
            # All-board STOP barrier precedes every trace operation, also on failure.
            try:
                stop_all(handles, args, events)
                stopped = len(handles) == len(results)
            except Exception as exc:
                errors.append(str(exc))
            if stopped:
                for result, ser in zip(results, handles):
                    if not result.arm_attempted:
                        continue
                    try:
                        freeze_trace(ser, result, args)
                    except Exception as exc:
                        result.error = str(exc)
                        errors.append(f"{result.name}: {exc}")
                for result, ser in zip(results, handles):
                    if not result.arm_attempted or result.error:
                        continue
                    try:
                        build = query(ser, "SYST:FW:BUILD?", args.timeout).strip('"')
                        if args.expected_build and build != args.expected_build:
                            raise RuntimeError(f"build mismatch {build}")
                        folder = args.out_dir / result.name
                        folder.mkdir(parents=True, exist_ok=True)
                        data, pages = download_capture(lambda c: query(ser, c, args.timeout), result.capture_id)
                        binary = folder / f"summary-{result.capture_id}.ram.bin"
                        binary.write_bytes(data)
                        result.binary = str(binary.relative_to(args.out_dir))
                        (folder / "read-pages.json").write_text(json.dumps(pages, indent=2), encoding="utf-8")
                        decoded = decode(data, expected_capture_id=result.capture_id)
                        path = folder / "decoded.json"
                        path.write_text(json.dumps(decoded, indent=2), encoding="utf-8")
                        result.decoded = str(path.relative_to(args.out_dir))
                        result.records = len(decoded["records"])
                        result.assessment = assess_capture(decoded, duration, result.name in origins,
                                                           gap_limit, summary_interval_ms)
                        result.coverage_incomplete = not result.assessment["coverage_complete"]
                        result.status_after_stop = result.status_before_stop
                        release_trace(ser, result, args)
                        result.passed = (not recovery and not result.coverage_incomplete
                                         and result.assessment["all_bins_have_success"]
                                         and result.assessment["success_gap_passed"])
                    except Exception as exc:
                        result.error = str(exc)
                        errors.append(f"{result.name}: {exc}")
                        # Keep failed/unknown evidence frozen for explicit recovery.
    summary = dict(schema="VDC_INTERNAL_SUMMARY_CAPTURE_V2", created_utc=timestamp_iso(),
        duration_s=duration, summary_interval_ms=summary_interval_ms or 1000,
        quiet_elapsed_s=quiet_elapsed, recovery_only=recovery,
        runtime_scpi_queries=0 if not recovery else None,
        runtime_host_polling=False if not recovery else None,
        runtime_writes="bounded SRAM counters and summary", all_boards_stopped=stopped,
        physical_lock_qualified=False, passed=not errors and all(r.passed for r in results),
        errors=errors, events=events, boards=[asdict(r) for r in results])
    (args.out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def main() -> int:
    args = parse_args()
    summary = run(args)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
