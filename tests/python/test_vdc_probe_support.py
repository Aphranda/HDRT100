"""Regression checks for migrated VDC priority probe support helpers."""

import hashlib
import json
from pathlib import Path
import sys
from types import ModuleType
from types import SimpleNamespace

import pytest

from tools.domains.vdc.priority_trace import probe_support as tracked
from tools.workflows.vdc_joint_capture import base as tracked_base
from tools.workflows.vdc_joint_capture import run_output as tracked_run_output
from tools.workflows.vdc_joint_capture.run_output import load_run_output


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_IDS = [
    "0010071E65B5CB38", "FB276192BEF9CCE1",
    "2BD5090FE009FA2A", "A1E549202D18ED6A",
]
EXPECTED_OBSERVER_FIELDS = (
    "service snapshot eligible path expand submitted accepted rejected "
    "last_sequence last_config last_result"
).split()
EXPECTED_TRACE_FIELDS = (
    "armed complete sample_count dropped first_update_seq last_update_seq "
    "start_ms end_ms"
).split()


def test_constants_and_pure_decoders_match_frozen_values():
    assert tracked.IDS == EXPECTED_IDS
    assert tracked.OBSERVER_FIELDS == EXPECTED_OBSERVER_FIELDS
    assert tracked.TRACE_FIELDS == EXPECTED_TRACE_FIELDS
    assert tracked.BASELINE_SOURCE == \
        "2e61755a453b740193a28061463f4e0bba3f1460616cec485918173fa6cfcbd9"
    assert tracked.BASELINE_PACKAGE == \
        "8353019b485c986b19ded5436a8a7261fb2cea94fbb0e920efbe6dbb9b9a9ce2"
    assert tracked.BASELINE_OTA == \
        "83db8bad02ccbaa40896b640fddb6500d66cfaf77db53f87c589c66ca3e15d51"
    assert tracked.fields('"a", 2,0x3') == ["a", "2", "0x3"]
    assert tracked.fields("1,2,3") == ["1", "2", "3"]
    names = ["a", "b", "c"]
    assert tracked.uints("1,2,0xffffffff", names) == {
        "a": 1, "b": 2, "c": 0xFFFFFFFF}
    for raw in ("1,2", "-1,2,3", "4294967296,2,3"):
        with pytest.raises((ValueError, TypeError)):
            tracked.uints(raw, names)
    before = {name: 0xFFFFFFFE for name in tracked.OBSERVER_FIELDS}
    after = {name: 1 for name in tracked.OBSERVER_FIELDS}
    assert tracked.observer_delta(before, after) == {
        name: 3 for name in EXPECTED_OBSERVER_FIELDS[:8]}


@pytest.mark.parametrize("with_samples", [False, True])
def test_native_input_summary_matches_frozen_values(with_samples):
    before = {"rx_count": 0xFFFFFFFE, "transport_bad": 2, "value": 7}
    after = {"rx_count": 1, "transport_bad": 5, "value": 9}
    samples = ([{"snapshot": {"runtime": after, "tag": "first"}}]
               if with_samples else [])
    decoded = {
        "samples": samples,
        "baseline": {"snapshot": {"runtime": before}},
        "terminal": {"state": "STOP"},
        "errors": [],
    }
    summary = tracked.native_input_summary(decoded)
    if not with_samples:
        assert summary == {
            "sample_count": 0, "reason": "no_runtime_snapshot",
            "terminal": {"state": "STOP"}}
    else:
        assert summary == {
            "sample_count": 1,
            "terminal": {"state": "STOP"},
            "collection_errors": [],
            "runtime_counter_deltas": {"rx_count": 3, "transport_bad": 3},
            "first_runtime_snapshot": {"runtime": after, "tag": "first"},
            "last_runtime_snapshot": {"runtime": after, "tag": "first"},
            "qualification": (
                "Sparse Core0 diagnostics; not per-edge accuracy or WCET proof"),
        }


