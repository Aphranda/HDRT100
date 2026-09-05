#!/usr/bin/env python3
"""Prove that analyzer ARM/STOP does not perturb the running TDMA ring."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from scpi_common.scpi_serial import open_serial_port, read_scpi_response  # noqa: E402
from tdma_field_parse import parse_status_named  # noqa: E402


MONITORED_FIELDS = (
    "ring_adapter_rx_bad_count",
    "ring_adapter_rx_transport_bad_count",
    "ring_adapter_rx_schedule_bad_count",
    "ring_adapter_rx_profile_bad_count",
    "ring_adapter_last_error",
)
PROGRESS_FIELDS = (
    "ring_adapter_rx_count",
    "ring_up_tx_sequence",
    "ring_down_rx_sequence",
)


def query(ser: Any, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, command, timeout_s, require_match=True).strip()


def sample_tdma(ser: Any, timeout_s: float) -> dict[str, Any]:
    raw = query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", timeout_s)
    if raw == "<timeout>":
        raise RuntimeError("TDMA status query timed out")
    return {"raw": raw, "fields": parse_status_named(raw)}


def sample_analyzer(ser: Any, timeout_s: float) -> dict[str, Any]:
    raw = query(ser, "REALtime:IO:ANALyzer:STATe?", timeout_s)
    if raw == "<timeout>":
        raise RuntimeError("analyzer status query timed out")
    fields = [int(part.strip().strip('"')) for part in raw.split(",")]
    if len(fields) < 4:
        raise RuntimeError(f"analyzer status schema too short: {raw!r}")
    return {"raw": raw, "initialized": fields[0], "active": fields[1],
            "state": fields[2], "mode": fields[3],
            "capture_sequence": fields[5]}


def compare(before: dict[str, Any], after: dict[str, Any], arm: str, stop: str,
            elapsed_s: float, baseline: tuple[dict[str, Any], dict[str, Any]] | None = None,
            baseline_allowance: int = 1) -> dict[str, Any]:
    before_fields = before["fields"]
    after_fields = after["fields"]
    deltas = {name: after_fields[name] - before_fields[name]
              for name in MONITORED_FIELDS + PROGRESS_FIELDS}
    baseline_deltas = {name: 0 for name in MONITORED_FIELDS}
    if baseline is not None:
        baseline_deltas = {
            name: baseline[1]["fields"][name] - baseline[0]["fields"][name]
            for name in MONITORED_FIELDS
        }
    checks = {
        "arm_ack": arm in {"1", "OK", '"OK"'},
        "stop_ack": stop in {"1", "OK", '"OK"'},
        "tdma_status_schema": len(before_fields) == len(after_fields),
        "ring_progressed": any(deltas[name] > 0 for name in PROGRESS_FIELDS),
        "no_analyzer_induced_bad_growth": all(
            deltas[name] <= baseline_deltas[name] + baseline_allowance
            for name in MONITORED_FIELDS),
        "ring_still_running": (
            before_fields["ring_up_running"] == after_fields["ring_up_running"] and
            before_fields["ring_down_running"] == after_fields["ring_down_running"]
        ),
    }
    return {
        "passed": all(checks.values()),
        "checks": checks,
        "elapsed_s": elapsed_s,
        "deltas": deltas,
        "baseline_deltas": baseline_deltas,
        "before": before,
        "after": after,
        "analyzer_arm_response": arm,
        "analyzer_stop_response": stop,
    }


def run(port: str, timeout_s: float, dwell_s: float, baseline_s: float,
        mode: str = "edge") -> dict[str, Any]:
    if mode == "triggered":
        # GPIO25 is the product TDMA TX clock pad.  The analyzer remains a
        # read-only observer; the running ring supplies the physical edge.
        trigger_mask = 1 << 25
        arm_command = (
            "REALtime:IO:ANALyzer:TRIGger:ARM "
            f"0,1000,64,5000000,1,8,8,2,{trigger_mask},{trigger_mask},0"
        )
        expected_mode = 3
    else:
        arm_command = "REALtime:IO:ANALyzer:EDGE:ARM 0,64,5000000,1"
        expected_mode = 2
    with open_serial_port(port, 115200, timeout_s, 1.0, read_timeout_s=0.2) as ser:
        baseline_before = sample_tdma(ser, timeout_s)
        time.sleep(baseline_s)
        baseline_after = sample_tdma(ser, timeout_s)
        before = sample_tdma(ser, timeout_s)
        start = time.monotonic()
        arm = query(ser, arm_command, timeout_s)
        analyzer_after_arm = sample_analyzer(ser, timeout_s)
        time.sleep(dwell_s)
        stop = query(ser, "REALtime:IO:ANALyzer:STOP", timeout_s)
        analyzer_after_stop = sample_analyzer(ser, timeout_s)
        arm2 = query(ser, arm_command, timeout_s)
        analyzer_second_arm = sample_analyzer(ser, timeout_s)
        stop2 = query(ser, "REALtime:IO:ANALyzer:STOP", timeout_s)
        analyzer_second_stop = sample_analyzer(ser, timeout_s)
        after = sample_tdma(ser, timeout_s)
        elapsed = time.monotonic() - start
    result = compare(before, after, arm, stop, elapsed,
                     (baseline_before, baseline_after))
    result["analyzer_after_arm"] = analyzer_after_arm
    result["analyzer_after_stop"] = analyzer_after_stop
    result["analyzer_second_arm"] = analyzer_second_arm
    result["analyzer_second_stop"] = analyzer_second_stop
    result["analyzer_second_arm_response"] = arm2
    result["analyzer_second_stop_response"] = stop2
    result["checks"]["requested_mode_observed"] = (
        analyzer_after_arm["mode"] == expected_mode and
        (analyzer_after_arm["active"] == 1 or
         analyzer_after_arm["state"] == 3))
    result["checks"]["capture_sequence_advances"] = (
        analyzer_after_arm["capture_sequence"] !=
        analyzer_second_arm["capture_sequence"] and
        analyzer_second_arm["capture_sequence"] != 0)
    result["passed"] = all(result["checks"].values())
    result["mode"] = mode
    result["arm_command"] = arm_command
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="running TDMA board, for example COM5")
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--dwell-s", type=float, default=2.0)
    parser.add_argument("--baseline-s", type=float, default=2.0)
    parser.add_argument("--mode", choices=("edge", "triggered"), default="edge")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    result = run(args.port, args.timeout, args.dwell_s, args.baseline_s,
                 args.mode)
    text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.out is None:
        sys.stdout.write(text)
    else:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8", newline="\n")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
