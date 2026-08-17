#!/usr/bin/env python3
"""Measure ring beacon TX/RX rates over a fixed window on both boards.

Samples SYSTem:REFMEM:SYNC:TDMA:STATus? on COM5 and COM6, parses the
110-field response, and reports deltas per second for the beacon legs and
core1 loop count (SYSTem:CORE?).
"""

from __future__ import annotations

import argparse
import sys
import time
from contextlib import ExitStack
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import open_serial_port, read_serial_line_idle  # noqa: E402

FIELD_TX = 99   # ring_idle_beacon_tx_count (0-based)
FIELD_RX = 100  # ring_idle_beacon_rx_count
FIELD_TXC = 107  # ring_adapter_tx_count
FIELD_RXC = 108  # ring_adapter_rx_count
FIELD_BAD = 109  # ring_adapter_rx_bad_count


def query(ser, cmd: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_serial_line_idle(ser, deadline)
        if line is None or line.startswith("["):
            continue
        return line.strip()
    return ""


def parse_fields(response: str) -> list[int]:
    values: list[int] = []
    for field in response.split(","):
        try:
            values.append(int(field.strip().strip('"'), 0))
        except ValueError:
            values.append(-1)
    return values


def sample(ser, timeout_s: float) -> dict:
    tdma = parse_fields(query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", timeout_s))
    core = parse_fields(query(ser, "SYSTem:CORE?", timeout_s))
    return {
        "tx": tdma[FIELD_TX] if len(tdma) > FIELD_TX else -1,
        "rx": tdma[FIELD_RX] if len(tdma) > FIELD_RX else -1,
        "txc": tdma[FIELD_TXC] if len(tdma) > FIELD_TXC else -1,
        "rxc": tdma[FIELD_RXC] if len(tdma) > FIELD_RXC else -1,
        "bad": tdma[FIELD_BAD] if len(tdma) > FIELD_BAD else -1,
        "core1": core[2] if len(core) > 2 else -1,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port-a", default="COM5")
    ap.add_argument("--port-b", default="COM6")
    ap.add_argument("--window-s", type=float, default=10.0)
    ap.add_argument("--timeout", type=float, default=3.0)
    args = ap.parse_args()

    with ExitStack() as stack:
        ser_a = stack.enter_context(open_serial_port(args.port_a, 115200,
                                                     args.timeout, 1.0))
        ser_b = stack.enter_context(open_serial_port(args.port_b, 115200,
                                                     args.timeout, 1.0))
        t0 = time.monotonic()
        a0 = sample(ser_a, args.timeout)
        b0 = sample(ser_b, args.timeout)
        time.sleep(args.window_s)
        a1 = sample(ser_a, args.timeout)
        b1 = sample(ser_b, args.timeout)
        t1 = time.monotonic()

    elapsed = t1 - t0
    for name, s0, s1 in (("A/COM5", a0, a1), ("B/COM6", b0, b1)):
        print(f"--- {name} (over ~{elapsed:.1f}s) ---")
        print(f"  beacon_tx     {s0['tx']:>9} -> {s1['tx']:>9}  +{s1['tx'] - s0['tx']:>6}  "
              f"{(s1['tx'] - s0['tx']) / elapsed:7.1f}/s")
        print(f"  beacon_rx     {s0['rx']:>9} -> {s1['rx']:>9}  +{s1['rx'] - s0['rx']:>6}  "
              f"{(s1['rx'] - s0['rx']) / elapsed:7.1f}/s")
        print(f"  adapter_tx    {s0['txc']:>9} -> {s1['txc']:>9}  +{s1['txc'] - s0['txc']:>6}  "
              f"{(s1['txc'] - s0['txc']) / elapsed:7.1f}/s")
        print(f"  adapter_rx    {s0['rxc']:>9} -> {s1['rxc']:>9}  +{s1['rxc'] - s0['rxc']:>6}  "
              f"{(s1['rxc'] - s0['rxc']) / elapsed:7.1f}/s")
        print(f"  rx_bad        {s0['bad']:>9} -> {s1['bad']:>9}")
        print(f"  core1_loop    {s0['core1']:>9} -> {s1['core1']:>9}  +{s1['core1'] - s0['core1']:>6}  "
              f"{(s1['core1'] - s0['core1']) / elapsed:7.1f}/s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
