from __future__ import annotations

from tools.calibration_ring_validate.calibration_marker_waveform import (
    analysis_console_summary,
    expand_zero_order_hold,
    firmware_correlate,
    firmware_rx_samples,
    parse_file_read_response,
    render_alignment_svg,
    scan_alignment,
    unpack_channels,
    validate_marker_window,
    validate_capture,
)
from tools.calibration_ring_validate.calibration_clk_codebook_eval import (
    marker_raw_waveform,
)
from tools.scpi_common.scpi_serial import scpi_response_matches_command


def test_unpack_interleaved_channels_in_capture_order() -> None:
    pairs = [0b00, 0b01, 0b10, 0b11] * 4
    word = sum(pair << (index * 2) for index, pair in enumerate(pairs))
    capture = validate_capture({
        "schema": "HAOFV_MARKER_CAPTURE_V1",
        "node": 1,
        "epoch": 2,
        "calibration_generation": 3,
        "tick_resolution_ns": 4,
        "raw_interleaved_word_count": 1,
        "raw_interleaved_sample_capacity": 16,
        "raw_interleaved_words": [word],
    })
    forward, incoming = unpack_channels(capture)
    assert forward[:4] == [0, 1, 0, 1]
    assert incoming[:4] == [0, 0, 1, 1]


def test_one_ns_scan_recovers_delay_and_inversion() -> None:
    reference = [0] * 10 + [1, 0, 1, 1, 0, 0, 1, 0] * 8 + [0] * 10
    delay = 7
    candidate = [1] * delay + [bit ^ 1 for bit in reference]
    result = scan_alignment(
        reference, candidate,
        shift_min_ns=-12, shift_max_ns=12, step_ns=1)
    assert result["best"]["polarity"] == "inverted"
    assert result["best"]["candidate_delay_ns"] == delay
    assert result["best"]["move_candidate_by_ns"] == -delay
    assert result["best"]["distance"] == 0


def test_zero_order_hold_declares_original_sample_information() -> None:
    assert expand_zero_order_hold([0, 1], 4, 1) == [0, 0, 0, 0, 1, 1, 1, 1]


def test_parse_storage_read_page() -> None:
    response = '"OK",9,128,4,4,132,1,1234,0,"01020304"'
    assert scpi_response_matches_command(
        'SYSTem:STORage:FILE:READ? "/cal/capture.json",128,4',
        response) is True
    page = parse_file_read_response(response, 128)
    assert page["payload"] == bytes((1, 2, 3, 4))
    assert page["file_size"] == 132
    assert page["eof"] is True


def test_analysis_console_summary_keeps_best_without_scan(tmp_path) -> None:
    result = {
        "schema": "HAOFV_MARKER_WAVEFORM_ANALYSIS_V1",
        "measured_tick_resolution_ns": 4,
        "scan_step_ns": 1,
        "one_ns_scan_is_not_one_ns_measurement_resolution": True,
        "alignment": {
            "best": {"candidate_delay_ns": -76, "distance": 0},
            "scan": [{"candidate_delay_ns": value} for value in range(10)],
        },
    }
    summary = analysis_console_summary(result, tmp_path / "analysis.json")
    assert summary["best"]["candidate_delay_ns"] == -76
    assert "scan" not in summary


def _capture_from_incoming(samples: list[int], *, offset: int = 0) -> dict[str, object]:
    padded = samples + [0] * ((-len(samples)) % 16)
    words = []
    for start in range(0, len(padded), 16):
        word = 0
        for index, sample in enumerate(padded[start:start + 16]):
            word |= int(sample) << (index * 2 + 1)
        words.append(word)
    return validate_capture({
        "schema": "HAOFV_MARKER_CAPTURE_V1",
        "node": 2,
        "epoch": 68,
        "calibration_generation": 29,
        "offset_sample_count": offset,
        "tick_resolution_ns": 4,
        "raw_interleaved_word_count": len(words),
        "raw_interleaved_sample_capacity": len(words) * 16,
        "raw_interleaved_words": words,
    })


def test_firmware_replay_accepts_follower_marker_and_flags() -> None:
    expected, vector = marker_raw_waveform(
        codebook_id=1, epoch=68, master_slot=0)
    offset = 1
    prefix = vector.half_chip_samples + 1 + offset + 1
    capture = _capture_from_incoming(expected[prefix:] + [0] * 32,
                                     offset=offset)
    observed, actual_prefix = firmware_rx_samples(
        capture, "incoming_link", half_chip_samples=vector.half_chip_samples,
        role="follower")
    assert actual_prefix == prefix
    result = firmware_correlate(expected, observed)
    validation = validate_marker_window(
        observed, result["best_lag_sample"],
        half_chip_samples=vector.half_chip_samples,
        expected_header=vector.header)
    assert result["detected_polarity"] == "normal"
    assert result["best_distance"] == 0
    assert validation["all_flags_valid"] is True


def test_firmware_replay_detects_inverted_polarity() -> None:
    expected, _ = marker_raw_waveform(
        codebook_id=0, epoch=3, master_slot=0)
    observed = [bit ^ 1 for bit in expected] + [0] * 16
    result = firmware_correlate(expected, observed)
    assert result["detected_polarity"] == "inverted"
    assert result["reject_reason"] == 4
    assert result["inverted_best_distance"] == 0


def test_capture_sample_count_trims_word_padding() -> None:
    capture = _capture_from_incoming([0, 1] * 9)
    capture["raw_interleaved_sample_count"] = 18
    _, incoming = unpack_channels(capture)
    assert incoming == [0, 1] * 9


def test_svg_contains_reference_comparison_and_best_in_1us_window() -> None:
    reference = [0] * 100 + [1] * 100 + [0] * 100
    candidate = [0] * 20 + reference
    svg = render_alignment_svg(
        reference, candidate, step_ns=1, measured_tick_ns=4,
        best_delay_ns=20, window_start_ns=0,
        window_duration_ns=1000,
        title="row70 link comparison")
    assert "candidate comparison (raw)" in svg
    assert "candidate best delay +20 ns" in svg
    assert "window 1 us" in svg
    assert "red = mismatch" in svg
