"""P0T must distinguish real START acknowledgment from an unknown outcome."""
from argparse import Namespace
from collections import defaultdict, deque
import json

import pytest

from tools.calibration_ring_validate import calibration_ring_topology as topology
from tools.tdma_ring_monitor.tdma_field_parse import RUNTIME_FIELDS
from tools.tdma_ring_monitor.tdma_start_ring import Board


DRIVER = Board("P", "A", "", "BUILD")
RECEIVER = Board("Q", "B", "", "BUILD")


def options(tmp_path):
    return Namespace(board_id=["A", "B"], anchor_id="A", expected_build="BUILD",
        level=7, cycles=512, train_chunk_cycles=0, pair_wait=.02,
        min_rx_frames=10, min_rx_words=8, timeout=.03, action_timeout=.01,
        settle=0., gap=0., arm_wait=.1, out_dir=tmp_path, verbose=False,
        no_assign=True, reboot_verify_no=False, adjacency_only=True,
        probe_phase_cycles=10, short_open=False, keep_open=True)


class Bench:
    def __init__(self, outcomes=None):
        self.calls = []
        self.states = {}
        self.errors = defaultdict(deque)
        self.outcomes = {key: deque(value) for key, value in (outcomes or {}).items()}
        self.snapshots = 0
        self.hook = None
        self.trains = []
        for board, slot in ((DRIVER, 0), (RECEIVER, 1)):
            state = dict.fromkeys(RUNTIME_FIELDS, 0)
            state.update(ring_node_count=2, ring_local_slot_id=slot,
                ring_config_seq=5, ring_applied_config_seq=5)
            self.states[board.address] = state

    def command(self, board, command, args):
        self.calls.append((board.address, command))
        if self.hook:
            response = self.hook(self, board, command)
            if response is not None:
                return response
        state = self.states[board.address]
        if command == "SYSTem:ERRor?":
            return self.errors[board.address].popleft() if self.errors[board.address] else '0,"No error"'
        if command == "SYSTem:TDMA:RING:STOP":
            state.update(ring_enabled=0, ring_adapter_started=0)
            state["ring_config_seq"] += 1
            state["ring_applied_config_seq"] = state["ring_config_seq"]
            return "OK"
        if command.startswith("SYSTem:TDMA:RING:TOPology "):
            values = tuple(map(int, command.split()[1].split(",")))
            for key, value in zip(("ring_node_count", "ring_local_slot_id", "ring_reference_slot_id"), values):
                state[key] = value
            return command.split()[1]
        if command == "SYSTem:TDMA:RING:ARM":
            state.update(ring_enabled=1, ring_adapter_started=1)
            state["ring_config_seq"] += 1
            state["ring_applied_config_seq"] = state["ring_config_seq"]
            return "OK"
        if command == "SYSTem:TDMA:RING:ARM:STATus?":
            return "1"
        if command == "SYSTem:TDMA:RING:STATus?":
            return ",".join(str(state[key]) for key in RUNTIME_FIELDS)
        if command == "SYSTem:TDMA:RING:START":
            outcomes = self.outcomes.get(board.address)
            outcome = outcomes.popleft() if outcomes else "OK"
            if outcome == "refused":
                self.errors[board.address].append('-200,"Execution error"')
                return "OK(no payload; verified by state readback)"
            if isinstance(outcome, Exception):
                raise outcome
            return outcome
        if command.startswith("SYSTem:TDMA:OPMode"):
            return "7,10000000,1000000,4096,0,123"
        if command == "CALibration:TOPology:PROBe 0":
            return "0,0"
        if command.startswith("CALibration:"):
            return "OK"
        raise AssertionError(command)

    def snapshot(self, board, timeout):
        self.snapshots += 1
        self.calls.append((board.address, "SNAPSHOT"))
        # Counters may rise even when START was refused: activity alone must
        # never turn an unknown command acknowledgment into an accepted pair.
        count = self.snapshots * 100
        state = dict(self.states[board.address])
        state.update(ring_adapter_rx_count=count, ring_adapter_tx_count=count)
        return {"tdma": state, "phys": dict(rx_dma_produced_words=count,
            rx_edge_count=count, rx_magic_fail_count=0)}

    def install(self, monkeypatch):
        monkeypatch.setattr(topology, "board_command", self.command)
        monkeypatch.setattr(topology, "snapshot", self.snapshot)
        monkeypatch.setattr(topology, "wait_started", lambda board, args: dict(self.states[board.address]))
        monkeypatch.setattr(topology, "close_persistent_connections", lambda: None)
        monkeypatch.setattr(topology, "train", lambda board, args: self.trains.append(board.address) or {"complete": True})


