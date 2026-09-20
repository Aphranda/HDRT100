"""Regression checks for the tracked VDC joint local-follow layer."""

from copy import deepcopy
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import follow as tracked_follow


ROOT = Path(__file__).resolve().parents[2]
ARCHIVED = (ROOT / "out/HardwareAcceptance/20260917/"
            "dpll-priority-follow-r1/capture_follow.py")
OBSERVER = (ROOT / "out/HardwareAcceptance/20260917/"
            "capture_observer_dma_match.py")
MATCH = (ROOT / "out/HardwareAcceptance/20260917/"
         "dpll-priority-match-r1/capture_match.py")
GOLDEN = (ROOT / "out/HardwareAcceptance/20260917/"
          "dpll-longwindow-adoption-r1/native-r1/input-probe.json")


def archived_module():
    spec = importlib.util.spec_from_file_location(
        "archived_follow_for_test", ARCHIVED)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_parser_and_decision_match_archived_and_frozen_hardware():
    old = archived_module()
    new = tracked_follow.load_follow(MATCH)
    report = json.loads(GOLDEN.read_text(encoding="utf-8"))
    assert new.U32 == old.U32
    assert new.I32 == old.I32
    assert new.U64 == old.U64
    assert new.I64 == old.I64
    assert new.MODEL == old.MODEL
    followers = list(report["boards"].values())[1:]
    for row in followers:
        parsed = new.parse_follow(row["follow_raw"])
        assert parsed == old.parse_follow(row["follow_raw"])
        assert parsed == row["follow"]
        assert new.check_decision(parsed) == old.check_decision(parsed)
        assert new.check_decision(parsed) == row["decision_check"]


@pytest.mark.parametrize("mutation", ["prepared", "interval", "error", "step", "sequence"])
def test_decision_check_rejects_same_closed_set_as_archived(mutation):
    old = archived_module()
    new = tracked_follow.load_follow(MATCH)
    report = json.loads(GOLDEN.read_text(encoding="utf-8"))
    status = deepcopy(list(report["boards"].values())[1]["follow"])
    if mutation == "prepared":
        status["prepared"] = 0
    elif mutation == "interval":
        status["interval_lo"] = 0
    elif mutation == "error":
        status["error_lo_ppb"] += 1
    elif mutation == "step":
        status["selected_delta_ppb"] += 1
    else:
        status["event_sequence"] = status["baseline_sequence"]
    for module in (old, new):
        with pytest.raises((AssertionError, RuntimeError)):
            module.check_decision(status)


def fake_observer(tmp_path, *, mismatch_slot=None):
    events = []
    golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
    frozen = list(golden["boards"].values())
    uids = tuple(golden["boards"])
    binding_generation = golden["binding_generation"]
    backend = SimpleNamespace(TDMA_CONTROL_RESULT_FIELDS={})
    base_path = tmp_path / "base.py"
    base_path.write_text("# base\n", encoding="utf-8")
    base = SimpleNamespace(__file__=str(base_path), BACKEND=backend)
    parent_path = tmp_path / "direct.py"
    parent_path.write_text("# direct\n", encoding="utf-8")
    parent = SimpleNamespace(
        __file__=str(parent_path), base=base,
        SYNC="SYSTem:VDC:PRIORity:SYNC",
        fields=lambda raw: raw.split(","),
        scpi_serial=SimpleNamespace(SCALAR_U32_QUERY_HEADERS=set()))

    def numbers(raw, count):
        values = [int(value) for value in raw.split(",")]
        if len(values) != count:
            raise ValueError("field count")
        return values

    direct = SimpleNamespace(numbers=numbers)
    match = SimpleNamespace(parent=parent, direct=direct)

    class ObserverProbe:
        def __init__(self, options):
            self.opt = options
            self.boards = [SimpleNamespace(address=uid) for uid in uids]
            self.report = {"schema": "observer", "plan": {},
                           "tool_sources": [],
                           "binding_generation": binding_generation,
                           "boards": {uid: {} for uid in uids},
                           "errors": [], "passed": True}
            self.enabled = {uid: 0 for uid in uids}
            self.barrier = True
            self.block_queries = False

        def control(self, board, text, expected="OK"):
            events.append(("control", board.address, text, expected))
            if text.startswith(tracked_follow.FOLLOW + " "):
                self.enabled[board.address] = int(text.rsplit(" ", 1)[1])
            return str(expected)

        def command(self, board, text):
            events.append(("command", board.address, text))
            slot = uids.index(board.address)
            if text == tracked_follow.FOLLOW + "?":
                return str(self.enabled[board.address])
            if text == tracked_follow.STATUS:
                return frozen[slot]["follow_raw"]
            return "OK"

        def collect(self):
            events.append(("parent_collect",))
            for slot, board in enumerate(self.boards):
                raw = frozen[slot]["local_model_raw"]
                if slot == mismatch_slot:
                    values = raw.split(",")
                    values[17] = "999"
                    raw = ",".join(values)
                self.report["boards"][board.address]["local_model_raw"] = raw

        def save(self):
            events.append(("save",))

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": str(exc)})

        def run(self):
            events.append(("parent_run",))

    observer_path = tmp_path / "observer.py"
    observer_path.write_text("# observer\n", encoding="utf-8")
    return SimpleNamespace(
        __file__=str(observer_path), ObserverProbe=ObserverProbe,
        probe=match), events, uids


def test_control_disables_one_board_and_enables_only_followers(tmp_path):
    observer, events, uids = fake_observer(tmp_path)
    module = tracked_follow.load_follow(Path("unused.py"), runtime_module=observer)
    probe = module.FollowProbe(SimpleNamespace(out=tmp_path))
    probe.control(probe.boards[0], observer.probe.parent.SYNC + " 0", 0)
    probe.control(probe.boards[0], observer.probe.parent.SYNC + " 7", 7)
    assert probe.enabled[uids[0]] == 0
    assert [probe.enabled[uid] for uid in uids[1:]] == [1, 1, 1]
    assert all(probe.report["boards"][uid]["follow_enable_requested"]
               for uid in uids[1:])
    assert all("follow_before_raw" in probe.report["boards"][uid]
               for uid in uids[1:])
    assert observer.probe.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS == {
        tracked_follow.FOLLOW.upper(): 1}
    assert tracked_follow.FOLLOW.upper() + "?" in \
        observer.probe.parent.scpi_serial.SCALAR_U32_QUERY_HEADERS


def test_collect_checks_follower_models_and_isolates_mismatch(tmp_path):
    observer, events, uids = fake_observer(tmp_path, mismatch_slot=2)
    module = tracked_follow.load_follow(Path("unused.py"), runtime_module=observer)
    probe = module.FollowProbe(SimpleNamespace(out=tmp_path))
    probe.collect()
    assert ("parent_collect",) in events
    assert probe.report["boards"][uids[1]]["actual_dco_consistent"] is True
    assert probe.report["boards"][uids[3]]["actual_dco_consistent"] is True
    assert "actual_dco_consistent" not in probe.report["boards"][uids[2]]
    assert any(row["scope"] == "follow-assessment:" + uids[2]
               for row in probe.report["errors"])
    assert probe.report["passed"] is False
    assert all("follow_raw" in probe.report["boards"][uid] for uid in uids)


def test_follow_factory_rejects_incomplete_observer_module():
    with pytest.raises(ValueError, match="ObserverProbe/probe"):
        tracked_follow.load_follow(Path("unused.py"), runtime_module=SimpleNamespace())
