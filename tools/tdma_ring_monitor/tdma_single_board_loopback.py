#!/usr/bin/env python3
"""Validate the product TDMA RJ45 output-to-input loopback on one board.

Connect the product board's output RJ45 to its input RJ45 with a network
cable before running this tool.  By default the tool prepares the resident
TDMA ring first, including STOP, LOCAL, ARM, clock TRAIN, and START, then uses
status queries to validate the electrical/data loopback.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from scpi_common.scpi_serial import open_serial_port  # noqa: E402
from scpi_common.board_identity import parse_idn_response  # noqa: E402
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


@contextmanager
def open_loopback_port(args: argparse.Namespace):
    last_error: Exception | None = None
    for attempt in range(6):
        try:
            with open_serial_port(args.port,
                                  args.baud,
                                  args.timeout,
                                  args.settle) as ser:
                yield ser
                return
        except (serial.SerialException, OSError) as exc:
            last_error = exc
            time.sleep(0.5 * (attempt + 1))
    if last_error is not None:
        raise last_error
    raise RuntimeError(f"failed to open {args.port}")


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
    parser.add_argument("--skip-ring-setup", action="store_true",
                        help="only query an already-running resident TDMA ring")
    parser.add_argument("--local-slot", type=int, default=0,
                        help="local logical slot used for single-board loopback")
    parser.add_argument("--train-cycles", type=int, default=4096,
                        help="clock-training cycles before START; 0 disables TRAIN")
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--start-wait", type=float, default=2.0)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def sample(ser, timeout_s: float) -> dict:
    tdma = sample_board(ser, timeout_s)
    phys = parse_named(query(ser, "SYSTem:SYNC:VDC:TDMA:PHYS?", timeout_s), PHYS_FIELDS)
    return {"tdma": tdma, "phys": phys}


def retryable_query(ser, command: str, timeout_s: float, attempts: int = 3) -> str:
    last_error: Exception | None = None
    for attempt in range(attempts):
        try:
            return query(ser, command, timeout_s)
        except (serial.SerialException, serial.SerialTimeoutException, OSError) as exc:
            last_error = exc
            time.sleep(0.5 * (attempt + 1))
    if last_error is not None:
        raise last_error
    return "<timeout>"


def ring_action(ser, command: str, timeout_s: float) -> str:
    response = retryable_query(ser, command, timeout_s)
    header = command.strip().split(maxsplit=1)[0].upper()
    ack_only = {
        "SYSTEM:TDMA:RING:STOP", "SYST:TDMA:RING:STOP",
        "SYSTEM:TDMA:RING:ARM", "SYST:TDMA:RING:ARM",
        "SYSTEM:TDMA:RING:START", "SYST:TDMA:RING:START",
    }
    if response == "<timeout>" and header in ack_only:
        return "OK(no payload; verified by state readback)"
    return response


def field(data: dict, index: int) -> int:
    values = data["tdma"]
    return values[index] if index < len(values) else -1


def delta(first: dict, last: dict, index: int) -> int:
    return field(last, index) - field(first, index)


def phys_delta(first: dict, last: dict, name: str) -> int:
    return last["phys"].get(name, -1) - first["phys"].get(name, -1)


def wait_for_ring_state(ser,
                        timeout_s: float,
                        wait_s: float,
                        *,
                        adapter_started: bool,
                        data_running: bool) -> dict:
    deadline = time.monotonic() + wait_s
    last = {"tdma": [], "phys": {}}
    while time.monotonic() < deadline:
        try:
            last = sample(ser, timeout_s)
        except (serial.SerialException, serial.SerialTimeoutException, OSError):
            time.sleep(0.25)
            continue
        enabled_ok = field(last, RING_ENABLED) == 1
        adapter_ok = field(last, RING_ADAPTER_STARTED) == 1
        up_ok = field(last, RING_UP_RUNNING) == 1
        down_ok = field(last, RING_DOWN_RUNNING) == 1
        if enabled_ok and (not adapter_started or adapter_ok) and (
            not data_running or (up_ok and down_ok)):
            return last
        time.sleep(0.05)
    return last


def prepare_single_board_ring(ser, args: argparse.Namespace) -> dict:
    if args.local_slot < 0:
        raise SystemExit("--local-slot must be non-negative")
    if args.train_cycles < 0 or args.train_cycles > 65536:
        raise SystemExit("--train-cycles must be in [0, 65536]")
    if args.train_cycles != 0 and args.train_cycles % 8 != 0:
        raise SystemExit("--train-cycles must be 0 or an 8-cycle multiple")

    steps: list[dict[str, str]] = []
    for command in (
        "SYSTem:TDMA:RING:STOP",
        f"SYSTem:TDMA:RING:LOCAL {args.local_slot}",
        "SYSTem:TDMA:RING:ARM",
    ):
        steps.append({"command": command,
                      "response": ring_action(ser, command, args.timeout)})
        time.sleep(0.2)

    armed = wait_for_ring_state(ser,
                                args.timeout,
                                args.arm_wait,
                                adapter_started=True,
                                data_running=False)

    if args.train_cycles != 0:
        command = f"SYSTem:TDMA:RING:TRAIN {args.train_cycles}"
        steps.append({"command": command,
                      "response": ring_action(ser, command, args.timeout)})
        time.sleep(0.2)

    command = "SYSTem:TDMA:RING:START"
    steps.append({"command": command,
                  "response": ring_action(ser, command, args.timeout)})
    started = wait_for_ring_state(ser,
                                  args.timeout,
                                  args.start_wait,
                                  adapter_started=True,
                                  data_running=True)
    return {
        "enabled": True,
        "local_slot": args.local_slot,
        "train_cycles": args.train_cycles,
        "steps": steps,
        "armed": {
            "ring_enabled": field(armed, RING_ENABLED),
            "adapter_started": field(armed, RING_ADAPTER_STARTED),
            "up_running": field(armed, RING_UP_RUNNING),
            "down_running": field(armed, RING_DOWN_RUNNING),
        },
        "started": {
            "ring_enabled": field(started, RING_ENABLED),
            "adapter_started": field(started, RING_ADAPTER_STARTED),
            "up_running": field(started, RING_UP_RUNNING),
            "down_running": field(started, RING_DOWN_RUNNING),
        },
    }


def main() -> int:
    args = parse_args()
    if args.duration_s <= 0.0 or args.poll_interval_s <= 0.0:
        raise SystemExit("duration and poll interval must be positive")

    samples: list[dict] = []
    build = ""
    ring_setup: dict = {"enabled": False}
    flight_tx_response = ""
    flight_rx_response = ""
    with open_loopback_port(args) as ser:
        identity = parse_idn_response(retryable_query(ser, "*IDN?", args.timeout))
        build = retryable_query(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')
        if args.expected_build and build != args.expected_build:
            raise SystemExit(f"build mismatch: {build} != {args.expected_build}")

        if not args.skip_ring_setup:
            ring_setup = prepare_single_board_ring(ser, args)
            for _ in range(8):
                drained = retryable_query(
                    ser, "SYSTem:TDMA:FLIGHT:RX?", args.timeout
                )
                if drained.startswith('"EMPTY"'):
                    break
            flight_tx_response = retryable_query(
                ser,
                "SYSTem:TDMA:FLIGHT:TX 32,165,1,1,1",
                args.timeout,
            )
            deadline = time.monotonic() + max(args.start_wait, 2.0)
            while time.monotonic() < deadline:
                flight_rx_response = retryable_query(
                    ser, "SYSTem:TDMA:FLIGHT:RX?", args.timeout
                )
                if not flight_rx_response.startswith("EMPTY"):
                    break
                time.sleep(0.1)

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
    if flight_tx_response:
        notes.append(f"flight tx published: {flight_tx_response}")
    if flight_rx_response:
        notes.append(f"flight rx observed: {flight_rx_response}")
    if not args.skip_ring_setup:
        if not flight_tx_response.startswith('"OK"'):
            failures.append(f"flight tx did not publish: {flight_tx_response}")
        if not flight_rx_response.startswith('"RX"'):
            failures.append(f"flight rx did not return a mirrored descriptor: {flight_rx_response}")

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
        "idn": identity.idn,
        "board_address": identity.address,
        "build": build,
        "duration_s": args.duration_s,
        "valid_sample_count": len(samples),
        "passed": not failures,
        "electrical_data_loopback_passed": not failures,
        "formal_feedback_evidence": formal_feedback_evidence,
        "ring_setup": ring_setup,
        "flight_tx_response": flight_tx_response,
        "flight_rx_response": flight_rx_response,
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
