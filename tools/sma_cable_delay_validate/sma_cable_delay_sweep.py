#!/usr/bin/env python3
"""Sweep the dynamic SMA cable-delay PIO persona on one board.

For a coarse equal-cable bring-up, connect one representative cable from the
selected local SMA_OUT to the selected SMA_IN. The result is a measured path
(local output + cable + input channel), not cable-only delay.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
HERE = Path(__file__).resolve().parent
for item in (TOOLS, HERE):
    if str(item) not in sys.path:
        sys.path.insert(0, str(item))

from scpi_common.scpi_serial import open_serial_port, read_scpi_response  # noqa: E402
from sma_cable_delay_analyze import PhasePoint, fit_phase  # noqa: E402

DEFAULT_FREQUENCIES_MHZ = "2,4,6,8,10,12,14,16,18"
RESPONSE_FIELD_COUNT = 17


def parse_frequencies(text: str) -> list[int]:
    values = [round(float(item.strip()) * 1_000_000)
              for item in text.split(",") if item.strip()]
    if len(values) < 3 or any(value <= 0 or value > 40_000_000 for value in values):
        raise ValueError("at least three frequencies in (0, 40] MHz are required")
    if any(current <= previous for previous, current in zip(values, values[1:])):
        raise ValueError("frequencies must be strictly increasing")
    return values


def parse_response(text: str) -> dict[str, object]:
    try:
        fields = [int(part.strip().strip('"'), 0) for part in text.split(",")]
    except ValueError as exc:
        raise ValueError(f"malformed SMA cable response: {text!r}") from exc
    if len(fields) != RESPONSE_FIELD_COUNT:
        raise ValueError(
            f"SMA cable response has {len(fields)} fields, expected {RESPONSE_FIELD_COUNT}")
    channels = []
    for channel in range(4):
        base = 5 + channel * 3
        channels.append({
            "channel": channel + 1,
            "valid": fields[base] != 0,
            "phase_mdeg": fields[base + 1],
            "rising_edge_count": fields[base + 2],
        })
    return {
        "requested_frequency_hz": fields[0],
        "actual_frequency_hz": fields[1],
        "sample_rate_hz": fields[2],
        "period_samples": fields[3],
        "capture_word_count": fields[4],
        "channels": channels,
    }


def query(ser, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    response = read_scpi_response(ser, command, timeout_s, require_match=True)
    if response == "<timeout>":
        raise RuntimeError(f"timeout waiting for {command}")
    return response


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output-channel", type=int, default=4)
    parser.add_argument("--input-channel", type=int, required=True)
    parser.add_argument("--frequencies-mhz", default=DEFAULT_FREQUENCIES_MHZ)
    parser.add_argument("--capture-words", type=int, default=256)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.5)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if args.output_channel not in range(1, 5) or args.input_channel not in range(1, 5):
        parser.error("input/output channel must be within 1..4")
    if args.capture_words < 16 or args.capture_words > 512:
        parser.error("capture-words must be within 16..512")
    try:
        frequencies = parse_frequencies(args.frequencies_mhz)
    except ValueError as exc:
        parser.error(str(exc))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []
    with open_serial_port(args.port, args.baud, args.timeout, args.settle) as ser:
        identity = query(ser, "*IDN?", args.timeout)
        build_id = query(ser, "SYST:FW:BUILD?", args.timeout).strip('"')
        coarse = query(ser, "READ:CAL:SMA:CABL:COAR?", args.timeout)
        for frequency_hz in frequencies:
            command = (
                "READ:CAL:SMA:CABL:PHAS? "
                f"{frequency_hz},{args.output_channel},{args.capture_words}")
            result = parse_response(query(ser, command, args.timeout))
            result["command"] = command
            records.append(result)

    selected_points: list[PhasePoint] = []
    csv_path = args.output_dir / "sma_cable_delay_phase.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=(
            "channel", "frequency_hz", "phase_mdeg", "rising_edge_count",
            "sample_rate_hz", "period_samples", "capture_word_count"))
        writer.writeheader()
        for record in records:
            channel = record["channels"][args.input_channel - 1]
            if not channel["valid"]:
                continue
            row = {
                "channel": args.input_channel - 1,
                "frequency_hz": record["actual_frequency_hz"],
                "phase_mdeg": channel["phase_mdeg"],
                "rising_edge_count": channel["rising_edge_count"],
                "sample_rate_hz": record["sample_rate_hz"],
                "period_samples": record["period_samples"],
                "capture_word_count": record["capture_word_count"],
            }
            writer.writerow(row)
            selected_points.append(PhasePoint(int(row["frequency_hz"]),
                                              int(row["phase_mdeg"])))

    if len(selected_points) < 3:
        raise RuntimeError(
            f"input channel {args.input_channel} produced fewer than three valid points")
    fit = fit_phase(selected_points)
    output = {
        "schema": "sma-cable-delay/self-loop-coarse-v1",
        "identity": identity,
        "build_id": build_id,
        "firmware_coarse_baseline": coarse,
        "output_channel": args.output_channel,
        "input_channel": args.input_channel,
        "fit": fit,
        "measured_path_delay_ps": fit["total_delay_ps"],
        "measurement_boundary": "local SMA output + cable + validator SMA input",
        "cable_only_delay_valid": False,
        "equal_cable_relative_baseline_ps": [0, 0, 0, 0],
        "usage": "validator-only; do not load into TDMA loop/link offset matrix",
        "records": records,
    }
    result_path = args.output_dir / "sma_cable_delay_sweep.json"
    result_path.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n",
                           encoding="utf-8")
    print(result_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
