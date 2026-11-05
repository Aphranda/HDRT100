"""Design model for the guide-campaign time budget (M7 feasibility).

NOT firmware and NOT measured data. It encodes the table from
``docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`` section 9.10 so
that changing a table size or a per-trial assumption fails a test instead of
silently blowing the overnight budget.

Assumptions are estimates to be replaced by measured T_trial:
- 80 B per maintenance frame (8 chunks x 10 B) and 1.2 s per frame (10K);
- one ring restart per segment boundary, 5 s each;
- per-trial 3 s (arm + inject + capture + poll + save).
"""

from __future__ import annotations

from dataclasses import dataclass

FRAME_PAYLOAD_BYTES = 80
FRAME_S = 1.2
RESTART_S = 5.0
MAINTENANCE_ACK_S = 2.4
CAMPAIGN_BUDGET_S = 900


@dataclass(frozen=True)
class Segment:
    name: str
    trials: int
    trial_s: float = 3.0


def plan_delivery_s(payload_bytes: int) -> float:
    frames = max(1, -(-payload_bytes // FRAME_PAYLOAD_BYTES))
    return frames * FRAME_S


def campaign(
    segments: list[Segment],
    plan_bytes: int = 304,
    plan_count: int = 5,
) -> dict[str, float]:
    trials = sum(segment.trials * segment.trial_s for segment in segments)
    plans = plan_count * plan_delivery_s(plan_bytes)
    restarts = len(segments) * RESTART_S
    total = trials + plans + restarts
    return {
        "trials_s": trials,
        "plans_s": plans,
        "restarts_s": restarts,
        "total_s": total,
    }


def baseline_segments() -> list[Segment]:
    return [
        Segment("M1", 12, 4.0),
        Segment("M2", 4 * 3),
        Segment("M3", 4 * 3),
        Segment("M4", 4 * 6 * 3),
        Segment("M5", 1, 30.0),
    ]


def test_baseline_campaign_fits_the_overnight_budget() -> None:
    result = campaign(baseline_segments())
    assert result["total_s"] <= CAMPAIGN_BUDGET_S
    assert result["total_s"] == 415.0


def test_plan_delivery_covers_the_304_byte_plan() -> None:
    assert plan_delivery_s(304) == 4.8
    assert plan_delivery_s(4) == 1.2


def test_ack_budget_covers_one_frame_with_retry_headroom() -> None:
    assert MAINTENANCE_ACK_S >= 2 * FRAME_S


def test_large_ladder_is_flagged_by_the_budget() -> None:
    segments = baseline_segments()
    segments[3] = Segment("M4", 4 * 6 * 10)
    result = campaign(segments)
    assert result["total_s"] > CAMPAIGN_BUDGET_S


def test_ten_overnight_runs_fit_in_eight_hours() -> None:
    result = campaign(baseline_segments())
    assert result["total_s"] * 10 <= 8 * 3600
