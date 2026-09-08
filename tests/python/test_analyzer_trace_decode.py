from __future__ import annotations

import struct
from pathlib import Path

from tools.analyzer_trace_decode.analyzer_trace_decode import (
    HEADER,
    METADATA,
    RECORD,
    MAGIC,
    decode,
    crc32,
    write_svg,
)


def test_decode_valid_segment_reports_crc_and_gaps(tmp_path: Path) -> None:
    records = b"".join([
        RECORD.pack(100, 7, 10, 1, 2, 0, 0),
        RECORD.pack(200, 7, 12, 3, 4, 4, 0),
    ])
    header = HEADER.pack(MAGIC, 1, HEADER.size, 99, 2, 5, crc32(records))
    path = tmp_path / "analyzer.bin"
    path.write_bytes(header + records)
    decoded = decode(path, tick_hz=1_000_000, expected_file_crc=crc32(header + records))
    assert decoded["checks"] == {
        "magic_ok": True,
        "schema_ok": True,
        "size_ok": True,
        "payload_crc_ok": True,
        "file_crc_ok": True,
    }
    assert decoded["discontinuity_count"] == 1
    assert decoded["drop_intervals"][0]["missing_count"] == 1
    assert decoded["drop_intervals"][1]["reason"] == "header_dropped_records"
    assert decoded["records"][1]["sequence_gap"] is True
    assert decoded["records"][1]["timestamp_ns"] == 200_000


def test_decode_rejects_payload_crc_mismatch(tmp_path: Path) -> None:
    records = RECORD.pack(1, 1, 1, 0, 0, 0, 0)
    header = HEADER.pack(MAGIC, 1, HEADER.size, 1, 1, 0, 0)
    path = tmp_path / "bad.bin"
    path.write_bytes(header + records)
    decoded = decode(path)
    assert decoded["checks"]["payload_crc_ok"] is False


def test_svg_output_contains_bounded_gpio_lanes(tmp_path: Path) -> None:
    records = RECORD.pack(1, 1, 1, 1, 1, 0, 0) + RECORD.pack(2, 1, 2, 0, 1, 1, 0)
    header = HEADER.pack(MAGIC, 1, HEADER.size, 1, 2, 0, crc32(records))
    path = tmp_path / "segment.bin"
    path.write_bytes(header + records)
    svg = tmp_path / "segment.svg"
    write_svg(decode(path), svg)
    text = svg.read_text(encoding="utf-8")
    assert text.startswith("<svg ")
    assert "GPIO0" in text
    assert "<circle" in text


def test_decode_extended_metadata_header(tmp_path: Path) -> None:
    records = RECORD.pack(1, 1, 1, 1, 0, 0, 0)
    header_size = HEADER.size + METADATA.size
    header = HEADER.pack(MAGIC, 1, header_size, 2, 1, 3, crc32(records))
    metadata = METADATA.pack(0x30, 11, 12, 1_000_000_000, 250_000, 7)
    path = tmp_path / "metadata.bin"
    path.write_bytes(header + metadata + records)
    decoded = decode(path)
    assert decoded["header"]["metadata"] == {
        "source_mask": 0x30,
        "profile_generation": 11,
        "persona_generation": 12,
        "hardware_tick_hz": 1_000_000_000,
        "timestamp_resolution_ns": 250_000,
        "capture_sequence": 7,
    }


def test_decode_schema_v2_and_uint32_sequence_wrap(tmp_path: Path) -> None:
    records = b"".join([
        RECORD.pack(0xFFFFFFFFFFFFFFF0, 9, 0xFFFFFFFF, 1, 0, 0, 0),
        RECORD.pack(0x0000000000000010, 9, 0, 0, 1, 0, 0),
    ])
    header_size = HEADER.size + 9 * 4
    header = HEADER.pack(MAGIC, 2, header_size, 17, 2, 0, crc32(records))
    metadata = struct.pack("<IIIIIIIII", 0x30, 21, 22, 1_000_000_000,
                           1, 9, 4, 0xFFFFFFFF, 12)
    path = tmp_path / "schema-v2.bin"
    path.write_bytes(header + metadata + records)
    decoded = decode(path)
    assert decoded["checks"]["schema_ok"] is True
    assert decoded["header"]["metadata"]["segment_index"] == 4
    assert decoded["header"]["metadata"]["batch_sequence"] == 12
    assert decoded["discontinuity_count"] == 0
    assert decoded["drop_intervals"] == []
