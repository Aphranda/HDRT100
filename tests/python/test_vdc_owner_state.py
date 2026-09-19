from __future__ import annotations

import pytest

from tools.scpi_common import vdc_owner_state as owner


@pytest.mark.parametrize("raw", ['"QUEUED",7', '"QUEUED",1,7'])
def test_queued_sequence(raw):
    assert owner.queued_sequence(raw) == 7


@pytest.mark.parametrize("raw", ['"OK"', '"QUEUED",0', '"QUEUED",-1',
                                 '"QUEUED",4294967296', '"QUEUED",bad'])
def test_invalid_ack_cannot_authorize_arm(raw):
    with pytest.raises(RuntimeError):
        owner.queued_sequence(raw)


def test_waits_for_core1_apply_and_core0_active_publication(monkeypatch):
    replies = iter(["7,6,4,1,1", "7,7,4,2,1", "7,7,4,2,0"])
    monkeypatch.setattr(owner.time, "sleep", lambda _: None)
    evidence = []
    result = owner.wait_owner_applied(lambda _: next(replies), 7, 1, evidence)
    assert result == {"sequence": 7, "opcode": 4, "applied": True}
    assert len(evidence) == 3


@pytest.mark.parametrize("raw,reason", [("7,7,4,3,1", "rejected"),
                                        ("8,8,4,2,0", "changed"),
                                        ("7,7,4,2", "invalid"),
                                        ("7,7,4,2,2", "invalid")])
def test_rejection_identity_change_and_bad_status_fail_closed(raw, reason):
    evidence = []
    with pytest.raises(RuntimeError, match=reason):
        owner.wait_owner_applied(lambda _: raw, 7, 1, evidence)
    assert evidence[0]["response"] == raw


def test_timeout_preserves_pending_evidence(monkeypatch):
    ticks = iter([0, 0, 2])
    monkeypatch.setattr(owner.time, "monotonic", lambda: next(ticks))
    evidence = []
    with pytest.raises(TimeoutError):
        owner.wait_owner_applied(lambda _: "7,6,4,1,1", 7, 1, evidence)
    assert evidence[0]["response"] == "7,6,4,1,1"
