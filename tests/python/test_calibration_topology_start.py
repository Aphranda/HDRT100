"""P0T must distinguish real START acknowledgment from an unknown outcome."""
from argparse import Namespace
from collections import defaultdict, deque
import json

import pytest

from tools.calibration_ring_validate import calibration_ring_topology as topology
from tools.tdma_ring_monitor.tdma_field_parse import FIELDS as TDMA_FIELDS, PHYS_FIELDS, RUNTIME_FIELDS
from tools.tdma_ring_monitor.tdma_start_ring import Board


DRIVER = Board("P", "A", "", "BUILD")
RECEIVER = Board("Q", "B", "", "BUILD")


def snapshot_with_raw(board, tdma, phys):
    return {"address": board.address, "port": board.port, "build": board.build,
            "tdma": tdma, "phys": phys,
            "raw": {plane: ",".join(str(values[name]) for name in fields)
                    for plane, fields, values in (("tdma", TDMA_FIELDS, tdma),
                                                   ("phys", PHYS_FIELDS, phys))}}


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
        self.profile_applies = defaultdict(int)
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
            state["ring_config_seq"] += 1
            state["ring_applied_config_seq"] = state["ring_config_seq"]
            return command.split()[1]
        if command == "SYSTem:VDC:FEEDback:SESSion?":
            return "0"
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
        if command == "SYSTem:TDMA:OPMode?":
            profile = "7,10000000,1000000,4096,0,123"
            return f"{profile},{profile},1,{self.profile_applies[board.address]},0,0"
        if command == "SYSTem:TDMA:OPMode:APPLy":
            self.profile_applies[board.address] += 1
            return "7,10000000,1000000,4096,0,123"
        if command.startswith("SYSTem:TDMA:OPMode"):
            return "7,10000000,1000000,4096,0,123"
        if command.startswith("CALibration:TOPology:PROBe 1,"):
            return command.split()[1]
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
        state = dict.fromkeys(TDMA_FIELDS, 0)
        state.update(self.states[board.address])
        state.update(ring_adapter_rx_count=count, ring_adapter_tx_count=count)
        phys = dict.fromkeys(PHYS_FIELDS, 0)
        phys.update(rx_dma_produced_words=count, rx_edge_count=count)
        return snapshot_with_raw(board, state, phys)

    def install(self, monkeypatch):
        monkeypatch.setattr(topology, "board_command", self.command)
        monkeypatch.setattr(topology.stopped_profile, "board_command", self.command)
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


def test_pair_topology_recovers_explicit_refusal_with_new_stopped_generation(monkeypatch, tmp_path):
    bench = Bench()
    attempts = []
    def hook(b, board, command):
        if command.startswith("SYSTem:TDMA:RING:TOPology "):
            attempts.append(command)
            if len(attempts) == 1:
                b.errors[board.address].append('-200,"Execution error"')
                return "<timeout>"
    bench.hook = hook
    bench.install(monkeypatch)
    actions = []
    result = topology.configure_pair_topology(DRIVER, 0, options(tmp_path), actions)
    assert result["passed"] and result["session_before"] == "0"
    assert attempts == ["SYSTem:TDMA:RING:TOPology 2,0,0"] * 2
    failed = [r for r in result["actions"] if r.get("error_after")]
    assert failed[0]["response"] == "<timeout>"
    assert failed[0]["error_after"] == '-200,"Execution error"'
    applied = result["actions"][-1]
    assert applied["command"] == "TOPOLOGY_APPLIED" and applied["attempt"] == 2
    assert applied["readback"]["ring_config_seq"] == 6
    assert all(not cmd.endswith(":ARM") and "SESSion 0" not in cmd for _, cmd in bench.calls)


@pytest.mark.parametrize("reply,error,limit", [
    ("<timeout>", '-200,"Execution error"', 3),
    ("<timeout>", '0,"No error"', 1),
    ("<timeout>", '-222,"Data out of range"', 1),
    ("2,1,0", '-200,"Execution error"', 1),
])
def test_pair_topology_unknown_or_persistent_refusal_never_admits(monkeypatch, tmp_path, reply, error, limit):
    bench = Bench()
    attempts = []
    def hook(b, board, command):
        if command.startswith("SYSTem:TDMA:RING:TOPology "):
            attempts.append(command)
            b.errors[board.address].append(error)
            return reply
    bench.hook = hook
    bench.install(monkeypatch)
    actions = []
    with pytest.raises(RuntimeError):
        topology.configure_pair_topology(DRIVER, 0, options(tmp_path), actions)
    assert not actions[0]["passed"] and len(attempts) == limit


