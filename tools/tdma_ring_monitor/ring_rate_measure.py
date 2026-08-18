#!/usr/bin/env python3
"""Measure ring beacon TX/RX rates over a fixed window on both boards.

Samples SYSTem:REFMEM:SYNC:TDMA:STATus? on COM5 and COM6, parses the
TDMA response plus SYSTem:SYNC:VDC:TDMA:PHYS?, and reports deltas per second
for adapter and physical-layer counters.
"""

from __future__ import annotations

import argparse
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from scpi_common.scpi_serial import open_serial_port, read_scpi_response  # noqa: E402
from tdma_field_parse import FIELDS as TDMA_FIELDS  # noqa: E402

FIELD_TX = 99   # ring_idle_beacon_tx_count (0-based)
FIELD_RX = 100  # ring_idle_beacon_rx_count
FIELD_TXC = 107  # ring_adapter_tx_count
FIELD_RXC = 108  # ring_adapter_rx_count
FIELD_BAD = 109  # ring_adapter_rx_bad_count

PHYS_FIELDS = [
    "armed", "role", "baud_hz", "tx_count", "rx_count", "rx_bad_count",
    "tx_busy_count", "rx_partial_count", "rx_stall_count",
    "tx_timeout_count", "last_error", "last_rx_size", "tx_sck_pin",
    "tx_pin", "rx_sck_pin", "rx_pin", "last_bad_header0",
    "last_bad_header1", "last_bad_header2", "last_bad_header3",
    "last_bad_words", "rx_busy_count", "rx_magic_fail_count",
    "rx_busy_word0", "rx_busy_word1", "rx_busy_word2", "rx_busy_word3",
    "rx_busy_moved", "rx_magic_at_zero", "rx_magic_at_shift",
    "tx_csn_pin", "rx_csn_pin", "rx_ring_overrun_count",
    "rx_dma_produced_words", "rx_scan_produced_words", "rx_dma_write_index",
    "rx_dma_channel",
]


def query(ser, cmd: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, cmd, timeout_s, require_match=False).strip()


def parse_fields(response: str) -> list[int]:
    values: list[int] = []
    for field in response.split(","):
        try:
            values.append(int(field.strip().strip('"'), 0))
        except ValueError:
            values.append(-1)
    return values


def parse_named(response: str, names: list[str]) -> dict[str, int]:
    values = parse_fields(response)
    return {
        name: values[index] if index < len(values) else -1
        for index, name in enumerate(names)
    }


def sample(ser, timeout_s: float) -> dict:
    tdma_response = query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", timeout_s)
    tdma = parse_fields(tdma_response)
    core = parse_fields(query(ser, "SYSTem:CORE?", timeout_s))
    phys = parse_named(query(ser, "SYSTem:SYNC:VDC:TDMA:PHYS?", timeout_s),
                       PHYS_FIELDS)
    return {
        "tx": tdma[FIELD_TX] if len(tdma) > FIELD_TX else -1,
        "rx": tdma[FIELD_RX] if len(tdma) > FIELD_RX else -1,
        "txc": tdma[FIELD_TXC] if len(tdma) > FIELD_TXC else -1,
        "rxc": tdma[FIELD_RXC] if len(tdma) > FIELD_RXC else -1,
        "bad": tdma[FIELD_BAD] if len(tdma) > FIELD_BAD else -1,
        "tdma_field_count": len(tdma),
        "ring_local_slot_id": tdma[TDMA_FIELDS.index("ring_local_slot_id")]
        if len(tdma) > TDMA_FIELDS.index("ring_local_slot_id") else -1,
        "ring_up_running": tdma[TDMA_FIELDS.index("ring_up_running")]
        if len(tdma) > TDMA_FIELDS.index("ring_up_running") else -1,
        "ring_down_running": tdma[TDMA_FIELDS.index("ring_down_running")]
        if len(tdma) > TDMA_FIELDS.index("ring_down_running") else -1,
        "ring_last_error": tdma[TDMA_FIELDS.index("ring_last_error")]
        if len(tdma) > TDMA_FIELDS.index("ring_last_error") else -1,
        "phys": phys,
        "core1": core[2] if len(core) > 2 else -1,
    }


