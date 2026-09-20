"""Regression checks for the tracked VDC joint long-window layer."""

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import longwindow as tracked_longwindow


ROOT = Path(__file__).resolve().parents[2]
ARCHIVED = (ROOT / "out/HardwareAcceptance/20260917/"
            "dpll-longwindow-adoption-r1/capture_longwindow.py")
TRACE = (ROOT / "out/HardwareAcceptance/20260917/"
         "dpll-priority-trace-r1/capture_trace.py")
FOLLOW = (ROOT / "out/HardwareAcceptance/20260917/"
          "dpll-priority-follow-r1/capture_follow.py")
OBSERVER = (ROOT / "out/HardwareAcceptance/20260917/"
            "capture_observer_dma_match.py")
GOLDEN = (ROOT / "out/HardwareAcceptance/20260917/"
          "dpll-longwindow-adoption-r1/native-r1/input-probe.json")


def archived_module():
    spec = importlib.util.spec_from_file_location(
        "archived_longwindow_for_test", ARCHIVED)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_oracles_match_archived_and_frozen_hardware_report():
    old = archived_module()
    new = tracked_longwindow.load_longwindow(OBSERVER)
    report = json.loads(GOLDEN.read_text(encoding="utf-8"))
    assert new.THRESHOLDS == old.THRESHOLDS
    assert new.MAX_NS == old.MAX_NS
    assert new.QUANTIZATION_NS == old.QUANTIZATION_NS
    assert new.RUN_SECONDS == old.RUN_SECONDS
    assert new.TRIAL_SECONDS == old.TRIAL_SECONDS
    followers = list(report["boards"].values())[1:]
    assert [len(row["trace_decisions"]) for row in followers] == [8, 8, 8]
    for row in followers:
        decisions = row["trace_decisions"]
        assert new.check_windows(decisions) == old.check_windows(decisions)
        assert new.check_windows(decisions) == row["longwindow_check"]
        status = row["trace_stop_ack"]["status"]
        assert new.check_last_projection(row["follow"], status) == \
            old.check_last_projection(row["follow"], status)
        assert new.check_last_projection(row["follow"], status) == \
            row["correlated_last_decision"]


@pytest.mark.parametrize("mutation", ["range", "error", "identity", "width"])
def test_decision_oracle_rejects_same_closed_set_as_archived(mutation):
    old = archived_module()
    new = tracked_longwindow.load_longwindow(OBSERVER)
    report = json.loads(GOLDEN.read_text(encoding="utf-8"))
    record = deepcopy(list(report["boards"].values())[1]["trace_decisions"][0])
    if mutation == "range":
        record["expected_delta_lo"] = 1
    elif mutation == "error":
        record["error_lo_ppb"] += 1
    elif mutation == "identity":
        record["before_seq"] = 0
    else:
        record["local_delta_hi"] += 100_000
    for module in (old, new):
        with pytest.raises(ValueError):
            module.decision_oracle(record)


def fake_trace_parent(tmp_path, *, trial_grant=24):
    events = []
    files = {}
    for name in ("trace", "follow", "observer", "match", "direct", "parent", "base"):
        path = tmp_path / f"{name}.py"
        path.write_text(f"# {name}\n", encoding="utf-8")
        files[name] = path
    base = SimpleNamespace(
        __file__=str(files["base"]),
        sha=lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest())
    follow_parent = SimpleNamespace(
        __file__=str(files["parent"]), base=base)
    direct = SimpleNamespace(__file__=str(files["direct"]))
    match = SimpleNamespace(__file__=str(files["match"]), direct=direct)
    observer = SimpleNamespace(__file__=str(files["observer"]))
    follow = SimpleNamespace(
        __file__=str(files["follow"]), observer=observer,
        match=match, parent=follow_parent)
    trace = SimpleNamespace(__file__=str(files["trace"]))

    class TraceProbe:
        def __init__(self, options):
            self.opt = options
            self.boards = [SimpleNamespace(address=f"uid-{index}")
                           for index in range(4)]
            self.report = {
                "schema": "parent", "plan": {}, "clock_hz": 250_000_000,
                "tool_sources": [{"path": str(files["trace"]),
                                  "sha256": base.sha(files["trace"]) }],
                "boards": {board.address: {} for board in self.boards},
                "errors": [], "passed": True,
            }

        def control(self, board, command):
            events.append(("control", board.address, command))
            return "OK"

        def command(self, board, command):
            events.append(("command", board.address, command))
            return str(trial_grant)

        def save(self):
            events.append(("save",))

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": str(exc)})

        def collect(self):
            events.append(("parent_collect",))

    parent_path = tmp_path / "capture_trace.py"
    parent_path.write_text("# trace parent\n", encoding="utf-8")
    return SimpleNamespace(
        __file__=str(parent_path), TraceProbe=TraceProbe,
        follow=follow, trace=trace), events


