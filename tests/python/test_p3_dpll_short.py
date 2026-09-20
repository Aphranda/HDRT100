"""Regression tests for the four-board P3 DPLL short observation."""

import json
from pathlib import Path

import pytest

from tools.hardware_acceptance import p3_dpll_short as short
from tools.hardware_acceptance import p3_hardware_acceptance as p3


UIDS = ["no1", "no2", "no3", "no4"]


def phase_raw(slot, low, high):
    words = [0] * short.PHASE_WORD_COUNT
    words[0] = 1
    words[12] = 1
    words[18] = 0
    words[19] = slot
    for offset, value in ((30, low), (32, high)):
        encoded = value & ((1 << 64) - 1)
        words[offset] = encoded & 0xFFFFFFFF
        words[offset + 1] = encoded >> 32
    return ",".join(map(str, words))


def reports(tmp_path, *, scope, maximum=80.0, missing=False):
    joint = tmp_path / "report.json"
    capture = tmp_path / "capture"
    capture.mkdir(parents=True)
    input_report = capture / "input-probe.json"
    joint.write_text(json.dumps({
        "passed": True, "complete": True, "main_completed": True,
        "errors": [], "cleanup_errors": [], "restore_needed": [],
    }), encoding="utf-8")
    boards = {UIDS[0]: {"final_phase_raw": ",".join(["0"] * 52)}}
    for slot, uid in enumerate(UIDS[1:], 1):
        boards[uid] = {"final_phase_raw": phase_raw(slot, -int(maximum), int(maximum))}
    samples = []
    if scope:
        for due in range(5, 61, 5):
            if missing and due == 60:
                continue
            folder = capture / f"sample-{due:04d}"
            folder.mkdir()
            for channel in range(1, 5):
                (folder / f"ch{channel}.block").write_bytes(
                    f"{due}:{channel}".encode("ascii"))
            (folder / "scope.json").write_text(json.dumps({
                "channels": [{"blocks": [{"path": f"ch{channel}.block"}]}
                             for channel in range(1, 5)]}), encoding="utf-8")
            samples.append({"due_s": due, "capture_complete": True,
                            "phase": {"relative_ns": [[maximum], [0.0], [0.0]]}})
    payload = {
        "passed": True, "joint_internal_health_passed": True,
        "quiet_elapsed_s": 62.0, "errors": [], "boards": boards,
        "joint_verdict": {"internal_health_passed": True,
                          "external_status": "PASS" if scope else "SKIPPED"},
    }
    if scope:
        payload["scope_samples"] = samples
    input_report.write_text(json.dumps(payload), encoding="utf-8")
    return joint, input_report


@pytest.mark.parametrize("value,severity", [
    (100.0, "INFO"), (100.001, "WARN"), (1000.0, "WARN"), (1000.001, "ERROR")])
def test_phase_severity_boundaries(value, severity):
    assert short.phase_severity(value) == severity


def test_internal_only_uses_endpoint_residual_and_skips_external(tmp_path):
    joint, inner = reports(tmp_path, scope=False, maximum=101)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is True
    assert summary["phase_severity"] == "WARN"
    assert summary["phase_max_abs_ns"] == 101
    assert summary["external_status"] == "SKIPPED"
    assert summary["phase_domain"] == "internal_final_endpoint_residual_interval"
    short.validate_summary(tmp_path, summary)


def test_scope_path_uses_relative_ns_and_binds_every_raw_block(tmp_path, monkeypatch):
    joint, inner = reports(tmp_path, scope=True, maximum=999)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    assert summary["passed"] is True
    assert summary["phase_severity"] == "WARN"
    assert summary["phase_max_abs_ns"] == 999
    assert summary["phase_domain"] == "external_physical_sampled_windows"
    assert len(summary["scope_raw_blocks"]) == 48
    assert len(summary["scope_records"]) == 12
    monkeypatch.setattr(short, "_analyze_scope_memory", lambda report, blocks: {
        "relative_ns": [[999], [0.0], [0.0]]})
    short.validate_summary(tmp_path, summary)