def receipt_fixture(tmp_path):
    package = tmp_path / "package.bin"
    package.write_bytes(b"package")
    ota = tmp_path / "ota.json"
    ota.write_text(json.dumps({"passed": True}), encoding="utf-8")
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    receipt = {
        "passed": True,
        "strict_gates_passed": True,
        "tdma_board_ids": list(tracked.IDS),
        "ota_board_ids": list(tracked.IDS),
        "build_id": "123",
        "source_tree_sha256": "source",
        "source_file_count": 7,
        "firmware_package": {"path": package.name, "sha256": digest(package)},
        "ota_summary": {"path": ota.name, "sha256": digest(ota)},
    }
    path = tmp_path / "acceptance.json"
    path.write_text(json.dumps(receipt), encoding="utf-8")
    return path


def test_receipt_provenance_matches_frozen_contract(tmp_path):
    path = receipt_fixture(tmp_path)
    calls = []

    def validate_ota(value, ids, build):
        calls.append((value, tuple(ids), build))

    fingerprint = lambda root: ("work", 9)
    receipt, evidence = tracked.bind_receipt(
        tmp_path, path, fingerprint, validate_ota)
    assert receipt["build_id"] == "123"
    assert calls == [({"passed": True}, tuple(tracked.IDS), "123")]
    assert evidence["receipt_path"] == str(path.resolve())
    assert evidence["receipt_sha256"] == hashlib.sha256(path.read_bytes()).hexdigest()
    assert evidence["board_source_sha256"] == "source"
    assert evidence["board_source_file_count"] == 7
    assert evidence["working_source_sha256"] == "work"
    assert evidence["working_source_file_count"] == 9
    assert evidence["working_source_matches_board_receipt"] is False
    assert evidence["known_p3_r2_baseline"] is False
    assert [Path(row["path"]).name for row in evidence["artifacts"]] == [
        "package.bin", "ota.json"]
    assert "Build labels alone" in evidence["binding_limit"]
    value = json.loads(path.read_text(encoding="utf-8"))
    value["strict_gates_passed"] = False
    path.write_text(json.dumps(value), encoding="utf-8")
    with pytest.raises(ValueError, match="strict gates"):
        tracked.bind_receipt(tmp_path, path, fingerprint, validate_ota)


@pytest.mark.parametrize("existing", [False, True])
def test_tracked_base_does_not_mutate_compat_alias(monkeypatch, existing):
    sentinel = ModuleType("probe_support")
    if existing:
        monkeypatch.setitem(sys.modules, "probe_support", sentinel)
    else:
        monkeypatch.delitem(sys.modules, "probe_support", raising=False)
    module = tracked_base.load_base()
    assert module.IDS is tracked.IDS
    assert module.fields is tracked.fields
    if existing:
        assert sys.modules["probe_support"] is sentinel
    else:
        assert "probe_support" not in sys.modules


def test_run_probe_replaces_archived_probe_support_provenance(tmp_path):
    class PhaseProbe:
        def __init__(self, opt):
            self.report = {"plan": {}, "tool_sources": [{
                "path": "archived/probe_support.py",
                "sha256": tracked_run_output.ARCHIVED_PROBE_SUPPORT_SHA256,
            }]}

    backend = SimpleNamespace(TDMA_CONTROL_RESULT_FIELDS={})
    base = SimpleNamespace(BACKEND=backend)
    parent = SimpleNamespace(fields=lambda raw: raw.split(","), base=base)
    phase = SimpleNamespace(
        PhaseProbe=PhaseProbe, follow=SimpleNamespace(parent=parent))
    loaded = load_run_output(Path("unused.py"), runtime_module=phase)
    probe = loaded.RunProbe(SimpleNamespace(period_ns=1_000_000))
    sources = probe.report["tool_sources"]
    assert not any(
        row["sha256"] == tracked_run_output.ARCHIVED_PROBE_SUPPORT_SHA256
        for row in sources)
    tracked_path = Path(tracked.__file__).resolve()
    assert any(
        Path(row["path"]).resolve() == tracked_path
        and row["sha256"] == hashlib.sha256(tracked_path.read_bytes()).hexdigest()
        for row in sources)
