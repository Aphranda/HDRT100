from pathlib import Path
import sys
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.t2_deploy_action_probe.t2_deploy_action_probe import (
    ActionBook, ActionError, ActionState, Completion, Fence, Intent, Ready,
    SyncReadyStamp,
    default_slots, run_positive_demo,
)


def intent(generation=1, reservation=1, target="GATEWAY"):
    return Intent(reservation, generation, 99, 20260923, 2, 1800000000,
                  1800000000000, target, "VNA_TRIG", 1)


def prepared_book():
    b = ActionBook(default_slots(), 1)
    b.create(intent())
    for slot in ("SWITCH_A", "SWITCH_B", "GATEWAY"):
        b.accept_ready(Ready(1, 1, slot, True, 500000000))
    for index, slot in enumerate(("SWITCH_A", "SWITCH_B", "GATEWAY"), 1):
        b.bind_sync_stamp(SyncReadyStamp(1, 1, slot, 100 + index,
                                         1800000000000 + index * 100,
                                         1799999999000, 1800000001000, index))
    return b


def test_positive_intent_ready_fence_execute_completion():
    r = run_positive_demo()
    assert r.state is ActionState.COMPLETED
    assert r.completion.actual_event_valid


@pytest.mark.parametrize("fn,reason", [
    (lambda b: b.create(intent(generation=2)), "DEPLOYMENT_GENERATION"),
    (lambda b: b.accept_ready(Ready(1, 1, "NOPE", True, 1)), "READY_TARGET"),
    (lambda b: b.issue_fence(1, {"SWITCH_A", "SWITCH_B", "GATEWAY"}, 1), "FENCE_STATE"),
])
def test_rejects_before_mutating_to_success(fn, reason):
    b = ActionBook(default_slots(), 1)
    if reason != "DEPLOYMENT_GENERATION": b.create(intent())
    with pytest.raises(ActionError, match=reason): fn(b)


def test_missing_ready_and_stale_fence_are_rejected():
    b = ActionBook(default_slots(), 1)
    b.create(intent())
    b.accept_ready(Ready(1, 1, "GATEWAY", True, 500000000))
    with pytest.raises(ActionError, match="FENCE_MISSING_READY"):
        b.issue_fence(1, {"SWITCH_A", "SWITCH_B", "GATEWAY"}, 1)
    b = prepared_book()
    fence = b.issue_fence(1, {"SWITCH_A", "SWITCH_B", "GATEWAY"}, 1)
    with pytest.raises(ActionError, match="FENCE_STALE"):
        b.execute(1, Fence(1, 1, 2, fence.required_slots, fence.ready_slots), 1)


def test_missing_sync_stamp_and_overlap_are_rejected():
    b = ActionBook(default_slots(), 1)
    b.create(intent())
    for slot in ("SWITCH_A", "SWITCH_B", "GATEWAY"):
        b.accept_ready(Ready(1, 1, slot, True, 500000000))
    with pytest.raises(ActionError, match="FENCE_MISSING_SYNC_STAMP"):
        b.issue_fence(1, {"SWITCH_A", "SWITCH_B", "GATEWAY"}, 1)
    for index, slot in enumerate(("SWITCH_A", "SWITCH_B", "GATEWAY"), 1):
        b.bind_sync_stamp(SyncReadyStamp(1, 1, slot, 100 + index,
                                         1800000000000 + index * 100,
                                         1799999999000, 1800000001000, index))
    b.issue_fence(1, {"SWITCH_A", "SWITCH_B", "GATEWAY"}, 1)
    other = b.create(intent(reservation=2))
    b.accept_ready(Ready(2, 1, "GATEWAY", True, 600000000))
    b.bind_sync_stamp(SyncReadyStamp(2, 1, "GATEWAY", 200, 1800000000050,
                                     1800000000000, 1800000002000, 2))
    with pytest.raises(ActionError, match="SYNC_WINDOW_OVERLAP"):
        b.issue_fence(2, {"GATEWAY"}, 2)


def test_late_and_invalid_completion_preserve_rejection():
    b = prepared_book()
    fence = b.issue_fence(1, {"SWITCH_A", "SWITCH_B", "GATEWAY"}, 1)
    with pytest.raises(ActionError, match="DEADLINE_LATE"):
        b.execute(1, fence, 500000001)
    b = prepared_book()
    fence = b.issue_fence(1, {"SWITCH_A", "SWITCH_B", "GATEWAY"}, 1)
    b.execute(1, fence, 499000000)
    with pytest.raises(ActionError, match="NO_LATCH"):
        b.complete(Completion(1, 1, "GATEWAY", True, False, None, "NO_LATCH"))


def test_old_generation_is_retired_and_cannot_complete():
    b = prepared_book()
    b.retire_generation(1)
    assert b.records[1].state is ActionState.REJECTED
    assert "GENERATION_RETIRED" in b.records[1].rejections