def test_over_one_microsecond_and_missing_sample_are_errors(tmp_path, monkeypatch):
    joint, inner = reports(tmp_path / "phase", scope=True, maximum=1001)
    phase = short.summarize_joint(
        tmp_path / "phase", joint, inner, scope_enabled=True, seconds=60)
    assert phase["passed"] is False and phase["phase_severity"] == "ERROR"
    monkeypatch.setattr(short, "_analyze_scope_memory", lambda report, blocks: {
        "relative_ns": [[1001], [0.0], [0.0]]})
    with pytest.raises(ValueError, match="blocking"):
        short.validate_summary(tmp_path / "phase", phase)

    joint, inner = reports(tmp_path / "missing", scope=True, missing=True)
    missing = short.summarize_joint(
        tmp_path / "missing", joint, inner, scope_enabled=True, seconds=60)
    assert missing["passed"] is False and missing["evidence_status"] == "ERROR"
    assert missing["phase_severity"] == "INFO" and missing["phase_max_abs_ns"] == 0.0
    assert not any("phase exceeds" in error for error in missing["errors"])
    assert any("sample count" in error for error in missing["errors"])


def test_bound_raw_change_is_rejected(tmp_path):
    joint, inner = reports(tmp_path, scope=True)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    raw = tmp_path / summary["scope_raw_blocks"][0]["path"]
    raw.write_bytes(b"changed")
    with pytest.raises(ValueError, match="missing or changed"):
        short.validate_summary(tmp_path, summary)


def test_rehashed_json_phase_must_still_match_offline_raw_decode(tmp_path, monkeypatch):
    joint, inner = reports(tmp_path, scope=True, maximum=80)
    payload = json.loads(inner.read_text(encoding="utf-8"))
    payload["scope_samples"][0]["phase"]["relative_ns"] = [[90], [0.0], [0.0]]
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    monkeypatch.setattr(short, "_analyze_scope_memory", lambda report, blocks: {
        "relative_ns": [[80], [0.0], [0.0]]})
    with pytest.raises(ValueError, match="offline RAW decode"):
        short.validate_summary(tmp_path, summary)


def test_provisional_receipt_is_source_bound_and_requires_stopped_handoff(
        tmp_path, monkeypatch):
    monkeypatch.setattr(p3, "working_source_fingerprint", lambda root: ("source", 7))
    monkeypatch.setattr(p3, "_read_package_build_id", lambda path: "123")
    monkeypatch.setattr(p3, "validate_ota", lambda value, ids, build: None)
    monkeypatch.setattr(p3, "training_inputs_read_back", lambda matrix, tdma, ids: True)
    expected_proof = {"mode": "STOPPED_RECORDS", "passed": True, "records": [1, 2, 3, 4]}
    monkeypatch.setattr(p3, "validate_tdma_stopped_handoff",
                        lambda root, tdma, ids, build: expected_proof)
    receipt = {"schema": short.PROVISIONAL_SCHEMA, "joint_capture_only": True,
               "passed": True, "build_id": "123", "source_tree_sha256": "source",
               "source_file_count": 7, "tdma_board_ids": list(UIDS),
               "ota_board_ids": list(UIDS), "calibration_generation": 71}
    for name in ("firmware_package", "ota_summary", "tdma_summary"):
        path = tmp_path / f"{name}.json"
        path.write_text("{}", encoding="utf-8")
        receipt[name] = short.evidence_entry(tmp_path, path)
    matrix = tmp_path / "trn03_matrix.json"
    matrix.write_text(json.dumps({"calibration_generation": 71}), encoding="utf-8")
    receipt["trn03_matrix"] = short.evidence_entry(tmp_path, matrix)
    handoff = tmp_path / "handoff.json"
    handoff.write_text(json.dumps(expected_proof),
                       encoding="utf-8")
    receipt["tdma_stopped_handoff"] = short.evidence_entry(tmp_path, handoff)
    short.validate_provisional_receipt(tmp_path, receipt, list(UIDS))
    handoff.write_text(json.dumps({"mode": "STOPPED_RECORDS", "passed": False}),
                       encoding="utf-8")
    receipt["tdma_stopped_handoff"] = short.evidence_entry(tmp_path, handoff)
    with pytest.raises(ValueError, match="differs from raw records"):
        short.validate_provisional_receipt(tmp_path, receipt, list(UIDS))


def test_scope_discovery_absence_is_not_an_error(monkeypatch):
    class Manager:
        def list_resources(self):
            return ("USB::OTHER",)

        def close(self):
            pass

    fake = type("Visa", (), {"ResourceManager": lambda: Manager()})
    monkeypatch.setitem(__import__("sys").modules, "pyvisa", fake)
    result = p3.discover_optional_scope("USB::HDO", "SERIAL")
    assert result["present"] is False
    assert result["reason"] == "CONFIGURED_RESOURCE_ABSENT"
