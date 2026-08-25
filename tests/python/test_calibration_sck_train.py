from tools.calibration_ring_validate.calibration_sck_train import (
    DESTINATION_REQUIRED_FLAGS,
    FLAG_DIAGNOSTIC_ONLY,
    FLAG_DMA_COMPLETE,
    FLAG_HARDWARE_MARKER,
    FLAG_HARDWARE_SCK_CAPTURE,
    SCK_FIELDS,
    build_sck_offset_matrix,
    parse_sck_status,
    summarize_repeat_matrix,
    summarize_sck_capture,
    validate_link,
)
from tools.scpi_common.scpi_serial import scpi_response_matches_command


def make_row(**overrides: int) -> dict[str, int | str]:
    row: dict[str, int | str] = {field: 0 for field in SCK_FIELDS}
    row.update({
        "tag": "SCKTRN",
        "version": 1,
        "state": 3,
        "source_node": 0,
        "destination_node": 1,
        "train_epoch": 7,
        "train_sequence": 7,
        "sck_codebook_id": 0,
        "sck_crc32": 1234,
        "observed_crc32": 1234,
        "calibration_generation": 9,
        "topology_generation": 10,
        "topology_crc32": 11,
        "profile_crc32": 12,
        "schedule_crc32": 13,
        "sample_period_ns": 4,
        "marker_to_sck_samples": 1000,
        "source_marker_offset_sample_count": 1,
        "destination_marker_offset_sample_count": -1,
        "configured_sck_offset_sample_count": 0,
        "search_start_offset_sample": -10,
        "search_end_offset_sample": 10,
        "resolved_offset_sample_count": 2,
        "resolved_offset_ns": 8,
    })
    row.update(overrides)
    return row


def encode(row: dict[str, int | str]) -> str:
    return ",".join(str(row[field]) for field in SCK_FIELDS)


def test_parse_sck_status_reassembles_ticks() -> None:
    row = make_row(board_id_lo=2, board_id_hi=1,
                   marker_capture_tick_lo=3, marker_capture_tick_hi=2,
                   sck_capture_tick_lo=4, sck_capture_tick_hi=2)
    parsed = parse_sck_status(encode(row))
    assert parsed["board_unique_id"] == (1 << 32) + 2
    assert parsed["marker_capture_tick"] == (2 << 32) + 3
    assert parsed["sck_capture_tick"] == (2 << 32) + 4


def test_sck_arm_accepts_fixed_four_field_scpi_response() -> None:
    assert scpi_response_matches_command(
        "CALibration:SCK:ARM 0,1,0,7,7,101,1000,1,-1,0,-10,10,0,512,0",
        "0,1,7,101") is True


def test_validate_link_uses_destination_correlation() -> None:
    source = make_row(flags=FLAG_DIAGNOSTIC_ONLY | FLAG_HARDWARE_MARKER |
                      FLAG_HARDWARE_SCK_CAPTURE | FLAG_DMA_COMPLETE,
                      observed_crc32=0)
    destination = make_row(topology_generation=20,
                           flags=FLAG_DIAGNOSTIC_ONLY |
                           DESTINATION_REQUIRED_FLAGS,
                           captured_sample_count=6440,
                           expected_sample_count=6420)
    result = validate_link(source, destination)
    assert result["passed"] is True
    assert result["calibrated_sck_offset_sample_count"] == 2


def test_sck_offset_matrix_is_complete_for_all_nodes() -> None:
    trials = []
    for link in range(4):
        for offset in (link - 1, link):
            trials.append({
                "link": link,
                "source_node": link,
                "destination_node": (link + 1) % 4,
                "passed": True,
                "calibrated_sck_offset_sample_count": offset,
                "destination": {
                    "calibration_generation": 9,
                    "topology_generation": 10 + ((link + 1) % 4),
                    "topology_crc32": 11,
                    "profile_crc32": 12,
                    "schedule_crc32": 13,
                    "sample_period_ns": 4,
                },
            })
    matrix = build_sck_offset_matrix(trials, 4, 4)
    assert matrix["full_matrix_row_count"] == 16
    assert matrix["missing_nodes"] == []
    summary = summarize_repeat_matrix(trials, 4, 2, 1, 4)
    assert summary["passed"] is True
    assert len(summary["offset_matrix"]["rows"]) == 16


def test_sck_capture_summary_is_raw_only() -> None:
    summary = summarize_sck_capture({
        "schema": "HAOFV_SCK_TRAIN_CAPTURE_V1",
        "raw_word_count": 1,
        "raw_sample_count": 5,
        "raw_words": [0b00110],
    })
    assert summary["transition_count"] == 2
    assert summary["sample_count"] == 5
