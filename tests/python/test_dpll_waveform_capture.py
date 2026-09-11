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
    RECORD_V4,
    SCHEMA,
    SCHEMA_V2,
    SCHEMA_V3,
    SCHEMA_V4,
    _convergence_svg,
    _phase_tracking,
    _span_convergence_summary,
    decode_segments,
    write_reports,
)


def _raw_word(samples: list[int]) -> int:
    return sum(value << ((7 - index) * 4)
               for index, value in enumerate(samples))


def test_decode_excludes_diagnostic_only_edges_from_tracking(tmp_path) -> None:
    record = (_raw_word([0, 1, 1, 1, 1, 1, 1, 1]),
              1, 0, 1_000_000, 0, 100, 2, 100, 1, 0)
    path = tmp_path / "diagnostic_only.bin"
    path.write_bytes(_segment(68, 0, 0, [record]))
    result = decode_segments([path])
    assert result["edge_count"] == 1
    assert result["valid_edge_count"] == 1
    assert result["eligible_edge_count"] == 0
    assert result["phase_round_count"] == 0
    assert result["observation_confidence"] == "no_eligible_edges"


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
    assert result["observation_confidence"] == "insufficient_common_cycles"
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


def test_decode_v4_preserves_quality_and_marks_svg_layers(tmp_path) -> None:
    payload = RECORD_V4.pack(
        _raw_word([0, 1, 3, 7, 15, 15, 15, 15]),
        0, 1_000_000, 1_000_000, 0, 41, 0x20,
    )
    header = HEADER_V3.pack(
        MAGIC, SCHEMA_V4, HEADER_V3.size, RECORD_V4.size, 0,
        45, 0, 0, 1, 0, 10, 11, 0x0F,
        100, 2, 100, 41, 1_000_000, 2,
        zlib.crc32(payload) & 0xFFFFFFFF,
    )
    path = tmp_path / "schema4.bin"
    path.write_bytes(header + payload)

    result = decode_segments([path])
    record = result["records"][0]
    assert record["sample_seq"] == 41
    assert record["quality_flags"] == 0x20
    assert "corrected_eligible" in record["quality_names"]

    svg = _convergence_svg(
        result["phase_tracking"], result["phase_trend"],
        result["phase_span_trend"], 1_000_000,
        result["phase_tracking"])
    assert 'data-quality-flags="32"' in svg
    assert 'data-corrected-eligible="true"' in svg


def test_decode_keeps_continuous_no_edge_word_without_gap(tmp_path) -> None:
    records = [
        RECORD_V4.pack(
            _raw_word([0, 0, 0, 0, 0, 0, 0, 0]),
            0, 1_000_000, 1_000_000, 0, 100,
            0x20 | 0x02 | 0x04 | 0x08,
        ),
        RECORD_V4.pack(
            _raw_word([0, 1, 1, 1, 1, 1, 1, 1]),
            0, 2_000_000, 2_000_000, 0, 101,
            0x20 | 0x02 | 0x04 | 0x08,
        ),
    ]
    payload = b"".join(records)
    header = HEADER_V3.pack(
        MAGIC, SCHEMA_V4, HEADER_V3.size, RECORD_V4.size, 0,
        46, 0, 0, len(records), 0, 10, 12, 0x0F,
        100, 2, 100, 100, 1_000_000, 2,
        zlib.crc32(payload) & 0xFFFFFFFF,
    )
    path = tmp_path / "continuous_no_edge.bin"
    path.write_bytes(header + payload)

    result = decode_segments([path])

    assert result["record_count"] == 2
    assert [row["sample_seq"] for row in result["records"]] == [100, 101]
    assert result["gap_count"] == 0
    assert result["edge_count"] == 1
    assert result["valid_edge_count"] == 1
    assert result["invalid_edge_count"] == 0


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


def test_decode_marks_gap_boundary_edge_invalid(tmp_path) -> None:
    first = (_raw_word([0, 1, 1, 1, 1, 1, 1, 1]),
             10, 0, 1_000_000, 1_000_000, 100, 2, 100, 2, 0)
    second = (_raw_word([1, 1, 1, 1, 1, 1, 1, 1]),
              12, 0, 2_000_000, 2_000_000, 100, 2, 100, 2, 1)
    path = tmp_path / "gap.bin"
    path.write_bytes(_segment(66, 0, 0, [first, second]))

    result = decode_segments([path])

    assert result["gap_count"] == 1
    assert result["edge_count"] == 2
    assert result["valid_edge_count"] == 1
    assert result["invalid_edge_count"] == 1
    assert result["edge_coverage"] == 0.5
    assert result["observation_confidence"] == "degraded_gap"
    assert len(result["phase_tracking"]) == result["valid_edge_count"]
    invalid = [edge for edge in result["edges"] if not edge["valid"]]
    assert invalid[0]["invalid_reason"] == "sample_sequence_gap;source_drop"


