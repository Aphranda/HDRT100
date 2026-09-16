import hashlib
import json
from pathlib import Path

import pytest

from tools.hardware_acceptance import sequence_single_board_gate as gate
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError


def cycle_report(tool_sha256: str, profile: str = "finite") -> dict:
    diagnostics = {
        command: {"error_before": '0,"No error"',
                  "error_after": '0,"No error"', "response": "0"}
        for command in ("READ:SEQ:LINK:TRANSPORT?", "SYST:TDMA:FLIGHT:PROCESS?",
                        "SYST:TDMA:FLIGHT:FIFO?", "SYST:REFMEM:SYNC:FLIGHT?")
    }
    diagnostics["READ:SEQ:LINK:TRANSPORT?"]["parsed"] = {
        "snapshot_quality": 1, "snapshot_quality_name": "FRESH"}
    pause = profile == "pause_resume"
    report = {
        "passed": True,
        "scope": "single_board_rj45_dut_vna_functional_cycle",
        "functional_cycle_verified": True,
        "tdma_stability_verified": False,
        "multi_board_verified": False,
        "independent_input_count_verified": False,
        "waveform_verified": False,
        "rf_path_verified": False,
        "p3_receipt": False,
        "failure": None,
        "cleanup_failures": [],
        "tool_sha256": tool_sha256,
        "settings": {"serial_number": "board-1", "build": "build-1",
                     "gui_control": True, "minimum_events": gate.MINIMUM_EVENTS,
                     "repeat": 0 if pause else gate.FINITE_REPEAT,
                     "pause_resume": pause, "scpi_next": True},
        "transport_before_cleanup": diagnostics,
        "stopped": {"io": {name: 0 for name in ("outputs", "owned", "armed", "busy")}},
        "after_stop_quiet": {"io": {
            name: 0 for name in ("outputs", "owned", "armed", "busy")}},
        "ring_stop_readbacks": [{"tdma": [0] * (
            max(gate.cycle.ring.RING_ENABLED,
                gate.cycle.ring.RING_ADAPTER_STARTED) + 1)}],
        "samples": [{"link": {"phase": 3, "exchange_id": 7}}],
        "scpi_next": [
            {"identity": [1, 2, step, step + 1], "response": "1"}
            for step in range(gate.MINIMUM_EVENTS)
        ],
    }
    if pause:
        report["pause_resume"] = {"passed": True, "exchange_rotated": True}
    else:
        report["repeat_result"] = (
            f"{gate.FINITE_REPEAT},{gate.FINITE_REPEAT},1")
    return report


