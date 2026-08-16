#!/usr/bin/env python3
"""Validate the VDC lock readiness maintenance view over USB CDC."""

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


READINESS_FIELD_COUNT = 24
OBSERVER_FIELD_COUNT = 40
TIMESTAMP_SOURCE_HARDWARE_TICK = 2
TIMESTAMP_FLAG_DIAGNOSTIC_ONLY = 0x00000001
VDC_GATE_TIMESTAMP_NOT_ELIGIBLE = 9
READINESS_OBSERVER_DISABLED = 2
READINESS_TIMESTAMP_NOT_ELIGIBLE = 5


@dataclass(frozen=True)
class ObserverConfig:
    enabled: int = 1
    initial_sample_mask: int = 1
    sample_period_ns: int = 1000
    frame_crc32: int = 0

    def command(self) -> str:
        values = (
            self.enabled,
            self.initial_sample_mask,
            self.sample_period_ns,
            self.frame_crc32,
        )
        return "SYST:SYNC:VDC:OBServer:TDMA " + ",".join(str(value) for value in values)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ports", nargs="+", help="USB CDC ports, for example COM5 COM6")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=2.0, help="per-command response timeout")
    parser.add_argument("--settle", type=float, default=1.0, help="seconds to wait after opening a port")
    parser.add_argument("--sample-hz", type=int, default=1000, help="capture sample rate")
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


def int_fields(response: str, expected_count: int) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != expected_count:
        raise AssertionError(f"field count {len(fields)} != {expected_count}: {response}")
    try:
        return [int(field, 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"non-integer response: {response}") from exc


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


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
        disabled = int_fields(run(ser, "SYST:SYNC:VDC:LOCK:READiness?"),
                              READINESS_FIELD_COUNT)
        require(disabled[0] == 0 and disabled[1] == 0,
                f"{port}: disabled readiness unexpectedly true: {disabled[:2]}")
        require(disabled[2] == READINESS_OBSERVER_DISABLED,
                f"{port}: disabled reason {disabled[2]} != {READINESS_OBSERVER_DISABLED}")

        require(run(ser, observer_config.command()) == "1",
                f"{port}: observer enable command rejected")
        before_observer = int_fields(run(ser, "SYST:SYNC:VDC:OBServer?"),
                                     OBSERVER_FIELD_COUNT)
        require(before_observer[1] == 8,
                f"{port}: TDMA observer batch mismatch: {before_observer[1]}")
        require(before_observer[21] == observer_config.initial_sample_mask,
                f"{port}: TDMA initial mask mismatch: {before_observer[21]}")
        require(before_observer[23] != 0 or before_observer[24] != 0,
                f"{port}: TDMA expected window is missing")
        require((before_observer[27] & 0x80000000) != 0,
                f"{port}: TDMA base quality flag missing: {before_observer[27]}")
        before_ready = int_fields(run(ser, "SYST:SYNC:VDC:LOCK:READiness?"),
                                  READINESS_FIELD_COUNT)
        require(before_ready[0] == 0 and before_ready[1] == 0,
                f"{port}: readiness true before eligible sample: {before_ready[:2]}")
        require(before_ready[17] != 0 and before_ready[18] != 0,
                f"{port}: active dictionary evidence missing: {before_ready[17:19]}")

        write(ser, f"REALtime:IO:SAMPle:RATE {args.sample_hz}")
        time.sleep(args.capture_s)
        after_observer = int_fields(run(ser, "SYST:SYNC:VDC:OBServer?"),
                                    OBSERVER_FIELD_COUNT)
        after_ready = int_fields(run(ser, "SYST:SYNC:VDC:LOCK:READiness?"),
                                 READINESS_FIELD_COUNT)

        require(after_observer[7] > before_observer[7],
                f"{port}: no observation submitted: {before_observer[7]} -> {after_observer[7]}")
        require(after_observer[8] == before_observer[8],
                f"{port}: diagnostic sample was accepted: {before_observer[8]} -> {after_observer[8]}")
        require(after_observer[9] > before_observer[9],
                f"{port}: diagnostic sample was not rejected: {before_observer[9]} -> {after_observer[9]}")
        require(after_ready[0] == 0 and after_ready[1] == 0,
                f"{port}: diagnostic readiness unexpectedly true: {after_ready[:2]}")
        require(after_ready[2] == READINESS_TIMESTAMP_NOT_ELIGIBLE,
                f"{port}: reason {after_ready[2]} != {READINESS_TIMESTAMP_NOT_ELIGIBLE}")
        require(after_ready[12] == VDC_GATE_TIMESTAMP_NOT_ELIGIBLE,
                f"{port}: observer gate {after_ready[12]} != {VDC_GATE_TIMESTAMP_NOT_ELIGIBLE}")
        require(after_ready[13] == TIMESTAMP_SOURCE_HARDWARE_TICK,
                f"{port}: source {after_ready[13]} != {TIMESTAMP_SOURCE_HARDWARE_TICK}")
        require(0 < after_ready[14] <= 100,
                f"{port}: timestamp resolution out of range: {after_ready[14]}")
        require((after_ready[15] & TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0,
                f"{port}: diagnostic flag missing: {after_ready[15]}")
        require(after_ready[16] == 0,
                f"{port}: diagnostic sample reported DPLL eligible")

        write(ser, "REALtime:IO:SAMPle:STATe 0")
        require(run(ser, "SYST:SYNC:VDC:OBServer 0") == "1",
                f"{port}: final observer disable command rejected")
        err = run(ser, "SYST:ERR?")
        require(err == '0,"No error"', f"{port}: SCPI error queue not empty: {err}")

    return {
        "port": port,
        "passed": True,
        "build": build,
        "reason": after_ready[2],
        "submitted": after_ready[9],
        "accepted": after_ready[10],
        "rejected": after_ready[11],
        "gate": after_ready[12],
        "timestamp_source": after_ready[13],
        "timestamp_resolution_ns": after_ready[14],
        "timestamp_flags": after_ready[15],
        "dictionary_crc32": after_ready[18],
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
        ROOT / "build" / f"vdc_lock_readiness_validate_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    )
    results: list[dict[str, object]] = []
    try:
        for port in args.ports:
            result = validate_port(port, args)
            results.append(result)
            print(
                f"PASS {port} build={result['build']} "
                f"reason={result['reason']} submitted={result['submitted']} "
                f"accepted={result['accepted']} rejected={result['rejected']} "
                f"gate={result['gate']} source={result['timestamp_source']} "
                f"resolution_ns={result['timestamp_resolution_ns']} "
                f"flags={result['timestamp_flags']}"
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
