#!/usr/bin/env python3
"""Validate the VDC raw capture observer SCPI maintenance path."""

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


OBSERVER_FIELD_COUNT = 40


@dataclass(frozen=True)
class ObserverConfig:
    enabled: int = 1
    max_words_per_service: int = 1
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


def int_fields(response: str) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != OBSERVER_FIELD_COUNT:
        raise AssertionError(f"field count {len(fields)} != {OBSERVER_FIELD_COUNT}: {response}")
    try:
        return [int(field, 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"non-integer observer response: {response}") from exc


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_port(port: str,
                  args: argparse.Namespace,
                  config: ObserverConfig) -> dict[str, object]:
    records: list[dict[str, str]] = []

    def run(ser, command: str) -> str:
        response = query(ser, command, args.timeout)
        records.append({"command": command, "response": response})
        return response

    with open_serial_port(port, args.baud, args.timeout, args.settle) as ser:
        build = run(ser, "SYST:FW:BUILD?")
        if args.expected_build:
            require(build == f'"{args.expected_build}"',
                    f"{port}: build {build!r} != {args.expected_build!r}")

        require(run(ser, "SYST:SYNC:VDC:OBServer") == "1",
                f"{port}: disable command rejected")
        disabled = int_fields(run(ser, "SYST:SYNC:VDC:OBServer?"))
        require(all(value == 0 for value in disabled),
                f"{port}: disabled observer fields are not all zero: {disabled}")

        require(run(ser, config.command()) == "1",
                f"{port}: enable command rejected")
        enabled = int_fields(run(ser, "SYST:SYNC:VDC:OBServer?"))
        require(enabled[0] == 1 and enabled[1] == config.max_words_per_service,
                f"{port}: observer not enabled with expected batch: {enabled[:2]}")
        require(enabled[18] == config.rising_event_id and
                enabled[19] == config.falling_event_id and
                enabled[20] == config.observed_mask and
                enabled[21] == config.initial_sample_mask,
                f"{port}: event/mask fields mismatch: {enabled[18:22]}")
        require(enabled[22] == config.sample_period_ns and
                enabled[23] == config.expected_window_start_lo and
                enabled[24] == config.expected_window_start_hi and
                enabled[25] == config.frame_crc32,
                f"{port}: timing/frame fields mismatch: {enabled[22:26]}")
        require(enabled[29] != 0 and enabled[30] != 0,
                f"{port}: schedule/dictionary CRC evidence missing: {enabled[29:31]}")
        require(enabled[32] == enabled[29],
                f"{port}: dictionary profile CRC {enabled[32]} != schedule CRC {enabled[29]}")

        require(run(ser, "SYST:SYNC:VDC:OBServer 0") == "1",
                f"{port}: final disable command rejected")
        final_disabled = int_fields(run(ser, "SYST:SYNC:VDC:OBServer?"))
        require(all(value == 0 for value in final_disabled),
                f"{port}: final disabled observer fields are not all zero: {final_disabled}")

        err = run(ser, "SYST:ERR?")
        require(err == '0,"No error"', f"{port}: SCPI error queue not empty: {err}")

    return {
        "port": port,
        "passed": True,
        "build": build,
        "schedule_crc32": enabled[29],
        "dictionary_crc32": enabled[30],
        "dictionary_profile_crc32": enabled[32],
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
    config = ObserverConfig()
    out_dir = args.out_dir or (
        ROOT / "build" / f"vdc_observer_validate_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    )
    results: list[dict[str, object]] = []
    try:
        for port in args.ports:
            result = validate_port(port, args, config)
            results.append(result)
            print(
                f"PASS {port} build={result['build']} "
                f"schedule_crc32={result['schedule_crc32']} "
                f"dictionary_crc32={result['dictionary_crc32']}"
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
