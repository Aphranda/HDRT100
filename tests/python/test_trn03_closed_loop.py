from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = ROOT / "tools" / "calibration_ring_validate"
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

import trn03_closed_loop as trn03  # noqa: E402
from trn03_closed_loop import (  # noqa: E402
    arm_with_evidence,
    counter_deltas,
    parse_active_profile,
    parse_snapshot,
    u32_delta,
    validate_node,
    validate_tx_seed,
)


class FakeBoard:
    address = "node0"


def test_arm_with_evidence_requires_explicit_success(monkeypatch) -> None:
    responses = {
        "SYSTem:TDMA:RING:ARM": "<timeout>",
        "SYSTem:TDMA:RING:ARM:STATus?": "1",
        "SYSTem:ERR?": '0,"No error"',
    }
    monkeypatch.setattr(trn03, "drain_errors", lambda board, args: [
        '0,"No error"'])
    monkeypatch.setattr(
        trn03, "board_command",
        lambda board, command, args: responses[command])
    evidence = arm_with_evidence(FakeBoard(), object())
    assert evidence["arm_result"] == 1
    assert evidence["error_after"] == '0,"No error"'


def test_arm_with_evidence_exposes_rejection_stage(monkeypatch) -> None:
    responses = {
        "SYSTem:TDMA:RING:ARM": "<timeout>",
        "SYSTem:TDMA:RING:ARM:STATus?": "7",
        "SYSTem:ERR?": '-200,"Execution error"',
    }
    monkeypatch.setattr(trn03, "drain_errors", lambda board, args: [
        '0,"No error"'])
    monkeypatch.setattr(
        trn03, "board_command",
        lambda board, command, args: responses[command])
    with pytest.raises(RuntimeError, match="arm_result=7"):
        arm_with_evidence(FakeBoard(), object())


def runtime() -> dict[str, int]:
    return {
        "ring_enabled": 1,
        "ring_node_count": 4,
        "ring_local_node": 2,
        "ring_reference_node": 0,
        "ring_up_running": 1,
        "ring_down_running": 1,
        "ring_seq": 10,
        "ring_last_error": 0,
        "ring_adapter_started": 1,
        "ring_adapter_service_count": 100,
        "ring_up_tx_sequence": 20,
        "ring_down_rx_sequence": 20,
        "ring_up_tx_frame_crc32": 0x1234,
        "ring_down_rx_frame_crc32": 0x1234,
        "ring_idle_beacon_tx_count": 10,
        "ring_idle_beacon_rx_count": 10,
        "ring_adapter_last_error": 0,
        "ring_adapter_tx_count": 20,
        "ring_adapter_rx_count": 20,
        "ring_adapter_rx_bad_count": 0,
        "node_index": 2,
    }


def flight() -> dict:
    return {
        "process": {
            "configured": 1, "active": 1, "local_node": 2,
            "map_apply_count": 5, "input_bytes": 100,
            "output_bytes": 100, "map_reject_count": 0,
            "length_reject_count": 0, "tx_unavailable_count": 0,
            "rx_bitmap_scan_count": 5, "rx_bitmap_hit_count": 5,
            "rx_bitmap_duplicate_count": 0,
        },
        "fifo": {
            "tx_publish_count": 1, "tx_publish_reject_count": 0,
            "tx_acquire_count": 5, "tx_image_stale_count": 0,
            "tx_reuse_count": 4, "tx_release_count": 5,
            "tx_ready_count": 0, "tx_active_buffer": 0,
            "tx_active_generation": 1,
            "rx_publish_count": 5, "rx_mirror_drop_count": 0,
            "rx_publish_drop_count": 0, "rx_acquire_count": 5,
            "rx_release_count": 5, "rx_queued_count": 0,
            "rx_parse_count": 0,
        },
    }


def increment(value: dict, amount: int = 1) -> dict:
    return {key: item + amount for key, item in value.items()}


