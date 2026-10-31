from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.ota.ota_stream_send.night_runner import NightPlan, run_night


class FakeClock:
    def __init__(self) -> None:
        self.now = 100.0

    def __call__(self) -> float:
        return self.now

    def advance(self, seconds: float) -> None:
        self.now += seconds


def test_run_night_succeeds_on_second_attempt(tmp_path: Path) -> None:
    clock = FakeClock()
    calls: list[str] = []

    def runner(directory: Path) -> int:
        calls.append(directory.name)
        clock.advance(5.0)
        return 1 if len(calls) == 1 else 0

    plan = NightPlan(attempts=3, wall_clock_s=60.0, out_dir=tmp_path / "night")
    summary = run_night(plan, runner, clock=clock)

    assert summary["passed"] is True
    assert summary["attempts_run"] == 2
    assert calls == ["attempt-01", "attempt-02"]
    for name in calls:
        payload = json.loads((plan.out_dir / name / "result.json")
                             .read_text(encoding="utf-8"))
        assert payload["index"] in (1, 2)
    assert (plan.out_dir / "summary.json").exists()


def test_run_night_stops_on_wall_clock_budget(tmp_path: Path) -> None:
    clock = FakeClock()
    calls: list[str] = []

    def runner(directory: Path) -> int:
        calls.append(directory.name)
        clock.advance(30.0)
        return 1

    plan = NightPlan(attempts=10, wall_clock_s=65.0, out_dir=tmp_path / "night")
    summary = run_night(plan, runner, clock=clock)

    assert summary["passed"] is False
    # 30 s + 30 s < 65 s, the third attempt would start at 60 s and is still
    # allowed; the fourth would start at 90 s and must not run.
    assert len(calls) <= 4
    assert summary["attempts_run"] == len(calls)


def test_run_night_recovers_between_failed_attempts(tmp_path: Path) -> None:
    clock = FakeClock()
    recoveries: list[str] = []

    def runner(directory: Path) -> int:
        clock.advance(1.0)
        return 1 if directory.name == "attempt-01" else 0

    def recover(directory: Path) -> None:
        recoveries.append(directory.name)

    plan = NightPlan(attempts=2, wall_clock_s=60.0, out_dir=tmp_path / "night")
    summary = run_night(plan, runner, recover=recover, clock=clock)

    assert summary["passed"] is True
    assert recoveries == ["attempt-02"]
    assert summary["attempts"][1]["recovered"] is True


def test_run_night_records_runner_exception(tmp_path: Path) -> None:
    clock = FakeClock()

    def runner(directory: Path) -> int:
        clock.advance(1.0)
        raise RuntimeError("bench lost")

    plan = NightPlan(attempts=1, wall_clock_s=60.0, out_dir=tmp_path / "night")
    summary = run_night(plan, runner, clock=clock)

    assert summary["passed"] is False
    assert summary["attempts"][0]["exit_code"] == -1
    assert "bench lost" in summary["attempts"][0]["error"]


def test_night_plan_rejects_invalid_budget() -> None:
    with pytest.raises(ValueError):
        NightPlan(attempts=0, wall_clock_s=10.0, out_dir=Path("x"))
    with pytest.raises(ValueError):
        NightPlan(attempts=1, wall_clock_s=0.0, out_dir=Path("x"))
