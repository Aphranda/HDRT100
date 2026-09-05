#!/usr/bin/env python3
"""NO5 DPLL/VDC external observation and SD waveform capture.

The monitor never arms a ring, submits a TDMA frame, or changes a DPLL offset.
Serial queries provide progress only.  NO5 captures the PIO0 raw SMA words to
SD, and offline decoding of those words is the analysis evidence source.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass, field
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
    STORAGE_FILE_READ_MAX_BYTES,
    is_scpi_log_line,
    open_serial_port,
    read_scpi_response,
)
from tools.dpll_waveform_capture.dpll_waveform_capture import (  # noqa: E402
    decode_segments,
    write_reports as write_waveform_reports,
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
SMA_INPUT_COMMAND = "REALtime:IO:INPut:LEVel?"
PHASE_COMMAND = "SYSTem:SYNC:VDC:OBServer:PHASe?"
PHASE_SELFTEST_COMMAND = "SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest"
PHASE_SELFTEST_QUERY = PHASE_SELFTEST_COMMAND + "?"
WAVEFORM_ARM_COMMAND = "SYSTem:SYNC:VDC:OBServer:WAVEform:ARM"
WAVEFORM_STOP_COMMAND = "SYSTem:SYNC:VDC:OBServer:WAVEform:STOP"
WAVEFORM_STATUS_COMMAND = "SYSTem:SYNC:VDC:OBServer:WAVEform:STATus?"
WAVEFORM_SAVE_COMMAND = "SYSTem:SYNC:VDC:OBServer:WAVEform:SAVE"
SMA_LOCK_EXPECTED_MASK = 0x0F

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
PHASE_FIELDS = (
    "enabled", "round_count", "complete_count", "missing_count",
    "ambiguous_count", "last_edge_mask", "last_span_ns", "offset0_ns",
    "offset1_ns", "offset2_ns", "offset3_ns", "initial_span_ns",
    "initial_offset0_ns", "initial_offset1_ns", "initial_offset2_ns",
    "initial_offset3_ns", "peak_span_ns", "min_span_ns",
    "stable_round_count", "stable_streak", "max_stable_streak",
    "stable_jitter_ns", "first_stable_round", "converged",
    "configured_max_span_ns", "configured_min_stable_rounds",
    "last_window_start_lo", "last_window_start_hi", "dropped_word_count",
)
PHASE_SELFTEST_FIELDS = (
    "active", "role", "output_index", "observed_mask",
    "initial_sample_mask", "sample_period_ns", "pulse_period_ns",
    "pulse_high_ns", "pulse_count", "frame_crc32", "schedule_crc32",
    "last_error", "started_ms", "start_delay_ns",
    "first_window_start_lo", "first_window_start_hi",
    "phase_max_span_ns", "phase_min_stable_rounds",
    "scheduled_pulse_count",
)
WAVEFORM_STATUS_FIELDS = (
    "armed", "stopping", "complete", "session_id", "record_count",
    "dropped_count", "source_dropped_count", "segment_count",
    "pending_record_count",
    "first_sample_seq", "last_sample_seq", "start_ms", "end_ms",
    "last_error", "last_job_id",
)

VECTOR_FLAG_VALID = 1 << 0
VECTOR_FLAG_STALE = 1 << 1
VECTOR_FLAG_LOCKED = 1 << 5
VECTOR_FLAG_PROVISIONAL = 1 << 6
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
    sma_input: dict[str, int] = field(default_factory=dict)
    phase_observation: dict[str, int] = field(default_factory=dict)
    phase_selftest: dict[str, int] = field(default_factory=dict)
    waveform_capture: dict[str, Any] = field(default_factory=dict)
    error: str = ""


class ProgressReporter:
    """Publish low-rate Core0 state; waveform evidence remains on NO5 SD."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.sequence = 0
        self.started = time.monotonic()

    def emit(self, event: str, **details: Any) -> None:
        self.sequence += 1
        payload = {
            "schema": "HAOFV_DPLL_MONITOR_PROGRESS_V1",
            "sequence": self.sequence,
            "event": event,
            "elapsed_s": round(time.monotonic() - self.started, 6),
            "updated_at": datetime.now().astimezone().isoformat(),
            "details": details,
            "source": "CORE0_SCPI_READ_ONLY_STATUS",
            "analysis_evidence": "NO5_SD_PIO0_RAW_WAVEFORM",
        }
        self.path.parent.mkdir(parents=True, exist_ok=True)
        pending = self.path.with_suffix(self.path.suffix + ".tmp")
        pending.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        pending.replace(self.path)
        print("DPLL_PROGRESS " + json.dumps(
            payload, ensure_ascii=False, separators=(",", ":")), flush=True)


