"""Regression checks for the tracked VDC joint direct-RX layer."""

from __future__ import annotations

import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import zlib

import pytest

from tools.domains.vdc.priority_trace import probe_support
from tools.workflows.vdc_joint_capture import direct as tracked_direct


ROOT = Path(__file__).resolve().parents[2]
ARCHIVED = (ROOT / "out/HardwareAcceptance/20260917/"
            "dpll-priority-rx-direct-r1/capture_direct_rx.py")
GOLDEN = (ROOT / "out/HardwareAcceptance/20260917/"
          "dpll-longwindow-adoption-r1/native-r1/input-probe.json")
UIDS = ("uid-0", "uid-1", "uid-2", "uid-3")
TX_FIELDS = (
    "schema generation active retired have_offer calls encoded repeated rejected "
    "last_reject source_epoch event_sequence source_identity published_version "
    "model_token session role_generation clock_epoch clock_run local_slot node_count "
    "schedule_crc32 profile_crc32 uncertainty_width"
).split()


def archived_module():
    spec = importlib.util.spec_from_file_location(
        "archived_direct_for_test", ARCHIVED)
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


def crc16(raw):
    crc = 0xFFFF
    for byte in raw:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def mailbox(*, generation=77, sequence=101, lower_ns=1_000,
            width_ns=10, flags=0):
    wire = bytearray(32)
    wire[3] = 0x14
    wire[5] = 0x0E
    struct.pack_into("<H", wire, 6, sequence & 0xFFFF)
    struct.pack_into("<IIQIH", wire, 8, generation, sequence,
                     lower_ns, width_ns, flags)
    struct.pack_into("<H", wire, 30, crc16(wire[:30]))
    return wire.hex()


def decode_mailbox(raw, generation, slot):
    wire = bytes.fromhex(raw)
    assert len(wire) == 32 and wire[3] == 0x14 and wire[4] == 0
    assert wire[5] == 0x0E and (slot == 0 or wire[5] & (1 << slot))
    assert crc16(wire[:30]) == struct.unpack_from("<H", wire, 30)[0]
    found_generation, sequence, lower_ns, width_ns, flags = \
        struct.unpack_from("<IIQIH", wire, 8)
    assert found_generation == generation and width_ns > 0 and flags == 0
    return {
        "binding_generation": found_generation,
        "event_sequence": sequence, "lower_ns": lower_ns,
        "width_ns": width_ns, "flags": flags,
        "mailbox_hex": wire.hex(),
    }


def retained_header(sequence=101, schedule_crc=11, profile_crc=12):
    header = bytearray(32)
    header[0:8] = b"DIRECT01"
    struct.pack_into("<I", header, 8, sequence)
    header[12:16] = b"ABCD"
    struct.pack_into("<I", header, 16, schedule_crc)
    struct.pack_into("<I", header, 20, profile_crc)
    struct.pack_into("<I", header, 24,
                     zlib.crc32(header[:14] + header[15:24]))
    struct.pack_into("<I", header, 28,
                     zlib.crc32(header[:28] + bytes(4)))
    return header.hex()


