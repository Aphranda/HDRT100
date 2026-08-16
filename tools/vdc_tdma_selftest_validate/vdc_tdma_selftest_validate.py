#!/usr/bin/env python3
"""Validate the VDC TDMA observer self-test over two wired boards."""

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
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import open_serial_port, read_serial_line_idle  # noqa: E402


OBSERVER_FIELD_COUNT = 40
READINESS_FIELD_COUNT = 24
SELFTEST_FIELD_COUNT = 16
TIMESTAMP_SOURCE_HARDWARE_TICK = 2
TIMESTAMP_FLAG_DPLL_ELIGIBLE = 0x00000002
TIMESTAMP_FLAG_DIAGNOSTIC_ONLY = 0x00000001
GATE_PASS = 0


@dataclass
class IoProfile:
    input_base: int
    input_count: int
    output_base: int
    output_count: int
    trig_in_pin: int
    rj45_in_pin: int
    trig_out_pin: int
    rj45_out_pin: int


@dataclass
class DirectionResult:
    source: str
    target: str
    output_index: int
    observed_input_index: int
    observed_mask: int
    accepted_before: int
    accepted_after: int
    submitted_after: int
    rejected_after: int
    gate_after: int
    timestamp_source: int
    timestamp_resolution_ns: int
    timestamp_flags: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--name-a", default="X")
    parser.add_argument("--name-b", default="Y")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--line-settle", type=float, default=0.05)
    parser.add_argument("--poll-timeout", type=float, default=3.0)
    parser.add_argument("--sample-period-ns", type=int, default=100)
    parser.add_argument("--pulse-period-ns", type=int, default=2000)
    parser.add_argument("--pulse-high-ns", type=int, default=1000)
    parser.add_argument("--pulse-count", type=int, default=4096)
    parser.add_argument("--start-delay-ns", type=int, default=1000000000)
    parser.add_argument("--expected-build")
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