def _progress_board(sample: BoardSample) -> dict[str, Any]:
    status = sample.phase_selftest
    return {
        "port": sample.port,
        "ok": sample.error == "",
        "error": sample.error,
        "selftest_active": status.get("active", 0),
        "selftest_role": status.get("role", 0),
        "selftest_last_error": status.get("last_error", 0),
        "scheduled_pulse_count": status.get("scheduled_pulse_count", 0),
        "first_window_start_ns": (
            (status.get("first_window_start_hi", 0) << 32) |
            status.get("first_window_start_lo", 0)),
        "phase_round_count": sample.phase_observation.get("round_count", 0),
        "phase_complete_count": sample.phase_observation.get("complete_count", 0),
        "phase_last_span_ns": sample.phase_observation.get("last_span_ns", 0),
        "waveform_record_count": sample.waveform_capture.get("record_count", 0),
        "waveform_dropped_count": sample.waveform_capture.get("dropped_count", 0),
        "waveform_segment_count": sample.waveform_capture.get("segment_count", 0),
        "waveform_last_error": sample.waveform_capture.get("last_error", 0),
        "dpll_update_seq": sample.dpll_status.get("update_seq", 0),
        "dpll_state": sample.dpll_status.get("state", 0),
    }


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


def _effective_phase_pulse_count(args: argparse.Namespace) -> int:
    period_ns = int(args.phase_pulse_period_ns)
    if period_ns <= 0:
        raise ValueError("phase pulse period must be positive")
    coverage_min_s = float(getattr(args, "phase_coverage_min_s", 2.0))
    if coverage_min_s < 0.0:
        raise ValueError("phase coverage minimum must be non-negative")
    coverage_s = float(args.duration_s) + max(
        coverage_min_s, 2.0 * float(args.poll_interval_s))
    duration_count = math.ceil(coverage_s * 1e9 / period_ns)
    count = max(int(args.phase_pulse_count), duration_count)
    if count <= 0 or count > 0xFFFFFFFF:
        raise ValueError("phase pulse count exceeds uint32 range")
    return count


def _arm_phase_observation(serials: dict[str, Any], specs: list[BoardSpec],
                           args: argparse.Namespace,
                           *, arm_observer: bool = True) -> None:
    """Arm the external pulse evidence persona on all five boards.

    The observer is armed first.  Ring nodes then schedule OUT1 pulses from
    their local VDC timebase; none of these commands changes the TDMA persona.
    """
    period = int(args.phase_pulse_period_ns)
    high = int(args.phase_pulse_high_ns)
    count = _effective_phase_pulse_count(args)
    delay = int(args.phase_start_delay_ns)
    if period <= high or high <= 0 or count <= 0 or delay < 0:
        raise ValueError("invalid external phase pulse configuration")

    observer_name = args.observer_name.upper()

    def arm(spec: BoardSpec, role: int) -> None:
        command = (f"{PHASE_SELFTEST_COMMAND} {role},0,15,0,"
                   f"{int(args.phase_sample_period_ns)},{period},{high},"
                   f"{count},0,{delay},1,{int(args.phase_max_span_ns)},"
                   f"{int(args.phase_min_complete_rounds)}")
        response = _query(serials[spec.name], command, args.timeout)
        if response.strip().strip('"') not in ("1", "1.0"):
            raise ValueError(f"{spec.name}: phase observation arm rejected: {response}")

    observer = next(spec for spec in specs if spec.name == observer_name)
    if arm_observer:
        arm(observer, 2)
    transmitters = [spec for spec in specs if spec.name != observer_name]
    # The four serial links are independent.  Issuing TX arms concurrently
    # keeps host command ordering out of the measured initial phase.
    with ThreadPoolExecutor(max_workers=len(transmitters)) as pool:
        list(pool.map(lambda spec: arm(spec, 1), transmitters))


