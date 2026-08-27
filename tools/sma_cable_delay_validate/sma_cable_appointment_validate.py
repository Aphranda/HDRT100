#!/usr/bin/env python3
"""Validate the NO1 mark + 40 ns SFCW appointment with NO5."""

from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
HERE = Path(__file__).resolve().parent
TDMA_TOOLS = TOOLS / "tdma_ring_monitor"
for item in (TOOLS, HERE, TDMA_TOOLS):
    if str(item) not in sys.path:
        sys.path.insert(0, str(item))

from scpi_common.scpi_serial import open_serial_port  # noqa: E402
from sma_cable_delay_sweep import parse_frequencies  # noqa: E402
from sma_cable_five_board_validate import (  # noqa: E402
    MAX_DUTY_ERROR_PPM,
    MAX_FREQUENCY_ERROR_PPM,
    VALIDATOR_RESPONSE_FIELD_COUNT,
    parse_validator_response,
    phase_repeat_spread_mdeg,
    run_wire_order_preflight,
)
from sma_cable_wire_order import (  # noqa: E402
    command_ack_optional,
    query,
    query_ints,
)
from tdma_start_ring import discover  # noqa: E402

SOURCE_BOARD_NO = 1
VALIDATOR_BOARD_NO = 5
REFERENCE_CHANNEL = 1
MARK_WIDTH_NS = 16
APPOINTMENT_NS = 40


