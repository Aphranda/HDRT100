"""Regression tests for the four-board P3 DPLL short observation."""

import json
from pathlib import Path
import zlib

import pytest

from tools.hardware_acceptance import p3_dpll_short as short
from tools.hardware_acceptance import p3_hardware_acceptance as p3


UIDS = ["no1", "no2", "no3", "no4"]


@pytest.fixture(autouse=True)
def decode_json_internal_trace(monkeypatch):
    monkeypatch.setattr(
        short, "_decode_internal_trace",
        lambda data, capture_id: json.loads(data.decode("utf-8")))


def phase_raw(slot, low, high):
    words = [0] * short.PHASE_WORD_COUNT
    words[0] = 1
    words[6] = 71
    words[7] = 72
    words[12] = 1
    words[18] = 0
    words[19] = slot
    for offset, value in ((30, low), (32, high)):
        encoded = value & ((1 << 64) - 1)
        words[offset] = encoded & 0xFFFFFFFF
        words[offset + 1] = encoded >> 32
    return ",".join(map(str, words))


def write_trace(folder, decoded):
    payload = json.dumps(decoded, sort_keys=True).encode("utf-8")
    (folder / "summary.ram.bin").write_bytes(payload)
    (folder / "decoded.json").write_text(
        json.dumps(decoded, sort_keys=True), encoding="utf-8")
    pages = []
    crc = zlib.crc32(payload)
    offset, size = 0, 4
    while offset < len(payload):
        chunk = payload[offset:offset + size]
        pages.append({
            "command": (
                f"SYSTem:VDC:PRIORity:TRACe:READ? "
                f"{decoded['status']['capture_id']},{offset},{size}"),
            "response": (
                f'{offset},{len(chunk)},{len(payload)},{crc},'
                f'"{chunk.hex()}"'),
        })
        offset += size
        size = min(128, len(payload) - offset)
    (folder / "read-pages.json").write_text(
        json.dumps(pages), encoding="utf-8")


def reports(tmp_path, *, scope, maximum=80.0, missing=False,
            trace_maximum=None, startup_maximum=10_000_000):
    joint = tmp_path / "report.json"
    capture = tmp_path / "capture"
    capture.mkdir(parents=True)
    input_report = capture / "input-probe.json"
    joint.write_text(json.dumps({
        "passed": True, "complete": True, "main_completed": True,
        "errors": [], "cleanup_errors": [], "restore_needed": [],
        "source_tree_sha256": "a" * 64, "receipt_build": "123",
        "workflow_inputs": {"board_uids": UIDS},
    }), encoding="utf-8")
    boards = {UIDS[0]: {"final_phase_raw": ",".join(["0"] * 52)}}
    trace_maximum = int(maximum if trace_maximum is None else trace_maximum)
    for slot, uid in enumerate(UIDS[1:], 1):
        capture_id = 100 + slot
        boards[uid] = {
            "final_phase_raw": phase_raw(slot, -int(maximum), int(maximum)),
            "trace_capture_id": capture_id,
        }
        folder = capture / f"NO{slot + 1}"
        folder.mkdir()
        records = []
        for index in range(6):
            bound = startup_maximum if index == 0 else trace_maximum
            records.append({
                "bin_index": index,
                "observed_start_raw": index * 2_500_000_000,
                "observed_end_raw": (index + 1) * 2_500_000_000,
                "success_count": 10,
                "first_success_offset_ticks": 1,
                "last_success_offset_ticks": 2_499_999_999,
                "max_success_gap_ticks": 1000,
                "max_service_gap_ticks": 1000,
                "residual_min_ns": -bound,
                "residual_max_ns": bound,
                "min_ppb": -1,
                "max_ppb": 1,
                "frequency_applied_count": 1,
                "phase_applied_count": 1,
                "residual_extrema_valid": True,
                "coverage_incomplete": False,
                "flag_names": (["PARTIAL", "TERMINAL", "STEADY_SUCCESS_GAP"]
                               if index == 5 else ["STEADY_SUCCESS_GAP"]),
            })
        decoded = {
            "schema": "VDC_SUMMARY_TRACE_DECODE_V9",
            "status": {"schema": 9, "capture_id": capture_id, "mode": 4,
                       "state": 3, "reason": 8, "session": 71,
                       "generation": 72, "local_slot": slot,
                       "reference_slot": 0, "node_count": 4,
                       "sample_interval_ms": 10000, "tick_hz": 250_000_000},
            "records": records,
        }
        write_trace(folder, decoded)
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


def mutate_trace(input_report, board_no, mutation):
    folder = input_report.parent / f"NO{board_no}"
    decoded = json.loads((folder / "decoded.json").read_text(encoding="utf-8"))
    mutation(decoded)
    write_trace(folder, decoded)