@pytest.mark.parametrize("session", ["42", "<timeout>"])
def test_pair_topology_preserves_session_and_refuses_unknown_owner(monkeypatch, tmp_path, session):
    bench = Bench()
    bench.hook = lambda b, board, cmd: session if cmd.endswith("SESSion?") else None
    bench.install(monkeypatch)
    with pytest.raises(RuntimeError, match="feedback session"):
        topology.configure_pair_topology(DRIVER, 0, options(tmp_path), [])
    assert not any("TOPology " in cmd or "SESSion 0" in cmd for _, cmd in bench.calls)


def test_pair_topology_cannot_use_old_stopped_generation_as_apply_proof(monkeypatch, tmp_path):
    bench = Bench()
    bench.hook = lambda b, board, cmd: "2,0,0" if "TOPology " in cmd else None
    bench.install(monkeypatch)
    args = options(tmp_path)
    args.arm_wait = .03
    actions = []
    with pytest.raises(RuntimeError, match="ring state deadline"):
        topology.configure_pair_topology(DRIVER, 0, args, actions)
    assert not actions[0]["passed"]


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
            result = snapshot_with_raw(board, result["tdma"], result["phys"])
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


@pytest.mark.parametrize("outcome", ["service_busy", "entry_busy", "unknown", "lost_success"])
def test_profile_uses_attributed_refusal_and_preserves_each_attempt(monkeypatch, tmp_path, outcome):
    from test_calibration_stopped_opmode import Bench as ProfileBench, PROFILE

    bench = ProfileBench((outcome, "success"))
    bench.active = (0,) * 6
    bench.apply_count = bench.reject_count = bench.last = 0
    calls = []

    def command(board, text, args):
        calls.append(text)
        if text == "SYSTem:TDMA:RING:STOP":
            return "OK"
        if text == "SYSTem:TDMA:OPMode:STAGe 7":
            return ",".join(map(str, PROFILE))
        if text == "CALibration:TOPology:PROBe 1,10":
            return "1,10"
        return bench.command(board, text, args)

    monkeypatch.setattr(topology.stopped_profile, "board_command", command)
    args = options(tmp_path)
    before_args = vars(args).copy()
    result = topology.apply_profile(DRIVER, args)
    attempts = [row for row in result["actions"] if row["command"].endswith(":APPLy")]
    accepted = outcome in {"service_busy", "entry_busy"}
    assert result["passed"] == accepted
    assert bench.applies == (2 if accepted else 1)
    assert len(attempts) == bench.applies and not attempts[0]["passed"]
    assert attempts[0]["response"] == "<timeout>"
    assert ("CALibration:TOPology:PROBe 1,10" in calls) == accepted
    assert calls.count("SYSTem:TDMA:OPMode:STAGe 7") == 1
    assert vars(args) == before_args
    if accepted:
        assert attempts[0]["disposition"] == "EXPLICIT_REFUSAL_STOPPED_PROFILE_UNCHANGED"
        assert attempts[-1]["passed"] and result["active_level"] == 7
    else:
        assert "error" in result and "error" in attempts[0]


def test_main_retains_failed_profile_actions_and_other_board_success(monkeypatch, tmp_path):
    bench = Bench()
    bench.hook = lambda b, board, command: "<timeout>" if (
        board.address == "A" and command == "SYSTem:TDMA:OPMode:APPLy") else None
    bench.install(monkeypatch)
    monkeypatch.setattr(topology, "parse_args", lambda: options(tmp_path))
    monkeypatch.setattr(topology, "discover", lambda _: {"A": DRIVER, "B": RECEIVER})
    assert topology.main() == 1
    report = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    profiles = {row["address"]: row for row in report["profile_apply"]}
    assert not profiles["A"]["passed"] and profiles["B"]["passed"]
    failed = [row for row in profiles["A"]["actions"] if row["command"].endswith(":APPLy")]
    assert len(failed) == 1 and failed[0]["response"] == "<timeout>"
    assert failed[0]["error_after"] == '0,"No error"'
    assert report["pair_actions"] == [] and len(report["cleanup"]) == 2


@pytest.mark.parametrize("plane", ["tdma", "phys"])
@pytest.mark.parametrize("phase", ["baseline", "activity"])
def test_malformed_snapshot_recovers_once_without_resending_control(monkeypatch, tmp_path, plane, phase):
    bench = Bench()
    bench.install(monkeypatch)
    bad_index = 1 if phase == "baseline" else 2

    def read(board, timeout):
        result = bench.snapshot(board, timeout)
        if bench.snapshots == bad_index:
            result[plane] = dict.fromkeys(result[plane], -1)
            result["raw"][plane] = "<timeout>"
        return result

    monkeypatch.setattr(topology, "snapshot", read)
    monkeypatch.setattr(topology, "parse_args", lambda: options(tmp_path))
    monkeypatch.setattr(topology, "discover", lambda _: {"A": DRIVER, "B": RECEIVER})
    assert topology.main() == 0
    report = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    rows = (report["pair_actions"][0]["baseline_observations"] if phase == "baseline"
            else report["pair_preparation"][0]["activity_observations"])
    assert [row["classification"] for row in rows] == ["MALFORMED", "VALID"]
    assert rows[0]["snapshot"]["raw"][plane] == "<timeout>"
    assert sum(command.endswith(":ARM") for _, command in bench.calls) == 4
    assert sum(command.endswith(":START") for _, command in bench.calls) == 4


