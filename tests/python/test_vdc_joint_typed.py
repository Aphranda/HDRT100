"""Regression checks for the tracked VDC joint typed-TX lifecycle."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from types import MethodType, SimpleNamespace

import pytest

from tools.domains.vdc.priority_trace import probe_support
from tools.workflows.vdc_joint_capture import typed as tracked_typed


ROOT = Path(__file__).resolve().parents[2]
ARCHIVED = (ROOT / "out/HardwareAcceptance/20260917/"
            "dpll-priority-tx-r1/capture_typed_tx.py")
GOLDEN = (ROOT / "out/HardwareAcceptance/20260917/"
          "dpll-longwindow-adoption-r1/native-r1/input-probe.json")
UIDS = ("uid-0", "uid-1", "uid-2", "uid-3")


def archived_module():
    spec = importlib.util.spec_from_file_location(
        "archived_typed_for_test", ARCHIVED)
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


def mailbox(*, generation=77, sequence=101, lower_ns=1_000,
            width_ns=10, flags=0, mask=0x0E):
    wire = bytearray(32)
    wire[3] = 0x14
    wire[5] = mask
    struct.pack_into("<H", wire, 6, sequence & 0xFFFF)
    struct.pack_into("<IIQIH", wire, 8, generation, sequence,
                     lower_ns, width_ns, flags)
    struct.pack_into("<H", wire, 30, tracked_typed.crc16(wire[:30]))
    return wire.hex()


def fake_base(tmp_path):
    source = tmp_path / "base.py"
    source.write_text("# fake base\n", encoding="utf-8")
    events = []
    class SerialSession:
        def __init__(self, port, *args, **kwargs):
            self.port = port
            self.ser = None

        def open(self):
            events.append(("open", self.port))
            self.ser = object()
            return self

        def close(self):
            events.append(("session_close", self.port))
            self.ser = None
            if self.port == backend.fail_close_port:
                raise RuntimeError("injected close failure")

    class BoardConnection:
        def __init__(self, board, args):
            self.board = board
            self.args = args
            self.session = None

        def close(self):
            if self.session is not None:
                self.session.close()
                self.session = None

    backend = SimpleNamespace(
        COMPOSITE_ACK_HEADERS=set(), TDMA_CONTROL_RESULT_FIELDS={},
        _PERSISTENT_CONNECTIONS={}, BoardConnection=BoardConnection,
        SerialSession=SerialSession, fail_close_port=None)
    transport = SimpleNamespace(
        SCALAR_U32_QUERY_HEADERS=set(), SCALAR_ONE_QUERY_HEADERS=set())
    stage_owner = SimpleNamespace(
        board_command=lambda *args: "original-stage",
        verify_board=lambda board, matrix, args: (
            events.append(("verify", board.address)) or {"passed": True}))
    trn = SimpleNamespace(
        APP_REALTIME_LOAD_VDC_MASK=1,
        APP_REALTIME_LOAD_DPLL_MASK=2,
        board_command=lambda *args: "original-board",
        flight_snapshot=lambda board, args: {},
        validate_fifo_reset=lambda snapshot: False,
        arm_with_evidence=lambda board, args, slot: (
            events.append(("arm", board.address, slot)) or {"slot": slot}),
        wait_runtime_config_applied=lambda board, args, slot: (
            events.append(("ready", board.address, slot)) or {"slot": slot}),
        close_persistent_connections=lambda: events.append(("close",)),
        Board=lambda port, uid, idn, build: SimpleNamespace(
            port=port, address=uid, idn=idn, build=str(build)),
    )

    class Probe:
        def __init__(self, options):
            self.opt = options
            self.args = argparse.Namespace(
                expected_build=0, baud=115200, timeout=3.0,
                settle=0.02, read_timeout=0.005)
            self.original_command = trn.board_command
            self.boards = [SimpleNamespace(address=uid, port=f"COM{slot}", build="123")
                           for slot, uid in enumerate(UIDS)]
            self.barrier = True
            self.block_queries = False
            self.sync = 1
            self.values = {tracked_typed.SESSION: 3}
            self.load_masks = {uid: 8 for uid in UIDS}
            self.report = {
                "schema": "base", "plan": {}, "tool_sources": [],
                "boards": {uid: {} for uid in UIDS}, "actions": [],
                "errors": [], "passed": True, "hardware_started": False,
            }

        def save(self):
            events.append(("save",))

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": str(exc)})

        def command(self, board, text, unused=None):
            events.append(("command", board.address, text))
            if text == tracked_typed.SYNC + "?":
                return str(self.sync)
            if text == tracked_typed.SESSION + "?":
                return str(self.values[tracked_typed.SESSION])
            if text == tracked_typed.TX:
                values = [0] * 24
                values[1] = 2
                return ",".join(map(str, values + [0, 0, mailbox(generation=2)]))
            if text.endswith("?") and text[:-1] in self.values:
                return str(self.values[text[:-1]])
            if " " in text:
                header, raw = text.split(" ", 1)
                if header in (tracked_typed.SESSION,
                              "SYSTem:VDC:FEEDback:REFerence",
                              "SYSTem:VDC:FEEDback:LOCALfollow"):
                    value = int(raw)
                    self.values[header] = value
                    return f"OK,{value}"
            if text == "SYSTem:TDMA:LOAD:MASK?":
                return str(self.load_masks[board.address])
            if text == "SYSTem:TDMA:RING:DIAGnostic?":
                return "0"
            if text == "SYSTem:TDMA:RING:BURSt?":
                return "1"
            if text.startswith("SYSTem:TDMA:FLIGHT:TX "):
                _, raw = text.split(" ", 1)
                _, seed, generation, slot, enabled = map(int, raw.split(","))
                checksum = sum((seed + index) & 255 for index in range(32))
                return f"OK,32,{checksum},{seed},{generation},{slot},{enabled}"
            if text == "SYSTem:TDMA:SCHEDule?":
                return "1,250000000"
            if text == tracked_typed.MODEL:
                return "model"
            if text == "*IDN?":
                return f"NO.1,DHRT100,{board.address},0.1.0"
            if text == "SYSTem:FW:BUILD?":
                return f'"{board.build}"'
            if text == "SYSTem:BOARD:NO?":
                return str(self.boards.index(board) + 1)
            if text.startswith("CALibration:ORIGin:TRIAL "):
                return "200"
            raise AssertionError(text)

        def control(self, board, text, expected="OK"):
            events.append(("control", board.address, text, expected))
            if text.startswith(tracked_typed.SYNC + " "):
                self.sync = int(text.split()[-1])
            return str(expected)

        def set_load_mask(self, board, mask):
            events.append(("load", board.address, mask))
            self.load_masks[board.address] = mask

        def stop_all(self, label, passes=1):
            events.append(("stop_all", label, passes))
            self.barrier = True
            self.block_queries = False

    return SimpleNamespace(
        __file__=str(source), Probe=Probe, BACKEND=backend, trn=trn,
        scpi_serial=transport,
        stage_owner=stage_owner, IDS=list(UIDS), fields=probe_support.fields,
        sha=lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest(),
        comports=lambda: [SimpleNamespace(serial_number=uid, device=f"COM{slot}")
                          for slot, uid in enumerate(UIDS)]), events


def options(tmp_path, **overrides):
    values = dict(out=tmp_path, seconds=3.0, baseline=None,
                  receipt=tmp_path / "receipt.json", check_only=False)
    values.update(overrides)
    return SimpleNamespace(**values)


def test_fields_crc_and_mailbox_match_archived_and_frozen_hardware():
    old = archived_module()
    new = tracked_typed.load_typed(None)
    assert new.scpi_serial is sys.modules[
        new.base.BACKEND.SerialSession.__module__]
    report = json.loads(GOLDEN.read_text(encoding="utf-8"))
    assert new.TX_FIELDS == old.TX_FIELDS
    assert new.fields('"a,b",2') == old.fields('"a,b",2') == ["a,b", "2"]
    for payload in (b"", b"123456789", bytes(range(30))):
        assert new.crc16(payload) == old.crc16(payload)
    tx_wire = old.fields(report["tx_raw"])[-1]
    assert new.typed_mailbox(tx_wire, report["binding_generation"], 0) == \
        old.typed_mailbox(tx_wire, report["binding_generation"], 0) == \
        report["tx_event"]
    for slot, row in enumerate(list(report["boards"].values())[1:], 1):
        raw = old.fields(row["direct_record_raw"])[-1]
        assert new.typed_mailbox(raw, report["binding_generation"], slot) == \
            old.typed_mailbox(raw, report["binding_generation"], slot) == \
            row["direct_record_event"]


@pytest.mark.parametrize("fault", [
    "size", "class", "mask", "crc", "generation", "width", "flags", "sequence16"])
def test_typed_mailbox_rejects_closed_set_faults(fault):
    raw = bytearray.fromhex(mailbox())
    generation = 77
    if fault == "size":
        raw.pop()
    elif fault == "class":
        raw[3] = 0x13
    elif fault == "mask":
        raw[5] = 0
    elif fault == "crc":
        raw[-1] ^= 1
    elif fault == "generation":
        generation = 78
    elif fault == "width":
        struct.pack_into("<I", raw, 24, 0)
        struct.pack_into("<H", raw, 30, tracked_typed.crc16(raw[:30]))
    elif fault == "flags":
        struct.pack_into("<H", raw, 28, 1)
        struct.pack_into("<H", raw, 30, tracked_typed.crc16(raw[:30]))
    else:
        struct.pack_into("<H", raw, 6, 102)
        struct.pack_into("<H", raw, 30, tracked_typed.crc16(raw[:30]))
    with pytest.raises(RuntimeError):
        tracked_typed.typed_mailbox(raw.hex(), generation, 1)


def test_typed_factory_registers_headers_and_composite_readback(tmp_path):
    base, events = fake_base(tmp_path)
    module = tracked_typed.load_typed(Path("unused.py"), runtime_module=base)
    probe = module.TypedProbe(options(tmp_path))
    assert probe.report["schema"] == "TYPED_PRIORITY_TX_SHORT_PROBE_V1"
    assert tracked_typed.SESSION.upper() in base.BACKEND.COMPOSITE_ACK_HEADERS
    assert tracked_typed.SYNC.upper() in base.BACKEND.TDMA_CONTROL_RESULT_FIELDS
    assert tracked_typed.SYNC.upper() + "?" in \
        module.scpi_serial.SCALAR_U32_QUERY_HEADERS
    probe.composite(probe.boards[0], tracked_typed.SESSION, 9)
    assert probe.values[tracked_typed.SESSION] == 9
    assert str(module.source_path) in [
        row["path"] for row in probe.report["tool_sources"]]


def test_typed_configure_reuses_stage_and_arms_reverse_order(
        tmp_path, monkeypatch):
    base, events = fake_base(tmp_path)
    module = tracked_typed.load_typed(Path("unused.py"), runtime_module=base)
    matrix = tmp_path / "matrix.json"
    matrix.write_text("{}", encoding="utf-8")
    monkeypatch.setattr(tracked_typed, "ROOT", tmp_path)
    monkeypatch.setattr(tracked_typed.time, "time", lambda: 100)
    probe = module.TypedProbe(options(tmp_path))
    probe.receipt = {"trn03_matrix": {"path": "matrix.json"},
                     "calibration_generation": 66}
    probe.configure()
    assert probe.report["binding_generation"] == 100
    assert probe.report["feedback_session"] == 100
    assert probe.report["clock_hz"] == 250_000_000
    assert probe.report["model_before_start"] == "model"
    assert [event[1] for event in events if event[0] == "arm"] == list(
        reversed(UIDS))
    assert [event[1] for event in events if event[0] == "verify"] == list(UIDS)
    assert set(probe.load_masks.values()) == {11}


def test_typed_acquire_starts_reverse_and_uses_bounded_trial(
        tmp_path, monkeypatch):
    base, events = fake_base(tmp_path)
    module = tracked_typed.load_typed(Path("unused.py"), runtime_module=base)
    probe = module.TypedProbe(options(tmp_path))
    probe.report["clock_hz"] = 250_000_000
    monkeypatch.setattr(tracked_typed.time, "time", lambda: 100)
    sleeps = []
    monkeypatch.setattr(tracked_typed.time, "sleep", sleeps.append)
    probe.acquire()
    starts = [event[1] for event in events
              if event[0] == "control" and event[2] == "SYSTem:TDMA:RING:START"]
    assert starts == list(reversed(UIDS))
    assert probe.report["trial_epoch"] == 200
    assert sleeps == [0.2, 3.0]
    assert probe.block_queries is True and probe.barrier is False


def test_typed_collect_matches_frozen_four_board_report(tmp_path):
    base, _ = fake_base(tmp_path)
    module = tracked_typed.load_typed(Path("unused.py"), runtime_module=base)
    report = json.loads(GOLDEN.read_text(encoding="utf-8"))
    probe = module.TypedProbe(options(tmp_path))
    probe.boards = [SimpleNamespace(address=uid) for uid in report["boards"]]
    probe.report["boards"] = {uid: {} for uid in report["boards"]}
    probe.report["binding_generation"] = report["binding_generation"]
    probe.report["feedback_session"] = report["feedback_session"]

    def command(self, board, text):
        row = report["boards"][board.address]
        return {
            tracked_typed.TX: report["tx_raw"],
            tracked_typed.MODEL: report["model_after_stop"],
            tracked_typed.CONS: row["consumer_raw"],
            "SYSTem:TDMA:FLIGHT:PRIORity?": row["priority_raw"],
            "SYSTem:TDMA:SCHEDule?": row["schedule_raw"],
            "SYSTem:ERR?": row["error_raw"],
        }[text]

    probe.command = MethodType(command, probe)
    probe.collect()
    assert probe.report["passed"] is True
    assert probe.report["flow_completed"] is True
    assert probe.report["tx_status"] == report["tx_status"]
    assert probe.report["tx_event"] == report["tx_event"]
    for uid, expected in list(report["boards"].items())[1:]:
        actual = probe.report["boards"][uid]
        assert actual["typed_event"] == expected["polled_event"]
        assert actual["matches_retained_master_event"] == \
            expected["polled_comparison"]["same_identity"]


def test_typed_check_only_validates_receipt_without_hardware(
        tmp_path, monkeypatch):
    base, events = fake_base(tmp_path)
    module = tracked_typed.load_typed(Path("unused.py"), runtime_module=base)
    receipt = {
        "source_tree_sha256": "source", "source_file_count": 7,
        "tdma_board_ids": list(UIDS), "build_id": "123"}
    path = tmp_path / "receipt.json"
    path.write_text(json.dumps(receipt), encoding="utf-8")
    monkeypatch.setattr(tracked_typed.gate, "_validate_quick_diagnostic_receipt",
                        lambda root, value: None)
    monkeypatch.setattr(tracked_typed.gate, "working_source_fingerprint",
                        lambda root: ("source", 7))
    probe = module.TypedProbe(options(tmp_path, receipt=path, check_only=True))
    probe.run()
    assert probe.report["check_only"] is True
    assert probe.report["passed"] is True
    assert not any(event[0] == "command" for event in events)
    assert base.BACKEND._PERSISTENT_CONNECTIONS == {}


def lifecycle_probe(tmp_path, monkeypatch, *, configure_failure=False,
                    collect_failure=False, restore_failure_uid=None,
                    close_failure_uid=None):
    base, events = fake_base(tmp_path)
    module = tracked_typed.load_typed(Path("unused.py"), runtime_module=base)
    receipt = {
        "source_tree_sha256": "source", "source_file_count": 7,
        "tdma_board_ids": list(UIDS), "build_id": "123"}
    path = tmp_path / "receipt.json"
    path.write_text(json.dumps(receipt), encoding="utf-8")
    monkeypatch.setattr(tracked_typed.gate, "_validate_quick_diagnostic_receipt",
                        lambda root, value: None)
    monkeypatch.setattr(tracked_typed.gate, "working_source_fingerprint",
                        lambda root: ("source", 7))
    probe = module.TypedProbe(options(tmp_path, receipt=path))
    original_board_owner = base.trn.board_command
    original_stage_owner = base.stage_owner.board_command
    if close_failure_uid is not None:
        base.BACKEND.fail_close_port = f"COM{UIDS.index(close_failure_uid)}"

    def configure():
        events.append(("configure",))
        for board in probe.boards:
            probe.report["boards"][board.address].update(
                original_load_mask=8, original_diagnostic=0)
        probe.report["boards"][probe.boards[0].address]["original_burst"] = 1
        if configure_failure:
            raise RuntimeError("injected configure failure")

    def acquire():
        events.append(("acquire",))

    def collect():
        events.append(("collect",))
        if collect_failure:
            raise RuntimeError("injected collect failure")

    original_control = probe.control

    def control(board, text, expected="OK"):
        if (board.address == restore_failure_uid and
                text == tracked_typed.SYNC + " 0"):
            events.append(("control_failure", board.address, text))
            raise RuntimeError("injected restore failure")
        return original_control(board, text, expected)

    probe.configure = configure
    probe.acquire = acquire
    probe.collect = collect
    probe.control = control
    probe.run()
    return (probe, base, events, original_board_owner,
            original_stage_owner)


def assert_lifecycle_restored(result):
    probe, base, events, original_board_owner, original_stage_owner = result
    assert base.trn.board_command is original_board_owner
    assert base.stage_owner.board_command is original_stage_owner
    assert base.BACKEND._PERSISTENT_CONNECTIONS == {}
    assert len([event for event in events if event[0] == "session_close"]) == 4
    assert any(event[:2] == ("stop_all", "final_stop") for event in events)
    assert any(event[0] == "control" and
               event[2] == "CALibration:ORIGin:REVOKe" for event in events)
    return probe, base, events


def test_typed_non_check_lifecycle_success_restores_all_owners(
        tmp_path, monkeypatch):
    probe, _, events = assert_lifecycle_restored(
        lifecycle_probe(tmp_path, monkeypatch))
    assert ("configure",) in events and ("acquire",) in events
    assert ("collect",) in events
    assert probe.report["passed"] is True
    assert not probe.report["errors"]


@pytest.mark.parametrize("fault", [
    "configure", "collect", "restore", "close"])
def test_typed_lifecycle_faults_still_restore_and_close_all(
        tmp_path, monkeypatch, fault):
    kwargs = {
        "configure_failure": fault == "configure",
        "collect_failure": fault == "collect",
        "restore_failure_uid": UIDS[1] if fault == "restore" else None,
        "close_failure_uid": UIDS[1] if fault == "close" else None,
    }
    probe, _, events = assert_lifecycle_restored(
        lifecycle_probe(tmp_path, monkeypatch, **kwargs))
    assert probe.report["passed"] is False
    scopes = [row["scope"] for row in probe.report["errors"]]
    expected = {
        "configure": "run",
        "collect": "final_collect",
        "restore": "disable_restore:" + UIDS[1] + ":sync",
        "close": "close",
    }[fault]
    assert expected in scopes
    if fault in ("configure", "collect", "restore"):
        assert any(event[0] == "load" and event[1] == UIDS[1]
                   for event in events)
    if fault == "restore":
        assert any(event[0] == "control" and event[1] == UIDS[2] and
                   event[2] == tracked_typed.SYNC + " 0" for event in events)


def test_typed_factory_rejects_incomplete_base_module():
    with pytest.raises(ValueError, match="Probe/BACKEND/trn"):
        tracked_typed.load_typed(
            Path("unused.py"), runtime_module=SimpleNamespace())
