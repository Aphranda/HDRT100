from __future__ import annotations

import struct
import zlib

import pytest

from tools.dpll_waveform_capture.dpll_waveform_capture import (
    HEADER,
    MAGIC,
    RECORD,
    SCHEMA,
    decode_segments,
    write_reports,
)


def _raw_word(samples: list[int]) -> int:
    return sum(value << ((7 - index) * 4)
               for index, value in enumerate(samples))


def _segment(session: int, index: int, first: int,
             records: list[tuple[int, ...]]) -> bytes:
    payload = b"".join(RECORD.pack(*record) for record in records)
    header = HEADER.pack(
        MAGIC, SCHEMA, HEADER.size, RECORD.size, 0,
        session, index, first, len(records), 0,
        10, 11, 0x0F, zlib.crc32(payload) & 0xFFFFFFFF)
    return header + payload


def test_decode_raw_pio_words_reconstructs_four_channel_phase(tmp_path) -> None:
    window = 1_000_000
    record = (
        _raw_word([0, 1, 3, 7, 15, 15, 15, 15]),
        7, 0, window, window, 100, 2, 100, 2, 0)
    path = tmp_path / "sma_00000010_0000.bin"
    path.write_bytes(_segment(10, 0, 0, [record]))

    result = decode_segments([path])
    assert result["source"] == "NO5_SD_PIO0_RAW_WAVEFORM"
    assert result["record_count"] == 1
    assert result["edge_count"] == 4
    assert result["phase_round_count"] == 1
    assert result["phase"][0]["span_ns"] == 300
    assert [result["phase"][0][f"offset{channel}_ns"]
            for channel in range(4)] == [0, 100, 200, 300]

    summary = write_reports(result, tmp_path / "analysis")
    assert summary["outputs"]["phase_curve"].endswith("phase_curve.csv")
    assert (tmp_path / "analysis" / "phase_curve.svg").exists()


def test_decode_rejects_segment_or_record_discontinuity(tmp_path) -> None:
    record = (_raw_word([0] * 8), 1, 0, 0, 0, 100, 2, 100, 0, 0)
    first = tmp_path / "first.bin"
    third = tmp_path / "third.bin"
    first.write_bytes(_segment(22, 0, 0, [record]))
    third.write_bytes(_segment(22, 2, 2, [record]))
    with pytest.raises(ValueError, match="missing waveform segment"):
        decode_segments([first, third])


def test_decode_rejects_payload_crc_corruption(tmp_path) -> None:
    record = (_raw_word([0] * 8), 1, 0, 0, 0, 100, 2, 100, 0, 0)
    data = bytearray(_segment(33, 0, 0, [record]))
    data[-1] ^= 0x80
    path = tmp_path / "bad.bin"
    path.write_bytes(data)
    with pytest.raises(ValueError, match="payload CRC"):
        decode_segments([path])
