#!/usr/bin/env python3
"""Create a UF2 image from multiple binary payloads at fixed flash addresses."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
UF2_BLOCK_SIZE = 512
UF2_PAYLOAD_SIZE = 256
RP2350_ARM_S_FAMILY_ID = 0xE48BFF59


def parse_address(value: str) -> int:
    return int(value, 0)


def parse_image(spec: str) -> tuple[Path, int]:
    if "@" not in spec:
        raise argparse.ArgumentTypeError("image spec must be path@address")

    path_text, address_text = spec.rsplit("@", 1)
    return Path(path_text), parse_address(address_text)


def make_block(address: int, payload: bytes, block_no: int, block_count: int, family_id: int) -> bytes:
    padded = payload + bytes(UF2_PAYLOAD_SIZE - len(payload))
    header = struct.pack(
        "<IIIIIIII",
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        UF2_FLAG_FAMILY_ID_PRESENT,
        address,
        UF2_PAYLOAD_SIZE,
        block_no,
        block_count,
        family_id,
    )
    return header + padded + bytes(UF2_BLOCK_SIZE - len(header) - len(padded) - 4) + struct.pack("<I", UF2_MAGIC_END)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("images", nargs="+", type=parse_image, help="path@address, for example app.bin@0x10040000")
    parser.add_argument("--family", type=parse_address, default=RP2350_ARM_S_FAMILY_ID)
    args = parser.parse_args()

    chunks: list[tuple[int, bytes]] = []
    for path, base_address in args.images:
        data = path.read_bytes()
        for offset in range(0, len(data), UF2_PAYLOAD_SIZE):
            chunks.append((base_address + offset, data[offset : offset + UF2_PAYLOAD_SIZE]))

    blocks = [
        make_block(address, payload, block_no, len(chunks), args.family)
        for block_no, (address, payload) in enumerate(chunks)
    ]
    args.output.write_bytes(b"".join(blocks))
    print(f"output={args.output}")
    print(f"blocks={len(blocks)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
