from tools.calibration_ring_validate.calibration_data_train import (
    DATA_FIELDS,
    DESTINATION_REQUIRED_FLAGS,
    FLAG_DIAGNOSTIC_ONLY,
    FLAG_DMA_COMPLETE,
    FLAG_HARDWARE_MARKER,
    direction_endpoints,
    parse_data_status,
    parse_storage_read,
    summarize_data_capture,
    summarize_repeat_matrix,
    validate_link,
)
from tools.calibration_ring_validate.calibration_data_waveform import (
    render_data_waveform,
    unpack_data_capture,
)
from tools.calibration_ring_validate.calibration_clk_codebook_eval import (
    marker_raw_waveform,
)


def make_row(**overrides: int) -> dict[str, int | str]:
    row: dict[str, int | str] = {field: 0 for field in DATA_FIELDS}
    row.update({
        "tag": "DATATRN",
        "version": 1,
        "state": 3,
        "flags": FLAG_DIAGNOSTIC_ONLY,
        "source_node": 0,
        "destination_node": 1,
        "train_epoch": 7,
        "train_sequence": 7,
        "data_codebook_id": 0,
        "data_crc32": 1234,
        "observed_crc32": 1234,
        "calibration_generation": 9,
        "topology_crc32": 11,
        "profile_crc32": 12,
        "schedule_crc32": 13,
        "sample_period_ns": 4,
        "marker_to_data_samples": 1000,
        "base_delay_ns": 40,
        "marker_offset_sample_count": -1,
        "configured_data_offset_sample_count": 0,
        "search_start_offset_sample": -10,
        "search_end_offset_sample": 10,
        "resolved_offset_sample_count": 1,
        "resolved_offset_ns": 4,
    })
    row.update(overrides)
    return row


def encode(row: dict[str, int | str]) -> str:
    return ",".join(str(row[field]) for field in DATA_FIELDS)


def test_parse_data_status_reassembles_u64_fields() -> None:
    row = make_row(board_id_lo=0x89ABCDEF, board_id_hi=0x01234567,
                   marker_capture_tick_lo=2, marker_capture_tick_hi=1,
                   data_capture_tick_lo=8, data_capture_tick_hi=1)
    parsed = parse_data_status(encode(row))
    assert parsed["board_unique_id"] == 0x0123456789ABCDEF
    assert parsed["marker_capture_tick"] == (1 << 32) + 2
    assert parsed["data_capture_tick"] == (1 << 32) + 8


def test_runtime_direction_parameter_resolves_each_physical_link() -> None:
    assert [direction_endpoints(link, 4, "forward")
            for link in range(4)] == [(0, 1), (1, 2), (2, 3), (3, 0)]
    assert [direction_endpoints(link, 4, "reverse")
            for link in range(4)] == [(1, 0), (2, 1), (3, 2), (0, 3)]
    assert direction_endpoints(7, 8, "forward") == (7, 0)
    assert direction_endpoints(7, 8, "reverse") == (0, 7)


def test_validate_link_accepts_source_transport_and_destination_evidence() -> None:
    source = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                     DESTINATION_REQUIRED_FLAGS,
                     captured_sample_count=6440,
                     expected_sample_count=6420)
    destination = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                           FLAG_HARDWARE_MARKER | FLAG_DMA_COMPLETE,
                           observed_crc32=0)
    result = validate_link(source, destination)
    assert result["passed"] is True
    assert result["resolved_offset_sample_count"] == 1


def test_validate_link_rejects_out_of_range_result() -> None:
    source = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                      DESTINATION_REQUIRED_FLAGS,
                      resolved_offset_sample_count=11)
    destination = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                           FLAG_HARDWARE_MARKER | FLAG_DMA_COMPLETE,
                           observed_crc32=0)
    result = validate_link(source, destination)
    assert result["passed"] is False
    assert "source_offset_range" in result["errors"]


