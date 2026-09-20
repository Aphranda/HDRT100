"""Device-free checks for the tracked joint runtime control/START base."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import runtime_base


UIDS = ("u1", "u2", "u3", "u4")


def fake_capture(events, *, start_result=None):
    boards = [SimpleNamespace(address=uid, port=f"COM{index}")
              for index, uid in enumerate(UIDS, 3)]

    class Connection:
        def __init__(self, board):
            self.board = board
            self.session = SimpleNamespace(ser=object())

        def command(self, command):
            events.append(("connection", self.board.address, command))
            value = (start_result or {}).get(self.board.address, "OK")
            if isinstance(value, Exception):
                raise value
            return value

    backend = SimpleNamespace(
        _PERSISTENT_CONNECTIONS={board.address: Connection(board) for board in boards})
    parent = SimpleNamespace(
        SYNC="SYNC", MODEL="MODEL?", fields=lambda raw: raw.split(","),
        base=SimpleNamespace(BACKEND=backend))

    class FollowProbe:
        @staticmethod
        def control(probe, board, text, expected="OK"):
            events.append(("follow_control", board.address, text, expected))
            return str(expected)

    follow = SimpleNamespace(parent=parent, FollowProbe=FollowProbe, STATUS="FOLLOW?")
    phase = SimpleNamespace(follow=follow, PHASE="PHASE")

    class RunProbe:
        def __init__(self, opt):
            self.opt = opt
            self.boards = boards
            self.deadline = 1e12
            self.block_queries = True
            self.barrier = False
            self.scope = SimpleNamespace(quiet=True)
            self.report = {
                "actions": [], "boards": {uid: {} for uid in UIDS},
                "plan": {}, "errors": [], "tool_sources": [],
            }
            self.saved = 0

        def command(self, board, command, unused=None):
            events.append(("probe_command", board.address, command))
            if command == "PHASE?":
                return "1"
            if command.startswith("RUN "):
                return str(100 + UIDS.index(board.address))
            return "OK"

        def save(self):
            self.saved += 1

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": repr(exc)})

    capture = SimpleNamespace(
        RunProbe=RunProbe, phase=phase, PREFIX="RUN")
    return capture, boards, backend


def options(tmp_path):
    return SimpleNamespace(out=tmp_path, seconds=60)


def test_runtime_control_prepares_phase_and_run_for_every_board(tmp_path):
    events = []
    capture, boards, _ = fake_capture(events)
    runtime = runtime_base.load_runtime(
        Path("unused.py"), UIDS, runtime_module=capture)
    probe = runtime.Probe(options(tmp_path))
    probe.control(boards[0], "SYNC 0", 0)
    probe.control(boards[0], "SYNC 7", 7)
    assert ("follow_control", UIDS[0], "PHASE 0", 0) in events
    assert [(event[1], event[2]) for event in events if event[0] == "probe_command"] == [
        (UIDS[0], "RUN 1000000,2000,0"),
        (UIDS[1], "PHASE?"), (UIDS[1], "RUN 1000000,2000,0"),
        (UIDS[2], "PHASE?"), (UIDS[2], "RUN 1000000,2000,0"),
        (UIDS[3], "PHASE?"), (UIDS[3], "RUN 1000000,2000,0"),
    ]
    assert [probe.report["boards"][uid]["run_request"] for uid in UIDS] == [
        100, 101, 102, 103]
    assert probe.report["plan"]["runtime_board_queries"] is False
    assert probe.report["monitor_script_sha256"] == runtime.source_sha256


def test_parallel_start_uses_distinct_preverified_connections_and_joins(tmp_path):
    events = []
    capture, _, _ = fake_capture(events)
    runtime = runtime_base.load_runtime(
        Path("unused.py"), UIDS, runtime_module=capture)
    probe = runtime.Probe(options(tmp_path))
    rows = runtime.startup.parallel_start(probe)
    assert len(rows) == 4 and all(row["raw"] == "OK" for row in rows)
    assert [row["uid"] for row in probe.report["parallel_start_intents"]] == list(UIDS)
    assert len([event for event in events if event[0] == "connection"]) == 4
    assert probe.report["parallel_start_joined_ns"] > 0
    journal = (tmp_path / "actions.jsonl").read_text(encoding="utf-8").splitlines()
    assert len(journal) == 4


def test_parallel_start_failure_is_reported_only_after_all_workers_join(tmp_path):
    events = []
    capture, _, _ = fake_capture(events, start_result={UIDS[1]: "<timeout>"})
    runtime = runtime_base.load_runtime(
        Path("unused.py"), UIDS, runtime_module=capture)
    probe = runtime.Probe(options(tmp_path))
    with pytest.raises(RuntimeError, match="all workers joined"):
        runtime.startup.parallel_start(probe)
    assert len([event for event in events if event[0] == "connection"]) == 4
    assert len(probe.report["actions"]) == 4


@pytest.mark.parametrize("fault", ["uid", "port", "transport", "missing", "deadline", "barrier"])
def test_parallel_start_rejects_precondition_faults(tmp_path, fault):
    events = []
    capture, boards, backend = fake_capture(events)
    runtime = runtime_base.load_runtime(
        Path("unused.py"), UIDS, runtime_module=capture)
    probe = runtime.Probe(options(tmp_path))
    if fault == "uid":
        boards[3].address = "other"
    elif fault == "port":
        boards[3].port = boards[2].port
    elif fault == "transport":
        backend._PERSISTENT_CONNECTIONS[UIDS[3]].session.ser = (
            backend._PERSISTENT_CONNECTIONS[UIDS[2]].session.ser)
    elif fault == "missing":
        backend._PERSISTENT_CONNECTIONS[UIDS[2]].session = None
    elif fault == "deadline":
        probe.deadline = 0
    elif fault == "barrier":
        probe.barrier = True
    with pytest.raises((ValueError, RuntimeError, TimeoutError)):
        runtime.startup.parallel_start(probe)
    assert not [event for event in events if event[0] == "connection"]
