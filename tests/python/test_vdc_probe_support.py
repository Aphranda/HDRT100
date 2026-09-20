"""Regression checks for migrated VDC priority probe support helpers."""

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import threading
import time
from types import ModuleType
from types import SimpleNamespace

import pytest

from tools.domains.vdc.priority_trace import probe_support as tracked
from tools.workflows.vdc_joint_capture import phase as tracked_phase
from tools.workflows.vdc_joint_capture import run_output as tracked_run_output
from tools.workflows.vdc_joint_capture.run_output import load_run_output


ROOT = Path(__file__).resolve().parents[2]
ARCHIVED = (ROOT / "out/HardwareAcceptance/20260915/dpll-input-probe-r1/"
            "probe_support.py")


def archived_module():
    spec = importlib.util.spec_from_file_location("archived_probe_support", ARCHIVED)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_constants_and_pure_decoders_match_archived():
    old = archived_module()
    for name in (
            "IDS", "OBSERVER_FIELDS", "TRACE_FIELDS", "BASELINE_SOURCE",
            "BASELINE_PACKAGE", "BASELINE_OTA"):
        assert getattr(tracked, name) == getattr(old, name)
    for raw in ('"a", 2,0x3', '1,2,3'):
        assert tracked.fields(raw) == old.fields(raw)
    names = ["a", "b", "c"]
    assert tracked.uints("1,2,0xffffffff", names) == old.uints(
        "1,2,0xffffffff", names)
    for raw in ("1,2", "-1,2,3", "4294967296,2,3"):
        with pytest.raises((ValueError, TypeError)):
            tracked.uints(raw, names)
        with pytest.raises((ValueError, TypeError)):
            old.uints(raw, names)
    before = {name: 0xFFFFFFFE for name in tracked.OBSERVER_FIELDS}
    after = {name: 1 for name in tracked.OBSERVER_FIELDS}
    assert tracked.observer_delta(before, after) == old.observer_delta(before, after)


@pytest.mark.parametrize("with_samples", [False, True])
def test_native_input_summary_matches_archived(with_samples):
    old = archived_module()
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
    assert tracked.native_input_summary(decoded) == old.native_input_summary(decoded)


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


def test_receipt_provenance_matches_archived(tmp_path):
    old = archived_module()
    path = receipt_fixture(tmp_path)
    calls = []

    def validate_ota(value, ids, build):
        calls.append((value, tuple(ids), build))

    fingerprint = lambda root: ("work", 9)
    expected = old.bind_receipt(tmp_path, path, fingerprint, validate_ota)
    calls.clear()
    actual = tracked.bind_receipt(tmp_path, path, fingerprint, validate_ota)
    assert actual == expected
    assert calls == [({"passed": True}, tuple(tracked.IDS), "123")]
    value = json.loads(path.read_text(encoding="utf-8"))
    value["strict_gates_passed"] = False
    path.write_text(json.dumps(value), encoding="utf-8")
    with pytest.raises(ValueError, match="strict gates"):
        tracked.bind_receipt(tmp_path, path, fingerprint, validate_ota)


def phase_source(path, *, fail=False):
    archived_sha = tracked_run_output.ARCHIVED_PROBE_SUPPORT_SHA256
    path.write_text(
        "from types import SimpleNamespace\n"
        "from probe_support import IDS, fields\n"
        "BOUND_IDS = IDS\n"
        "BOUND_FIELDS = fields\n"
        "class PhaseProbe:\n"
        "    def __init__(self, opt):\n"
        f"        self.report = {{'plan': {{}}, 'tool_sources': ["
        f"{{'path': 'archived/probe_support.py', 'sha256': {archived_sha!r}}}]}}\n"
        "backend = SimpleNamespace(TDMA_CONTROL_RESULT_FIELDS={})\n"
        "base = SimpleNamespace(BACKEND=backend)\n"
        "parent = SimpleNamespace(fields=lambda raw: raw.split(','), base=base)\n"
        "follow = SimpleNamespace(parent=parent)\n"
        + ("raise RuntimeError('phase load failed')\n" if fail else ""),
        encoding="utf-8")


@pytest.mark.parametrize("existing", [False, True])
@pytest.mark.parametrize("fail", [False, True])
def test_phase_loader_restores_compat_alias(tmp_path, monkeypatch, existing, fail):
    phase = tmp_path / "phase.py"
    phase_source(phase, fail=fail)
    sentinel = ModuleType("probe_support")
    if existing:
        monkeypatch.setitem(sys.modules, "probe_support", sentinel)
    else:
        monkeypatch.delitem(sys.modules, "probe_support", raising=False)
    if fail:
        with pytest.raises(RuntimeError, match="phase load failed"):
            tracked_phase._load_previous(phase)
    else:
        module = tracked_phase._load_previous(phase)
        assert module.BOUND_IDS is tracked.IDS
        assert module.BOUND_FIELDS is tracked.fields
    if existing:
        assert sys.modules["probe_support"] is sentinel
    else:
        assert "probe_support" not in sys.modules


def test_phase_loader_serializes_concurrent_aliases(tmp_path, monkeypatch):
    phase = tmp_path / "phase.py"
    phase_source(phase)
    sentinel = ModuleType("probe_support")
    monkeypatch.setitem(sys.modules, "probe_support", sentinel)
    entered = threading.Event()
    release = threading.Event()
    original = tracked_phase.importlib.util.spec_from_file_location

    class BlockingLoader:
        def __init__(self, loader):
            self.loader = loader

        def create_module(self, spec):
            return self.loader.create_module(spec)

        def exec_module(self, module):
            if not entered.is_set():
                entered.set()
                assert release.wait(2)
            self.loader.exec_module(module)

    def blocking_spec(*args, **kwargs):
        spec = original(*args, **kwargs)
        spec.loader = BlockingLoader(spec.loader)
        return spec

    monkeypatch.setattr(
        tracked_phase.importlib.util, "spec_from_file_location", blocking_spec)
    results = []
    errors = []

    def worker():
        try:
            results.append(tracked_phase._load_previous(phase).BOUND_IDS)
        except Exception as exc:
            errors.append(exc)

    first = threading.Thread(target=worker)
    second = threading.Thread(target=worker)
    first.start()
    assert entered.wait(2)
    second.start()
    time.sleep(0.05)
    release.set()
    first.join(2)
    second.join(2)
    assert not first.is_alive() and not second.is_alive()
    assert errors == []
    assert results == [tracked.IDS, tracked.IDS]
    assert sys.modules["probe_support"] is sentinel


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