def test_acquire_starts_reverse_order_and_runs_declared_quiet_window(
        tmp_path, monkeypatch):
    parent, events = fake_trace_parent(tmp_path)
    module = tracked_longwindow.load_longwindow(
        Path("unused.py"), runtime_module=parent)
    now = [100.0]

    def sleep(seconds):
        now[0] += seconds

    monkeypatch.setattr(tracked_longwindow.time, "monotonic", lambda: now[0])
    monkeypatch.setattr(tracked_longwindow.time, "time", lambda: 1000.0)
    monkeypatch.setattr(tracked_longwindow.time, "sleep", sleep)
    probe = module.LongWindowProbe(SimpleNamespace(out=tmp_path))
    probe.acquire()

    starts = [event[1] for event in events
              if event[0] == "control" and event[2] == "SYSTem:TDMA:RING:START"]
    assert starts == ["uid-3", "uid-2", "uid-1", "uid-0"]
    trial = next(event for event in events
                 if event[0] == "command" and "ORIGin:TRIAL" in event[2])
    assert trial[1] == "uid-0"
    assert trial[2].endswith(",5000000000")
    assert probe.report["trial_epoch"] == 24
    assert probe.report["quiet_wait_elapsed_s"] == module.RUN_SECONDS
    assert probe.block_queries is True and probe.barrier is False


@pytest.mark.parametrize("grant", [0, 1, 25])
def test_acquire_rejects_invalid_trial_grant(tmp_path, monkeypatch, grant):
    parent, _ = fake_trace_parent(tmp_path, trial_grant=grant)
    module = tracked_longwindow.load_longwindow(
        Path("unused.py"), runtime_module=parent)
    monkeypatch.setattr(tracked_longwindow.time, "sleep", lambda seconds: None)
    probe = module.LongWindowProbe(SimpleNamespace(out=tmp_path))
    with pytest.raises(ValueError, match="admission"):
        probe.acquire()


def test_collect_isolates_one_follower_oracle_failure(tmp_path):
    parent, events = fake_trace_parent(tmp_path)
    module = tracked_longwindow.load_longwindow(
        Path("unused.py"), runtime_module=parent)
    probe = module.LongWindowProbe(SimpleNamespace(out=tmp_path))
    golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
    frozen = list(golden["boards"].values())
    for board, row in zip(probe.boards, frozen):
        probe.report["boards"][board.address] = deepcopy(row)
        probe.report["boards"][board.address].pop(
            "longwindow_independent_checks_passed", None)
    probe.report["boards"]["uid-2"]["trace_decisions"][0][
        "expected_delta_lo"] = 1
    probe.collect()

    assert ("parent_collect",) in events
    assert probe.report["boards"]["uid-1"][
        "longwindow_independent_checks_passed"] is True
    assert probe.report["boards"]["uid-3"][
        "longwindow_independent_checks_passed"] is True
    assert "longwindow_independent_checks_passed" not in \
        probe.report["boards"]["uid-2"]
    assert any(row["scope"] == "longwindow-independent:uid-2"
               for row in probe.report["errors"])
    assert probe.report["passed"] is False
