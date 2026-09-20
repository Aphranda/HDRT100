"""Regression checks for the tracked VDC joint observer snapshot layer."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import observer as tracked_observer


UIDS = ("uid-0", "uid-1", "uid-2", "uid-3")


def fake_match(events):
    class MatchProbe:
        def __init__(self, options):
            self.opt = options
            self.boards = [SimpleNamespace(address=uid) for uid in UIDS]
            self.report = {"boards": {uid: {} for uid in UIDS}}
            self.barrier = True
            self.block_queries = False

        def command(self, board, command):
            events.append(("command", board.address, command))
            return f"snapshot:{board.address}"

        def save(self):
            events.append(("save",))

        def collect(self):
            events.append(("parent_collect",))

    parent = SimpleNamespace()
    return SimpleNamespace(MatchProbe=MatchProbe, parent=parent)


def test_observer_collect_snapshots_all_boards_before_parent(tmp_path):
    events = []
    module = tracked_observer.load_observer(
        Path("unused.py"), runtime_module=fake_match(events))
    probe = module.ObserverProbe(SimpleNamespace(out=tmp_path))
    probe.collect()
    commands = [event for event in events if event[0] == "command"]
    assert commands == [
        ("command", uid, tracked_observer.SNAPSHOT) for uid in UIDS]
    assert events.index(("parent_collect",)) > max(
        index for index, event in enumerate(events) if event[0] == "save")
    assert [probe.report["boards"][uid]["event_snapshot_raw"] for uid in UIDS] == [
        f"snapshot:{uid}" for uid in UIDS]


@pytest.mark.parametrize("barrier,blocked", [(False, False), (True, True)])
def test_observer_collect_requires_stop_barrier(tmp_path, barrier, blocked):
    events = []
    module = tracked_observer.load_observer(
        Path("unused.py"), runtime_module=fake_match(events))
    probe = module.ObserverProbe(SimpleNamespace(out=tmp_path))
    probe.barrier = barrier
    probe.block_queries = blocked
    with pytest.raises(RuntimeError, match="all-board STOP"):
        probe.collect()
    assert events == []


def test_observer_factory_rejects_incomplete_match_module():
    with pytest.raises(ValueError, match="MatchProbe/parent"):
        tracked_observer.load_observer(
            Path("unused.py"), runtime_module=SimpleNamespace())
