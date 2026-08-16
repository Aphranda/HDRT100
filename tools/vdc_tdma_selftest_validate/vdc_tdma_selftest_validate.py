#!/usr/bin/env python3
"""Validate the VDC self-test path mounted on the common TDMA service."""

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


SELFTEST_FIELD_COUNT = 16
TDMA_STATUS_FIELD_COUNT = 19
TDMA_RESULT_FRAME_READY = 2
TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE = 1
TIMESTAMP_SOURCE_SOFTWARE_US = 1
TIMESTAMP_FLAG_DIAGNOSTIC_ONLY = 0x00000001
TIMESTAMP_FLAG_DPLL_ELIGIBLE = 0x00000002


@dataclass
class BoardResult:
    name: str
    port: str
    build: str
    ready_before: int
    ready_after: int
    intent_seq: int
    completed_seq: int
    last_result: int
    payload_class: int
    timestamp_source: int
    timestamp_resolution_ns: int
    timestamp_flags: int
    selftest_status: list[int]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--name-a", default="X")
    parser.add_argument("--name-b", default="Y")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--poll-timeout", type=float, default=3.0)
    parser.add_argument("--sample-period-ns", type=int, default=100)
    parser.add_argument("--pulse-period-ns", type=int, default=2000)
    parser.add_argument("--pulse-high-ns", type=int, default=1000)
    parser.add_argument("--pulse-count", type=int, default=16)
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


def tdma_status(response: str) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != TDMA_STATUS_FIELD_COUNT or fields[0].strip().strip('"') != "OK":
        raise AssertionError(f"malformed TDMA status: {response}")
    try:
        return [int(field.strip().strip('"'), 0) for field in fields[1:]]
    except ValueError as exc:
        raise AssertionError(f"non-integer TDMA status: {response}") from exc


def selftest_command(args: argparse.Namespace) -> str:
    values = (
        1,
        0,
        1,
        0,
        args.sample_period_ns,
        args.pulse_period_ns,
        args.pulse_high_ns,
        args.pulse_count,
        0,
        args.start_delay_ns,
    )
    return "SYST:SYNC:VDC:OBServer:TDMA:SELFtest " + ",".join(str(v) for v in values)


def run_board(name: str, port: str, ser, args: argparse.Namespace) -> BoardResult:
    build = query(ser, "SYST:FW:BUILD?", args.timeout)
    if args.expected_build and build != f'"{args.expected_build}"':
        raise AssertionError(f"{name}: build mismatch {build} != {args.expected_build}")

    before = tdma_status(query(ser, "SYST:SYNC:VDC:TDMA:STATus?", args.timeout))
    if query(ser, selftest_command(args), args.timeout) != "1":
        raise AssertionError(f"{name}: VDC TDMA self-test command rejected")

    after = before
    deadline = time.monotonic() + args.poll_timeout
    while time.monotonic() < deadline:
        after = tdma_status(query(ser, "SYST:SYNC:VDC:TDMA:STATus?", args.timeout))
        if after[5] == after[4] and after[9] > before[9]:
            break
        time.sleep(0.05)

    selftest = int_fields(query(ser, "SYST:SYNC:VDC:OBServer:TDMA:SELFtest?", args.timeout),
                          SELFTEST_FIELD_COUNT)

    if after[9] <= before[9]:
        raise AssertionError(f"{name}: TDMA ready_count did not increase: {before} -> {after}")
    if after[13] != TDMA_RESULT_FRAME_READY:
        raise AssertionError(f"{name}: TDMA last_result {after[13]} != FRAME_READY")
    if after[8] != TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE:
        raise AssertionError(f"{name}: payload_class {after[8]} != VDC_SYNC_SAMPLE")
    if after[15] != TIMESTAMP_SOURCE_SOFTWARE_US:
        raise AssertionError(f"{name}: timestamp source {after[15]} != SOFTWARE_US")
    if after[16] != 1000:
        raise AssertionError(f"{name}: timestamp resolution {after[16]} != 1000ns")
    if (after[17] & TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0:
        raise AssertionError(f"{name}: DIAGNOSTIC_ONLY flag missing: 0x{after[17]:X}")
    if (after[17] & TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0:
        raise AssertionError(f"{name}: DPLL_ELIGIBLE flag must not be set: 0x{after[17]:X}")

    return BoardResult(
        name=name,
        port=port,
        build=build,
        ready_before=before[9],
        ready_after=after[9],
        intent_seq=after[4],
        completed_seq=after[5],
        last_result=after[13],
        payload_class=after[8],
        timestamp_source=after[15],
        timestamp_resolution_ns=after[16],
        timestamp_flags=after[17],
        selftest_status=selftest,
    )


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
        results = [
            run_board(args.name_a, args.port_a, ser_a, args),
            run_board(args.name_b, args.port_b, ser_b, args),
        ]

    summary = {
        "passed": True,
        "ports": {args.name_a: args.port_a, args.name_b: args.port_b},
        "results": [asdict(result) for result in results],
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                          encoding="utf-8")
    for result in results:
        print(
            f"PASS {result.name} {result.port} "
            f"ready={result.ready_before}->{result.ready_after} "
            f"intent={result.intent_seq} completed={result.completed_seq} "
            f"payload={result.payload_class} "
            f"source={result.timestamp_source} "
            f"resolution_ns={result.timestamp_resolution_ns} "
            f"flags=0x{result.timestamp_flags:X}"
        )
    print(f"summary: passed=True out_dir={out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
