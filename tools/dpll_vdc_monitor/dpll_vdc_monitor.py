#!/usr/bin/env python3
"""Read-only NO5 DPLL/VDC observation monitor.

The monitor never arms a ring, submits a frame, changes an offset, or writes
storage.  It only reads the TDMA execution snapshot and the core1-owned VDC/
DPLL vectors, then writes a CSV/JSON/SVG report under ``out/``.  Raw SD
waveform captures remain an independent evidence source; ``--waveform-analysis``
can attach an existing ``ring_capture_analysis.json`` to the report.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import (  # noqa: E402
    is_scpi_log_line,
    open_serial_port,
    read_scpi_response,
)
try:
    # Package import is required when the monitor is imported by pytest or by
    # another tool from the repository root.  ``tdma_ring_monitor`` also has a
    # legacy script entry point with the same stem, so importing the package
    # form directly can resolve to that module on some Python path layouts.
    from tools.tdma_ring_monitor.tdma_field_parse import (  # noqa: E402
        parse_status_named,
    )
except ModuleNotFoundError:
    # Keep the standalone ``python tools/dpll_vdc_monitor/...`` invocation
    # working when ``tools`` itself is the only injected import root.
    from tdma_field_parse import parse_status_named  # type: ignore # noqa: E402


TDMA_STATUS_COMMAND = "SYSTem:REFMEM:SYNC:TDMA:STATus?"
VDC_STATUS_COMMAND = "SYSTem:SYNC:VDC:STATus?"
DPLL_STATUS_COMMAND = "SYSTem:SYNC:VDC:DPLL:STATus?"
READINESS_COMMAND = "SYSTem:SYNC:VDC:LOCK:READiness?"
VDC_VECTOR_COMMAND = "SYSTem:REFMEM:VDC:VECtor?"
DPLL_VECTOR_COMMAND = "SYSTem:REFMEM:DPLL:VECtor?"

VDC_STATUS_FIELDS = (
    "ready", "lock_state", "service_count", "first_service_ms",
    "last_service_ms", "sync_seq",
)
DPLL_STATUS_FIELDS = (
    "ready", "state", "service_count", "first_service_ms",
    "last_service_ms", "update_seq",
)
READINESS_FIELDS = (
    "input_ready", "locked", "reason", "state", "health_state",
    "accepted_count", "rejected_count", "last_reject_code",
    "observer_enabled", "observer_submitted", "observer_accepted",
    "observer_rejected", "observer_gate", "timestamp_source",
    "timestamp_resolution_ns", "timestamp_flags", "timestamp_eligible",
    "dictionary_entry_count", "dictionary_crc32", "dictionary_profile_crc32",
    "schedule_crc32", "payload_class", "source_slot_id", "reference_slot_id",
)
VDC_VECTOR_FIELDS = (
    "flags", "stable_sequence", "publish_sequence", "source_update_seq",
    "source_service_count", "schedule_epoch", "local_node_id",
    "reference_node_id", "node_count", "schedule_crc32", "servo_profile_crc32",
    "path_delay_table_crc32", "path_delay_generation", "dpll_state",
    "dpll_last_phase_error_ns", "dpll_last_frequency_error_ppb",
    "clock_phase_offset_ns", "clock_period_adjust_ppb", "quality_health_state",
    "quality_lock_quality_tier", "quality_last_timestamp_source",
    "quality_last_timestamp_resolution_ns", "quality_last_timestamp_flags",
    "gate_passed", "gate_reject_code", "payload_crc32",
)
DPLL_VECTOR_FIELDS = (
    "flags", "stable_sequence", "publish_sequence", "source_update_seq",
    "source_service_count", "ready", "schedule_epoch", "local_node_id",
    "reference_node_id", "node_count", "schedule_crc32", "servo_profile_crc32",
    "state", "dpll_update_seq", "last_phase_error_ns",
    "last_frequency_error_ppb", "last_offset_ns", "dco_valid", "dco_update_seq",
    "dco_source_model_seq", "dco_lock_state", "dco_phase_offset_ns",
    "dco_period_adjust_ppb", "quality_health_state", "quality_lock_quality_tier",
    "quality_last_timestamp_source", "quality_last_timestamp_resolution_ns",
    "quality_last_timestamp_flags", "gate_passed", "gate_reject_code",
    "path_delay_table_crc32", "path_delay_generation", "payload_crc32",
)

VECTOR_FLAG_VALID = 1 << 0
VECTOR_FLAG_STALE = 1 << 1
VECTOR_FLAG_LOCKED = 1 << 5
TIMESTAMP_SOURCE_HARDWARE_TICK = 2
TIMESTAMP_FLAG_DIAGNOSTIC_ONLY = 1 << 0
TIMESTAMP_FLAG_DPLL_ELIGIBLE = 1 << 1


@dataclass(frozen=True)
class BoardSpec:
    name: str
    port: str


@dataclass
class BoardSample:
    ts_utc: str
    elapsed_s: float
    board: str
    port: str
    tdma: dict[str, int]
    vdc_status: dict[str, int]
    dpll_status: dict[str, int]
    readiness: dict[str, int]
    vdc_vector: dict[str, int]
    dpll_vector: dict[str, int]
    trigger_sequence: int
    trigger_interval_ms: float | None
    simultaneous_feedback: bool
    error: str = ""


def _ring_sequence(sample: BoardSample) -> int | None:
    """Return a sequence that can be compared between ring participants.

    NO5 is an out-of-ring observer.  Its local DPLL update sequence is useful
    for checking that observations continue, but it is not a TDMA ring
    sequence and must not be compared with the NO1..NO4 ring counter.  The
    ring sequence is therefore only exported when the sample advertises an
    enabled/running ring or a valid ring clock observation.
    """
    tdma = sample.tdma
    if tdma.get("ring_clock_observation_valid", 0):
        value = tdma.get("ring_clock_observation_sequence", 0)
        return value if value > 0 else None
    if tdma.get("ring_enabled", 0):
        value = tdma.get("ring_seq", 0)
        return value if value > 0 else None
    return None


def _sequence_is_monotonic(samples: list[BoardSample]) -> bool:
    values = [sample.trigger_sequence for sample in samples
              if sample.error == "" and sample.trigger_sequence > 0]
    return bool(values) and all(current > previous
                                for previous, current in zip(values, values[1:]))


def parse_board_arg(value: str) -> BoardSpec:
    if "=" not in value:
        raise ValueError(f"board must be NAME=PORT: {value}")
    name, port = value.split("=", 1)
    name, port = name.strip(), port.strip()
    if not name or not port or not re.fullmatch(r"NO[1-8]", name.upper()):
        raise ValueError(f"board name must be NO1..NO8 and port must be non-empty: {value}")
    return BoardSpec(name.upper(), port)


def parse_int_csv(response: str, expected: int, *, status: str = "") -> dict[str, int]:
    fields = next(csv.reader([response]), [])
    fields = [field.strip().strip('"') for field in fields]
    if status:
        if not fields or fields[0].upper() != status.upper():
            raise ValueError(f"expected {status} response: {response!r}")
        fields = fields[1:]
    if len(fields) != expected:
        raise ValueError(f"field count {len(fields)} != {expected}: {response!r}")
    try:
        return {str(index): int(field, 0) for index, field in enumerate(fields)}
    except ValueError as exc:
        raise ValueError(f"non-integer response: {response!r}") from exc


def parse_named_int_response(response: str,
                             names: Iterable[str],
                             *, status: str = "") -> dict[str, int]:
    fields = next(csv.reader([response]), [])
    fields = [field.strip().strip('"') for field in fields]
    if status:
        if not fields or fields[0].upper() != status.upper():
            raise ValueError(f"expected {status} response: {response!r}")
        fields = fields[1:]
    names = tuple(names)
    if len(fields) != len(names):
        raise ValueError(f"field count {len(fields)} != {len(names)}: {response!r}")
    values: list[int] = []
    for field in fields:
        text = field.upper()
        if text == "TRUE":
            values.append(1)
        elif text == "FALSE":
            values.append(0)
        else:
            values.append(int(field, 0))
    return dict(zip(names, values, strict=True))


def parse_vector_response(response: str, names: Iterable[str]) -> dict[str, int]:
    return parse_named_int_response(response, names, status="OK")


def _select_trigger_sequence(tdma: dict[str, int],
                             vdc_vector: dict[str, int],
                             dpll_vector: dict[str, int]) -> int:
    """Select the monotonic counter used for trigger-period checks.

    In-ring nodes have a TDMA observation/ring counter.  The NO5 observer is
    deliberately outside that ring, therefore it must use the core1 vector's
    publish sequence.  ``source_update_seq`` is retained only as a final
    compatibility fallback for older firmware that did not expose publish
    sequences.
    """
    return int(
        tdma.get("ring_clock_observation_sequence") or
        tdma.get("ring_seq") or
        dpll_vector.get("publish_sequence") or
        vdc_vector.get("publish_sequence") or
        dpll_vector.get("source_update_seq") or
        vdc_vector.get("source_update_seq") or 0)


def _query(ser: Any, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    response = read_scpi_response(ser, command, timeout_s, require_match=True)
    if response == "<timeout>" or is_scpi_log_line(response):
        raise TimeoutError(f"{command}: {response}")
    return response


def _read_board(ser: Any, spec: BoardSpec, timeout_s: float,
                elapsed_s: float, previous: BoardSample | None) -> BoardSample:
    try:
        tdma_response = _query(ser, TDMA_STATUS_COMMAND, timeout_s)
        tdma = parse_status_named(tdma_response)
        vdc_status = parse_named_int_response(
            _query(ser, VDC_STATUS_COMMAND, timeout_s), VDC_STATUS_FIELDS)
        dpll_status = parse_named_int_response(
            _query(ser, DPLL_STATUS_COMMAND, timeout_s), DPLL_STATUS_FIELDS)
        readiness = parse_named_int_response(
            _query(ser, READINESS_COMMAND, timeout_s), READINESS_FIELDS)
        vdc_vector = parse_vector_response(
            _query(ser, VDC_VECTOR_COMMAND, timeout_s), VDC_VECTOR_FIELDS)
        dpll_vector = parse_vector_response(
            _query(ser, DPLL_VECTOR_COMMAND, timeout_s), DPLL_VECTOR_FIELDS)
        trigger_sequence = _select_trigger_sequence(tdma, vdc_vector, dpll_vector)
        interval_ms: float | None = None
        if previous is not None and trigger_sequence > previous.trigger_sequence:
            delta = trigger_sequence - previous.trigger_sequence
            if delta > 0:
                interval_ms = (elapsed_s - previous.elapsed_s) * 1000.0 / delta
        simultaneous = bool(
            tdma.get("ring_up_running", 0) and
            tdma.get("ring_down_running", 0) and
            tdma.get("simultaneous_feedback_loop_evidence", 0))
        return BoardSample(
            ts_utc=datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            elapsed_s=elapsed_s,
            board=spec.name,
            port=spec.port,
            tdma=tdma,
            vdc_status=vdc_status,
            dpll_status=dpll_status,
            readiness=readiness,
            vdc_vector=vdc_vector,
            dpll_vector=dpll_vector,
            trigger_sequence=trigger_sequence,
            trigger_interval_ms=interval_ms,
            simultaneous_feedback=simultaneous,
        )
    except (OSError, ValueError, TimeoutError, KeyError) as exc:
        return BoardSample(
            ts_utc=datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            elapsed_s=elapsed_s,
            board=spec.name,
            port=spec.port,
            tdma={}, vdc_status={}, dpll_status={}, readiness={},
            vdc_vector={}, dpll_vector={}, trigger_sequence=0,
            trigger_interval_ms=None, simultaneous_feedback=False,
            error=str(exc),
        )


def _board_summary(samples: list[BoardSample], *, expected_interval_ms: float,
                   interval_tolerance_ms: float,
                   observer: bool = False) -> dict[str, Any]:
    errors = [sample.error for sample in samples if sample.error]
    intervals = [sample.trigger_interval_ms for sample in samples
                 if sample.trigger_interval_ms is not None]
    latest = samples[-1] if samples else None
    interval_ok = bool(intervals) and all(
        abs(interval - expected_interval_ms) <= interval_tolerance_ms
        for interval in intervals)
    sequence_monotonic = _sequence_is_monotonic(samples)
    vector_ok = bool(latest and latest.vdc_vector and latest.dpll_vector and
                     (latest.vdc_vector.get("flags", 0) & VECTOR_FLAG_VALID) and
                     not (latest.vdc_vector.get("flags", 0) & VECTOR_FLAG_STALE))
    timestamp_ok = bool(latest and latest.readiness and
                        latest.readiness.get("timestamp_source") ==
                        TIMESTAMP_SOURCE_HARDWARE_TICK and
                        latest.readiness.get("timestamp_resolution_ns", 0) > 0 and
                        latest.readiness.get("timestamp_eligible") and
                        not (latest.readiness.get("timestamp_flags", 0) &
                             TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) and
                        (latest.readiness.get("timestamp_flags", 0) &
                         TIMESTAMP_FLAG_DPLL_ELIGIBLE))
    return {
        "board": latest.board if latest else "",
        "role": "observer" if observer else "ring_node",
        "samples": len(samples),
        "errors": errors,
        "ring_up_running": bool(latest and latest.tdma.get("ring_up_running")),
        "ring_down_running": bool(latest and latest.tdma.get("ring_down_running")),
        "simultaneous_feedback": bool(latest and latest.simultaneous_feedback),
        "trigger_sequence": latest.trigger_sequence if latest else 0,
        "trigger_sequence_monotonic": sequence_monotonic,
        "trigger_intervals_ms": intervals,
        "trigger_interval_ok": interval_ok,
        "vector_valid": vector_ok,
        "timestamp_eligible": timestamp_ok,
        "dpll_state": latest.dpll_vector.get("state", 0) if latest else 0,
        "dpll_locked": bool(latest and
                             (latest.dpll_vector.get("flags", 0) & VECTOR_FLAG_LOCKED)),
        "quality_health_state": latest.dpll_vector.get("quality_health_state", 0)
        if latest else 0,
        "quality_lock_quality_tier": latest.dpll_vector.get(
            "quality_lock_quality_tier", 0) if latest else 0,
        "phase_offset_ns": latest.dpll_vector.get("dco_phase_offset_ns", 0)
        if latest else 0,
        "period_adjust_ppb": latest.dpll_vector.get("dco_period_adjust_ppb", 0)
        if latest else 0,
        "last_reject_code": latest.readiness.get("last_reject_code", 0)
        if latest else 0,
    }


def _ring_sequence_consistency(samples_by_board: dict[str, list[BoardSample]],
                               *, tolerance: int) -> tuple[bool, int | None]:
    """Compare the latest TDMA sequence only across in-ring participants."""
    values: list[int] = []
    for samples in samples_by_board.values():
        if not samples:
            continue
        value = _ring_sequence(samples[-1])
        if value is not None:
            values.append(value)
    if len(values) < 2:
        return True, 0 if values else None
    skew = max(values) - min(values)
    return skew <= max(0, tolerance), skew


def _svg(samples_by_board: dict[str, list[BoardSample]], summaries: list[dict[str, Any]],
         *, duration_s: float, expected_interval_ms: float,
         sequence_skew_tolerance: int = 1) -> str:
    width, height = 1500, max(220, 150 + 90 * len(summaries))
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<style>text{font-family:ui-monospace,Consolas,monospace;fill:#172033}'
        '.title{font-size:20px;font-weight:700}.label{font-size:14px;font-weight:600}'
        '.small{font-size:12px}.ok{fill:#16803c}.bad{fill:#b42318}.grid{stroke:#d7dce5}'
        '.line{fill:none;stroke-width:2}</style>',
        f'<rect width="100%" height="100%" fill="#fff"/>',
        f'<text x="24" y="30" class="title">DPLL/VDC read-only observation — '
        f'{duration_s:g}s; expected trigger interval {expected_interval_ms:g}ms; '
        f'ring sequence skew tolerance {sequence_skew_tolerance}</text>',
    ]
    for index, summary in enumerate(summaries):
        y = 70 + index * 90
        observer = summary.get("role") == "observer"
        status = (summary["vector_valid"] and summary["timestamp_eligible"] and
                  summary["trigger_interval_ok"] and
                  summary.get("trigger_sequence_monotonic", False) and
                  (observer or summary["simultaneous_feedback"]))
        color = "ok" if status else "bad"
        chunks.append(
            f'<text x="24" y="{y}" class="label">{escape(summary["board"])} '
            f'role={escape(summary.get("role", "ring_node"))} '
            f'up={int(summary["ring_up_running"])} down={int(summary["ring_down_running"])} '
            f'simultaneous={int(summary["simultaneous_feedback"])} '
            f'vector={int(summary["vector_valid"])} timestamp={int(summary["timestamp_eligible"])} '
            f'dpll_state={summary["dpll_state"]}</text>')
        chunks.append(
            f'<text x="24" y="{y + 22}" class="small {color}">'
            f'seq={summary["trigger_sequence"]} '
            f'interval_ok={int(summary["trigger_interval_ok"])} '
            f'seq_monotonic={int(summary.get("trigger_sequence_monotonic", False))} '
            f'locked={int(summary["dpll_locked"])} '
            f'phase_ns={summary["phase_offset_ns"]} '
            f'rate_ppb={summary["period_adjust_ppb"]} '
            f'quality={summary["quality_health_state"]}/'
            f'{summary["quality_lock_quality_tier"]} '
             f"errors={len(summary['errors'])}</text>")
        samples = samples_by_board.get(summary["board"], [])
        if samples:
            x0, plot_w = 500.0, 900.0
            max_elapsed = max(duration_s, samples[-1].elapsed_s, 0.001)
            max_seq = max(sample.trigger_sequence for sample in samples) or 1
            points = []
            for sample in samples:
                x = x0 + plot_w * sample.elapsed_s / max_elapsed
                yy = y + 48 - 30 * sample.trigger_sequence / max_seq
                points.append(f'{x:.1f},{yy:.1f}')
            chunks.append(f'<polyline points="{" ".join(points)}" class="line" '
                          f'stroke="#2563eb"/>')
            chunks.append(f'<line x1="{x0}" y1="{y + 48}" x2="{x0 + plot_w}" '
                          f'y2="{y + 48}" class="grid"/>')
    chunks.append('</svg>')
    return "\n".join(chunks) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", action="append", required=True,
                        metavar="NO1=COM3", help="repeat for NO1..NO8; include NO5")
    parser.add_argument("--observer-name", default="NO5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--duration-s", type=float, default=60.0)
    parser.add_argument("--poll-interval-s", type=float, default=1.0)
    parser.add_argument("--expected-interval-ms", type=float, default=1.0)
    parser.add_argument("--interval-tolerance-ms", type=float, default=0.35)
    parser.add_argument("--sequence-skew-tolerance", type=int, default=1,
                        help="maximum latest TDMA sequence skew between ring nodes")
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "out" / "dpll-vdc-monitor")
    parser.add_argument("--waveform-analysis", action="append", type=Path,
                        help="existing SD ring_capture_analysis.json (read-only)")
    parser.add_argument("--fail-on-gate", action="store_true")
    return parser.parse_args()


def run(args: argparse.Namespace) -> dict[str, Any]:
    specs = [parse_board_arg(value) for value in args.board]
    if len({spec.name for spec in specs}) != len(specs):
        raise ValueError("duplicate board name")
    if args.observer_name.upper() not in {spec.name for spec in specs}:
        raise ValueError(f"observer board {args.observer_name} is not listed")
    if args.duration_s <= 0 or args.poll_interval_s <= 0:
        raise ValueError("duration and poll interval must be positive")
    if args.sequence_skew_tolerance < 0:
        raise ValueError("sequence skew tolerance must be non-negative")
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    samples_by_board: dict[str, list[BoardSample]] = {spec.name: [] for spec in specs}
    started = time.monotonic()
    with open_serial_ports(specs, args) as serials:
        while True:
            elapsed = time.monotonic() - started
            if elapsed >= args.duration_s and any(samples_by_board.values()):
                break
            for spec in specs:
                previous = samples_by_board[spec.name][-1] if samples_by_board[spec.name] else None
                sample = _read_board(serials[spec.name], spec, args.timeout, elapsed, previous)
                samples_by_board[spec.name].append(sample)
            if elapsed >= args.duration_s:
                break
            time.sleep(args.poll_interval_s)

    summaries = [_board_summary(
        samples_by_board[spec.name],
        expected_interval_ms=args.expected_interval_ms,
        interval_tolerance_ms=args.interval_tolerance_ms,
        observer=spec.name == args.observer_name.upper())
                 for spec in specs]
    sequence_consistent, sequence_skew = _ring_sequence_consistency(
        samples_by_board, tolerance=args.sequence_skew_tolerance)
    for summary in summaries:
        summary["ring_sequence_consistent"] = sequence_consistent
        summary["ring_sequence_skew"] = sequence_skew
    waveform: list[dict[str, Any]] = []
    for path in args.waveform_analysis or []:
        try:
            waveform.append({"path": str(path), "analysis": json.loads(
                path.read_text(encoding="utf-8"))})
        except (OSError, json.JSONDecodeError) as exc:
            waveform.append({"path": str(path), "error": str(exc)})
    passed = bool(summaries) and sequence_consistent and all(
        summary["samples"] > 0 and not summary["errors"] and
        summary["vector_valid"] and summary["timestamp_eligible"] and
        summary["trigger_interval_ok"] and
        summary.get("trigger_sequence_monotonic", False) and
        (summary.get("role") == "observer" or
         (summary["ring_up_running"] and summary["ring_down_running"] and
          summary["simultaneous_feedback"]))
        for summary in summaries)
    result = {
        "schema": "HAOFV_DPLL_VDC_MONITOR_V1",
        "passed": passed,
        "observer_board": args.observer_name.upper(),
        "duration_s": args.duration_s,
        "poll_interval_s": args.poll_interval_s,
        "expected_interval_ms": args.expected_interval_ms,
        "interval_tolerance_ms": args.interval_tolerance_ms,
        "sequence_skew_tolerance": args.sequence_skew_tolerance,
        "ring_sequence_consistent": sequence_consistent,
        "ring_sequence_skew": sequence_skew,
        "boards": summaries,
        "waveform_analysis": waveform,
        "read_only": True,
        "commands": [TDMA_STATUS_COMMAND, VDC_STATUS_COMMAND, DPLL_STATUS_COMMAND,
                     READINESS_COMMAND, VDC_VECTOR_COMMAND, DPLL_VECTOR_COMMAND],
    }
    (out_dir / "samples.json").write_text(json.dumps({
        name: [asdict(sample) for sample in samples]
        for name, samples in samples_by_board.items()
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with (out_dir / "samples.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["ts_utc", "elapsed_s", "board", "trigger_sequence",
                         "trigger_interval_ms", "ring_up_running", "ring_down_running",
                         "simultaneous_feedback", "vdc_vector_flags", "dpll_state",
                         "dco_phase_offset_ns", "dco_period_adjust_ppb", "error"])
        for samples in samples_by_board.values():
            for sample in samples:
                writer.writerow([sample.ts_utc, f"{sample.elapsed_s:.6f}", sample.board,
                                 sample.trigger_sequence, sample.trigger_interval_ms,
                                 sample.tdma.get("ring_up_running", 0),
                                 sample.tdma.get("ring_down_running", 0),
                                 int(sample.simultaneous_feedback),
                                 sample.vdc_vector.get("flags", 0),
                                 sample.dpll_vector.get("state", 0),
                                 sample.dpll_vector.get("dco_phase_offset_ns", 0),
                                 sample.dpll_vector.get("dco_period_adjust_ppb", 0),
                                 sample.error])
    (out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out_dir / "summary.svg").write_text(
        _svg(samples_by_board, summaries, duration_s=args.duration_s,
             expected_interval_ms=args.expected_interval_ms,
             sequence_skew_tolerance=args.sequence_skew_tolerance), encoding="utf-8")
    return result


class open_serial_ports:
    """Small context manager keeping all board ports open for one sample round."""

    def __init__(self, specs: list[BoardSpec], args: argparse.Namespace):
        self.specs = specs
        self.args = args
        self.stack = None
        self.serials: dict[str, Any] = {}

    def __enter__(self) -> dict[str, Any]:
        from contextlib import ExitStack
        self.stack = ExitStack()
        for spec in self.specs:
            self.serials[spec.name] = self.stack.enter_context(
                open_serial_port(spec.port, self.args.baud, self.args.timeout,
                                 self.args.settle))
        return self.serials

    def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
        if self.stack is not None:
            self.stack.close()


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (OSError, ValueError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if args.fail_on_gate and not result["passed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
