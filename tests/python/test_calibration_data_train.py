from pathlib import Path

from tools.calibration_ring_validate.calibration_data_train import (
    DATA_FIELDS,
    DESTINATION_REQUIRED_FLAGS,
    FAULT_DMA_OVERRUN,
    FAULT_PIO_STALL,
    FLAG_DIAGNOSTIC_ONLY,
    FLAG_DMA_COMPLETE,
    FLAG_HARDWARE_MARKER,
    decode_observed_header,
    direction_endpoints,
    parse_data_status,
    parse_storage_read,
    summarize_data_capture,
    summarize_repeat_matrix,
    validate_expected_rejection,
    validate_link,
    validate_responder_wire_fault,
    validate_transport_fault,
)


ROOT = Path(__file__).resolve().parents[2]
from tools.calibration_ring_validate.calibration_data_waveform import (
    render_data_waveform,
    unpack_data_capture,
)
from tools.calibration_ring_validate.calibration_clk_codebook_eval import (
    marker_raw_waveform,
)
from tools.calibration_ring_validate.trn02_offset_fault import (
    classify_offset_fault,
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
        "link_base_delay_ns": 40,
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


def test_observed_wire_header_proves_epoch_and_crc_faults() -> None:
    header = (0 << 14) | (0 << 12) | (6 << 4) | (0 << 1) | 0
    inverse = header ^ 0xFFFF
    from tools.calibration_ring_validate.calibration_clk_codebook_eval import (
        crc8_atm,
    )
    calculated = crc8_atm(bytes((
        header >> 8, header & 0xFF, inverse >> 8, inverse & 0xFF)))
    epoch_row = make_row(
        observed_header_fields_valid=1,
        observed_header=header,
        observed_header_inverse=inverse,
        observed_header_crc8=calculated,
        correlation_reject_reason=9)
    assert decode_observed_header(epoch_row)["epoch"] == 6
    assert validate_responder_wire_fault(
        epoch_row, expected_epoch=7, expected_source_node=0,
        wire_epoch=6) == []

    crc_row = make_row(
        observed_header_fields_valid=1,
        observed_header=header,
        observed_header_inverse=inverse,
        observed_header_crc8=calculated ^ 0x01,
        correlation_reject_reason=8)
    assert validate_responder_wire_fault(
        crc_row, expected_epoch=6, expected_source_node=0,
        header_crc8_xor=0x01) == []
    crc_row["observed_header_crc8"] = calculated
    assert validate_responder_wire_fault(
        crc_row, expected_epoch=6, expected_source_node=0,
        header_crc8_xor=0x01) == ["receiver_observed_crc8_xor"]


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


def test_validate_link_rejects_offset_induced_timeout() -> None:
    source = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                      DESTINATION_REQUIRED_FLAGS,
                      timeout_count=1)
    destination = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                           FLAG_HARDWARE_MARKER | FLAG_DMA_COMPLETE,
                           observed_crc32=0)
    result = validate_link(source, destination)
    assert result["passed"] is False
    assert "source_timeout_count" in result["errors"]


def test_validate_link_rejects_stale_requested_offset_readback() -> None:
    source = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                      DESTINATION_REQUIRED_FLAGS,
                      configured_data_offset_sample_count=5)
    destination = make_row(flags=FLAG_DIAGNOSTIC_ONLY |
                           FLAG_HARDWARE_MARKER | FLAG_DMA_COMPLETE,
                           observed_crc32=0,
                           configured_data_offset_sample_count=5)
    result = validate_link(
        source, destination,
        expected_config={"configured_data_offset_sample_count": 4})
    assert result["passed"] is False
    assert result["errors"] == [
        "source_configured_data_offset_sample_count_readback",
        "destination_configured_data_offset_sample_count_readback",
    ]


