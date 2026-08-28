from __future__ import annotations

import json
import zlib
from pathlib import Path
from types import SimpleNamespace

import tools.calibration_ring_validate.trn03_waveform as waveform_module
from tools.calibration_ring_validate.trn03_waveform import (
    analyze_capture_set,
    analyze_sck_timing,
    best_alignment,
    byte_bits,
    decode_transport_evidence,
    latest_complete_packet,
    read_tdma_schedule,
    save_ring_capture,
    validate_capture_schedule,
    validate_capture,
)
from tools.scpi_common.scpi_serial import scpi_response_matches_command


def capture(node: int, rx: list[int], tx: list[int]) -> dict:
    rx_frame = ([0x54, 0x44, len(rx), 0, *rx] if rx else [])
    tx_frame = ([0x54, 0x44, len(tx), 0, *tx] if tx else [])
    return {
        "schema": "HAOFV_TRN03_RING_CAPTURE_V1",
        "node": node,
        "node_count": 4,
        "build_id": "1",
        "calibration_generation": 101,
        "capture_epoch": 10,
        "capture_version": 1,
        "baud_hz": 10_000_000,
        "bit_period_ns": 100,
        "capture_anchor": "normal_rx_sck_rising_edge",
        "rx_produced_bytes": len(rx_frame),
        "tx_produced_bytes": len(tx_frame),
        "rx_byte_count": len(rx_frame),
        "tx_byte_count": len(tx_frame),
        "rx_bytes": rx_frame,
        "tx_bytes": tx_frame,
    }


def config() -> dict:
    return {
        "node_count": 4,
        "calibration_generation": 101,
        "links": [{
            "link_index": node,
            "marker_source_node": node,
            "marker_destination_node": (node + 1) % 4,
            "data_source_node": (node + 1) % 4,
            "data_destination_node": node,
            "marker_direction": "forward",
            "data_direction": "reverse",
        } for node in range(4)],
    }


def schedule_response(*, enabled_mask: int, run_count: int,
                      overrun_count: int = 0,
                      deadline_miss_count: int = 0) -> str:
    values = [1, 250_000_000, 250_000, 5, enabled_mask, 0, 100, 0]
    for phase in range(5):
        values.extend([
            phase * 1000, phase * 1000 + 999, 34_000,
            phase * 1000, 1_000, 2_000,
            run_count if phase == 3 else 100,
            0, 0,
            overrun_count if phase == 3 else 0,
            deadline_miss_count if phase == 3 else 0,
        ])
    return ",".join(str(value) for value in values)


def test_byte_bits_and_alignment_use_msb_first() -> None:
    bits = byte_bits([0xA5])
    assert bits == [1, 0, 1, 0, 0, 1, 0, 1]
    assert best_alignment(bits, [0, 0] + bits)["lag_bits"] == 2


def test_validate_capture_accepts_empty_rx_evidence() -> None:
    assert validate_capture(capture(1, [], [0x54]))["node"] == 1


def test_v2_capture_accepts_raw_tx_without_frame_judgment() -> None:
    value = capture(0, [], [0x12, 0x34])
    value.update({
        "schema": "HAOFV_TRN03_RING_CAPTURE_V2",
        "capture_version": 2,
        "tx_complete_frame_count": 17,
    })
    assert validate_capture(value)["tx_bytes"] == [0x54, 0x44, 2, 0,
                                                     0x12, 0x34]
    value["tx_bytes"] = [0, 0, 0, 0, 0, 0]
    assert validate_capture(value)["tx_bytes"] == [0, 0, 0, 0, 0, 0]