def test_decode_marks_segment_capture_drop_boundary_invalid(tmp_path) -> None:
    first = (_raw_word([0, 1, 1, 1, 1, 1, 1, 1]),
             10, 0, 1_000_000, 1_000_000, 100, 2, 100, 2, 0)
    second = (_raw_word([1, 1, 1, 1, 1, 1, 1, 1]),
              11, 0, 2_000_000, 2_000_000, 100, 2, 100, 2, 0)
    first_path = tmp_path / "segment0.bin"
    second_path = tmp_path / "segment1.bin"
    first_path.write_bytes(_segment(69, 0, 0, [first]))
    payload = b"".join(RECORD.pack(*record) for record in [second])
    header = HEADER.pack(
        MAGIC, SCHEMA, HEADER.size, RECORD.size, 0,
        69, 1, 1, 1, 3, 10, 12, 0x0F, zlib.crc32(payload) & 0xFFFFFFFF)
    second_path.write_bytes(header + payload)

    result = decode_segments([first_path, second_path])

    assert result["capture_dropped_count"] == 3
    assert result["gap_count"] == 1
    assert result["valid_edge_count"] == 1
    invalid = [edge for edge in result["edges"] if not edge["valid"]]
    assert invalid[0]["invalid_reason"] == "capture_drop"


def test_decode_downgrades_confidence_when_channel_is_missing(tmp_path) -> None:
    record = (_raw_word([0, 1, 1, 1, 1, 1, 1, 1]),
              10, 0, 1_000_000, 1_000_000, 100, 2, 100, 2, 0)
    path = tmp_path / "single_channel.bin"
    path.write_bytes(_segment(67, 0, 0, [record]))

    result = decode_segments([path])

    assert result["valid_channels"] == [0]
    assert result["channel_coverage"] == 0.25
    assert result["quality_counts"]["incomplete_window"] == 1
    assert result["observation_confidence"] == "low_channel_coverage"


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


def test_phase_tracking_pairs_only_common_cycles_and_rejects_adjacent_pulse() -> None:
    # NO2's second edge is almost one period after NO1's second edge.  A
    # nearest-in-time matcher would silently associate it with that pulse;
    # cycle-index matching must leave it unmatched.
    edges = [
        {"channel": 0, "edge": "rising", "timestamp_ns": 100},
        {"channel": 1, "edge": "rising", "timestamp_ns": 10_100},
        {"channel": 2, "edge": "rising", "timestamp_ns": 20_100},
        {"channel": 3, "edge": "rising", "timestamp_ns": 30_100},
        {"channel": 0, "edge": "rising", "timestamp_ns": 1_000_100},
        {"channel": 1, "edge": "rising", "timestamp_ns": 1_990_100},
    ]
    tracking, _, _, convergence = _phase_tracking(edges, 1_000_000)
    no2 = [row for row in tracking if row["channel"] == 1]
    assert no2[0]["matched_common_cycle"] is True
    assert no2[1]["matched_common_cycle"] is False
    assert no2[1]["relative_to_no1_ns"] is None
    assert convergence["common_cycle_count"] == 1


def test_phase_tracking_applies_per_channel_delay_before_residual() -> None:
    edges = [
        {"channel": 0, "edge": "rising", "timestamp_ns": 1_000_000},
        {"channel": 1, "edge": "rising", "timestamp_ns": 1_000_150},
        {"channel": 2, "edge": "rising", "timestamp_ns": 1_000_250},
        {"channel": 3, "edge": "rising", "timestamp_ns": 1_000_350},
    ]
    tracking, _, _, convergence = _phase_tracking(
        edges, 1_000_000, {1: 150, 2: 250, 3: 350})
    assert convergence["channel_delays_ns"] == {0: 0, 1: 150, 2: 250, 3: 350}
    assert convergence["common_cycle_count"] == 1
    assert {row["relative_to_no1_ns"] for row in tracking} == {0}


