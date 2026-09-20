"""Four-board 60-second DPLL evidence and phase severity policy."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any

SCHEMA = "HAOFV_P3_DPLL_SHORT_V1"
PROVISIONAL_SCHEMA = "HAOFV_P3_JOINT_PROVISIONAL_V1"
PHASE_WARN_NS = 100.0
PHASE_ERROR_NS = 1000.0
PHASE_WORD_COUNT = 52
U32_FIELD_COUNT = 26


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def evidence_entry(root: Path, path: Path) -> dict[str, str]:
    resolved = path.resolve()
    return {"path": resolved.relative_to(root.resolve()).as_posix(),
            "sha256": sha256_file(resolved)}


def _evidence_path(root: Path, entry: Any, label: str) -> Path:
    if not isinstance(entry, dict):
        raise ValueError(f"missing {label} evidence")
    relative, expected = entry.get("path"), entry.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise ValueError(f"invalid {label} evidence")
    path = (root / relative).resolve()
    if not path.is_file() or sha256_file(path) != expected:
        raise ValueError(f"{label} evidence is missing or changed")
    return path


def _signed64(lo: int, hi: int) -> int:
    value = lo | (hi << 32)
    return value - (1 << 64) if value & (1 << 63) else value


def parse_phase_status(raw: str) -> dict[str, int]:
    try:
        words = [int(token.strip(), 0) for token in raw.split(",")]
    except (AttributeError, ValueError) as exc:
        raise ValueError("phase status contains a non-integer field") from exc
    if len(words) != PHASE_WORD_COUNT or any(not 0 <= value <= 0xFFFFFFFF for value in words):
        raise ValueError("phase status must contain 52 UInt32 words")
    signed = [_signed64(words[index], words[index + 1])
              for index in range(U32_FIELD_COUNT, U32_FIELD_COUNT + 8, 2)]
    return {"schema": words[0], "active": words[2], "applied": words[12],
            "source_slot": words[18], "target_slot": words[19],
            "delta_ns": signed[0], "cumulative_ns": signed[1],
            "residual_lo": signed[2], "residual_hi": signed[3]}


def phase_severity(max_abs_ns: float) -> str:
    if not math.isfinite(max_abs_ns) or max_abs_ns < 0:
        raise ValueError("phase maximum must be finite and non-negative")
    if max_abs_ns > PHASE_ERROR_NS:
        return "ERROR"
    if max_abs_ns > PHASE_WARN_NS:
        return "WARN"
    return "INFO"


def _flatten_scope_phase(samples: Any, expected_count: int) -> list[float]:
    if not isinstance(samples, list) or len(samples) != expected_count:
        raise ValueError("scope sample count is incomplete")
    values: list[float] = []
    for row in samples:
        phase = row.get("phase", {}) if isinstance(row, dict) else {}
        relative = phase.get("relative_ns")
        if (row.get("capture_complete") is not True or row.get("missed") or
                not isinstance(relative, list) or len(relative) != 3):
            raise ValueError("scope sample is missing or incomplete")
        for channel in relative:
            if not isinstance(channel, list) or not channel:
                raise ValueError("scope phase channel is empty")
            for value in channel:
                if (isinstance(value, bool) or not isinstance(value, (int, float)) or
                        not math.isfinite(value)):
                    raise ValueError("scope phase value is invalid")
                values.append(float(value))
    if not values:
        raise ValueError("scope phase samples are empty")
    return values


def summarize_joint(root: Path, joint_report_path: Path, input_report_path: Path,
                    *, scope_enabled: bool, seconds: int) -> dict[str, Any]:
    root = root.resolve()
    outer = json.loads(joint_report_path.read_text(encoding="utf-8"))
    inner = json.loads(input_report_path.read_text(encoding="utf-8"))
    errors: list[str] = []
    if (outer.get("passed") is not True or outer.get("complete") is not True or
            outer.get("main_completed") is not True):
        errors.append("outer lifecycle did not complete successfully")
    if outer.get("errors") or outer.get("cleanup_errors") or outer.get("restore_needed"):
        errors.append("outer lifecycle reported execution, cleanup, or restore failure")
    verdict = inner.get("joint_verdict", {})
    plan = inner.get("plan", {})
    if (inner.get("passed") is not True or inner.get("joint_internal_health_passed") is not True or
            verdict.get("internal_health_passed") is not True or inner.get("errors")):
        errors.append("four-board internal health did not pass")
    elapsed = inner.get("quiet_elapsed_s")
    if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or elapsed < seconds:
        errors.append("internal observation did not cover the requested duration")

    internal_rows, internal_values = [], []
    boards = inner.get("boards")
    if not isinstance(boards, dict) or len(boards) != 4:
        errors.append("internal report does not contain four boards")
    else:
        for index, (uid, board) in enumerate(boards.items()):
            if index == 0:
                continue
            try:
                phase = parse_phase_status(board.get("final_phase_raw"))
                if (phase["schema"] != 1 or phase["active"] != 0 or phase["applied"] <= 0 or
                        phase["source_slot"] != 0 or phase["target_slot"] != index or
                        phase["residual_lo"] > phase["residual_hi"]):
                    raise ValueError("phase endpoint identity or state is invalid")
                maximum = float(max(abs(phase["residual_lo"]), abs(phase["residual_hi"])))
                internal_values.append(maximum)
                internal_rows.append({"board_id": uid,
                    "residual_lo_ns": phase["residual_lo"],
                    "residual_hi_ns": phase["residual_hi"], "max_abs_ns": maximum})
            except (AttributeError, TypeError, ValueError) as exc:
                errors.append(f"{uid}: {exc}")

    raw_blocks: list[dict[str, str]] = []
    scope_records: list[dict[str, str]] = []
    if scope_enabled:
        try:
            external_values = _flatten_scope_phase(inner.get("scope_samples"), seconds // 5)
        except ValueError as exc:
            external_values = []
            errors.append(str(exc))
        if verdict.get("external_status") != "PASS":
            errors.append("scope-enabled run did not report external PASS")
        for path in sorted(input_report_path.parent.glob("sample-*/*.block")):
            raw_blocks.append(evidence_entry(root, path))
        for path in sorted(input_report_path.parent.glob("sample-*/scope.json")):
            scope_records.append(evidence_entry(root, path))
        if len(raw_blocks) != (seconds // 5) * 4:
            errors.append("scope RAW block set is incomplete")
        if len(scope_records) != seconds // 5:
            errors.append("scope record set is incomplete")
        phase_source = "scope_samples[].phase.relative_ns"
        phase_domain = "external_physical_sampled_windows"
        phase_values = external_values
        external_status = "PASS" if not errors else "ERROR"
    else:
        if verdict.get("external_status") != "SKIPPED" or inner.get("scope_samples"):
            errors.append("scope-absent run must record external SKIPPED without samples")
        phase_source = "boards[].final_phase_raw.residual_lo/residual_hi"
        phase_domain = "internal_final_endpoint_residual_interval"
        phase_values = internal_values
        external_status = "SKIPPED"

    if not phase_values:
        errors.append("selected phase source contains no samples")
    max_abs_ns = max(phase_values) if phase_values else 0.0
    severity = phase_severity(max_abs_ns)
    if severity == "ERROR":
        errors.append(f"phase exceeds {PHASE_ERROR_NS:g} ns")
    return {"schema": SCHEMA, "passed": not errors and severity != "ERROR",
        "evidence_status": "ERROR" if errors else "PASS",
        "duration_s": seconds, "scope_enabled": scope_enabled,
        "external_status": external_status, "phase_source": phase_source,
        "phase_domain": phase_domain, "phase_warn_ns": PHASE_WARN_NS,
        "phase_error_ns": PHASE_ERROR_NS, "phase_max_abs_ns": max_abs_ns,
        "phase_severity": severity, "internal_phase_endpoints": internal_rows,
        "scope_resource": plan.get("scope_resource") if scope_enabled else None,
        "scope_serial": plan.get("scope_serial") if scope_enabled else None,
        "joint_report": evidence_entry(root, joint_report_path),
        "joint_input_report": evidence_entry(root, input_report_path),
        "scope_records": scope_records, "scope_raw_blocks": raw_blocks,
        "errors": errors}


def _analyze_scope_memory(report: dict[str, Any], blocks: dict[str, bytes]) -> dict[str, Any]:
    from tools.common.hdo_scope.reader import decode_block
    from tools.vdc_priority_trace.vdc_scope_evidence import analyze_memory
    return analyze_memory(report, blocks, decode_block)


def _validate_scope_decode(root: Path, summary: dict[str, Any], input_path: Path) -> None:
    if summary.get("scope_enabled") is not True:
        if summary.get("scope_records") or summary.get("scope_raw_blocks"):
            raise ValueError("scope-disabled summary contains external artifacts")
        return
    inner = json.loads(input_path.read_text(encoding="utf-8"))
    samples = inner.get("scope_samples", [])
    records = summary.get("scope_records", [])
    if len(records) != len(samples):
        raise ValueError("scope record/sample count differs")
    for index, (entry, sample) in enumerate(zip(records, samples)):
        path = _evidence_path(root, entry, f"scope record {index}")
        report = json.loads(path.read_text(encoding="utf-8"))
        blocks = {}
        for channel in report.get("channels", []):
            for block in channel.get("blocks", []):
                block_path = path.parent / block["path"]
                blocks[block["path"]] = block_path.read_bytes()
        decoded = _analyze_scope_memory(report, blocks)
        if decoded.get("relative_ns") != sample.get("phase", {}).get("relative_ns"):
            raise ValueError("scope phase differs from offline RAW decode")


def validate_summary(root: Path, summary: dict[str, Any]) -> None:
    if summary.get("schema") != SCHEMA:
        raise ValueError("DPLL short summary schema is invalid")
    report_path = _evidence_path(root, summary.get("joint_report"), "joint report")
    input_path = _evidence_path(root, summary.get("joint_input_report"), "joint input report")
    for index, entry in enumerate(summary.get("scope_records", [])):
        _evidence_path(root, entry, f"scope record {index}")
    for index, entry in enumerate(summary.get("scope_raw_blocks", [])):
        _evidence_path(root, entry, f"scope RAW block {index}")
    _validate_scope_decode(root, summary, input_path)
    recomputed = summarize_joint(root, report_path, input_path,
        scope_enabled=summary.get("scope_enabled") is True,
        seconds=int(summary.get("duration_s", 0)))
    if recomputed != summary:
        raise ValueError("DPLL short summary differs from raw joint evidence")
    if (summary.get("passed") is not True or summary.get("evidence_status") != "PASS" or
            summary.get("phase_severity") == "ERROR"):
        raise ValueError("DPLL short evidence contains a blocking result")


def validate_provisional_receipt(root: Path, receipt: dict[str, Any],
                                 board_uids: list[str]) -> None:
    if (receipt.get("schema") != PROVISIONAL_SCHEMA or
            receipt.get("joint_capture_only") is not True or
            receipt.get("passed") is not True or
            receipt.get("tdma_board_ids") != board_uids or
            receipt.get("ota_board_ids") != board_uids or
            not str(receipt.get("build_id", "")).isdigit()):
        raise ValueError("invalid P3 joint provisional receipt")
    from tools.hardware_acceptance import p3_hardware_acceptance as gate
    if gate.working_source_fingerprint(root) != (
            receipt.get("source_tree_sha256"), receipt.get("source_file_count")):
        raise ValueError("provisional receipt source fingerprint differs")
    paths = {}
    for name in ("firmware_package", "ota_summary", "trn03_matrix", "tdma_summary",
                 "tdma_stopped_handoff"):
        paths[name] = _evidence_path(root, receipt.get(name), name)
    if gate._read_package_build_id(paths["firmware_package"]) != receipt["build_id"]:
        raise ValueError("provisional package build differs")
    ota = json.loads(paths["ota_summary"].read_text(encoding="utf-8"))
    gate.validate_ota(ota, board_uids, receipt["build_id"])
    matrix = json.loads(paths["trn03_matrix"].read_text(encoding="utf-8"))
    tdma = json.loads(paths["tdma_summary"].read_text(encoding="utf-8"))
    if (isinstance(receipt.get("calibration_generation"), bool) or
            not isinstance(receipt.get("calibration_generation"), int) or
            receipt["calibration_generation"] <= 0 or
            matrix.get("calibration_generation") != receipt["calibration_generation"]):
        raise ValueError("provisional calibration generation differs")
    if not gate.training_inputs_read_back(matrix, tdma, board_uids):
        raise ValueError("provisional TRN-03/TDMA readback is invalid")
    proof = json.loads(paths["tdma_stopped_handoff"].read_text(encoding="utf-8"))
    expected = gate.validate_tdma_stopped_handoff(
        root, tdma, board_uids, receipt["build_id"])
    if proof != expected or proof.get("passed") is not True:
        raise ValueError("provisional stopped handoff differs from raw records")
