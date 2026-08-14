from __future__ import annotations

import hashlib
import struct

import pytest

from tools.ota_packager import ota_packager


def _read_c_string(data: bytes) -> str:
    return data.split(b"\0", 1)[0].decode("ascii")


def test_ota_package_header_matches_payload_layout() -> None:
    image_a = b"A" * 17
    image_b = b"B" * 19

    package = ota_packager.build_package(
        image_a,
        image_b,
        product_id="RP2350_TRIG",
        hardware_id="rp2350_trig",
        app_version=(1, 2, 3),
        build_id="202608140001",
        min_bootloader_version=(0, 1, 0),
    )

    magic, version, header_size, package_size, _, image_count = struct.unpack_from("<IIIIII", package, 0)
    assert magic == ota_packager.PACKAGE_MAGIC
    assert version == ota_packager.PACKAGE_VERSION
    assert header_size == ota_packager.PACKAGE_HEADER_SIZE
    assert package_size == len(package)
    assert image_count == 2
    assert _read_c_string(package[32:64]) == "RP2350_TRIG"
    assert _read_c_string(package[64:96]) == "rp2350_trig"

    slot_a, offset_a, size_a, crc_a, run_a, _ = struct.unpack_from("<IIIIII", package, 192)
    slot_b, offset_b, size_b, crc_b, run_b, _ = struct.unpack_from("<IIIIII", package, 224)
    assert (slot_a, offset_a, size_a, crc_a, run_a) == (
        ota_packager.SLOT_A,
        ota_packager.PACKAGE_HEADER_SIZE,
        len(image_a),
        ota_packager.crc32(image_a),
        ota_packager.SLOT_A_RUN_OFFSET,
    )
    assert (slot_b, size_b, crc_b, run_b) == (
        ota_packager.SLOT_B,
        len(image_b),
        ota_packager.crc32(image_b),
        ota_packager.SLOT_B_RUN_OFFSET,
    )
    assert offset_b % ota_packager.PACKAGE_PAYLOAD_ALIGNMENT == 0
    assert package[offset_a : offset_a + size_a] == image_a
    assert package[offset_b : offset_b + size_b] == image_b
    assert package[144:176] == hashlib.sha256(package[ota_packager.PACKAGE_HEADER_SIZE:]).digest()


@pytest.mark.parametrize("value", ["1.2", "1.2.3.4", "1.2.256", "1.-1.0"])
def test_parse_semver_rejects_invalid_versions(value: str) -> None:
    with pytest.raises(ValueError):
        ota_packager.parse_semver(value)


def test_parse_semver_packs_three_byte_version() -> None:
    assert ota_packager.parse_semver("1.2.3") == (1, 2, 3)
    assert ota_packager.pack_version((1, 2, 3)) == 0x010203
