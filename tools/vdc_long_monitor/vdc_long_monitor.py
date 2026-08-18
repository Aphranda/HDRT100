#!/usr/bin/env python3
"""Run repeated VDC TDMA observation self-tests and monitor lock stability."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import statistics
import sys
import time
from contextlib import ExitStack
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import (  # noqa: E402
    open_serial_port,
    read_serial_line_idle,
)


LOCK_READINESS_FIELD_COUNT = 24
OBSERVER_FIELD_COUNT = 40
SYNC_QUALITY_FIELD_COUNT = 17
MODEL_PULSE_FIELD_COUNT = 10
SAMPLE_WINDOW_FIELD_COUNT = 13
DCO_FIELD_COUNT = 20
SELFTEST_FIELD_COUNT = 16

TIMESTAMP_SOURCE_HARDWARE_TICK = 2
TIMESTAMP_FLAG_DIAGNOSTIC_ONLY = 0x00000001
TIMESTAMP_FLAG_DPLL_ELIGIBLE = 0x00000002
VDC_GATE_PASS = 0
QUALITY_SVG_NAME = "lock_quality.svg"
SUMMARY_SVG_NAME = "lock_quality_summary.svg"
PLOT_ARCHIVE_ROOT = ROOT / "docs" / "temp" / "vdc_long_monitor"


@dataclass(frozen=True)
class Direction:
    source_name: str
    source_port: str
    target_name: str
    target_port: str


@dataclass
class WindowSummary:
    index: int
    direction: str
    started_monotonic_s: float
    elapsed_s: float
    accepted_delta: int
    observer_accepted_delta: int
    rejected_delta: int
    observer_rejected_delta: int
    submitted_delta: int
    model_completed: int
    model_total: int
    final_gate: int
    final_timestamp_source: int
    final_timestamp_resolution_ns: int
    final_timestamp_flags: int
    final_lock_state: int
    final_locked: int
    final_health: int
    final_lock_quality_tier: int
    final_last_offset_ns: int
    final_rms_offset_ns: int
    final_max_abs_offset_ns: int
    final_freq_offset_ppb: int
    final_jitter_pk_ns: int
    final_dco_phase_offset_ns: int
    final_dco_period_adjust_ppb: int
    ok: bool
    reason: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-x", required=True)
    parser.add_argument("--port-y", required=True)
    parser.add_argument("--name-x", default="COM5")
    parser.add_argument("--name-y", default="COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--duration-s", type=float, default=300.0)
    parser.add_argument("--poll-interval-s", type=float, default=1.0)
    parser.add_argument("--window-timeout-s", type=float, default=8.0)
    parser.add_argument("--sample-period-ns", type=int, default=100)
    parser.add_argument("--pulse-period-ns", type=int, default=1000000)
    parser.add_argument("--pulse-high-ns", type=int, default=1000)
    parser.add_argument("--pulse-count", type=int, default=4096)
    parser.add_argument("--start-delay-ns", type=int, default=1000000000)
    parser.add_argument("--max-resolution-ns", type=int, default=100)
    parser.add_argument("--output-index", type=int, default=2)
    parser.add_argument("--observed-mask", type=int, default=4)
    parser.add_argument("--expected-build")
    parser.add_argument("--reverse", action="store_true",
                        help="alternate X->Y and Y->X windows")
    parser.add_argument("--allow-zero-accepted-window", action="store_true",
                        help="record a failed window but keep monitoring")
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--plot-archive-dir", type=Path, default=PLOT_ARCHIVE_ROOT,
                        help="mirror generated SVG plots into a stable docs/temp archive")
    parser.add_argument("--no-plot-archive", action="store_true",
                        help="do not mirror generated SVG plots into docs/temp")
    return parser.parse_args()


def query(ser, command: str, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    while time.monotonic() < deadline:
        line = read_serial_line_idle(ser, deadline)
        if line is None or is_log_line(line):
            continue
        line = strip_leading_ack(trim_embedded_log(line))
        if line:
            return line
    return "<timeout>"


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def trim_embedded_log(line: str) -> str:
    match = re.search(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+', line)
    return line[:match.start()].strip() if match else line


def strip_leading_ack(line: str) -> str:
    if line in ('"OK"', "OK"):
        return line
    if line.startswith('"OK[') or line.startswith("OK["):
        return ""
    if line.startswith('"OK"['):
        return line[4:].strip()
    if line.startswith('OK"['):
        return line[3:].strip()
    return line


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def int_fields(response: str, expected_count: int, *, allow_status: str | None = None) -> list[int]:
    fields = parse_csv_response(response)
    if allow_status is not None:
        if len(fields) < 1 or fields[0].strip().strip('"') != allow_status:
            raise AssertionError(f"expected status {allow_status}: {response}")
        fields = fields[1:]
        expected_count -= 1
    if len(fields) != expected_count:
        raise AssertionError(f"field count {len(fields)} != {expected_count}: {response}")
    values: list[int] = []
    for field in fields:
        text = field.strip().strip('"')
        if text.upper() == "TRUE":
            values.append(1)
        elif text.upper() == "FALSE":
            values.append(0)
        else:
            values.append(int(text, 0))
    return values


def sync_quality(response: str) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != SYNC_QUALITY_FIELD_COUNT:
        raise AssertionError(f"malformed sync quality: {response}")
    return [int(field.strip().strip('"'), 0) for field in fields[1:]]


def split_direction_label(direction: str) -> tuple[str, str] | None:
    if "->" not in direction:
        return None
    source, target = direction.split("->", 1)
    source = source.strip()
    target = target.strip()
    if not source or not target:
        return None
    return source, target


def loop_label_from_names(names: list[str]) -> str:
    unique_names: list[str] = []
    for name in names:
        if name and name not in unique_names:
            unique_names.append(name)
    if len(unique_names) >= 2:
        return f"TDMA feedback loop {unique_names[0]}<->{unique_names[1]}"
    if unique_names:
        return unique_names[0]
    return "single-loop"


def loop_label_from_direction(direction: str) -> str:
    parts = split_direction_label(direction)
    if parts is None:
        return direction
    return f"TDMA feedback loop {parts[0]}<->{parts[1]}"


def stimulus_observer_label(direction: str) -> str:
    parts = split_direction_label(direction)
    if parts is None:
        return direction
    source, target = parts
    return f"TDMA leg {source}->{target}"


def tdma_leg_detail(direction: str) -> str:
    parts = split_direction_label(direction)
    if parts is None:
        return direction
    source, target = parts
    return f"{source} UP -> {target} DOWN"


def tdma_observed_legs(windows: list[WindowSummary]) -> list[str]:
    legs: list[str] = []
    for window in windows:
        if split_direction_label(window.direction) is None:
            continue
        if window.direction not in legs:
            legs.append(window.direction)
    return legs


def has_tdma_feedback_loop(windows: list[WindowSummary]) -> bool:
    directed = {
        parts
        for window in windows
        if (parts := split_direction_label(window.direction)) is not None
    }
    return any((target, source) in directed for source, target in directed)


def timestamp_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def release_runtime(ser, timeout_s: float) -> None:
    for command in (
        "SYST:SYNC:VDC:OBServer 0",
        "REALtime:IO:SAMPle:STATe 0",
        "REALtime:IO:MODel:OUTPut:MASK 0,0",
        "REALtime:IO:MODel:OUTPut:RELease",
    ):
        _ = query(ser, command, timeout_s)


def selftest_command(role: int, args: argparse.Namespace) -> str:
    values = (
        role,
        args.output_index,
        args.observed_mask,
        0,
        args.sample_period_ns,
        args.pulse_period_ns,
        args.pulse_high_ns,
        args.pulse_count,
        0,
        args.start_delay_ns,
    )
    return "SYST:SYNC:VDC:OBServer:TDMA:SELFtest " + ",".join(str(v) for v in values)


def snapshot_board(ser, timeout_s: float) -> dict[str, Any]:
    quality = sync_quality(query(ser, "READ:SYNC:QUALity?", timeout_s))
    readiness = int_fields(query(ser, "SYST:SYNC:VDC:LOCK:READiness?", timeout_s),
                           LOCK_READINESS_FIELD_COUNT)
    observer = int_fields(query(ser, "SYST:SYNC:VDC:OBServer?", timeout_s),
                          OBSERVER_FIELD_COUNT)
    window = int_fields(query(ser, "REALtime:IO:SAMPle:WINDow?", timeout_s),
                        SAMPLE_WINDOW_FIELD_COUNT)
    dco = int_fields(query(ser, "SYST:SYNC:VDC:DCO?", timeout_s),
                     DCO_FIELD_COUNT)
    path_delay = int_fields(query(ser, "SYST:SYNC:VDC:PATH:DELay?", timeout_s),
                            17,
                            allow_status="OK")
    return {
        "quality": quality,
        "readiness": readiness,
        "observer": observer,
        "window": window,
        "dco": dco,
        "path_delay": path_delay,
    }


def model_snapshot(ser, timeout_s: float) -> list[int]:
    return int_fields(query(ser, "REALtime:IO:MODel:PULSe:SCHEDule?", timeout_s),
                      MODEL_PULSE_FIELD_COUNT)


def selftest_snapshot(ser, timeout_s: float) -> list[int]:
    return int_fields(query(ser, "SYST:SYNC:VDC:OBServer:TDMA:SELFtest?", timeout_s),
                      SELFTEST_FIELD_COUNT)


def emit_jsonl(handle, event: str, payload: dict[str, Any]) -> None:
    row = {"ts_utc": timestamp_iso(), "event": event}
    row.update(payload)
    handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
    handle.flush()


def csv_row(direction: Direction,
            window_index: int,
            elapsed_s: float,
            target_snapshot: dict[str, Any],
            source_model: list[int],
            source_selftest: list[int],
            target_selftest: list[int]) -> dict[str, Any]:
    q = target_snapshot["quality"]
    r = target_snapshot["readiness"]
    o = target_snapshot["observer"]
    w = target_snapshot["window"]
    dco = target_snapshot["dco"]
    pd = target_snapshot["path_delay"]
    return {
        "ts_utc": timestamp_iso(),
        "elapsed_s": f"{elapsed_s:.3f}",
        "window": window_index,
        "direction": f"{direction.source_name}->{direction.target_name}",
        "accepted": q[7],
        "rejected": q[8],
        "last_reject": q[6],
        "last_offset_ns": q[0],
        "rms_offset_ns": q[1],
        "max_abs_offset_ns": q[2],
        "freq_offset_ppb": q[3],
        "jitter_pk_ns": q[4],
        "sample_age_us": q[5],
        "health_state": q[10],
        "lock_quality_tier": q[11],
        "input_ready": r[0],
        "locked": r[1],
        "readiness_reason": r[2],
        "dpll_state": r[3],
        "observer_enabled": r[8],
        "observer_submitted": o[7],
        "observer_accepted": o[8],
        "observer_rejected": o[9],
        "observer_gate": o[15],
        "timestamp_source": o[34],
        "timestamp_resolution_ns": o[35],
        "timestamp_flags": o[36],
        "timestamp_eligible": r[16],
        "source_slot": o[37],
        "reference_slot": o[38],
        "payload_class": o[39],
        "window_armed": w[0],
        "window_periodic": w[1],
        "model_running": source_model[0],
        "model_total": source_model[5],
        "model_completed": source_model[6],
        "model_fault": source_model[9],
        "source_selftest_active": source_selftest[0],
        "target_selftest_active": target_selftest[0],
        "dco_valid": dco[0],
        "dco_lock_state": dco[9],
        "dco_phase_offset_ns": dco[10],
        "dco_period_adjust_ppb": dco[11],
        "dco_update_seq": dco[7],
        "path_delay_ns": pd[9],
        "path_delay_jitter_ns": pd[10],
        "path_delay_freshness_us": pd[13],
    }


def validate_build(name: str, ser, args: argparse.Namespace) -> str:
    build = query(ser, "SYST:FW:BUILD?", args.timeout)
    if args.expected_build and build != f'"{args.expected_build}"':
        raise AssertionError(f"{name}: build mismatch {build} != {args.expected_build}")
    return build


def run_window(index: int,
               direction: Direction,
               source,
               target,
               args: argparse.Namespace,
               csv_writer: csv.DictWriter,
               jsonl_handle,
               monitor_start_s: float) -> WindowSummary:
    release_runtime(source, args.timeout)
    release_runtime(target, args.timeout)

    before = snapshot_board(target, args.timeout)
    before_model = model_snapshot(source, args.timeout)
    emit_jsonl(jsonl_handle, "window_start", {
        "window": index,
        "direction": f"{direction.source_name}->{direction.target_name}",
        "before": before,
        "before_model": before_model,
    })

    rx_resp = query(target, selftest_command(2, args), args.timeout)
    tx_resp = query(source, selftest_command(1, args), args.timeout)
    if rx_resp != "1" or tx_resp != "1":
        raise AssertionError(
            f"{direction.source_name}->{direction.target_name}: "
            f"self-test rejected rx={rx_resp} tx={tx_resp}"
        )

    window_start_s = time.monotonic()
    deadline = window_start_s + args.window_timeout_s
    last_snapshot = before
    last_model = before_model
    last_source_selftest = selftest_snapshot(source, args.timeout)
    last_target_selftest = selftest_snapshot(target, args.timeout)
    sample_rows = 0

    while True:
        now = time.monotonic()
        last_model = model_snapshot(source, args.timeout)
        last_snapshot = snapshot_board(target, args.timeout)
        last_source_selftest = selftest_snapshot(source, args.timeout)
        last_target_selftest = selftest_snapshot(target, args.timeout)
        row = csv_row(direction,
                      index,
                      now - monitor_start_s,
                      last_snapshot,
                      last_model,
                      last_source_selftest,
                      last_target_selftest)
        csv_writer.writerow(row)
        sample_rows += 1

        model_done = last_model[5] != 0 and last_model[6] >= last_model[5]
        target_inactive = last_target_selftest[0] == 0
        accepted_delta = last_snapshot["quality"][7] - before["quality"][7]
        if model_done and (target_inactive or accepted_delta > 0):
            break
        if now >= deadline:
            break
        time.sleep(max(0.05, args.poll_interval_s))

    release_runtime(source, args.timeout)
    release_runtime(target, args.timeout)

    q = last_snapshot["quality"]
    r = last_snapshot["readiness"]
    o = last_snapshot["observer"]
    dco = last_snapshot["dco"]
    accepted_delta = q[7] - before["quality"][7]
    observer_accepted_delta = o[8] - before["observer"][8]
    rejected_delta = q[8] - before["quality"][8]
    observer_rejected_delta = o[9] - before["observer"][9]
    submitted_delta = o[7] - before["observer"][7]
    timestamp_ok = (
        o[34] == TIMESTAMP_SOURCE_HARDWARE_TICK and
        0 < o[35] <= args.max_resolution_ns and
        (o[36] & TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0 and
        (o[36] & TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0
    )
    gate_ok = o[15] == VDC_GATE_PASS and r[12] == VDC_GATE_PASS
    accepted_ok = accepted_delta > 0 and observer_accepted_delta > 0
    ok = accepted_ok and timestamp_ok and gate_ok
    reason = "OK"
    if not accepted_ok:
        reason = "NO_ACCEPTED_SAMPLE"
    elif not timestamp_ok:
        reason = "TIMESTAMP_NOT_ELIGIBLE"
    elif not gate_ok:
        reason = "GATE_REJECT"

    summary = WindowSummary(
        index=index,
        direction=f"{direction.source_name}->{direction.target_name}",
        started_monotonic_s=window_start_s,
        elapsed_s=time.monotonic() - window_start_s,
        accepted_delta=accepted_delta,
        observer_accepted_delta=observer_accepted_delta,
        rejected_delta=rejected_delta,
        observer_rejected_delta=observer_rejected_delta,
        submitted_delta=submitted_delta,
        model_completed=last_model[6],
        model_total=last_model[5],
        final_gate=o[15],
        final_timestamp_source=o[34],
        final_timestamp_resolution_ns=o[35],
        final_timestamp_flags=o[36],
        final_lock_state=r[3],
        final_locked=r[1],
        final_health=q[10],
        final_lock_quality_tier=q[11],
        final_last_offset_ns=q[0],
        final_rms_offset_ns=q[1],
        final_max_abs_offset_ns=q[2],
        final_freq_offset_ppb=q[3],
        final_jitter_pk_ns=q[4],
        final_dco_phase_offset_ns=dco[10],
        final_dco_period_adjust_ppb=dco[11],
        ok=ok,
        reason=reason,
    )
    emit_jsonl(jsonl_handle, "window_end", {"summary": asdict(summary),
                                            "sample_rows": sample_rows})
    if not ok and not args.allow_zero_accepted_window:
        raise AssertionError(
            f"{summary.direction}: {reason} accepted_delta={accepted_delta} "
            f"observer_accepted_delta={observer_accepted_delta} "
            f"gate={o[15]}/{r[12]} source={o[34]} "
            f"resolution_ns={o[35]} flags=0x{o[36]:X}"
        )
    return summary


def aggregate_summary(out_dir: Path,
                      builds: dict[str, str],
                      args: argparse.Namespace,
                      windows: list[WindowSummary],
                      monitor_elapsed_s: float) -> dict[str, Any]:
    accepted = [w.accepted_delta for w in windows]
    offsets = [w.final_last_offset_ns for w in windows]
    rms_offsets = [w.final_rms_offset_ns for w in windows]
    max_offsets = [w.final_max_abs_offset_ns for w in windows]
    freq_offsets = [w.final_freq_offset_ppb for w in windows]
    ok_windows = sum(1 for w in windows if w.ok)
    failed = [w for w in windows if not w.ok]
    summary = {
        "passed": len(windows) > 0 and len(failed) == 0,
        "leg_monitor_passed": len(windows) > 0 and len(failed) == 0,
        "out_dir": str(out_dir),
        "builds": builds,
        "loop": loop_label_from_names(list(builds.keys())),
        "hil_mode": "LEG_SELFTEST_MONITOR",
        "simultaneous_feedback_required": True,
        "simultaneous_feedback_loop_evidence": False,
        "observed_tdma_legs": tdma_observed_legs(windows),
        "closed_loop_assessment": (
            "not_proven_by_this_tool; realtime VDC/DPLL closure requires "
            "simultaneous TDMA uplink/downlink operation in firmware"
        ),
        "window_direction_semantics": (
            "direction is one unidirectional TDMA leg inside one closed-loop pair; "
            "self-test windows can validate each leg, but realtime feedback closure "
            "requires simultaneous uplink/downlink TDMA groups"
        ),
        "duration_s_requested": args.duration_s,
        "duration_s_elapsed": monitor_elapsed_s,
        "windows": len(windows),
        "ok_windows": ok_windows,
        "failed_windows": len(failed),
        "accepted_total_delta": sum(accepted),
        "accepted_min_delta": min(accepted) if accepted else 0,
        "accepted_max_delta": max(accepted) if accepted else 0,
        "last_offset_ns_min": min(offsets) if offsets else 0,
        "last_offset_ns_max": max(offsets) if offsets else 0,
        "last_offset_ns_mean": statistics.fmean(offsets) if offsets else 0.0,
        "rms_offset_ns_max": max(rms_offsets) if rms_offsets else 0,
        "max_abs_offset_ns_max": max(max_offsets) if max_offsets else 0,
        "freq_offset_ppb_min": min(freq_offsets) if freq_offsets else 0,
        "freq_offset_ppb_max": max(freq_offsets) if freq_offsets else 0,
        "quality_svg": str(out_dir / QUALITY_SVG_NAME),
        "summary_svg": str(out_dir / SUMMARY_SVG_NAME),
        "final_windows": [asdict(w) for w in windows[-4:]],
        "failed_window_summaries": [asdict(w) for w in failed[:8]],
        "criteria": {
            "timestamp_source": "HARDWARE_TICK",
            "max_resolution_ns": args.max_resolution_ns,
            "requires_dpll_eligible": True,
            "requires_gate_pass": True,
            "requires_accepted_growth_each_window": not args.allow_zero_accepted_window,
            "does_not_require_fine_100ns_lock_yet": True,
            "does_not_prove_realtime_feedback_loop": True,
        },
    }
    return summary


def svg_text(value: Any) -> str:
    return escape(str(value), entities={"'": "&apos;", '"': "&quot;"})


def scaled_y(value: float, minimum: float, maximum: float, top: float, height: float) -> float:
    if maximum <= minimum:
        return top + height / 2.0
    ratio = (value - minimum) / (maximum - minimum)
    return top + height - ratio * height


def window_label(index: int, total: int) -> str:
    if total <= 12:
        return f"W{index}"
    if index == 1 or index == total or index % max(1, total // 8) == 0:
        return f"W{index}"
    return ""


def generate_summary_svg(out_dir: Path,
                         builds: dict[str, str],
                         args: argparse.Namespace,
                         windows: list[WindowSummary],
                         summary: dict[str, Any]) -> Path | None:
    if not windows:
        return None

    svg_path = out_dir / SUMMARY_SVG_NAME
    total = len(windows)
    ok_count = sum(1 for w in windows if w.ok)
    accepted_total = sum(w.accepted_delta for w in windows)
    accepted_max = max(max(w.accepted_delta for w in windows), 1)
    rejected_max = max(max(w.rejected_delta for w in windows), 1)
    bar_max = max(accepted_max, rejected_max, 1)
    offset_values = [w.final_last_offset_ns for w in windows]
    rms_values = [w.final_rms_offset_ns for w in windows]
    freq_values = [w.final_freq_offset_ppb for w in windows]
    phase_values = [w.final_dco_phase_offset_ns for w in windows]
    offset_min = min(min(offset_values), -args.max_resolution_ns)
    offset_max = max(max(offset_values), args.max_resolution_ns)
    rms_max = max(max(rms_values), 1)
    freq_min = min(freq_values)
    freq_max = max(freq_values)
    phase_min = min(phase_values)
    phase_max = max(phase_values)

    chart_x = 72.0
    chart_w = 468.0
    chart_top = 54.0
    chart_h = 144.0
    gap = 10.0
    slot = chart_w / max(total, 1)
    accepted_bar_w = max(5.0, min(18.0, slot * 0.28))
    rejected_bar_w = accepted_bar_w

    bar_items: list[str] = []
    state_items: list[str] = []
    offset_points: list[str] = []
    rms_points: list[str] = []
    phase_points: list[str] = []
    for i, window in enumerate(windows):
        x_center = chart_x + slot * (i + 0.5)
        acc_h = (window.accepted_delta / bar_max) * chart_h
        rej_h = (window.rejected_delta / bar_max) * chart_h
        acc_x = x_center - accepted_bar_w - 1.0
        rej_x = x_center + 1.0
        bar_items.append(
            f'<rect class="green" x="{acc_x:.1f}" y="{chart_top + chart_h - acc_h:.1f}" '
            f'width="{accepted_bar_w:.1f}" height="{acc_h:.1f}"/>'
        )
        bar_items.append(
            f'<rect class="red" x="{rej_x:.1f}" y="{chart_top + chart_h - rej_h:.1f}" '
            f'width="{rejected_bar_w:.1f}" height="{rej_h:.1f}"/>'
        )
        label = window_label(window.index, total)
        if label:
            bar_items.append(
                f'<text class="tiny" x="{x_center - 10:.1f}" y="{chart_top + chart_h + 20:.1f}">{label}</text>'
            )

        state_class = "ok" if window.ok else "bad"
        state_items.append(
            f'<rect class="{state_class}" x="{72 + i * (1100 / max(total, 1)):.1f}" y="66" '
            f'width="{max(36.0, 1100 / max(total, 1) - 8):.1f}" height="38" rx="6"/>'
        )
        state_items.append(
            f'<text class="state-label" x="{80 + i * (1100 / max(total, 1)):.1f}" y="90">'
            f'W{window.index} {svg_text(tdma_leg_detail(window.direction))}</text>'
        )

        offset_y = scaled_y(window.final_last_offset_ns,
                            offset_min,
                            offset_max,
                            chart_top,
                            chart_h)
        rms_y = scaled_y(window.final_rms_offset_ns, 0, rms_max, chart_top, chart_h)
        phase_y = scaled_y(window.final_dco_phase_offset_ns,
                           phase_min,
                           phase_max,
                           chart_top,
                           chart_h)
        offset_points.append(f"{x_center:.1f},{offset_y:.1f}")
        rms_points.append(f"{x_center:.1f},{rms_y:.1f}")
        phase_points.append(f"{x_center:.1f},{phase_y:.1f}")

    build_text = ", ".join(f"{name}={build}" for name, build in builds.items())
    loop_text = summary.get("loop") or loop_label_from_names(list(builds.keys()))
    failed_reasons: dict[str, int] = {}
    for window in windows:
        if not window.ok:
            failed_reasons[window.reason] = failed_reasons.get(window.reason, 0) + 1
    failed_text = ", ".join(f"{reason}:{count}" for reason, count in failed_reasons.items()) or "none"
    effective = accepted_total > 0 and all(
        w.final_timestamp_source == TIMESTAMP_SOURCE_HARDWARE_TICK and
        0 < w.final_timestamp_resolution_ns <= args.max_resolution_ns and
        (w.final_timestamp_flags & TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0 and
        (w.final_timestamp_flags & TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0
        for w in windows if w.accepted_delta > 0
    )
    stable = ok_count == total and total > 0
    leg_verdict = (
        "有效且稳定：所有窗口 gate PASS，accepted evidence 持续进入 DPLL。"
        if stable else
        "有效但未稳定：accepted evidence 已恢复，但仍存在 gate/relock/window 收尾问题。"
        if effective else
        "未形成有效闭环：accepted evidence 或 timestamp eligibility 未达标。"
    )
    verdict = (
        f"{leg_verdict} 当前结果是节点/单向 leg 监控，不证明实时环路闭环；"
        "实时闭环必须由固件同时运行 TDMA 上行组和下行组。"
    )

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="820" viewBox="0 0 1280 820">
  <defs>
    <style>
      .bg {{ fill: #f7f8fb; }}
      .panel {{ fill: #ffffff; stroke: #d8dee9; stroke-width: 1.2; }}
      .panel-soft {{ fill: #f9fbff; stroke: #d8dee9; stroke-width: 1.2; }}
      .title {{ font: 700 28px "Microsoft YaHei", "Noto Sans CJK SC", Arial, sans-serif; fill: #172033; }}
      .sub {{ font: 15px "Microsoft YaHei", "Noto Sans CJK SC", Arial, sans-serif; fill: #5b6678; }}
      .h2 {{ font: 700 18px "Microsoft YaHei", "Noto Sans CJK SC", Arial, sans-serif; fill: #172033; }}
      .label {{ font: 13px "Microsoft YaHei", "Noto Sans CJK SC", Arial, sans-serif; fill: #596579; }}
      .tiny {{ font: 10px Consolas, "Courier New", monospace; fill: #667085; }}
      .state-label {{ font: 10px Consolas, "Courier New", monospace; fill: #344054; }}
      .code {{ font: 12px Consolas, "Courier New", monospace; fill: #344054; }}
      .num {{ font: 700 24px Consolas, "Courier New", monospace; fill: #172033; }}
      .ok {{ fill: #dff4e8; stroke: #49a675; }}
      .warn {{ fill: #fff2d8; stroke: #d99022; }}
      .bad {{ fill: #ffe2e2; stroke: #d85d5d; }}
      .green {{ fill: #3ca16f; }}
      .red {{ fill: #df6b6b; }}
      .blue {{ fill: #4e7dd9; }}
      .orange {{ fill: #e69b32; }}
      .violet {{ fill: #7866cc; }}
      .grid {{ stroke: #e6ebf2; stroke-width: 1; }}
      .axis {{ stroke: #98a2b3; stroke-width: 1.2; }}
      .line-offset {{ fill: none; stroke: #4e7dd9; stroke-width: 3; }}
      .line-rms {{ fill: none; stroke: #e69b32; stroke-width: 3; }}
      .line-phase {{ fill: none; stroke: #7866cc; stroke-width: 2.4; stroke-dasharray: 7 5; }}
    </style>
  </defs>
  <rect class="bg" x="0" y="0" width="1280" height="820"/>
  <text class="title" x="44" y="52">DPLL 节点长监控评估</text>
  <text class="sub" x="44" y="80">数据源：{svg_text(out_dir.name)}，{svg_text(loop_text)}，{svg_text(build_text)}，duration={summary.get("duration_s_elapsed", 0):.1f}s，windows={total}。</text>

  <g transform="translate(44 112)">
    <rect class="panel" x="0" y="0" width="284" height="114" rx="10"/>
    <text class="label" x="20" y="30">leg 监控窗口通过率</text>
    <text class="num" x="20" y="66">{ok_count} / {total}</text>
    <text class="label" x="20" y="92">失败原因：{svg_text(failed_text)}</text>
  </g>
  <g transform="translate(348 112)">
    <rect class="panel" x="0" y="0" width="284" height="114" rx="10"/>
    <text class="label" x="20" y="30">accepted 总增量</text>
    <text class="num" x="20" y="66">{accepted_total}</text>
    <text class="label" x="20" y="92">窗口范围：{summary.get("accepted_min_delta", 0)}..{summary.get("accepted_max_delta", 0)}</text>
  </g>
  <g transform="translate(652 112)">
    <rect class="panel" x="0" y="0" width="284" height="114" rx="10"/>
    <text class="label" x="20" y="30">timestamp 质量</text>
    <text class="num" x="20" y="66">{args.max_resolution_ns} ns</text>
    <text class="label" x="20" y="92">目标：HARDWARE_TICK + DPLL_ELIGIBLE</text>
  </g>
  <g transform="translate(956 112)">
    <rect class="panel" x="0" y="0" width="284" height="114" rx="10"/>
    <text class="label" x="20" y="30">offset / freq 范围</text>
    <text class="num" x="20" y="66">{summary.get("last_offset_ns_min", 0)}..{summary.get("last_offset_ns_max", 0)} ns</text>
    <text class="label" x="20" y="92">freq={freq_min}..{freq_max} ppb</text>
  </g>

  <g transform="translate(44 256)">
    <rect class="panel" x="0" y="0" width="560" height="246" rx="10"/>
    <text class="h2" x="20" y="34">窗口吞吐：accepted / rejected</text>
    <line class="axis" x1="72" y1="198" x2="540" y2="198"/>
    <line class="axis" x1="72" y1="54" x2="72" y2="198"/>
    <line class="grid" x1="72" y1="150" x2="540" y2="150"/>
    <line class="grid" x1="72" y1="102" x2="540" y2="102"/>
    <text class="tiny" x="20" y="202">0</text>
    <text class="tiny" x="16" y="154">{bar_max // 3}</text>
    <text class="tiny" x="16" y="106">{(bar_max * 2) // 3}</text>
    {''.join(bar_items)}
    <rect class="green" x="430" y="22" width="14" height="14"/><text class="label" x="450" y="34">accepted</text>
    <rect class="red" x="430" y="44" width="14" height="14"/><text class="label" x="450" y="56">rejected</text>
  </g>

  <g transform="translate(636 256)">
    <rect class="panel" x="0" y="0" width="604" height="246" rx="10"/>
    <text class="h2" x="20" y="34">相位与 DCO 质量</text>
    <line class="axis" x1="72" y1="198" x2="540" y2="198"/>
    <line class="axis" x1="72" y1="54" x2="72" y2="198"/>
    <line class="grid" x1="72" y1="150" x2="540" y2="150"/>
    <line class="grid" x1="72" y1="102" x2="540" y2="102"/>
    <polyline class="line-offset" points="{' '.join(offset_points)}"/>
    <polyline class="line-rms" points="{' '.join(rms_points)}"/>
    <polyline class="line-phase" points="{' '.join(phase_points)}"/>
    <text class="tiny" x="20" y="202">min</text>
    <text class="tiny" x="20" y="58">max</text>
    <rect class="blue" x="394" y="22" width="14" height="14"/><text class="label" x="414" y="34">last offset</text>
    <rect class="orange" x="394" y="44" width="14" height="14"/><text class="label" x="414" y="56">RMS offset</text>
    <rect class="violet" x="394" y="66" width="14" height="14"/><text class="label" x="414" y="78">DCO phase</text>
  </g>

  <g transform="translate(44 530)">
    <rect class="panel" x="0" y="0" width="1196" height="136" rx="10"/>
    <text class="h2" x="20" y="34">节点 / TDMA leg 最终状态条</text>
    {''.join(state_items)}
    <text class="label" x="72" y="122">绿色=窗口最终 gate PASS；红色=窗口最终未通过。该条用于观察节点或单向 leg 质量；实时环路闭环需上/下行组同时运行。</text>
  </g>

  <g transform="translate(44 700)">
    <rect class="panel-soft" x="0" y="0" width="1196" height="78" rx="10"/>
    <text class="h2" x="20" y="30">评估结论</text>
    <text class="label" x="20" y="56">{svg_text(verdict)}</text>
  </g>
</svg>
'''
    svg_path.write_text(svg, encoding="utf-8")
    return svg_path


def csv_float(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, "0") or "0")
    except ValueError:
        return 0.0


def csv_int(row: dict[str, str], key: str) -> int:
    try:
        return int(float(row.get(key, "0") or "0"))
    except ValueError:
        return 0


def nice_range(values: list[float],
               *,
               symmetric: bool = False,
               minimum_span: float = 1.0) -> tuple[float, float]:
    if not values:
        return 0.0, minimum_span
    lo = min(values)
    hi = max(values)
    if symmetric:
        bound = max(abs(lo), abs(hi), minimum_span / 2.0)
        return -bound, bound
    if hi - lo < minimum_span:
        mid = (hi + lo) / 2.0
        return mid - minimum_span / 2.0, mid + minimum_span / 2.0
    pad = (hi - lo) * 0.08
    return lo - pad, hi + pad


def fmt_axis(value: float) -> str:
    value_abs = abs(value)
    if value_abs >= 1000000:
        return f"{value / 1000000:.1f}M"
    if value_abs >= 1000:
        return f"{value / 1000:.1f}k"
    if value_abs >= 10 or value == 0:
        return f"{value:.0f}"
    return f"{value:.2f}"


def plot_x(elapsed_s: float, max_elapsed_s: float, left: float, width: float) -> float:
    if max_elapsed_s <= 0:
        return left
    return left + (elapsed_s / max_elapsed_s) * width


def plot_y(value: float, lo: float, hi: float, top: float, height: float) -> float:
    if hi <= lo:
        return top + height / 2.0
    return top + height - ((value - lo) / (hi - lo)) * height


def polyline(rows: list[dict[str, str]],
             key: str,
             lo: float,
             hi: float,
             top: float,
             height: float,
             max_elapsed_s: float,
             left: float,
             width: float) -> str:
    points = []
    for row in rows:
        x = plot_x(csv_float(row, "elapsed_s"), max_elapsed_s, left, width)
        y = plot_y(csv_float(row, key), lo, hi, top, height)
        points.append(f"{x:.1f},{y:.1f}")
    return " ".join(points)


def draw_grid(left: float,
              top: float,
              width: float,
              height: float,
              lo: float,
              hi: float,
              max_elapsed_s: float,
              title: str) -> str:
    items = [
        f'<rect x="{left:.0f}" y="{top:.0f}" width="{width:.0f}" height="{height:.0f}" fill="#fbfdff" stroke="#cbd5e1"/>',
        f'<text class="label" x="{left:.0f}" y="{top - 10:.0f}">{svg_text(title)}</text>',
    ]
    for i in range(7):
        x = left + width * i / 6.0
        t = max_elapsed_s * i / 6.0
        items.append(f'<line x1="{x:.1f}" y1="{top:.0f}" x2="{x:.1f}" y2="{top + height:.0f}" stroke="#d7dde8" stroke-width="1"/>')
        if top > 760:
            items.append(f'<text class="tick" x="{x:.1f}" y="{top + height + 22:.0f}" text-anchor="middle">{fmt_axis(t)}s</text>')
    for i in range(6):
        y = top + height * i / 5.0
        value = hi - (hi - lo) * i / 5.0
        items.append(f'<line x1="{left:.0f}" y1="{y:.1f}" x2="{left + width:.0f}" y2="{y:.1f}" stroke="#d7dde8" stroke-width="1"/>')
        items.append(f'<text class="tick" x="{left - 10:.0f}" y="{y + 4:.1f}" text-anchor="end">{fmt_axis(value)}</text>')
    return "\n".join(items)


def distribute_label_positions(raw_positions: list[float],
                               top: float,
                               height: float,
                               min_gap: float = 17.0) -> list[float]:
    if not raw_positions:
        return []

    low = top + 12.0
    high = top + height - 12.0
    indexed = sorted(enumerate(max(low, min(high, y)) for y in raw_positions),
                     key=lambda item: item[1])
    placed = [y for _, y in indexed]

    for i in range(1, len(placed)):
        placed[i] = max(placed[i], placed[i - 1] + min_gap)
    overflow = placed[-1] - high
    if overflow > 0:
        placed = [y - overflow for y in placed]
    for i in range(len(placed) - 2, -1, -1):
        placed[i] = min(placed[i], placed[i + 1] - min_gap)
    if placed[0] < low:
        shift = low - placed[0]
        placed = [y + shift for y in placed]

    result = [0.0] * len(raw_positions)
    for (original_index, _), y in zip(indexed, placed):
        result[original_index] = y
    return result


def curve_endpoint_labels(rows: list[dict[str, str]],
                          specs: list[tuple[str, str, str]],
                          lo: float,
                          hi: float,
                          top: float,
                          height: float,
                          left: float,
                          width: float) -> str:
    if not rows:
        return ""

    last_row = rows[-1]
    label_x = left + width - 12.0
    raw_y = [plot_y(csv_float(last_row, key), lo, hi, top, height)
             for key, _, _ in specs]
    label_y = distribute_label_positions(raw_y, top, height)
    items: list[str] = []
    for (key, label, color), y in zip(specs, label_y):
        value = csv_float(last_row, key)
        text = f"{label} {fmt_axis(value)}"
        text_width = max(70.0, min(168.0, len(text) * 7.0))
        rect_x = label_x - text_width - 10.0
        items.append(
            f'<rect x="{rect_x:.1f}" y="{y - 10.5:.1f}" width="{text_width + 8:.1f}" '
            f'height="16.5" rx="4" fill="#ffffff" stroke="{color}" opacity="0.92"/>'
        )
        items.append(
            f'<circle cx="{rect_x + 9:.1f}" cy="{y - 2.5:.1f}" r="3.0" fill="{color}"/>'
        )
        items.append(
            f'<text class="curve-tag" x="{label_x:.1f}" y="{y + 1.5:.1f}" '
            f'text-anchor="end">{svg_text(text)}</text>'
        )
    return "\n".join(items)


def generate_lock_quality_curve_svg(out_dir: Path,
                                    summary: dict[str, Any],
                                    samples_csv: Path) -> Path | None:
    if not samples_csv.exists():
        return None

    with samples_csv.open("r", encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        return None

    svg_path = out_dir / QUALITY_SVG_NAME
    max_elapsed_s = max(csv_float(row, "elapsed_s") for row in rows)
    max_elapsed_s = max(max_elapsed_s, 1.0)
    left = 88.0
    width = 1310.0
    offset_top, offset_h = 116.0, 260.0
    freq_top, freq_h = 410.0, 185.0
    count_top, count_h = 629.0, 150.0
    state_top, state_h = 813.0, 115.0

    offset_us = [csv_float(row, "last_offset_ns") / 1000.0 for row in rows]
    rms_us = [csv_float(row, "rms_offset_ns") / 1000.0 for row in rows]
    max_abs_us = [csv_float(row, "max_abs_offset_ns") / 1000.0 for row in rows]
    freq = [csv_float(row, "freq_offset_ppb") for row in rows]
    dco_period = [csv_float(row, "dco_period_adjust_ppb") for row in rows]
    accepted = [csv_float(row, "observer_accepted") for row in rows]
    rejected = [csv_float(row, "observer_rejected") for row in rows]
    submitted = [csv_float(row, "observer_submitted") for row in rows]

    offset_lo, offset_hi = nice_range(offset_us + rms_us + max_abs_us,
                                      minimum_span=1.0)
    freq_lo, freq_hi = nice_range(freq + dco_period,
                                  symmetric=True,
                                  minimum_span=1000.0)
    count_lo, count_hi = nice_range(accepted + rejected + submitted,
                                    minimum_span=10.0)
    state_lo, state_hi = -0.5, 3.5

    normalized_rows: list[dict[str, str]] = []
    for row in rows:
        normalized = dict(row)
        normalized["last_offset_us"] = str(csv_float(row, "last_offset_ns") / 1000.0)
        normalized["rms_offset_us"] = str(csv_float(row, "rms_offset_ns") / 1000.0)
        normalized["max_abs_offset_us"] = str(csv_float(row, "max_abs_offset_ns") / 1000.0)
        normalized_rows.append(normalized)

    failed_windows = {
        item.get("index")
        for item in summary.get("failed_window_summaries", [])
        if isinstance(item, dict)
    }
    shade_items: list[str] = []
    sample_dx = width / max(len(rows), 1)
    for row in rows:
        gate_bad = csv_int(row, "observer_gate") != VDC_GATE_PASS or csv_int(row, "last_reject") != VDC_GATE_PASS
        if not gate_bad:
            continue
        x = plot_x(csv_float(row, "elapsed_s"), max_elapsed_s, left, width)
        shade_w = max(2.0, sample_dx * 0.85)
        for top, height in ((offset_top, offset_h), (freq_top, freq_h),
                            (count_top, count_h), (state_top, state_h)):
            shade_items.append(
                f'<rect x="{x - shade_w / 2:.1f}" y="{top:.1f}" width="{shade_w:.1f}" '
                f'height="{height:.1f}" fill="#fee2e2" opacity="0.70"/>'
            )

    failed_band_items: list[str] = []
    if failed_windows:
        for window in failed_windows:
            related = [row for row in rows if csv_int(row, "window") == window]
            if not related:
                continue
            x0 = plot_x(min(csv_float(row, "elapsed_s") for row in related),
                        max_elapsed_s, left, width)
            x1 = plot_x(max(csv_float(row, "elapsed_s") for row in related),
                        max_elapsed_s, left, width)
            for top, height in ((offset_top, offset_h), (freq_top, freq_h),
                                (count_top, count_h), (state_top, state_h)):
                failed_band_items.append(
                    f'<rect x="{x0:.1f}" y="{top:.1f}" width="{max(2.0, x1 - x0):.1f}" '
                    f'height="{height:.1f}" fill="#fecaca" opacity="0.38"/>'
                )

    state_items: list[str] = []
    for row in rows:
        x = plot_x(csv_float(row, "elapsed_s"), max_elapsed_s, left, width)
        locked_y = state_top + 92.0 - csv_int(row, "locked") * 14.0
        tier_y = state_top + 56.0 - min(csv_int(row, "lock_quality_tier"), 3) * 7.0
        gate_y = state_top + 21.0
        gate_ok = csv_int(row, "observer_gate") == VDC_GATE_PASS
        state_items.append(
            f'<circle cx="{x:.1f}" cy="{locked_y:.1f}" r="3.1" fill="#16a34a" opacity="{0.95 if csv_int(row, "locked") else 0.22}"/>'
        )
        state_items.append(
            f'<circle cx="{x:.1f}" cy="{tier_y:.1f}" r="3.0" fill="#9333ea" opacity="0.75"/>'
        )
        state_items.append(
            f'<circle cx="{x:.1f}" cy="{gate_y:.1f}" r="2.6" fill="{"#16a34a" if gate_ok else "#ef4444"}" opacity="{0.75 if gate_ok else 0.95}"/>'
        )

    title = "VDC DPLL 节点 / TDMA leg 长时间监控质量"
    subtitle = (
        f"{summary.get('loop', 'TDMA feedback loop')} / "
        f"{summary.get('hil_mode', 'LEG_SELFTEST_MONITOR')} / "
        f"elapsed {summary.get('duration_s_elapsed', 0):.1f}s / "
        f"windows {summary.get('windows', 0)} "
        f"(OK {summary.get('ok_windows', 0)}, FAIL {summary.get('failed_windows', 0)}) / "
        f"accepted delta {summary.get('accepted_total_delta', 0)} / "
        f"offset [{summary.get('last_offset_ns_min', 0)}, {summary.get('last_offset_ns_max', 0)}] ns / "
        f"RMS max {summary.get('rms_offset_ns_max', 0)} ns / "
        f"freq [{summary.get('freq_offset_ppb_min', 0)}, {summary.get('freq_offset_ppb_max', 0)}] ppb"
    )
    failed_indices = ",".join(str(item.get("index")) for item in summary.get("failed_window_summaries", []))
    result_line = (
        f"leg monitor passed={str(summary.get('leg_monitor_passed', summary.get('passed', False))).lower()} ; "
        f"realtime loop proven={str(summary.get('simultaneous_feedback_loop_evidence', False)).lower()} ; "
        f"failed windows={failed_indices or 'none'} ; gate reject code 11 = WINDOW_BOUND"
    )
    offset_labels = curve_endpoint_labels(
        normalized_rows,
        [
            ("last_offset_us", "last offset us", "#2563eb"),
            ("rms_offset_us", "RMS offset us", "#059669"),
            ("max_abs_offset_us", "max abs us", "#d97706"),
        ],
        offset_lo,
        offset_hi,
        offset_top,
        offset_h,
        left,
        width,
    )
    freq_labels = curve_endpoint_labels(
        rows,
        [
            ("freq_offset_ppb", "freq offset ppb", "#7c3aed"),
            ("dco_period_adjust_ppb", "DCO adjust ppb", "#0f766e"),
        ],
        freq_lo,
        freq_hi,
        freq_top,
        freq_h,
        left,
        width,
    )
    count_labels = curve_endpoint_labels(
        rows,
        [
            ("observer_accepted", "accepted", "#0284c7"),
            ("observer_rejected", "rejected", "#dc2626"),
            ("observer_submitted", "submitted", "#475569"),
        ],
        count_lo,
        count_hi,
        count_top,
        count_h,
        left,
        width,
    )
    final_row = rows[-1]
    gate_text = "PASS" if csv_int(final_row, "observer_gate") == VDC_GATE_PASS else "REJECT"

    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="1440" height="960" viewBox="0 0 1440 960">
<rect width="100%" height="100%" fill="#ffffff"/>
<style>
text{{font-family:Segoe UI,Microsoft YaHei,Arial,sans-serif}}
.title{{font-size:26px;font-weight:700;fill:#111827}}
.subtitle{{font-size:14px;fill:#475569}}
.label{{font-size:13px;fill:#334155}}
.tick{{font-size:11px;fill:#64748b}}
.legend{{font-size:12px;fill:#334155}}
.small{{font-size:11px;fill:#64748b}}
.curve-tag{{font-size:11px;fill:#1f2937}}
</style>
<text class="title" x="88" y="38">{svg_text(title)}</text>
<text class="subtitle" x="88" y="64">{svg_text(subtitle)}</text>
<text class="subtitle" x="88" y="84">{svg_text(result_line)}</text>
{''.join(failed_band_items)}
{''.join(shade_items)}
{draw_grid(left, offset_top, width, offset_h, offset_lo, offset_hi, max_elapsed_s, 'Offset quality (us)')}
{draw_grid(left, freq_top, width, freq_h, freq_lo, freq_hi, max_elapsed_s, 'Frequency correction (ppb)')}
{draw_grid(left, count_top, width, count_h, count_lo, count_hi, max_elapsed_s, 'Observer counters (count)')}
{draw_grid(left, state_top, width, state_h, state_lo, state_hi, max_elapsed_s, 'Lock status / tier / gate (state)')}
<polyline points="{polyline(normalized_rows, 'last_offset_us', offset_lo, offset_hi, offset_top, offset_h, max_elapsed_s, left, width)}" fill="none" stroke="#2563eb" stroke-width="2.2" stroke-linejoin="round" stroke-linecap="round"/>
<polyline points="{polyline(normalized_rows, 'rms_offset_us', offset_lo, offset_hi, offset_top, offset_h, max_elapsed_s, left, width)}" fill="none" stroke="#059669" stroke-width="2.2" stroke-linejoin="round" stroke-linecap="round"/>
<polyline points="{polyline(normalized_rows, 'max_abs_offset_us', offset_lo, offset_hi, offset_top, offset_h, max_elapsed_s, left, width)}" fill="none" stroke="#d97706" stroke-width="1.8" stroke-linejoin="round" stroke-linecap="round"/>
{offset_labels}
<polyline points="{polyline(rows, 'freq_offset_ppb', freq_lo, freq_hi, freq_top, freq_h, max_elapsed_s, left, width)}" fill="none" stroke="#7c3aed" stroke-width="2.2" stroke-linejoin="round" stroke-linecap="round"/>
<polyline points="{polyline(rows, 'dco_period_adjust_ppb', freq_lo, freq_hi, freq_top, freq_h, max_elapsed_s, left, width)}" fill="none" stroke="#0f766e" stroke-width="2.2" stroke-linejoin="round" stroke-linecap="round"/>
{freq_labels}
<polyline points="{polyline(rows, 'observer_accepted', count_lo, count_hi, count_top, count_h, max_elapsed_s, left, width)}" fill="none" stroke="#0284c7" stroke-width="2.2" stroke-linejoin="round" stroke-linecap="round"/>
<polyline points="{polyline(rows, 'observer_rejected', count_lo, count_hi, count_top, count_h, max_elapsed_s, left, width)}" fill="none" stroke="#dc2626" stroke-width="1.8" stroke-linejoin="round" stroke-linecap="round"/>
<polyline points="{polyline(rows, 'observer_submitted', count_lo, count_hi, count_top, count_h, max_elapsed_s, left, width)}" fill="none" stroke="#475569" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round" opacity="0.75"/>
{count_labels}
<line x1="{left:.0f}" y1="{state_top + 92:.1f}" x2="{left + width:.0f}" y2="{state_top + 92:.1f}" stroke="#e2e8f0"/>
<text class="tick" x="{left - 10:.0f}" y="{state_top + 96:.1f}" text-anchor="end">locked</text>
<line x1="{left:.0f}" y1="{state_top + 56:.1f}" x2="{left + width:.0f}" y2="{state_top + 56:.1f}" stroke="#e2e8f0"/>
<text class="tick" x="{left - 10:.0f}" y="{state_top + 60:.1f}" text-anchor="end">tier</text>
<line x1="{left:.0f}" y1="{state_top + 21:.1f}" x2="{left + width:.0f}" y2="{state_top + 21:.1f}" stroke="#e2e8f0"/>
<text class="tick" x="{left - 10:.0f}" y="{state_top + 25:.1f}" text-anchor="end">gate</text>
{''.join(state_items)}
<rect x="{left + width - 218:.1f}" y="{state_top + 11:.1f}" width="206" height="76" rx="5" fill="#ffffff" stroke="#cbd5e1" opacity="0.94"/>
<text class="curve-tag" x="{left + width - 204:.1f}" y="{state_top + 31:.1f}">gate: {svg_text(gate_text)}</text>
<text class="curve-tag" x="{left + width - 204:.1f}" y="{state_top + 52:.1f}">tier: {csv_int(final_row, "lock_quality_tier")}</text>
<text class="curve-tag" x="{left + width - 204:.1f}" y="{state_top + 73:.1f}">locked: {csv_int(final_row, "locked")}</text>
<text class="small" x="88" y="944">红色背景/标记表示当前 sample 的 gate 或 reject 状态不是 PASS；每条曲线右侧标签为末端值。该图监控节点/单向 leg，实时环路闭环需固件内上行组和下行组同时运行。</text>
</svg>
'''
    svg_path.write_text(svg, encoding="utf-8")
    return svg_path


def mirror_plot_artifacts(out_dir: Path,
                          args: argparse.Namespace,
                          summary: dict[str, Any],
                          paths: list[Path]) -> Path | None:
    if getattr(args, "no_plot_archive", False):
        return None
    archive_root = getattr(args, "plot_archive_dir", None)
    if archive_root is None:
        return None

    archive_dir = Path(archive_root) / out_dir.name
    archive_dir.mkdir(parents=True, exist_ok=True)
    copied: dict[str, str] = {}
    for path in paths:
        if not path.exists():
            continue
        target = archive_dir / path.name
        shutil.copy2(path, target)
        copied[path.name] = str(target)

    if copied:
        summary["plot_archive_dir"] = str(archive_dir)
        if SUMMARY_SVG_NAME in copied:
            summary["summary_svg_archive"] = copied[SUMMARY_SVG_NAME]
        if QUALITY_SVG_NAME in copied:
            summary["quality_svg_archive"] = copied[QUALITY_SVG_NAME]
        summary["summary_json_archive"] = str(archive_dir / "summary.json")
        return archive_dir
    return None


def write_monitor_artifacts(out_dir: Path,
                            builds: dict[str, str],
                            args: argparse.Namespace,
                            windows: list[WindowSummary],
                            summary: dict[str, Any],
                            samples_csv: Path) -> None:
    summary_svg = generate_summary_svg(out_dir, builds, args, windows, summary)
    quality_svg = generate_lock_quality_curve_svg(out_dir, summary, samples_csv)
    generated_paths: list[Path] = []
    if summary_svg is not None:
        summary["summary_svg"] = str(summary_svg)
        generated_paths.append(summary_svg)
    if quality_svg is not None:
        summary["quality_svg"] = str(quality_svg)
        generated_paths.append(quality_svg)

    archive_dir = mirror_plot_artifacts(out_dir, args, summary, generated_paths)
    summary_path = out_dir / "summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if archive_dir is not None:
        shutil.copy2(summary_path, archive_dir / "summary.json")


def main() -> int:
    args = parse_args()
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" /
        f"vdc_long_monitor_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    directions = [
        Direction(args.name_x, args.port_x, args.name_y, args.port_y),
    ]
    if args.reverse:
        directions.append(Direction(args.name_y, args.port_y, args.name_x, args.port_x))

    csv_path = out_dir / "samples.csv"
    jsonl_path = out_dir / "events.jsonl"
    windows: list[WindowSummary] = []
    builds: dict[str, str] = {}
    monitor_start = time.monotonic()
    fieldnames: list[str] | None = None

    try:
        with ExitStack() as stack, \
                csv_path.open("w", encoding="utf-8", newline="") as csv_file, \
                jsonl_path.open("w", encoding="utf-8", newline="\n") as jsonl_handle:
            ser_x = stack.enter_context(open_serial_port(args.port_x,
                                                         args.baud,
                                                         args.timeout,
                                                         args.settle))
            ser_y = stack.enter_context(open_serial_port(args.port_y,
                                                         args.baud,
                                                         args.timeout,
                                                         args.settle))
            serials = {
                args.name_x: ser_x,
                args.name_y: ser_y,
            }
            builds[args.name_x] = validate_build(args.name_x, ser_x, args)
            builds[args.name_y] = validate_build(args.name_y, ser_y, args)
            emit_jsonl(jsonl_handle, "monitor_start", {
                "builds": builds,
                "args": vars(args) | {"out_dir": str(out_dir)},
            })

            probe_row = csv_row(directions[0],
                                0,
                                0.0,
                                snapshot_board(ser_y, args.timeout),
                                model_snapshot(ser_x, args.timeout),
                                selftest_snapshot(ser_x, args.timeout),
                                selftest_snapshot(ser_y, args.timeout))
            fieldnames = list(probe_row.keys())
            writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerow(probe_row)

            window_index = 1
            while time.monotonic() - monitor_start < args.duration_s:
                for direction in directions:
                    if time.monotonic() - monitor_start >= args.duration_s:
                        break
                    summary = run_window(window_index,
                                         direction,
                                         serials[direction.source_name],
                                         serials[direction.target_name],
                                         args,
                                         writer,
                                         jsonl_handle,
                                         monitor_start)
                    windows.append(summary)
                    print(
                        f"{summary.direction} window={summary.index} "
                        f"ok={summary.ok} reason={summary.reason} "
                        f"accepted+={summary.accepted_delta} "
                        f"offset={summary.final_last_offset_ns}ns "
                        f"rms={summary.final_rms_offset_ns}ns "
                        f"freq={summary.final_freq_offset_ppb}ppb "
                        f"tier={summary.final_lock_quality_tier} "
                        f"locked={summary.final_locked}"
                    )
                    window_index += 1

            for ser in (ser_x, ser_y):
                release_runtime(ser, args.timeout)

    except Exception as exc:
        summary = aggregate_summary(out_dir,
                                    builds,
                                    args,
                                    windows,
                                    time.monotonic() - monitor_start)
        summary["passed"] = False
        summary["error"] = str(exc)
        write_monitor_artifacts(out_dir, builds, args, windows, summary, csv_path)
        print(f"FAIL {exc}")
        print(f"summary: passed=False out_dir={out_dir}")
        return 1

    summary = aggregate_summary(out_dir,
                                builds,
                                args,
                                windows,
                                time.monotonic() - monitor_start)
    write_monitor_artifacts(out_dir, builds, args, windows, summary, csv_path)
    print(
        "summary: "
        f"passed={summary['passed']} windows={summary['windows']} "
        f"ok={summary['ok_windows']} accepted_total={summary['accepted_total_delta']} "
        f"offset_range=[{summary['last_offset_ns_min']},{summary['last_offset_ns_max']}]ns "
        f"rms_max={summary['rms_offset_ns_max']}ns "
        f"freq_range=[{summary['freq_offset_ppb_min']},{summary['freq_offset_ppb_max']}]ppb "
        f"out_dir={out_dir}"
    )
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