def test_parse_storage_read_and_summarize_capture() -> None:
    page = parse_storage_read(
        'OK,7,0,2,2,2,1,123,0,"0102"', 0)
    assert page["payload"] == b"\x01\x02"
    assert page["eof"] is True

    summary = summarize_data_capture({
        "schema": "HAOFV_DATA_TRAIN_CAPTURE_V1",
        "raw_word_count": 1,
        "raw_sample_count": 5,
        "raw_words": [0b00110],
    })
    assert summary == {
        "sample_count": 5,
        "high_samples": 2,
        "low_samples": 3,
        "transition_count": 2,
        "first_transition_sample": 1,
        "last_transition_sample": 3,
        "constant_level": None,
    }


def test_repeat_matrix_requires_one_generation_and_bounded_offset_span() -> None:
    trials = []
    for link in range(4):
        for repeat, offset in enumerate((4, 5), 1):
            trials.append({
                "link": link,
                "repeat_index": repeat,
                "passed": True,
                "marker_source_node": link,
                "marker_destination_node": (link + 1) % 4,
                "data_source_node": (link + 1) % 4,
                "data_destination_node": link,
                "marker_direction": "forward",
                "data_direction": "reverse",
                "resolved_offset_sample_count": offset,
                "marker_data_skew_ns": 0,
                "source": {
                    "best_distance": link,
                    "margin": 100,
                    "calibration_generation": 84,
                    "topology_generation": 21,
                    "topology_crc32": 22,
                    "profile_crc32": 23,
                    "schedule_crc32": 24,
                    "sample_period_ns": 4,
                },
            })
    summary = summarize_repeat_matrix(trials, 4, 2, 1)
    assert summary["passed"] is True
    assert summary["links"][0]["offset_histogram"] == {"4": 1, "5": 1}
    assert [
        (row["marker_source_node"], row["marker_destination_node"])
        for row in summary["links"]
    ] == [(0, 1), (1, 2), (2, 3), (3, 0)]
    assert [
        (row["data_source_node"], row["data_destination_node"])
        for row in summary["links"]
    ] == [(1, 0), (2, 1), (3, 2), (0, 3)]

    trials[-1]["source"]["calibration_generation"] = 85
    rejected = summarize_repeat_matrix(trials, 4, 2, 1)
    assert rejected["passed"] is False
    assert "calibration_generation" in rejected["identity_failures"]


def test_data_waveform_renders_expected_capture(tmp_path) -> None:
    expected, _ = marker_raw_waveform(
        codebook_id=0, epoch=7, master_slot=0, polarity=0)
    samples = [0] * 5 + expected + [0] * 5
    words = []
    for start in range(0, len(samples), 32):
        word = 0
        for bit, value in enumerate(samples[start:start + 32]):
            word |= int(value) << bit
        words.append(word)
    capture, unpacked = unpack_data_capture({
        "schema": "HAOFV_DATA_TRAIN_CAPTURE_V1",
        "source_node": 0,
        "destination_node": 1,
        "epoch": 7,
        "calibration_generation": 84,
        "sample_period_ns": 4,
        "base_delay_ns": 0,
        "marker_offset_sample_count": -1,
        "configured_data_offset_sample_count": 0,
        "resolved_offset_sample_count": 5,
        "resolved_offset_ns": 20,
        "raw_word_count": len(words),
        "raw_sample_count": len(samples),
        "raw_words": words,
    })
    svg = tmp_path / "data.svg"
    result = render_data_waveform(
        capture, unpacked, codebook_id=0, svg_path=svg,
        window_start_ns=0, window_duration_ns=1000,
        marker_direction="forward", data_direction="reverse")
    assert result["best_candidate_delay_ns"] == 20
    assert result["capture_buffer_lag_ns"] == 20
    assert result["alignment_move_ns"] == -20
    assert result["calibrated_alignment_delay_ns"] == 20
    assert result["physical_capture_center_ns"] == 20
    assert result["alignment_consistency_error_ns"] == 0
    assert result["correlation"]["best_distance"] == 0
    assert result["correlation"]["accepted"] is True
    assert result["marker_source_node"] == 0
    assert result["marker_destination_node"] == 1
    assert result["data_source_node"] == 1
    assert result["data_destination_node"] == 0
    svg_text = svg.read_text(encoding="utf-8")
    assert svg_text.startswith("<svg")
    assert "DATA reverse node1-&gt;node0" in svg_text
    assert "MARK forward node0-&gt;node1" in svg_text