def test_main_rejected_start_is_not_a_successful_adjacency(monkeypatch, tmp_path):
    bench = Bench({"A": ["refused"] * 8, "B": ["refused"] * 8})
    bench.install(monkeypatch)
    monkeypatch.setattr(topology, "parse_args", lambda: options(tmp_path))
    monkeypatch.setattr(topology, "discover", lambda _: {"A": DRIVER, "B": RECEIVER})
    assert topology.main() == 1
    report = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    assert not report["passed"] and report["error"]
    assert report["pair_actions"]
    assert len(report["cleanup"]) == 2


def invoke(monkeypatch, tmp_path, bench, *, adjacency_only=True):
    bench.install(monkeypatch)
    args = options(tmp_path)
    args.adjacency_only = adjacency_only
    actions, recoveries = [], []
    before = topology.start_pair(DRIVER, RECEIVER, args, actions, recoveries)
    return before, actions, recoveries


@pytest.mark.parametrize("board", ["A", "B"])
@pytest.mark.parametrize("failure", ["refused", "<timeout>",
    "OK(no payload; verified by state readback)", "1", OSError("lost reply")])
def test_unknown_start_requires_stopped_rearm_and_new_baseline(monkeypatch, tmp_path, board, failure):
    bench = Bench({board: [failure, "OK"]})
    before, actions, recoveries = invoke(monkeypatch, tmp_path, bench)
    assert len(actions) == 2 and not actions[0]["passed"] and actions[1]["passed"]
    assert recoveries[0]["action"] == "STOP_CONFIRMED_REARM_PAIR"
    assert before == actions[1]["before"] != actions[0]["before"]
    assert before["tdma"]["ring_config_seq"] != actions[0]["before"]["tdma"]["ring_config_seq"]
    assert all(row["passed"] for row in actions[0]["recovery_stop"])
    for uid in ("A", "B"):
        calls = [cmd for address, cmd in bench.calls if address == uid]
        arm_positions = [i for i, cmd in enumerate(calls) if cmd == "SYSTem:TDMA:RING:ARM"]
        assert len(arm_positions) == 2
        between = calls[arm_positions[0]:arm_positions[1]]
        assert "SYSTem:TDMA:RING:STOP" in between
        assert "SYSTem:TDMA:RING:STATus?" in between[between.index("SYSTem:TDMA:RING:STOP") + 1:]


def test_both_real_ack_first_attempt_and_quoted_ok(monkeypatch, tmp_path):
    bench = Bench({"A": ['"OK"']})
    _, actions, recoveries = invoke(monkeypatch, tmp_path, bench)
    assert len(actions) == 1 and actions[0]["passed"] and not recoveries
    assert all(row["passed"] for row in actions[0]["start"])
    assert not any(cmd == "SYSTem:TDMA:RING:STOP" for _, cmd in bench.calls)


def test_training_repeats_only_after_stop_rearm(monkeypatch, tmp_path):
    bench = Bench({"A": ["refused", "OK"]})
    _, actions, _ = invoke(monkeypatch, tmp_path, bench, adjacency_only=False)
    assert bench.trains == ["B", "A", "B", "A"]
    assert all(len(row["training"]) == 2 for row in actions)


