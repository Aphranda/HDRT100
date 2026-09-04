from __future__ import annotations

import struct
from pathlib import Path

from tools.analyzer_trace_decode.analyzer_trace_decode import (
    HEADER,
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