def sample_pair(ser_a, ser_b, timeout_s: float) -> tuple[dict, dict]:
    with ThreadPoolExecutor(max_workers=2) as pool:
        fut_a = pool.submit(sample, ser_a, timeout_s)
        fut_b = pool.submit(sample, ser_b, timeout_s)
        return fut_a.result(), fut_b.result()


def delta(s0: dict, s1: dict, key: str) -> int:
    return s1[key] - s0[key]


def rate(s0: dict, s1: dict, key: str, elapsed: float) -> float:
    return delta(s0, s1, key) / elapsed


def phys_delta(s0: dict, s1: dict, key: str) -> int:
    return s1["phys"].get(key, -1) - s0["phys"].get(key, -1)


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
        a0, b0 = sample_pair(ser_a, ser_b, args.timeout)
        t0 = time.monotonic()
        time.sleep(args.window_s)
        a1, b1 = sample_pair(ser_a, ser_b, args.timeout)
        t1 = time.monotonic()

    elapsed = t1 - t0
    for name, s0, s1 in ((f"A/{args.port_a}", a0, a1),
                         (f"B/{args.port_b}", b0, b1)):
        print(f"--- {name} (over ~{elapsed:.1f}s) ---")
        print(f"  slot/up/down/err {s1['ring_local_slot_id']}/"
              f"{s1['ring_up_running']}/{s1['ring_down_running']}/"
              f"{s1['ring_last_error']}  tdma_fields={s1['tdma_field_count']}")
        print(f"  beacon_tx     {s0['tx']:>9} -> {s1['tx']:>9}  +{delta(s0, s1, 'tx'):>6}  "
              f"{rate(s0, s1, 'tx', elapsed):7.1f}/s")
        print(f"  beacon_rx     {s0['rx']:>9} -> {s1['rx']:>9}  +{delta(s0, s1, 'rx'):>6}  "
              f"{rate(s0, s1, 'rx', elapsed):7.1f}/s")
        print(f"  adapter_tx    {s0['txc']:>9} -> {s1['txc']:>9}  +{delta(s0, s1, 'txc'):>6}  "
              f"{rate(s0, s1, 'txc', elapsed):7.1f}/s")
        print(f"  adapter_rx    {s0['rxc']:>9} -> {s1['rxc']:>9}  +{delta(s0, s1, 'rxc'):>6}  "
              f"{rate(s0, s1, 'rxc', elapsed):7.1f}/s")
        print(f"  rx_bad        {s0['bad']:>9} -> {s1['bad']:>9}")
        print(f"  phys_baud     {s1['phys'].get('baud_hz', -1):>9}  "
              f"cs={s1['phys'].get('tx_csn_pin', -1)}->{s1['phys'].get('rx_csn_pin', -1)}")
        print(f"  phys_tx/rx    +{phys_delta(s0, s1, 'tx_count'):>6} /"
              f" +{phys_delta(s0, s1, 'rx_count'):>6}")
        print(f"  phys_bad      rx_bad +{phys_delta(s0, s1, 'rx_bad_count'):>5}  "
              f"magic_fail +{phys_delta(s0, s1, 'rx_magic_fail_count'):>5}  "
              f"shift +{phys_delta(s0, s1, 'rx_magic_at_shift'):>5}")
        print(f"  phys_flow     busy +{phys_delta(s0, s1, 'rx_busy_count'):>6}  "
              f"stall +{phys_delta(s0, s1, 'rx_stall_count'):>5}  "
              f"tx_timeout +{phys_delta(s0, s1, 'tx_timeout_count'):>5}  "
              f"ring_ovr +{phys_delta(s0, s1, 'rx_ring_overrun_count'):>5}")
        print(f"  phys_dma      produced +{phys_delta(s0, s1, 'rx_dma_produced_words'):>6}  "
              f"scan +{phys_delta(s0, s1, 'rx_scan_produced_words'):>6}  "
              f"write_idx {s1['phys'].get('rx_dma_write_index', -1):>4}  "
              f"ch {s1['phys'].get('rx_dma_channel', -1)}")
        print(f"  core1_loop    {s0['core1']:>9} -> {s1['core1']:>9}  +{s1['core1'] - s0['core1']:>6}  "
              f"{(s1['core1'] - s0['core1']) / elapsed:7.1f}/s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
