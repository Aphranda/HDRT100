#!/usr/bin/env python3
"""Create a unified RP2350_TRIG OTA package containing Slot A and Slot B images."""

from __future__ import annotations

import argparse
import binascii
import struct
from pathlib import Path


PACKAGE_MAGIC = 0x474B5054
PACKAGE_VERSION = 1
PACKAGE_HEADER_SIZE = 512
PACKAGE_PAYLOAD_ALIGNMENT = 512
SLOT_A = 1
SLOT_B = 2
SLOT_A_RUN_OFFSET = 0x00040000
SLOT_B_RUN_OFFSET = 0x001C0000


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image-a", required=True, type=Path, help="Slot A linked App .bin")
    parser.add_argument("--image-b", required=True, type=Path, help="Slot B linked App .bin")
    parser.add_argument("-o", "--output", required=True, type=Path, help="output unified OTA package")
    return parser.parse_args()


def put_u32(header: bytearray, offset: int, value: int) -> None:
    header[offset : offset + 4] = struct.pack("<I", value & 0xFFFFFFFF)


def put_image(header: bytearray, index: int, slot: int, offset: int, image: bytes, run_offset: int) -> None:
    cursor = 32 + index * 32
    values = (slot, offset, len(image), crc32(image), run_offset, 0)
    for item_index, value in enumerate(values):
        put_u32(header, cursor + item_index * 4, value)


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def build_package(image_a: bytes, image_b: bytes) -> bytes:
    offset_a = PACKAGE_HEADER_SIZE
    offset_b = align_up(offset_a + len(image_a), PACKAGE_PAYLOAD_ALIGNMENT)
    padding_a = offset_b - (offset_a + len(image_a))
    package_size = offset_b + len(image_b)

    header = bytearray([0xFF] * PACKAGE_HEADER_SIZE)
    put_u32(header, 0, PACKAGE_MAGIC)
    put_u32(header, 4, PACKAGE_VERSION)
    put_u32(header, 8, PACKAGE_HEADER_SIZE)
    put_u32(header, 12, package_size)
    put_u32(header, 16, 0)
    put_u32(header, 20, 2)
    put_image(header, 0, SLOT_A, offset_a, image_a, SLOT_A_RUN_OFFSET)
    put_image(header, 1, SLOT_B, offset_b, image_b, SLOT_B_RUN_OFFSET)

    return bytes(header) + image_a + bytes([0xFF] * padding_a) + image_b


def main() -> int:
    args = parse_args()
    image_a = args.image_a.read_bytes()
    image_b = args.image_b.read_bytes()
    package = build_package(image_a, image_b)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)

    package_crc = crc32(package)
    print(f"output={args.output}")
    print(f"package_size={len(package)}")
    print(f"package_crc32=0x{package_crc:08X}")
    print(f"image_a_size={len(image_a)} image_a_crc32=0x{crc32(image_a):08X}")
    print(f"image_b_size={len(image_b)} image_b_crc32=0x{crc32(image_b):08X}")
    print(f"begin=SYST:OTA:PBEGIN {len(package)},{package_crc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
