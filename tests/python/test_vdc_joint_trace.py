"""Regression checks for the tracked VDC joint native trace layer."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import trace as tracked_trace


UIDS = ("uid-0", "uid-1", "uid-2", "uid-3")


def status(slot, command, state, *, capture_id=77):
    return {"request_seq": command, "ack_seq": command,
            "command": command, "state": state, "capture_id": capture_id,
            "session": 77, "generation": 77, "local_slot": slot,
            "reference_slot": 0, "reason": 1, "dropped_count": 0,
            "tick_hz": 250_000_000}


def decision(slot):
    return {"kind": 2, "index": 21, "event_sequence": 100 + slot,
            "baseline_sequence": 90, "before_seq": 3, "after_seq": 4,
            "before_ppb": -10, "after_ppb": -9, "delta_ppb": 1,
            "error_lo_ppb": -20, "error_hi_ppb": -10}


def fake_follow(tmp_path, *, fail_download_slot=None,
                mismatch_slot=None, fail_release_slot=None):
    events = []
    files = {}
    for name in ("follow", "observer", "parent", "base"):
        path = tmp_path / f"{name}.py"
        path.write_text(f"# {name}\n", encoding="utf-8")
        files[name] = path
    backend = SimpleNamespace(TDMA_CONTROL_RESULT_FIELDS={})
    base = SimpleNamespace(
        __file__=str(files["base"]), BACKEND=backend,
        sha=lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest())
    parent = SimpleNamespace(
        __file__=str(files["parent"]), base=base,
        SYNC="SYSTem:VDC:PRIORity:SYNC",
        scpi_serial=SimpleNamespace(SCALAR_ONE_QUERY_HEADERS=set()))
    observer = SimpleNamespace(__file__=str(files["observer"]))

    class FollowProbe:
        def __init__(self, options):
            self.opt = options
            self.boards = [SimpleNamespace(address=uid) for uid in UIDS]
            self.report = {"schema": "follow", "plan": {},
                           "tool_sources": [], "binding_generation": 77,
                           "boards": {uid: {} for uid in UIDS},
                           "errors": [], "passed": True}
            self.statuses = {uid: [] for uid in UIDS}
            for slot, uid in enumerate(UIDS[1:], 1):
                self.statuses[uid].extend((
                    status(slot, 9, 0, capture_id=1), status(slot, 1, 1),
                    status(slot, 2, 3), status(slot, 3, 0),
                ))
            self.barrier = True
            self.block_queries = False

        def control(self, board, text, expected="OK"):
            events.append(("control", board.address, text, expected))
            slot = self.boards.index(board)
            if text.endswith("RELease") and slot == fail_release_slot:
                raise RuntimeError("release failed")
            return str(expected)

        def command(self, board, text):
            events.append(("command", board.address, text))
            if text == tracked_trace.PREFIX + "STATus?":
                return json.dumps(self.statuses[board.address].pop(0))
            if text == "__trace_uid__":
                return board.address
            return "OK"

        def collect(self):
            events.append(("parent_collect",))
            for slot, board in enumerate(self.boards[1:], 1):
                row = self.report["boards"][board.address]
                last = decision(slot)
                row["follow"] = {
                    "event_sequence": last["event_sequence"],
                    "baseline_sequence": last["baseline_sequence"],
                    "before_dco_seq": last["before_seq"],
                    "after_dco_seq": last["after_seq"],
                    "before_ppb": last["before_ppb"],
                    "after_ppb": (999 if slot == mismatch_slot else
                                  last["after_ppb"]),
                    "selected_delta_ppb": last["delta_ppb"],
                    "error_lo_ppb": last["error_lo_ppb"],
                    "error_hi_ppb": last["error_hi_ppb"],
                }

        def save(self):
            events.append(("save",))

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": str(exc)})

    follow_path = tmp_path / "capture_follow.py"
    follow_path.write_text("# follow parent\n", encoding="utf-8")
    follow = SimpleNamespace(
        __file__=str(follow_path), FollowProbe=FollowProbe,
        FOLLOW="SYSTem:VDC:PRIORity:FOLLow", parent=parent,
        observer=observer)

    def download_capture(command, capture_id):
        uid = command("__trace_uid__")
        slot = int(uid.rsplit("-", 1)[1])
        if slot == fail_download_slot:
            raise RuntimeError("download failed")
        return uid.encode("ascii"), [{"capture_id": capture_id}]

    def decode(data, capture_id):
        slot = int(data.decode("ascii").rsplit("-", 1)[1])
        matches = [{"kind": 1, "index": index,
                    "raw_hi": index * 1000,
                    "raw_lo": index * 1000 + (1_750_000_000 if index == 19 else 0)}
                   for index in range(20)]
        return {"file_crc32": 123, "status": status(slot, 2, 3),
                "records": matches + [decision(slot)]}

    return follow, events, download_capture, decode


def build_probe(tmp_path, monkeypatch, **faults):
    follow, events, download, decode = fake_follow(tmp_path, **faults)
    monkeypatch.setattr(tracked_trace.trace_codec, "parse_status", json.loads)
    monkeypatch.setattr(tracked_trace.trace_codec, "download_capture", download)
    monkeypatch.setattr(tracked_trace.trace_codec, "decode", decode)
    module = tracked_trace.load_trace(Path("unused.py"), runtime_module=follow)
    return module, module.TraceProbe(SimpleNamespace(out=tmp_path)), events, follow


def test_trace_factory_registers_commands_and_arms_followers(tmp_path, monkeypatch):
    module, probe, events, follow = build_probe(tmp_path, monkeypatch)
    probe.control(probe.boards[0], follow.parent.SYNC + " 7", 7)
    assert probe.report["schema"] == "TYPED_TRACE_EIGHT_SECOND_PROBE_V1"
    assert probe.report["plan"]["trace_matches_min"] == 20
    assert follow.FOLLOW.upper() + "?" in \
        follow.parent.scpi_serial.SCALAR_ONE_QUERY_HEADERS
    assert follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS == {
        (tracked_trace.PREFIX + suffix).upper(): 1
        for suffix in ("ARM", "STOP", "RELease")}
    assert all(probe.report["boards"][uid]["trace_arm_requested"]
               for uid in UIDS[1:])
    assert len([event for event in events
                if event[0] == "control" and "ARM" in event[2]]) == 3
    sources = [row["path"] for row in probe.report["tool_sources"]]
    assert str(module.source_path) in sources
    assert not any("dpll-priority-trace-r1/capture_trace.py" in path.replace("\\", "/")
                   for path in sources)


def test_trace_collect_success_downloads_checks_and_releases(tmp_path, monkeypatch):
    module, probe, events, follow = build_probe(tmp_path, monkeypatch)
    probe.control(probe.boards[0], follow.parent.SYNC + " 7", 7)
    probe.collect()
    assert not probe.report["errors"] and probe.report["passed"] is True
    for uid in UIDS[1:]:
        row = probe.report["boards"][uid]
        assert row["trace_read_complete"] is True
        assert row["trace_match_span_s"] >= 6
        assert row["trace_last_decision_consistent"] is True
        assert row["trace_release_ack"]
        assert (tmp_path / f"{uid}-typed.ram.bin").is_file()


@pytest.mark.parametrize("fault", ["download", "mismatch", "release"])
def test_trace_collect_faults_preserve_release_and_verdict(
        tmp_path, monkeypatch, fault):
    kwargs = ({"fail_download_slot": 2} if fault == "download" else
              {"mismatch_slot": 2} if fault == "mismatch" else
              {"fail_release_slot": 2})
    module, probe, events, follow = build_probe(tmp_path, monkeypatch, **kwargs)
    probe.control(probe.boards[0], follow.parent.SYNC + " 7", 7)
    probe.collect()
    row = probe.report["boards"]["uid-2"]
    assert probe.report["passed"] is False
    if fault == "download":
        assert row["trace_retained_for_recovery"] is True
        assert "trace_release_ack" not in row
        assert any(error["scope"] == "typed-trace:uid-2"
                   for error in probe.report["errors"])
    elif fault == "mismatch":
        assert row["trace_release_ack"]
        assert any(error["scope"] == "trace-last-decision:uid-2"
                   for error in probe.report["errors"])
    else:
        assert "trace_release_ack" not in row
        assert any(error["scope"] == "trace-release:uid-2"
                   for error in probe.report["errors"])
    assert all("trace_release_ack" in probe.report["boards"][f"uid-{slot}"]
               for slot in (1, 3))


def test_trace_ack_times_out_with_recorded_samples(tmp_path, monkeypatch):
    module, probe, _, _ = build_probe(tmp_path, monkeypatch)
    probe.statuses["uid-1"] = [status(1, 9, 0, capture_id=1)] * 3
    now = [0.0]
    monkeypatch.setattr(tracked_trace.time, "monotonic", lambda: now[0])
    monkeypatch.setattr(tracked_trace.time, "sleep", lambda seconds: now.__setitem__(0, now[0] + 1.0))
    with pytest.raises(RuntimeError, match="not acknowledged"):
        probe.trace_ack(probe.boards[1], 1, 1, 77)
