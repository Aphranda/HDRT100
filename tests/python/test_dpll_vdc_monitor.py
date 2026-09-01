from __future__ import annotations

import pytest

from tools.dpll_vdc_monitor.dpll_vdc_monitor import (
    DPLL_VECTOR_FIELDS,
    VDC_VECTOR_FIELDS,
    VECTOR_FLAG_LOCKED,
    VECTOR_FLAG_VALID,
    SMA_LOCK_EXPECTED_MASK,
    PHASE_SELFTEST_FIELDS,
    BoardSample,
    ProgressReporter,
    _effective_phase_pulse_count,
    _progress_board,
    _finish_waveform_capture,
    _parse_storage_read,
    _parse_waveform_status,
    _board_summary,
    _svg,
    _select_trigger_sequence,
    parse_board_arg,
    parse_vector_response,
    _ring_sequence_consistency,
)
from tools.scpi_common.scpi_serial import scpi_response_matches_command


def test_phase_pulse_count_covers_the_full_monitor_duration() -> None:
    args = __import__("types").SimpleNamespace(
        duration_s=10.0,
        poll_interval_s=1.0,
        phase_pulse_period_ns=1_000_000,
        phase_pulse_count=4096,
        phase_coverage_min_s=2.0,
    )

    assert _effective_phase_pulse_count(args) == 12_000


def test_selftest_progress_exposes_tx_schedule_without_becoming_evidence(
        tmp_path) -> None:
    sample = BoardSample(
        ts_utc="2026-09-01T00:00:00+00:00", elapsed_s=1.0,
        board="NO1", port="COM3", tdma={}, vdc_status={},
        dpll_status={"state": 1, "update_seq": 7}, readiness={},
        vdc_vector={}, dpll_vector={}, trigger_sequence=0,
        trigger_interval_ms=None, simultaneous_feedback=False,
        phase_selftest={
            "active": 1, "role": 1, "last_error": 0,
            "scheduled_pulse_count": 12,
            "first_window_start_lo": 0x12345678,
            "first_window_start_hi": 2,
        })
    status = _progress_board(sample)
    assert len(PHASE_SELFTEST_FIELDS) == 19
    assert status["scheduled_pulse_count"] == 12
    assert status["first_window_start_ns"] == 0x212345678

    path = tmp_path / "progress.json"
    ProgressReporter(path).emit("observing", boards={"NO1": status})
    payload = __import__("json").loads(path.read_text(encoding="utf-8"))
    assert payload["source"] == "CORE0_SCPI_READ_ONLY_STATUS"
    assert payload["analysis_evidence"] == "NO5_SD_PIO0_RAW_WAVEFORM"


def test_waveform_status_and_storage_page_parsers_keep_raw_evidence() -> None:
    status = _parse_waveform_status(
        'FALSE,FALSE,TRUE,123,360,0,0,2,0,7,366,100,500,0,19,'
        '"/traces/run/sma_00000123_0001.bin"')
    assert status["complete"] == 1
    assert status["record_count"] == 360
    assert status["last_path"].endswith("_0001.bin")

    page = _parse_storage_read(
        '"OK","/traces/run/x.bin",0,4,4,4,1,99,0,"01020304"', 0)
    assert page["payload"] == b"\x01\x02\x03\x04"
    assert page["eof"] is True


def test_empty_waveform_is_a_recorded_gate_failure_without_save(
        monkeypatch, tmp_path) -> None:
    commands: list[str] = []

    def query(_ser, command: str, _timeout: float) -> str:
        commands.append(command)
        if command.endswith(":STOP"):
            return '"OK",0,0'
        if command.endswith(":STATus?"):
            return '0,0,1,123,0,0,0,0,0,0,0,10,20,0,0,""'
        raise AssertionError(f"unexpected command: {command}")

    monkeypatch.setattr(
        "tools.dpll_vdc_monitor.dpll_vdc_monitor._query", query)
    args = __import__("types").SimpleNamespace(
        timeout=1.0, waveform_flush_timeout_s=1.0,
        phase_pulse_period_ns=1_000_000,
        phase_max_span_ns=500, phase_min_complete_rounds=3,
        out_dir=tmp_path)
    result = _finish_waveform_capture(
        object(), args, ProgressReporter(tmp_path / "progress.json"))

    assert result["raw_gate"]["passed"] is False
    assert result["raw_gate"]["errors"] == ["no_raw_waveform_records"]
    assert result["sd_paths"] == []
    assert all(not command.endswith(":SAVE") for command in commands)


