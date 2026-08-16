#!/usr/bin/env python3
"""Run repeated VDC TDMA observation self-tests and monitor lock stability."""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import sys
import time
from contextlib import ExitStack
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

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
        "sample_age_1e3ns": q[5],
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
        "path_delay_freshness_1e3ns": pd[13],
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
        "out_dir": str(out_dir),
        "builds": builds,
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
        "final_windows": [asdict(w) for w in windows[-4:]],
        "failed_window_summaries": [asdict(w) for w in failed[:8]],
        "criteria": {
            "timestamp_source": "HARDWARE_TICK",
            "max_resolution_ns": args.max_resolution_ns,
            "requires_dpll_eligible": True,
            "requires_gate_pass": True,
            "requires_accepted_growth_each_window": not args.allow_zero_accepted_window,
            "does_not_require_fine_100ns_lock_yet": True,
        },
    }
    return summary


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
        (out_dir / "summary.json").write_text(
            json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"FAIL {exc}")
        print(f"summary: passed=False out_dir={out_dir}")
        return 1

    summary = aggregate_summary(out_dir,
                                builds,
                                args,
                                windows,
                                time.monotonic() - monitor_start)
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
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
