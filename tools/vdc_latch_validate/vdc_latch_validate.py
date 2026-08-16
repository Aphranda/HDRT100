#!/usr/bin/env python3
"""Validate the first Sync IO capture latch prototype over USB CDC."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import open_serial_port, read_serial_line_idle  # noqa: E402


LATCH_FIELD_COUNT = 8
OBSERVER_FIELD_COUNT = 40
TIMESTAMP_SOURCE_SOFTWARE_US = 1
TIMESTAMP_RESOLUTION_NS_SOFTWARE_US = 1000


@dataclass(frozen=True)
class LatchStatus:
    initialized: int
    capture_running: int
    capture_sample_hz: int
    dropped_capture_words: int
    latched_capture_words: int
    dropped_latched_capture_words: int
    capture_latch_source: int
    capture_latch_resolution_ns: int

    @classmethod
    def parse(cls, response: str) -> "LatchStatus":
        fields = parse_csv_response(response)
        if len(fields) != LATCH_FIELD_COUNT:
            raise AssertionError(f"field count {len(fields)} != {LATCH_FIELD_COUNT}: {response}")
        return cls(*(int(field, 0) for field in fields))


@dataclass(frozen=True)
class ObserverConfig:
    enabled: int = 1
    max_words_per_service: int = 8
    rising_event_id: int = 1
    falling_event_id: int = 2
    observed_mask: int = 1
    initial_sample_mask: int = 0
    next_base_time_l32_ns: int = 0
    sample_period_ns: int = 1000
    expected_window_start_lo: int = 0
    expected_window_start_hi: int = 0
    frame_crc32: int = 1

    def command(self) -> str:
        values = (
            self.enabled,
            self.max_words_per_service,
            self.rising_event_id,
            self.falling_event_id,
            self.observed_mask,
            self.initial_sample_mask,
            self.next_base_time_l32_ns,
            self.sample_period_ns,
            self.expected_window_start_lo,
            self.expected_window_start_hi,
            self.frame_crc32,
        )
        return "SYST:SYNC:VDC:OBServer " + ",".join(str(value) for value in values)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ports", nargs="+", help="USB CDC ports, for example COM5 COM6")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=2.0, help="per-command response timeout")
    parser.add_argument("--settle", type=float, default=1.0, help="seconds to wait after opening a port")
    parser.add_argument("--sample-hz", type=int, default=1000, help="capture sample rate for latch smoke test")
    parser.add_argument("--capture-s", type=float, default=0.25, help="capture duration in seconds")
    parser.add_argument("--expected-build", help="optional expected SYSTem:FW:BUILD? text without quotes")
    parser.add_argument("--out-dir", type=Path, help="output directory for transcript and summary")
    return parser.parse_args()


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


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


def write_command(ser, command: str) -> None:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    deadline = time.monotonic() + 0.2
    while time.monotonic() < deadline:
        if getattr(ser, "in_waiting", 0):
            ser.read(ser.in_waiting)
            deadline = time.monotonic() + 0.02
            continue
        time.sleep(0.005)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def int_fields(response: str, expected_count: int) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != expected_count:
        raise AssertionError(f"field count {len(fields)} != {expected_count}: {response}")
    try:
        return [int(field, 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"non-integer response: {response}") from exc


def validate_port(port: str, args: argparse.Namespace) -> dict[str, object]:
    records: list[dict[str, str]] = []
    observer_config = ObserverConfig()

    def run(ser, command: str) -> str:
        response = query(ser, command, args.timeout)
        records.append({"command": command, "response": response})
        return response

    def write(ser, command: str) -> None:
        write_command(ser, command)
        records.append({"command": command, "response": "<sent>"})

    with open_serial_port(port, args.baud, args.timeout, args.settle) as ser:
        build = run(ser, "SYST:FW:BUILD?")
        if args.expected_build:
            require(build == f'"{args.expected_build}"',
                    f"{port}: build {build!r} != {args.expected_build!r}")

        require(run(ser, "SYST:SYNC:VDC:OBServer 0") == "1",
                f"{port}: observer disable command rejected")
        require(run(ser, observer_config.command()) == "1",
                f"{port}: observer enable command rejected")
        observer_before = int_fields(run(ser, "SYST:SYNC:VDC:OBServer?"),
                                     OBSERVER_FIELD_COUNT)

        write(ser, f"REALtime:IO:SAMPle:RATE {args.sample_hz}")
        before = LatchStatus.parse(run(ser, "REALtime:IO:SAMPle:LATCh?"))
        require(before.initialized == 1, f"{port}: sync_io is not initialized")
        require(before.capture_running == 1, f"{port}: sample rate did not start capture")

        time.sleep(args.capture_s)
        during = LatchStatus.parse(run(ser, "REALtime:IO:SAMPle:LATCh?"))
        require(during.capture_latch_source == TIMESTAMP_SOURCE_SOFTWARE_US,
                f"{port}: unexpected timestamp source {during.capture_latch_source}")
        require(during.capture_latch_resolution_ns == TIMESTAMP_RESOLUTION_NS_SOFTWARE_US,
                f"{port}: unexpected timestamp resolution {during.capture_latch_resolution_ns}")
        require(during.latched_capture_words >= before.latched_capture_words,
                f"{port}: latch counter regressed {before} -> {during}")
        observer_during = int_fields(run(ser, "SYST:SYNC:VDC:OBServer?"),
                                     OBSERVER_FIELD_COUNT)
        require(observer_during[3] > observer_before[3],
                f"{port}: observer raw word count did not grow: "
                f"{observer_before[3]} -> {observer_during[3]}")
        require(observer_during[0] == 1 and observer_during[1] == observer_config.max_words_per_service,
                f"{port}: observer config not active: {observer_during[:2]}")

        write(ser, "REALtime:IO:SAMPle:STATe 0")
        stopped = LatchStatus.parse(run(ser, "REALtime:IO:SAMPle:LATCh?"))
        require(stopped.capture_running == 0, f"{port}: capture did not stop")
        require(run(ser, "SYST:SYNC:VDC:OBServer 0") == "1",
                f"{port}: final observer disable command rejected")

        err = run(ser, "SYST:ERR?")
        require(err == '0,"No error"', f"{port}: SCPI error queue not empty: {err}")

    return {
        "port": port,
        "passed": True,
        "build": build,
        "sample_hz": args.sample_hz,
        "latched_before": before.latched_capture_words,
        "latched_during": during.latched_capture_words,
        "dropped_latched_during": during.dropped_latched_capture_words,
        "observer_raw_before": observer_before[3],
        "observer_raw_during": observer_during[3],
        "observer_no_edge_during": observer_during[4],
        "observer_submitted_during": observer_during[7],
        "observer_rejected_during": observer_during[9],
        "timestamp_source": during.capture_latch_source,
        "timestamp_resolution_ns": during.capture_latch_resolution_ns,
        "records": records,
    }


def write_outputs(out_dir: Path, results: list[dict[str, object]]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps({"passed": all(r["passed"] for r in results),
                    "ports": results}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    with (out_dir / "transcript.txt").open("w", encoding="utf-8", newline="\n") as handle:
        for result in results:
            handle.write(f"# port={result['port']} passed={result['passed']}\n")
            for record in result["records"]:
                handle.write(f"> {record['command']}\n")
                handle.write(f"< {record['response']}\n")


def main() -> int:
    args = parse_args()
    out_dir = args.out_dir or (
        ROOT / "build" / f"vdc_latch_validate_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    )
    results: list[dict[str, object]] = []
    try:
        for port in args.ports:
            result = validate_port(port, args)
            results.append(result)
            print(
                f"PASS {port} build={result['build']} "
                f"latched={result['latched_before']}->{result['latched_during']} "
                f"observer_raw={result['observer_raw_before']}->{result['observer_raw_during']} "
                f"source={result['timestamp_source']} "
                f"resolution_ns={result['timestamp_resolution_ns']}"
            )
    except AssertionError as exc:
        print(f"FAIL {exc}")
        if results:
            write_outputs(out_dir, results)
            print(f"out_dir={out_dir}")
        return 1

    write_outputs(out_dir, results)
    print(f"summary: passed=True total={len(results)} out_dir={out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
