from pathlib import Path

from tools.calibration_ring_validate.calibration_sck_train import (
    DESTINATION_REQUIRED_FLAGS,
    FLAG_DIAGNOSTIC_ONLY,
    FLAG_DMA_COMPLETE,
    FLAG_HARDWARE_SCK_ORIGIN,
    FLAG_HARDWARE_SCK_CAPTURE,
    SCK_FIELDS,
    build_sck_offset_matrix,
    parse_sck_status,
    sck_link_candidate_coverage_complete,
    summarize_repeat_matrix,
    summarize_sck_capture,
    validate_link,
)
from tools.scpi_common.scpi_serial import scpi_response_matches_command


def make_row(**overrides: int) -> dict[str, int | str]:
    row: dict[str, int | str] = {field: 0 for field in SCK_FIELDS}
    row.update({
        "tag": "SCKTRN",
        "version": 3,
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
        "sck_launch_guard_sample_count": 32,
        "link_base_delay_ns": 40,
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
                   sck_capture_origin_tick_lo=3,
                   sck_capture_origin_tick_hi=2,
                   sck_code_capture_tick_lo=4,
                   sck_code_capture_tick_hi=2)
    parsed = parse_sck_status(encode(row))
    assert parsed["board_unique_id"] == (1 << 32) + 2
    assert parsed["sck_capture_origin_tick"] == (2 << 32) + 3
    assert parsed["sck_code_capture_tick"] == (2 << 32) + 4


def test_sck_arm_accepts_fixed_four_field_scpi_response() -> None:
    assert scpi_response_matches_command(
        "CALibration:SCK:ARM 0,1,0,7,7,101,32,40,0,-10,10,0,512,0",
        "0,1,7,101") is True


def test_validate_link_uses_destination_correlation() -> None:
    source = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                      FLAG_HARDWARE_SCK_ORIGIN |
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


def test_sck_adaptive_repeats_wait_for_follower_candidate_coverage() -> None:
    trials = []
    for repeat, offset in enumerate((1, 1, 0), 1):
        trials.append({
            "link": 0,
            "destination_node": 1,
            "repeat_index": repeat,
            "passed": True,
            "calibrated_sck_offset_sample_count": offset,
        })
        assert sck_link_candidate_coverage_complete(
            trials, link=0, destination_node=1, reference_node=0,
            min_repeats=3, min_follower_candidates=2) is (repeat == 3)

    reference_trials = [{
        "link": 3,
        "destination_node": 0,
        "passed": True,
        "calibrated_sck_offset_sample_count": 1,
    } for _ in range(3)]
    assert sck_link_candidate_coverage_complete(
        reference_trials, link=3, destination_node=0, reference_node=0,
        min_repeats=3, min_follower_candidates=2) is True


def test_sck_adaptive_summary_accepts_per_link_repeat_counts() -> None:
    trials = []
    offsets_by_link = ((1, 1, 0), (1, 1, 1, 0),
                       (1, 1, 1, 1, 0), (1, 1, 1))
    for link, offsets in enumerate(offsets_by_link):
        for offset in offsets:
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
    summary = summarize_repeat_matrix(
        trials, 4, 8, 1, 4, min_repeats=3,
        min_follower_candidates=2, reference_node=0)
    assert summary["passed"] is True
    assert summary["adaptive_repeat_enabled"] is True
    assert summary["trial_count"] == 15
    assert [link["trial_count"] for link in summary["links"]] == [3, 4, 5, 3]


def test_sck_adaptive_summary_reports_missing_candidate_coverage() -> None:
    trials = []
    for link in range(4):
        for _ in range(3):
            trials.append({
                "link": link,
                "source_node": link,
                "destination_node": (link + 1) % 4,
                "passed": True,
                "calibrated_sck_offset_sample_count": 1,
                "destination": {
                    "calibration_generation": 9,
                    "topology_generation": 10 + ((link + 1) % 4),
                    "topology_crc32": 11,
                    "profile_crc32": 12,
                    "schedule_crc32": 13,
                    "sample_period_ns": 4,
                },
            })
    summary = summarize_repeat_matrix(
        trials, 4, 8, 1, 4, min_repeats=3,
        min_follower_candidates=2, reference_node=0)
    assert summary["passed"] is False
    assert summary["links"][0]["gate_failures"] == ["candidate_coverage"]
    assert summary["links"][3]["gate_failures"] == []


def test_sck_offset_matrix_recommends_weighted_mode() -> None:
    trials = []
    distributions = ((0, 0, 0, 1), (1, 1, 0, 1),
                     (-1, -1, -1, 0), (2, 2, 1, 2))
    for link, offsets in enumerate(distributions):
        for offset in offsets:
            trials.append({
                "link": link,
                "source_node": link,
                "destination_node": (link + 1) % 4,
                "passed": True,
                "calibrated_sck_offset_sample_count": offset,
            })
    matrix = build_sck_offset_matrix(trials, 4, 4)
    assert matrix["schema"] == "HAOFV_SCK_OFFSET_MATRIX_V2"
    assert matrix["recommended_offset_sample_counts_by_node"] == [2, 0, 1, -1]
    active = matrix["rows"][matrix["active_row_id"]]
    assert active["sck_offset_sample_counts_by_node"] == [2, 0, 1, -1]


def test_sck_capture_summary_is_raw_only() -> None:
    summary = summarize_sck_capture({
        "schema": "HAOFV_SCK_TRAIN_CAPTURE_V3",
        "link_base_delay_ns": 40,
        "raw_word_count": 1,
        "raw_sample_count": 5,
        "raw_words": [0b00110],
    })
    assert summary["transition_count"] == 2
    assert summary["sample_count"] == 5


def test_sck_source_phase_patch_preserves_origin_pulse() -> None:
    pio_source = Path("components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    program = pio_source.split(
        ".program tdma_pio_spi_sck_train_source", 1)[1].split(
            ".program tdma_pio_spi_sck_train_sink", 1)[0]
    program_init = pio_source.split(
        "static inline void tdma_pio_spi_sck_train_source_program_init", 1
    )[1].split(
        "static inline void tdma_pio_spi_sck_train_sink_program_init", 1
    )[0]
    assert "mov y, x" in program
    assert "set pins, 1\nsck_train_source_origin_wait:" in program
    assert "jmp y-- sck_train_source_origin_wait\n    set pins, 0" in program
    assert "pio->instr_mem[offset + 10u]" in program_init
    assert "pio->instr_mem[offset + 8u]" not in program_init
    assert "sm_config_set_set_pins(&c, tx_sck_pin, 1u)" in program_init