def fake_typed(tmp_path, *, bad_slot=None, corrupt_header_slot=None,
               corrupt_mailbox_slot=None, payload_mismatch_slot=None):
    source = tmp_path / "typed.py"
    source.write_text("# fake typed parent\n", encoding="utf-8")
    events = []
    base = SimpleNamespace(
        sha=lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest())

    def fields(raw):
        return next(csv.reader([raw]))

    def tx_raw():
        status = dict.fromkeys(TX_FIELDS, 0)
        status.update(
            schema=1, generation=77, retired=1, have_offer=1,
            calls=10, encoded=8, source_epoch=8, event_sequence=101,
            source_identity=1, published_version=2, model_token=3,
            session=55, role_generation=2, clock_epoch=1, clock_run=1,
            local_slot=0, node_count=4, schedule_crc32=11,
            profile_crc32=12, uncertainty_width=10)
        return ",".join(map(str, [status[name] for name in TX_FIELDS] +
                            [1_000, 0, mailbox()]))

    def direct_raw(slot):
        values = dict.fromkeys(tracked_direct.RX_FIELDS, 0)
        if slot:
            values.update(
                schema=1, epoch=8, have_record=1, carrier_count=10,
                typed_accept_count=8, typed_reject_count=(1 if slot == bad_slot else 0),
                duplicate_count=2, unique_count=6,
                conflict_count=(1 if slot == bad_slot else 0),
                last_status=7, source_slot=0, target_mask=0x0E,
                carrier_sequence=101, irq_low=1_000 + slot,
                binding_generation=77, event_sequence=101,
                lower_low=1_000, width_ns=10)
        return ",".join(str(values[name]) for name in tracked_direct.RX_FIELDS)

    def lane_raw(slot):
        values = dict.fromkeys(tracked_direct.LANE_FIELDS, 0)
        if slot:
            values.update(
                schema=1, epoch=8, irq_count=10, publish_count=10,
                latest_sequence=101, latest_mailbox_seq16=101,
                irq_last_cycles=20, irq_max_cycles=30,
                total_low=200, entry_low=1_000 + slot,
                retained_mask=0x0F, sequence0=98, sequence1=99,
                sequence2=100, sequence3=101)
        return ",".join(str(values[name]) for name in tracked_direct.LANE_FIELDS)

    def consumer_raw(slot):
        values = [0] * 26
        if slot:
            values[0] = 1
            values[2] = 8
            values[3] = 1
            values[5] = 5
            values[12] = 0x14
            values[20] = 8
            values[21] = 101
        return ",".join(map(str, values + [retained_header(), mailbox()]))

    class TypedProbe:
        def __init__(self, options):
            self.opt = options
            self.boards = [SimpleNamespace(address=uid) for uid in UIDS]
            self.barrier = True
            self.block_queries = False
            self.report = {
                "schema": "typed", "plan": {}, "tool_sources": [],
                "binding_generation": 77, "feedback_session": 55,
                "boards": {uid: {} for uid in UIDS},
                "errors": [], "passed": True,
            }

        def save(self):
            events.append(("save",))

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": str(exc)})

        def command(self, board, text):
            events.append(("command", board.address, text))
            slot = self.boards.index(board)
            if text == "TX?":
                return tx_raw()
            if text == "MODEL?":
                return "model"
            if text == tracked_direct.RX:
                return direct_raw(slot)
            if text == tracked_direct.LANE:
                return lane_raw(slot)
            if text == tracked_direct.TIMING:
                return "1,10,5,6,7,8"
            if text == tracked_direct.BUDGET:
                return "1,2500,3500,250000,0,0,30,40,0"
            if text == "CONS?":
                return consumer_raw(slot)
            if text.startswith(tracked_direct.RECORD + " "):
                header = retained_header()
                if slot == corrupt_header_slot:
                    damaged = bytearray.fromhex(header)
                    damaged[24] ^= 1
                    header = damaged.hex()
                event = mailbox(lower_ns=(1_001 if slot == payload_mismatch_slot
                                          else 1_000))
                if slot == corrupt_mailbox_slot:
                    damaged = bytearray.fromhex(event)
                    damaged[-1] ^= 1
                    event = damaged.hex()
                return ",".join(map(str, [
                    8, 101, 1_000 + slot, 0, 0, 0,
                    header, event]))
            if text == "SYSTem:TDMA:SCHEDule?":
                return "1,250000000"
            if text == "SYSTem:ERR?":
                return '0,"No error"'
            raise AssertionError(text)

        def run(self):
            events.append(("parent_run",))

    return SimpleNamespace(
        __file__=str(source), TypedProbe=TypedProbe, base=base,
        fields=fields, TX="TX?", MODEL="MODEL?", CONS="CONS?",
        TX_FIELDS=TX_FIELDS, typed_mailbox=decode_mailbox), events