def test_dropped_waveform_is_saved_and_analyzed_as_failed_evidence(
        monkeypatch, tmp_path) -> None:
    commands: list[str] = []

    def query(_ser, command: str, _timeout: float) -> str:
        commands.append(command)
        if command.endswith(":STOP"):
            return '"OK",10,3'
        if command.endswith(":STATus?"):
            return (
                '0,0,1,123,10,3,7,1,0,1,10,10,20,0,1,'
                '"/traces/run/sma_00000123_0000.bin"')
        if command.endswith(":SAVE"):
            return '"OK",1,"/traces/run/sma_00000123_"'
        raise AssertionError(f"unexpected command: {command}")

    monkeypatch.setattr(
        "tools.dpll_vdc_monitor.dpll_vdc_monitor._query", query)
    monkeypatch.setattr(
        "tools.dpll_vdc_monitor.dpll_vdc_monitor._download_waveform_segment",
        lambda _ser, _path, _timeout: b"segment")
    monkeypatch.setattr(
        "tools.dpll_vdc_monitor.dpll_vdc_monitor.decode_segments",
        lambda _paths, **_kwargs: {
            "dropped_count": 0,
            "source_dropped_count": 2,
            "phase_round_count": 1,
            "phase": [{"span_ns": 42}],
        })
    monkeypatch.setattr(
        "tools.dpll_vdc_monitor.dpll_vdc_monitor.write_waveform_reports",
        lambda _decoded, out_dir: {"out_dir": str(out_dir)})
    args = __import__("types").SimpleNamespace(
        timeout=1.0, waveform_flush_timeout_s=1.0,
        phase_pulse_period_ns=1_000_000,
        phase_max_span_ns=500, phase_min_complete_rounds=3,
        out_dir=tmp_path)

    result = _finish_waveform_capture(
        object(), args, ProgressReporter(tmp_path / "progress.json"))

    assert any(command.endswith(":SAVE") for command in commands)
    assert result["sd_paths"] == [
        "/traces/run/sma_00000123_0000.bin"]
    assert result["raw_gate"]["passed"] is False
    assert result["raw_gate"]["capture_dropped_count"] == 3
    assert result["raw_gate"]["source_dropped_count"] == 2
    assert result["raw_gate"]["errors"] == [
        "capture_dropped_records",
        "source_dma_or_latch_dropped_records",
        "source_dropped_records",
        "insufficient_stable_circular_span_windows",
    ]


@pytest.mark.parametrize("command,response", [
    ("SYSTem:SYNC:VDC:OBServer:WAVEform:ARM", '"OK",123,180'),
    ("SYSTem:SYNC:VDC:OBServer:WAVEform:STOP", '"OK",360,0'),
    ("SYSTem:SYNC:VDC:OBServer:WAVEform:SAVE",
     '"OK",2,"/traces/run/sma_00000123_"'),
])
def test_waveform_composite_scpi_responses_are_preserved(
        command: str, response: str) -> None:
    assert scpi_response_matches_command(command, response)


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


def test_observer_summary_requires_external_phase_evidence() -> None:
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
        phase_observation={
            "enabled": 1, "round_count": 8, "complete_count": 8,
            "missing_count": 0, "ambiguous_count": 0,
            "last_edge_mask": SMA_LOCK_EXPECTED_MASK, "last_span_ns": 42,
            "initial_span_ns": 900, "peak_span_ns": 900,
            "stable_streak": 4, "max_stable_streak": 4,
            "stable_jitter_ns": 12, "converged": 1,
            "configured_max_span_ns": 500,
            "configured_min_stable_rounds": 3,
        },
    )
    summary = _board_summary([sample], expected_interval_ms=1.0,
                             interval_tolerance_ms=0.1, observer=True)
    assert summary["role"] == "observer"
    assert summary["simultaneous_feedback"] is False
    assert summary["vector_valid"] is False
    assert summary["sma_all_locked"] is True
    assert summary["phase_gate_passed"] is True
    assert summary["phase_initial_span_ns"] == 900
    assert summary["phase_final_span_ns"] == 42

    sample.sma_input["level_mask"] = 0x07
    unlocked = _board_summary([sample], expected_interval_ms=1.0,
                              interval_tolerance_ms=0.1, observer=True)
    assert unlocked["sma_all_locked"] is False

    sample.sma_input["level_mask"] = SMA_LOCK_EXPECTED_MASK
    sample.phase_observation["last_span_ns"] = 900
    sample.phase_observation["stable_streak"] = 0
    sample.phase_observation["converged"] = 0
    phase_bad = _board_summary([sample], expected_interval_ms=1.0,
                               interval_tolerance_ms=0.1, observer=True,
                               phase_max_span_ns=500)
    assert phase_bad["sma_all_locked"] is True
    assert phase_bad["phase_gate_passed"] is False


@pytest.mark.parametrize("field", ["missing_count", "ambiguous_count"])
def test_observer_summary_rejects_incomplete_phase_rounds(field: str) -> None:
    phase = {
        "enabled": 1, "round_count": 12, "complete_count": 10,
        "missing_count": 0, "ambiguous_count": 0,
        "last_edge_mask": SMA_LOCK_EXPECTED_MASK, "last_span_ns": 40,
        "initial_span_ns": 800, "peak_span_ns": 800,
        "stable_streak": 6, "max_stable_streak": 6,
        "stable_jitter_ns": 8, "converged": 1,
        "configured_max_span_ns": 500,
        "configured_min_stable_rounds": 3,
    }
    phase[field] = 1
    sample = BoardSample(
        ts_utc="2026-08-28T00:00:00+00:00", elapsed_s=1.0,
        board="NO5", port="COM25", tdma={}, vdc_status={}, dpll_status={},
        readiness={}, vdc_vector={}, dpll_vector={}, trigger_sequence=0,
        trigger_interval_ms=None, simultaneous_feedback=False,
        sma_input={"base_pin": 20, "pin_count": 4,
                   "level_mask": SMA_LOCK_EXPECTED_MASK},
        phase_observation=phase,
    )
    summary = _board_summary([sample], expected_interval_ms=1.0,
                             interval_tolerance_ms=0.1, observer=True)
    assert summary["phase_gate_passed"] is False


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
