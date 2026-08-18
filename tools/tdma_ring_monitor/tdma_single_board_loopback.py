#!/usr/bin/env python3
"""Validate the product TDMA RJ45 output-to-input loopback on one board.

Connect the product board's output RJ45 to its input RJ45 with a network
cable before running this tool.  The tool is read-only: resident core1 TDMA
traffic supplies CS/clock/data and only status queries are sent over SCPI.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from scpi_common.scpi_serial import open_serial_port  # noqa: E402
from ring_rate_measure import PHYS_FIELDS, parse_named  # noqa: E402
from tdma_ring_monitor import (  # noqa: E402
    RING_ADAPTER_LAST_ERROR,
    RING_ADAPTER_RX_BAD_COUNT,
    RING_ADAPTER_RX_COUNT,
    RING_ADAPTER_STARTED,
    RING_ADAPTER_TX_COUNT,
    RING_DOWN_RUNNING,
    RING_DOWN_RX_SEQUENCE,
    RING_ENABLED,
    RING_LAST_ERROR,
    RING_SEQ,
    RING_UP_RUNNING,
    RING_UP_TX_SEQUENCE,
    SIMULTANEOUS,
    query,
    sample_board,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="product-board USB CDC port, normally COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--duration-s", type=float, default=15.0)
    parser.add_argument("--poll-interval-s", type=float, default=0.5)
    parser.add_argument("--expected-build")
    parser.add_argument("--expected-baud-hz", type=int, default=10000000)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def sample(ser, timeout_s: float) -> dict:
    tdma = sample_board(ser, timeout_s)
    phys = parse_named(query(ser, "SYSTem:SYNC:VDC:TDMA:PHYS?", timeout_s), PHYS_FIELDS)
    return {"tdma": tdma, "phys": phys}


def field(data: dict, index: int) -> int:
    values = data["tdma"]
    return values[index] if index < len(values) else -1


def delta(first: dict, last: dict, index: int) -> int:
    return field(last, index) - field(first, index)


def phys_delta(first: dict, last: dict, name: str) -> int:
    return last["phys"].get(name, -1) - first["phys"].get(name, -1)


def main() -> int:
    args = parse_args()
    if args.duration_s <= 0.0 or args.poll_interval_s <= 0.0:
        raise SystemExit("duration and poll interval must be positive")

    samples: list[dict] = []
    build = ""
    with open_serial_port(args.port, args.baud, args.timeout, args.settle) as ser:
        build = query(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')
        if args.expected_build and build != args.expected_build:
            raise SystemExit(f"build mismatch: {build} != {args.expected_build}")

        deadline = time.monotonic() + args.duration_s
        while time.monotonic() < deadline:
            try:
                samples.append(sample(ser, args.timeout))
            except AssertionError:
                pass
            time.sleep(args.poll_interval_s)

    failures: list[str] = []
    if len(samples) < 2:
        failures.append("fewer than two valid 110-field TDMA samples")
        first = last = {"tdma": [], "phys": {}}
    else:
        first, last = samples[0], samples[-1]

    expected_phys = {
        "baud_hz": args.expected_baud_hz,
        "tx_csn_pin": 26,
        "tx_sck_pin": 25,
        "tx_pin": 29,
        "rx_csn_pin": 27,
        "rx_sck_pin": 28,
        "rx_pin": 24,
    }
    for name, expected in expected_phys.items():
        actual = last["phys"].get(name, -1)
        if actual != expected:
            failures.append(f"physical {name}={actual}, expected {expected}")

    required_fields = {
        "ring_enabled": (RING_ENABLED, 1),
        "adapter_started": (RING_ADAPTER_STARTED, 1),
        "up_running": (RING_UP_RUNNING, 1),
        "down_running": (RING_DOWN_RUNNING, 1),
        "adapter_last_error": (RING_ADAPTER_LAST_ERROR, 0),
    }
    for name, (index, expected) in required_fields.items():
        actual = field(last, index)
        if actual != expected:
            failures.append(f"{name}={actual}, expected {expected}")

    # Electrical/data loopback and formal HAOFV feedback evidence are separate
    # gates.  TIMESTAMP_MISSING is expected until reference TX and feedback RX
    # timestamps are latched at the PIO/DMA edge.  Never require or fabricate
    # simultaneous evidence for this product-board wiring check.
    simultaneous_feedback = field(last, SIMULTANEOUS)
    ring_last_error = field(last, RING_LAST_ERROR)
    if ring_last_error not in (0, 5):
        failures.append(
            f"ring_last_error={ring_last_error}, expected NONE(0) or TIMESTAMP_MISSING(5)"
        )
    formal_feedback_evidence = (
        simultaneous_feedback == 1 and ring_last_error == 0
    )
    notes: list[str] = []
    if not formal_feedback_evidence:
        notes.append(
            "formal feedback timestamp evidence pending: "
            f"simultaneous={simultaneous_feedback}, ring_last_error={ring_last_error}"
        )

    counter_deltas = {
        "ring_seq": delta(first, last, RING_SEQ),
        "up_tx_sequence": delta(first, last, RING_UP_TX_SEQUENCE),
        "down_rx_sequence": delta(first, last, RING_DOWN_RX_SEQUENCE),
        "adapter_tx_count": delta(first, last, RING_ADAPTER_TX_COUNT),
        "adapter_rx_count": delta(first, last, RING_ADAPTER_RX_COUNT),
    }
    for name, value in counter_deltas.items():
        if value <= 0:
            failures.append(f"{name} did not advance: delta={value}")

    bad_deltas = {
        "adapter_rx_bad_count": delta(first, last, RING_ADAPTER_RX_BAD_COUNT),
        "phys_rx_bad_count": phys_delta(first, last, "rx_bad_count"),
        "phys_rx_magic_fail_count": phys_delta(first, last, "rx_magic_fail_count"),
        "phys_rx_ring_overrun_count": phys_delta(first, last, "rx_ring_overrun_count"),
    }
    for name, value in bad_deltas.items():
        if value != 0:
            failures.append(f"{name} grew by {value}")

    summary = {
        "port": args.port,
        "build": build,
        "duration_s": args.duration_s,
        "valid_sample_count": len(samples),
        "passed": not failures,
        "electrical_data_loopback_passed": not failures,
        "formal_feedback_evidence": formal_feedback_evidence,
        "last": {
            name: field(last, index)
            for name, (index, _) in required_fields.items()
        },
        "physical": {name: last["phys"].get(name, -1) for name in expected_phys},
        "counter_deltas": counter_deltas,
        "bad_deltas": bad_deltas,
        "ring_last_error": ring_last_error,
        "simultaneous_feedback": simultaneous_feedback,
        "notes": notes,
        "failures": failures,
    }

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = (args.out_dir or ROOT / "build-rtos-multicore-smoke" /
               f"tdma_single_loopback_{stamp}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    text_lines = ["PASS" if not failures else "FAIL"]
    text_lines.extend(f"- NOTE: {item}" for item in notes)
    text_lines.extend(f"- {item}" for item in failures)
    (out_dir / "summary.txt").write_text("\n".join(text_lines) + "\n", encoding="utf-8")

    print(f"out_dir={out_dir}")
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
