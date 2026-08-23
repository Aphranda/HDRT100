from __future__ import annotations

import hashlib
import struct

from tools.ota_stream_send.ota_stream_send import (
    SOURCE_USB_CDC,
    STREAM_CAPABILITIES,
    STREAM_OPEN_WIRE_FORMAT,
    STREAM_OPEN_WIRE_SIZE,
    app_partition_id,
    crc32,
    encode_open,
    parse_journal_status,
    parse_stream_status,
    scpi_block,
    stream_chunks,
)


def test_encode_open_uses_fixed_little_endian_layout() -> None:
    image = b"payload"
    identity = bytes(range(16))
    wire = encode_open(identity, image, 2, 0x11223344, 7, 2, 3,
                       SOURCE_USB_CDC)
    assert len(wire) == STREAM_OPEN_WIRE_SIZE
    fields = struct.unpack(STREAM_OPEN_WIRE_FORMAT, wire)
    assert fields[:9] == (
        0x11223344,
        7,
        STREAM_CAPABILITIES,
        2,
        3,
        2,
        1,
        len(image),
        crc32(image),
    )
    assert fields[9] == 0
    assert fields[10] == identity
    assert fields[11] == hashlib.sha256(image).digest()

    package_wire = encode_open(identity, image, 2, 0x11223344, 7, 2, 3,
                               SOURCE_USB_CDC, True)
    assert struct.unpack(STREAM_OPEN_WIRE_FORMAT, package_wire)[9] == 1


def test_scpi_block_and_status_parser() -> None:
    assert scpi_block(b"abc") == b"#13abc"
    assert parse_stream_status("0,2,512,42,0") == (0, 2, 512, 42, 0)
    assert parse_journal_status("1,0,2,3,4,5,6,512,1024,7,8,9,0") == (
        1, 0, 2, 3, 4, 5, 6, 512, 1024, 7, 8, 9, 0)


def test_package_chunks_split_at_unaligned_image_boundaries() -> None:
    package = bytearray(512 + 400 + 528)
    struct.pack_into("<I", package, 20, 2)
    struct.pack_into("<6I", package, 192, 1, 512, 400, 1, 2, 0)
    struct.pack_into("<6I", package, 224, 2, 912, 528, 3, 4, 0)
    chunks = stream_chunks(bytes(package), 0, 512, True)
    assert [(offset, len(chunk)) for offset, chunk in chunks] == [
        (0, 512), (512, 400), (912, 512), (1424, 16)]
    resumed = stream_chunks(bytes(package), 912, 256, True)
    assert [(offset, len(chunk)) for offset, chunk in resumed] == [
        (912, 256), (1168, 256), (1424, 16)]


def test_partition_id_comes_from_canonical_map() -> None:
    assert app_partition_id(1, 1) == 1
    assert app_partition_id(1, 2) == 2
    assert app_partition_id(2, 1) == 2
    assert app_partition_id(2, 2) == 3