@pytest.mark.parametrize("value,severity", [
    (100.0, "INFO"), (100.001, "WARN"), (1000.0, "WARN"), (1000.001, "ERROR")])
def test_phase_severity_boundaries(value, severity):
    assert short.phase_severity(value) == severity


def test_internal_only_uses_steady_native_envelope_and_skips_external(tmp_path):
    joint, inner = reports(tmp_path, scope=False, maximum=101)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is True
    assert summary["phase_severity"] == "WARN"
    assert summary["phase_max_abs_ns"] == 101
    assert summary["external_status"] == "SKIPPED"
    assert summary["phase_domain"] == "internal_steady_summary_residual_envelope"
    assert summary["internal_settling_bins_excluded"] == 1
    assert len(summary["internal_phase_traces"]) == 3
    assert all(row["steady_bins"] == 5 and row["endpoint_within_last_bin"]
               for row in summary["internal_phase_traces"])
    short.validate_summary(tmp_path, summary)


def test_internal_startup_transient_is_recorded_but_not_used_for_severity(tmp_path):
    joint, inner = reports(
        tmp_path, scope=False, maximum=80, startup_maximum=5_000_000)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["phase_max_abs_ns"] == 80
    assert summary["phase_severity"] == "INFO"
    assert all(row["startup_residual_min_ns"] == -5_000_000
               for row in summary["internal_phase_traces"])


def test_internal_endpoint_must_match_last_steady_bin(tmp_path):
    joint, inner = reports(
        tmp_path, scope=False, maximum=101, trace_maximum=80)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("outside the last steady trace bin" in error
               for error in summary["errors"])


def test_internal_decoded_json_must_match_native_replay(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    path = inner.parent / "NO2" / "decoded.json"
    decoded = json.loads(path.read_text(encoding="utf-8"))
    decoded["records"][1]["residual_max_ns"] += 1
    path.write_text(json.dumps(decoded), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("differs from native bytes" in error for error in summary["errors"])


def test_internal_read_pages_must_reconstruct_native_bytes(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    path = inner.parent / "NO2" / "read-pages.json"
    pages = json.loads(path.read_text(encoding="utf-8"))
    response = pages[1]["response"]
    payload_hex = response.split('"')[1]
    changed = ("0" if payload_hex[0] != "0" else "1") + payload_hex[1:]
    pages[1]["response"] = response.replace(
        f'"{payload_hex}"', f'"{changed}"')
    path.write_text(json.dumps(pages), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("read page" in error for error in summary["errors"])


def test_internal_read_pages_must_use_canonical_first_page(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    path = inner.parent / "NO2" / "read-pages.json"
    pages = json.loads(path.read_text(encoding="utf-8"))
    pages[0]["command"] = pages[0]["command"].rsplit(",", 1)[0] + ",128"
    path.write_text(json.dumps(pages), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("command or offset" in error for error in summary["errors"])


@pytest.mark.parametrize("mutation", [
    lambda decoded: decoded["status"].update(sample_interval_ms=5000),
    lambda decoded: decoded["records"][-1].update(flag_names=["STEADY_SUCCESS_GAP"]),
    lambda decoded: decoded["records"][2].update(max_success_gap_ticks=300_000_000),
])
def test_internal_trace_requires_fixed_interval_terminal_and_success_gap(
        tmp_path, mutation):
    joint, inner = reports(tmp_path, scope=False)
    mutate_trace(inner, 2, mutation)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("coverage or success-gap" in error or "bin count" in error
               for error in summary["errors"])


def test_internal_trace_binds_endpoint_session_and_board_slot(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    mutate_trace(inner, 2, lambda decoded: decoded["status"].update(local_slot=2))
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("identity" in error for error in summary["errors"])


def test_internal_traces_share_one_session_generation(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    mutate_trace(inner, 2, lambda decoded: decoded["status"].update(
        session=73, generation=74))
    payload = json.loads(inner.read_text(encoding="utf-8"))
    words = [int(value) for value in payload["boards"][UIDS[1]]["final_phase_raw"].split(",")]
    words[6], words[7] = 73, 74
    payload["boards"][UIDS[1]]["final_phase_raw"] = ",".join(map(str, words))
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("do not share one session" in error for error in summary["errors"])


def test_summary_identity_must_match_acceptance_receipt(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    with pytest.raises(ValueError, match="source identity differs"):
        short.validate_summary(
            tmp_path, summary,
            expected_source_tree_sha256="b" * 64,
            expected_build_id="123",
            expected_board_uids=UIDS)


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


def test_bound_internal_native_change_is_rejected(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    raw = tmp_path / summary["internal_phase_traces"][0]["native_trace"]["path"]
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
