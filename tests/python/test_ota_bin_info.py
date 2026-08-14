from __future__ import annotations

import binascii

from tools.ota_bin_info import ota_bin_info


def test_crc32_matches_standard_binascii_crc32() -> None:
    data = b"DTC100-ota-image\x00\x01\x02"

    assert ota_bin_info.crc32(data) == (binascii.crc32(data) & 0xFFFFFFFF)


def test_crc32_returns_unsigned_value() -> None:
    assert ota_bin_info.crc32(b"\xff" * 8) >= 0
    assert ota_bin_info.crc32(b"\xff" * 8) <= 0xFFFFFFFF
