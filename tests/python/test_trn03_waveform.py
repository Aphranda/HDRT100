from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace

import tools.calibration_ring_validate.trn03_waveform as waveform_module
from tools.calibration_ring_validate.trn03_waveform import (
    analyze_capture_set,
    best_alignment,
    byte_bits,
    latest_complete_packet,
    save_ring_capture,
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


def test_byte_bits_and_alignment_use_msb_first() -> None:
    bits = byte_bits([0xA5])
    assert bits == [1, 0, 1, 0, 0, 1, 0, 1]
    assert best_alignment(bits, [0, 0] + bits)["lag_bits"] == 2


def test_validate_capture_accepts_empty_rx_evidence() -> None:
    assert validate_capture(capture(1, [], [0x54]))["node"] == 1


def test_v2_capture_requires_one_complete_tx_frame() -> None:
    value = capture(0, [], [0x12, 0x34])
    value.update({
        "schema": "HAOFV_TRN03_RING_CAPTURE_V2",
        "capture_version": 2,
        "tx_complete_frame_count": 17,
    })
    assert validate_capture(value)["tx_bytes"][:2] == [0x54, 0x44]
    value["tx_bytes"] = [0, 0, 0, 0, 0, 0]
    try:
        validate_capture(value)
    except ValueError as exc:
        assert "complete TX frame" in str(exc)
    else:
        raise AssertionError("V2 capture accepted TX evidence without header")


def test_latest_complete_packet_uses_newest_frame_boundary() -> None:
    stream = [0, 0, 0x54, 0x44, 1, 0, 0x11,
              0x54, 0x44, 2, 0, 0x22, 0x33]
    assert latest_complete_packet(stream) == [0x54, 0x44, 2, 0, 0x22, 0x33]


def test_ring_capture_scpi_composite_responses_are_preserved() -> None:
    assert scpi_response_matches_command(
        "CALibration:RING:CAPTure:LATCh 101,12345", "101,12345")
    assert scpi_response_matches_command(
        "CALibration:RING:CAPTure:SAVE 101,12345",
        '"OK",17,"/cal/trn03b_node0_g101_e12345.json"')


def test_ring_capture_retries_a_stale_pending_latch(monkeypatch) -> None:
    latch_calls = 0

    def command(_board, value: str, _args) -> str:
        nonlocal latch_calls
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
    assert result["capture_debug"]["consumed_sequence"] == 2


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
