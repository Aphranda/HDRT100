#!/usr/bin/env python3
"""Print OTA transfer parameters for a standard raw firmware .bin file."""

from __future__ import annotations

import argparse
import binascii
from pathlib import Path


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="standard raw firmware .bin")
    args = parser.parse_args()

    data = args.input.read_bytes()
    image_crc = crc32(data)

    print(f"input={args.input}")
    print(f"size={len(data)}")
    print(f"crc32=0x{image_crc:08X}")
    print(f"scpi_begin=SYST:OTA:BEGIN {len(data)},0x{image_crc:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
