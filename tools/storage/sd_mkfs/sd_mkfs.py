#!/usr/bin/env python3
"""Format the Pico-attached SD card as FAT32 over protected SCPI."""

from __future__ import annotations

import argparse
import time

import serial


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM5")
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--yes", action="store_true", help="confirm destructive SD card format")
    return parser.parse_args()


def query(ser: serial.Serial, command: str, delay: float = 0.25) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    time.sleep(delay)
    for _ in range(32):
        line = ser.readline().decode("ascii", errors="replace").strip()
        if not line:
            return "<timeout>"
        if line.startswith("[") or line == '"OK"':
            continue
        return line
    return "<timeout>"


def main() -> int:
    args = parse_args()
    if not args.yes:
        raise SystemExit("refusing to format SD card without --yes")

    with serial.Serial(args.port, 115200, timeout=args.timeout, write_timeout=args.timeout) as ser:
        time.sleep(0.3)
        for command in (
            "SYST:FW:BUILD?",
            "SYST:SD:STAT?",
            "SYST:SD:INFO?",
            'SYST:SD:MKFS "ERASE"',
            "SYST:SD:RAW:READ? 0",
            "SYST:SD:STAT?",
            "SYST:SD:INFO?",
        ):
            delay = 4.0 if "MKFS" in command else 0.25
            print(f"{command} -> {query(ser, command, delay)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
