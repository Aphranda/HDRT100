#!/usr/bin/env python3
"""Create a unified RP2350_TRIG OTA package containing Slot A and Slot B images."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import struct
from pathlib import Path


PACKAGE_MAGIC = 0x474B5054
PACKAGE_VERSION = 2
PACKAGE_HEADER_SIZE = 512
PACKAGE_PAYLOAD_ALIGNMENT = 512
TEXT_FIELD_SIZE = 32
SLOT_A = 1
SLOT_B = 2
SLOT_A_RUN_OFFSET = 0x00040000
SLOT_B_RUN_OFFSET = 0x001C0000
DEFAULT_PRODUCT_ID = "RP2350_TRIG"
DEFAULT_HARDWARE_ID = "rp2350_trig"
DEFAULT_APP_VERSION = "0.1.0"
DEFAULT_MIN_BOOTLOADER_VERSION = "0.1.0"


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image-a", required=True, type=Path, help="Slot A linked App .bin")
    parser.add_argument("--image-b", required=True, type=Path, help="Slot B linked App .bin")
    parser.add_argument("-o", "--output", required=True, type=Path, help="output unified OTA package")
    parser.add_argument("--product-id", default=DEFAULT_PRODUCT_ID, help="target product id")
    parser.add_argument("--hardware-id", default=DEFAULT_HARDWARE_ID, help="target hardware id")
    parser.add_argument("--app-version", default=DEFAULT_APP_VERSION, help="semantic App version, for example 0.1.0")
    parser.add_argument("--build-id", default="dev", help="firmware build id recorded in the package header")
    parser.add_argument("--build-id-file", type=Path, help="generated project_build_info.c to read build id from")
    parser.add_argument(
        "--min-bootloader-version",
        default=DEFAULT_MIN_BOOTLOADER_VERSION,
        help="minimum compatible Bootloader version, for example 0.1.0",
    )
    return parser.parse_args()


def put_u32(header: bytearray, offset: int, value: int) -> None:
    header[offset : offset + 4] = struct.pack("<I", value & 0xFFFFFFFF)


def put_text(header: bytearray, offset: int, value: str) -> None:
    encoded = value.encode("ascii")
    if len(encoded) >= TEXT_FIELD_SIZE:
        raise ValueError(f"text field too long: {value!r}")
    header[offset : offset + TEXT_FIELD_SIZE] = encoded + bytes(TEXT_FIELD_SIZE - len(encoded))


def put_image(header: bytearray, index: int, slot: int, offset: int, image: bytes, run_offset: int) -> None:
    cursor = 192 + index * 32
    values = (slot, offset, len(image), crc32(image), run_offset, 0)
    for item_index, value in enumerate(values):
        put_u32(header, cursor + item_index * 4, value)


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def parse_semver(value: str) -> tuple[int, int, int]:
    parts = value.split(".")
    if len(parts) != 3:
        raise ValueError(f"version must use major.minor.patch: {value!r}")
    version = tuple(int(part, 10) for part in parts)
    if any(part < 0 or part > 255 for part in version):
        raise ValueError(f"version fields must be in range 0..255: {value!r}")
    return version


def pack_version(version: tuple[int, int, int]) -> int:
    major, minor, patch = version
    return (major << 16) | (minor << 8) | patch


def read_build_id(args: argparse.Namespace) -> str:
    if args.build_id_file is None:
        return args.build_id

    text = args.build_id_file.read_text(encoding="utf-8")
    marker = 'g_project_build_id[] = "'
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"build id marker not found in {args.build_id_file}")
    start += len(marker)
    end = text.find('"', start)
    if end < 0:
        raise ValueError(f"build id terminator not found in {args.build_id_file}")
    return text[start:end]


def build_package(
    image_a: bytes,
    image_b: bytes,
    *,
    product_id: str,
    hardware_id: str,
    app_version: tuple[int, int, int],
    build_id: str,
    min_bootloader_version: tuple[int, int, int],
) -> bytes:
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
    put_text(header, 32, product_id)
    put_text(header, 64, hardware_id)
    put_u32(header, 96, app_version[0])
    put_u32(header, 100, app_version[1])
    put_u32(header, 104, app_version[2])
    put_u32(header, 108, pack_version(min_bootloader_version))
    put_text(header, 112, build_id)
    put_image(header, 0, SLOT_A, offset_a, image_a, SLOT_A_RUN_OFFSET)
    put_image(header, 1, SLOT_B, offset_b, image_b, SLOT_B_RUN_OFFSET)

    payload = image_a + bytes([0xFF] * padding_a) + image_b
    header[144:176] = hashlib.sha256(payload).digest()
    return bytes(header) + image_a + bytes([0xFF] * padding_a) + image_b


def main() -> int:
    args = parse_args()
    image_a = args.image_a.read_bytes()
    image_b = args.image_b.read_bytes()
    app_version = parse_semver(args.app_version)
    min_bootloader_version = parse_semver(args.min_bootloader_version)
    build_id = read_build_id(args)
    package = build_package(
        image_a,
        image_b,
        product_id=args.product_id,
        hardware_id=args.hardware_id,
        app_version=app_version,
        build_id=build_id,
        min_bootloader_version=min_bootloader_version,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)

    package_crc = crc32(package)
    print(f"output={args.output}")
    print(f"package_size={len(package)}")
    print(f"package_crc32=0x{package_crc:08X}")
    print(f"payload_sha256={hashlib.sha256(package[PACKAGE_HEADER_SIZE:]).hexdigest()}")
    print(f"product_id={args.product_id}")
    print(f"hardware_id={args.hardware_id}")
    print(f"app_version={args.app_version}")
    print(f"build_id={build_id}")
    print(f"min_bootloader_version={args.min_bootloader_version}")
    print(f"image_a_size={len(image_a)} image_a_crc32=0x{crc32(image_a):08X}")
    print(f"image_b_size={len(image_b)} image_b_crc32=0x{crc32(image_b):08X}")
    print(f"begin=SYST:OTA:PBEGIN {len(package)},{package_crc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