def test_permanent_failure_has_two_lifetimes_and_retained_negative_reply(monkeypatch, tmp_path):
    bench = Bench({"A": ["refused"] * 3})
    bench.install(monkeypatch)
    actions = []
    with pytest.raises(RuntimeError, match="two lifetimes"):
        topology.start_pair(DRIVER, RECEIVER, options(tmp_path), actions, [])
    assert len(actions) == 2 and all(not row["passed"] for row in actions)
    assert all(row["start"][-1]["error_after"].startswith("-200,") for row in actions)
    assert all(all(stop["passed"] for stop in row["recovery_stop"]) for row in actions)
    assert len([1 for uid, cmd in bench.calls if uid == "A" and cmd.endswith(":START")]) == 2


@pytest.mark.parametrize("stop_response", ["<timeout>", "OK(no payload; verified by state readback)", "1", None])
def test_unknown_stop_ack_blocks_rearm_even_when_physically_stopped(monkeypatch, tmp_path, stop_response):
    bench = Bench({"B": ["refused"]})
    def hook(b, board, command):
        if command == "SYSTem:TDMA:RING:STOP" and board.address == "B":
            b.states["B"].update(ring_enabled=0, ring_adapter_started=0)
            if stop_response is None:
                raise OSError("STOP disconnected")
            return stop_response
    bench.hook = hook
    bench.install(monkeypatch)
    actions = []
    with pytest.raises(RuntimeError, match="STOP unconfirmed"):
        topology.start_pair(DRIVER, RECEIVER, options(tmp_path), actions, [])
    assert len(actions) == 1
    assert not actions[0]["recovery_stop"][0]["passed"]
    assert actions[0]["recovery_stop"][0]["stopped"]["ring_enabled"] == 0
    assert actions[0]["recovery_stop"][1]["passed"]  # Other STOP still attempted.


@pytest.mark.parametrize("drift", ["enabled", "adapter", "config"])
def test_stop_ack_without_state_barrier_blocks_rearm(monkeypatch, tmp_path, drift):
    bench = Bench({"B": ["refused"]})
    def hook(b, board, command):
        if command == "SYSTem:TDMA:RING:STOP" and board.address == "B":
            b.states["B"].update(ring_enabled=0, ring_adapter_started=0)
            key = {"enabled": "ring_enabled", "adapter": "ring_adapter_started",
                   "config": "ring_applied_config_seq"}[drift]
            b.states["B"][key] += 1
            return "OK"
    bench.hook = hook
    bench.install(monkeypatch)
    actions = []
    with pytest.raises(RuntimeError, match="STOP unconfirmed"):
        topology.start_pair(DRIVER, RECEIVER, options(tmp_path), actions, [])
    assert len(actions) == 1


@pytest.mark.parametrize("reply", ["<timeout>", "OK(no payload; verified by state readback)", "1"])
def test_arm_unknown_ack_cannot_borrow_old_status_one(monkeypatch, tmp_path, reply):
    bench = Bench()
    bench.hook = lambda b, board, cmd: reply if cmd == "SYSTem:TDMA:RING:ARM" else None
    bench.install(monkeypatch)
    with pytest.raises(RuntimeError, match="ARM acknowledgment unknown"):
        topology.start_pair(DRIVER, RECEIVER, options(tmp_path), [], [])
    assert not any(cmd.endswith(":START") for _, cmd in bench.calls)


@pytest.mark.parametrize("error", ['-200,"Execution error"', 'bad', "<timeout>"])
def test_ack_plus_error_is_failed_attempt(monkeypatch, tmp_path, error):
    bench = Bench()
    calls = []
    def hook(b, board, command):
        if command == "SYSTem:TDMA:RING:START" and not calls:
            calls.append(True)
            b.errors[board.address].append(error)
    bench.hook = hook
    _, actions, _ = invoke(monkeypatch, tmp_path, bench)
    assert len(actions) == 2 and not actions[0]["passed"]
    assert actions[0]["start"][0]["response"] == "OK"
    assert actions[0]["start"][0]["error_after"] == error


