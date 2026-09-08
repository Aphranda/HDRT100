from __future__ import annotations

from types import SimpleNamespace

import pytest

import tools.dpll_servo_tune.dpll_servo_tune as servo_tune
from tools.dpll_servo_tune.dpll_servo_tune import (
    Board,
    Profile,
    _gradient_candidate,
    _gradient_score_hard_safe,
    _persist_profile,
    _project_profile,
    _select_improved_trial,
    _window_score,
)


def _args() -> SimpleNamespace:
    return SimpleNamespace(
        sanity_freq_limit_ppb=10_000,
        saturation_ratio=0.8,
        saturation_penalty=10.0,
        frequency_weight=0.05,
        hard_failure_penalty=1_000_000.0,
        objective="sum",
        kp_min=4096,
        kp_max=24576,
        ki_min=64,
        ki_max=512,
        gradient_kp_probe=512,
        gradient_ki_probe=16,
        gradient_kp_step=256,
        gradient_ki_step=8,
    )


def _board_sample(*, phase: int, frequency: int = 0,
                  adjust: int = 0, locked: int = 1,
                  reject: int = 0) -> dict[str, object]:
    return {
        "vector": {"last_phase_error_ns": phase, "state": 5,
                   "dco_period_adjust_ppb": adjust,
                   "gate_reject_code": reject},
        "filter": {"phase_error_ns": phase,
                   "frequency_error_ppb": frequency,
                   "period_adjust_ppb": adjust,
                   "state": 5, "reject_count": reject},
        "readiness": {"locked": locked, "input_ready": 1,
                      "rejected_count": reject},
    }


def test_window_score_penalizes_period_adjust_saturation() -> None:
    args = _args()
    samples = [{"boards": {
        "NO1": _board_sample(phase=100, adjust=7_000),
        "NO2": _board_sample(phase=-100, adjust=9_000),
    }}]

    result = _window_score(samples, args)

    assert result["safe"] is False
    assert result["boards"][1]["saturation_excess_ppb"] == 1_000
    assert result["value"] > 10_000


def test_window_score_marks_unlock_as_hard_failure() -> None:
    args = _args()
    result = _window_score([{"boards": {
        "NO1": _board_sample(phase=10, locked=0),
    }}], args)

    assert result["safe"] is False
    assert _gradient_score_hard_safe({"score": result}) is False
    assert result["value"] >= args.hard_failure_penalty


def test_worst_board_objective_rejects_average_only_improvement() -> None:
    args = _args()
    args.objective = "worst-board"
    baseline = _window_score([{"boards": {
        "NO1": _board_sample(phase=1_000),
        "NO2": _board_sample(phase=100),
    }}], args)
    candidate = _window_score([{"boards": {
        "NO1": _board_sample(phase=1_050),
        "NO2": _board_sample(phase=0),
    }}], args)

    assert baseline["worst_board"] == "NO1"
    assert candidate["worst_board"] == "NO1"
    assert candidate["value"] > baseline["value"]
    assert sum(row["loss"] for row in candidate["boards"]) < sum(
        row["loss"] for row in baseline["boards"])


def test_probe_line_search_selects_safe_minimax_improvement() -> None:
    args = _args()
    args.objective = "worst-board"
    baseline_score = _window_score([{"boards": {
        "NO1": _board_sample(phase=1_000),
        "NO2": _board_sample(phase=100),
    }}], args)
    probe_score = _window_score([{"boards": {
        "NO1": _board_sample(phase=900),
        "NO2": _board_sample(phase=0),
    }}], args)
    gradient_score = _window_score([{"boards": {
        "NO1": _board_sample(phase=1_050),
        "NO2": _board_sample(phase=0),
    }}], args)
    current = Profile(16384, 256, 1000, 10000, 10000)
    selected = _select_improved_trial(
        {"score": baseline_score},
        [("gradient", Profile(16512, 260, 1000, 10000, 10000),
          {"score": gradient_score}),
         ("ki_plus", Profile(16384, 264, 1000, 10000, 10000),
          {"score": probe_score})],
        current)

    assert selected is not None
    assert selected[0] == "ki_plus"


def test_response_is_ok_accepts_detailed_tune_response() -> None:
    assert servo_tune.response_is_ok('"OK",12,16384,256,1000,10000,10000,1')
    assert servo_tune.response_is_ok("OK")
    assert not servo_tune.response_is_ok('"UNAVAILABLE"')