def _stop_phase_observation(serials: dict[str, Any], specs: list[BoardSpec],
                            args: argparse.Namespace) -> None:
    command = (f"{PHASE_SELFTEST_COMMAND} 0,0,15,0,"
               f"{int(args.phase_sample_period_ns)},"
               f"{int(args.phase_pulse_period_ns)},"
               f"{int(args.phase_pulse_high_ns)},"
               f"{_effective_phase_pulse_count(args)},0,0,1,"
               f"{int(args.phase_max_span_ns)},"
               f"{int(args.phase_min_complete_rounds)}")

    def stop(spec: BoardSpec) -> None:
        response = _query(serials[spec.name], command, args.timeout)
        if response.strip().strip('"') not in ("1", "1.0"):
            raise ValueError(f"{spec.name}: phase observation stop rejected: {response}")

    with ThreadPoolExecutor(max_workers=len(specs)) as pool:
        list(pool.map(stop, specs))


def _parse_waveform_status(response: str) -> dict[str, Any]:
    fields = [field.strip().strip('"')
              for field in next(csv.reader([response]), [])]
    if len(fields) != len(WAVEFORM_STATUS_FIELDS) + 1:
        raise ValueError(f"invalid waveform status: {response!r}")
    values: list[int] = []
    for field in fields[:-1]:
        if field.upper() == "TRUE":
            values.append(1)
        elif field.upper() == "FALSE":
            values.append(0)
        else:
            values.append(int(field, 0))
    status: dict[str, Any] = dict(zip(
        WAVEFORM_STATUS_FIELDS, values, strict=True))
    status["last_path"] = fields[-1]
    return status


def _parse_storage_read(response: str, expected_offset: int) -> dict[str, Any]:
    values = [value.strip().strip('"')
              for value in next(csv.reader([response]), [])]
    if len(values) != 10 or values[0].upper() != "OK":
        raise ValueError(f"storage read rejected: {response!r}")
    offset = int(values[2], 0)
    requested = int(values[3], 0)
    returned = int(values[4], 0)
    error = int(values[8], 0)
    payload = bytes.fromhex(values[9])
    if (offset != expected_offset or error != 0 or
            len(payload) != returned or returned > requested):
        raise ValueError(f"storage read mismatch: {response!r}")
    return {
        "file_size": int(values[5], 0),
        "eof": int(values[6], 0) != 0,
        "payload": payload,
    }


def _download_waveform_segment(ser: Any, path: str,
                               timeout_s: float) -> bytes:
    data = bytearray()
    file_size: int | None = None
    while file_size is None or len(data) < file_size:
        requested = (STORAGE_FILE_READ_MAX_BYTES if file_size is None else
                     min(STORAGE_FILE_READ_MAX_BYTES, file_size - len(data)))
        response = _query(
            ser, f'SYSTem:STORage:FILE:READ? "{path}",'
                 f'{len(data)},{requested}', timeout_s)
        page = _parse_storage_read(response, len(data))
        if file_size is None:
            file_size = page["file_size"]
        elif page["file_size"] != file_size:
            raise ValueError(f"waveform file changed during download: {path}")
        payload = page["payload"]
        if not payload and not page["eof"]:
            raise ValueError(f"waveform download made no progress: {path}")
        data.extend(payload)
        if page["eof"]:
            break
    if file_size is None or len(data) != file_size:
        raise ValueError(f"incomplete waveform download: {path}")
    return bytes(data)


