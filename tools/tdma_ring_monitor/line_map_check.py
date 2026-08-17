#!/usr/bin/env python3
"""Two-board GPIO wiring mapper for the minimum-system board set.

Drives every debug/TDMA line on one board and samples the opposite board:
  GPIO4..7    via REALtime:IO:MODel:OUTPut:MASK / MODel:INPut:LEVel?
  GPIO16..19  via SYSTem:REFMEM:SYNC:SPI:LINE:DRIVe / SPI:LINE:STATus?
  GPIO21..24  via REALtime:IO:OUTPut:MASK / REALtime:IO:INPut:LEVel?

This is a bring-up diagnostic. Driving these lines reconfigures pins that the
resident TDMA ring owns (PIO), so after the check both boards must be rebooted
(or the ring re-armed) before ring monitoring continues.
"""

from __future__ import annotations

import argparse
import csv
import sys
import time
from contextlib import ExitStack
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import open_serial_port, read_serial_line_idle  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--name-a", default="A")
    parser.add_argument("--name-b", default="B")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=1.0)
    return parser.parse_args()


def query(ser, command: str, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    while time.monotonic() < deadline:
        line = read_serial_line_idle(ser, deadline)
        if line is None or line.startswith("["):
            continue
        text = line.strip()
        if text in ('"OK"', "OK"):
            return '"OK"'
        return text
    return "<timeout>"


def parse_csv_ints(response: str) -> list[int]:
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


def read_input_mask(ser, timeout_s: float) -> int:
    values = parse_csv_ints(query(ser, "REALtime:IO:INPut:LEVel?", timeout_s))
    return values[2] if len(values) >= 3 else 0


def read_model_input_mask(ser, timeout_s: float) -> int:
    values = parse_csv_ints(query(ser, "REALtime:IO:MODel:INPut:LEVel?", timeout_s))
    return values[2] if len(values) >= 3 else 0


def read_spi_line_mask(ser, timeout_s: float) -> int:
    values = parse_csv_ints(query(ser, "SYSTem:REFMEM:SYNC:SPI:LINE:STATus?", timeout_s))
    return values[0] if values else 0


def drive_gpio4_7(ser, bit: int, level: int, timeout_s: float) -> None:
    enable = 1 << bit
    value = enable if level else 0
    _ = query(ser, f"REALtime:IO:MODel:OUTPut:MASK {enable},{value}", timeout_s)


def drive_gpio16_19(ser, bit: int, level: int, timeout_s: float) -> None:
    _ = query(ser, f"SYSTem:REFMEM:SYNC:SPI:LINE:DRIVe {bit},{level}", timeout_s)


def drive_gpio21_24(ser, bit: int, level: int, timeout_s: float) -> None:
    mask = (1 << bit) if level else 0
    _ = query(ser, f"REALtime:IO:OUTPut:MASK {mask}", timeout_s)


def release_all(ser, timeout_s: float) -> None:
    _ = query(ser, "REALtime:IO:OUTPut:MASK 0", timeout_s)
    _ = query(ser, "REALtime:IO:OUTPut:RELease", timeout_s)
    _ = query(ser, "REALtime:IO:MODel:OUTPut:MASK 0,0", timeout_s)
    _ = query(ser, "REALtime:IO:MODel:OUTPut:RELease", timeout_s)


def sample_all(ser, timeout_s: float) -> tuple[int, int]:
    """Return (gpio4_7_mask, gpio16_19_mask) as seen by this board."""
    m47 = read_input_mask(ser, timeout_s) | read_model_input_mask(ser, timeout_s)
    m1619 = read_spi_line_mask(ser, timeout_s)
    return m47, m1619


def drive_row(driver_name: str, drive_fn, driver_ser, sampler_name: str,
              sampler_ser, label: str, gpio_base: int, count: int,
              timeout_s: float) -> None:
    print(f"{driver_name} drives {label} (GPIO{gpio_base}..{gpio_base + count - 1}) "
          f"-> {sampler_name} sees:")
    for bit in range(count):
        drive_fn(driver_ser, bit, 1, timeout_s)
        time.sleep(0.15)
        m47, m1619 = sample_all(sampler_ser, timeout_s)
        seen: list[str] = []
        for i in range(4):
            if (m47 >> i) & 1:
                seen.append(f"4.{i}(GPIO{4 + i})")
        for i in range(4):
            if (m1619 >> i) & 1:
                seen.append(f"16.{i}(GPIO{16 + i})")
        print(f"  GPIO{gpio_base + bit} -> " + (", ".join(seen) if seen else "(nothing)"))
        drive_fn(driver_ser, bit, 0, timeout_s)
        time.sleep(0.1)
    print()


def main() -> int:
    args = parse_args()
    with ExitStack() as stack:
        ser_a = stack.enter_context(open_serial_port(args.port_a, args.baud,
                                                     args.timeout, args.settle))
        ser_b = stack.enter_context(open_serial_port(args.port_b, args.baud,
                                                     args.timeout, args.settle))

        for name, ser in ((args.name_a, ser_a), (args.name_b, ser_b)):
            release_all(ser, args.timeout)
            time.sleep(0.2)

        # A drives -> B samples
        drive_row(args.name_a, drive_gpio4_7, ser_a, args.name_b, ser_b,
                  "model overlay", 4, 4, args.timeout)
        drive_row(args.name_a, drive_gpio16_19, ser_a, args.name_b, ser_b,
                  "refmem spi", 16, 4, args.timeout)
        drive_row(args.name_a, drive_gpio21_24, ser_a, args.name_b, ser_b,
                  "realtime out", 21, 4, args.timeout)

        # B drives -> A samples
        drive_row(args.name_b, drive_gpio4_7, ser_b, args.name_a, ser_a,
                  "model overlay", 4, 4, args.timeout)
        drive_row(args.name_b, drive_gpio16_19, ser_b, args.name_a, ser_a,
                  "refmem spi", 16, 4, args.timeout)
        drive_row(args.name_b, drive_gpio21_24, ser_b, args.name_a, ser_a,
                  "realtime out", 21, 4, args.timeout)

    print("done; reboot both boards before continuing ring monitoring "
          "(driving these lines reconfigures pins the TDMA ring owns)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