def test_apply_profile_retries_transient_unavailable(monkeypatch) -> None:
    responses = iter(['"UNAVAILABLE"', '"OK",12,16384,256,1000,10000,10000,1'])

    monkeypatch.setattr(servo_tune, "command",
                        lambda _serial, _text, _timeout: next(responses))
    monkeypatch.setattr(servo_tune.time, "sleep", lambda _seconds: None)
    events: list[dict[str, object]] = []

    servo_tune._apply_profile({"NO1": "serial"}, [Board("NO1", "COM1")],
                              Profile(16384, 256, 1000, 10000, 10000),
                              2.0, events, "apply")

    assert events[0]["attempt"] == 1
    assert events[0]["response"].startswith('"OK"')


def test_observe_window_retries_transient_parse_failure(monkeypatch, tmp_path) -> None:
    args = _args()
    args.settle_s = 0.0
    args.observation_s = 0.0
    args.sample_interval_s = 0.0
    args.scpi_read_retries = 1
    args.scpi_retry_delay_s = 0.0
    args.timeout = 2.0
    attempts = 0

    def fake_read_board(_serial: str, _timeout: float) -> dict[str, object]:
        nonlocal attempts
        attempts += 1
        if attempts == 1:
            raise ValueError("field count 1 != 8: '<timeout>'")
        return _board_sample(phase=10)

    monkeypatch.setattr(servo_tune, "read_board", fake_read_board)
    monkeypatch.setattr(servo_tune.time, "sleep", lambda _seconds: None)
    events: list[dict[str, object]] = []

    result = servo_tune._observe_window(
        {"NO1": "serial"}, [Board("NO1", "COM1")],
        Profile(16384, 256, 1000, 10000, 10000), args, tmp_path,
        "window", events)

    assert result["score"]["safe"] is True
    assert attempts == 2
    assert events[0]["label"] == "scpi_read_recovered"


def test_gradient_step_and_projection_stay_in_conservative_bounds() -> None:
    args = _args()
    profile = Profile(4096, 64, 1000, 10_000, 10_000)

    projected = _project_profile(profile, -1, 999, args)
    candidate = _gradient_candidate(profile, 100.0, 100.0, args)

    assert projected.kp_q16 == args.kp_min
    assert projected.ki_q16 == args.ki_max
    assert args.kp_min <= candidate.kp_q16 <= args.kp_max
    assert args.ki_min <= candidate.ki_q16 <= args.ki_max


def test_persist_profile_requires_applied_runtime_profile(monkeypatch) -> None:
    profile = Profile(18432, 272, 1000, 10_000, 10_000)
    boards = [Board("NO1", "COM1"), Board("NO2", "COM2")]
    calls: list[tuple[str, str]] = []

    def fake_command(serial: str, text: str, timeout: float) -> str:
        calls.append((serial, text))
        if text == servo_tune.COEFFICIENT_QUERY:
            return "18432,272,1000,10000,10000,1234,17,17,0"
        assert text == servo_tune.STORE_COMMAND
        return '"OK"'

    monkeypatch.setattr(servo_tune, "command", fake_command)
    events: list[dict[str, object]] = []

    _persist_profile({"NO1": "NO1", "NO2": "NO2"}, boards, profile,
                     2.0, events, 3)

    assert [text for _, text in calls] == [
        servo_tune.COEFFICIENT_QUERY,
        servo_tune.COEFFICIENT_QUERY,
        servo_tune.STORE_COMMAND,
        servo_tune.STORE_COMMAND,
    ]
    assert [event["board"] for event in events] == ["NO1", "NO2"]
    assert all(event["label"] == "flash_store" for event in events)


def test_persist_profile_rejects_pending_or_mismatched_profile(monkeypatch) -> None:
    profile = Profile(18432, 272, 1000, 10_000, 10_000)
    calls: list[str] = []

    def fake_command(serial: str, text: str, timeout: float) -> str:
        calls.append(text)
        if serial == "NO1":
            return "18432,272,1000,10000,10000,1234,17,17,0"
        return "18432,256,1000,10000,10000,1234,17,16,1"

    monkeypatch.setattr(servo_tune, "command", fake_command)

    with pytest.raises(RuntimeError, match="not applied"):
        _persist_profile(
            {"NO1": "NO1", "NO2": "NO2"},
            [Board("NO1", "COM1"), Board("NO2", "COM2")],
            profile, 2.0, [], 0)

    assert calls == [servo_tune.COEFFICIENT_QUERY,
                     servo_tune.COEFFICIENT_QUERY]