def cleanup_command(ser, command: str, timeout_s: float) -> None:
    try:
        query(ser, command, timeout_s)
    except Exception:
        pass


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def int_fields(response: str, expected_count: int) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != expected_count:
        raise AssertionError(f"field count {len(fields)} != {expected_count}: {response}")
    try:
        return [int(field.strip().strip('"'), 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"non-integer response: {response}") from exc


def read_profile(ser, timeout_s: float) -> IoProfile:
    values = int_fields(query(ser, "REALtime:IO:PROFile?", timeout_s), 8)
    return IoProfile(*values[:8])


def read_input_mask(ser, timeout_s: float) -> int:
    values = int_fields(query(ser, "REALtime:IO:INPut:LEVel?", timeout_s), 3)
    return values[2]


def release_outputs(ser, timeout_s: float) -> None:
    query(ser, "REALtime:IO:OUTPut:MASK 0", timeout_s)
    query(ser, "REALtime:IO:OUTPut:RELease", timeout_s)


def detect_first_wire(source, target, source_profile: IoProfile, target_profile: IoProfile,
                      timeout_s: float, settle_s: float) -> tuple[int, int]:
    count = min(source_profile.output_count, target_profile.input_count)
    release_outputs(source, timeout_s)
    time.sleep(settle_s)
    for output_index in range(count):
        query(source, f"REALtime:IO:OUTPut:MASK {1 << output_index}", timeout_s)
        time.sleep(settle_s)
        observed = read_input_mask(target, timeout_s) & ((1 << target_profile.input_count) - 1)
        query(source, "REALtime:IO:OUTPut:MASK 0", timeout_s)
        time.sleep(settle_s)
        if observed != 0 and observed & (observed - 1) == 0:
            return output_index, observed.bit_length() - 1
    raise AssertionError("no single-wire output->input mapping detected")


def selftest_command(role: int,
                     output_index: int,
                     observed_mask: int,
                     initial_sample_mask: int,
                     args: argparse.Namespace) -> str:
    values = (
        role,
        output_index,
        observed_mask,
        initial_sample_mask,
        args.sample_period_ns,
        args.pulse_period_ns,
        args.pulse_high_ns,
        args.pulse_count,
        0,
        args.start_delay_ns,
    )
    return "SYST:SYNC:VDC:OBServer:TDMA:SELFtest " + ",".join(str(v) for v in values)


def run_direction(source_name: str, source, source_profile: IoProfile,
                  target_name: str, target, target_profile: IoProfile,
                  args: argparse.Namespace) -> DirectionResult:
    output_index, input_index = detect_first_wire(source,
                                                  target,
                                                  source_profile,
                                                  target_profile,
                                                  args.timeout,
                                                  args.line_settle)
    observed_mask = 1 << input_index
    initial_sample_mask = read_input_mask(target, args.timeout) & observed_mask

    query(target, "SYST:SYNC:VDC:OBServer 0", args.timeout)
    before = int_fields(query(target, "SYST:SYNC:VDC:OBServer?", args.timeout),
                        OBSERVER_FIELD_COUNT)
    rx_cmd = selftest_command(2, 0, observed_mask, initial_sample_mask, args)
    tx_cmd = selftest_command(1, output_index, 1, 0, args)
    after = before
    readiness = [0] * READINESS_FIELD_COUNT
    try:
        with ThreadPoolExecutor(max_workers=2) as executor:
            rx_future = executor.submit(query, target, rx_cmd, args.timeout)
            tx_future = executor.submit(query, source, tx_cmd, args.timeout)
            if rx_future.result() != "1":
                raise AssertionError(f"{target_name}: RX self-test command rejected")
            if tx_future.result() != "1":
                raise AssertionError(f"{source_name}: TX self-test command rejected")
        if len(int_fields(query(target, "SYST:SYNC:VDC:OBServer:TDMA:SELFtest?", args.timeout),
                          SELFTEST_FIELD_COUNT)) != SELFTEST_FIELD_COUNT:
            raise AssertionError(f"{target_name}: malformed self-test status")

        deadline = time.monotonic() + args.poll_timeout
        while time.monotonic() < deadline:
            after = int_fields(query(target, "SYST:SYNC:VDC:OBServer?", args.timeout),
                               OBSERVER_FIELD_COUNT)
            readiness = int_fields(query(target, "SYST:SYNC:VDC:LOCK:READiness?", args.timeout),
                                   READINESS_FIELD_COUNT)
            if after[8] > before[8] and after[15] == GATE_PASS:
                break
            time.sleep(0.05)
    finally:
        cleanup_command(target, "REALtime:IO:SAMPle:STATe 0", args.timeout)
        cleanup_command(target, "SYST:SYNC:VDC:OBServer 0", args.timeout)
        try:
            release_outputs(source, args.timeout)
        except Exception:
            pass

    if after[8] <= before[8]:
        print(f"{target_name}: observer_before={before}")
        print(f"{target_name}: observer_after={after}")
        print(f"{target_name}: readiness={readiness}")
        print(f"{target_name}: selftest={int_fields(query(target, 'SYST:SYNC:VDC:OBServer:TDMA:SELFtest?', args.timeout), SELFTEST_FIELD_COUNT)}")
        raise AssertionError(f"{target_name}: accepted sample did not increase")
    if after[15] != GATE_PASS:
        raise AssertionError(f"{target_name}: VDC gate did not pass: {after[15]}")
    if readiness[13] != TIMESTAMP_SOURCE_HARDWARE_TICK:
        raise AssertionError(f"{target_name}: bad timestamp source: {readiness[13]}")
    if readiness[14] <= 0 or readiness[14] > 100:
        raise AssertionError(f"{target_name}: bad timestamp resolution: {readiness[14]}")
    if (readiness[15] & TIMESTAMP_FLAG_DPLL_ELIGIBLE) == 0:
        raise AssertionError(f"{target_name}: DPLL eligible flag missing: {readiness[15]}")
    if (readiness[15] & TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0:
        raise AssertionError(f"{target_name}: diagnostic flag still set: {readiness[15]}")

    return DirectionResult(source_name,
                           target_name,
                           output_index,
                           input_index,
                           observed_mask,
                           before[8],
                           after[8],
                           after[7],
                           after[9],
                           after[15],
                           readiness[13],
                           readiness[14],
                           readiness[15])


def main() -> int:
    args = parse_args()
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" /
        f"vdc_tdma_selftest_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    with ExitStack() as stack:
        ser_a = stack.enter_context(open_serial_port(args.port_a,
                                                     args.baud,
                                                     args.timeout,
                                                     args.settle))
        ser_b = stack.enter_context(open_serial_port(args.port_b,
                                                     args.baud,
                                                     args.timeout,
                                                     args.settle))
        build_a = query(ser_a, "SYST:FW:BUILD?", args.timeout)
        build_b = query(ser_b, "SYST:FW:BUILD?", args.timeout)
        if args.expected_build:
            expected = f'"{args.expected_build}"'
            if build_a != expected or build_b != expected:
                raise SystemExit(f"build mismatch: {args.port_a}={build_a} {args.port_b}={build_b}")

        profile_a = read_profile(ser_a, args.timeout)
        profile_b = read_profile(ser_b, args.timeout)
        release_outputs(ser_a, args.timeout)
        release_outputs(ser_b, args.timeout)

        results = [
            run_direction(args.name_a, ser_a, profile_a, args.name_b, ser_b, profile_b, args),
            run_direction(args.name_b, ser_b, profile_b, args.name_a, ser_a, profile_a, args),
        ]

    summary = {
        "passed": True,
        "build": {args.name_a: build_a, args.name_b: build_b},
        "ports": {args.name_a: args.port_a, args.name_b: args.port_b},
        "profiles": {args.name_a: asdict(profile_a), args.name_b: asdict(profile_b)},
        "directions": [asdict(result) for result in results],
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                          encoding="utf-8")
    for result in results:
        print(
            f"PASS {result.source}->{result.target} "
            f"OUT{result.output_index}->IN{result.observed_input_index} "
            f"accepted={result.accepted_before}->{result.accepted_after} "
            f"gate={result.gate_after} source={result.timestamp_source} "
            f"resolution_ns={result.timestamp_resolution_ns} "
            f"flags=0x{result.timestamp_flags:X}"
        )
    print(f"summary: passed=True out_dir={out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
