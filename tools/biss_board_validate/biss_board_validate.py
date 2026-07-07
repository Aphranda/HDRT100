#!/usr/bin/env python3
"""Validate BiSS-C TAP smoke paths on RP2350_TRIG over SCPI USB CDC."""

from __future__ import annotations

import argparse
import csv
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]

BISS_STATUS_FIELDS = (
    "role_name",
    "role_id",
    "role_status",
    "trigger_state",
    "device_id",
    "clock_hz",
    "frame_bits",
    "position_offset",
    "position_bits",
    "position_modulo",
    "target",
    "last_position",
    "last_seq",
    "frame_error_count",
    "status_block_count",
    "crc_error_count",
    "fifo_overflow_count",
    "timeout_count",
    "trigger_count",
    "pulse_in_count",
    "tx_frame_count",
    "rx_frame_count",
    "pulse_out_count",
    "active_sample_edge",
    "active_sample_delay_cycles",
    "sample_scan_index",
    "sample_scan_wrap_count",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM4")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by USB CDC on most hosts")
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0, help="seconds to wait after opening the port")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    parser.add_argument("--clock-hz", type=int, default=1_000_000)
    parser.add_argument("--frame-bits", type=int, default=48)
    parser.add_argument("--position-offset", type=int, default=8)
    parser.add_argument("--position-bits", type=int, default=24)
    parser.add_argument("--position-modulo", type=int, default=16_777_216)
    parser.add_argument("--target", type=int, default=100)
    parser.add_argument("--sample-edge", type=int, choices=(0, 1), default=0)
    parser.add_argument("--sample-delay", type=int, default=8)
    parser.add_argument("--timeout-us", type=int, default=10_000)
    parser.add_argument("--enable-scan", action="store_true", help="enable timeout sample-delay scan")
    parser.add_argument("--scan-start", type=int, default=4)
    parser.add_argument("--scan-end", type=int, default=24)
    parser.add_argument("--scan-step", type=int, default=2)
    parser.add_argument("--skip-arm", action="store_true", help="only configure and query, do not arm PIO")
    parser.add_argument("--skip-inject", action="store_true", help="do not inject software position frames")
    parser.add_argument(
        "--inject-positions",
        help="comma-separated positions injected after ARM; default brackets --target to force one crossing",
    )
    return parser.parse_args()


def normalize_scpi_line(line: str) -> str:
    return line.strip()


def is_log_or_empty(line: str) -> bool:
    return not line or line.startswith("[")


def read_scpi_line(ser: serial.Serial, timeout_s: float, *, accept_ack: bool) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = normalize_scpi_line(raw.decode("utf-8", errors="replace"))
        if is_log_or_empty(line):
            continue
        if line == '"OK"' and not accept_ack:
            continue
        return line
    return "<timeout>"


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_line(ser, timeout_s, accept_ack=False)