@pytest.mark.parametrize("kind", ["timeout", "missing", "extra", "negative_counter",
                                  "overflow_counter", "bool_counter", "raw_disagrees"])
def test_bad_snapshot_has_only_two_observations_and_never_starts(monkeypatch, tmp_path, kind):
    bench = Bench()
    bench.install(monkeypatch)

    def read(board, timeout):
        result = bench.snapshot(board, timeout)
        if kind == "timeout":
            result["raw"]["tdma"] = "<timeout>"
        elif kind == "missing":
            del result["phys"]["rx_edge_count"]
        elif kind == "extra":
            result["raw"]["tdma"] += ",0"
        elif kind == "raw_disagrees":
            result["tdma"]["ring_adapter_rx_count"] += 1
        else:
            result["phys"]["rx_edge_count"] = {
                "negative_counter": -1, "overflow_counter": 0x100000000,
                "bool_counter": True}[kind]
        return result

    monkeypatch.setattr(topology, "snapshot", read)
    actions = []
    with pytest.raises(RuntimeError, match="malformed"):
        topology.start_pair(DRIVER, RECEIVER, options(tmp_path), actions, [])
    assert bench.snapshots == 2
    assert len(actions[0]["baseline_observations"]) == 2
    assert not any(command.endswith(":START") for _, command in bench.calls)


@pytest.mark.parametrize("kind", ["lifetime", "lifetime_and_bad_phys", "address", "build", "port"])
def test_valid_lifetime_or_identity_change_never_retries_snapshot(monkeypatch, tmp_path, kind):
    bench = Bench()
    bench.install(monkeypatch)

    def read(board, timeout):
        result = bench.snapshot(board, timeout)
        if kind.startswith("lifetime"):
            result["tdma"]["ring_config_seq"] += 1
            result = snapshot_with_raw(board, result["tdma"], result["phys"])
            if kind == "lifetime_and_bad_phys":
                result["raw"]["phys"] = "<timeout>"
            return result
        result[kind] = "different"
        return result

    monkeypatch.setattr(topology, "snapshot", read)
    actions = []
    with pytest.raises(RuntimeError):
        topology.start_pair(DRIVER, RECEIVER, options(tmp_path), actions, [])
    assert bench.snapshots == 1 and len(actions[0]["baseline_observations"]) == 1
    assert not any(command.endswith(":START") for _, command in bench.calls)


def test_valid_no_activity_is_a_negative_pair_and_scan_continues(monkeypatch, tmp_path):
    bench = Bench()
    bench.install(monkeypatch)

    def read(board, timeout):
        result = bench.snapshot(board, timeout)
        for key in ("ring_adapter_rx_count", "ring_adapter_tx_count"):
            result["tdma"][key] = 0
        for key in ("rx_dma_produced_words", "rx_edge_count"):
            result["phys"][key] = 0
        return snapshot_with_raw(board, result["tdma"], result["phys"])

    monkeypatch.setattr(topology, "snapshot", read)
    monkeypatch.setattr(topology, "parse_args", lambda: options(tmp_path))
    monkeypatch.setattr(topology, "discover", lambda _: {"A": DRIVER, "B": RECEIVER})
    assert topology.main() == 1  # Both pairs measured; no complete ring exists.
    report = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    assert report["error"] == "" and len(report["pair_results"]) == 2
    assert not any(row["detected"] for row in report["pair_results"])
    assert all(row["passed"] for row in report["pair_actions"])


def test_frequency_snapshot_keeps_original_queries_without_changing_named_values(monkeypatch):
    from contextlib import nullcontext
    import importlib

    sweep = importlib.import_module(topology.snapshot.__module__)
    raw = {"*IDN?": "identity", "SYSTem:REFMEM:SYNC:TDMA:STATus?": "<timeout>",
           "SYSTem:SYNC:VDC:TDMA:PHYS?": ",".join("0" for _ in PHYS_FIELDS)}
    monkeypatch.setattr(sweep, "open_serial_port", lambda *a, **kw: nullcontext(None))
    monkeypatch.setattr(sweep, "command", lambda ser, command, timeout: raw[command])
    monkeypatch.setattr(sweep, "parse_idn_response", lambda _: Namespace(address=DRIVER.address))
    result = sweep.snapshot(DRIVER, .01)
    assert result["raw"]["tdma"] == "<timeout>"
    assert result["raw"]["phys"] == raw["SYSTem:SYNC:VDC:TDMA:PHYS?"]
    assert set(result["tdma"].values()) == {-1} and set(result["phys"].values()) == {0}
