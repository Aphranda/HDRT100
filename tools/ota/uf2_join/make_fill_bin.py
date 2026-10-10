#!/usr/bin/env python3
"""Create a binary file filled with a constant byte."""

from __future__ import annotations

import argparse
from pathlib import Path


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("size", type=parse_int)
    parser.add_argument("--byte", type=parse_int, default=0xFF)
    args = parser.parse_args()

    if args.size < 0:
        raise ValueError("size must be non-negative")
    if args.byte < 0 or args.byte > 0xFF:
        raise ValueError("byte must be in range 0..255")

    args.output.write_bytes(bytes([args.byte]) * args.size)
    print(f"output={args.output}")
    print(f"size={args.size}")
    print(f"byte=0x{args.byte:02X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
