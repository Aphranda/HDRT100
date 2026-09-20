"""Regression checks for the tracked VDC joint exact-match layer."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import pytest

from tools.domains.vdc.priority_trace import probe_support
from tools.workflows.vdc_joint_capture import match as tracked_match


ROOT = Path(__file__).resolve().parents[2]
ARCHIVED = (ROOT / "out/HardwareAcceptance/20260917/"
            "dpll-priority-match-r1/capture_match.py")
BASE = (ROOT / "out/HardwareAcceptance/20260915/"
        "dpll-input-probe-r1/input_probe.py")
GOLDEN = (ROOT / "out/HardwareAcceptance/20260917/"
          "dpll-longwindow-adoption-r1/native-r1/input-probe.json")
UIDS = ("uid-0", "uid-1", "uid-2", "uid-3")


def archived_module():
    spec = importlib.util.spec_from_file_location(
        "archived_match_for_test", ARCHIVED)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    missing = object()
    previous = sys.modules.get("probe_support", missing)
    try:
        sys.modules["probe_support"] = probe_support
        spec.loader.exec_module(module)
    finally:
        if previous is missing:
            sys.modules.pop("probe_support", None)
        else:
            sys.modules["probe_support"] = previous
    return module


def encode_match(slot, delay, *, generation=77, event_sequence=101):
    scalars = dict.fromkeys(tracked_match.SCALARS, 0)
    scalars.update(
        schema=1, generation=generation, retired=1, have_match=1,
        calls=10, matched=8, source_slot=0, event_sequence=event_sequence,
        carrier_sequence=102, rx_epoch=8, session=55, role_generation=2,
        clock_epoch=1, clock_run=1, model_token=9, ring_config_seq=4,
        ring_applied_seq=4, local_slot=slot, reference_slot=0,
        node_count=4, schedule_crc32=11, profile_crc32=12,
        observer_epoch=6, tick_hz=250_000_000, path_crc32=13,
        delay_ns=delay, path_direction=0, path_qualification=1)
    remote_lo, remote_hi = 1_000, 1_010
    expected_lo, expected_hi = remote_lo + delay, remote_hi + delay
    wide = dict.fromkeys(tracked_match.WIDE, 0)
    wide.update(
        arm_epoch=7, valid_from_raw=90, raw_lo=100, raw_hi=110,
        local_lo=expected_lo - 5, local_hi=expected_hi + 5,
        remote_lo=remote_lo, remote_hi=remote_hi,
        expected_lo=expected_lo, expected_hi=expected_hi,
        residual_lo=-15, residual_hi=15)
    words = [scalars[name] for name in tracked_match.SCALARS]
    for name in tracked_match.WIDE:
        value = wide[name] & ((1 << 64) - 1)
        words.extend((value & 0xFFFFFFFF, value >> 32))
    return ",".join(map(str, words))


def fake_direct(tmp_path, *, bad_slot=None, parent_failure=False):
    source = tmp_path / "direct.py"
    source.write_text("# fake direct parent\n", encoding="utf-8")
    events = []
    backend = SimpleNamespace(TDMA_CONTROL_RESULT_FIELDS={})
    scpi = SimpleNamespace(SCALAR_U32_QUERY_HEADERS=set())
    base = SimpleNamespace(
        BACKEND=backend,
        sha=lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest())
    parent = SimpleNamespace(
        base=base, scpi_serial=scpi,
        SYNC="SYSTem:VDC:PRIORity:SYNC",
        SESSION="SYSTem:VDC:FEEDback:SESSion",
        MODEL="SYSTem:VDC:MODEL?",
    )
    links = [
        {"marker_source_node": 0, "marker_destination_node": 1,
         "valid": 1, "link_base_delay_ns": 40},
        {"marker_source_node": 1, "marker_destination_node": 2,
         "valid": 1, "link_base_delay_ns": 40},
        {"marker_source_node": 2, "marker_destination_node": 3,
         "valid": 1, "link_base_delay_ns": 41},
        {"marker_source_node": 3, "marker_destination_node": 0,
         "valid": 1, "link_base_delay_ns": 42},
    ]

    def numbers(raw, count):
        values = [int(value) for value in raw.strip('"').split(",")]
        if len(values) != count or any(value < 0 or value > 0xFFFFFFFF
                                       for value in values):
            raise ValueError("invalid field set")
        return values

    class DirectProbe:
        def __init__(self, options):
            self.opt = options
            self.boards = [SimpleNamespace(address=uid) for uid in UIDS]
            self.barrier = True
            self.block_queries = False
            self.match_generation = {uid: 0 for uid in UIDS}
            self.tap = {uid: [0, slot + 10, slot + 20]
                        for slot, uid in enumerate(UIDS)}
            self.tap_active = {uid: False for uid in UIDS}
            self.report = {
                "schema": "direct", "plan": {}, "tool_sources": [],
                "binding_generation": 77, "feedback_session": 55,
                "tx_status": {"schedule_crc32": 11, "profile_crc32": 12},
                "boards": {}, "errors": [], "passed": True,
            }
            for slot, uid in enumerate(UIDS):
                self.report["boards"][uid] = {
                    "stage_reuse": {"links": links},
                    "direct": {"epoch": 8, "event_sequence": 101,
                               "lower_ns": 1_000, "width_ns": 10},
                }

        def save(self):
            events.append(("save",))

        def error(self, scope, exc):
            self.report["errors"].append(
                {"scope": scope, "error": str(exc)})

        def control(self, board, text, expected="OK"):
            events.append(("control", board.address, text, expected))
            upper = text.upper()
            tap_prefix = "SYSTEM:TDMA:EVENT:TAP "
            if upper.startswith(tap_prefix):
                self.tap[board.address] = [
                    int(value) for value in text.split(" ", 1)[1].split(",")]
                self.tap_active[board.address] = True
            if upper.startswith(tracked_match.MATCH.upper() + " "):
                self.match_generation[board.address] = int(text.split()[-1])
            return str(expected)

        def composite(self, board, text, generation):
            events.append(("composite", board.address, text, generation))
            return str(generation)

        def command(self, board, text):
            events.append(("command", board.address, text))
            uid = board.address
            slot = self.boards.index(board)
            if text == tracked_match.MATCH + "?":
                return str(self.match_generation[uid])
            if text == "SYSTem:TDMA:EVENt:TAP?":
                request = self.tap[uid]
                if self.tap_active[uid]:
                    return ",".join(map(str, request + [77] + request +
                                        [77, 1, 1] + request[1:]))
                return ",".join(map(str, request + [0] * 9))
            if text == tracked_match.STATUS:
                generation = 78 if slot == bad_slot else 77
                return encode_match(slot, tracked_match_delay(slot),
                                    generation=generation)
            return "1"

        def collect(self):
            events.append(("parent_collect",))
            if parent_failure:
                raise RuntimeError("parent transport failed")

        def run(self):
            events.append(("parent_run",))

    def tracked_match_delay(slot):
        return sum(link["link_base_delay_ns"] * 2
                   for link in links[:slot])

    return SimpleNamespace(
        __file__=str(source), DirectProbe=DirectProbe, parent=parent,
        numbers=numbers), events


def build_probe(tmp_path, **faults):
    direct, events = fake_direct(tmp_path, **faults)
    tap = tmp_path / "tap.json"
    tap.write_text(json.dumps({
        "passed": True,
        "boards": {
            uid: {"tap_request": [0, 0, 0] if slot == 0
                  else [1, slot + 2, slot + 3]}
            for slot, uid in enumerate(UIDS)},
    }), encoding="utf-8")
    module = tracked_match.load_match(
        Path("unused.py"), runtime_module=direct)
    options = SimpleNamespace(out=tmp_path, tap_baseline=tap)
    return module, module.MatchProbe(options), events, direct


def test_parser_and_forward_delay_match_archived_and_frozen_hardware():
    old = archived_module()
    new = tracked_match.load_match(BASE)
    report = json.loads(GOLDEN.read_text(encoding="utf-8"))
    assert new.SCALARS == old.SCALARS
    assert new.WIDE == old.WIDE
    followers = list(report["boards"].values())[1:]
    for slot, row in enumerate(followers, 1):
        assert new.parse_match(row["match_raw"]) == old.parse_match(
            row["match_raw"]) == row["match"]
        links = row["stage_reuse"]["links"]
        assert new.forward_delay(links, 0, slot) == \
            old.forward_delay(links, 0, slot) == \
            row["independent_forward_delay_ns"]


@pytest.mark.parametrize("fault", ["duplicate", "cycle", "invalid", "negative"])
def test_forward_delay_rejects_invalid_directed_paths(fault):
    links = [
        {"marker_source_node": 0, "marker_destination_node": 1,
         "valid": 1, "link_base_delay_ns": 4},
        {"marker_source_node": 1, "marker_destination_node": 2,
         "valid": 1, "link_base_delay_ns": 5},
    ]
    if fault == "duplicate":
        links[1]["marker_source_node"] = 0
    elif fault == "cycle":
        links[1]["marker_destination_node"] = 0
    elif fault == "invalid":
        links[0]["valid"] = 0
    else:
        links[0]["link_base_delay_ns"] = -1
    with pytest.raises(RuntimeError):
        tracked_match.load_match(BASE).forward_delay(links, 0, 2)


def test_match_factory_registers_commands_installs_and_restores_tap(tmp_path):
    module, probe, events, direct = build_probe(tmp_path)
    master = probe.boards[0]
    probe.control(master, direct.parent.SYNC + " 77", 77)
    assert probe.report["schema"] == "PRIORITY_EXACT_MATCH_SHORT_PROBE_V1"
    assert direct.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[
        tracked_match.MATCH.upper()] == 1
    assert tracked_match.MATCH.upper() + "?" in \
        direct.parent.scpi_serial.SCALAR_U32_QUERY_HEADERS
    assert len([event for event in events if event[0] == "composite"]) == 3
    for uid in UIDS[1:]:
        row = probe.report["boards"][uid]
        assert row["tap_installed"] is True
        assert probe.match_generation[uid] == 77

    follower = probe.boards[2]
    original = probe.report["boards"][follower.address]["original_tap"][:3]
    probe.control(follower, direct.parent.SYNC + " 0", 0)
    row = probe.report["boards"][follower.address]
    assert row["tap_installed"] is False
    assert row["restored_tap"][:3] == original
    follower_controls = [event[2] for event in events
                         if event[:2] == ("control", follower.address)]
    assert tracked_match.MATCH + " 0" in follower_controls
    assert any(text.startswith("SYSTem:TDMA:EVENt:TAP ")
               for text in follower_controls)
    assert str(module.source_path) in [
        row["path"] for row in probe.report["tool_sources"]]


@pytest.mark.parametrize("barrier,blocked", [(False, False), (True, True)])
def test_match_collect_requires_stop_barrier(tmp_path, barrier, blocked):
    _, probe, events, _ = build_probe(tmp_path)
    probe.barrier = barrier
    probe.block_queries = blocked
    with pytest.raises(RuntimeError, match="all-board STOP"):
        probe.collect()
    assert events == []


def test_match_collect_assesses_all_followers_after_parent(tmp_path):
    _, probe, events, direct = build_probe(tmp_path)
    probe.control(probe.boards[0], direct.parent.SYNC + " 77", 77)
    probe.collect()
    assert probe.report["passed"] is True
    assert not probe.report["errors"]
    assert all(probe.report["boards"][uid]["matched_observation_passed"]
               for uid in UIDS[1:])
    assert [probe.report["boards"][uid]["independent_forward_delay_ns"]
            for uid in UIDS[1:]] == [80, 160, 242]
    parent_index = events.index(("parent_collect",))
    match_queries = [index for index, event in enumerate(events)
                     if event[0] == "command" and event[2] == tracked_match.STATUS]
    assert match_queries and max(match_queries) < parent_index


def test_match_collect_isolates_one_follower_assessment_failure(tmp_path):
    _, probe, _, direct = build_probe(tmp_path, bad_slot=2)
    probe.control(probe.boards[0], direct.parent.SYNC + " 77", 77)
    probe.collect()
    assert probe.report["passed"] is False
    assert probe.report["boards"][UIDS[1]]["matched_observation_passed"] is True
    assert "matched_observation_passed" not in probe.report["boards"][UIDS[2]]
    assert probe.report["boards"][UIDS[3]]["matched_observation_passed"] is True
    assert [row["scope"] for row in probe.report["errors"]] == [
        "match-assessment:" + UIDS[2]]


def test_match_collect_retains_assessment_after_parent_transport_failure(tmp_path):
    _, probe, _, direct = build_probe(tmp_path, parent_failure=True)
    probe.control(probe.boards[0], direct.parent.SYNC + " 77", 77)
    probe.collect()
    assert probe.report["passed"] is False
    assert all(probe.report["boards"][uid]["matched_observation_passed"]
               for uid in UIDS[1:])
    assert probe.report["errors"][0]["scope"] == "transport-assessment"


def test_match_factory_rejects_incomplete_direct_module():
    with pytest.raises(ValueError, match="DirectProbe/parent"):
        tracked_match.load_match(
            Path("unused.py"), runtime_module=SimpleNamespace())