def _finish_waveform_capture(ser: Any, args: argparse.Namespace,
                             progress: ProgressReporter) -> dict[str, Any]:
    response = _query(ser, WAVEFORM_STOP_COMMAND, args.timeout)
    if not response.lstrip().lstrip('"').upper().startswith("OK"):
        raise ValueError(f"waveform stop rejected: {response!r}")
    deadline = time.monotonic() + args.waveform_flush_timeout_s
    status: dict[str, Any] = {}
    while time.monotonic() < deadline:
        status = _parse_waveform_status(
            _query(ser, WAVEFORM_STATUS_COMMAND, args.timeout))
        progress.emit("waveform_flushing", waveform=status)
        if status["complete"]:
            break
        time.sleep(0.05)
    if not status.get("complete"):
        raise TimeoutError(f"NO5 waveform flush timeout: {status}")
    if status["record_count"] == 0 or status["segment_count"] == 0:
        progress.emit("waveform_empty", waveform=status)
        return {
            "status": status,
            "sd_paths": [],
            "analysis": {},
            "raw_gate": {
                "passed": False,
                "errors": ["no_raw_waveform_records"],
                "phase_round_count": 0,
                "source_dropped_count": 0,
                "initial_span_ns": None,
                "final_span_ns": None,
                "stable_streak": 0,
                "max_span_ns": args.phase_max_span_ns,
                "min_stable_rounds": args.phase_min_complete_rounds,
            },
        }
    if status["last_error"]:
        raise RuntimeError(f"NO5 waveform capture is incomplete: {status}")
    save = [field.strip().strip('"') for field in next(csv.reader([
        _query(ser, WAVEFORM_SAVE_COMMAND, args.timeout)]), [])]
    if len(save) != 3 or save[0].upper() != "OK":
        raise ValueError(f"waveform save rejected: {save!r}")
    segment_count, prefix = int(save[1], 0), save[2]
    segment_dir = args.out_dir / "waveform" / "segments"
    segment_dir.mkdir(parents=True, exist_ok=True)
    local_paths = []
    sd_paths = []
    for index in range(segment_count):
        sd_path = f"{prefix}{index:04d}.bin"
        local_path = segment_dir / Path(sd_path).name
        local_path.write_bytes(
            _download_waveform_segment(ser, sd_path, args.timeout))
        local_paths.append(local_path)
        sd_paths.append(sd_path)
        progress.emit("waveform_download", segment=index + 1,
                      segment_count=segment_count, sd_path=sd_path)
    decoded = decode_segments(
        local_paths, pulse_period_ns=args.phase_pulse_period_ns)
    analysis = write_waveform_reports(
        decoded, args.out_dir / "waveform" / "analysis")
    span_trend = decoded.get("phase_span_trend", [])
    stable_streak = 0
    for row in reversed(span_trend):
        if row["span_ns"] > args.phase_max_span_ns:
            break
        stable_streak += 1
    raw_gate_errors = []
    if status["dropped_count"] != 0:
        raw_gate_errors.append("capture_dropped_records")
    if status["source_dropped_count"] != 0:
        raw_gate_errors.append("source_dma_or_latch_dropped_records")
    if decoded["dropped_count"] != 0:
        raw_gate_errors.append("segment_dropped_records")
    if decoded["source_dropped_count"] != 0:
        raw_gate_errors.append("source_dropped_records")
    if stable_streak < args.phase_min_complete_rounds:
        raw_gate_errors.append("insufficient_stable_circular_span_windows")
    raw_gate = {
        "passed": not raw_gate_errors,
        "errors": raw_gate_errors,
        "phase_round_count": decoded["phase_round_count"],
        "circular_span_window_count": len(span_trend),
        "capture_dropped_count": status["dropped_count"],
        "source_capture_dropped_count": status["source_dropped_count"],
        "segment_dropped_count": decoded["dropped_count"],
        "source_dropped_count": decoded["source_dropped_count"],
        "initial_span_ns": (decoded["phase"][0]["span_ns"]
                            if decoded["phase"] else None),
        "final_span_ns": (decoded["phase"][-1]["span_ns"]
                            if decoded["phase"] else None),
        "circular_span_initial_ns": (
            span_trend[0]["span_ns"] if span_trend else None),
        "circular_span_final_ns": (
            span_trend[-1]["span_ns"] if span_trend else None),
        "circular_span_convergence": decoded.get("convergence", {}).get(
            "four_node_span", {}).get("convergence", {}),
        "stable_streak": stable_streak,
        "max_span_ns": args.phase_max_span_ns,
        "min_stable_rounds": args.phase_min_complete_rounds,
    }
    return {"status": status, "sd_paths": sd_paths,
            "analysis": analysis, "raw_gate": raw_gate}


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
        phase_selftest = parse_named_int_response(
            _query(ser, PHASE_SELFTEST_QUERY, timeout_s), PHASE_SELFTEST_FIELDS)
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
            phase_selftest=phase_selftest,
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


