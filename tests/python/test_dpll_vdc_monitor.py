from __future__ import annotations

import pytest

from tools.dpll_vdc_monitor.dpll_vdc_monitor import (
    DPLL_VECTOR_FIELDS,
    VDC_VECTOR_FIELDS,
    VECTOR_FLAG_LOCKED,
    VECTOR_FLAG_VALID,
    SMA_LOCK_EXPECTED_MASK,
    BoardSample,
    _board_summary,
    _svg,
    _select_trigger_sequence,
    parse_board_arg,
    parse_vector_response,
    _ring_sequence_consistency,
)


def test_monitor_accepts_only_numbered_board_names() -> None:
    assert parse_board_arg("no5=COM7").name == "NO5"
    assert parse_board_arg("NO8=/dev/ttyUSB7").port == "/dev/ttyUSB7"
    with pytest.raises(ValueError, match="NO1..NO8"):
        parse_board_arg("observer=COM7")


def test_vector_parser_requires_current_status_and_field_count() -> None:
    response = "OK," + ",".join(str(index) for index in range(len(VDC_VECTOR_FIELDS)))
    parsed = parse_vector_response(response, VDC_VECTOR_FIELDS)
    assert parsed["flags"] == 0
    assert parsed["payload_crc32"] == len(VDC_VECTOR_FIELDS) - 1

    with pytest.raises(ValueError, match="expected OK"):
        parse_vector_response("UNAVAILABLE", VDC_VECTOR_FIELDS)
    with pytest.raises(ValueError, match="field count"):
        parse_vector_response("OK,1", DPLL_VECTOR_FIELDS)


def test_observer_trigger_sequence_prefers_publish_counter() -> None:
    # NO5 is out of the ring: ring counters and source_update_seq can remain
    # zero while core1 continues publishing a fresh vector each service.
    assert _select_trigger_sequence(
        {"ring_enabled": 0, "ring_seq": 0},
        {"publish_sequence": 12, "source_update_seq": 0},
        {"publish_sequence": 13, "source_update_seq": 0},
    ) == 13

    # In-ring TDMA evidence remains authoritative when available.
    assert _select_trigger_sequence(
        {"ring_clock_observation_sequence": 42, "ring_seq": 41},
        {"publish_sequence": 12},
        {"publish_sequence": 13},
    ) == 42

    # Older firmware remains readable through the compatibility fallback.
    assert _select_trigger_sequence(
        {}, {"source_update_seq": 7}, {"source_update_seq": 8}
    ) == 8


def test_board_summary_requires_simultaneous_hardware_evidence() -> None:
    sample = BoardSample(
        ts_utc="2026-08-28T00:00:00+00:00",
        elapsed_s=1.0,
        board="NO5",
        port="COM7",
        tdma={
            "ring_up_running": 1,
            "ring_down_running": 1,
            "simultaneous_feedback_loop_evidence": 1,
        },
        vdc_status={},
        dpll_status={},
        readiness={
            "timestamp_source": 2,
            "timestamp_resolution_ns": 4,
            "timestamp_eligible": 1,
            "timestamp_flags": 2,
            "last_reject_code": 0,
        },
        vdc_vector={"flags": VECTOR_FLAG_VALID, "gate_passed": 1},
        dpll_vector={
            "flags": VECTOR_FLAG_VALID | VECTOR_FLAG_LOCKED,
            "state": 5,
            "gate_passed": 1,
            "quality_health_state": 1,
            "quality_lock_quality_tier": 2,
            "dco_phase_offset_ns": 3,
            "dco_period_adjust_ppb": 4,
        },
        trigger_sequence=10,
        trigger_interval_ms=1.0,
        simultaneous_feedback=True,
    )
    summary = _board_summary([sample], expected_interval_ms=1.0,
                             interval_tolerance_ms=0.1)
    assert summary["vector_valid"] is True
    assert summary["timestamp_eligible"] is True
    assert summary["simultaneous_feedback"] is True
    assert summary["dpll_locked"] is True
    assert summary["trigger_interval_ok"] is True

    sample.readiness["timestamp_flags"] = 3
    degraded = _board_summary([sample], expected_interval_ms=1.0,
                              interval_tolerance_ms=0.1)
    assert degraded["timestamp_eligible"] is False