def command_ack(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_line(ser, timeout_s, accept_ack=True)


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def parse_biss_status(response: str) -> dict[str, str]:
    fields = parse_csv_response(response)
    return {
        name: fields[index] if index < len(fields) else ""
        for index, name in enumerate(BISS_STATUS_FIELDS)
    }


def parse_positions(text: str) -> list[int]:
    positions: list[int] = []
    for token in text.split(","):
        token = token.strip()
        if not token:
            continue
        positions.append(int(token, 0))
    return positions


def inject_positions(args: argparse.Namespace) -> list[int]:
    if args.inject_positions:
        return parse_positions(args.inject_positions)
    if args.target == 0:
        return [0, 1, 2]
    if args.position_modulo > 1:
        before = (args.target + args.position_modulo - 1) % args.position_modulo
        after = (args.target + 1) % args.position_modulo
        return [before, after, (after + 1) % args.position_modulo]
    return [max(args.target - 1, 0), args.target + 1, args.target + 2]


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def expect(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def status_uint(status: dict[str, str], field: str) -> int:
    try:
        return int(status.get(field, ""), 0)
    except ValueError:
        return 0


def configure_commands(args: argparse.Namespace) -> list[str]:
    commands = [
        "TRIG:BISS:ROLE 0",
        "TRIG:BISS:DEV 0",
        f"TRIG:BISS:CLOC {args.clock_hz}",
        f"TRIG:BISS:FBIT {args.frame_bits}",
        f"TRIG:BISS:POFF {args.position_offset}",
        f"TRIG:BISS:PBIT {args.position_bits}",
        f"TRIG:BISS:PMOD {args.position_modulo}",
        f"TRIG:BISS:TARG {args.target}",
        f"TRIG:BISS:SAMP:EDGE {args.sample_edge}",
        f"TRIG:BISS:SAMP:DEL {args.sample_delay}",
        f"TRIG:BISS:TIME {args.timeout_us}",
        "TRIG:BISS:ANCH:BITS 0",
        "TRIG:BISS:ERR:BIT 4294967295",
        "TRIG:BISS:WARN:BIT 4294967295",
        "TRIG:BISS:STAT:GATE 0",
        "TRIG:BISS:CRC:BITS 0",
        "TRIG:BISS:CRC:GATE 0",
        f"TRIG:BISS:SAMP:SCAN {1 if args.enable_scan else 0}",
    ]
    if args.enable_scan:
        commands.extend(
            [
                f"TRIG:BISS:SAMP:SCAN:STAR {args.scan_start}",
                f"TRIG:BISS:SAMP:SCAN:END {args.scan_end}",
                f"TRIG:BISS:SAMP:SCAN:STEP {args.scan_step}",
            ]
        )
    return commands


def query_commands() -> tuple[str, ...]:
    return (
        "*IDN?",
        "SYST:FW:BUILD?",
        "TRIG:BISS:ROLE?",
        "TRIG:BISS:CLOC?",
        "TRIG:BISS:FBIT?",
        "TRIG:BISS:POFF?",
        "TRIG:BISS:PBIT?",
        "TRIG:BISS:PMOD?",
        "TRIG:BISS:TARG?",
        "TRIG:BISS:SAMP:EDGE?",
        "TRIG:BISS:SAMP:DEL?",
        "TRIG:BISS:SAMP:SCAN?",
        "TRIG:BISS:PINS?",
        "TRIG:MODE?",
        "STAT:BISS?",
    )


def run_serial(args: argparse.Namespace, out_dir: Path) -> dict[str, str]:
    results: dict[str, str] = {}
    positions = inject_positions(args)

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        results["pre:TRIG:DISA"] = command_ack(ser, "TRIG:DISA", args.timeout)
        time.sleep(0.1)

        for command in configure_commands(args):
            results[command] = command_ack(ser, command, args.timeout)
            time.sleep(0.05)

        for command in query_commands():
            results[command] = query(ser, command, args.timeout)
            time.sleep(0.05)

        results["TRIG:MODE 3"] = command_ack(ser, "TRIG:MODE 3", args.timeout)
        time.sleep(0.1)
        results["configured:TRIG:MODE?"] = query(ser, "TRIG:MODE?", args.timeout)
        results["configured:STAT:BISS?"] = query(ser, "STAT:BISS?", args.timeout)

        if not args.skip_arm:
            results["TRIG:ARM"] = command_ack(ser, "TRIG:ARM", args.timeout)
            time.sleep(0.2)
            results["armed:STAT:BISS?"] = query(ser, "STAT:BISS?", args.timeout)

            if not args.skip_inject:
                for position in positions:
                    command = f"TRIG:BISS:FRAM {position}"
                    results[command] = command_ack(ser, command, args.timeout)
                    time.sleep(0.05)
                results["injected:STAT:BISS?"] = query(ser, "STAT:BISS?", args.timeout)

            results["TRIG:DISA"] = command_ack(ser, "TRIG:DISA", args.timeout)
            time.sleep(0.1)
            results["final:STAT:BISS?"] = query(ser, "STAT:BISS?", args.timeout)

    lines = [f"{command} -> {response}" for command, response in results.items()]
    write_text(out_dir / "queries.txt", "\n".join(lines) + "\n")
    return results


def validate_results(args: argparse.Namespace, results: dict[str, str]) -> list[str]:
    failures: list[str] = []

    for command in configure_commands(args):
        expect(results.get(command) == '"OK"', failures, f"{command} did not return OK: {results.get(command)!r}")

    expect(results.get("TRIG:MODE 3") == '"OK"',
           failures,
           f"TRIG:MODE 3 did not return OK: {results.get('TRIG:MODE 3')!r}")

    initial = parse_biss_status(results.get("STAT:BISS?", ""))
    configured = parse_biss_status(results.get("configured:STAT:BISS?", ""))
    expect(initial.get("role_name") == "TAP", failures, "initial BiSS role is not TAP")
    expect(configured.get("role_status") == "OK", failures, "configured BiSS role status is not OK")
    expect(status_uint(configured, "clock_hz") == args.clock_hz, failures, "configured clock does not match")
    expect(status_uint(configured, "frame_bits") == args.frame_bits, failures, "configured frame bits do not match")
    expect(status_uint(configured, "position_offset") == args.position_offset,
           failures,
           "configured position offset does not match")
    expect(status_uint(configured, "position_bits") == args.position_bits,
           failures,
           "configured position bits do not match")
    expect(status_uint(configured, "position_modulo") == args.position_modulo,
           failures,
           "configured position modulo does not match")
    expect(status_uint(configured, "target") == args.target, failures, "configured target does not match")

    if args.skip_arm:
        return failures

    expect(results.get("TRIG:ARM") == '"OK"',
           failures,
           f"TRIG:ARM did not return OK: {results.get('TRIG:ARM')!r}")
    armed = parse_biss_status(results.get("armed:STAT:BISS?", ""))
    expect(status_uint(armed, "trigger_state") == 7, failures, "BiSS state after ARM is not BISS_ARMED")
    expect(status_uint(armed, "active_sample_edge") == args.sample_edge,
           failures,
           "active sample edge did not freeze to requested edge")
    expect(status_uint(armed, "active_sample_delay_cycles") == args.sample_delay,
           failures,
           "active sample delay did not freeze to requested delay")

    if not args.skip_inject:
        positions = inject_positions(args)
        armed_rx = status_uint(armed, "rx_frame_count")
        armed_trigger = status_uint(armed, "trigger_count")
        armed_pulse = status_uint(armed, "pulse_out_count")
        for position in positions:
            command = f"TRIG:BISS:FRAM {position}"
            expect(results.get(command) == '"OK"',
                   failures,
                   f"{command} did not return OK: {results.get(command)!r}")
        injected = parse_biss_status(results.get("injected:STAT:BISS?", ""))
        expect(status_uint(injected, "rx_frame_count") >= armed_rx + len(positions),
               failures,
               "software frame injection did not advance rx_frame_count")
        if len(positions) >= 2 and args.target != 0:
            expect(status_uint(injected, "trigger_count") >= armed_trigger + 1,
                   failures,
                   "software frame injection did not produce a crossing trigger")
            expect(status_uint(injected, "pulse_out_count") >= armed_pulse + 1,
                   failures,
                   "software frame injection did not produce pulse_out_count")

    expect(results.get("TRIG:DISA") == '"OK"',
           failures,
           f"TRIG:DISA did not return OK: {results.get('TRIG:DISA')!r}")
    final = parse_biss_status(results.get("final:STAT:BISS?", ""))
    expect(status_uint(final, "trigger_state") != 7, failures, "BiSS state is still armed after DISARM")
    return failures


def write_summary(out_dir: Path,
                  args: argparse.Namespace,
                  results: dict[str, str],
                  failures: Iterable[str]) -> int:
    failure_list = list(failures)
    summary = {
        "started": out_dir.name.removeprefix("biss_validation_"),
        "port": args.port,
        "passed": not failure_list,
        "failures": failure_list,
        "results": results,
        "biss_status_fields": BISS_STATUS_FIELDS,
    }
    write_text(out_dir / "summary.json", json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    summary_text = "PASS\n" if not failure_list else "FAIL\n" + "\n".join(f"- {item}" for item in failure_list) + "\n"
    write_text(out_dir / "summary.txt", summary_text)
    print(f"out_dir={out_dir}")
    print(summary_text, end="")
    return 0 if not failure_list else 1


def main() -> int:
    args = parse_args()
    started = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = (args.out_dir or ROOT / "build" / f"biss_validation_{started}").resolve()

    results = run_serial(args, out_dir)
    failures = validate_results(args, results)
    return write_summary(out_dir, args, results, failures)


if __name__ == "__main__":
    raise SystemExit(main())