def _read_observer(ser: Any, spec: BoardSpec, timeout_s: float,
                   elapsed_s: float) -> BoardSample:
    try:
        sma_input = parse_named_int_response(
            _query(ser, SMA_INPUT_COMMAND, timeout_s),
            ("base_pin", "pin_count", "level_mask"))
        if sma_input["pin_count"] < 4:
            raise ValueError(
                f"observer exposes only {sma_input['pin_count']} SMA inputs")
        phase = parse_named_int_response(
            _query(ser, PHASE_COMMAND, timeout_s), PHASE_FIELDS)
        phase_selftest = parse_named_int_response(
            _query(ser, PHASE_SELFTEST_QUERY, timeout_s), PHASE_SELFTEST_FIELDS)
        waveform = _parse_waveform_status(
            _query(ser, WAVEFORM_STATUS_COMMAND, timeout_s))
        return BoardSample(
            ts_utc=datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            elapsed_s=elapsed_s,
            board=spec.name,
            port=spec.port,
            tdma={}, vdc_status={}, dpll_status={}, readiness={},
            vdc_vector={}, dpll_vector={}, trigger_sequence=0,
            trigger_interval_ms=None, simultaneous_feedback=False,
            sma_input=sma_input,
            phase_observation=phase,
            phase_selftest=phase_selftest,
            waveform_capture=waveform,
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
                   observer: bool = False,
                   phase_max_span_ns: int = 500,
                   phase_min_complete_rounds: int = 3) -> dict[str, Any]:
    errors = [sample.error for sample in samples if sample.error]
    intervals = [sample.trigger_interval_ms for sample in samples
                 if sample.trigger_interval_ms is not None]
    latest = samples[-1] if samples else None
    if observer:
        input_masks = [sample.sma_input.get("level_mask", 0) &
                       SMA_LOCK_EXPECTED_MASK for sample in samples
                       if not sample.error and sample.sma_input]
        phase = latest.phase_observation if latest else {}
        phase_ok = bool(
            phase and phase.get("enabled", 0) and
            phase.get("complete_count", 0) >= phase_min_complete_rounds and
            phase.get("last_edge_mask", 0) == SMA_LOCK_EXPECTED_MASK and
            phase.get("configured_max_span_ns", 0) == phase_max_span_ns and
            phase.get("configured_min_stable_rounds", 0) ==
            phase_min_complete_rounds and
            phase.get("stable_streak", 0) >= phase_min_complete_rounds and
            phase.get("converged", 0) and
            phase.get("missing_count", 0) == 0 and
            phase.get("ambiguous_count", 0) == 0)
        return {
            "board": latest.board if latest else "",
            "role": "observer",
            "samples": len(samples),
            "errors": errors,
            "ring_up_running": False,
            "ring_down_running": False,
            "simultaneous_feedback": False,
            "reference_node": False,
            "trigger_sequence": 0,
            "trigger_sequence_monotonic": False,
            "trigger_intervals_ms": [],
            "trigger_interval_ok": False,
            "vector_valid": False,
            "provisional": False,
            "timestamp_eligible": False,
            "dpll_state": 0,
            "dpll_locked": False,
            "formal_locked": False,
            "quality_health_state": 0,
            "quality_lock_quality_tier": 0,
            "phase_offset_ns": 0,
            "period_adjust_ppb": 0,
            "last_reject_code": 0,
            "sma_input_masks": input_masks,
            "sma_expected_mask": SMA_LOCK_EXPECTED_MASK,
            "sma_all_locked": bool(input_masks and
                                   input_masks[-1] == SMA_LOCK_EXPECTED_MASK),
            "phase_observation": phase,
            "phase_gate_passed": phase_ok,
            "phase_max_span_ns": phase_max_span_ns,
            "phase_min_complete_rounds": phase_min_complete_rounds,
            "phase_initial_span_ns": phase.get("initial_span_ns", 0),
            "phase_final_span_ns": phase.get("last_span_ns", 0),
            "phase_peak_span_ns": phase.get("peak_span_ns", 0),
            "phase_stable_jitter_ns": phase.get("stable_jitter_ns", 0),
        }
    interval_ok = bool(intervals) and all(
        abs(interval - expected_interval_ms) <= interval_tolerance_ms
        for interval in intervals)
    sequence_monotonic = _sequence_is_monotonic(samples)
    vector_ok = bool(
        latest and latest.vdc_vector and latest.dpll_vector and
        (latest.vdc_vector.get("flags", 0) & VECTOR_FLAG_VALID) and
        not (latest.vdc_vector.get("flags", 0) & VECTOR_FLAG_STALE) and
        (latest.dpll_vector.get("flags", 0) & VECTOR_FLAG_VALID) and
        not (latest.dpll_vector.get("flags", 0) & VECTOR_FLAG_STALE) and
        latest.vdc_vector.get("gate_passed", 0) and
        latest.dpll_vector.get("gate_passed", 0))
    timestamp_ok = bool(latest and latest.readiness and
                        latest.readiness.get("timestamp_source") ==
                        TIMESTAMP_SOURCE_HARDWARE_TICK and
                        latest.readiness.get("timestamp_resolution_ns", 0) > 0 and
                        latest.readiness.get("timestamp_eligible") and
                        not (latest.readiness.get("timestamp_flags", 0) &
                             TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) and
                        (latest.readiness.get("timestamp_flags", 0) &
                         TIMESTAMP_FLAG_DPLL_ELIGIBLE))
    provisional = bool(latest and latest.dpll_vector.get("flags", 0) &
                       VECTOR_FLAG_PROVISIONAL)
    formal_locked = bool(latest and latest.dpll_vector.get("flags", 0) &
                         VECTOR_FLAG_LOCKED)
    servo_locked = bool(latest and
                        latest.dpll_vector.get("state", 0) == 5)
    # The reference node is the only in-ring node expected to expose the
    # complete TX->RX feedback proof.  Followers prove their own correlated
    # observation; requiring ``simultaneous_feedback`` on every follower would
    # incorrectly mark a healthy ring as failed.
    reference_node = bool(latest and latest.tdma and
                          latest.tdma.get("ring_local_slot_id", -1) ==
                          latest.tdma.get("ring_reference_slot_id", -2))
    return {
        "board": latest.board if latest else "",
        "role": "observer" if observer else "ring_node",
        "samples": len(samples),
        "errors": errors,
        "ring_up_running": bool(latest and latest.tdma.get("ring_up_running")),
        "ring_down_running": bool(latest and latest.tdma.get("ring_down_running")),
        "simultaneous_feedback": bool(latest and latest.simultaneous_feedback),
        "reference_node": reference_node,
        "trigger_sequence": latest.trigger_sequence if latest else 0,
        "trigger_sequence_monotonic": sequence_monotonic,
        "trigger_intervals_ms": intervals,
        "trigger_interval_ok": interval_ok,
        "vector_valid": vector_ok,
        "provisional": provisional,
        "timestamp_eligible": timestamp_ok,
        "dpll_state": latest.dpll_vector.get("state", 0) if latest else 0,
        "dpll_locked": servo_locked if provisional else formal_locked,
        "formal_locked": formal_locked,
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
        "sma_input_masks": [],
        "sma_expected_mask": SMA_LOCK_EXPECTED_MASK,
            "sma_all_locked": False,
            "phase_observation": {},
            "phase_gate_passed": False,
        }


