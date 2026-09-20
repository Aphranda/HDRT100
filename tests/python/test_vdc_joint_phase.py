"""Regression checks for the tracked VDC joint phase layer."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import phase as tracked_phase


ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = ROOT / "tools/archive/vdc/vdc_joint_capture/lineage_v1"
ARCHIVED_PHASE = ARCHIVE / "phase_timer1.py"
ARCHIVED_LONGWINDOW = ARCHIVE / "longwindow.py"
ARCHIVED_TRACE = ARCHIVE / "native_trace.py"
FOLLOW_ENTRYPOINT = ARCHIVE / "local_follow.py"
ARCHIVED_FOLLOW = FOLLOW_ENTRYPOINT
OBSERVER_ENTRYPOINT = ARCHIVE / "observer_snapshot.py"
ARCHIVED_OBSERVER = OBSERVER_ENTRYPOINT
ARCHIVED_BASE = ARCHIVE / "base_probe.py"
ARCHIVED_MATCH = ARCHIVE / "exact_match.py"
ARCHIVED_DIRECT = ARCHIVE / "direct_rx.py"
ARCHIVED_TYPED = ARCHIVE / "typed_tx.py"
GOLDEN_REPORT = ARCHIVE / "golden_phase_report.json"
GOLDEN_NATIVE = ARCHIVE / "golden_native_report.json"
GOLDEN_PHASE_SHA256 = "1d4c9fa2a67b6ab8e2bd13fe30c7f7d8857e6db6364acdce9d0df7998555b6d5"
GOLDEN_NATIVE_SHA256 = "c6ad2b8ffb5814caaf76dc9106a6dbfce37b7e907943f798f6112fe6795ed051"
EXPECTED_U32 = (
    "schema enabled active state reason request session generation calls prepared attempted "
    "committed applied held rejected cancelled event_sequence carrier_sequence source_slot "
    "target_slot before_model after_model before_dco after_dco rate_epoch reserved"
).split()
EXPECTED_I64 = "delta_ns cumulative_ns residual_lo residual_hi".split()
EXPECTED_U64 = (
    "before_base_vdc after_base_vdc raw_lo raw_hi local_lo local_hi expected_lo expected_hi "
    "valid_from_raw"
).split()
EXPECTED_PHASES = [
    {
        "schema": 1, "enabled": 1, "active": 0, "state": 6,
        "reason": 12, "request": 8, "session": 1789915962,
        "generation": 1789915962, "calls": 12073, "prepared": 12088,
        "attempted": 605, "committed": 570, "applied": 570,
        "held": 50, "rejected": 0, "cancelled": 0,
        "event_sequence": 62348, "carrier_sequence": 62350,
        "source_slot": 0, "target_slot": 1, "before_model": 585,
        "after_model": 586, "before_dco": 585, "after_dco": 586,
        "rate_epoch": 16, "reserved": 0, "delta_ns": -8,
        "cumulative_ns": -606, "residual_lo": -78, "residual_hi": 94,
        "before_base_vdc": 127517475384, "after_base_vdc": 127517475376,
        "raw_lo": 39741371450, "raw_hi": 39741371463,
        "local_lo": 159053829787, "local_hi": 159053829839,
        "expected_lo": 159053829745, "expected_hi": 159053829865,
        "valid_from_raw": 39742282834,
    },
    {
        "schema": 1, "enabled": 1, "active": 0, "state": 6,
        "reason": 12, "request": 8, "session": 1789915962,
        "generation": 1789915962, "calls": 12066, "prepared": 12083,
        "attempted": 606, "committed": 571, "applied": 571,
        "held": 52, "rejected": 0, "cancelled": 0,
        "event_sequence": 62322, "carrier_sequence": 62328,
        "source_slot": 0, "target_slot": 2, "before_model": 589,
        "after_model": 0, "before_dco": 589, "after_dco": 0,
        "rate_epoch": 18, "reserved": 0, "delta_ns": 0,
        "cumulative_ns": 53, "residual_lo": -86, "residual_hi": 86,
        "before_base_vdc": 157961644891, "after_base_vdc": 157961644891,
        "raw_lo": 39712329026, "raw_hi": 39712329039,
        "local_lo": 159027821905, "local_hi": 159027821957,
        "expected_lo": 159027821871, "expected_hi": 159027821991,
        "valid_from_raw": 39688695978,
    },
    {
        "schema": 1, "enabled": 1, "active": 0, "state": 6,
        "reason": 12, "request": 8, "session": 1789915962,
        "generation": 1789915962, "calls": 11858, "prepared": 11870,
        "attempted": 604, "committed": 556, "applied": 556,
        "held": 60, "rejected": 0, "cancelled": 0,
        "event_sequence": 62384, "carrier_sequence": 62388,
        "source_slot": 0, "target_slot": 3, "before_model": 568,
        "after_model": 569, "before_dco": 568, "after_dco": 569,
        "rate_epoch": 13, "reserved": 0, "delta_ns": -7,
        "cumulative_ns": -243, "residual_lo": -79, "residual_hi": 93,
        "before_base_vdc": 151687565746, "after_base_vdc": 151687565739,
        "raw_lo": 39704676194, "raw_hi": 39704676207,
        "local_lo": 159089840967, "local_hi": 159089841019,
        "expected_lo": 159089840926, "expected_hi": 159089841046,
        "valid_from_raw": 39706095117,
    },
]


def encode_phase(*, slot=1, residual_lo=-91, residual_hi=81):
    words = [0] * 52
    words[0] = 1
    words[6] = words[7] = 77
    words[11] = words[12] = 3
    words[18] = 0
    words[19] = slot
    for position, value in ((26, -4), (28, -8),
                            (30, residual_lo), (32, residual_hi)):
        encoded = value & ((1 << 64) - 1)
        words[position] = encoded & 0xFFFFFFFF
        words[position + 1] = encoded >> 32
    for position, value in enumerate(range(100, 109), 34):
        words[position] = value
    return ",".join(map(str, words))


def fake_longwindow(tmp_path, *, fail_download_slot=None,
                    fail_endpoint_slot=None, fail_release_slot=None):
    source = tmp_path / "longwindow.py"
    source.write_text("# fake admitted long-window source\n", encoding="utf-8")
    events = []
    backend = SimpleNamespace(TDMA_CONTROL_RESULT_FIELDS={})
    scpi = SimpleNamespace(SCALAR_ONE_QUERY_HEADERS=set())
    base = SimpleNamespace(BACKEND=backend, sha=lambda path: hashlib.sha256(
        Path(path).read_bytes()).hexdigest())
    follow_parent = SimpleNamespace(
        base=base, scpi_serial=scpi, SYNC="SYSTem:VDC:PRIORity:SYNC",
        fields=lambda raw: raw.split(","))

    class FollowProbe:
        def control(self, board, text, expected="OK"):
            events.append((board.address, text, expected))
            slot = int(board.address.rsplit("-", 1)[1])
            if text.endswith("RELease") and slot == fail_release_slot:
                raise RuntimeError("release failed")
            return str(expected)

    class ObserverProbe:
        @staticmethod
        def collect(probe):
            for slot, board in enumerate(probe.boards[1:], 1):
                probe.report["boards"][board.address]["local_model_raw"] = \
                    f"1,9,77,{slot},5,-10,1000"

    def parse_follow(raw):
        slot = int(raw.rsplit("-", 1)[1])
        if slot == fail_endpoint_slot:
            raise RuntimeError("endpoint failed")
        return {"schema": 1, "mode": 4, "session": 77, "generation": 77,
                "local_slot": slot, "reference_slot": 0, "active": 0,
                "last_reason": 12, "applied": 1, "no_adjust": 0,
                "after_dco_seq": 5, "after_ppb": -10}

    follow = SimpleNamespace(
        FOLLOW="SYSTem:VDC:PRIORity:FOLLow",
        STATUS="SYSTem:VDC:PRIORity:FOLLow:STATus?",
        MODEL=["schema", "token", "session", "local_slot", "dco_update_seq",
               "period_adjust_ppb", "base_vdc_time64_ns"],
        parent=follow_parent,
        match=SimpleNamespace(direct=SimpleNamespace(
            numbers=lambda raw, count: [int(value) for value in raw.split(",")]
            if len(raw.split(",")) == count else (_ for _ in ()).throw(
                ValueError("field count")))),
        signed=lambda value, bits: value - (1 << bits)
        if value & (1 << (bits - 1)) else value,
        FollowProbe=FollowProbe,
        observer=SimpleNamespace(ObserverProbe=ObserverProbe),
        parse_follow=parse_follow,
    )

    class TraceProbe:
        def __init__(self, options):
            self.opt = options
            self.report = {"plan": {}, "tool_sources": [], "boards": {},
                           "binding_generation": 77, "errors": []}
            self.boards = []
            self.saved = 0

        def save(self):
            self.saved += 1

        def command(self, board, text):
            events.append((board.address, text, None))
            slot = self.boards.index(board)
            if text == follow.STATUS:
                return f"follow-{slot}"
            if text == follow.FOLLOW + ":PHASe:STATus?":
                return encode_phase(slot=slot)
            if text == "__trace_uid__":
                return board.address
            return "1"

        def trace_status(self, board):
            return {"capture_id": 1}

        def trace_ack(self, board, *args):
            index = self.boards.index(board)
            return {"status": {"schema": 5 if index == 0 else 6,
                               "record_count": 0, "session": 77,
                               "generation": 77}}

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": str(exc)})

    class LongWindowProbe:
        def acquire(self):
            return "long-window-acquire"

    def download_capture(command, capture_id):
        uid = command("__trace_uid__")
        if int(uid.rsplit("-", 1)[1]) == fail_download_slot:
            raise RuntimeError("download failed")
        return uid.encode("ascii"), [{"capture_id": capture_id}]

    def decode(data, capture_id):
        slot = int(data.decode("ascii").rsplit("-", 1)[1])
        records = ([{"kind": 0, "index": index} for index in range(8)]
                   if slot == 0 else [
                       {"kind": 1, "index": 0, "residual_lo": -91,
                        "residual_hi": 81},
                       {"kind": 4, "index": 1, "delta_ns": -4},
                       {"kind": 1, "index": 2, "residual_lo": -90,
                        "residual_hi": 82},
                       {"kind": 2, "index": 3},
                   ])
        return {"file_crc32": 123, "records": records,
                "replay_matches_encoded": slot == 0,
                "status": {"schema": 5 if slot == 0 else 6,
                           "session": 77, "generation": 77,
                           "local_slot": slot, "reason": 1,
                           "dropped_count": 0}}

    parent = SimpleNamespace(
        TraceProbe=TraceProbe, follow=follow,
        trace=SimpleNamespace(download_capture=download_capture, decode=decode),
        PREFIX="SYSTem:VDC:PRIORity:TRACe:")
    return SimpleNamespace(
        __file__=str(source), LongWindowProbe=LongWindowProbe, parent=parent), events


def test_frozen_handoff_reports_have_expected_source_hashes():
    assert hashlib.sha256(GOLDEN_REPORT.read_bytes()).hexdigest() == \
        GOLDEN_PHASE_SHA256
    assert hashlib.sha256(GOLDEN_NATIVE.read_bytes()).hexdigest() == \
        GOLDEN_NATIVE_SHA256


def test_phase_parser_matches_frozen_four_board_endpoints():
    tracked = tracked_phase.load_phase(None)
    assert tracked.U32 == EXPECTED_U32
    assert tracked.I64 == EXPECTED_I64
    assert tracked.U64 == EXPECTED_U64
    report = json.loads(GOLDEN_REPORT.read_text(encoding="utf-8"))
    rows = [row["final_phase_raw"] for row in report["boards"].values()
            if row["final_phase_raw"].split(",", 1)[0] == "1"]
    assert len(rows) == 3
    assert [tracked.parse_phase(raw) for raw in rows] == EXPECTED_PHASES
    synthetic = tracked.parse_phase(encode_phase())
    assert (synthetic["target_slot"], synthetic["residual_lo"],
            synthetic["residual_hi"]) == (1, -91, 81)


@pytest.mark.parametrize("raw", ["", "1,2", ",".join(["x"] * 52)])
def test_phase_parser_rejects_malformed_closed_set(raw):
    tracked = tracked_phase.load_phase(None)
    with pytest.raises((ValueError, TypeError, AssertionError, RuntimeError)):
        tracked.parse_phase(raw)


def test_phase_factory_owns_plan_provenance_and_arm_sequence(tmp_path):
    previous, events = fake_longwindow(tmp_path)
    tracked = tracked_phase.load_phase(Path("unused.py"), runtime_module=previous)
    probe = tracked.PhaseProbe(SimpleNamespace(out=tmp_path))
    boards = [SimpleNamespace(address=f"uid-{index}") for index in range(4)]
    probe.boards = boards
    probe.report["boards"] = {board.address: {} for board in boards}

    assert probe.report["schema"] == "DPLL_LOCAL_PHASE_NATIVE_PREFIX_V1"
    assert probe.report["plan"]["timebase"] == "TIMER1_NS"
    assert probe.report["plan"]["phase_enabled"] is True
    assert probe.acquire() == "long-window-acquire"
    assert probe.report["tool_sources"] == [
        {"path": str(tracked.source_path), "sha256": tracked.source_sha256},
        {"path": str(Path(previous.__file__).resolve()),
         "sha256": hashlib.sha256(Path(previous.__file__).read_bytes()).hexdigest()},
    ]
    assert tracked.PHASE.upper() in previous.parent.follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS
    assert tracked.PHASE.upper() + "?" in \
        previous.parent.follow.parent.scpi_serial.SCALAR_ONE_QUERY_HEADERS

    probe.control(boards[0], previous.parent.follow.parent.SYNC + " 1", 1)
    controls = [(uid, text, expected) for uid, text, expected in events
                if expected is not None]
    assert controls[0] == (boards[0].address,
                           previous.parent.follow.parent.SYNC + " 1", 1)
    assert any(text == tracked.PHASE + " 1" for _, text, _ in controls)
    assert [probe.report["boards"][board.address]["trace_arm_ack"]["status"]["schema"]
            for board in boards] == [5, 6, 6, 6]
    assert all(probe.report["boards"][board.address]["trace_arm_requested"]
               for board in boards)


def test_phase_factory_rejects_incomplete_runtime_module():
    with pytest.raises(ValueError, match="LongWindowProbe/parent"):
        tracked_phase.load_phase(Path("unused.py"), runtime_module=SimpleNamespace())


def test_phase_collects_native_records_checks_endpoints_and_releases(tmp_path):
    previous, events = fake_longwindow(tmp_path)
    tracked = tracked_phase.load_phase(Path("unused.py"), runtime_module=previous)
    probe = tracked.PhaseProbe(SimpleNamespace(out=tmp_path))
    probe.boards = [SimpleNamespace(address=f"uid-{index}") for index in range(4)]
    probe.report["boards"] = {
        board.address: {"trace_capture_id": 77, "trace_arm_requested": True}
        for board in probe.boards}
    probe.barrier = True
    probe.block_queries = False
    probe.collect()

    assert not probe.report["errors"] and probe.report["passed"] is True
    assert all(probe.report["boards"][board.address]["trace_read_complete"]
               for board in probe.boards)
    assert all(probe.report["boards"][board.address]["trace_release_ack"]
               for board in probe.boards)
    assert all(probe.report["boards"][board.address][
        "phase_frequency_endpoint_consistent"] for board in probe.boards[1:])
    assert all((tmp_path / f"{board.address}-native.ram.bin").is_file()
               for board in probe.boards)
    assert len([event for event in events
                if event[1] == tracked.PREFIX + "RELease"]) == 4


def test_phase_collect_retains_failed_trace_and_releases_completed_ones(tmp_path):
    previous, events = fake_longwindow(tmp_path, fail_download_slot=2)
    tracked = tracked_phase.load_phase(Path("unused.py"), runtime_module=previous)
    probe = tracked.PhaseProbe(SimpleNamespace(out=tmp_path))
    probe.boards = [SimpleNamespace(address=f"uid-{index}") for index in range(4)]
    probe.report["boards"] = {
        board.address: {"trace_capture_id": 77, "trace_arm_requested": True}
        for board in probe.boards}
    probe.barrier = True
    probe.block_queries = False
    probe.collect()

    failed = probe.report["boards"]["uid-2"]
    assert failed["trace_retained_for_recovery"] is True
    assert "trace_release_ack" not in failed
    assert any(row["scope"] == "phase-native:uid-2"
               for row in probe.report["errors"])
    released = [event[0] for event in events
                if event[1] == tracked.PREFIX + "RELease"]
    assert released == ["uid-0", "uid-1", "uid-3"]


def test_phase_endpoint_failure_still_releases_every_completed_trace(tmp_path):
    previous, events = fake_longwindow(tmp_path, fail_endpoint_slot=2)
    tracked = tracked_phase.load_phase(Path("unused.py"), runtime_module=previous)
    probe = tracked.PhaseProbe(SimpleNamespace(out=tmp_path))
    probe.boards = [SimpleNamespace(address=f"uid-{index}") for index in range(4)]
    probe.report["boards"] = {
        board.address: {"trace_capture_id": 77, "trace_arm_requested": True}
        for board in probe.boards}
    probe.barrier = True
    probe.block_queries = False
    probe.collect()

    assert any(row["scope"] == "phase-endpoint:uid-2"
               for row in probe.report["errors"])
    assert probe.report["passed"] is False
    assert len([event for event in events
                if event[1] == tracked.PREFIX + "RELease"]) == 4
    assert all("trace_release_ack" in probe.report["boards"][board.address]
               for board in probe.boards)


def test_phase_release_failure_is_recorded_and_fails_verdict(tmp_path):
    previous, events = fake_longwindow(tmp_path, fail_release_slot=2)
    tracked = tracked_phase.load_phase(Path("unused.py"), runtime_module=previous)
    probe = tracked.PhaseProbe(SimpleNamespace(out=tmp_path))
    probe.boards = [SimpleNamespace(address=f"uid-{index}") for index in range(4)]
    probe.report["boards"] = {
        board.address: {"trace_capture_id": 77, "trace_arm_requested": True}
        for board in probe.boards}
    probe.barrier = True
    probe.block_queries = False
    probe.collect()

    assert any(row["scope"] == "phase-release:uid-2"
               for row in probe.report["errors"])
    assert probe.report["passed"] is False
    assert "trace_release_ack" not in probe.report["boards"]["uid-2"]
    assert all("trace_release_ack" in probe.report["boards"][f"uid-{slot}"]
               for slot in (0, 1, 3))
    assert len([event for event in events
                if event[1] == tracked.PREFIX + "RELease"]) == 4


def test_repository_runtime_closure_is_exact_in_fresh_process():
    code = r'''
import json
import sys
from pathlib import Path
from types import ModuleType, SimpleNamespace
from tools.workflows.vdc_joint_capture.inputs import load_inputs
from tools.workflows.vdc_joint_capture.internal_seal import load_adapter
root = Path('.').resolve()
inputs = load_inputs(
    root / 'config/tool_workflows/vdc_joint_capture_internal_seal.json', root,
    root / 'tools/archive/vdc/vdc_joint_capture/lineage_v1/internal_seal.py')
adapter = load_adapter(inputs.runtime_entrypoint, board_uids=inputs.board_uids)
seen, actual, loaded = set(), set(), set()
stack = [adapter, *tuple(sys.modules.values())]
while stack:
    value = stack.pop()
    identity = id(value)
    if identity in seen:
        continue
    seen.add(identity)
    path = getattr(value, '__file__', None)
    if path:
        try:
            relative = Path(path).resolve().relative_to(root).as_posix()
        except ValueError:
            relative = ''
        if relative:
            loaded.add(relative)
        if relative.startswith('out/HardwareAcceptance/'):
            actual.add(relative)
    if isinstance(value, (ModuleType, SimpleNamespace)):
        stack.extend(child for child in vars(value).values()
                     if isinstance(child, (ModuleType, SimpleNamespace)))
declared = {path.relative_to(root).as_posix()
            for path in inputs.executable_closure}
migrated = {path.relative_to(root).as_posix()
            for path in inputs.migrated_sources}
print(json.dumps({
    'actual': sorted(actual), 'declared': sorted(declared),
    'missing': sorted(actual - declared),
    'declared_not_loaded': sorted(declared - actual),
    'migrated_loaded': sorted(loaded & migrated),
}))
'''
    completed = subprocess.run(
        [sys.executable, "-c", code], cwd=ROOT,
        check=True, capture_output=True, text=True)
    result = json.loads(completed.stdout)
    assert len(result["actual"]) == len(result["declared"]) == 0
    assert not result["missing"]
    assert not result["declared_not_loaded"]
    assert not result["migrated_loaded"]
    assert ARCHIVED_PHASE.relative_to(ROOT).as_posix() not in result["actual"]
    assert ARCHIVED_LONGWINDOW.relative_to(ROOT).as_posix() not in result["actual"]
    assert ARCHIVED_TRACE.relative_to(ROOT).as_posix() not in result["actual"]
    assert ARCHIVED_FOLLOW.relative_to(ROOT).as_posix() not in result["actual"]
    assert ARCHIVED_OBSERVER.relative_to(ROOT).as_posix() not in result["actual"]
    assert ARCHIVED_MATCH.relative_to(ROOT).as_posix() not in result["actual"]
    assert ARCHIVED_DIRECT.relative_to(ROOT).as_posix() not in result["actual"]
    assert ARCHIVED_TYPED.relative_to(ROOT).as_posix() not in result["actual"]
    assert ARCHIVED_BASE.relative_to(ROOT).as_posix() not in result["actual"]


def test_repository_adapter_constructs_final_probe_in_fresh_process():
    code = r'''
import os
from pathlib import Path
from types import SimpleNamespace
from tools.workflows.vdc_joint_capture.inputs import load_inputs
from tools.workflows.vdc_joint_capture.internal_seal import load_adapter
from tools.vdc_priority_trace.vdc_priority_joint_capture import make_probe_class
root = Path('.').resolve()
inputs = load_inputs(
    root / 'config/tool_workflows/vdc_joint_capture_internal_seal.json', root,
    root / 'tools/archive/vdc/vdc_joint_capture/lineage_v1/internal_seal.py')
adapter = load_adapter(inputs.runtime_entrypoint, board_uids=inputs.board_uids)
options = SimpleNamespace(
    out=root / f'out/pytest/vdc-typed-construction-probe-{os.getpid()}',
    seconds=60, period_ns=1000000,
    summary_interval_ms=0, no_reference=False, expire_output=False,
    scope=False, external_scope=True, workflow_inputs_evidence=inputs.evidence(root),
    baseline=inputs.baseline, tap_baseline=inputs.tap_baseline,
    receipt=inputs.receipt, check_only=True)
probe = make_probe_class(adapter)(options)
assert probe.report['tool_sources']
typed = adapter.base.follow.match.parent
assert typed.scpi_serial is __import__('sys').modules[
    typed.base.BACKEND.SerialSession.__module__]
'''
    completed = subprocess.run(
        [sys.executable, "-c", code], cwd=ROOT,
        capture_output=True, text=True)
    assert completed.returncode == 0, completed.stdout + completed.stderr
