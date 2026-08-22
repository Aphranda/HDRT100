from __future__ import annotations

import hashlib
import struct

from tools.ota_stream_send.ota_stream_send import (
    SOURCE_USB_CDC,
    STREAM_CAPABILITIES,
    STREAM_OPEN_WIRE_FORMAT,
    STREAM_OPEN_WIRE_SIZE,
    crc32,
    encode_open,
    parse_stream_status,
    scpi_block,
)


def test_encode_open_uses_fixed_little_endian_layout() -> None:
    image = b"payload"
    identity = bytes(range(16))
    wire = encode_open(identity, image, 2, 0x11223344, 7, SOURCE_USB_CDC)
    assert len(wire) == STREAM_OPEN_WIRE_SIZE
    fields = struct.unpack(STREAM_OPEN_WIRE_FORMAT, wire)
    assert fields[:9] == (
        0x11223344,
        7,
        STREAM_CAPABILITIES,
        1,
        2,
        2,
        1,
        len(image),
        crc32(image),
    )
    assert fields[9] == 0
    assert fields[10] == identity
    assert fields[11] == hashlib.sha256(image).digest()


def test_scpi_block_and_status_parser() -> None:
    assert scpi_block(b"abc") == b"#13abc"
    assert parse_stream_status("0,2,512,42,0") == (0, 2, 512, 42, 0)