def build_probe(tmp_path, **faults):
    typed, events = fake_typed(tmp_path, **faults)
    baseline = tmp_path / "baseline.json"
    baseline.write_text(json.dumps([
        {"uid": uid, "command": tracked_direct.LANE,
         "raw": "1,0,8,1,9,0,0,0,0,0,1,1,1,20,100,0,1,0,1,1,1,1,1"}
        for uid in UIDS
    ] + [
        {"uid": uid, "command": tracked_direct.TIMING,
         "raw": "1,9,4,5,6,7"}
        for uid in UIDS
    ] + [
        {"uid": uid, "command": "SYSTEM:FW:BUILD?", "raw": '"123"'}
        for uid in UIDS
    ]), encoding="utf-8")
    module = tracked_direct.load_direct(
        Path("unused.py"), runtime_module=typed)
    probe = module.DirectProbe(SimpleNamespace(out=tmp_path, baseline=baseline))
    return module, probe, events, typed


def test_parsers_and_helpers_match_archived_and_frozen_hardware():
    old = archived_module()
    new = tracked_direct.load_direct(None)
    report = json.loads(GOLDEN.read_text(encoding="utf-8"))
    assert new.RX_FIELDS == old.RX_FIELDS
    assert new.LANE_FIELDS == old.LANE_FIELDS
    for row in report["boards"].values():
        assert new.parse_direct(row["direct_raw"]) == \
            old.parse_direct(row["direct_raw"]) == row["direct"]
        assert new.parse_lane(row["priority_raw"]) == \
            old.parse_lane(row["priority_raw"]) == row["lane"]
        assert new.parse_timing(row["timing_raw"]) == \
            old.parse_timing(row["timing_raw"]) == row["timing"]
        assert new.parse_budget(row["budget_raw"]) == \
            old.parse_budget(row["budget_raw"]) == row["budget"]
    for row in list(report["boards"].values())[1:]:
        record = old.parent.fields(row["direct_record_raw"])
        new.validate_header(record[6], row["direct"]["carrier_sequence"],
                            report["tx_status"])
        old.validate_header(record[6], row["direct"]["carrier_sequence"],
                            report["tx_status"])
        assert new.typed_subset(row["direct"]) == old.typed_subset(row["direct"])
        assert new.compare_events(row["direct"], report["tx_event"]) == \
            old.compare_events(row["direct"], report["tx_event"]) == \
            row["master_comparison"]


@pytest.mark.parametrize("parser,raw", [
    ("parse_direct", "1,2"),
    ("parse_lane", "-1," + ",".join(["0"] * 22)),
    ("parse_timing", "2,0,0,0,0,0"),
    ("parse_budget", "1,2,3"),
])
def test_direct_parsers_reject_malformed_closed_sets(parser, raw):
    module = tracked_direct.load_direct(None)
    with pytest.raises((RuntimeError, ValueError)):
        getattr(module, parser)(raw)


@pytest.mark.parametrize("fault", [
    "sequence", "transport_crc", "packet_crc", "schedule", "profile"])
def test_validate_header_rejects_each_integrity_fault(fault):
    module = tracked_direct.load_direct(None)
    raw = retained_header()
    sequence = 101
    tx = {"schedule_crc32": 11, "profile_crc32": 12}
    if fault == "sequence":
        sequence += 1
    elif fault in ("transport_crc", "packet_crc"):
        damaged = bytearray.fromhex(raw)
        damaged[24 if fault == "transport_crc" else 28] ^= 1
        raw = damaged.hex()
    else:
        tx[fault + "_crc32"] += 1
    with pytest.raises(RuntimeError):
        module.validate_header(raw, sequence, tx)


def test_compare_events_distinguishes_identity_from_payload():
    module = tracked_direct.load_direct(None)
    first = decode_mailbox(mailbox(), 77, 1)
    different_identity = dict(first, event_sequence=102)
    same_identity_different_payload = dict(first, lower_ns=1_001)
    assert module.compare_events(first, different_identity) == {
        "same_identity": False, "payload_equal": False,
        "disposition": "different_retained_events_not_corruption"}
    assert module.compare_events(first, same_identity_different_payload) == {
        "same_identity": True, "payload_equal": False,
        "disposition": "same_identity_check_payload"}