def test_v3_capture_measures_raw_sck_frequency_and_duty() -> None:
    value = capture(1, [0xAA], [])
    # 4 ns samples, repeated 24 ns high / 76 ns low at 10 MHz.
    samples = [0] * 10 + ([1] * 6 + [0] * 19) * 10
    samples = samples[:256]
    words = []
    for start in range(0, len(samples), 32):
        word = 0
        for bit, sample in enumerate(samples[start:start + 32]):
            word |= sample << bit
        words.append(word)
    value.update({
        "schema": "HAOFV_TRN03_RING_CAPTURE_V3",
        "capture_version": 3,
        "tx_complete_frame_count": 0,
        "sck_sample_period_ns": 4,
        "sck_sample_count": len(samples),
        "sck_word_count": len(words),
        "rx_sck_words": words,
    })
    validated = validate_capture(value)
    timing = analyze_sck_timing(validated)
    assert timing["available"] is True
    assert timing["period_ns"] == 100.0
    assert timing["clock_high_ns"] == 24.0
    assert timing["clock_low_ns"] == 76.0
    assert timing["frequency_ok"] is True
    assert timing["duty_ok"] is False
    assert timing["passed"] is False
    assert timing["gate_failures"] == ["duty"]


def test_v3_capture_accepts_quantized_ten_mhz_sck() -> None:
    value = capture(0, [0xAA], [])
    samples = [0] * 10 + ([1] * 12 + [0] * 13) * 10
    samples = samples[:256]
    words = []
    for start in range(0, len(samples), 32):
        word = 0
        for bit, sample in enumerate(samples[start:start + 32]):
            word |= sample << bit
        words.append(word)
    value.update({
        "schema": "HAOFV_TRN03_RING_CAPTURE_V3",
        "capture_version": 3,
        "tx_complete_frame_count": 0,
        "sck_sample_period_ns": 4,
        "sck_sample_count": len(samples),
        "sck_word_count": len(words),
        "rx_sck_words": words,
    })
    timing = analyze_sck_timing(validate_capture(value))
    assert timing["period_ns"] == 100.0
    assert timing["duty_percent"] == 48.0
    assert timing["period_span_ns"] == 0
    assert timing["frequency_ok"] is True
    assert timing["duty_ok"] is True
    assert timing["passed"] is True


def test_latest_complete_packet_uses_newest_frame_boundary() -> None:
    stream = [0, 0, 0x54, 0x44, 1, 0, 0x11,
              0x54, 0x44, 2, 0, 0x22, 0x33]
    assert latest_complete_packet(stream) == [0x54, 0x44, 2, 0, 0x22, 0x33]


def transport_packet() -> list[int]:
    transport = bytearray(36)
    transport[0:2] = b"TD"
    transport[2] = 1
    transport[3] = 1
    transport[4:6] = len(transport).to_bytes(2, "little")
    transport[6] = 32
    transport[7] = 0
    transport[8:12] = (17).to_bytes(4, "little")
    transport[12] = 1
    transport[13] = 2
    transport[15] = 4
    transport[16:20] = (0x12345678).to_bytes(4, "little")
    transport[20:24] = (0x9ABCDEF0).to_bytes(4, "little")
    identity_input = transport[0:14] + transport[15:16] + transport[16:24]
    transport[24:28] = zlib.crc32(identity_input).to_bytes(4, "little")
    transport[32:36] = b"data"
    transport_crc_input = bytearray(transport)
    transport_crc_input[28:32] = b"\0\0\0\0"
    if (transport_crc_input[13] & 0x04) != 0:
        transport_crc_input = transport_crc_input[:32]
    transport[28:32] = zlib.crc32(transport_crc_input).to_bytes(4, "little")
    return [0x54, 0x44, len(transport), 0, *transport]


def test_transport_evidence_distinguishes_valid_and_corrupted_frames() -> None:
    packet = transport_packet()
    assert latest_complete_packet(packet) == packet
    evidence = decode_transport_evidence(packet)
    assert evidence is not None
    assert evidence["valid"] is True
    assert evidence["result"] == "OK"
    assert evidence["sequence"] == 17
    assert evidence["hop_limit"] == 4
    packet[-1] ^= 1
    corrupted = decode_transport_evidence(packet)
    assert corrupted is not None
    assert corrupted["valid"] is False
    assert corrupted["result"] == "TRANSPORT_CRC_MISMATCH"
    assert corrupted["transport_single_bit_repairs"] == [
        {"transport_byte_offset": 35, "bit": 0}]