def test_u32_delta_wraps() -> None:
    assert u32_delta(0xFFFFFFFE, 1) == 3
    assert counter_deltas({"x": 8}, {"x": 11}, ("x",)) == {"x": 3}


def test_parse_snapshot_requires_exact_field_count() -> None:
    assert parse_snapshot("1,2", ("a", "b"), "x") == {"a": 1, "b": 2}


def test_parse_active_profile_accepts_extended_status() -> None:
    profile = parse_active_profile(
        "7,10000000,1000000,4096,3,1234,99,98,97,96", "x")
    assert profile["level"] == 7
    assert profile["profile_crc32"] == 1234


def test_validate_node_accepts_growing_runtime_and_fifo() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count", "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {
        "process": increment(before_flight["process"], 3),
        "fifo": increment(before_flight["fifo"], 3),
    }
    for field in ("configured", "active", "local_node",
                  "map_reject_count", "length_reject_count"):
        after_flight["process"][field] = before_flight["process"][field]
    for field in ("tx_publish_reject_count", "rx_mirror_drop_count",
                  "rx_publish_drop_count"):
        after_flight["fifo"][field] = before_flight["fifo"][field]
    # Publication is proven in the pre-ARM seed phase. Release only occurs
    # when a newer TX image replaces ACTIVE, while rx_parse_count is a gauge.
    for field in ("tx_publish_count", "tx_release_count", "rx_parse_count"):
        after_flight["fifo"][field] = before_flight["fifo"][field]
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight,
        [], {"tx_publish_count": 1, "tx_publish_reject_count": 0})
    assert errors == []


def test_validate_tx_seed_uses_pre_arm_publish_delta() -> None:
    before = flight()
    after = {group: dict(values) for group, values in before.items()}
    after["fifo"]["tx_publish_count"] += 1
    errors, deltas = validate_tx_seed(before, after)
    assert errors == []
    assert deltas == {
        "tx_publish_count": 1,
        "tx_publish_reject_count": 0,
    }


def test_validate_tx_seed_reports_rejected_or_missing_publish() -> None:
    before = flight()
    after = {group: dict(values) for group, values in before.items()}
    after["fifo"]["tx_publish_reject_count"] += 1
    errors, _ = validate_tx_seed(before, after)
    assert errors == ["fifo_tx_not_published", "fifo_tx_seed_rejected"]


def test_validate_node_reports_stalled_receive_path() -> None:
    before_runtime = runtime()
    after_runtime = dict(before_runtime)
    after_runtime["ring_down_running"] = 0
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight)
    assert "down_not_running" in errors
    assert "adapter_rx_not_growing" in errors
    assert "fifo_rx_not_published" in errors


def test_validate_raw_flight_accepts_no_fifo_exchange() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count", "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight,
        require_process_image=False, physical_after={"program_persona": 12})
    assert errors == []


def test_validate_raw_flight_ignores_process_image_backpressure() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count", "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    after_flight["fifo"]["tx_publish_reject_count"] += 100
    after_flight["fifo"]["rx_mirror_drop_count"] += 10
    after_flight["process"]["map_reject_count"] += 5
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight,
        require_process_image=False, physical_after={"program_persona": 12})
    assert errors == []


def test_validate_raw_flight_requires_role_persona() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count", "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight,
        require_process_image=False, physical_after={"program_persona": 1})
    assert "physical_flight_persona_mismatch" in errors


def test_validate_node_requires_rx_acquire_and_release() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count", "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {
        "process": increment(before_flight["process"], 3),
        "fifo": increment(before_flight["fifo"], 3),
    }
    for field in ("configured", "active", "local_node",
                  "map_reject_count", "length_reject_count"):
        after_flight["process"][field] = before_flight["process"][field]
    for field in ("tx_publish_reject_count", "rx_mirror_drop_count",
                  "rx_publish_drop_count", "rx_acquire_count",
                  "rx_release_count"):
        after_flight["fifo"][field] = before_flight["fifo"][field]
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight)
    assert "fifo_rx_not_acquired" in errors
    assert "fifo_rx_not_released" in errors
