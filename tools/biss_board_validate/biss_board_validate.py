#!/usr/bin/env python3
"""Validate BiSS-C TAP smoke paths on RP2350_TRIG over SCPI USB CDC."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.sd_trace_decode.sd_trace_decode import decode_trace  # noqa: E402

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

BISS_SCPI_PREFIX = "COMMunication:BISS"
BISS_STATUS_QUERY = f"{BISS_SCPI_PREFIX}:STATus?"


def biss_command(suffix: str) -> str:
    return f"{BISS_SCPI_PREFIX}:{suffix}"


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
    parser.add_argument("--scan-wait-s", type=float, default=0.0,
                        help="seconds to wait for timeout sample-scan progress after ARM")
    parser.add_argument("--expect-scan-steps", type=int, default=0,
                        help="minimum sample_scan_index expected after --scan-wait-s")
    parser.add_argument("--capture-trace", action="store_true",
                        help="after scan validation, trigger fault evidence and decode latest fault trace")
    parser.add_argument(
        "--inject-positions",
        help="comma-separated positions injected after ARM; default brackets --target to force one crossing",
    )
    return parser.parse_args()


def normalize_scpi_line(line: str) -> str:
    return line.strip()


def is_log_or_empty(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:") or "[     " in line


def read_scpi_line(ser: serial.Serial, timeout_s: float, *, accept_ack: bool) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = normalize_scpi_line(raw.decode("utf-8", errors="replace"))
        if accept_ack and line.replace('"', '') == "OK":
            return '"OK"'
        if accept_ack and (line.startswith('"O[') or line.startswith('O[')):
            return '"OK"'
        if is_log_or_empty(line):
            continue
        if line == '"OK"' or line.startswith('"OK"['):
            if accept_ack:
                return '"OK"'
            continue
        return line
    return "<timeout>"


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_line(ser, timeout_s, accept_ack=False)


def command_ack(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_line(ser, timeout_s, accept_ack=True)


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def quote_path(path: str) -> str:
    return '"' + path.replace('"', '') + '"'


def path_from_response(response: str, index: int) -> str:
    fields = parse_csv_response(response)
    return fields[index] if len(fields) > index else ""


def file_info_size(response: str) -> int:
    fields = parse_csv_response(response)
    if len(fields) < 6 or fields[0] != "OK" or fields[3] != "FILE":
        return 0
    try:
        return int(fields[2], 0)
    except ValueError:
        return 0


def read_file_via_scpi(ser: serial.Serial,
                       path: str,
                       expected_size: int,
                       timeout_s: float,
                       results: dict[str, str],
                       key_prefix: str,
                       chunk_size: int = 128) -> bytes:
    data = bytearray()
    for _ in range(64):
        if expected_size > 0 and len(data) >= expected_size:
            break
        command = f"MMEM:READ? {quote_path(path)},{len(data)},{chunk_size}"
        response = query(ser, command, timeout_s)
        results[f"{key_prefix}:{command}"] = response
        fields = parse_csv_response(response)
        if len(fields) < 9 or fields[0] != "OK":
            break
        try:
            offset = int(fields[2], 0)
            returned = int(fields[4], 0)
        except ValueError:
            break
        if offset != len(data) or returned < 0:
            break
        if returned == 0:
            break
        chunk = bytes.fromhex(fields[8])
        if len(chunk) != returned:
            break
        data.extend(chunk)
        if fields[5] == "1":
            break
        time.sleep(0.05)
    return bytes(data)


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


def wait_until_query(ser: serial.Serial,
                     command: str,
                     timeout_s: float,
                     predicate) -> str:
    deadline = time.monotonic() + timeout_s
    last = ""
    while time.monotonic() < deadline:
        last = query(ser, command, min(0.5, timeout_s))
        if predicate(last):
            return last
        time.sleep(0.05)
    return last or "<timeout>"


def wait_until_biss_state(ser: serial.Serial, state: int, timeout_s: float) -> str:
    return wait_until_query(
        ser,
        BISS_STATUS_QUERY,
        timeout_s,
        lambda response: status_uint(parse_biss_status(response), "trigger_state") == state,
    )


def wait_until_biss_scan_index(ser: serial.Serial,
                               expected_index: int,
                               timeout_s: float) -> str:
    return wait_until_query(
        ser,
        BISS_STATUS_QUERY,
        timeout_s,
        lambda response: status_uint(parse_biss_status(response), "sample_scan_index") >= expected_index,
    )


def csv_field(response: str, index: int) -> str:
    fields = parse_csv_response(response)
    return fields[index] if index < len(fields) else ""


def uint_response_matches(expected: int):
    return lambda response: csv_field(response, 0) == str(expected)


def write_and_confirm(ser: serial.Serial,
                      results: dict[str, str],
                      command: str,
                      timeout_s: float,
                      query_command: str | None = None,
                      predicate=None) -> None:
    results[command] = command_ack(ser, command, timeout_s)
    time.sleep(0.05)
    if query_command is not None and predicate is not None:
        results[f"confirm:{query_command}:{command}"] = wait_until_query(
            ser,
            query_command,
            timeout_s,
            predicate,
        )


def configure_commands(args: argparse.Namespace) -> list[str]:
    commands = [
        biss_command("ROLE 0"),
        biss_command("DEV 0"),
        biss_command(f"CLOC {args.clock_hz}"),
        biss_command(f"FBIT {args.frame_bits}"),
        biss_command(f"POFF {args.position_offset}"),
        biss_command(f"PBIT {args.position_bits}"),
        biss_command(f"PMOD {args.position_modulo}"),
        biss_command(f"TARG {args.target}"),
        biss_command(f"SAMP:EDGE {args.sample_edge}"),
        biss_command(f"SAMP:DEL {args.sample_delay}"),
        biss_command(f"TIME {args.timeout_us}"),
        biss_command("ANCH:BITS 0"),
        biss_command("ERR:BIT 4294967295"),
        biss_command("WARN:BIT 4294967295"),
        biss_command("STAT:GATE 0"),
        biss_command("CRC:BITS 0"),
        biss_command("CRC:COV:BITS 0"),
        biss_command("CRC:GATE 0"),
        biss_command(f"SAMP:SCAN {1 if args.enable_scan else 0}"),
    ]
    if args.enable_scan:
        commands.extend(
            [
                biss_command(f"SAMP:SCAN:STAR {args.scan_start}"),
                biss_command(f"SAMP:SCAN:END {args.scan_end}"),
                biss_command(f"SAMP:SCAN:STEP {args.scan_step}"),
            ]
        )
    return commands


def configure_steps(args: argparse.Namespace) -> list[tuple[str, str | None, object | None]]:
    steps: list[tuple[str, str | None, object | None]] = [
        (biss_command("ROLE 0"), biss_command("ROLE?"), lambda response: csv_field(response, 1) == "0"),
        (biss_command("DEV 0"), biss_command("DEV?"), uint_response_matches(0)),
        (biss_command(f"CLOC {args.clock_hz}"), biss_command("CLOC?"), uint_response_matches(args.clock_hz)),
        (biss_command(f"FBIT {args.frame_bits}"), biss_command("FBIT?"), uint_response_matches(args.frame_bits)),
        (biss_command(f"POFF {args.position_offset}"), biss_command("POFF?"), uint_response_matches(args.position_offset)),
        (biss_command(f"PBIT {args.position_bits}"), biss_command("PBIT?"), uint_response_matches(args.position_bits)),
        (biss_command(f"PMOD {args.position_modulo}"), biss_command("PMOD?"), uint_response_matches(args.position_modulo)),
        (biss_command(f"TARG {args.target}"), biss_command("TARG?"), uint_response_matches(args.target)),
        (biss_command(f"SAMP:EDGE {args.sample_edge}"), biss_command("SAMP:EDGE?"), lambda response: csv_field(response, 1) == str(args.sample_edge)),
        (biss_command(f"SAMP:DEL {args.sample_delay}"), biss_command("SAMP:DEL?"), uint_response_matches(args.sample_delay)),
        (biss_command(f"TIME {args.timeout_us}"), biss_command("TIME?"), uint_response_matches(args.timeout_us)),
        (biss_command("ANCH:BITS 0"), biss_command("ANCH:BITS?"), uint_response_matches(0)),
        (biss_command("ERR:BIT 4294967295"), biss_command("ERR:BIT?"), uint_response_matches(4294967295)),
        (biss_command("WARN:BIT 4294967295"), biss_command("WARN:BIT?"), uint_response_matches(4294967295)),
        (biss_command("STAT:GATE 0"), biss_command("STAT:GATE?"), lambda response: csv_field(response, 1) == "0"),
        (biss_command("CRC:BITS 0"), biss_command("CRC:BITS?"), uint_response_matches(0)),
        (biss_command("CRC:COV:BITS 0"), biss_command("CRC:COV:BITS?"), uint_response_matches(0)),
        (biss_command("CRC:GATE 0"), biss_command("CRC:GATE?"), lambda response: csv_field(response, 1) == "0"),
        (biss_command(f"SAMP:SCAN {1 if args.enable_scan else 0}"), biss_command("SAMP:SCAN?"), lambda response: csv_field(response, 1) == ("1" if args.enable_scan else "0")),
    ]
    if args.enable_scan:
        steps.extend(
            [
                (biss_command(f"SAMP:SCAN:STAR {args.scan_start}"), biss_command("SAMP:SCAN:STAR?"), uint_response_matches(args.scan_start)),
                (biss_command(f"SAMP:SCAN:END {args.scan_end}"), biss_command("SAMP:SCAN:END?"), uint_response_matches(args.scan_end)),
                (biss_command(f"SAMP:SCAN:STEP {args.scan_step}"), biss_command("SAMP:SCAN:STEP?"), uint_response_matches(args.scan_step)),
            ]
        )
    return steps


def query_commands() -> tuple[str, ...]:
    return (
        "*IDN?",
        "SYST:FW:BUILD?",
        biss_command("ROLE?"),
        biss_command("CLOC?"),
        biss_command("FBIT?"),
        biss_command("POFF?"),
        biss_command("PBIT?"),
        biss_command("PMOD?"),
        biss_command("TARG?"),
        biss_command("SAMP:EDGE?"),
        biss_command("SAMP:DEL?"),
        biss_command("SAMP:SCAN?"),
        biss_command("PINS?"),
        "TRIG:MODE?",
        BISS_STATUS_QUERY,
    )


def run_serial(args: argparse.Namespace, out_dir: Path) -> dict[str, str]:
    results: dict[str, str] = {}
    positions = inject_positions(args)

    try:
        with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as ser:
            time.sleep(args.settle)
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            results["pre:TRIG:DISarm"] = command_ack(ser, "TRIG:DISarm", args.timeout)
            time.sleep(0.2)

            for command, query_command, predicate in configure_steps(args):
                write_and_confirm(ser,
                                  results,
                                  command,
                                  args.timeout,
                                  query_command,
                                  predicate)

            for command in query_commands():
                results[command] = query(ser, command, args.timeout)
                time.sleep(0.05)

            configure_command = biss_command("CONF")
            results[configure_command] = command_ack(ser, configure_command, args.timeout)
            results[f"configured:wait:{BISS_STATUS_QUERY}"] = wait_until_biss_state(ser, 6, args.timeout)
            results["configured:TRIG:MODE?"] = query(ser, "TRIG:MODE?", args.timeout)
            results[f"configured:{BISS_STATUS_QUERY}"] = query(ser, BISS_STATUS_QUERY, args.timeout)

            if not args.skip_arm:
                results["pre-arm:STAT:TRIG?"] = query(ser, "STAT:TRIG?", args.timeout)
                results[f"pre-arm:{BISS_STATUS_QUERY}"] = query(ser, BISS_STATUS_QUERY, args.timeout)
                results["TRIG:ARM"] = command_ack(ser, "TRIG:ARM", args.timeout)
                results["post-arm:STAT:TRIG?"] = query(ser, "STAT:TRIG?", args.timeout)
                results[f"armed:wait:{BISS_STATUS_QUERY}"] = wait_until_biss_state(ser, 7, args.timeout)
                results[f"armed:{BISS_STATUS_QUERY}"] = query(ser, BISS_STATUS_QUERY, args.timeout)

                if args.expect_scan_steps > 0:
                    wait_s = args.scan_wait_s if args.scan_wait_s > 0.0 else args.timeout
                    results[f"scan:wait:{BISS_STATUS_QUERY}"] = wait_until_biss_scan_index(
                        ser,
                        args.expect_scan_steps,
                        wait_s,
                    )

                if not args.skip_inject:
                    for position in positions:
                        command = biss_command(f"FRAM {position}")
                        results[command] = command_ack(ser, command, args.timeout)
                        time.sleep(0.15)
                    results[f"injected:{BISS_STATUS_QUERY}"] = query(ser, BISS_STATUS_QUERY, args.timeout)

                if args.capture_trace:
                    results["TRIG:FAULT"] = command_ack(ser, "TRIG:FAULT", args.timeout)
                    time.sleep(0.3)
                    for attempt in range(8):
                        key = f"FAULT:SYST:STOR:JOB?:{attempt}"
                        results[key] = query(ser, "SYST:STOR:JOB?", args.timeout)
                        fields = parse_csv_response(results[key])
                        if fields and fields[0] in ("DONE", "FAILED"):
                            break
                        time.sleep(0.1)
                    results["FAULT:SYST:TRAC:LAST?"] = query(ser, "SYST:TRAC:LAST?", args.timeout)
                    trace_path = path_from_response(results["FAULT:SYST:TRAC:LAST?"], 3)
                    if trace_path:
                        results["INFO:FAULT:SYST:TRAC:LAST?"] = query(
                            ser,
                            f"MMEM:INFO? {quote_path(trace_path)}",
                            args.timeout,
                        )
                        idx_path = trace_path[:-4] + ".idx" if trace_path.endswith(".bin") else ""
                        if idx_path:
                            results["INFO:FAULT:SYST:TRAC:IDX?"] = query(
                                ser,
                                f"MMEM:INFO? {quote_path(idx_path)}",
                                args.timeout,
                            )
                        trace_size = file_info_size(results.get("INFO:FAULT:SYST:TRAC:LAST?", ""))
                        idx_size = file_info_size(results.get("INFO:FAULT:SYST:TRAC:IDX?", ""))
                        trace_dir = out_dir / "trace_readback"
                        if trace_size > 0:
                            trace_data = read_file_via_scpi(
                                ser,
                                trace_path,
                                trace_size,
                                args.timeout,
                                results,
                                "READ:FAULT:SYST:TRAC:LAST?",
                            )
                            trace_out = trace_dir / Path(trace_path).name
                            trace_out.parent.mkdir(parents=True, exist_ok=True)
                            trace_out.write_bytes(trace_data)
                            results["READBACK:FAULT:SYST:TRAC:LAST?"] = (
                                f"OK,{trace_path},{trace_size},{len(trace_data)},{trace_out}"
                            )
                        if idx_path and idx_size > 0:
                            idx_data = read_file_via_scpi(
                                ser,
                                idx_path,
                                idx_size,
                                args.timeout,
                                results,
                                "READ:FAULT:SYST:TRAC:IDX?",
                            )
                            idx_out = trace_dir / Path(idx_path).name
                            idx_out.parent.mkdir(parents=True, exist_ok=True)
                            idx_out.write_bytes(idx_data)
                            results["READBACK:FAULT:SYST:TRAC:IDX?"] = (
                                f"OK,{idx_path},{idx_size},{len(idx_data)},{idx_out}"
                            )

                results["TRIG:DISarm"] = command_ack(ser, "TRIG:DISarm", args.timeout)
                time.sleep(0.3)
                results[f"final:{BISS_STATUS_QUERY}"] = query(ser, BISS_STATUS_QUERY, args.timeout)
    except (OSError, serial.SerialException) as exc:
        results["exception"] = f"{type(exc).__name__}: {exc}"
    finally:
        lines = [f"{command} -> {response}" for command, response in results.items()]
        write_text(out_dir / "queries.txt", "\n".join(lines) + "\n")
    return results


def validate_results(args: argparse.Namespace, results: dict[str, str]) -> list[str]:
    failures: list[str] = []

    for command in configure_commands(args):
        expect(results.get(command) == '"OK"', failures, f"{command} did not return OK: {results.get(command)!r}")

    configure_command = biss_command("CONF")
    expect(results.get(configure_command) == '"OK"',
           failures,
           f"{configure_command} did not return OK: {results.get(configure_command)!r}")
    expect(status_uint(parse_biss_status(results.get(f"configured:{BISS_STATUS_QUERY}", "")), "trigger_state") == 6,
           failures,
           "BiSS state after COMMunication:BISS:CONFigure is not BISS_CONFIGURED")

    initial = parse_biss_status(results.get(BISS_STATUS_QUERY, ""))
    configured = parse_biss_status(results.get(f"configured:{BISS_STATUS_QUERY}", ""))
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
    armed = parse_biss_status(results.get(f"armed:{BISS_STATUS_QUERY}", ""))
    expect(status_uint(armed, "trigger_state") == 7, failures, "BiSS state after ARM is not BISS_ARMED")
    expect(status_uint(armed, "active_sample_edge") == args.sample_edge,
           failures,
           "active sample edge did not freeze to requested edge")
    if args.expect_scan_steps == 0:
        expect(status_uint(armed, "active_sample_delay_cycles") == args.sample_delay,
               failures,
               "active sample delay did not freeze to requested delay")

    if args.expect_scan_steps > 0:
        expect(args.enable_scan,
               failures,
               "--expect-scan-steps requires --enable-scan")
        scanned = parse_biss_status(results.get(f"scan:wait:{BISS_STATUS_QUERY}", ""))
        expect(status_uint(scanned, "trigger_state") == 7,
               failures,
               "BiSS state changed before scan validation completed")
        expect(status_uint(scanned, "timeout_count") >= args.expect_scan_steps,
               failures,
               "timeout_count did not advance during sample scan")
        expect(status_uint(scanned, "sample_scan_index") >= args.expect_scan_steps,
               failures,
               "sample_scan_index did not reach expected scan steps")
        expect(status_uint(scanned, "active_sample_delay_cycles") != args.sample_delay,
               failures,
               "active sample delay did not change during sample scan")

    if not args.skip_inject:
        positions = inject_positions(args)
        armed_rx = status_uint(armed, "rx_frame_count")
        armed_trigger = status_uint(armed, "trigger_count")
        armed_pulse = status_uint(armed, "pulse_out_count")
        for position in positions:
            command = biss_command(f"FRAM {position}")
            expect(results.get(command) == '"OK"',
                   failures,
                   f"{command} did not return OK: {results.get(command)!r}")
        injected = parse_biss_status(results.get(f"injected:{BISS_STATUS_QUERY}", ""))
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

    if args.capture_trace:
        expect(results.get("TRIG:FAULT") == '"OK"',
               failures,
               f"TRIG:FAULT did not return OK: {results.get('TRIG:FAULT')!r}")
        fault_job_keys = sorted(k for k in results if k.startswith("FAULT:SYST:STOR:JOB?:"))
        expect(bool(fault_job_keys), failures, "fault evidence job was not queried")
        if fault_job_keys:
            fault_job = parse_csv_response(results[fault_job_keys[-1]])
            expect(len(fault_job) >= 8, failures, "fault evidence job returned too few fields")
            if len(fault_job) >= 8:
                expect(fault_job[0] == "DONE",
                       failures,
                       f"fault evidence job state is {fault_job[0]!r}, expected DONE")
                expect(fault_job[2] == "FAULT_EVIDENCE",
                       failures,
                       f"fault evidence job type is {fault_job[2]!r}, expected FAULT_EVIDENCE")
                expect(fault_job[7] == "0",
                       failures,
                       f"fault evidence job error is {fault_job[7]!r}, expected 0")
        fault_trace = parse_csv_response(results.get("FAULT:SYST:TRAC:LAST?", ""))
        expect(len(fault_trace) >= 7, failures, "FAULT SYST:TRAC:LAST? returned too few fields")
        if len(fault_trace) >= 7:
            expect(fault_trace[0] == "OK",
                   failures,
                   f"FAULT trace status is {fault_trace[0]!r}, expected OK")
            expect(fault_trace[6] == "0",
                   failures,
                   f"FAULT trace error is {fault_trace[6]!r}, expected 0")
        readback_bin = parse_csv_response(results.get("READBACK:FAULT:SYST:TRAC:LAST?", ""))
        readback_idx = parse_csv_response(results.get("READBACK:FAULT:SYST:TRAC:IDX?", ""))
        expect(len(readback_bin) >= 5 and readback_bin[0] == "OK",
               failures,
               "fault trace .bin was not read back over MMEM:READ?")
        expect(len(readback_idx) >= 5 and readback_idx[0] == "OK",
               failures,
               "fault trace .idx was not read back over MMEM:READ?")
        if len(readback_bin) >= 5 and len(readback_idx) >= 5:
            expect(readback_bin[2] == readback_bin[3],
                   failures,
                   "fault trace .bin readback size does not match expected size")
            expect(readback_idx[2] == readback_idx[3],
                   failures,
                   "fault trace .idx readback size does not match expected size")
            try:
                decoded = decode_trace(Path(readback_bin[4]), Path(readback_idx[4]))
                checks = decoded["checks"]
                for check_name in ("magic_ok", "schema_ok", "size_ok", "crc_ok", "idx_ok"):
                    expect(bool(checks.get(check_name)),
                           failures,
                           f"trace decode check failed: {check_name}")
                event_names = {str(record.get("event_name")) for record in decoded["records"]}
                expect("trigger.biss_timeout" in event_names,
                       failures,
                       "decoded trace missing trigger.biss_timeout")
                expect("trigger.biss_scan_step" in event_names,
                       failures,
                       "decoded trace missing trigger.biss_scan_step")
                write_text(Path(readback_bin[4]).parent / "decoded_fault_trace.json",
                           json.dumps(decoded, indent=2, ensure_ascii=False) + "\n")
            except (OSError, ValueError) as exc:
                failures.append(f"trace readback decode failed: {exc}")

    expect(results.get("TRIG:DISarm") == '"OK"',
           failures,
           f"TRIG:DISarm did not return OK: {results.get('TRIG:DISarm')!r}")
    final = parse_biss_status(results.get(f"final:{BISS_STATUS_QUERY}", ""))
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