def test_ring_capture_scpi_composite_responses_are_preserved() -> None:
    assert scpi_response_matches_command(
        "CALibration:RING:CAPTure:LATCh 101,12345", "101,12345")
    assert scpi_response_matches_command(
        "CALibration:RING:CAPTure:SAVE 101,12345",
        '"OK",17,"/cal/trn03b_node0_g101_e12345.json"')
    assert scpi_response_matches_command(
        "SYSTem:TDMA:LOAD:MASK?", "91")
    assert scpi_response_matches_command(
        "SYSTem:TDMA:LOAD:MASK 91", '"OK",91')


def test_capture_schedule_gate_uses_counter_deltas() -> None:
    board = SimpleNamespace(address="node0")
    args = SimpleNamespace()
    responses = iter((schedule_response(enabled_mask=91, run_count=10),
                      schedule_response(enabled_mask=95, run_count=14)))
    original = waveform_module.board_command
    try:
        waveform_module.board_command = lambda *_args: next(responses)
        before = read_tdma_schedule(board, args)
        after = read_tdma_schedule(board, args)
    finally:
        waveform_module.board_command = original
    result = validate_capture_schedule(before, after)
    assert result["passed"] is True
    assert result["deltas"]["run_count"] == 4


def test_capture_schedule_gate_rejects_overrun_growth() -> None:
    board = SimpleNamespace(address="node0")
    args = SimpleNamespace()
    responses = iter((schedule_response(enabled_mask=91, run_count=10),
                      schedule_response(enabled_mask=95, run_count=14,
                                        overrun_count=1)))
    original = waveform_module.board_command
    try:
        waveform_module.board_command = lambda *_args: next(responses)
        before = read_tdma_schedule(board, args)
        after = read_tdma_schedule(board, args)
    finally:
        waveform_module.board_command = original
    result = validate_capture_schedule(before, after)
    assert result["passed"] is False
    assert "overrun_count_grew" in result["errors"]


def test_ring_capture_retries_a_stale_pending_latch(monkeypatch) -> None:
    latch_calls = 0
    schedule_reads = 0

    def command(_board, value: str, _args) -> str:
        nonlocal latch_calls, schedule_reads
        if value == "SYSTem:TDMA:SCHEDule?":
            schedule_reads += 1
            return schedule_response(
                enabled_mask=91 if schedule_reads == 1 else 95,
                run_count=10 if schedule_reads == 1 else 14)
        if value == "SYSTem:TDMA:LOAD:MASK?":
            return "91"
        if value in ("SYSTem:TDMA:LOAD:MASK 95",
                     "SYSTem:TDMA:LOAD:MASK 91"):
            return '"OK"'
        if value.startswith("CALibration:RING:CAPTure:LATCh"):
            latch_calls += 1
            return "<timeout>" if latch_calls == 1 else "101,10"
        if value == "READ:CALibration:RING:CAPTure?":
            return "2,2,101,10,0,4,0,8,0,8,100,0,2,2,0,2"
        if value.startswith("CALibration:RING:CAPTure:SAVE"):
            return '"OK",7,"/cal/capture.json"'
        if value == "SYSTem:STORage:JOB?":
            return '"DONE",7,0,0,123,0,0,0'
        raise AssertionError(value)

    monkeypatch.setattr(waveform_module, "board_command", command)
    result = save_ring_capture(
        SimpleNamespace(address="node0"),
        SimpleNamespace(capture_timeout=1.0, capture_latch_retries=1),
        calibration_generation=101, capture_epoch=10)
    assert result["latch_attempts"] == 2
    assert result["load_mask_before"] == 91
    assert result["load_mask_during_capture"] == 95
    assert result["load_mask_restored"] == 91
    assert result["schedule_validation"]["passed"] is True
    assert result["capture_debug"]["consumed_sequence"] == 2


