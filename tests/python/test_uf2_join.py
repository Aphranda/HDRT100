from __future__ import annotations

import argparse
import struct

import pytest

from tools.uf2_join import make_fill_bin, uf2_join


def test_uf2_block_has_valid_header_payload_and_trailer() -> None:
    payload = bytes(range(16))
    block = uf2_join.make_block(0x10040000, payload, 3, 7, uf2_join.RP2350_ARM_S_FAMILY_ID)

    fields = struct.unpack_from("<IIIIIIII", block, 0)
    assert len(block) == uf2_join.UF2_BLOCK_SIZE
    assert fields == (
        uf2_join.UF2_MAGIC_START0,
        uf2_join.UF2_MAGIC_START1,
        uf2_join.UF2_FLAG_FAMILY_ID_PRESENT,
        0x10040000,
        uf2_join.UF2_PAYLOAD_SIZE,
        3,
        7,
        uf2_join.RP2350_ARM_S_FAMILY_ID,
    )
    assert block[32 : 32 + len(payload)] == payload
    assert block[32 + len(payload) : 32 + uf2_join.UF2_PAYLOAD_SIZE] == bytes(
        uf2_join.UF2_PAYLOAD_SIZE - len(payload)
    )
    assert struct.unpack_from("<I", block, uf2_join.UF2_BLOCK_SIZE - 4)[0] == uf2_join.UF2_MAGIC_END


def test_uf2_parse_image_requires_address() -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        uf2_join.parse_image("app.bin")

    path, address = uf2_join.parse_image("app.bin@0x10040000")
    assert str(path) == "app.bin"
    assert address == 0x10040000


def test_make_fill_bin_parse_int_accepts_decimal_and_hex() -> None:
    assert make_fill_bin.parse_int("16") == 16
    assert make_fill_bin.parse_int("0x10") == 16