def test_observer_summary_uses_sma_lock_bits_not_ring_vectors() -> None:
    sample = BoardSample(
        ts_utc="2026-08-28T00:00:00+00:00",
        elapsed_s=1.0,
        board="NO5",
        port="COM25",
        tdma={},
        vdc_status={},
        dpll_status={},
        readiness={
            "timestamp_source": 2,
            "timestamp_resolution_ns": 4,
            "timestamp_eligible": 1,
            "timestamp_flags": 2,
            "last_reject_code": 0,
        },
        vdc_vector={}, dpll_vector={},
        trigger_sequence=0,
        trigger_interval_ms=None,
        simultaneous_feedback=False,
        sma_input={"base_pin": 20, "pin_count": 4,
                   "level_mask": SMA_LOCK_EXPECTED_MASK},
    )
    summary = _board_summary([sample], expected_interval_ms=1.0,
                             interval_tolerance_ms=0.1, observer=True)
    assert summary["role"] == "observer"
    assert summary["simultaneous_feedback"] is False
    assert summary["vector_valid"] is False
    assert summary["sma_all_locked"] is True

    sample.sma_input["level_mask"] = 0x07
    unlocked = _board_summary([sample], expected_interval_ms=1.0,
                              interval_tolerance_ms=0.1, observer=True)
    assert unlocked["sma_all_locked"] is False


def test_ring_sequence_consistency_excludes_out_of_ring_observer() -> None:
    def sample(name: str, sequence: int, *, enabled: int) -> BoardSample:
        return BoardSample(
            ts_utc="2026-08-28T00:00:00+00:00", elapsed_s=1.0,
            board=name, port="COM", tdma={"ring_enabled": enabled,
                                           "ring_seq": sequence},
            vdc_status={}, dpll_status={}, readiness={}, vdc_vector={},
            dpll_vector={}, trigger_sequence=sequence,
            trigger_interval_ms=None, simultaneous_feedback=False)

    samples = {
        "NO1": [sample("NO1", 20, enabled=1)],
        "NO2": [sample("NO2", 21, enabled=1)],
        "NO5": [sample("NO5", 900, enabled=0)],
    }
    assert _ring_sequence_consistency(samples, tolerance=1) == (True, 1)


def test_ring_sequence_consistency_does_not_mix_reference_local_counter() -> None:
    def sample(name: str, *, local: int, reference: int,
               ring_seq: int, observation: int = 0) -> BoardSample:
        return BoardSample(
            ts_utc="2026-08-28T00:00:00+00:00", elapsed_s=1.0,
            board=name, port="COM", tdma={
                "ring_enabled": 1, "ring_seq": ring_seq,
                "ring_local_slot_id": local,
                "ring_reference_slot_id": reference,
                "ring_clock_observation_valid": int(observation > 0),
                "ring_clock_observation_sequence": observation,
            }, vdc_status={}, dpll_status={}, readiness={}, vdc_vector={},
            dpll_vector={}, trigger_sequence=ring_seq,
            trigger_interval_ms=None, simultaneous_feedback=False)

    samples = {
        "NO1": [sample("NO1", local=0, reference=0, ring_seq=900000)],
        "NO2": [sample("NO2", local=1, reference=0, ring_seq=900100,
                       observation=225000)],
        "NO3": [sample("NO3", local=2, reference=0, ring_seq=900200,
                       observation=225001)],
        "NO5": [sample("NO5", local=4, reference=0, ring_seq=0)],
    }
    assert _ring_sequence_consistency(samples, tolerance=1) == (True, 1)


def test_monitor_svg_is_read_only_observation_report() -> None:
    svg = _svg({"NO5": []}, [{
        "board": "NO5",
        "ring_up_running": True,
        "ring_down_running": True,
        "simultaneous_feedback": True,
        "vector_valid": True,
        "timestamp_eligible": True,
        "dpll_state": 5,
        "trigger_sequence": 10,
        "trigger_interval_ok": True,
        "dpll_locked": True,
        "phase_offset_ns": 0,
        "period_adjust_ppb": 0,
        "quality_health_state": 1,
        "quality_lock_quality_tier": 2,
        "errors": [],
    }], duration_s=2.0, expected_interval_ms=1.0)
    assert svg.startswith("<svg ")
    assert "read-only observation" in svg
    assert "NO5" in svg
