#!/usr/bin/env python3
"""Observe peer SYNC_IO input masks while RefMem PIO-SPI RAW:TX is active."""

from __future__ import annotations

import argparse
import csv
import re
import threading
import time
from dataclasses import dataclass

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROLE_MASTER = 1


@dataclass
class IoProfile:
    input_base: int
    input_count: int
    output_base: int
    output_count: int
    trig_in_pin: int
    rj45_in_pin: int
    trig_out_pin: int
    rj45_out_pin: int


@dataclass(frozen=True)
class SpiPins:
    rx: int
    csn: int
    sck: int
    tx: int


def read_serial_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = bytearray()
        while time.monotonic() < deadline:
            ch = ser.read(1)
            if not ch:
                continue
            raw.extend(ch)
            if ch in (b"\n", b"\r"):
                break
        if not raw:
            continue
        line = bytes(raw).decode("utf-8", errors="replace").strip()
        maybe_log = line[1:] if line.startswith('"[') else line
        if not line or maybe_log.startswith("[") or maybe_log.startswith("log:"):
            continue
        if line in {'"OK"', "OK", 'OK"'} or line.startswith('"OK[') or line.startswith("OK["):
            return '"OK"'
        return re.sub(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+.*$', "", line).strip()
    return "<timeout>"


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_serial_line(ser, timeout_s)


def parse_ints(response: str) -> list[int]:
    try:
        fields = next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []
    values: list[int] = []
    for field in fields:
        try:
            values.append(int(field.strip().strip('"'), 0))
        except ValueError:
            pass
    return values


def parse_line_map(text: str) -> list[int]:
    values = [int(part.strip(), 0) for part in text.split(",")]
    if len(values) != 4 or sorted(values) != [0, 1, 2, 3]:
        raise SystemExit(f"line remap must be a permutation of 0..3: {text!r}")
    return values


def read_io_profile(ser: serial.Serial, timeout_s: float) -> IoProfile:
    values = parse_ints(query(ser, "REALtime:IO:PROFile?", timeout_s))
    if len(values) < 8:
        raise RuntimeError(f"REALtime:IO:PROFile? returned malformed response: {values}")
    return IoProfile(*values[:8])


def output_index_for_target_input(remap: list[int], target_input_index: int) -> int:
    return remap.index(target_input_index)


def master_pins(sender_profile: IoProfile, sender_to_receiver: list[int]) -> SpiPins:
    return SpiPins(
        rx=sender_profile.input_base,
        csn=sender_profile.output_base + output_index_for_target_input(sender_to_receiver, 1),
        sck=sender_profile.output_base + output_index_for_target_input(sender_to_receiver, 2),
        tx=sender_profile.output_base + output_index_for_target_input(sender_to_receiver, 0),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sender", default="COM5")
    parser.add_argument("--receiver", default="COM6")
    parser.add_argument("--sender-to-receiver", default="1,2,0,3")
    parser.add_argument("--baud", type=int, default=1000)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--timeout-s", type=float, default=3.0)
    parser.add_argument("--count", type=int, default=256)
    parser.add_argument("--seed", type=int, default=255)
    parser.add_argument("--sample-count", type=int, default=80)
    parser.add_argument("--sample-period-s", type=float, default=0.02)
    args = parser.parse_args()

    remap = parse_line_map(args.sender_to_receiver)
    with serial.Serial(args.sender, args.serial_baud, timeout=0.1, write_timeout=args.timeout_s) as tx, \
            serial.Serial(args.receiver, args.serial_baud, timeout=0.1, write_timeout=args.timeout_s) as rx:
        time.sleep(0.5)
        for ser in (tx, rx):
            ser.reset_input_buffer()
            ser.reset_output_buffer()

        sender_profile = read_io_profile(tx, args.timeout_s)
        receiver_profile = read_io_profile(rx, args.timeout_s)
        pins = master_pins(sender_profile, remap)
        print(f"sender_profile={sender_profile}")
        print(f"receiver_profile={receiver_profile}")
        print(f"master_pins={pins}")
        print(query(rx, "REALtime:IO:OUTPut:RELease", args.timeout_s))
        print(query(tx, f"SYSTem:REFMEM:SYNC:SPI:ARM {ROLE_MASTER},{args.baud},{pins.rx},{pins.csn},{pins.sck},{pins.tx}", args.timeout_s))

        tx_response: dict[str, str] = {}

        def tx_worker() -> None:
            tx_response["value"] = query(
                tx,
                f"SYSTem:REFMEM:SYNC:SPI:RAW:TX {args.count},{args.seed}",
                args.timeout_s + 5.0,
            )

        thread = threading.Thread(target=tx_worker)
        thread.start()
        time.sleep(0.05)

        masks: list[int] = []
        for _ in range(args.sample_count):
            values = parse_ints(query(rx, "REALtime:IO:INPut:LEVel?", args.timeout_s))
            if len(values) >= 3:
                masks.append(values[2] & ((1 << receiver_profile.input_count) - 1))
            time.sleep(args.sample_period_s)

        thread.join(args.timeout_s + 6.0)
        unique_masks = sorted(set(masks))
        print(f"tx_response={tx_response.get('value', '<thread-timeout>')}")
        print(f"unique_masks={[hex(mask) for mask in unique_masks]}")
        for index in range(receiver_profile.input_count):
            high_seen = any(mask & (1 << index) for mask in masks)
            low_seen = any((mask & (1 << index)) == 0 for mask in masks)
            print(f"IN{index}/GPIO{receiver_profile.input_base + index}: high_seen={high_seen} low_seen={low_seen}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