def test_ring_capture_retries_transient_load_mask_query(monkeypatch) -> None:
    mask_reads = 0
    schedule_reads = 0

    def command(_board, value: str, _args) -> str:
        nonlocal mask_reads, schedule_reads
        if value == "SYSTem:TDMA:SCHEDule?":
            schedule_reads += 1
            return schedule_response(
                enabled_mask=91 if schedule_reads == 1 else 95,
                run_count=10 if schedule_reads == 1 else 14)
        if value == "SYSTem:TDMA:LOAD:MASK?":
            mask_reads += 1
            return "<timeout>" if mask_reads == 1 else "91"
        if value in ("SYSTem:TDMA:LOAD:MASK 95",
                     "SYSTem:TDMA:LOAD:MASK 91"):
            return '"OK"'
        if value.startswith("CALibration:RING:CAPTure:LATCh"):
            return "101,10"
        if value == "READ:CALibration:RING:CAPTure?":
            return "2,2,101,10,0,4,0,8,0,8,100,0,2,2,0,2"
        if value.startswith("CALibration:RING:CAPTure:SAVE"):
            return '"OK",7,"/cal/capture.json"'
        if value == "SYSTem:STORage:JOB?":
            return '"DONE",7,0,0,123,0,0,0'
        raise AssertionError(value)

    monkeypatch.setattr(waveform_module, "board_command", command)
    result = save_ring_capture(
        SimpleNamespace(address="node0"),
        SimpleNamespace(capture_timeout=1.0, capture_latch_retries=1),
        calibration_generation=101, capture_epoch=10)
    assert mask_reads == 2
    assert result["load_mask_restored"] == 91


def test_ring_capture_accepts_load_mask_readback_after_lost_ack(
        monkeypatch) -> None:
    active_mask = 91
    schedule_reads = 0

    def command(_board, value: str, _args) -> str:
        nonlocal active_mask, schedule_reads
        if value == "SYSTem:TDMA:SCHEDule?":
            schedule_reads += 1
            return schedule_response(
                enabled_mask=91 if schedule_reads == 1 else 95,
                run_count=10 if schedule_reads == 1 else 14)
        if value == "SYSTem:TDMA:LOAD:MASK?":
            return str(active_mask)
        if value.startswith("SYSTem:TDMA:LOAD:MASK "):
            active_mask = int(value.rsplit(" ", 1)[1], 0)
            return "<timeout>"
        if value.startswith("CALibration:RING:CAPTure:LATCh"):
            return "101,10"
        if value == "READ:CALibration:RING:CAPTure?":
            return "2,2,101,10,0,4,0,8,0,8,100,0,2,2,0,2"
        if value.startswith("CALibration:RING:CAPTure:SAVE"):
            return '"OK",7,"/cal/capture.json"'
        if value == "SYSTem:STORage:JOB?":
            return '"DONE",7,0,0,123,0,0,0'
        raise AssertionError(value)

    monkeypatch.setattr(waveform_module, "board_command", command)
    result = save_ring_capture(
        SimpleNamespace(address="node0"),
        SimpleNamespace(capture_timeout=1.0, capture_latch_retries=1),
        calibration_generation=101, capture_epoch=10)
    assert result["load_mask_during_capture"] == 95
    assert result["load_mask_restored"] == 91
    assert active_mask == 91


def test_analysis_uses_configured_reverse_data_source(tmp_path: Path) -> None:
    captures = [
        capture(0, [0x22], [0x10]),
        capture(1, [0x30], [0x22]),
        capture(2, [0x40], [0x30]),
        capture(3, [0x10], [0x40]),
    ]
    paths = []
    for node, value in enumerate(captures):
        path = tmp_path / f"node{node}.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        paths.append(path)
    result = analyze_capture_set(config(), paths, tmp_path / "analysis", 1000)
    node0 = result["nodes"][0]
    assert node0["marker_source_node"] == 3
    assert node0["data_source_node"] == 1
    assert node0["physical_alignment"]["distance"] == 0
    assert (tmp_path / "analysis/node0_ring_capture_1us.svg").exists()