def summarize_appointment(records: list[dict[str, object]],
                          expected_count: int) -> dict[str, object]:
    valid_records = [record for record in records
                     if record["channels"][0]["valid"]]
    frequency_errors_ppm = []
    duty_errors_ppm = []
    for record in valid_records:
        channel = record["channels"][0]
        source_hz = int(record["source_actual_frequency_hz"])
        observed_hz = int(channel["observed_frequency_hz"])
        frequency_errors_ppm.append(
            abs(observed_hz - source_hz) * 1_000_000 // source_hz)
        duty_errors_ppm.append(
            abs(int(channel["duty_cycle_ppm"]) - 500_000))
    maximum_frequency_error_ppm = (
        max(frequency_errors_ppm) if frequency_errors_ppm else None)
    maximum_duty_error_ppm = max(duty_errors_ppm) if duty_errors_ppm else None
    passed = bool(
        len(valid_records) == expected_count and
        maximum_frequency_error_ppm is not None and
        maximum_frequency_error_ppm <= MAX_FREQUENCY_ERROR_PPM and
        maximum_duty_error_ppm is not None and
        maximum_duty_error_ppm <= MAX_DUTY_ERROR_PPM)
    return {
        "expected_capture_count": expected_count,
        "valid_capture_count": len(valid_records),
        "maximum_frequency_error_ppm": maximum_frequency_error_ppm,
        "maximum_duty_error_ppm": maximum_duty_error_ppm,
        "frequency_error_limit_ppm": MAX_FREQUENCY_ERROR_PPM,
        "duty_error_limit_ppm": MAX_DUTY_ERROR_PPM,
        "signal_quality_passed": passed,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address; repeat for NO1..NO5")
    parser.add_argument("--frequencies-mhz", default="2,4,6,8,10,12,14,16,18")
    parser.add_argument("--repeats", type=int, default=8)
    parser.add_argument("--capture-words", type=int, default=512)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.3)
    parser.add_argument("--signal-settle", type=float, default=0.05)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if len(args.board_id) != 5 or len(set(args.board_id)) != 5:
        parser.error("exactly five unique --board-id values are required")
    if args.repeats < 2:
        parser.error("repeats must be at least 2")
    if args.capture_words < 16 or args.capture_words > 512:
        parser.error("capture-words must be within 16..512")
    try:
        frequencies = parse_frequencies(args.frequencies_mhz)
    except ValueError as exc:
        parser.error(str(exc))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    wire_path, wire_result = run_wire_order_preflight(args)
    discovery_args = SimpleNamespace(
        board_ids=list(args.board_id),
        baud=args.baud,
        timeout=args.timeout,
        settle=args.settle,
        expected_build=None,
        short_open=False,
    )
    boards = discover(discovery_args)
    missing = set(args.board_id) - set(boards)
    if missing:
        raise RuntimeError(f"boards not discovered: {sorted(missing)}")

    identities: dict[int, dict[str, str]] = {}
    records: list[dict[str, object]] = []
    with ExitStack() as stack:
        connections = {}
        for board in boards.values():
            ser = stack.enter_context(open_serial_port(
                board.port, args.baud, args.timeout, args.settle))
            board_no = int(query(
                ser, "SYST:BOARD:NO?", args.timeout).strip('"'), 0)
            connections[board_no] = ser
            identities[board_no] = {
                "address": board.address,
                "port": board.port,
                "build": board.build,
            }
        if sorted(connections) != [1, 2, 3, 4, 5]:
            raise RuntimeError(
                f"BOARD:NO values must be [1,2,3,4,5], got {sorted(connections)}")

        source = connections[SOURCE_BOARD_NO]
        validator = connections[VALIDATOR_BOARD_NO]
        with ThreadPoolExecutor(max_workers=1) as executor:
            for frequency_hz in frequencies:
                for repeat in range(args.repeats):
                    validator_command = (
                        "READ:CAL:SMA:CABL:VAL? "
                        f"{frequency_hz},{args.capture_words},1")
                    capture_future = executor.submit(
                        query_ints,
                        validator,
                        validator_command,
                        VALIDATOR_RESPONSE_FIELD_COUNT,
                        args.timeout,
                    )
                    time.sleep(args.signal_settle)
                    try:
                        source_response = query_ints(
                            source,
                            f"CAL:SMA:CABL:SOUR:STAR {frequency_hz},1,1",
                            3,
                            args.timeout,
                        )
                        response = capture_future.result(
                            timeout=args.timeout + 1.0)
                    finally:
                        command_ack_optional(
                            source,
                            "CAL:SMA:CABL:SOUR:STOP",
                            args.timeout,
                        )
                    parsed = parse_validator_response(
                        ",".join(str(value) for value in response))
                    parsed["repeat"] = repeat
                    parsed["source_actual_frequency_hz"] = source_response[1]
                    records.append(parsed)

    expected_count = len(frequencies) * args.repeats
    summary = summarize_appointment(records, expected_count)
    repeatability = []
    for frequency_hz in frequencies:
        phases = [
            int(record["channels"][0]["phase_mdeg"])
            for record in records
            if int(record["requested_frequency_hz"]) == frequency_hz and
            record["channels"][0]["valid"]
        ]
        repeatability.append({
            "frequency_hz": frequency_hz,
            "valid_count": len(phases),
            "phase_spread_mdeg": phase_repeat_spread_mdeg(phases),
        })
    spreads = [int(item["phase_spread_mdeg"])
               for item in repeatability
               if item["phase_spread_mdeg"] is not None]
    summary["maximum_phase_spread_mdeg"] = max(spreads) if spreads else None

    output = {
        "schema": "sma-cable-delay/mark-appointment-v1",
        "wire_order_gate": {
            "passed": True,
            "evidence": str(wire_path),
            "inferred_source_to_input": wire_result["inferred_source_to_input"],
        },
        "identities": identities,
        "protocol": {
            "source": "NO1 OUT1",
            "validator": "NO5 IN1",
            "mark_width_ns": MARK_WIDTH_NS,
            "appointment_after_mark_ns": APPOINTMENT_NS,
            "sample_period_ns": 4,
        },
        "measurement_boundary": "relative to mark arrival on the same cable",
        "absolute_cable_delay_valid": False,
        "summary": summary,
        "repeatability": repeatability,
        "records": records,
    }
    result_path = args.output_dir / "sma_cable_appointment.json"
    result_path.write_text(
        json.dumps(output, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(result_path)
    return 0 if summary["signal_quality_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