def ota_summary(package: str) -> dict:
    return {"passed": True, "results": [{
        "board": {"serial_number": "board-1"},
        "passed": True,
        "forced_continue": False,
        "send": {"passed": True, "command": ["ota_send.py", package]},
        "commit": {"passed": True, "command": [
            "ota_boot_commit.py", "--expected-build", "build-1"]},
    }]}


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def evidence(path: Path, root: Path) -> dict:
    return {"path": path.relative_to(root).as_posix(),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def receipt_fixture(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    tool = tmp_path / "tools/hardware_acceptance/sequence_tdma_cycle_validate.py"
    tool.parent.mkdir(parents=True)
    tool.write_text("validator\n", encoding="utf-8")
    tool_sha = hashlib.sha256(tool.read_bytes()).hexdigest()
    package = tmp_path / "out/fw.pkg"
    package.parent.mkdir()
    package.write_bytes(b"firmware")
    ota = tmp_path / "out/ota.json"
    finite = tmp_path / "out/finite.json"
    pause = tmp_path / "out/pause.json"
    write_json(ota, ota_summary(str(package.resolve())))
    write_json(finite, cycle_report(tool_sha))
    write_json(pause, cycle_report(tool_sha, "pause_resume"))
    changed = ["components/sync_trigger/src/trigger_sequence_link.c"]
    receipt = {
        "schema": gate.RECEIPT_SCHEMA,
        "passed": True,
        "acceptance_scope": gate.ACCEPTANCE_SCOPE,
        "limitations": ["no_p3", "no_multi_board", "no_waveform", "no_rf",
                        "no_independent_edge_count", "no_tdma_stability"],
        "serial_number": "board-1",
        "build_id": "build-1",
        "source_tree_sha256": "tree",
        "source_file_count": 4,
        "changed_sources": changed,
        "validator_sha256": tool_sha,
        "firmware_package": evidence(package, tmp_path),
        "ota_summary": evidence(ota, tmp_path),
        "finite_report": evidence(finite, tmp_path),
        "pause_resume_report": evidence(pause, tmp_path),
    }
    monkeypatch.setattr(gate, "changed_staged_sources", lambda root: changed)
    monkeypatch.setattr(gate, "staged_source_fingerprint", lambda root: ("tree", 4))
    monkeypatch.setattr(gate, "working_source_fingerprint", lambda root: ("tree", 4))
    monkeypatch.setattr(gate.p3, "read_index_json", lambda root, path: receipt)
    return receipt, finite


def test_check_staged_accepts_matching_receipt(tmp_path, monkeypatch):
    receipt_fixture(tmp_path, monkeypatch)
    gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_check_staged_rejects_stale_source_fingerprint(tmp_path, monkeypatch):
    receipt_fixture(tmp_path, monkeypatch)
    monkeypatch.setattr(gate, "staged_source_fingerprint", lambda root: ("new-tree", 4))
    monkeypatch.setattr(gate, "working_source_fingerprint", lambda root: ("new-tree", 4))
    with pytest.raises(AcceptanceError, match="no matching"):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_check_staged_rejects_modified_evidence(tmp_path, monkeypatch):
    _, finite = receipt_fixture(tmp_path, monkeypatch)
    finite.write_text("{}", encoding="utf-8")
    with pytest.raises(AcceptanceError, match="missing or changed"):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_scope_rejects_non_allowlisted_firmware():
    with pytest.raises(AcceptanceError, match="outside"):
        gate.validate_scope(["drivers/flash/src/flash.c"])


def test_report_rejects_unavailable_transport_snapshot():
    report = cycle_report("a" * 64)
    report["transport_before_cleanup"]["READ:SEQ:LINK:TRANSPORT?"]["parsed"][
        "snapshot_quality"] = 0
    with pytest.raises(AcceptanceError, match="coherent attributed"):
        gate.validate_cycle_report(report, profile="finite", serial_number="board-1",
                                   build_id="build-1", tool_sha256="a" * 64)


def test_report_rejects_pause_without_exchange_rotation():
    report = cycle_report("a" * 64, "pause_resume")
    report["pause_resume"]["exchange_rotated"] = False
    with pytest.raises(AcceptanceError, match="exchange rotation"):
        gate.validate_cycle_report(report, profile="pause_resume",
                                   serial_number="board-1", build_id="build-1",
                                   tool_sha256="a" * 64)


@pytest.mark.parametrize("mutation", ["missing", "duplicate", "rejected"])
def test_report_rejects_incomplete_scpi_next_evidence(mutation):
    report = cycle_report("a" * 64)
    if mutation == "missing":
        report["scpi_next"] = report["scpi_next"][:-1]
    elif mutation == "duplicate":
        report["scpi_next"][-1]["identity"] = report["scpi_next"][0]["identity"]
    else:
        report["scpi_next"][-1]["response"] = "0"
    with pytest.raises(AcceptanceError, match="SCPI NEXT"):
        gate.validate_cycle_report(report, profile="finite", serial_number="board-1",
                                   build_id="build-1", tool_sha256="a" * 64)


def test_ota_summary_rejects_another_package(tmp_path):
    requested = tmp_path / "current.pkg"
    requested.write_bytes(b"current")
    summary = ota_summary(str(tmp_path / "old.pkg"))
    with pytest.raises(AcceptanceError, match="supplied firmware package"):
        gate.validate_ota_summary(summary, serial_number="board-1",
                                  build_id="build-1", package=requested)


def test_precommit_routes_only_allowlisted_source_to_single_board_gate():
    hook = (gate.ROOT / ".githooks/pre-commit").read_text(encoding="utf-8")
    assert "sequence_single_board_gate.py covers-staged" in hook
    assert "sequence_single_board_gate.py check-staged" in hook
    assert hook.index("covers-staged") < hook.index("p3_hardware_acceptance.py check-staged")
    assert "|tools/*|tests/*)" in hook