def test_direct_factory_owns_plan_provenance_and_baseline(tmp_path):
    module, probe, _, _ = build_probe(tmp_path)
    assert probe.report["schema"] == "DIRECT_PRIORITY_RX_SHORT_PROBE_V1"
    assert probe.report["plan"]["required"][0] == "three_follower_direct_delivery"
    assert probe.report["baseline"]["sha256"] == hashlib.sha256(
        probe.opt.baseline.read_bytes()).hexdigest()
    assert str(module.source_path) in [
        row["path"] for row in probe.report["tool_sources"]]


@pytest.mark.parametrize("barrier,blocked", [(False, False), (True, True)])
def test_direct_collect_requires_stop_barrier(tmp_path, barrier, blocked):
    _, probe, events, _ = build_probe(tmp_path)
    probe.barrier = barrier
    probe.block_queries = blocked
    with pytest.raises(RuntimeError, match="all-board STOP"):
        probe.collect()
    assert events == []


def test_direct_collect_validates_retained_and_polled_delivery(tmp_path):
    _, probe, events, _ = build_probe(tmp_path)
    probe.collect()
    assert probe.report["passed"] is True
    assert probe.report["flow_completed"] is True
    assert not probe.report["errors"]
    for uid in UIDS[1:]:
        row = probe.report["boards"][uid]
        assert row["delivery_counts"]["typed_carriers"] == 8
        assert row["direct_record_event"]["event_sequence"] == 101
        assert row["master_comparison"]["payload_equal"] is True
        assert row["polled_comparison"]["payload_equal"] is True
        assert row["timing_comparison"]["irq_body"]["max_delta_cycles"] == 10
        assert row["timing_comparison"]["segments"][
            "individual_max_delta_cycles"] == [1, 1, 1, 1]
    assert len([event for event in events
                if event[0] == "command" and event[2].startswith(
                    tracked_direct.RECORD + " ")]) == 3


def test_direct_collect_isolates_one_follower_failure(tmp_path):
    _, probe, _, _ = build_probe(tmp_path, bad_slot=2)
    probe.collect()
    assert probe.report["passed"] is False
    assert "delivery_counts" in probe.report["boards"][UIDS[1]]
    assert "direct_record_event" not in probe.report["boards"][UIDS[2]]
    assert "delivery_counts" in probe.report["boards"][UIDS[3]]
    assert [row["scope"] for row in probe.report["errors"]] == [
        "assessment:" + UIDS[2]]


@pytest.mark.parametrize("faults", [
    {"corrupt_header_slot": 2},
    {"corrupt_mailbox_slot": 2},
    {"payload_mismatch_slot": 2},
])
def test_direct_collect_isolates_retained_evidence_corruption(tmp_path, faults):
    _, probe, _, _ = build_probe(tmp_path, **faults)
    probe.collect()
    assert probe.report["passed"] is False
    assert "direct_record_event" in probe.report["boards"][UIDS[1]]
    assert not probe.report["boards"][UIDS[2]].get(
        "master_comparison", {}).get("payload_equal", False)
    assert "direct_record_event" in probe.report["boards"][UIDS[3]]
    assert [row["scope"] for row in probe.report["errors"]] == [
        "assessment:" + UIDS[2]]


def test_direct_run_preserves_conclusion_scope(tmp_path):
    _, probe, events, _ = build_probe(tmp_path)
    probe.run()
    assert events[0] == ("parent_run",)
    assert "No DCO update" in probe.report["conclusion_scope"]


def test_direct_factory_rejects_incomplete_typed_module():
    with pytest.raises(ValueError, match="TypedProbe/base"):
        tracked_direct.load_direct(
            Path("unused.py"), runtime_module=SimpleNamespace())
