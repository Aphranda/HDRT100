from __future__ import annotations

import struct
import zlib

import pytest

from tools.dpll_waveform_capture.dpll_waveform_capture import (
    HEADER,
    HEADER_V2,
    HEADER_V3,
    MAGIC,
    RECORD,
    RECORD_V2,
    RECORD_V3,
    SCHEMA,
    SCHEMA_V2,
    SCHEMA_V3,
    _convergence_svg,
    _span_convergence_summary,
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


def _segment_v2(session: int, records: list[tuple[int, ...]]) -> bytes:
    payload = b"".join(RECORD_V2.pack(
        record[0], record[1], record[2], record[3], record[4], record[8],
        record[9]) for record in records)
    header = HEADER_V2.pack(
        MAGIC, SCHEMA_V2, HEADER_V2.size, RECORD_V2.size, 0,
        session, 0, 0, len(records), 0, 10, 11, 0x0F,
        records[0][5], records[0][6], records[0][7],
        zlib.crc32(payload) & 0xFFFFFFFF)
    return header + payload


def _segment_v3(session: int, records: list[tuple[int, ...]]) -> bytes:
    payload = b"".join(RECORD_V3.pack(
        record[0], record[2], record[3], record[4] & 0xFFFFFFFF, record[9])
        for record in records)
    header = HEADER_V3.pack(
        MAGIC, SCHEMA_V3, HEADER_V3.size, RECORD_V3.size, 0,
        session, 0, 0, len(records), 0, 10, 11, 0x0F,
        records[0][5], records[0][6], records[0][7], records[0][1],
        records[0][4], records[0][8], zlib.crc32(payload) & 0xFFFFFFFF)
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
    assert result["convergence"]["pulse_period_ns"] == 1_000_000
    assert len(result["phase_tracking"]) == 4
    assert result["phase"][0]["span_ns"] == 300
    assert [result["phase"][0][f"offset{channel}_ns"]
            for channel in range(4)] == [0, 100, 200, 300]

    summary = write_reports(result, tmp_path / "analysis")
    assert summary["outputs"]["phase_curve"].endswith("phase_curve.csv")
    assert (tmp_path / "analysis" / "phase_curve.svg").exists()
    assert summary["outputs"]["dpll_convergence_svg"].endswith(
        "dpll_convergence.svg")
    assert (tmp_path / "analysis" / "dpll_convergence.svg").exists()
    assert (tmp_path / "analysis" / "dpll_convergence_span.csv").exists()


def test_decode_compact_v2_segment_restores_common_metadata(tmp_path) -> None:
    record = (_raw_word([0, 1, 3, 7, 15, 15, 15, 15]),
              7, 0, 1_000_000, 1_000_000, 100, 2, 100, 2, 0)
    path = tmp_path / "sma_v2.bin"
    path.write_bytes(_segment_v2(44, [record]))

    result = decode_segments([path])

    assert result["segments"][0]["schema"] == SCHEMA_V2
    assert result["records"][0]["sample_period_ns"] == 100
    assert result["records"][0]["timestamp_source"] == 2
    assert result["records"][0]["timestamp_resolution_ns"] == 100
    assert result["phase_round_count"] == 1


def test_decode_v3_reconstructs_sequence_drop_and_window_wrap(tmp_path) -> None:
    first_window = (7 << 32) | 0xFFFF_FF00
    records = [
        (_raw_word([0] * 8), 100, 0, 0xFFFF_FF00, first_window,
         100, 2, 100, 2, 4),
        (_raw_word([0] * 8), 102, 0, 0x0000_0100,
         first_window + 0x200, 100, 2, 100, 2, 5),
    ]
    path = tmp_path / "sma_v3.bin"
    path.write_bytes(_segment_v3(55, records))

    result = decode_segments([path])

    assert result["segments"][0]["schema"] == SCHEMA_V3
    assert [row["sample_seq"] for row in result["records"]] == [100, 102]
    assert result["records"][1]["matched_window_start_ns"] == \
        first_window + 0x200
    assert result["source_dropped_count"] == 5


def test_convergence_svg_breaks_wrap_and_marks_real_jump() -> None:
    trend = [
        {"elapsed_s": 0.0, "channel": 0, "phase_center_ns": 990_000},
        {"elapsed_s": 1.0, "channel": 0, "phase_center_ns": 10_000},
        {"elapsed_s": 2.0, "channel": 0, "phase_center_ns": 450_000},
    ]
    rows = [
        {"elapsed_s": row["elapsed_s"], "channel": row["channel"],
         "phase_ns": row["phase_center_ns"]}
        for row in trend
    ]
    svg = _convergence_svg(
        rows, trend,
        [{"elapsed_s": 0.0, "span_ns": 600_000, "node_count": 4},
         {"elapsed_s": 2.0, "span_ns": 300_000, "node_count": 4}],
        1_000_000)

    assert svg.count('class="outlier"') == 1
    assert svg.count('data-node="NO1" data-segment=') == 3
    assert 'class="x-tick-label">2.00</text>' in svg
    assert 'data-series="four-node-circular-span"' in svg


@pytest.mark.parametrize(("spans", "direction"), [
    ([400_000, 390_000, 380_000, 370_000, 360_000,
      200_000, 190_000, 180_000, 170_000, 160_000], "CONVERGING"),
    ([160_000, 170_000, 180_000, 190_000, 200_000,
      360_000, 370_000, 380_000, 390_000, 400_000], "DIVERGING"),
    ([200_000, 205_000, 198_000, 202_000, 201_000,
      210_000, 208_000, 212_000, 209_000, 211_000],
     "STABLE_OR_INCONCLUSIVE"),
])
def test_four_node_span_reports_long_term_direction(
        spans: list[int], direction: str) -> None:
    trend = [{"elapsed_s": index * 0.25, "span_ns": span,
              "node_count": 4}
             for index, span in enumerate(spans)]

    convergence = _span_convergence_summary(trend, 1_000_000)

    assert convergence["available"] is True
    assert convergence["direction"] == direction


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
