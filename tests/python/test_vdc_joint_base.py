"""Regression checks for the fully tracked VDC joint base probe."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import time
from types import MethodType, SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import base as tracked_base
from tools.workflows.vdc_joint_capture.inputs import load_inputs


ROOT = Path(__file__).resolve().parents[2]


def options(tmp_path, **overrides):
    values = dict(
        out=tmp_path / "run", check_only=True,
        p3=tmp_path / "receipt.json")
    values.update(overrides)
    return SimpleNamespace(**values)


def boards():
    return [SimpleNamespace(address=uid, port=f"COM{index}")
            for index, uid in enumerate(tracked_base.IDS)]


class Connection:
    def __init__(self):
        self.session = object()


def install_connections(monkeypatch):
    values = {uid: Connection() for uid in tracked_base.IDS}
    monkeypatch.setattr(tracked_base.BACKEND, "_PERSISTENT_CONNECTIONS", values)
    return values


def test_load_base_has_tracked_provenance_and_no_out_runtime(tmp_path):
    module = tracked_base.load_base()
    probe = module.Probe(options(tmp_path))
    sources = [Path(row["path"]).resolve() for row in probe.report["tool_sources"]]
    assert module.source_path.resolve() in sources
    assert Path(tracked_base.tracked_probe_support.__file__).resolve() in sources
    assert not any(path.is_relative_to(ROOT / "out") for path in sources)
    source = module.source_path.read_text(encoding="utf-8")
    assert "sys.path.insert" not in source
    assert module.BACKEND is tracked_base.sys.modules[
        tracked_base.trn.board_command.__module__]


def test_repository_manifest_has_zero_runtime_closure():
    manifest = ROOT / "config/tool_workflows/vdc_joint_capture_internal_seal.json"
    adapter = (ROOT / "tools/archive/vdc/vdc_joint_capture/lineage_v1/"
               "internal_seal.py")
    inputs = load_inputs(manifest, ROOT, adapter)
    assert inputs.runtime_entrypoint is None
    assert inputs.executable_closure == ()
    assert len(inputs.migrated_sources) == 20
    archive = ROOT / "tools/archive/vdc/vdc_joint_capture/lineage_v1"
    assert all(path.is_relative_to(archive) for path in inputs.migrated_sources)
    assert inputs.baseline == archive / "stopped_baseline.json"
    assert inputs.tap_baseline == archive / "tap_baseline.json"


def test_base_import_has_no_device_side_effects_in_fresh_process():
    code = r'''
import serial
from serial.tools import list_ports
list_ports.comports = lambda: (_ for _ in ()).throw(AssertionError('enumerated'))
class ForbiddenSerial:
    def __init__(self, *args, **kwargs):
        raise AssertionError('opened serial')
serial.Serial = ForbiddenSerial
from tools.workflows.vdc_joint_capture import base
assert base.BACKEND._PERSISTENT_CONNECTIONS == {}
assert 'sys.path.insert' not in base.Path(base.__file__).read_text(encoding='utf-8')
'''
    completed = subprocess.run(
        [sys.executable, "-c", code], cwd=ROOT,
        capture_output=True, text=True)
    assert completed.returncode == 0, completed.stdout + completed.stderr


def test_command_enforces_allowlist_query_barrier_and_persists_actions(
        tmp_path, monkeypatch):
    install_connections(monkeypatch)
    probe = tracked_base.Probe(options(tmp_path))
    board = boards()[0]
    calls = []
    probe.original_command = lambda value, text, args: (
        calls.append((value.address, text)) or "OK")

    with pytest.raises(RuntimeError, match="Query forbidden"):
        probe.command(board, "SYSTem:TEST?")
    assert probe.report["queries_during_acquisition"] == 1
    probe.block_queries = False
    assert probe.command(board, "SYSTem:SET 1") == "OK"
    probe.deadline = time.monotonic() - 1
    with pytest.raises(TimeoutError, match="budget"):
        probe.command(board, "SYSTem:SET 2")
    assert probe.command(board, tracked_base.STOP) == "OK"
    outsider = SimpleNamespace(address="not-allowed", port="COMX")
    with pytest.raises(RuntimeError, match="allowlist"):
        probe.command(outsider, tracked_base.STOP)

    rows = [json.loads(line) for line in (
        probe.opt.out / "actions.jsonl").read_text(encoding="utf-8").splitlines()]
    assert len(rows) == 5
    assert rows[0]["command"] == "SYSTem:TEST?" and "error" in rows[0]
    assert rows[3]["raw"] == "OK"


def test_command_rejects_missing_session_and_unbounded_file_read(
        tmp_path, monkeypatch):
    connections = install_connections(monkeypatch)
    probe = tracked_base.Probe(options(tmp_path))
    probe.block_queries = False
    board = boards()[0]
    connections[board.address].session = None
    with pytest.raises(RuntimeError, match="Persistent session missing"):
        probe.command(board, "SYSTem:TEST")
    connections[board.address].session = object()
    with pytest.raises(RuntimeError, match="64 KiB"):
        probe.command(board, 'MMEMory:FILE:READ? "/x",65536,16')


@pytest.mark.parametrize("response", [
    "OK(no payload; verified by state readback)",
    '"OK(no payload; verified by state readback)"'])
def test_control_accepts_explicit_synthetic_timeout_marker(
        tmp_path, response):
    probe = tracked_base.Probe(options(tmp_path))
    probe.command = lambda board, text: response
    assert probe.control(boards()[0], "COMMAND") == response


def test_control_rejects_wrong_ack(tmp_path):
    probe = tracked_base.Probe(options(tmp_path))
    probe.command = lambda board, text: "WRONG"
    with pytest.raises(RuntimeError, match="ACK mismatch"):
        probe.control(boards()[0], "COMMAND")


def test_stop_all_retries_only_failed_board_and_publishes_barrier(
        tmp_path, monkeypatch):
    probe = tracked_base.Probe(options(tmp_path))
    probe.boards = boards()
    probe.report["boards"] = {board.address: {} for board in probe.boards}
    attempts = {board.address: 0 for board in probe.boards}

    def control(self, board, text, expected="OK"):
        attempts[board.address] += 1
        if board == self.boards[1] and attempts[board.address] == 1:
            raise RuntimeError("retry")
        return "OK"

    probe.control = MethodType(control, probe)
    monkeypatch.setattr(
        tracked_base.trn, "wait_runtime_stopped",
        lambda board, args, index: {"uid": board.address, "slot": index})
    probe.stop_all("final", passes=2)
    assert attempts == {
        probe.boards[0].address: 1,
        probe.boards[1].address: 2,
        probe.boards[2].address: 1,
        probe.boards[3].address: 1,
    }
    assert probe.barrier is True and probe.block_queries is False
    assert all("final" in probe.report["boards"][board.address]
               for board in probe.boards)
    assert probe.report["errors"][0]["scope"].startswith(
        "final:stop:" + probe.boards[1].address)


def test_stop_all_incomplete_keeps_queries_blocked(tmp_path):
    probe = tracked_base.Probe(options(tmp_path))
    probe.boards = boards()[:3]
    probe.report["boards"] = {board.address: {} for board in probe.boards}
    probe.control = lambda board, text, expected="OK": "OK"
    with pytest.raises(RuntimeError, match="STOP ACK barrier incomplete"):
        probe.stop_all("final")
    assert probe.barrier is False and probe.block_queries is True


def test_set_load_mask_requires_stop_and_exact_readback(tmp_path):
    probe = tracked_base.Probe(options(tmp_path))
    board = boards()[0]
    probe.barrier = False
    with pytest.raises(RuntimeError, match="stopped"):
        probe.set_load_mask(board, 7)
    probe.barrier = True
    responses = iter(('"OK",7', "7"))
    probe.command = lambda value, text: next(responses)
    probe.set_load_mask(board, 7)


def test_pipeline_uses_tracked_decoders(tmp_path, monkeypatch):
    probe = tracked_base.Probe(options(tmp_path))
    board = boards()[0]
    probe.report["boards"] = {board.address: {}}
    probe.command = lambda value, text: text
    monkeypatch.setattr(
        tracked_base, "uints", lambda raw, names: {"raw": raw, "count": len(names)})
    monkeypatch.setattr(
        tracked_base.capture, "parse_role_status", lambda raw: {"role": raw})
    monkeypatch.setattr(
        tracked_base.capture, "parse_refmem_vdc_follower_rx",
        lambda raw: {"rx": raw})
    value = probe.pipeline(board, "baseline")
    assert value["observer"]["raw"] == tracked_base.PIPE_QUERIES["observer"]
    assert value["role"] == {"role": tracked_base.PIPE_QUERIES["role"]}
    assert value["rx"] == {"rx": tracked_base.PIPE_QUERIES["rx"]}
    assert probe.report["boards"][board.address]["baseline"] == value


def test_trace_control_and_record_poll_are_bounded(tmp_path, monkeypatch):
    probe = tracked_base.Probe(options(tmp_path))
    board = boards()[0]
    responses = iter(("OK,3,4", "record", "record"))
    probe.command = lambda value, text: next(responses)
    assert probe.trace_control(board, "STOP") == [3, 4]
    states = iter(({"state": 2}, {"state": 5, "epoch": 7}))
    monkeypatch.setattr(tracked_base, "record_status", lambda raw: next(states))
    monkeypatch.setattr(tracked_base.time, "sleep", lambda seconds: None)
    assert probe.poll_record(board, {5}, seconds=1)["epoch"] == 7


def test_export_tdma_preserves_cancel_save_and_sd_readback_order(
        tmp_path, monkeypatch):
    probe = tracked_base.Probe(options(tmp_path))
    board = boards()[0]
    probe.boards = [board]
    probe.report["boards"] = {board.address: {}}
    probe.barrier = True
    probe.args.expected_build = 123
    events = []
    statuses = iter(({"state": 5, "epoch": 9}, {"state": 8, "epoch": 9}))
    probe.poll_record = lambda value, states, seconds=3: next(statuses)
    probe.control = lambda value, text, expected="OK": (
        events.append(("control", text)) or "OK")

    from tools.calibration_ring_validate import tdma_board_record

    def export_frozen(board, args, command, *, epoch, path):
        path.write_bytes(b"native")
        events.append(("ram", epoch))
        return {"path": str(path)}

    monkeypatch.setattr(tdma_board_record, "export_frozen", export_frozen)
    monkeypatch.setattr(
        tracked_base.capture, "download",
        lambda board, path, args: (events.append(("download", path)) or b"native"))
    monkeypatch.setattr(
        tracked_base, "decode_record",
        lambda data, **kwargs: {"samples": [], "terminal": {"state": "STOP"}})
    monkeypatch.setattr(
        tracked_base, "native_input_summary", lambda decoded: {"valid": True})
    probe.export_tdma(board, "run", expected_epoch=9)
    row = probe.report["boards"][board.address]["run_tdma"]
    assert events == [
        ("control", "SYSTem:TDMA:RECord:CANCel"),
        ("ram", 9),
        ("control", "SYSTem:TDMA:RECord:SAVE"),
        ("download", "/logs/tdma_9.bin"),
    ]
    assert row["saved_status"]["state"] == 8
    assert row["bytes"] == len(b"native")


def test_export_trace_validates_frozen_count_and_decode(tmp_path, monkeypatch):
    probe = tracked_base.Probe(options(tmp_path))
    board = boards()[0]
    probe.boards = boards()
    probe.report["boards"] = {board.address: {}}
    probe.barrier = True
    probe.command = lambda value, text: {
        tracked_base.TRACE + "STATus?": "status",
        tracked_base.TRACE + "SAVE": "save",
    }[text]
    monkeypatch.setattr(
        tracked_base, "uints",
        lambda raw, names: {"armed": 0, "sample_count": 2})
    monkeypatch.setattr(
        tracked_base.capture, "parse_save", lambda raw: (7, "/trace.bin", 2))
    monkeypatch.setattr(
        tracked_base.capture, "wait_job", lambda board, job, args: {"done": True})
    monkeypatch.setattr(
        tracked_base.capture, "download", lambda board, path, args: b"trace")
    monkeypatch.setattr(
        tracked_base.capture, "decode", lambda path, name: {"name": name})
    probe.export_trace(board, "run")
    row = probe.report["boards"][board.address]["run_trace"]
    assert row["sample_count"] == 2 and row["bytes"] == 5
    assert row["decoded"] == {"name": "NO1"}


def test_base_configure_acquire_and_collect_boundaries(tmp_path, monkeypatch):
    probe = tracked_base.Probe(options(tmp_path))
    probe.boards = boards()
    probe.report["boards"] = {board.address: {} for board in probe.boards}
    probe.receipt = {"calibration_generation": 66}
    events = []
    masks = {board.address: 8 for board in probe.boards}

    def command(board, text):
        events.append(("command", board.address, text))
        if text == "SYSTem:TDMA:SCHEDule?":
            return "1,250000000,0,0,11,0"
        if text == "SYSTem:TDMA:LOAD:MASK?":
            return str(masks[board.address])
        if text == "SYSTem:TDMA:RING:DIAGnostic?":
            return "0"
        if text == "SYSTem:TDMA:RING:BURSt?":
            return "1"
        if text.startswith("SYSTem:TDMA:FLIGHT:TX "):
            _, raw = text.split(" ", 1)
            _, seed, generation, slot, enabled = map(int, raw.split(","))
            checksum = sum((seed + index) & 255 for index in range(32))
            return f"OK,32,{checksum},{seed},{generation},{slot},{enabled}"
        if text.startswith("CALibration:ORIGin:TRIAL "):
            return "200"
        if text.startswith(tracked_base.TRACE):
            return "OK,1,2"
        if text.startswith("READ:CALibration:ORIGin"):
            return "origin"
        raise AssertionError(text)

    probe.command = command
    probe.control = lambda board, text, expected="OK": (
        events.append(("control", board.address, text)) or str(expected))
    probe.set_load_mask = lambda board, mask: (
        masks.__setitem__(board.address, mask),
        events.append(("load", board.address, mask)))
    monkeypatch.setattr(tracked_base.trn, "flight_snapshot", lambda board, args: {})
    monkeypatch.setattr(tracked_base.trn, "validate_fifo_reset", lambda value: False)
    monkeypatch.setattr(
        tracked_base.trn, "arm_with_evidence",
        lambda board, args, slot: events.append(("arm", board.address, slot)) or {})
    monkeypatch.setattr(
        tracked_base.trn, "wait_runtime_config_applied",
        lambda board, args, slot: events.append(("ready", board.address, slot)) or {})
    probe.configure()
    assert [event[1] for event in events if event[0] == "arm"] == list(
        reversed(tracked_base.IDS))
    assert probe.report["clock_hz"] == 250_000_000

    probe.poll_record = lambda board, states, seconds=3: {"state": 3, "epoch": 100}
    probe.pipeline = lambda board, label: events.append(
        ("pipeline", board.address, label)) or {}
    probe.trace_control = lambda board, action: (
        probe.report["actions"].append({"board": board.address, "action": action}) or
        [1, 2])
    monkeypatch.setattr(tracked_base.time, "time", lambda: 100)
    monkeypatch.setattr(tracked_base.time, "sleep", lambda seconds: None)
    probe.acquire()
    starts = [event[1] for event in events
              if event[0] == "control" and event[2] == "SYSTem:TDMA:RING:START"]
    assert starts == list(reversed(tracked_base.IDS))
    assert probe.record_armed == probe.trace_armed == set(tracked_base.IDS)

    probe.barrier = True
    probe.block_queries = False
    probe.pipeline = lambda board, label: {}
    exported = []
    probe.export_tdma = lambda board, label, epoch=None: (
        exported.append(("tdma", board.address, epoch)))
    probe.export_trace = lambda board, label: (
        exported.append(("trace", board.address)))
    probe.collect()
    assert len([row for row in exported if row[0] == "tdma"]) == 4
    assert len([row for row in exported if row[0] == "trace"]) == 4


def lifecycle_probe(tmp_path, monkeypatch, *, configure_failure=False,
                    collect_failure=False, restore_failure_uid=None,
                    close_failure_uid=None):
    events = []

    class Session:
        def __init__(self, port, *args, **kwargs):
            self.port = port

        def open(self):
            events.append(("open", self.port))

        def close(self):
            events.append(("close", self.port))
            if self.port == close_failure_uid:
                raise RuntimeError("injected close failure")

    class BoardConnection:
        def __init__(self, board, args):
            self.board = board
            self.session = None

        def close(self):
            if self.session is not None:
                self.session.close()

    monkeypatch.setattr(tracked_base.BACKEND, "BoardConnection", BoardConnection)
    monkeypatch.setattr(tracked_base.BACKEND, "SerialSession", Session)
    monkeypatch.setattr(tracked_base.BACKEND, "_PERSISTENT_CONNECTIONS", {})
    monkeypatch.setattr(
        tracked_base, "comports",
        lambda: [SimpleNamespace(serial_number=uid, device=uid)
                 for uid in tracked_base.IDS])
    monkeypatch.setattr(
        tracked_base.trn, "Board",
        lambda port, uid, idn, build: SimpleNamespace(
            port=port, address=uid, idn=idn, build=str(build)))
    monkeypatch.setattr(
        tracked_base, "bind_receipt",
        lambda root, path, fingerprint, validator: ({
            "build_id": "123", "calibration_generation": 1}, {"bound": True}))
    probe = tracked_base.Probe(options(tmp_path, check_only=False))
    original_board = tracked_base.trn.board_command
    stage_owner = tracked_base.sys.modules["trn03_stage"]
    original_stage = stage_owner.board_command
    original_capture = tracked_base.capture.query

    def command(board, text, unused=None):
        events.append(("command", board.address, text))
        if text == "*IDN?":
            return board.address
        if text == "SYSTem:FW:BUILD?":
            return '"123"'
        if text == "SYSTem:BOARD:NO?":
            return str(probe.boards.index(board) + 1)
        return "OK"

    def configure():
        events.append(("configure",))
        for board in probe.boards:
            probe.report["boards"][board.address].update(
                original_diagnostic=0, original_load_mask=8,
                load_mask_change_requested=True)
        probe.report["boards"][probe.boards[0].address]["original_burst"] = 1
        if configure_failure:
            raise RuntimeError("injected configure failure")

    def collect():
        events.append(("collect",))
        if collect_failure:
            raise RuntimeError("injected collect failure")
        for board in probe.boards:
            probe.report["boards"][board.address]["run_tdma"] = {}
            probe.report["boards"][board.address]["run_trace"] = {}

    probe.command = command
    probe.stop_all = lambda label, passes=1: (
        events.append(("stop", label, passes)),
        setattr(probe, "barrier", True),
        setattr(probe, "block_queries", False))
    probe.trace_control = lambda board, action: events.append(
        ("trace", board.address, action)) or [1, 2]
    probe.export_tdma = lambda board, label, expected_epoch=None: events.append(
        ("export_tdma", board.address, label))
    probe.export_trace = lambda board, label: events.append(
        ("export_trace", board.address, label))
    probe.configure = configure
    probe.acquire = lambda: events.append(("acquire",))
    probe.collect = collect

    def control(board, text, expected="OK"):
        events.append(("control", board.address, text))
        if (board.address == restore_failure_uid and
                "DIAGnostic" in text):
            raise RuntimeError("injected restore failure")
        return str(expected)

    probe.control = control
    probe.set_load_mask = lambda board, mask: events.append(
        ("load", board.address, mask))
    probe.run()
    return (probe, events, original_board, original_stage, original_capture)


def assert_run_restored(result):
    probe, events, original_board, original_stage, original_capture = result
    assert tracked_base.trn.board_command is original_board
    assert tracked_base.sys.modules["trn03_stage"].board_command is original_stage
    assert tracked_base.capture.query is original_capture
    assert tracked_base.BACKEND._PERSISTENT_CONNECTIONS == {}
    assert len([event for event in events if event[0] == "close"]) == 4
    assert any(event[:2] == ("stop", "final_stop") for event in events)
    assert any(event[0] == "control" and
               event[2] == "CALibration:ORIGin:REVOKe" for event in events)
    return probe, events


def test_base_run_success_restores_three_owners(tmp_path, monkeypatch):
    probe, events = assert_run_restored(lifecycle_probe(tmp_path, monkeypatch))
    assert probe.report["passed"] is True
    assert probe.report["flow_completed"] is True
    assert ("configure",) in events and ("acquire",) in events


@pytest.mark.parametrize("fault", ["configure", "collect", "restore", "close"])
def test_base_run_faults_still_restore_and_close_all(
        tmp_path, monkeypatch, fault):
    result = lifecycle_probe(
        tmp_path, monkeypatch,
        configure_failure=fault == "configure",
        collect_failure=fault == "collect",
        restore_failure_uid=tracked_base.IDS[1] if fault == "restore" else None,
        close_failure_uid=tracked_base.IDS[1] if fault == "close" else None)
    probe, events = assert_run_restored(result)
    assert probe.report["passed"] is False
    scopes = [row["scope"] for row in probe.report["errors"]]
    expected = {
        "configure": "run",
        "collect": "final_collect",
        "restore": "restore:" + tracked_base.IDS[1] + ":diagnostic",
        "close": "close",
    }[fault]
    assert expected in scopes
    if fault == "restore":
        assert any(event[0] == "control" and
                   event[1] == tracked_base.IDS[2] and
                   "DIAGnostic" in event[2] for event in events)
