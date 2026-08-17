#!/usr/bin/env python3
"""Validate VDC/DPLL lock acquisition through GPIO4..7 debug observation."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from contextlib import ExitStack
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import open_serial_port, read_serial_line_idle  # noqa: E402


LOCK_READINESS_FIELD_COUNT = 24
OBSERVER_FIELD_COUNT = 40
SYNC_QUALITY_FIELD_COUNT = 17
MODEL_PULSE_FIELD_COUNT = 10
SAMPLE_WINDOW_FIELD_COUNT = 13
REALTIME_IO_PROFILE_FIELD_COUNT = 8
MODEL_IO_PROFILE_FIELD_COUNT = 4

TIMESTAMP_SOURCE_HARDWARE_TICK = 2
TIMESTAMP_FLAG_DIAGNOSTIC_ONLY = 0x00000001
TIMESTAMP_FLAG_DPLL_ELIGIBLE = 0x00000002
VDC_GATE_PASS = 0
VDC_LOCK_LOCKED = 5
VDC_HEALTH_HEALTHY = 4
VDC_LOCK_QUALITY_FINE_100NS = 3


@dataclass
class DirectionResult:
    source_name: str
    source_port: str
    target_name: str
    target_port: str
    accepted_before: int
    accepted_after: int
    observer_submitted_before: int
    observer_submitted_after: int
    observer_accepted_before: int
    observer_accepted_after: int
    observer_gate: int
    dpll_state: int
    health_state: int
    lock_quality_tier: int
    timestamp_source: int
    timestamp_resolution_ns: int
    timestamp_flags: int
    input_ready: int
    locked: int
    reason: int
    model_running: int
    model_pio_enabled: int
    model_dma_busy: int
    model_total_pulses: int
    model_completed_pulses: int
    model_transfer_count: int
    sample_window_armed: int
    sample_window_periodic: int
    sample_window_period_ns: int
    sample_window_sample_period_ns: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-x", required=True)
    parser.add_argument("--port-y", required=True)
    parser.add_argument("--name-x", default="X")
    parser.add_argument("--name-y", default="Y")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--poll-timeout", type=float, default=8.0)
    parser.add_argument("--sample-period-ns", type=int, default=100)
    parser.add_argument("--pulse-period-ns", type=int, default=1000000)
    parser.add_argument("--pulse-high-ns", type=int, default=1000)
    parser.add_argument("--pulse-count", type=int, default=512)
    parser.add_argument("--start-delay-ns", type=int, default=1000000000)
    parser.add_argument("--max-resolution-ns", type=int, default=100)
    parser.add_argument("--output-index", type=int, default=0)
    parser.add_argument("--observed-mask", type=int, default=1)
    parser.add_argument("--expected-build")
    parser.add_argument("--accepted-only", action="store_true",
                        help="validate the hardware timestamp accepted path without requiring FINE/HEALTHY lock")
    parser.add_argument("--reverse", action="store_true",
                        help="also validate Y -> X after X -> Y")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


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


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def int_fields(response: str, expected_count: int) -> list[int]:
    fields = parse_csv_response(response)
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


def validate_build(name: str, ser, args: argparse.Namespace) -> str:
    build = query(ser, "SYST:FW:BUILD?", args.timeout)
    if args.expected_build and build != f'"{args.expected_build}"':
        raise AssertionError(f"{name}: build mismatch {build} != {args.expected_build}")
    return build


def validate_gpio_overlay_profile(name: str, ser, args: argparse.Namespace) -> None:
    realtime_profile = int_fields(query(ser, "REALtime:IO:PROFile?",
                                        args.timeout),
                                  REALTIME_IO_PROFILE_FIELD_COUNT)
    model_profile = int_fields(query(ser, "REALtime:IO:MODel:PROFile?",
                                     args.timeout),
                               MODEL_IO_PROFILE_FIELD_COUNT)

    input_base = realtime_profile[0]
    input_count = realtime_profile[1]
    model_base = model_profile[0]
    model_count = model_profile[1]
    uart_enabled = model_profile[3]

    if input_base != model_base or input_count < model_count:
        raise AssertionError(
            f"{name}: VDC GPIO overlay profile mismatch: "
            f"REALtime:IO input={input_base}..{input_base + input_count - 1}, "
            f"model_overlay={model_base}..{model_base + model_count - 1}. "
            "Accepted-only VDC GPIO HIL requires the capture input group to "
            "observe the GPIO4..7 model overlay. Reconfigure the active build "
            "with -DPROJECT_SYNC_IO_INPUT_BASE_PIN=4. GPIO16..24 is the "
            "TDMA/RefMem transport path and must not be used as this overlay."
        )
    if args.output_index >= model_count:
        raise AssertionError(
            f"{name}: output-index {args.output_index} exceeds model overlay "
            f"pin count {model_count}"
        )
    if args.observed_mask == 0 or args.observed_mask >= (1 << input_count):
        raise AssertionError(
            f"{name}: observed-mask 0x{args.observed_mask:X} does not fit "
            f"capture input count {input_count}"
        )
    if uart_enabled != 0:
        raise AssertionError(
            f"{name}: GPIO4/5 UART is enabled, model overlay cannot be used"
        )


def run_direction(source_name: str,
                  source_port: str,
                  source,
                  target_name: str,
                  target_port: str,
                  target,
                  args: argparse.Namespace) -> DirectionResult:
    release_runtime(source, args.timeout)
    release_runtime(target, args.timeout)

    observer_before = int_fields(query(target, "SYST:SYNC:VDC:OBServer?",
                                       args.timeout),
                                 OBSERVER_FIELD_COUNT)
    quality_before = sync_quality(query(target, "READ:SYNC:QUALity?",
                                        args.timeout))

    rx_resp = query(target, selftest_command(2, args), args.timeout)
    if rx_resp != "1":
        raise AssertionError(f"{target_name}: RX self-test rejected: {rx_resp}")
    tx_resp = query(source, selftest_command(1, args), args.timeout)
    if tx_resp != "1":
        raise AssertionError(f"{source_name}: TX self-test rejected: {tx_resp}")

    readiness = int_fields(query(target, "SYST:SYNC:VDC:LOCK:READiness?",
                                 args.timeout),
                           LOCK_READINESS_FIELD_COUNT)
    observer_after = observer_before
    quality_after = quality_before
    model_after = int_fields(query(source,
                                   "REALtime:IO:MODel:PULSe:SCHEDule?",
                                   args.timeout),
                             MODEL_PULSE_FIELD_COUNT)
    window_after = int_fields(query(target,
                                    "REALtime:IO:SAMPle:WINDow?",
                                    args.timeout),
                              SAMPLE_WINDOW_FIELD_COUNT)
    armed_window = window_after

    deadline = time.monotonic() + args.poll_timeout
    while time.monotonic() < deadline:
        model_after = int_fields(query(source,
                                       "REALtime:IO:MODel:PULSe:SCHEDule?",
                                       args.timeout),
                                 MODEL_PULSE_FIELD_COUNT)
        window_after = int_fields(query(target,
                                        "REALtime:IO:SAMPle:WINDow?",
                                        args.timeout),
                                  SAMPLE_WINDOW_FIELD_COUNT)
        if window_after[0] != 0:
            armed_window = window_after
        observer_after = int_fields(query(target, "SYST:SYNC:VDC:OBServer?",
                                          args.timeout),
                                    OBSERVER_FIELD_COUNT)
        quality_after = sync_quality(query(target, "READ:SYNC:QUALity?",
                                           args.timeout))
        readiness = int_fields(query(target, "SYST:SYNC:VDC:LOCK:READiness?",
                                     args.timeout),
                               LOCK_READINESS_FIELD_COUNT)
        if observer_after[8] > observer_before[8]:
            if args.accepted_only:
                if (quality_after[7] > 0 and
                        readiness[12] == VDC_GATE_PASS and
                        readiness[13] == TIMESTAMP_SOURCE_HARDWARE_TICK and
                        0 < readiness[14] <= args.max_resolution_ns and
                        (readiness[15] & TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0 and
                        (readiness[15] & TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0):
                    break
            elif (quality_after[7] > quality_before[7] and
                  quality_after[10] == VDC_HEALTH_HEALTHY and
                  quality_after[11] == VDC_LOCK_QUALITY_FINE_100NS and
                  readiness[1] == 1 and
                  readiness[3] == VDC_LOCK_LOCKED):
                break
        time.sleep(0.05)

    release_runtime(source, args.timeout)
    release_runtime(target, args.timeout)

    if observer_after[8] <= observer_before[8]:
        raise AssertionError(
            f"{source_name}->{target_name}: observer accepted count did not grow: "
            f"{observer_before[8]} -> {observer_after[8]} "
            f"model={model_after} observer={observer_after} "
            f"window={window_after} armed_window={armed_window} "
            f"readiness={readiness}"
        )
    if args.accepted_only:
        if quality_after[7] == 0:
            raise AssertionError(
                f"{source_name}->{target_name}: quality accepted count is still zero: "
                f"{quality_before[7]} -> {quality_after[7]} "
                f"model={model_after} observer={observer_after} "
                f"window={window_after} armed_window={armed_window} "
                f"readiness={readiness}"
            )
    else:
        if quality_after[7] <= quality_before[7]:
            raise AssertionError(
                f"{source_name}->{target_name}: accepted count did not grow: "
                f"{quality_before[7]} -> {quality_after[7]} "
                f"model={model_after} observer={observer_after} "
                f"window={window_after} armed_window={armed_window} "
                f"readiness={readiness}"
            )
        if readiness[1] != 1 or readiness[3] != VDC_LOCK_LOCKED:
            raise AssertionError(
                f"{source_name}->{target_name}: not locked readiness={readiness}"
            )
        if quality_after[10] != VDC_HEALTH_HEALTHY:
            raise AssertionError(
                f"{source_name}->{target_name}: health not healthy: "
                f"{quality_before} -> {quality_after}"
            )
        if quality_after[11] != VDC_LOCK_QUALITY_FINE_100NS:
            raise AssertionError(
                f"{source_name}->{target_name}: quality tier not fine: "
                f"{quality_before} -> {quality_after}"
            )
    if readiness[12] != VDC_GATE_PASS:
        raise AssertionError(
            f"{source_name}->{target_name}: observer gate {readiness[12]} != PASS"
        )
    if readiness[13] != TIMESTAMP_SOURCE_HARDWARE_TICK:
        raise AssertionError(
            f"{source_name}->{target_name}: source {readiness[13]} != HARDWARE_TICK"
        )
    if readiness[14] <= 0 or readiness[14] > args.max_resolution_ns:
        raise AssertionError(
            f"{source_name}->{target_name}: resolution {readiness[14]}ns "
            f"> {args.max_resolution_ns}ns"
        )
    if (readiness[15] & TIMESTAMP_FLAG_DPLL_ELIGIBLE) == 0:
        raise AssertionError(
            f"{source_name}->{target_name}: DPLL_ELIGIBLE missing flags=0x{readiness[15]:X}"
        )
    if (readiness[15] & TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0:
        raise AssertionError(
            f"{source_name}->{target_name}: DIAGNOSTIC_ONLY still set flags=0x{readiness[15]:X}"
        )

    return DirectionResult(
        source_name=source_name,
        source_port=source_port,
        target_name=target_name,
        target_port=target_port,
        accepted_before=quality_before[7],
        accepted_after=quality_after[7],
        observer_submitted_before=observer_before[7],
        observer_submitted_after=observer_after[7],
        observer_accepted_before=observer_before[8],
        observer_accepted_after=observer_after[8],
        observer_gate=readiness[12],
        dpll_state=readiness[3],
        health_state=readiness[4],
        lock_quality_tier=quality_after[11],
        timestamp_source=readiness[13],
        timestamp_resolution_ns=readiness[14],
        timestamp_flags=readiness[15],
        input_ready=readiness[0],
        locked=readiness[1],
        reason=readiness[2],
        model_running=model_after[0],
        model_pio_enabled=model_after[1],
        model_dma_busy=model_after[2],
        model_total_pulses=model_after[5],
        model_completed_pulses=model_after[6],
        model_transfer_count=model_after[7],
        sample_window_armed=window_after[0],
        sample_window_periodic=window_after[1],
        sample_window_period_ns=window_after[6],
        sample_window_sample_period_ns=window_after[7],
    )


def main() -> int:
    args = parse_args()
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" /
        f"vdc_gpio_lock_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    results: list[DirectionResult] = []
    builds: dict[str, str] = {}
    with ExitStack() as stack:
        ser_x = stack.enter_context(open_serial_port(args.port_x,
                                                     args.baud,
                                                     args.timeout,
                                                     args.settle))
        ser_y = stack.enter_context(open_serial_port(args.port_y,
                                                     args.baud,
                                                     args.timeout,
                                                     args.settle))
        builds[args.name_x] = validate_build(args.name_x, ser_x, args)
        builds[args.name_y] = validate_build(args.name_y, ser_y, args)
        validate_gpio_overlay_profile(args.name_x, ser_x, args)
        validate_gpio_overlay_profile(args.name_y, ser_y, args)
        results.append(run_direction(args.name_x,
                                     args.port_x,
                                     ser_x,
                                     args.name_y,
                                     args.port_y,
                                     ser_y,
                                     args))
        if args.reverse:
            results.append(run_direction(args.name_y,
                                         args.port_y,
                                         ser_y,
                                         args.name_x,
                                         args.port_x,
                                         ser_x,
                                         args))

    summary = {
        "passed": True,
        "builds": builds,
        "sample_period_ns": args.sample_period_ns,
        "pulse_period_ns": args.pulse_period_ns,
        "pulse_high_ns": args.pulse_high_ns,
        "pulse_count": args.pulse_count,
        "max_resolution_ns": args.max_resolution_ns,
        "results": [asdict(result) for result in results],
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                          encoding="utf-8")
    for result in results:
        print(
            f"PASS {result.source_name}->{result.target_name} "
            f"accepted={result.accepted_before}->{result.accepted_after} "
            f"observer_accepted={result.observer_accepted_before}->{result.observer_accepted_after} "
        f"state={result.dpll_state} locked={result.locked} "
        f"health={result.health_state} tier={result.lock_quality_tier} "
        f"source={result.timestamp_source} "
            f"resolution_ns={result.timestamp_resolution_ns} "
            f"flags=0x{result.timestamp_flags:X} gate={result.observer_gate}"
        )
    print(f"summary: passed=True out_dir={out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
