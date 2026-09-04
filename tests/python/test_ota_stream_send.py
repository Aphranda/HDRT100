from __future__ import annotations

import hashlib
import struct
import pytest
from unittest.mock import MagicMock, patch

from tools.ota_stream_send.ota_stream_send import (
    DEFAULT_DEPLOYMENT_MAP_VERSION,
    SOURCE_USB_CDC,
    STREAM_CAPABILITIES,
    STREAM_OPEN_WIRE_FORMAT,
    STREAM_OPEN_WIRE_SIZE,
    app_partition_id,
    crc32,
    encode_open,
    effective_block_size,
    parse_journal_status,
    parse_stream_capability,
    parse_stream_status,
    scpi_block,
    selected_package_object,
    stream_chunks,
    verified_connection,
)
from tools.scpi_common.scpi_serial import scpi_response_matches_command


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


def test_stream_capability_negotiates_a_safe_effective_block() -> None:
    assert parse_stream_capability("4096,3,2") == (4096, 3, 2)
    assert effective_block_size(4096, 4096) == 4096
    assert effective_block_size(4096, 512) == 512


def test_stream_capability_rejects_invalid_or_too_small_target() -> None:
    with pytest.raises(ValueError, match="invalid stream capability"):
        parse_stream_capability("8192,3,2")
    assert effective_block_size(256, 256) == 256
    with pytest.raises(ValueError, match="one of"):
        effective_block_size(768, 4096)


def test_shared_scpi_parser_accepts_stream_capability_tuple() -> None:
    assert scpi_response_matches_command(
        "SYST:OTA:STREAM:CAP?", "4096,3,2")
    assert not scpi_response_matches_command(
        "SYST:OTA:STREAM:CAP?", "4096,3")


@pytest.mark.parametrize("block_size", [256, 512, 1024, 2048, 4096])
def test_stream_sender_supports_configured_block_sizes(block_size: int) -> None:
    assert effective_block_size(block_size, 4096) == block_size
    chunks = stream_chunks(bytes(8192), 0, block_size, False)
    assert max(len(chunk) for _, chunk in chunks) == block_size


def test_package_chunks_split_at_unaligned_image_boundaries() -> None:
    package = bytearray(512 + 400 + 528)
    struct.pack_into("<4I", package, 0, 0x474B5054, 2, 512, len(package))
    struct.pack_into("<I", package, 20, 2)
    struct.pack_into("<6I", package, 192, 1, 512, 400, 1, 2, 0)
    struct.pack_into("<6I", package, 224, 2, 912, 528, 3, 4, 0)
    selected = selected_package_object(bytes(package), 2)
    assert len(selected) == 512 + 528
    assert selected[512:] == package[912:]
    chunks = stream_chunks(selected, 0, 512, True)
    assert [(offset, len(chunk)) for offset, chunk in chunks] == [
        (0, 512), (512, 512), (1024, 16)]
    resumed = stream_chunks(selected, 512, 256, True)
    assert [(offset, len(chunk)) for offset, chunk in resumed] == [
        (512, 256), (768, 256), (1024, 16)]


def test_partition_id_comes_from_canonical_map() -> None:
    assert app_partition_id(1, 1) == 1
    assert app_partition_id(1, 2) == 2
    assert app_partition_id(2, 1) == 2
    assert app_partition_id(2, 2) == 3


def test_stream_sender_defaults_to_deployed_compatibility_map() -> None:
    assert DEFAULT_DEPLOYMENT_MAP_VERSION == 1


def test_explicit_port_reuses_verified_connection() -> None:
    serial_handle = MagicMock()
    serial_handle.readline.return_value = b"GTS,DHRT100,NO5,0.1.0\r\n"

    with patch("tools.ota_stream_send.ota_stream_send.serial.Serial",
               return_value=serial_handle) as open_serial, \
            patch("tools.ota_stream_send.ota_stream_send.time.sleep"):
        with verified_connection("COM7", 115200, 3.0) as (port, identity, ser):
            assert port == "COM7"
            assert "DHRT100" in identity
            assert ser is serial_handle

    open_serial.assert_called_once_with(
        "COM7", 115200, timeout=0.1, write_timeout=3.0)
    serial_handle.close.assert_called_once()