def test_expected_margin_rejection_requires_receiver_role_and_exact_reason(
        ) -> None:
    receiver = make_row(state=4, reject_reason=14)
    responder = make_row(state=3, reject_reason=0)
    result = validate_expected_rejection(receiver, responder, 14)
    assert result["passed"] is True
    assert result["accepted"] is False
    assert result["expected_reject_name"] == "MARGIN"
    assert result["active_candidate_allowed"] is False

    wrong_endpoint = validate_expected_rejection(responder, receiver, 14)
    assert wrong_endpoint["passed"] is False
    assert "receiver_state" in wrong_endpoint["errors"]
    assert "responder_state" in wrong_endpoint["errors"]


def test_transport_pio_stall_belongs_to_initiator_and_keeps_root_cause() -> None:
    receiver = make_row(
        state=4, reject_reason=11,
        diagnostic_fault_flags=FAULT_PIO_STALL,
        pio_stall_count=1, dma_overrun_count=1, timeout_count=0)
    responder = make_row(
        state=3, reject_reason=0, diagnostic_fault_flags=0)
    result = validate_transport_fault(
        receiver, responder, expected_reason=11,
        expected_fault_flags=FAULT_PIO_STALL)
    assert result["passed"] is True
    assert result["expected_reject_name"] == "PIO_STALL"
    assert result["active_candidate_allowed"] is False


def test_transport_pio_stall_rejects_timeout_override() -> None:
    receiver = make_row(
        state=4, reject_reason=11,
        diagnostic_fault_flags=FAULT_PIO_STALL,
        pio_stall_count=1, timeout_count=1)
    responder = make_row(state=3, reject_reason=0)
    result = validate_transport_fault(
        receiver, responder, expected_reason=11,
        expected_fault_flags=FAULT_PIO_STALL)
    assert result["passed"] is False
    assert "receiver_timeout_overrode_pio_stall" in result["errors"]


def test_transport_dma_short_keeps_partial_evidence_on_initiator() -> None:
    receiver = make_row(
        state=4, reject_reason=10,
        diagnostic_fault_flags=FAULT_DMA_OVERRUN,
        dma_overrun_count=1, captured_sample_count=640)
    responder = make_row(state=3, reject_reason=0)
    result = validate_transport_fault(
        receiver, responder, expected_reason=10,
        expected_fault_flags=FAULT_DMA_OVERRUN)
    assert result["passed"] is True
    assert result["expected_reject_name"] == "DMA"


def test_paused_rx_dma_keeps_logical_remainder_until_fifo_stalls() -> None:
    source = "\n".join(
        (ROOT / "components" / "tdma" / "src" / name).read_text(
            encoding="utf-8")
        for name in (
            "tdma_pio_spi_phys.c",
            "tdma_pio_spi_phys_data_train_restored.inc",
        )
    )
    assert "const uint32_t hardware_data_remaining" in source
    assert "const uint32_t data_remaining = inject_rx_dma_pause" in source
    assert "? phys->data_train.capture_word_count" in source
    assert "PIO FIFO-full evidence" in source


def test_offset_fault_is_never_promoted_to_trn03() -> None:
    result = classify_offset_fault(0, 11, -10, 10, 0)
    assert result["expected_failure"] == "ARM_REJECTED_OFFSET_RANGE"
    assert result["accepted"] is False
    assert result["active_candidate_allowed"] is False
    assert result["trn03_staging_allowed"] is False


def test_offset_fault_can_expect_window_timeout() -> None:
    result = classify_offset_fault(0, 9, -4, 4, 1)
    assert result["expected_failure"] == "TIMEOUT_EXPECTED_WINDOW_MISSED"


