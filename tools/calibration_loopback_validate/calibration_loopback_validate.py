#!/usr/bin/env python3
"""Run the single-board PIO calibration loopback smoke on a USB CDC port.

The tool only orchestrates commands and reads the firmware snapshot.  It does
not reconstruct timestamps or calculate calibration on the host.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import open_serial_port, read_scpi_response


def query(ser, command: str, timeout_s: float) -> str:
    ser.write((command + "\n").encode("ascii"))
    return read_scpi_response(ser, command, timeout_s, require_match=True)


def command(ser, text: str, timeout_s: float) -> str:
    return query(ser, text, timeout_s)


def command_no_data(ser, text: str, timeout_s: float) -> str:
    """Send a command whose firmware ACK is intentionally filtered as noise."""
    response = query(ser, text, timeout_s)
    return "<ack/no-data>" if response == "<timeout>" else response


def parse_uints(text: str) -> list[int]:
    return [int(part.strip().strip('"')) for part in text.split(",")]


def snapshot_passed(snapshot: list[int], words: int, baseline_epoch: int) -> bool:
    return (
        len(snapshot) >= 18
        and snapshot[0] == 0
        and snapshot[1] == 1
        and snapshot[2] > 0
        and snapshot[3] > 0
        and snapshot[4] == words
        and (snapshot[5] & 0x0F) == 0x0F
        and (snapshot[6] & 0x07) == 0x07
        and snapshot[7] == 0
        and snapshot[8] != baseline_epoch
        and snapshot[9] > 0
        and snapshot[9] <= snapshot[10] <= snapshot[11] <= snapshot[12]
        and snapshot[13] == 1
        and snapshot[14] > 0
        and snapshot[15] > 0
        and snapshot[16] >= 0
        and snapshot[17] == 0
    )


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []
    run_results: list[dict[str, object]] = []
    with open_serial_port(args.port, args.baud, args.timeout_s, args.settle_s) as ser:
        idn = query(ser, "*IDN?", args.timeout_s)
        build = query(ser, "SYST:FW:BUILD?", args.timeout_s).strip('"')
        records.extend((
            {"command": "*IDN?", "response": idn},
            {"command": "SYST:FW:BUILD?", "response": build},
        ))
        records.append({"command": "SYST:TDMA:RING:STOP",
                        "response": command_no_data(
                            ser, "SYST:TDMA:RING:STOP", args.timeout_s)})
        previous_epoch: int | None = None
        for run_index in range(args.runs):
            baseline_response = query(ser, "READ:CAL:LOOP?", args.timeout_s)
            baseline = parse_uints(baseline_response)
            baseline_epoch = baseline[8] if len(baseline) >= 9 else 0
            records.append({"run": run_index + 1,
                            "command": "READ:CAL:LOOP? (baseline)",
                            "response": baseline_response})
            start = command(ser, f"CAL:LOOP:START {args.words}", args.timeout_s)
            records.append({"run": run_index + 1,
                            "command": "CAL:LOOP:START", "response": start})
            deadline = time.monotonic() + args.duration_s
            snapshot: list[int] = []
            while time.monotonic() < deadline:
                response = query(ser, "READ:CAL:LOOP?", args.timeout_s)
                values = parse_uints(response)
                records.append({"run": run_index + 1,
                                "command": "READ:CAL:LOOP?", "response": response})
                if len(values) >= 9:
                    snapshot = values
                    if values[1] != 0 and values[8] != baseline_epoch:
                        break
                time.sleep(args.poll_s)
            epoch_unique = (
                len(snapshot) >= 9 and
                (previous_epoch is None or snapshot[8] != previous_epoch)
            )
            run_passed = snapshot_passed(
                snapshot, args.words, baseline_epoch) and epoch_unique
            run_results.append({
                "run": run_index + 1,
                "baseline_epoch": baseline_epoch,
                "snapshot": snapshot,
                "epoch_unique": epoch_unique,
                "passed": run_passed,
            })
            if len(snapshot) >= 9:
                previous_epoch = snapshot[8]
            records.append({"run": run_index + 1,
                            "command": "CAL:LOOP:STOP",
                            "response": command_no_data(
                                ser, "CAL:LOOP:STOP", args.timeout_s)})
            time.sleep(args.inter_run_s)
        error_response = query(ser, "SYST:ERR?", args.timeout_s)
        records.append({"command": "SYST:ERR?", "response": error_response})

    valid_snapshots = [
        result["snapshot"] for result in run_results
        if len(result["snapshot"]) >= 18
    ]
    diagnostic_ranges = {}
    for name, index in (("residence_ns", 14),
                        ("raw_path_sum_ns", 15),
                        ("delay_estimate_ns", 16)):
        values = [snapshot[index] for snapshot in valid_snapshots]
        diagnostic_ranges[name] = {
            "min": min(values) if values else None,
            "max": max(values) if values else None,
        }
    passed = (
        len(run_results) == args.runs
        and all(result["passed"] for result in run_results)
        and error_response == '0,"No error"'
    )
    summary = {
        "port": args.port,
        "idn": idn,
        "build": build,
        "words": args.words,
        "runs_requested": args.runs,
        "runs_passed": sum(1 for result in run_results if result["passed"]),
        "run_results": run_results,
        "diagnostic_ranges": diagnostic_ranges,
        "final_error": error_response,
        "passed": passed,
        "evidence_boundary": "REFERENCE_LOOPBACK + DIAGNOSTIC_ONLY; not active calibration",
        "records": records,
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="USB CDC port, for example COM8")
    parser.add_argument("--words", type=int, default=128)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--duration-s", type=float, default=5.0)
    parser.add_argument("--poll-s", type=float, default=0.1)
    parser.add_argument("--inter-run-s", type=float, default=0.05)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout-s", type=float, default=2.0)
    parser.add_argument("--settle-s", type=float, default=1.0)
    parser.add_argument("--out-dir", default="build-calibration-loopback")
    args = parser.parse_args()
    if args.words <= 0 or args.runs <= 0 or args.inter_run_s < 0:
        parser.error("--words/--runs must be positive and --inter-run-s non-negative")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