def _ring_sequence_consistency(samples_by_board: dict[str, list[BoardSample]],
                               *, tolerance: int) -> tuple[bool, int | None]:
    """Compare only counters that share one wire-identity domain.

    A follower's ``ring_clock_observation_sequence`` is the sequence carried
    by the reference frame.  The reference node itself has no such
    observation (it originates the frame), so mixing it with its local service
    counter produces a false several-hundred-thousand-frame skew.  Prefer the
    correlated observation domain whenever at least two participants expose
    it; use legacy local ring counters only when no observation is available.
    The out-of-ring NO5 observer is never included.
    """
    ring_samples = [samples[-1] for name, samples in samples_by_board.items()
                    if samples and name.upper() != "NO5" and
                    samples[-1].tdma.get("ring_enabled", 0)]
    observed_values = [
        int(sample.tdma.get("ring_clock_observation_sequence", 0))
        for sample in ring_samples
        if sample.tdma.get("ring_clock_observation_valid", 0) and
        sample.tdma.get("ring_clock_observation_sequence", 0) > 0
    ]
    values = observed_values if len(observed_values) >= 2 else [
        value for sample in ring_samples
        for value in [_ring_sequence(sample)] if value is not None
    ]
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
        status = (summary.get("phase_gate_passed", False) if observer else
                  (summary["vector_valid"] and summary["timestamp_eligible"] and
                   summary["trigger_interval_ok"] and
                   summary.get("trigger_sequence_monotonic", False) and
                   summary.get("dpll_locked", False) and
                   summary.get("ring_up_running") and
                   summary.get("ring_down_running") and
                   (not summary.get("reference_node") or
                    summary["simultaneous_feedback"])))
        color = "ok" if status else "bad"
        if observer:
            phase = summary.get("phase_observation", {})
            chunks.append(
                f'<text x="24" y="{y}" class="label">{escape(summary["board"])} '
                f'role=observer phase mask=0x{phase.get("last_edge_mask", 0):X} '
                f'span={phase.get("last_span_ns", 0)}ns '
                f'complete={phase.get("complete_count", 0)}</text>')
        else:
            chunks.append(
                f'<text x="24" y="{y}" class="label">{escape(summary["board"])} '
                f'role=ring_node up={int(summary["ring_up_running"])} '
                f'down={int(summary["ring_down_running"])} '
                f'simultaneous={int(summary["simultaneous_feedback"])} '
                f'vector={int(summary["vector_valid"])} '
                f'timestamp={int(summary["timestamp_eligible"])} '
                f'reference={int(summary.get("reference_node", False))} '
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
            if observer:
                values = [abs(sample.phase_observation.get(
                    f"offset{channel}_ns", 0))
                    for sample in samples for channel in range(4)]
                max_seq = max(values) if values else 1
                max_seq = max(max_seq, 1)
            else:
                max_seq = max(sample.trigger_sequence for sample in samples) or 1
            points = []
            for sample in samples:
                x = x0 + plot_w * sample.elapsed_s / max_elapsed
                value = (sample.phase_observation.get("last_span_ns", 0)
                         if observer else sample.trigger_sequence)
                yy = y + 48 - 30 * value / max_seq
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
    parser.add_argument("--phase-sample-period-ns", type=int, default=500)
    parser.add_argument("--phase-pulse-period-ns", type=int, default=1000000)
    parser.add_argument("--phase-pulse-high-ns", type=int, default=2000)
    parser.add_argument(
        "--phase-pulse-count", type=int, default=4096,
        help="minimum pulse count; automatically extended to cover duration")
    parser.add_argument("--phase-start-delay-ns", type=int, default=1000000000)
    parser.add_argument(
        "--phase-coverage-min-s", type=float, default=2.0,
        help="minimum pulse coverage beyond duration; 2 s for full runs")
    parser.add_argument("--phase-max-span-ns", type=int, default=500)
    parser.add_argument("--phase-min-complete-rounds", type=int, default=3)
    parser.add_argument("--waveform-flush-timeout-s", type=float, default=30.0)
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
    if (args.phase_max_span_ns <= 0 or args.phase_min_complete_rounds <= 0 or
            args.phase_coverage_min_s < 0):
        raise ValueError("phase convergence thresholds must be positive")
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    progress = ProgressReporter(out_dir / "progress.json")
    samples_by_board: dict[str, list[BoardSample]] = {spec.name: [] for spec in specs}
    started = time.monotonic()
    waveform_result: dict[str, Any] = {}
    progress.emit("opening_ports", boards={spec.name: spec.port for spec in specs})
    with open_serial_ports(specs, args) as serials:
        observer_serial = serials[args.observer_name.upper()]
        waveform_armed = False
        try:
            response = _query(observer_serial, WAVEFORM_ARM_COMMAND, args.timeout)
            if not response.lstrip().lstrip('"').upper().startswith("OK"):
                raise ValueError(f"NO5 waveform arm rejected: {response!r}")
            waveform_armed = True
            progress.emit("waveform_armed", response=response)
            _arm_phase_observation(serials, specs, args)
            progress.emit("phase_observation_armed")
            poll_index = 0
            while True:
                elapsed = time.monotonic() - started
                if elapsed >= args.duration_s and any(samples_by_board.values()):
                    break

                def read_spec(spec: BoardSpec) -> BoardSample:
                    if spec.name == args.observer_name.upper():
                        return _read_observer(
                            serials[spec.name], spec, args.timeout, elapsed)
                    previous = (samples_by_board[spec.name][-1]
                                if samples_by_board[spec.name] else None)
                    return _read_board(
                        serials[spec.name], spec, args.timeout, elapsed, previous)

                # Board serial sessions are independent.  Read all Nodes in one
                # observation round concurrently so poll cadence is not multiplied
                # by the number of boards.
                with ThreadPoolExecutor(max_workers=len(specs)) as pool:
                    samples = list(pool.map(read_spec, specs))
                for spec, sample in zip(specs, samples, strict=True):
                    samples_by_board[spec.name].append(sample)
                poll_index += 1
                progress.emit(
                    "observing",
                    poll_index=poll_index,
                    boards={sample.board: _progress_board(sample)
                            for sample in samples})
                if elapsed >= args.duration_s:
                    break
                time.sleep(args.poll_interval_s)
        finally:
            try:
                _stop_phase_observation(serials, specs, args)
                progress.emit("phase_observation_stopped")
            finally:
                if waveform_armed:
                    try:
                        waveform_result = _finish_waveform_capture(
                            observer_serial, args, progress)
                    except (OSError, ValueError, TimeoutError,
                            RuntimeError) as exc:
                        waveform_result = {
                            "status": {},
                            "sd_paths": [],
                            "analysis": {},
                            "error": str(exc),
                            "raw_gate": {
                                "passed": False,
                                "errors": ["waveform_capture_failed"],
                                "detail": str(exc),
                            },
                        }
                        progress.emit(
                            "waveform_failed", error=str(exc))

    summaries = [_board_summary(
        samples_by_board[spec.name],
        expected_interval_ms=args.expected_interval_ms,
        interval_tolerance_ms=args.interval_tolerance_ms,
        observer=spec.name == args.observer_name.upper(),
        phase_max_span_ns=args.phase_max_span_ns,
        phase_min_complete_rounds=args.phase_min_complete_rounds)
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
    observer_summary = next((summary for summary in summaries
                             if summary.get("role") == "observer"), None)
    raw_gate_passed = bool(waveform_result.get("raw_gate", {}).get("passed"))
    passed = bool(summaries) and sequence_consistent and bool(observer_summary) and \
        raw_gate_passed and all(
        summary["samples"] > 0 and not summary["errors"] and
        (True
         if summary.get("role") == "observer" else
         (summary["ring_up_running"] and summary["ring_down_running"] and
          (not summary.get("reference_node", False) or
           summary["simultaneous_feedback"])))
        for summary in summaries)
    result = {
        "schema": "HAOFV_DPLL_VDC_MONITOR_V2",
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
        "sd_waveform": waveform_result,
        "serial_status_role": "PROGRESS_ONLY",
        "analysis_evidence": "NO5_SD_PIO0_RAW_WAVEFORM",
        "observer_transport": "SMA_SYNC_PULSES_PIO0",
        "phase_sample_period_ns": args.phase_sample_period_ns,
        "phase_pulse_period_ns": args.phase_pulse_period_ns,
        "phase_pulse_high_ns": args.phase_pulse_high_ns,
        "phase_pulse_count": _effective_phase_pulse_count(args),
        "phase_pulse_count_minimum": args.phase_pulse_count,
        "phase_coverage_min_s": args.phase_coverage_min_s,
        "phase_max_span_ns": args.phase_max_span_ns,
        "phase_min_complete_rounds": args.phase_min_complete_rounds,
        "phase_gate_semantics": "INITIAL_RECORDED_FINAL_STABLE_STREAK_GATED",
        "commands": [TDMA_STATUS_COMMAND, VDC_STATUS_COMMAND, DPLL_STATUS_COMMAND,
                     READINESS_COMMAND, VDC_VECTOR_COMMAND, DPLL_VECTOR_COMMAND,
                     SMA_INPUT_COMMAND, PHASE_COMMAND, PHASE_SELFTEST_COMMAND,
                     PHASE_SELFTEST_QUERY, WAVEFORM_ARM_COMMAND,
                     WAVEFORM_STOP_COMMAND, WAVEFORM_STATUS_COMMAND,
                     WAVEFORM_SAVE_COMMAND],
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
                         "dco_phase_offset_ns", "dco_period_adjust_ppb",
                         "phase_round_count", "phase_complete_count",
                         "phase_initial_span_ns", "phase_span_ns",
                         "phase_peak_span_ns", "phase_stable_streak",
                         "phase_stable_jitter_ns", "phase_converged",
                         "phase_offset_no1_ns",
                         "phase_offset_no2_ns", "phase_offset_no3_ns",
                         "phase_offset_no4_ns", "error"])
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
                                 sample.phase_observation.get("round_count", 0),
                                 sample.phase_observation.get("complete_count", 0),
                                 sample.phase_observation.get("initial_span_ns", 0),
                                 sample.phase_observation.get("last_span_ns", 0),
                                 sample.phase_observation.get("peak_span_ns", 0),
                                 sample.phase_observation.get("stable_streak", 0),
                                 sample.phase_observation.get("stable_jitter_ns", 0),
                                 sample.phase_observation.get("converged", 0),
                                 sample.phase_observation.get("offset0_ns", 0),
                                 sample.phase_observation.get("offset1_ns", 0),
                                 sample.phase_observation.get("offset2_ns", 0),
                                 sample.phase_observation.get("offset3_ns", 0),
                                 sample.error])
    (out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out_dir / "summary.svg").write_text(
        _svg(samples_by_board, summaries, duration_s=args.duration_s,
             expected_interval_ms=args.expected_interval_ms,
             sequence_skew_tolerance=args.sequence_skew_tolerance), encoding="utf-8")
    progress.emit("complete", passed=passed,
                  summary=str(out_dir / "summary.json"))
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