def test_phase_tracking_keeps_edges_straddling_period_boundary_in_one_cycle() -> None:
    edges = [
        {"channel": 0, "edge": "rising", "timestamp_ns": 1_000_000},
        {"channel": 1, "edge": "rising", "timestamp_ns": 999_900},
        {"channel": 2, "edge": "rising", "timestamp_ns": 1_000_100},
        {"channel": 3, "edge": "rising", "timestamp_ns": 1_000_200},
    ]
    tracking, _, _, convergence = _phase_tracking(edges, 1_000_000)

    assert convergence["common_cycle_count"] == 1
    assert {row["relative_to_no1_ns"] for row in tracking} == {
        -100, 0, 100, 200}


def test_phase_tracking_uses_authoritative_schedule_phase_and_reports_bias() -> None:
    edges = []
    for cycle in range(4):
        base = 1_000_000 + cycle * 1_000_000
        for channel, schedule_phase, bias in (
                (0, 0, 0), (1, 100_000, 25),
                (2, 200_000, -30), (3, 300_000, 10)):
            edges.append({
                "channel": channel,
                "edge": "rising",
                "timestamp_ns": base + schedule_phase + bias,
            })
    tracking, _, _, convergence = _phase_tracking(
        edges, 1_000_000,
        channel_phase_ns={1: 100_000, 2: 200_000, 3: 300_000})
    assert convergence["alignment_mode"] == "schedule_contract"
    assert convergence["common_cycle_count"] == 4
    assert convergence["nodes"]["NO2"]["fixed_bias_relative_to_no1_ns"] == 25
    assert convergence["nodes"]["NO3"]["fixed_bias_relative_to_no1_ns"] == -30
    assert convergence["nodes"]["NO2"]["bias_corrected_jitter_rms_ns"] == 0
    assert all(row["matched_common_cycle"] for row in tracking)


def test_phase_tracking_marks_inferred_alignment_as_weak_contract() -> None:
    edges = []
    for cycle in range(3):
        base = 1_000_000 + cycle * 1_000_000
        edges.extend({
            "channel": channel,
            "edge": "rising",
            "timestamp_ns": base + channel * 100,
        } for channel in range(4))
    tracking, _, _, convergence = _phase_tracking(edges, 1_000_000)
    assert convergence["alignment_mode"] == "inferred_channel_phase"
    assert convergence["schedule_phase_supplied"] is False


def test_phase_tracking_rejects_partial_schedule_contract_for_strong_evidence() -> None:
    edges = []
    for cycle in range(3):
        base = 1_000_000 + cycle * 1_000_000
        edges.extend({
            "channel": channel,
            "edge": "rising",
            "timestamp_ns": base + channel * 100,
        } for channel in range(4))
    _, _, _, convergence = _phase_tracking(
        edges, 1_000_000, channel_phase_ns={1: 100})
    assert convergence["alignment_mode"] == "incomplete_schedule_contract"
    assert convergence["schedule_phase_complete"] is False


def test_phase_tracking_does_not_join_edges_from_different_explicit_windows() -> None:
    # The timestamps are close enough to fool a period-only matcher, but the
    # capture contract assigns each edge to a different TDMA window.  Without
    # a complete schedule contract these are not a common observation round.
    edges = [
        {"channel": 0, "edge": "rising", "timestamp_ns": 1_000_000,
         "window_start_ns": 1_000_000},
        {"channel": 1, "edge": "rising", "timestamp_ns": 1_000_100,
         "window_start_ns": 2_000_000},
        {"channel": 2, "edge": "rising", "timestamp_ns": 1_000_200,
         "window_start_ns": 3_000_000},
        {"channel": 3, "edge": "rising", "timestamp_ns": 1_000_300,
         "window_start_ns": 4_000_000},
    ]
    tracking, _, _, convergence = _phase_tracking(edges, 1_000_000)

    assert convergence["pairing_mode"] == "explicit_window_cooccurrence"
    assert convergence["window_group_count"] == 4
    assert convergence["complete_window_count"] == 0
    assert convergence["common_cycle_count"] == 0
    assert not any(row["matched_common_cycle"] for row in tracking)


def test_phase_tracking_pairs_only_complete_explicit_window() -> None:
    edges = [
        {"channel": channel, "edge": "rising",
         "timestamp_ns": 1_000_000 + channel * 100,
         "window_start_ns": 1_000_000}
        for channel in range(4)
    ]
    tracking, _, _, convergence = _phase_tracking(edges, 1_000_000)

    assert convergence["pairing_mode"] == "explicit_window_cooccurrence"
    assert convergence["window_group_count"] == 1
    assert convergence["complete_window_count"] == 1
    assert convergence["common_cycle_count"] == 1
    assert all(row["matched_common_cycle"] for row in tracking)
    assert {row["window_start_ns"] for row in tracking} == {1_000_000}