def test_parse_storage_read_and_summarize_capture() -> None:
    page = parse_storage_read(
        'OK,7,0,2,2,2,1,123,0,"0102"', 0)
    assert page["payload"] == b"\x01\x02"
    assert page["eof"] is True

    summary = summarize_data_capture({
        "schema": "HAOFV_DATA_TRAIN_CAPTURE_V3",
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


def test_selected_link_repeats_form_stable_window_without_fake_full_matrix(
        ) -> None:
    trials = []
    for repeat, calibrated in enumerate((3, 4, 4, 3), 1):
        trials.append({
            "link": 0,
            "repeat_index": repeat,
            "passed": True,
            "marker_source_node": 0,
            "marker_destination_node": 1,
            "data_source_node": 1,
            "data_destination_node": 0,
            "marker_direction": "forward",
            "data_direction": "reverse",
            "resolved_offset_sample_count": calibrated - 4,
            "calibrated_data_offset_sample_count": calibrated,
            "marker_data_skew_ns": 0,
            "source": {
                "best_distance": 10,
                "margin": 100,
                "calibration_generation": 210,
                "topology_generation": 3,
                "topology_crc32": 22,
                "profile_crc32": 23,
                "schedule_crc32": 24,
                "sample_period_ns": 4,
            },
        })
    summary = summarize_repeat_matrix(
        trials, 4, 4, 1, selected_links=[0])
    assert summary["passed"] is True
    assert summary["selected_links"] == [0]
    assert summary["complete_node_matrix"] is False
    assert summary["links"][0]["offset_histogram"] == {"-1": 2, "0": 2}
    assert summary["links"][0]["residual_offset_histogram"] == {
        "-1": 2, "0": 2}
    assert summary["links"][0]["calibrated_offset_histogram"] == {
        "3": 2, "4": 2}
    assert summary["offset_matrix"]["candidate_values_by_node"][0] == [3, 4]
    assert summary["offset_matrix"]["missing_nodes"] == [1, 2, 3]
    assert "data_offset_matrix_incomplete" not in summary["gate_failures"]


def test_data_waveform_renders_expected_capture(tmp_path) -> None:
    expected, _ = marker_raw_waveform(
        codebook_id=0, epoch=7, source_node=0, polarity=0)
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


def test_data_waveform_applies_header_epoch_and_crc_gates(tmp_path) -> None:
    expected, _ = marker_raw_waveform(
        codebook_id=0, epoch=7, source_node=0, polarity=0)
    stale, _ = marker_raw_waveform(
        codebook_id=0, epoch=6, source_node=0, polarity=0)

    def make_capture(samples: list[int]) -> tuple[dict[str, object], list[int]]:
        padded = [0] * 2 + samples
        words = []
        for start in range(0, len(padded), 32):
            word = 0
            for bit, value in enumerate(padded[start:start + 32]):
                word |= int(value) << bit
            words.append(word)
        return unpack_data_capture({
            "schema": "HAOFV_DATA_TRAIN_CAPTURE_V3",
            "source_node": 0,
            "destination_node": 1,
            "epoch": 7,
            "calibration_generation": 214,
            "sample_period_ns": 4,
            "link_base_delay_ns": 40,
            "max_best_distance": 512,
            "min_margin": 0,
            "raw_word_count": len(words),
            "raw_sample_count": len(padded),
            "raw_words": words,
        })

    capture, samples = make_capture(stale)
    stale_result = render_data_waveform(
        capture, samples, codebook_id=0, svg_path=tmp_path / "stale.svg",
        window_start_ns=0, window_duration_ns=1000,
        marker_direction="forward", data_direction="reverse")
    assert stale_result["correlation"]["accepted"] is False
    assert stale_result["correlation"]["reject_name"] == "HEADER_MISMATCH"
    decoded_stale_header = int(
        stale_result["correlation"]["marker_validation"]["decoded_header"])
    assert ((decoded_stale_header >> 4) & 0xFF) == 6

    bad_crc = list(expected)
    crc_logical_bit = 13 + 16 + 16 + 7
    raw_start = crc_logical_bit * 2 * 5
    for index in range(raw_start, raw_start + 2 * 5):
        bad_crc[index] ^= 1
    capture, samples = make_capture(bad_crc)
    crc_result = render_data_waveform(
        capture, samples, codebook_id=0, svg_path=tmp_path / "crc.svg",
        window_start_ns=0, window_duration_ns=1000,
        marker_direction="forward", data_direction="reverse")
    assert crc_result["correlation"]["accepted"] is False
    assert crc_result["correlation"]["reject_name"] == "HEADER_CRC"