def test_stale_error_is_drained_before_start(monkeypatch, tmp_path):
    bench = Bench()
    bench.errors["B"].append('-200,"old error"')
    _, actions, _ = invoke(monkeypatch, tmp_path, bench)
    assert len(actions) == 1
    assert actions[0]["start"][0]["errors_before"] == ['-200,"old error"', '0,"No error"']


@pytest.mark.parametrize("errors", [["<timeout>"], ['-200,"old error"'] * 16])
def test_no_clean_error_baseline_prevents_start(monkeypatch, tmp_path, errors):
    bench = Bench()
    bench.errors["B"].extend(errors)
    bench.install(monkeypatch)
    with pytest.raises(RuntimeError):
        topology.start_pair(DRIVER, RECEIVER, options(tmp_path), [], [])
    assert not any(cmd.endswith(":START") for _, cmd in bench.calls)


@pytest.mark.parametrize("key", ["ring_config_seq", "ring_node_count", "ring_local_slot_id",
    "ring_reference_slot_id", "ring_enabled", "ring_adapter_started"])
def test_main_activity_from_new_lifetime_never_forms_edge(monkeypatch, tmp_path, key):
    bench = Bench()
    bench.install(monkeypatch)
    def changed(board, timeout):
        result = bench.snapshot(board, timeout)
        if bench.snapshots == 2:
            result["tdma"][key] += 1
        return result
    monkeypatch.setattr(topology, "snapshot", changed)
    monkeypatch.setattr(topology, "parse_args", lambda: options(tmp_path))
    monkeypatch.setattr(topology, "discover", lambda _: {"A": DRIVER, "B": RECEIVER})
    assert topology.main() == 1
    report = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    assert "ARM lifetime" in report["error"] and not report["passed"]
    assert report["adjacency"] == {"A": [], "B": []}
    assert all(row["passed"] for row in report["cleanup"])


@pytest.mark.parametrize("failure", ["profile", "cleanup_stop", "cleanup_probe", "topology"])
def test_main_failure_preserves_summary_and_cleans_every_board(monkeypatch, tmp_path, failure):
    bench = Bench()
    def hook(b, board, command):
        if board.address != "A":
            return None
        if failure == "profile" and command.startswith("SYSTem:TDMA:OPMode:STAGe"):
            raise OSError("profile failure")
        if failure == "topology" and command.startswith("SYSTem:TDMA:RING:TOPology"):
            return "2,1,0"  # Wrong local slot for the first driver.
        if failure == "cleanup_probe" and command == "CALibration:TOPology:PROBe 0":
            raise OSError("probe failure")
        if failure == "cleanup_stop" and b.snapshots >= 4 and command.endswith(":STOP"):
            raise OSError("cleanup STOP failure")
    bench.hook = hook
    bench.install(monkeypatch)
    monkeypatch.setattr(topology, "parse_args", lambda: options(tmp_path))
    monkeypatch.setattr(topology, "discover", lambda _: {"A": DRIVER, "B": RECEIVER})
    assert topology.main() == 1
    report = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    assert not report["passed"]
    assert {row["board"] for row in report["cleanup"]} == {"A", "B"}
    assert all("probe_response" in row or "probe_error" in row for row in report["cleanup"])


def test_successful_main_retains_ack_actions_and_exact_probe_readback(monkeypatch, tmp_path):
    bench = Bench()
    bench.install(monkeypatch)
    monkeypatch.setattr(topology, "parse_args", lambda: options(tmp_path))
    monkeypatch.setattr(topology, "discover", lambda _: {"A": DRIVER, "B": RECEIVER})
    assert topology.main() == 0
    report = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    assert report["passed"] and len(report["pair_actions"]) == 2
    assert all(row["passed"] for row in report["pair_actions"])
    assert all(row["probe_response"] == "0,0" for row in report["cleanup"])
