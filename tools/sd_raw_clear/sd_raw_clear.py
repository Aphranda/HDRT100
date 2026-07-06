#!/usr/bin/env python3
"""Send the protected SD raw-prefix clear maintenance command over SCPI."""

from __future__ import annotations

import argparse
import time

import serial


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM5")
    parser.add_argument("--sectors", type=int, default=64, help="prefix sector count, max firmware limit is 64")
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--yes", action="store_true", help="confirm destructive SD prefix clear")
    return parser.parse_args()


def query(ser: serial.Serial, command: str, delay: float = 0.2) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    time.sleep(delay)
    for _ in range(16):
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
        raise SystemExit("refusing to clear SD prefix without --yes")
    if args.sectors <= 0 or args.sectors > 64:
        raise SystemExit("--sectors must be 1..64")

    with serial.Serial(args.port, 115200, timeout=args.timeout, write_timeout=args.timeout) as ser:
        time.sleep(0.3)
        for command in (
            "SYST:FW:BUILD?",
            "SYST:SD:STAT?",
            "SYST:SD:INFO?",
            f'SYST:SD:RAW:CLEAR {args.sectors},"ERASE"',
            "SYST:SD:STAT?",
        ):
            delay = 1.0 if "RAW:CLEAR" in command else 0.25
            print(f"{command} -> {query(ser, command, delay)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
