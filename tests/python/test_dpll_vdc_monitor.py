from __future__ import annotations

import pytest

from tools.dpll_vdc_monitor.dpll_vdc_monitor import (
    DPLL_VECTOR_FIELDS,
    VDC_VECTOR_FIELDS,
    VECTOR_FLAG_LOCKED,
    VECTOR_FLAG_VALID,
    BoardSample,
    _board_summary,
    _svg,
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
        vdc_vector={"flags": VECTOR_FLAG_VALID},
        dpll_vector={
            "flags": VECTOR_FLAG_VALID | VECTOR_FLAG_LOCKED,
            "state": 5,
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


def test_observer_summary_does_not_require_ring_feedback() -> None:
    sample = BoardSample(
        ts_utc="2026-08-28T00:00:00+00:00",
        elapsed_s=1.0,
        board="NO5",
        port="COM25",
        tdma={"ring_enabled": 0},
        vdc_status={},
        dpll_status={},
        readiness={
            "timestamp_source": 2,
            "timestamp_resolution_ns": 4,
            "timestamp_eligible": 1,
            "timestamp_flags": 2,
            "last_reject_code": 0,
        },
        vdc_vector={"flags": VECTOR_FLAG_VALID},
        dpll_vector={"flags": VECTOR_FLAG_VALID | VECTOR_FLAG_LOCKED,
                     "state": 5},
        trigger_sequence=4,
        trigger_interval_ms=1.0,
        simultaneous_feedback=False,
    )
    summary = _board_summary([sample], expected_interval_ms=1.0,
                             interval_tolerance_ms=0.1, observer=True)
    assert summary["role"] == "observer"
    assert summary["simultaneous_feedback"] is False
    assert summary["trigger_sequence_monotonic"] is True


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
