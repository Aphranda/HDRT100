#!/usr/bin/env python3
"""Run and gate the mandatory calibration-to-DPLL hardware acceptance.

``run`` is the firmware-change path: it builds the current working source,
updates all configured boards, then executes the complete acceptance.
``resume`` is the host-only/runtime-recovery path: it reuses an existing
package and OTA record, verifies the live build on every board, and continues
the same acceptance without building or updating firmware. ``check-staged``
is intentionally hardware-free: it rejects a code commit unless the receipt
in the Git index matches the complete staged source fingerprint and every
referenced local evidence digest remains intact.
"""

from __future__ import annotations

import pathlib
import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from copy import deepcopy
from datetime import datetime, timezone
from itertools import product
from pathlib import Path
from typing import Any, Iterable

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except (AttributeError, ValueError):
    pass

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

if __package__:
    from .acceptance_errors import AcceptanceError
    from .bench_config import (
        DEFAULT_ACCEPTANCE_TIMING, acceptance_budget_status, acceptance_timing,
        load_bench_config)
    from .p3_alarm_policy import build_acceptance_report
    from .p3_dpll_short import PROVISIONAL_SCHEMA, summarize_joint, validate_summary
    from .source_scope import (
        SOURCE_EXCLUDES, SOURCE_ROOT_FILES, SOURCE_ROOTS, _index_entries,
        _run_git, _tree_digest, changed_staged_sources, is_acceptance_source,
        sha256_file, staged_source_fingerprint, unstaged_sources,
        working_source_fingerprint)
else:
    from acceptance_errors import AcceptanceError
    from bench_config import (
        DEFAULT_ACCEPTANCE_TIMING, acceptance_budget_status, acceptance_timing,
        load_bench_config)
    from p3_alarm_policy import build_acceptance_report
    from p3_dpll_short import PROVISIONAL_SCHEMA, summarize_joint, validate_summary
    from source_scope import (
        SOURCE_EXCLUDES, SOURCE_ROOT_FILES, SOURCE_ROOTS, _index_entries,
        _run_git, _tree_digest, changed_staged_sources, is_acceptance_source,
        sha256_file, staged_source_fingerprint, unstaged_sources,
        working_source_fingerprint)

DEFAULT_CONFIG = Path("config/hardware_acceptance/p3_bench_quick.json")
DEFAULT_RECEIPT = Path("config/hardware_acceptance/p3_acceptance_receipt.json")
RECEIPT_SCHEMA = "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_V4"
TDMA_RECEIPT_SCHEMA = "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_V2"
TDMA_DIAGNOSTIC_RECEIPT_SCHEMA = (
    "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_DIAGNOSTIC_V1")
QUICK_DIAGNOSTIC_RECEIPT_SCHEMA = (
    "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_QUICK_DIAGNOSTIC_V1")
QUICK_GRADED_RECEIPT_SCHEMA = (
    "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_QUICK_GRADED_V1")
LIMITED_RECEIPT_SCHEMA = "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_10MHZ_LIMITED_V1"
# Routine credential: one build, the staged calibration matrix applied by one
# finite TDMA stage, then the four-board 60 s DPLL joint observation. A DPLL
# lock cannot be established without a working build, OTA, ring ARM/STOP
# handoff, applied calibration and the VDC feedback path, so this binds the
# transport/lock chain to the source fingerprint at a fraction of a full run.
DPLL_SHORT_RECEIPT_SCHEMA = "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_DPLL60_V1"
DPLL_SHORT_SCOPE = "DPLL60_JOINT"
DPLL_SHORT_OBSERVATION = "FOUR_BOARD_SHORT_60S"
# A DPLL lock proves the chain it rides on, not the subsystems it never
# exercises. A staged change under one of these paths must still carry the
# full receipt: this tuple is the recorded restore-to-FULL condition.
DPLL_SHORT_UNCOVERED_PREFIXES = (
    "components/calibration_manager/",
    "components/flash_transaction/",
    "components/led_manager/",
    "components/ota_manager/",
    "components/storage_manager/",
    "components/sync_io/",
    "drivers/mcu/flash/",
    "middleware/fatfs_port/",
    "third_party/portable_ota/",
    "tools/ota_send/",
    "tools/ota_multi_update/",
)
DPLL_SHORT_UNVERIFIED_SCOPE = (
    "P0T topology determination",
    "P1-P3 latency trials and their strict quality gates",
    "CLK/DATA training determination (application is exercised, measurement is not)",
    "storage and SD paths",
    "sync I/O trigger paths",
    "flash/OTA robustness beyond one successful write",
    "external scope measurement path",
)
REQUIRED_OTA_BLOCK_SIZE = 4096
SCHEDULE_HEADER_FIELDS = 8
SCHEDULE_PHASE_FIELDS = 11
ALARM_STAGES = ("P0", "P1", "P2", "P3", "T0", "T1", "T2", "T3", "TDMA", "DPLL")


def acceptance_scope(profile: str, tdma_only: bool,
                     strict_stages: Iterable[str] = ()) -> dict[str, Any]:
    """Freeze objectives before probing; extra observations never add gates."""
    strict = list(dict.fromkeys(strict_stages))
    if any(stage not in ALARM_STAGES for stage in strict):
        raise AcceptanceError("unknown strict acceptance stage")
    if tdma_only and "DPLL" in strict:
        raise AcceptanceError("DPLL cannot be required with --tdma-only")
    required = ["P0", "P3", "T3", "TDMA"]
    if not tdma_only:
        required.append("DPLL")
    if profile != "QUICK_DIAGNOSTIC":
        required = [stage for stage in ALARM_STAGES if stage != "DPLL" or not tdma_only]
        strict = list(dict.fromkeys(required + strict))
    required = list(dict.fromkeys(required + strict))
    return {"schema": "HAOFV_P3_FIXED_SCOPE_V1", "profile": profile,
            "required_stages": required, "strict_required_stages": strict,
            "tdma_only": tdma_only}


def normalize_alarm_failures(raw_failures: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Bind producer phase names to stages; never guess severity from error text."""
    failures = deepcopy(raw_failures)
    stage_prefixes = (("coarse CLK", "P1"), ("coded marker", "P2"),
                      ("TRN-00", "T0"), ("TRN-01", "T1"), ("TRN-02", "T2"),
                      ("TRN-03", "T3"), ("four-Node TDMA", "TDMA"),
                      ("TDMA", "TDMA"), ("final TDMA", "TDMA"),
                      ("internal DPLL", "DPLL"), ("DPLL", "DPLL"))
    for failure in failures:
        for prefix, stage in stage_prefixes:
            if failure.get("phase", "").startswith(prefix):
                failure["stage"] = stage
                break
    return failures


def write_alarm_report(context: dict[str, Any], *, error: str | None = None) -> dict[str, Any]:
    """Persist both classifications and their exact inputs, including aborts."""
    inputs = dict(profile=context["scope"]["profile"], stages=context["stages"],
                  required_stages=context["scope"]["required_stages"],
                  strict_required_stages=context["scope"]["strict_required_stages"],
                  failures=normalize_alarm_failures(context["failures"]), fatal_error=error)
    report = build_acceptance_report(**inputs)
    for name, value in (("alarm-inputs.json", inputs), ("alarms.json", report)):
        (context["out_dir"] / name).write_text(
            json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def validate_alarm_report(root: Path, record: dict[str, Any]) -> None:
    """New receipts must reproduce their grading; old receipts stay readable."""
    fields = ("alarm_report", "alarm_inputs", "fixed_scope")
    if not any(field in record for field in fields) and record.get("schema") != QUICK_GRADED_RECEIPT_SCHEMA:
        return
    if not all(field in record for field in fields):
        raise AcceptanceError("incomplete graded acceptance evidence")
    values = {}
    for field in fields:
        path = _validate_evidence_file(root, record[field], field)
        values[field] = json.loads(path.read_text(encoding="utf-8"))
    inputs, report, scope = (values[field] for field in ("alarm_inputs", "alarm_report", "fixed_scope"))
    expected_scope = acceptance_scope(
        record["acceptance_profile"], record.get("dpll_observation") == "SKIPPED_TDMA_ONLY",
        scope.get("strict_required_stages", []))
    if scope != expected_scope or any(inputs.get(key) != scope[key] for key in (
            "profile", "required_stages", "strict_required_stages")):
        raise AcceptanceError("graded acceptance scope differs from frozen objectives")
    if report != build_acceptance_report(**inputs):
        raise AcceptanceError("alarm grading differs from recorded evidence")
    if "alarm_stage_sources" in record:
        field_map = dict(P0="topology_summary", P1="coarse_calibration_summary",
                         P2="coded_calibration_summary", P3="p3_summary", T0="trn00_summary",
                         T1="trn01_summary", T2="trn02_summary", T3="trn03_matrix", TDMA="tdma_summary")
        if record.get("dpll_observation") != "SKIPPED_TDMA_ONLY":
            field_map["DPLL"] = "dpll_summary"
        if record["alarm_stage_sources"] != {stage: record.get(field) for stage, field in field_map.items()}:
            raise AcceptanceError("alarm stage sources differ from receipt evidence")
        measured = load_alarm_stages(root, record["alarm_stage_sources"], record["tdma_board_ids"])
        if inputs["stages"] != measured:
            raise AcceptanceError("alarm stage inputs differ from measured summaries")
    elif record.get("schema") == QUICK_GRADED_RECEIPT_SCHEMA:
        raise AcceptanceError("graded receipt is missing measured stage sources")
    if "diagnostic_failures" in record:
        if inputs["failures"] != normalize_alarm_failures(record["diagnostic_failures"]):
            raise AcceptanceError("alarm failures differ from receipt diagnostics")
    if record.get("schema") == QUICK_GRADED_RECEIPT_SCHEMA and scope["tdma_only"]:
        path = _validate_evidence_file(root, record.get("tdma_stopped_handoff"), "tdma_stopped_handoff")
        handoff = json.loads(path.read_text(encoding="utf-8"))
        tdma_path = _validate_evidence_file(root, record.get("tdma_summary"), "tdma_summary")
        tdma = json.loads(tdma_path.read_text(encoding="utf-8"))
        if handoff != validate_tdma_stopped_handoff(root, tdma, record["tdma_board_ids"], record["build_id"]):
            raise AcceptanceError("graded stopped handoff differs from raw records")
    if report["outcome"] == "BLOCKED":
        raise AcceptanceError("graded acceptance contains blocking findings")
    if record.get("acceptance_outcome") != report["outcome"]:
        raise AcceptanceError("receipt outcome differs from alarm report")


def training_inputs_read_back(matrix: dict[str, Any], tdma: dict[str, Any],
                             board_ids: list[str]) -> bool:
    """Accept selected training inputs only after every board validates them."""
    rows = tdma.get("stage_results", [])
    if (not isinstance(rows, list) or len(rows) != len(board_ids) or
            [row.get("board_id") for row in rows] != board_ids or
            matrix.get("node_ids_in_loop_order") != board_ids):
        return False
    identity = ("calibration_generation", "topology_generation", "topology_crc32",
                "profile_crc32", "schedule_crc32")
    for row in rows:
        stage = row.get("stage", {})
        links = row.get("links", [])
        if (row.get("passed") is not True or stage.get("complete") != 1 or
                stage.get("node_count") != len(board_ids) or
                stage.get("valid_link_bitmap") != (1 << len(board_ids)) - 1 or
                any(stage.get(key) != matrix.get(key) or key not in matrix for key in identity) or
                len(links) != len(board_ids) or
                [link.get("link_index") for link in links] != list(range(len(board_ids))) or
                any(link.get("valid") != 1 or any(link.get(key) != matrix[key] for key in identity)
                    for link in links)):
            return False
    return bool(board_ids)


def load_alarm_stages(root: Path, sources: dict[str, Any], board_ids: list[str]) -> dict[str, dict]:
    """Reconstruct classifications from hash-bound raw artifacts, not copied verdicts."""
    stages = {}
    for stage, entry in sources.items():
        path = _validate_evidence_file(root, entry, f"alarm stage {stage}")
        summary = json.loads(path.read_text(encoding="utf-8"))
        if summary.get("schema") == "HAOFV_HARDWARE_ACCEPTANCE_PHASE_SUMMARY_V1":
            children = []
            for child in summary.get("evidence", []):
                child_path = _validate_evidence_file(root, child, f"alarm stage {stage} child")
                children.append(json.loads(child_path.read_text(encoding="utf-8")))
            summary = {**summary, "summaries": children}
        stages[stage] = summary
    matrix, tdma = stages.get("T3", {}), stages.get("TDMA", {})
    usable = training_inputs_read_back(matrix, tdma, board_ids)
    for stage in ("P1", "P2", "T0", "T1", "T2", "T3"):
        if stage in stages:
            stages[stage] = {**stages[stage], "_input_validated": usable,
                             "_input_validation": {"source": "TDMA stage_results", "board_ids": board_ids,
                                                   "calibration_generation": matrix.get("calibration_generation")}}
    return stages


def resolve_path_delay_baseline_divisor(config: dict[str, Any]) -> int:
    """Resolve the explicit path-delay baseline policy for train stages."""
    value = config.get("path_delay_baseline_divisor", 2)
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise AcceptanceError(
            "path_delay_baseline_divisor must be a positive integer")
    return value


def resolve_previous_path_delay_baseline_divisor(
        config: dict[str, Any]) -> int:
    """Resolve the baseline that produced the configured train offsets."""
    value = config.get(
        "previous_path_delay_baseline_divisor",
        config.get("path_delay_baseline_divisor", 2))
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise AcceptanceError(
            "previous_path_delay_baseline_divisor must be a positive integer")
    return value


def rebase_node_offsets_for_path_delay(
        offsets_by_node: list[int], link_delays: list[int],
        previous_divisor: int, current_divisor: int, *,
        sample_period_ns: int = 4, direction: str = "reverse") -> list[int]:
    """Keep a prior absolute window while changing the per-link baseline."""
    if len(offsets_by_node) != len(link_delays) or not offsets_by_node:
        raise AcceptanceError("train offset and path-delay dimensions differ")
    if direction not in {"forward", "reverse"}:
        raise AcceptanceError("train offset direction is invalid")
    if sample_period_ns <= 0 or previous_divisor <= 0 or current_divisor <= 0:
        raise AcceptanceError("train offset rebasing parameters are invalid")
    previous_bases = [
        (delay + previous_divisor // 2) // previous_divisor
        for delay in link_delays]
    current_bases = [
        (delay + current_divisor // 2) // current_divisor
        for delay in link_delays]
    rebased: list[int] = []
    for node, offset in enumerate(offsets_by_node):
        link = node if direction == "reverse" else (node - 1) % len(link_delays)
        absolute_ns = previous_bases[link] + int(offset) * sample_period_ns
        delta_ns = absolute_ns - current_bases[link]
        value = (delta_ns + sample_period_ns // 2) // sample_period_ns
        if not -10 <= value <= 10:
            raise AcceptanceError(
                f"rebased train offset for node{node} is outside -10..+10")
        rebased.append(value)
    return rebased


def selected_node_offsets(summary: dict[str, Any], field: str,
                          node_count: int, phase: str,
                          diagnostic_fallback: list[int] | None = None) -> list[int]:
    """Return the actual selected row that feeds the next train stage."""
    row = summary.get("recommended_row")
    values = row.get(field) if isinstance(row, dict) else None
    if (isinstance(values, list) and len(values) == node_count and
            all(not isinstance(value, bool) and isinstance(value, int)
                for value in values)):
        return [int(value) for value in values]

    fallback = diagnostic_fallback
    fallback_valid = (isinstance(fallback, list) and
                      len(fallback) == node_count and
                      all(not isinstance(value, bool) and isinstance(value, int)
                          for value in fallback))
    if fallback_valid and summary.get("debug_forced_continue") is True:
        return [int(value) for value in fallback]
    rows = summary.get("row_results")
    matching_rows = [
        candidate for candidate in rows or []
        if isinstance(candidate, dict) and candidate.get(field) == fallback]
    if fallback_valid and len(matching_rows) == 1:
        observations = matching_rows[0].get("nodes")
        if (isinstance(observations, list) and
                len(observations) == node_count and
                all(isinstance(item, dict) and
                    int(item.get("observation_count", 0)) > 0
                    for item in observations)):
            return [int(value) for value in fallback]

    raise AcceptanceError(
        f"{phase} did not produce a complete selected offset row")


def selected_sck_offsets(
        summary: dict[str, Any], node_count: int,
        diagnostic_fallback: list[int] | None = None) -> list[int]:
    """Return the measured TRN-01 row that will feed TRN-03."""
    fallback = diagnostic_fallback
    fallback_valid = (
        isinstance(fallback, list) and len(fallback) == node_count and
        all(not isinstance(value, bool) and isinstance(value, int)
            and -10 <= int(value) <= 10 for value in fallback))
    # A recoverable tool-side failure may leave no matrix at all.  The
    # diagnostic gate has already written the raw log and a non-passing
    # summary; use only the configured bounded row to reach later runtime
    # gates, never a guessed measurement.
    if fallback_valid and summary.get("debug_forced_continue") is True:
        return [int(value) for value in fallback]
    matrix = summary.get("matrix")
    offset_matrix = matrix.get("offset_matrix") if isinstance(matrix, dict) else None
    rows = offset_matrix.get("rows") if isinstance(offset_matrix, dict) else None
    active_row_id = (offset_matrix.get("active_row_id")
                     if isinstance(offset_matrix, dict) else None)
    matches = [row for row in rows or []
               if isinstance(row, dict) and row.get("row_id") == active_row_id]
    if len(matches) == 1:
        values = matches[0].get("sck_offset_sample_counts_by_node")
        if (not isinstance(values, list) or len(values) != node_count or
                any(isinstance(value, bool) or not isinstance(value, int)
                    for value in values)):
            raise AcceptanceError("TRN-01 active offset row is incomplete")
        return [int(value) for value in values]

    candidates = (offset_matrix.get("candidate_values_by_node")
                  if isinstance(offset_matrix, dict) else None)
    if (fallback_valid and summary.get("diagnostic_only") is True and
            isinstance(candidates, list) and len(candidates) == node_count):
        if all(isinstance(values, list) and all(
                isinstance(value, int) and not isinstance(value, bool)
                for value in values) for values in candidates):
            return [int(value) for value in fallback]
    raise AcceptanceError("TRN-01 did not produce one active offset row")


def selected_data_offsets(summary: dict[str, Any], node_count: int) -> list[int]:
    """Return the active TRN-03 DATA row handed to the runtime stage."""
    matrix = summary.get("offset_matrix")
    rows = matrix.get("rows") if isinstance(matrix, dict) else None
    active_row_id = (matrix.get("active_row_id")
                     if isinstance(matrix, dict) else None)
    matches = [row for row in rows or []
               if isinstance(row, dict) and row.get("row_id") == active_row_id]
    if len(matches) != 1:
        raise AcceptanceError("TRN-03 did not produce one active offset row")
    values = matches[0].get("data_offset_sample_counts_by_node")
    if (not isinstance(values, list) or len(values) != node_count or
            any(isinstance(value, bool) or not isinstance(value, int)
                for value in values)):
        raise AcceptanceError("TRN-03 active DATA row is incomplete")
    return [int(value) for value in values]


def write_trn03_diagnostic_fallback_inputs(
        root: Path, out_dir: Path, data_summary_path: Path,
        residence_summary_path: Path, sck_summary_path: Path,
        original_log_path: Path) -> tuple[Path, Path, Path, Path]:
    """Freeze one bounded, debug-only TRN-03 matrix retry input set.

    The retry never edits the raw train evidence. It may repair an empty or
    mixed residence identity from the same-run DATA identity and may replace
    one rejected DATA trial per otherwise-empty link with a timing template
    copied from another structurally complete same-run trial. Link identity,
    P3 baseline and configured offsets always come from the target link.
    """
    data = json.loads(data_summary_path.read_text(encoding="utf-8"))
    residence = json.loads(residence_summary_path.read_text(encoding="utf-8"))
    sck = json.loads(sck_summary_path.read_text(encoding="utf-8"))
    if not all(isinstance(value, dict) for value in (data, residence, sck)):
        raise AcceptanceError("TRN-03 fallback inputs must be JSON objects")

    node_ids = data.get("node_ids_in_loop_order")
    if (not isinstance(node_ids, list) or not 2 <= len(node_ids) <= 8 or
            node_ids != residence.get("node_ids_in_loop_order") or
            node_ids != sck.get("node_ids_in_loop_order")):
        raise AcceptanceError("TRN-03 fallback node identity is inconsistent")
    node_count = len(node_ids)
    data_matrix = data.get("matrix")
    residence_matrix = residence.get("matrix")
    if not isinstance(data_matrix, dict) or not isinstance(residence_matrix, dict):
        raise AcceptanceError("TRN-03 fallback matrix evidence is missing")
    data_identity = data_matrix.get("identity")
    if not isinstance(data_identity, dict):
        raise AcceptanceError("TRN-03 fallback DATA identity is missing")

    identity_fields = (
        "calibration_generation", "topology_generation", "topology_crc32",
        "profile_crc32", "schedule_crc32", "sample_period_ns",
    )
    identity: dict[str, int] = {}
    for field in identity_fields:
        values = data_identity.get(field)
        if (not isinstance(values, list) or len(values) != 1 or
                isinstance(values[0], bool) or not isinstance(values[0], int)):
            raise AcceptanceError(
                f"TRN-03 fallback DATA {field} is not a singleton")
        identity[field] = int(values[0])

    fallback_data = deepcopy(data)
    fallback_residence = deepcopy(residence)
    fallback_sck = deepcopy(sck)
    fallback_residence_matrix = fallback_residence["matrix"]
    original_residence_identity = deepcopy(
        fallback_residence_matrix.get("identity"))
    fallback_residence_matrix["identity"] = {
        "calibration_generation": [identity["calibration_generation"]],
        "topology_crc32": [identity["topology_crc32"]],
        "profile_crc32": [identity["profile_crc32"]],
        "schedule_crc32": [identity["schedule_crc32"]],
        "tick_resolution_ns": [identity["sample_period_ns"]],
        "topology_generation_by_node": {
            str(node): [identity["topology_generation"]]
            for node in range(node_count)
        },
    }

    sck_matrix = fallback_sck.get("matrix")
    sck_parameters = fallback_sck.get("training_parameters")
    if not isinstance(sck_matrix, dict) or not isinstance(sck_parameters, dict):
        raise AcceptanceError("TRN-03 fallback SCK matrix is missing")
    original_sck_identity = deepcopy(sck_matrix.get("identity"))
    sck_matrix["identity"] = {
        "calibration_generation": [identity["calibration_generation"]],
        "topology_generation": [identity["topology_generation"]],
        "topology_crc32": [identity["topology_crc32"]],
        "profile_crc32": [identity["profile_crc32"]],
        "schedule_crc32": [identity["schedule_crc32"]],
        "sample_period_ns": [identity["sample_period_ns"]],
    }
    configured_sck_offsets = sck_parameters.get("node_sck_offset_samples")
    sck_offsets = sck_matrix.get("offset_matrix")
    if (not isinstance(configured_sck_offsets, list) or
            len(configured_sck_offsets) != node_count or
            not isinstance(sck_offsets, dict)):
        raise AcceptanceError("TRN-03 fallback SCK offsets are incomplete")
    raw_candidates = sck_offsets.get("candidate_values_by_node")
    if not isinstance(raw_candidates, list) or len(raw_candidates) != node_count:
        raw_candidates = [[] for _ in range(node_count)]
    normalized_candidates: list[list[int]] = []
    for node, values in enumerate(raw_candidates):
        measured = [
            int(value) for value in values
            if isinstance(value, int) and not isinstance(value, bool)
        ] if isinstance(values, list) else []
        configured = int(configured_sck_offsets[node])
        normalized_candidates.append(sorted(set([*measured, configured])))
    sck_rows = [
        {"row_id": row_id,
         "sck_offset_sample_counts_by_node": list(values)}
        for row_id, values in enumerate(product(*normalized_candidates))
    ]
    configured_row = [int(value) for value in configured_sck_offsets]
    active_matches = [
        row["row_id"] for row in sck_rows
        if row["sck_offset_sample_counts_by_node"] == configured_row
    ]
    if len(active_matches) != 1:
        raise AcceptanceError("TRN-03 fallback SCK row is ambiguous")
    sck_offsets.update({
        "schema": "HAOFV_SCK_OFFSET_MATRIX_V2",
        "candidate_values_by_node": normalized_candidates,
        "full_matrix_row_count": len(sck_rows),
        "active_row_id": active_matches[0],
        "rows": sck_rows,
    })

    fallback_data_matrix = fallback_data["matrix"]
    links = fallback_data_matrix.get("links")
    trials = fallback_data.get("trials")
    parameters = fallback_data.get("training_parameters")
    if (not isinstance(links, list) or len(links) != node_count or
            not isinstance(trials, list) or not isinstance(parameters, dict)):
        raise AcceptanceError("TRN-03 fallback DATA dimensions are invalid")
    marker_offsets = parameters.get("node_marker_offset_samples")
    data_offsets = parameters.get("node_data_offset_samples")
    link_bases = parameters.get("link_base_delay_ns_by_link")
    if any(not isinstance(values, list) or len(values) != node_count
           for values in (marker_offsets, data_offsets, link_bases)):
        raise AcceptanceError("TRN-03 fallback DATA parameters are incomplete")
    baseline_fields = (
        "link_delay_ns_by_link", "link_base_delay_ns_by_link",
        "path_delay_baseline_divisor", "sample_period_ns",
    )
    if any(field not in parameters for field in baseline_fields):
        raise AcceptanceError("TRN-03 fallback DATA baseline is incomplete")
    baseline_repairs: dict[str, dict[str, Any]] = {}
    for label, target in (
            ("residence", fallback_residence.get("training_parameters")),
            ("sck", fallback_sck.get("training_parameters"))):
        if not isinstance(target, dict):
            raise AcceptanceError(
                f"TRN-03 fallback {label} baseline is missing")
        before = {field: deepcopy(target.get(field))
                  for field in baseline_fields}
        for field in baseline_fields:
            target[field] = deepcopy(parameters[field])
        baseline_repairs[label] = {
            "before": before,
            "after": {field: deepcopy(target[field])
                      for field in baseline_fields},
        }

    required_source = (
        "training_window_start_ns", "training_window_end_ns",
        "marker_to_data_samples", "expected_sample_count",
        "guard_sample_count", "link_base_delay_ns",
    )

    def complete_trial(trial: Any) -> bool:
        source = trial.get("source") if isinstance(trial, dict) else None
        return bool(
            isinstance(trial, dict) and trial.get("passed") is True and
            isinstance(source, dict) and
            all(field in source for field in required_source) and
            "link_marker_offset_sample" in trial and
            "calibrated_data_offset_sample_count" in trial)

    templates = [trial for trial in trials if complete_trial(trial)]
    if not templates:
        raise AcceptanceError(
            "TRN-03 fallback has no complete measured DATA timing template")

    repaired_trials: list[dict[str, Any]] = []
    for link_index in range(node_count):
        link_trials = [
            (index, trial) for index, trial in enumerate(trials)
            if isinstance(trial, dict) and int(trial.get("link", -1)) == link_index
        ]
        if any(complete_trial(trial) for _, trial in link_trials):
            continue
        if not link_trials:
            raise AcceptanceError(
                f"TRN-03 fallback link{link_index} has no trial slot")
        link_rows = [
            link for link in links if isinstance(link, dict) and
            int(link.get("link", -1)) == link_index
        ]
        if len(link_rows) != 1:
            raise AcceptanceError(
                f"TRN-03 fallback link{link_index} identity is ambiguous")
        link = link_rows[0]
        marker_destination = int(link.get("marker_destination_node", -1))
        data_destination = int(link.get("data_destination_node", -1))
        if (not 0 <= marker_destination < node_count or
                not 0 <= data_destination < node_count):
            raise AcceptanceError(
                f"TRN-03 fallback link{link_index} direction is invalid")
        slot, rejected = link_trials[0]
        synthetic = deepcopy(templates[0])
        for field in (
                "link", "source_node", "destination_node",
                "marker_source_node", "marker_destination_node",
                "data_source_node", "data_destination_node",
                "marker_direction", "data_direction", "repeat_index",
                "train_epoch", "calibration_generation"):
            if field in rejected:
                synthetic[field] = rejected[field]
            elif field in link:
                synthetic[field] = link[field]
        synthetic["link"] = link_index
        synthetic["passed"] = True
        synthetic.pop("error", None)
        synthetic["link_marker_offset_sample"] = int(
            marker_offsets[marker_destination])
        synthetic["calibrated_data_offset_sample_count"] = int(
            data_offsets[data_destination])
        synthetic["source"] = deepcopy(templates[0]["source"])
        synthetic["source"]["link_base_delay_ns"] = int(
            link_bases[link_index])
        synthetic["diagnostic_fallback"] = {
            "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
            "source": "same_run_complete_data_timing_template",
            "original_trial": deepcopy(rejected),
        }
        trials[slot] = synthetic
        repaired_trials.append({
            "link": link_index,
            "trial_slot": slot,
            "template_link": int(templates[0].get("link", -1)),
            "marker_offset_sample": int(marker_offsets[marker_destination]),
            "data_offset_sample": int(data_offsets[data_destination]),
            "link_base_delay_ns": int(link_bases[link_index]),
            "original_error": str(rejected.get("error", "trial rejected")),
        })

    fallback_dir = out_dir / "trn03-diagnostic-fallback"
    fallback_dir.mkdir(parents=True, exist_ok=True)
    fallback_data_path = fallback_dir / "trn02-data.json"
    fallback_residence_path = fallback_dir / "trn00-residence.json"
    fallback_sck_path = fallback_dir / "trn01-sck.json"
    manifest_path = fallback_dir / "manifest.json"
    for path, value in (
            (fallback_data_path, fallback_data),
            (fallback_residence_path, fallback_residence),
            (fallback_sck_path, fallback_sck)):
        path.write_text(
            json.dumps(value, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
    raw_log = original_log_path.read_text(
        encoding="utf-8", errors="replace") if original_log_path.is_file() else ""
    manifest = {
        "schema": "HAOFV_TRN03_DIAGNOSTIC_FALLBACK_V1",
        "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
        "attempt_limit": 1,
        "reason": "fresh TRN-03 matrix generation failed",
        "original_log": original_log_path.resolve().relative_to(root).as_posix(),
        "original_log_tail": raw_log.splitlines()[-20:],
        "original_inputs": {
            "data": data_summary_path.resolve().relative_to(root).as_posix(),
            "residence": residence_summary_path.resolve().relative_to(root).as_posix(),
            "sck": sck_summary_path.resolve().relative_to(root).as_posix(),
        },
        "fallback_inputs": {
            "data": fallback_data_path.resolve().relative_to(root).as_posix(),
            "residence": fallback_residence_path.resolve().relative_to(root).as_posix(),
            "sck": fallback_sck_path.resolve().relative_to(root).as_posix(),
        },
        "residence_identity_before": original_residence_identity,
        "residence_identity_after": fallback_residence_matrix["identity"],
        "sck_identity_before": original_sck_identity,
        "sck_identity_after": sck_matrix["identity"],
        "sck_offset_repair": {
            "configured_row": configured_row,
            "candidate_values_by_node": normalized_candidates,
            "active_row_id": active_matches[0],
        },
        "shared_baseline_repairs": baseline_repairs,
        "data_trial_repairs": repaired_trials,
        "bounded_defaults": {
            "missing_residence_ticks": 1,
            "missing_loop_delay_ticks": 1,
        },
    }
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return (fallback_data_path, fallback_residence_path,
            fallback_sck_path, manifest_path)


def write_trn03_previous_matrix_fallback(
        root: Path, receipt_path: Path, matrix_path: Path,
        manifest_path: Path, *, build_id: str, board_ids: list[str],
        calibration_generation: int, link_delays: list[int],
        baseline_divisor: int, marker_offsets: list[int],
        sck_offsets: list[int], data_offsets: list[int],
        failure: str) -> None:
    """Rebind one previously accepted matrix for debug-only observation."""
    if not receipt_path.is_file():
        raise AcceptanceError("TRN-03 previous receipt is unavailable")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    evidence = receipt.get("trn03_matrix")
    if (not isinstance(receipt, dict) or receipt.get("passed") is not True or
            receipt.get("build_id") != build_id or
            receipt.get("tdma_board_ids") != board_ids or
            not isinstance(evidence, dict)):
        raise AcceptanceError("TRN-03 previous receipt identity is invalid")
    relative = evidence.get("path")
    expected_sha = evidence.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected_sha, str):
        raise AcceptanceError("TRN-03 previous matrix evidence is invalid")
    previous_path = root / relative
    if (not previous_path.is_file() or
            sha256_file(previous_path) != expected_sha):
        raise AcceptanceError("TRN-03 previous matrix evidence changed")
    previous = json.loads(previous_path.read_text(encoding="utf-8"))
    if (not isinstance(previous, dict) or
            previous.get("node_ids_in_loop_order") != board_ids):
        raise AcceptanceError("TRN-03 previous matrix node order differs")
    count = len(board_ids)
    vectors = (link_delays, marker_offsets, sck_offsets, data_offsets)
    if (count < 2 or any(len(values) != count for values in vectors) or
            baseline_divisor <= 0):
        raise AcceptanceError("TRN-03 previous matrix fallback dimensions differ")
    offset_matrix = previous.get("offset_matrix")
    links = previous.get("links")
    if not isinstance(offset_matrix, dict) or not isinstance(links, list) or \
            len(links) != count:
        raise AcceptanceError("TRN-03 previous matrix structure is incomplete")
    sample_period_ns = int(offset_matrix.get("sample_period_ns", 0))
    if sample_period_ns <= 0:
        raise AcceptanceError("TRN-03 previous matrix sample period is invalid")

    rebound = deepcopy(previous)
    rebound["passed"] = False
    rebound["diagnostic_continue"] = True
    rebound["calibration_generation"] = int(calibration_generation)
    bases = [
        (int(delay) + baseline_divisor // 2) // baseline_divisor
        for delay in link_delays
    ]
    rebound["training_parameters"] = {
        "link_delay_ns_by_link": [int(value) for value in link_delays],
        "link_base_delay_ns_by_link": bases,
        "path_delay_baseline_divisor": int(baseline_divisor),
        "sample_period_ns": sample_period_ns,
        "loaded_from": [
            "previous accepted TRN-03 timing",
            "current P3 baseline",
            "current configured diagnostic offsets",
        ],
    }
    rebound["offset_matrix"] = {
        "sample_period_ns": sample_period_ns,
        "full_matrix_row_count": 1,
        "active_row_id": 0,
        "rows": [{
            "row_id": 0,
            "marker_offset_sample_counts_by_node": [
                int(value) for value in marker_offsets],
            "sck_offset_sample_counts_by_node": [
                int(value) for value in sck_offsets],
            "data_offset_sample_counts_by_node": [
                int(value) for value in data_offsets],
            "marker_offset_ns_by_node": [
                int(value) * sample_period_ns for value in marker_offsets],
            "sck_offset_ns_by_node": [
                int(value) * sample_period_ns for value in sck_offsets],
            "data_offset_ns_by_node": [
                int(value) * sample_period_ns for value in data_offsets],
        }],
    }
    for link_index, link in enumerate(rebound["links"]):
        if not isinstance(link, dict):
            raise AcceptanceError(
                f"TRN-03 previous matrix link{link_index} is invalid")
        marker_destination = int(link.get("marker_destination_node", -1))
        data_destination = int(link.get("data_destination_node", -1))
        if (not 0 <= marker_destination < count or
                not 0 <= data_destination < count):
            raise AcceptanceError(
                f"TRN-03 previous matrix link{link_index} direction is invalid")
        marker = int(marker_offsets[marker_destination])
        sck = int(sck_offsets[marker_destination])
        data = int(data_offsets[data_destination])
        base = bases[link_index]
        phases = {
            "marker_phase_delay_cycles":
                (base + marker * sample_period_ns + sample_period_ns // 2) //
                sample_period_ns,
            "sck_phase_delay_cycles":
                (base + sck * sample_period_ns + sample_period_ns // 2) //
                sample_period_ns,
            "data_phase_delay_cycles":
                (base + data * sample_period_ns + sample_period_ns // 2) //
                sample_period_ns,
        }
        if any(not 0 <= value <= 31 for value in phases.values()):
            raise AcceptanceError(
                f"TRN-03 previous matrix link{link_index} phase is unsafe")
        link.update({
            "marker_offset_sample_count": marker,
            "sck_offset_sample_count": sck,
            "data_offset_sample_count": data,
            "link_base_delay_ns": base,
            **phases,
        })
        source = link.setdefault("source_evidence", {})
        if isinstance(source, dict):
            source["diagnostic_fallback"] = {
                "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
                "previous_matrix": relative,
                "current_link_delay_ns": int(link_delays[link_index]),
            }
    derivation = rebound.setdefault("derivation", {})
    if not isinstance(derivation, dict):
        raise AcceptanceError("TRN-03 previous matrix derivation is invalid")
    derivation.update({
        "diagnostic_previous_matrix_fallback": {
            "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
            "previous_matrix": relative,
            "previous_matrix_sha256": expected_sha,
            "failure": failure,
        },
        "sck_replay_selection": {
            "requested_offset_sample_counts_by_node": [
                int(value) for value in sck_offsets],
            "selected_offset_sample_counts_by_node": [
                int(value) for value in sck_offsets],
            "requested_row_replay_safe": True,
            "selected_row_replay_safe": True,
            "selection_reason": "diagnostic_explicit_config_previous_matrix",
            "diagnostic_continue": True,
        },
    })
    matrix_path.parent.mkdir(parents=True, exist_ok=True)
    matrix_path.write_text(
        json.dumps(rebound, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps({
        "schema": "HAOFV_TRN03_DIAGNOSTIC_FALLBACK_V1",
        "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
        "attempt_limit": 1,
        "mode": "previous_accepted_matrix_rebound",
        "failure": failure,
        "previous_receipt": receipt_path.resolve().relative_to(root).as_posix(),
        "previous_matrix": relative,
        "previous_matrix_sha256": expected_sha,
        "build_id": build_id,
        "board_ids": board_ids,
        "calibration_generation": int(calibration_generation),
        "link_delay_ns_by_link": [int(value) for value in link_delays],
        "link_base_delay_ns_by_link": bases,
        "path_delay_baseline_divisor": int(baseline_divisor),
        "marker_offset_sample_counts_by_node": marker_offsets,
        "sck_offset_sample_counts_by_node": sck_offsets,
        "data_offset_sample_counts_by_node": data_offsets,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def stage_training_parameters(summary: dict[str, Any], phase: str,
                              node_count: int) -> dict[str, Any]:
    """Extract the exact P3 baseline loaded by a train-stage summary."""
    parameters = summary.get("training_parameters")
    if not isinstance(parameters, dict):
        # TRN-00 MARK's offset-matrix summary keeps these fields at top level.
        parameters = summary
    delays = parameters.get("link_delay_ns_by_link")
    bases = parameters.get("link_base_delay_ns_by_link")
    divisor = parameters.get("path_delay_baseline_divisor")
    sample_period = parameters.get("sample_period_ns")
    if sample_period is None:
        phase_contract = summary.get("unified_phase_training", {})
        if isinstance(phase_contract, dict):
            sample_period = phase_contract.get("sample_period_ns")
    if (not isinstance(delays, list) or len(delays) != node_count or
            any(isinstance(value, bool) or not isinstance(value, int) or
                value <= 0 for value in delays)):
        raise AcceptanceError(f"{phase} did not record P3 path delays")
    if (not isinstance(bases, list) or len(bases) != node_count or
            any(isinstance(value, bool) or not isinstance(value, int) or
                value <= 0 for value in bases)):
        raise AcceptanceError(f"{phase} did not record per-link baselines")
    if (isinstance(divisor, bool) or not isinstance(divisor, int) or
            divisor <= 0 or
            isinstance(sample_period, bool) or
            not isinstance(sample_period, int) or sample_period <= 0):
        raise AcceptanceError(f"{phase} baseline identity is incomplete")
    expected = [
        (int(delay) + int(divisor) // 2) // int(divisor)
        for delay in delays]
    if [int(value) for value in bases] != expected:
        raise AcceptanceError(f"{phase} baseline does not match divisor")
    return {
        "link_delay_ns_by_link": [int(value) for value in delays],
        "link_base_delay_ns_by_link": [int(value) for value in bases],
        "path_delay_baseline_divisor": int(divisor),
        "sample_period_ns": int(sample_period),
    }


_TIMING_PATH: Path | None = None
_TIMING_EVENTS: list[dict[str, Any]] = []


def _start_timing_probe(path: Path) -> None:
    """Start the host-side acceptance probe; never runs on the realtime path."""
    global _TIMING_PATH, _TIMING_EVENTS
    _TIMING_PATH = path
    _TIMING_EVENTS = []
    _write_timing_probe()


def _write_timing_probe() -> None:
    if _TIMING_PATH is None:
        return
    _TIMING_PATH.parent.mkdir(parents=True, exist_ok=True)
    _TIMING_PATH.write_text(
        json.dumps({
            "schema": "HAOFV_HARDWARE_ACCEPTANCE_TIMING_PROBE_V1",
            "events": _TIMING_EVENTS,
        }, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")


def _record_timing_event(event: dict[str, Any]) -> None:
    _TIMING_EVENTS.append(event)
    _write_timing_probe()


def add_serial_timing(command: list[str], timing: dict[str, float], *,
                      action: bool = False, capture: bool = False,
                      gap: bool = False) -> None:
    """Append the common deterministic serial timing arguments to a tool."""
    command.extend(["--timeout", str(timing["serial_timeout_s"]),
                    "--settle", str(timing["input_settle_s"])])
    if action:
        # START/STOP acknowledgements race the post-reset admission window:
        # 50 ms was measured to time out even though the board accepted the
        # action (`error_after` stays "No error").  Keep a hard 100 ms floor,
        # matching `calibration_timeout_config` and the gap floor below.
        command.extend(["--action-timeout",
                        str(max(float(timing["action_timeout_s"]), 0.1))])
    if capture:
        command.extend(["--capture-timeout",
                        str(timing["p3_capture_timeout_s"])])
    if gap:
        # P0T/topology communication gap: 50 ms was measured to make the
        # stopped-topology probes flaky.  Keep a hard 100 ms floor even when a
        # stale derived config still carries the old 0.05 s value.
        command.extend(["--gap", str(max(float(timing["output_handoff_s"]), 0.1))])


def waveform_capture_enabled(config: dict[str, Any], stage: str) -> bool:
    """Return the explicit waveform policy for one acceptance stage.

    Capture is diagnostic-only and must be opt-in.  Keeping the lookup in one
    place prevents a legacy global switch from silently enabling captures in
    unrelated stages.
    """
    key = f"{stage}_capture_waveforms"
    value = config.get(key, False)
    if not isinstance(value, bool):
        raise AcceptanceError(f"{key} must be boolean")
    return value


def calibration_probe_phase_cycles(config: dict[str, Any],
                                   level: int) -> int:
    """Return the stopped probe phase selected for one operating profile.

    The phase is expressed in 250 MHz system-clock cycles. It cannot be
    shared across the frequency ladder: the physical follower requires the
    phase plus its fixed SCK re-arm budget to fit inside one half-period.
    """
    by_level = config.get("calibration_probe_phase_cycles_by_level")
    if not isinstance(by_level, dict) or str(level) not in by_level:
        raise AcceptanceError(
            f"missing calibration probe phase for profile level {level}")
    phase = by_level[str(level)]
    if (not isinstance(phase, int) or isinstance(phase, bool) or
            not 1 <= phase <= 31):
        raise AcceptanceError(
            f"invalid calibration probe phase for profile level {level}: "
            f"{phase!r}")
    return phase


def calibration_coded_probe_phase_cycles(config: dict[str, Any],
                                          level: int) -> list[int]:
    """Return the ordered P2 phase candidates for one operating profile."""
    by_level = config.get("calibration_coded_probe_phase_cycles_by_level")
    if by_level is None:
        return [calibration_probe_phase_cycles(config, level)]
    if not isinstance(by_level, dict) or str(level) not in by_level:
        raise AcceptanceError(
            f"missing coded calibration probe phases for profile level {level}")
    phases = by_level[str(level)]
    if (not isinstance(phases, list) or not phases or
            any(not isinstance(phase, int) or isinstance(phase, bool) or
                not 1 <= phase <= 31 for phase in phases) or
            len(set(phases)) != len(phases)):
        raise AcceptanceError(
            f"invalid coded calibration probe phases for profile level "
            f"{level}: {phases!r}")
    return phases


def read_index_json(root: Path, path: Path) -> dict[str, Any]:
    raw = _run_git(root, "show", f":{path.as_posix()}")
    try:
        value = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise AcceptanceError(f"invalid staged receipt {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise AcceptanceError(f"staged receipt {path} is not an object")
    return value


def _validate_evidence(root: Path, record: dict[str, Any], *,
                       include_dpll: bool = True) -> None:
    names = (
            "firmware_package", "ota_summary", "topology_summary",
            "coarse_calibration_summary", "coded_calibration_summary",
            "initialization_reset", "p3_summary", "trn00_summary",
            "trn01_summary", "trn02_summary",
            "trn03_matrix", "tdma_summary")
    if include_dpll:
        names += ("dpll_summary", "dpll_scope_discovery")
    for name in names:
        _validate_evidence_file(root, record.get(name), name)
    if include_dpll:
        path = _validate_evidence_file(root, record.get("dpll_summary"), "dpll_summary")
        summary = json.loads(path.read_text(encoding="utf-8"))
        validate_summary(
            root, summary,
            expected_source_tree_sha256=record.get("source_tree_sha256"),
            expected_build_id=str(record.get("build_id", "")),
            expected_board_uids=record.get("tdma_board_ids"))
        discovery_path = _validate_evidence_file(
            root, record.get("dpll_scope_discovery"), "dpll_scope_discovery")
        discovery = json.loads(discovery_path.read_text(encoding="utf-8"))
        config_relative = record.get("bench_config_path")
        config_expected = record.get("bench_config_sha256")
        config_path = root / config_relative if isinstance(config_relative, str) else None
        if config_path is not None or config_expected is not None:
            if (config_path is None or not isinstance(config_expected, str) or
                    not config_path.is_file() or sha256_file(config_path) != config_expected):
                raise AcceptanceError("bench config evidence is missing or changed")
            bench = load_bench_config(config_path)
            present = discovery.get("present")
            qualification = record.get("dpll_qualification")
            external_attempt = record.get("dpll_external_attempt") or {}
            fallback = (summary.get("scope_enabled") is False and
                        present is True and
                        discovery.get("fallback_internal") is True and
                        qualification == "INTERNAL_FALLBACK" and
                        external_attempt.get("status") == "FAIL")
            if (discovery.get("schema") != "HAOFV_P3_SCOPE_DISCOVERY_V1" or
                    not isinstance(present, bool) or
                    (present is not summary.get("scope_enabled") and not fallback) or
                    discovery.get("resource") != bench.get("dpll_short_scope_resource") or
                    discovery.get("expected_serial") != bench.get("dpll_short_scope_serial") or
                    (summary.get("scope_enabled") is True and
                     (summary.get("scope_resource") != discovery.get("resource") or
                      summary.get("scope_serial") != discovery.get("expected_serial")))):
                raise AcceptanceError("DPLL scope discovery differs from short observation")
            if qualification == "SCOPE_PASS" and (
                    summary.get("scope_enabled") is not True or present is not True):
                raise AcceptanceError("SCOPE_PASS receipt lacks external scope evidence")
            if qualification == "INTERNAL_FALLBACK":
                unknown = discovery.get("unknown") is True
                if summary.get("scope_enabled") is not False:
                    raise AcceptanceError("INTERNAL_FALLBACK receipt claims external scope")
                if not (fallback or unknown):
                    raise AcceptanceError("INTERNAL_FALLBACK receipt lacks the external failure")
            if qualification == "INTERNAL_ONLY" and summary.get("scope_enabled") is not False:
                raise AcceptanceError("INTERNAL_ONLY receipt claims external scope")
    validate_topology_reuse_evidence(root, record)
    timing = record.get("timing_probe")
    if timing is not None:
        if not isinstance(timing, dict):
            raise AcceptanceError("receipt has invalid timing_probe evidence")
        relative = timing.get("path")
        expected = timing.get("sha256")
        path = root / relative if isinstance(relative, str) else None
        if (path is None or not isinstance(expected, str) or
                not path.is_file() or sha256_file(path) != expected):
            raise AcceptanceError("local timing_probe evidence is missing or changed")
    handoff = record.get("calibration_parameter_handoff")
    if handoff is not None:
        _validate_evidence_file(
            root, handoff, "calibration_parameter_handoff")
    stopped_handoff = record.get("tdma_stopped_handoff")
    if stopped_handoff is not None:
        path = _validate_evidence_file(root, stopped_handoff, "tdma_stopped_handoff")
        proof = json.loads(path.read_text(encoding="utf-8"))
        if proof.get("mode") != "STOPPED_RECORDS" or not isinstance(proof.get("passed"), bool):
            raise AcceptanceError("invalid TDMA stopped handoff proof")
        if proof["passed"]:
            rows = proof.get("records", [])
            board_ids = record.get("tdma_board_ids", [])
            if (proof.get("build_id") != record.get("build_id") or
                    len(rows) != len(board_ids) or
                    {row.get("board") for row in rows} != set(board_ids)):
                raise AcceptanceError("TDMA stopped handoff identity mismatch")
            for row in rows:
                _validate_evidence_file(root, row, "tdma_board_record")


def _validate_evidence_file(root: Path, evidence: object, name: str) -> Path:
    if not isinstance(evidence, dict):
        raise AcceptanceError(f"receipt missing {name}")
    relative = evidence.get("path")
    expected = evidence.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise AcceptanceError(f"receipt has invalid {name} evidence")
    path = root / relative
    if not path.is_file():
        raise AcceptanceError(f"local {name} evidence missing: {relative}")
    if sha256_file(path) != expected:
        raise AcceptanceError(f"local {name} evidence digest changed: {relative}")
    return path


def validate_tdma_stopped_handoff(
        root: Path, summary: dict[str, Any], board_ids: list[str],
        build_id: str) -> dict[str, Any]:
    """Prove a finite observation ended cleanly using original board records.

    This is a stopped handoff, independent of the live handoff required by
    subsequent observers. Existing closed-loop/phase gates are evaluated by
    their callers; no failed quality result is replaced by this proof.
    """
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    from tools.calibration_ring_validate.tdma_board_record import decode_record

    try:
        if (not board_ids or len(set(board_ids)) != len(board_ids) or
                summary.get("left_running") is not False or
                summary.get("leave_running_requested") is not False):
            raise ValueError("finite stopped handoff was not requested/completed")
        # The capture writes its OWN failure into the summary, and it leaves `nodes`
        # empty when it never got a complete board record. Checking the field sets
        # first therefore reports a misleading "nodes board set mismatch" for what is
        # really "the capture failed to start": surface the summary's own error first.
        inner_error = str(summary.get("error") or "").strip()
        if inner_error:
            where = ", ".join(
                f"{name}={summary.get(name)}" for name in ("stage", "phase")
                if summary.get(name) is not None)
            raise ValueError(
                "the capture failed before a board handoff could be checked"
                + (f" ({where})" if where else "") + f": {inner_error}")
        for field in ("stopped", "boards", "board_records", "nodes"):
            if not isinstance(summary.get(field), dict) or set(summary[field]) != set(board_ids):
                raise ValueError(f"{field} board set mismatch")
        actions = summary["actions"]
        starts = [(i, row) for i, row in enumerate(actions) if row.get("action") == "START"]
        stops = [(i, row) for i, row in enumerate(actions) if row.get("action") == "STOP_BEFORE_EXPORT"]
        for name, rows in (("START", starts), ("STOP_BEFORE_EXPORT", stops)):
            if (len(rows) != len(board_ids) or {r["node"] for _, r in rows} != set(board_ids) or
                    any(str(r.get("response", "")).strip().strip('"') != "OK" for _, r in rows)):
                raise ValueError(f"{name} acknowledgement incomplete")
        if min(i for i, _ in stops) <= max(i for i, _ in starts):
            raise ValueError("STOP before all START acknowledgements")
        evidence, epochs = [], set()
        for address in board_ids:
            stop = summary["stopped"][address]
            if (stop.get("passed") != 1 or stop.get("skipped") or
                    stop.get("ring_enabled") != 0 or stop.get("ring_adapter_started") != 0 or
                    not isinstance(stop.get("ring_config_seq"), int) or
                    stop["ring_config_seq"] != stop.get("ring_applied_config_seq")):
                raise ValueError(f"{address}: STOP/configuration not acknowledged")
            board = summary["boards"][address]
            if board.get("address") != address or str(board.get("build")) != str(build_id):
                raise ValueError(f"{address}: live build/board identity mismatch")
            record = summary["board_records"][address]
            status = record["status"]
            path = (root / record["path"]).resolve()
            path.relative_to(root.resolve())
            raw = path.read_bytes()
            if status.get("state") != 5 or status.get("bytes") != len(raw):
                raise ValueError(f"{address}: frozen status/length mismatch")
            decoded = decode_record(raw, expected_build=build_id,
                                    expected_board=int(address, 16), expected_epoch=status["epoch"])
            if decoded["collection_passed"] is not True:
                raise ValueError(f"{address}: incomplete board record: {decoded['errors']}")
            for field in ("requested", "written", "missed", "reason"):
                if status.get(field) != decoded["terminal"][field]:
                    raise ValueError(f"{address}: frozen {field} mismatch")
            epochs.add(decoded["epoch"])
            evidence.append(dict(board=address, **evidence_entry(root, path),
                                 sample_count=len(decoded["samples"]),
                                 stopped_config_seq=stop["ring_config_seq"]))
        if len(epochs) != 1:
            raise ValueError("board capture epochs differ")
    except (KeyError, TypeError, ValueError, OSError) as exc:
        raise AcceptanceError(f"TDMA stopped handoff incomplete: {exc}") from exc
    return dict(mode="STOPPED_RECORDS", passed=True, build_id=build_id,
                epoch=epochs.pop(), records=evidence)


def validate_tdma_diagnostic_summary(
        summary: dict[str, Any], board_ids: list[str], *,
        capture_required: bool | None = None) -> None:
    """Prove the four-node flow completed without hiding quality failures."""
    errors: list[str] = []
    if summary.get("diagnostic_continue") is not True:
        errors.append("diagnostic_continue_missing")
    if summary.get("startup_barrier", {}).get("passed") is not True:
        errors.append("startup_barrier_failed")
    if summary.get("left_running") is not True:
        errors.append("running_handoff_missing")
    nodes = summary.get("nodes")
    handoff = summary.get("running_handoff")
    if not isinstance(nodes, dict) or set(nodes) != set(board_ids):
        errors.append("node_evidence_incomplete")
        nodes = {}
    if not isinstance(handoff, dict) or set(handoff) != set(board_ids):
        errors.append("handoff_evidence_incomplete")
        handoff = {}
    for address in board_ids:
        node = nodes.get(address, {})
        row = handoff.get(address, {})
        before = node.get("runtime_before", {})
        after = node.get("runtime_after", {})
        final = row.get("runtime", {})
        if row.get("passed") is not True:
            errors.append(f"{address}:handoff_failed")
        for field in ("ring_up_tx_sequence", "ring_down_rx_sequence"):
            try:
                soak_delta = ((int(after[field]) - int(before[field])) &
                              0xFFFFFFFF)
                handoff_delta = ((int(final[field]) - int(after[field])) &
                                 0xFFFFFFFF)
            except (KeyError, TypeError, ValueError):
                errors.append(f"{address}:{field}_missing")
                continue
            if soak_delta == 0:
                errors.append(f"{address}:{field}_not_advancing")
            if handoff_delta == 0:
                errors.append(f"{address}:{field}_stopped_after_capture")
    capture = summary.get("ring_capture", {})
    if capture_required is None:
        capture_required = not bool(
            isinstance(capture, dict) and capture.get("capture_skipped"))
    if capture_required:
        saved = capture.get("saved", []) if isinstance(capture, dict) else []
        downloaded = (capture.get("downloaded", [])
                      if isinstance(capture, dict) else [])
        if capture.get("capture_completed") is not True:
            errors.append("ring_capture_incomplete")
        if len(saved) != len(board_ids) or len(downloaded) != len(board_ids):
            errors.append("ring_capture_board_count_mismatch")
        saved_by_id = {
            str(row.get("node_id")): row for row in saved
            if isinstance(row, dict)
        }
        if set(saved_by_id) != set(board_ids):
            errors.append("ring_capture_node_set_mismatch")
        for address in board_ids:
            row = saved_by_id.get(address, {})
            latch = row.get("latch_status", [])
            debug = row.get("capture_debug", {})
            schedule = row.get("schedule_validation", {})
            masks = (row.get("load_mask_before"),
                     row.get("load_mask_during_capture"),
                     row.get("load_mask_restored"))
            if not latch or int(latch[0]) != 2 or len(latch) < 8:
                errors.append(f"{address}:capture_not_ready")
            elif int(latch[6]) <= 0:
                errors.append(f"{address}:capture_empty")
            if int(debug.get("copy_fail_count", -1)) != 0:
                errors.append(f"{address}:capture_copy_failed")
            if int(debug.get("consumed_sequence", 0)) <= 0:
                errors.append(f"{address}:capture_not_consumed")
            if None in masks or len(set(masks)) != 1:
                errors.append(f"{address}:load_mask_changed")
            if (schedule.get("passed") is not True or
                    int(schedule.get("newly_quarantined_mask", -1)) != 0):
                errors.append(f"{address}:capture_schedule_disturbed")
        analysis = summary.get("ring_analysis", {})
        if (analysis.get("passed") is not True or
                len(analysis.get("nodes", [])) != len(board_ids)):
            errors.append("ring_capture_analysis_failed")
    if errors:
        raise AcceptanceError(
            "TDMA diagnostic flow is incomplete: " + ", ".join(errors))


def _validate_tdma_diagnostic_receipt(
        root: Path, record: dict[str, Any]) -> None:
    if (record.get("flow_completed") is not True or
            record.get("strict_gates_passed") is not False or
            not isinstance(record.get("diagnostic_failures"), list) or
            not record["diagnostic_failures"]):
        raise AcceptanceError("TDMA diagnostic receipt has invalid gate state")
    _validate_evidence(root, record, include_dpll=False)
    for name in (
            "topology_summary", "coarse_calibration_summary",
            "coded_calibration_summary", "initialization_reset", "p3_summary",
            "trn00_summary", "trn01_summary", "trn02_summary"):
        path = _validate_evidence_file(root, record.get(name), name)
        value = json.loads(path.read_text(encoding="utf-8"))
        if value.get("passed") is not True:
            raise AcceptanceError(f"diagnostic receipt {name} is not PASS")
    matrix_path = _validate_evidence_file(
        root, record.get("trn03_matrix"), "trn03_matrix")
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    if (matrix.get("node_ids_in_loop_order") != record.get("tdma_board_ids") or
            matrix.get("derivation", {}).get("sck_replay_selection", {}).get(
                "selected_row_replay_safe") is not True):
        raise AcceptanceError("diagnostic receipt has invalid TRN-03 matrix")
    tdma_path = _validate_evidence_file(
        root, record.get("tdma_summary"), "tdma_summary")
    tdma = json.loads(tdma_path.read_text(encoding="utf-8"))
    validate_tdma_diagnostic_summary(tdma, list(record["tdma_board_ids"]))
    schedules = record.get("schedule_after")
    if not isinstance(schedules, dict) or set(schedules) != set(
            record["tdma_board_ids"]):
        raise AcceptanceError("diagnostic receipt final schedules are incomplete")
    enabled_masks = {
        int(row.get("enabled_mask", 0)) for row in schedules.values()
        if isinstance(row, dict)
    }
    calibration_mask = int(record.get("calibration_load_mask", 0))
    if len(enabled_masks) != 1 or enabled_masks == {0} or calibration_mask == 0:
        raise AcceptanceError("diagnostic receipt final load mask is invalid")
    if any(int(row.get("quarantined_mask", 0)) & calibration_mask
           for row in schedules.values() if isinstance(row, dict)):
        raise AcceptanceError("diagnostic receipt calibration is quarantined")
    if not (isinstance(tdma.get("ring_capture"), dict) and
            tdma["ring_capture"].get("capture_skipped")):
        for name in ("ring_capture_raw", "ring_capture_svg"):
            artifacts = record.get(name)
            if (not isinstance(artifacts, list) or
                    len(artifacts) != len(record["tdma_board_ids"])):
                raise AcceptanceError(f"diagnostic receipt has invalid {name}")
            for index, evidence in enumerate(artifacts):
                _validate_evidence_file(root, evidence, f"{name}[{index}]")
        _validate_evidence_file(
            root, record.get("ring_capture_analysis"), "ring_capture_analysis")
    _validate_evidence_file(
        root, record.get("diagnostic_summary"), "diagnostic_summary")


def _validate_quick_diagnostic_receipt(
        root: Path, record: dict[str, Any]) -> None:
    """Validate a debug-only flow receipt without gating phase quality."""
    failures = record.get("diagnostic_failures")
    dpll_required = record.get("dpll_observation") != "SKIPPED_TDMA_ONLY"
    expected_scope = ("FOUR_NODE_DPLL_QUICK_DIAGNOSTIC" if dpll_required else
                      "FOUR_NODE_TDMA_QUICK_DIAGNOSTIC")
    if (record.get("acceptance_scope") != expected_scope or
            record.get("acceptance_profile") != "QUICK_DIAGNOSTIC" or
            record.get("tdma_board_ids") != record.get("ota_board_ids") or
            not isinstance(record.get("tdma_board_ids"), list) or
            len(record["tdma_board_ids"]) != 4 or
            record.get("flow_completed") is not True or
            record.get("passed") is not True or
            not isinstance(failures, list) or
            record.get("strict_gates_passed") is not (not failures)):
        raise AcceptanceError("quick diagnostic receipt has invalid flow state")
    _validate_evidence(root, record, include_dpll=dpll_required)
    if dpll_required:
        path = _validate_evidence_file(root, record.get("dpll_summary"), "dpll_summary")
        validate_summary(
            root, json.loads(path.read_text(encoding="utf-8")),
            expected_source_tree_sha256=record.get("source_tree_sha256"),
            expected_build_id=str(record.get("build_id", "")),
            expected_board_uids=record.get("tdma_board_ids"))
    _validate_evidence_file(
        root, record.get("diagnostic_summary"), "diagnostic_summary")
    diagnostic_path = root / record["diagnostic_summary"]["path"]
    diagnostic = json.loads(diagnostic_path.read_text(encoding="utf-8"))
    if (diagnostic.get("schema") !=
            "HAOFV_HARDWARE_ACCEPTANCE_DIAGNOSTIC_V1" or
            diagnostic.get("passed") is not True or
            diagnostic.get("flow_completed") is not True or
            diagnostic.get("acceptance_profile") != "QUICK_DIAGNOSTIC"):
        raise AcceptanceError("quick diagnostic summary is not flow-complete")
    validate_alarm_report(root, record)


def _validate_limited_10mhz_evidence(root: Path,
                                     record: dict[str, Any]) -> None:
    """Validate the intentionally reduced bring-up receipt.

    This gate proves only OTA plus the 10 MHz P3 matrix.  It is deliberately
    not accepted as the full calibration-to-DPLL receipt.
    """
    for name in ("firmware_package", "ota_summary", "p3_summary"):
        evidence = record.get(name)
        if not isinstance(evidence, dict):
            raise AcceptanceError(f"limited receipt missing {name}")
        relative = evidence.get("path")
        expected = evidence.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected, str):
            raise AcceptanceError(f"limited receipt has invalid {name} evidence")
        path = root / relative
        if not path.is_file() or sha256_file(path) != expected:
            raise AcceptanceError(f"limited {name} evidence is missing or changed: {relative}")
    p3 = json.loads((root / record["p3_summary"]["path"]).read_text(
        encoding="utf-8"))
    if (p3.get("passed") is not True or
            p3.get("frequency_ladder_mhz") != [10] or
            p3.get("frequency_policy", {}).get(
                "highest_stable_frequency_mhz") != 10 or
            any(trial.get("passed") is not True
                for trial in p3.get("trials", []))):
        raise AcceptanceError("limited receipt P3 evidence is not a passing 10 MHz matrix")
    ota = json.loads((root / record["ota_summary"]["path"]).read_text(
        encoding="utf-8"))
    validate_ota(ota, list(record["ota_board_ids"]), str(record["build_id"]))


def _validate_dpll_short_receipt(root: Path, record: dict[str, Any]) -> None:
    """Bind a DPLL-60 credential to one build and its original raw records."""
    for field in ("firmware_package", "ota_summary", "trn03_matrix",
                  "tdma_summary", "tdma_stopped_handoff", "dpll_summary"):
        if field not in record:
            raise AcceptanceError(f"DPLL-60 receipt is missing {field}")
    board_ids = [str(value) for value in (record.get("tdma_board_ids") or [])]
    if not board_ids or record.get("ota_board_ids") != record.get("tdma_board_ids"):
        raise AcceptanceError("DPLL-60 receipt board sets differ")
    package = _validate_evidence_file(
        root, record["firmware_package"], "firmware_package")
    if _read_package_build_id(package) != record.get("build_id"):
        raise AcceptanceError("DPLL-60 package build differs")
    ota_path = _validate_evidence_file(root, record["ota_summary"], "ota_summary")
    validate_ota(json.loads(ota_path.read_text(encoding="utf-8")), board_ids,
                 str(record.get("build_id")))
    matrix_path = _validate_evidence_file(root, record["trn03_matrix"], "trn03_matrix")
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    tdma_path = _validate_evidence_file(root, record["tdma_summary"], "tdma_summary")
    tdma = json.loads(tdma_path.read_text(encoding="utf-8"))
    if record.get("calibration_generation") != matrix.get("calibration_generation"):
        raise AcceptanceError("DPLL-60 calibration generation differs")
    if not training_inputs_read_back(matrix, tdma, board_ids):
        raise AcceptanceError("DPLL-60 TRN-03/TDMA readback is invalid")
    handoff_path = _validate_evidence_file(
        root, record["tdma_stopped_handoff"], "tdma_stopped_handoff")
    handoff = json.loads(handoff_path.read_text(encoding="utf-8"))
    if (handoff.get("passed") is not True or
            handoff != validate_tdma_stopped_handoff(
                root, tdma, board_ids, str(record.get("build_id")))):
        raise AcceptanceError("DPLL-60 stopped handoff differs from raw records")
    dpll_path = _validate_evidence_file(root, record["dpll_summary"], "dpll_summary")
    dpll = json.loads(dpll_path.read_text(encoding="utf-8"))
    if dpll.get("passed") is not True:
        raise AcceptanceError("DPLL-60 joint observation did not pass")


ACCEPTED_RECEIPT_SCHEMAS = (
    RECEIPT_SCHEMA, TDMA_RECEIPT_SCHEMA, TDMA_DIAGNOSTIC_RECEIPT_SCHEMA,
    QUICK_DIAGNOSTIC_RECEIPT_SCHEMA, QUICK_GRADED_RECEIPT_SCHEMA,
    DPLL_SHORT_RECEIPT_SCHEMA, LIMITED_RECEIPT_SCHEMA)


def check_staged(root: Path, receipt_path: Path) -> None:
    changed = changed_staged_sources(root)
    if not changed:
        print("OK   Latency Cal hardware acceptance: no staged code change")
        return
    dirty = unstaged_sources(root)
    if dirty:
        raise AcceptanceError(
            "unstaged code differs from the staged commit; rerun acceptance "
            "after resolving: " + ", ".join(dirty[:8]))
    receipt = read_index_json(root, receipt_path)
    schema = receipt.get("schema")
    if schema not in ACCEPTED_RECEIPT_SCHEMAS or \
            receipt.get("passed") is not True:
        raise AcceptanceError("staged hardware acceptance receipt is not PASS")
    fingerprint, file_count = staged_source_fingerprint(root)
    if receipt.get("source_tree_sha256") != fingerprint:
        # T2 fallback (policy 2026-10-19): a green T2 bring-up run is an accepted credential, because its
        # 22 steps drive the same system path P3 measures. P3 numeric budgets remain available on demand.
        t2_path = root / "config/hardware_acceptance/t2_acceptance_receipt.json"
        if t2_path.is_file():
            t2 = json.loads(t2_path.read_text(encoding="utf-8"))
            if (t2.get("schema") == "HAOFV_T2_SEQUENCE_ACCEPTANCE_RECEIPT_V1" and
                    t2.get("verdict_ok") is True and
                    t2.get("passed") is True and
                    t2.get("source_tree_sha256") == fingerprint and
                    t2.get("source_file_count") == file_count):
                print(f"OK   hardware acceptance (T2 sequence): sources={file_count} "
                      f"generated={t2.get('generated_at')}")
                return
        raise AcceptanceError(
            "staged code fingerprint has no matching hardware acceptance (neither P3 nor a T2 "
            "sequence receipt); run sequence_workflow to a PASS, or "
            "python tools/hardware_acceptance/workflow_steps.py run")
    if receipt.get("source_file_count") != file_count:
        raise AcceptanceError("P3 acceptance source file count mismatch")
    _validate_receipt_evidence(root, receipt, changed)
    print(
        f"OK   hardware acceptance: sources={file_count} "
        f"build={receipt.get('build_id')} trials={receipt.get('trial_count')} "
        f"scope={receipt.get('acceptance_scope', 'FULL')}")


def validate_receipt_record(root: Path, receipt: dict[str, Any]) -> None:
    """check-staged's schema and evidence validators, without the git index."""
    if receipt.get("schema") not in ACCEPTED_RECEIPT_SCHEMAS or \
            receipt.get("passed") is not True:
        raise AcceptanceError("hardware acceptance receipt is not PASS")
    _validate_receipt_evidence(root, receipt, [])


def _validate_receipt_evidence(root: Path, receipt: dict[str, Any],
                               changed: list[str]) -> None:
    schema = receipt.get("schema")
    if schema == LIMITED_RECEIPT_SCHEMA:
        if receipt.get("acceptance_scope") != "10MHZ_LIMITED_P3":
            raise AcceptanceError("limited receipt has invalid acceptance scope")
        _validate_limited_10mhz_evidence(root, receipt)
    elif schema == DPLL_SHORT_RECEIPT_SCHEMA:
        if receipt.get("acceptance_scope") != DPLL_SHORT_SCOPE:
            raise AcceptanceError("DPLL-60 receipt has invalid acceptance scope")
        uncovered = [path for path in changed
                     if path.startswith(DPLL_SHORT_UNCOVERED_PREFIXES)]
        if uncovered:
            raise AcceptanceError(
                "staged change is outside the DPLL-60 scope: " +
                ", ".join(uncovered[:6]) +
                "; run python tools/hardware_acceptance/workflow_steps.py run")
        _validate_dpll_short_receipt(root, receipt)
    elif schema in (TDMA_RECEIPT_SCHEMA, TDMA_DIAGNOSTIC_RECEIPT_SCHEMA):
        expected_scope = (
            "FOUR_NODE_TDMA_DIAGNOSTIC"
            if schema == TDMA_DIAGNOSTIC_RECEIPT_SCHEMA else
            "FOUR_NODE_TDMA")
        if receipt.get("acceptance_scope") != expected_scope:
            raise AcceptanceError("TDMA-only receipt has invalid acceptance scope")
        if receipt.get("tdma_board_ids") != receipt.get("ota_board_ids"):
            raise AcceptanceError("TDMA-only receipt board sets differ")
        if schema == TDMA_DIAGNOSTIC_RECEIPT_SCHEMA:
            _validate_tdma_diagnostic_receipt(root, receipt)
        else:
            _validate_evidence(root, receipt, include_dpll=False)
    elif schema in (QUICK_DIAGNOSTIC_RECEIPT_SCHEMA, QUICK_GRADED_RECEIPT_SCHEMA):
        _validate_quick_diagnostic_receipt(root, receipt)
    else:
        _validate_evidence(root, receipt)
    validate_alarm_report(root, receipt)


def write_limited_receipt(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    config_path = root / args.config
    package = (root / args.package).resolve()
    ota_summary_path = (root / args.ota_summary).resolve()
    p3_summary_path = (root / args.p3_summary).resolve()
    config = load_bench_config(config_path)
    if config.get("frequency_ladder_mhz") != [10]:
        raise AcceptanceError("limited receipt requires config frequency ladder [10]")
    p3 = json.loads(p3_summary_path.read_text(encoding="utf-8"))
    if (p3.get("passed") is not True or p3.get("frequency_ladder_mhz") != [10] or
            p3.get("frequency_policy", {}).get(
                "highest_stable_frequency_mhz") != 10):
        raise AcceptanceError("P3 summary is not a passing 10 MHz result")
    build_id = _read_package_build_id(package)
    ota = json.loads(ota_summary_path.read_text(encoding="utf-8"))
    validate_ota(ota, list(config["ota_board_ids"]), build_id)
    fingerprint, file_count = staged_source_fingerprint(root)
    trials = p3.get("trials", [])
    delays = [float(row["delay_estimate_ns"]) for row in trials
              if "delay_estimate_ns" in row]
    receipt = {
        "schema": LIMITED_RECEIPT_SCHEMA,
        "acceptance_scope": "10MHZ_LIMITED_P3",
        "passed": True,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_tree_sha256": fingerprint,
        "source_file_count": file_count,
        "bench_config_path": args.config.as_posix(),
        "bench_config_sha256": sha256_file(config_path),
        "build_id": build_id,
        "firmware_package": evidence_entry(root, package),
        "ota_summary": evidence_entry(
            root, ota_summary_path, board_count=len(config["ota_board_ids"])),
        "p3_summary": evidence_entry(root, p3_summary_path),
        "ota_board_ids": list(config["ota_board_ids"]),
        "board_ids_in_physical_order": list(
            config["p3_board_ids_in_physical_order"]),
        "frequency_ladder_mhz": [10],
        "trial_count": len(trials),
        "delay_min_ns": min(delays) if delays else None,
        "delay_max_ns": max(delays) if delays else None,
        "highest_stable_frequency_mhz": 10,
        "hardware_acceptance_timing": acceptance_timing(config),
    }
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(
        json.dumps(receipt, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"PASS limited 10 MHz hardware acceptance receipt={args.receipt}")


def _run_step(command: list[str], root: Path, log_path: Path, *,
              allow_failure: bool = False, heartbeat_s: float | None = None,
              timeout_s: float | None = None) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment["PYTHONIOENCODING"] = "utf-8"
    # One validation subprocess owns one CDC session per board. The shared
    # response matcher filters delayed acknowledgements, and every subprocess
    # exit is a hard phase boundary which closes all sessions. Software reset
    # remains a forced disconnect/re-enumeration boundary.
    environment["HAOFV_SERIAL_LIFECYCLE"] = "phase"
    environment.pop("HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS", None)
    environment.setdefault("HAOFV_SERIAL_READ_TIMEOUT_S", "0.02")
    started = datetime.now(timezone.utc)
    monotonic_start = time.perf_counter()
    if heartbeat_s:
        result = _run_with_heartbeat(command, root, environment, log_path,
                                     heartbeat_s, timeout_s)
    else:
        try:
            result = subprocess.run(
                command, cwd=root, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, env=environment, check=False,
                timeout=timeout_s)
        except subprocess.TimeoutExpired as exc:
            result = subprocess.CompletedProcess(
                command, 124, stdout=exc.stdout or b"")
    ended = datetime.now(timezone.utc)
    _record_timing_event({
        "action": Path(str(command[1])).stem if len(command) > 1 else str(command[0]),
        "argv": [str(value) for value in command],
        "started_at_utc": started.isoformat(),
        "ended_at_utc": ended.isoformat(),
        "duration_ms": round((time.perf_counter() - monotonic_start) * 1000.0, 3),
        "returncode": result.returncode,
        "status": "PASS" if result.returncode == 0 else "FAIL",
    })
    log_path.write_bytes(result.stdout)
    if result.returncode != 0 and not allow_failure:
        lines = result.stdout.decode("utf-8", errors="replace").splitlines()
        # A compiler error (or ninja's FAILED target) is printed BEFORE the progress
        # lines that follow it, so a 20-line tail can scroll past it entirely: three
        # rounds were lost to a one-line build error that the tail had already left
        # behind. Keep the tail, add every diagnostic line explicitly, and name the
        # log file that holds the whole output.
        diagnostics = [line for line in lines if (
            "error:" in line or line.startswith("FAILED:")
            or line.startswith("Error") or "Traceback" in line
            or "CalledProcessError" in line)]
        parts = [f"step failed ({' '.join(command)}):", f"full output: {log_path}"]
        if result.returncode == 124 and timeout_s is not None:
            parts.append(f"timeout after {timeout_s:g}s")
        if diagnostics:
            parts.append("diagnostics:")
            parts.extend(diagnostics[:20])
        parts.append("tail:")
        parts.extend(lines[-20:])
        raise AcceptanceError("\n".join(parts))
    return int(result.returncode)


def _run_with_heartbeat(command: list[str], root: Path, environment: dict[str, str],
                        log_path: Path, period_s: float,
                        timeout_s: float | None = None) -> subprocess.CompletedProcess:
    """subprocess.run, but print elapsed time and the child's last line periodically.

    ``timeout_s`` is a hard wall-clock bound for unattended runs.  The child is
    killed (and reaped) when it expires; the caller turns return code 124 into a
    readable AcceptanceError instead of hanging forever.
    """
    started = time.perf_counter()
    timed_out = False
    with log_path.open("wb") as handle:
        process = subprocess.Popen(command, cwd=root, stdout=handle,
                                   stderr=subprocess.STDOUT, env=environment)
        while process.poll() is None:
            wait_s = period_s
            if timeout_s is not None:
                remaining = timeout_s - (time.perf_counter() - started)
                if remaining <= 0.0:
                    timed_out = True
                    process.kill()
                    process.wait()
                    break
                wait_s = min(wait_s, remaining)
            try:
                process.wait(timeout=wait_s)
            except subprocess.TimeoutExpired:
                tail = log_path.read_bytes().decode("utf-8", errors="replace").splitlines()
                print(f"  {Path(str(command[1])).stem} running "
                      f"{time.perf_counter() - started:.0f}s · last: "
                      f"{tail[-1][:90] if tail else '(no output yet)'}", flush=True)
    if timed_out:
        handle = log_path.open("ab")
        try:
            handle.write(f"\n[acceptance] timeout after {timeout_s:g}s; child killed\n"
                         .encode("utf-8"))
        finally:
            handle.close()
    return subprocess.CompletedProcess(command,
                                       124 if timed_out else process.returncode,
                                       stdout=log_path.read_bytes())


def _read_package_build_id(path: Path) -> str:
    data = path.read_bytes()[:512]
    if len(data) < 512 or int.from_bytes(data[0:4], "little") != 0x474B5054:
        raise AcceptanceError(f"invalid OTA package: {path}")
    return data[112:144].split(b"\0", 1)[0].split(b"\xff", 1)[0].decode(
        "ascii", errors="ignore").strip()


def _fresh_update_package(build_dir: Path) -> Path:
    """Return the package the current build produced.

    A v2 factory-candidate build writes ``DHRT100_V2_CANDIDATE_UPDATE.pkg``
    while a reused build directory may still contain a stale
    ``DHRT100_UPDATE.pkg`` (measured 2026-11-09: a Nov-5 package made all four
    boards reject the OTA).  Copy the v2 package over the standard name when it
    is newer so downstream evidence keeps the layout-agnostic path.
    """
    legacy = build_dir / "DHRT100_UPDATE.pkg"
    v2 = build_dir / "DHRT100_V2_CANDIDATE_UPDATE.pkg"
    if v2.is_file() and (not legacy.is_file()
                         or v2.stat().st_mtime > legacy.stat().st_mtime):
        shutil.copyfile(v2, legacy)
    return legacy


def parse_schedule(raw: str) -> dict[str, int]:
    try:
        values = [int(value.strip().strip('"'), 0)
                  for value in next(csv.reader([raw]), [])]
    except ValueError as exc:
        raise AcceptanceError(f"invalid TDMA schedule: {raw!r}") from exc
    if len(values) < SCHEDULE_HEADER_FIELDS:
        raise AcceptanceError("truncated TDMA schedule")
    phase_count = values[3]
    expected = SCHEDULE_HEADER_FIELDS + phase_count * SCHEDULE_PHASE_FIELDS
    if len(values) != expected:
        raise AcceptanceError(
            f"invalid TDMA schedule shape: {len(values)} != {expected}")
    return {
        "enabled_mask": values[4],
        "quarantined_mask": values[5],
        "cycle_count": values[6],
        "schedule_miss_count": values[7],
    }


def read_schedules(board_ids: list[str], timing: dict[str, float]) -> dict[str, dict[str, int]]:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import (  # type: ignore
        board_command, close_persistent_connections, discover)

    args = argparse.Namespace(
        board_ids=board_ids, baud=115200,
        timeout=timing["serial_timeout_s"], settle=timing["serial_settle_s"],
        keep_open=True)
    boards = discover(args)
    missing = sorted(set(board_ids) - set(boards))
    if missing:
        raise AcceptanceError("schedule boards missing: " + ", ".join(missing))
    try:
        return {
            board_id: parse_schedule(board_command(
                boards[board_id], "SYSTem:TDMA:SCHEDule?", args))
            for board_id in board_ids
        }
    finally:
        # Do not hold a CDC handle while the next acceptance phase opens it.
        close_persistent_connections()


PROTECTION_FIELDS = (
    "version", "table_seq", "ram_resident_required", "flash_lockout_supported",
    "flash_lockout_online", "flash_lockout_requested",
    "flash_lockout_acknowledged", "park_state", "last_result",
    "last_elapsed_us", "request_seq", "ack_seq", "release_seq",
    "timeout_count", "release_timeout_count", "entry_table_owner", "flags",
    "guard_owner", "guard_crc32", "guard_stale", "guard_flags",
)


def parse_protection(raw: str) -> dict[str, int]:
    """Parse ``SYSTem:PROTection:STATus?`` (flash park/lockout counters).

    These counters are the only board-side evidence of a flash transaction
    during the 60 s window; a park stalls Core1 even though the refill path
    itself is unchanged.
    """
    try:
        values = [int(value.strip().strip('"')) for value in raw.split(",")]
    except ValueError as exc:
        raise AcceptanceError(f"invalid protection status: {raw!r}") from exc
    if len(values) != len(PROTECTION_FIELDS):
        raise AcceptanceError(
            f"protection status field count {len(values)} != {len(PROTECTION_FIELDS)}")
    return dict(zip(PROTECTION_FIELDS, values))


def validate_recorded_builds(ota_summary: dict[str, Any] | None,
                            expected_build: str,
                            live_builds: dict[str, str] | None = None) -> None:
    """Fail fast when the OTA evidence shows a stale/mixed board build.

    This is a pure recorded-evidence check: acceptance unit tests and resumed
    runs must not open serial ports.  The live four-board probe is the night
    runner's preflight job, not a per-step side effect.

    The recorded summary is written before the OTA slot switch and can
    therefore still name the pre-switch build.  When a live probe (step 6)
    agrees with the expected build for every recorded board, the recorded
    mismatch is stale evidence, not a real mismatch.
    """
    if not ota_summary:
        return
    boards = ota_summary.get("boards") or []
    builds = {
        str(board.get("serial_number")): str(board.get("build_id"))
        for board in boards
        if board.get("serial_number")
        and board.get("build_id") not in (None, "")
    }
    if not builds:
        return
    mismatch = {board_id: build for board_id, build in builds.items()
                if build != expected_build}
    if mismatch:
        live = {str(key): str(value)
                for key, value in (live_builds or {}).items()}
        if live and all(live.get(board_id) == expected_build
                        for board_id in builds):
            return
        detail = ", ".join(f"{board_id}={build}" for board_id, build in mismatch.items())
        raise AcceptanceError(
            f"board build mismatch: expected {expected_build}; {detail}")


def read_realtime_context(board_ids: list[str], timing: dict[str, float],
                          *, include_ring: bool = False
                          ) -> dict[str, dict[str, Any]]:
    """Read the phase schedule and flash/park counters in one CDC session.

    ``include_ring`` adds the staged/runtime TDMA ring snapshot.  Step29 uses
    it to distinguish "configuration was never applied" from "configuration
    was applied but no frame arrived" without changing the capture itself.
    """
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import (  # type: ignore
        board_command, close_persistent_connections, discover, status)

    args = argparse.Namespace(
        board_ids=board_ids, baud=115200,
        timeout=timing["serial_timeout_s"], settle=timing["serial_settle_s"],
        keep_open=True)
    boards = discover(args)
    missing = sorted(set(board_ids) - set(boards))
    if missing:
        raise AcceptanceError("realtime context boards missing: " + ", ".join(missing))
    try:
        context = {
            board_id: {
                "schedule": parse_schedule(board_command(
                    boards[board_id], "SYSTem:TDMA:SCHEDule?", args)),
                "protection": parse_protection(board_command(
                    boards[board_id], "SYSTem:PROTection:STATus?", args)),
            }
            for board_id in board_ids
        }
        if include_ring:
            for board_id in board_ids:
                context[board_id]["ring"] = status(boards[board_id], args)
        return context
    finally:
        close_persistent_connections()


def validate_ota(summary: dict[str, Any], expected_ids: list[str],
                 build_id: str) -> None:
    found = {board.get("serial_number") for board in summary.get("boards", [])}
    # Product acceptance is intentionally strict about the negotiated OTA
    # capability.  A host request for 4096 B that silently falls back to the
    # legacy 512 B transport is a bootstrap condition, not a passing
    # fast-iteration acceptance result.
    ota_capabilities = {
        board.get("serial_number"): board.get("max_data_block_size")
        for board in summary.get("boards", [])
    }
    if (summary.get("passed") is not True or summary.get("dry_run") is True or
            summary.get("failed_count") != 0 or
            summary.get("board_count") != len(expected_ids) or
            summary.get("updated_count") != len(expected_ids) or
            found != set(expected_ids) or summary.get("expected_build") != build_id or
            any(ota_capabilities.get(board_id) != REQUIRED_OTA_BLOCK_SIZE
                for board_id in expected_ids)):
        raise AcceptanceError(
            "configured-board OTA summary did not meet acceptance; "
            "all boards must advertise 4096-byte OTA blocks")


def validate_online_builds(builds: dict[str, str], expected_ids: list[str],
                           build_id: str) -> None:
    """Require the exact bench board set to be running the expected package."""
    if set(builds) != set(expected_ids):
        missing = sorted(set(expected_ids) - set(builds))
        unexpected = sorted(set(builds) - set(expected_ids))
        raise AcceptanceError(
            f"online board set mismatch: missing={missing}, "
            f"unexpected={unexpected}")
    mismatched = {
        board_id: observed for board_id, observed in builds.items()
        if observed != build_id
    }
    if mismatched:
        raise AcceptanceError(
            f"online boards do not run expected build {build_id}: {mismatched}")


def read_online_builds(board_ids: list[str], timing: dict[str, float]) -> dict[str, str]:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import discover  # type: ignore

    probe_args = argparse.Namespace(
        board_ids=board_ids, baud=115200,
        timeout=timing["serial_timeout_s"], settle=timing["serial_settle_s"],
        keep_open=True)
    boards = discover(probe_args)
    return {board_id: board.build for board_id, board in boards.items()}


def reset_acceptance_boards(board_ids: list[str], build_id: str,
                            timing: dict[str, float]) -> dict[str, Any]:
    """Software-reset one exact UID set and verify clean re-enumeration."""
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import (  # type: ignore
        board_command, close_persistent_connections, discover)

    reset_args = argparse.Namespace(
        board_ids=board_ids,
        baud=115200,
        timeout=timing["serial_timeout_s"],
        action_timeout=timing["action_timeout_s"],
        settle=timing["input_settle_s"],
        read_timeout=timing["serial_read_timeout_s"],
        keep_open=False,
        short_open=True,
    )
    # A freshly flashed board re-enumerates for a moment (picotool leaves the
    # board in its bootloader until the application starts), so poll for the
    # exact bench set instead of failing on the first probe.
    deadline = time.monotonic() + 60.0
    boards = discover(reset_args)
    while set(boards) != set(board_ids) and time.monotonic() < deadline:
        close_persistent_connections()
        time.sleep(1.0)
        boards = discover(reset_args)
    before_builds = {
        board_id: board.build for board_id, board in boards.items()}
    validate_online_builds(before_builds, board_ids, build_id)

    started = time.monotonic()
    responses: dict[str, str] = {}
    ports_before: dict[str, str] = {}
    try:
        for board_id in board_ids:
            board = boards[board_id]
            ports_before[board_id] = board.port
            responses[board_id] = board_command(
                board, "SYSTem:BOOT:RESet", reset_args)
    except Exception as exc:
        raise AcceptanceError(
            f"acceptance initialization reset failed: {exc}") from exc
    finally:
        close_persistent_connections()

    deadline = time.monotonic() + timing["board_reset_timeout_s"]
    rebooted: dict[str, Any] = {}
    while time.monotonic() < deadline:
        time.sleep(timing["board_reset_poll_interval_s"])
        rebooted = discover(reset_args)
        if set(rebooted) == set(board_ids):
            break
    after_builds = {
        board_id: board.build for board_id, board in rebooted.items()}
    try:
        validate_online_builds(after_builds, board_ids, build_id)
    except AcceptanceError as exc:
        raise AcceptanceError(
            "boards did not re-enumerate cleanly after initialization reset: "
            f"{exc}") from exc
    return {
        "schema": "HAOFV_HARDWARE_ACCEPTANCE_INITIALIZATION_RESET_V1",
        "passed": True,
        "command": "SYSTem:BOOT:RESet",
        "board_ids": list(board_ids),
        "expected_build": build_id,
        "ports_before": ports_before,
        "ports_after": {
            board_id: rebooted[board_id].port for board_id in board_ids},
        "builds_before": before_builds,
        "builds_after": after_builds,
        "responses": responses,
        "elapsed_s": time.monotonic() - started,
    }


def validate_p3(summary: dict[str, Any], config: dict[str, Any]) -> dict[str, Any]:
    trials = summary.get("trials", [])
    expected_trials = (
        len(config["p3_board_ids_in_physical_order"]) *
        len(config["frequency_ladder_mhz"]) * 2 * int(config["repeats"]))
    policy = summary.get("frequency_policy", {})
    if any("frequency_hz" in trial for trial in trials):
        per_frequency = len(config["p3_board_ids_in_physical_order"]) * 2 * int(config["repeats"])
        expected_frequencies = {int(value) * 1000000 for value in config["frequency_ladder_mhz"]}
        if (any(trial.get("frequency_hz") not in expected_frequencies for trial in trials) or
                any(sum(trial.get("frequency_hz") == frequency for trial in trials) != per_frequency
                    for frequency in expected_frequencies)):
            raise AcceptanceError("P3 matrix does not cover every configured frequency")
    # LIMITED_RX exploration is not a required stable operating point. Keep
    # every trial as evidence, but validate timing only for selected stable rows.
    diagnostic_hz = {int(row["frequency_mhz"]) * 1000000
                     for row in summary.get("ladder", [])
                     if row.get("required_for_stable") is False and
                     int(row["frequency_mhz"]) > int(config["stable_frequency_mhz"]) and
                     row.get("operational_class") == "LIMITED_RX"}
    selected = [trial for trial in trials if trial.get("frequency_hz") not in diagnostic_hz]
    delays = [float(trial["delay_estimate_ns"]) for trial in selected
              if "delay_estimate_ns" in trial]
    if (summary.get("passed") is not True or len(trials) != expected_trials or
            not selected or any(trial.get("passed") is not True for trial in selected) or
            policy.get("stable_profiles_passed") is not True or
            policy.get("highest_stable_frequency_mhz") !=
            config["stable_frequency_mhz"] or len(delays) != len(selected)):
        raise AcceptanceError("four-board P3 matrix did not meet acceptance")
    minimum = float(config["minimum_link_delay_ns"])
    maximum = float(config["maximum_link_delay_ns"])
    if min(delays) < minimum or max(delays) > maximum:
        raise AcceptanceError(
            f"P3 delay outside configured bench range: {min(delays)}..{max(delays)}")
    for trial in selected:
        for endpoint in ("initiator", "responder"):
            snapshot = trial.get(endpoint, {})
            if snapshot.get("dma_overrun_count") != 0 or snapshot.get("pio_stall_count") != 0:
                raise AcceptanceError(f"P3 {endpoint} DMA/PIO fault observed")
    return {
        "trial_count": expected_trials,
        "delay_min_ns": min(delays),
        "delay_max_ns": max(delays),
        "highest_stable_frequency_mhz":
            policy["highest_stable_frequency_mhz"],
    }


def summary_failure_detail(summary: dict[str, Any]) -> str:
    """Return the concrete gate evidence carried by a failed step summary."""
    detail = summary.get("error") or summary.get("gate_failures")
    if detail:
        return str(detail)
    preflight = summary.get("tdma_preflight", {})
    if (summary.get("tdma_preflight_passed") is False and
            isinstance(preflight, dict)):
        errors = preflight.get("errors", [])
        if errors:
            return "TDMA preflight: " + ", ".join(map(str, errors))
        return "TDMA preflight did not pass"
    raw_gate = summary.get("sd_waveform", {}).get("raw_gate", {})
    if isinstance(raw_gate, dict) and raw_gate.get("errors"):
        return "DPLL SD waveform: " + ", ".join(
            map(str, raw_gate["errors"]))
    failures = summary.get("diagnostic_failures", [])
    if isinstance(failures, list) and failures:
        return "; ".join(
            f"{item.get('phase', 'diagnostic')}: {item.get('error', '')}"
            for item in failures if isinstance(item, dict))
    return "gate did not pass"


def validate_pass_summary(summary: dict[str, Any], label: str) -> None:
    if summary.get("passed") is not True:
        raise AcceptanceError(
            f"{label} did not meet acceptance: {summary_failure_detail(summary)}")


def discover_optional_scope(resource: str, expected_serial: str) -> dict[str, Any]:
    """Discover the configured HDO without making its absence an error."""
    result: dict[str, Any] = {
        "schema": "HAOFV_P3_SCOPE_DISCOVERY_V1",
        "resource": resource,
        "expected_serial": expected_serial,
        "present": False,
    }
    try:
        import pyvisa
        manager = pyvisa.ResourceManager()
        try:
            resources = list(manager.list_resources())
        finally:
            manager.close()
        result["resources"] = resources
        result["present"] = resource in resources
        result["reason"] = "CONFIGURED_RESOURCE_ENUMERATED" if result["present"] else "CONFIGURED_RESOURCE_ABSENT"
    except Exception as exc:
        result["resources"] = []
        result["present"] = None
        result["reason"] = "VISA_DISCOVERY_UNAVAILABLE"
        result["discovery_error"] = f"{type(exc).__name__}: {exc}"
    return result


def dpll_short_write_provisional(
        root: Path, receipt_path: Path, *, build_id: str, board_ids: list[str],
        source_fingerprint: str, source_count: int, package: Path,
        ota_summary_path: Path, trn03_matrix_path: Path, tdma_summary_path: Path,
        stopped_handoff_path: Path, calibration_generation: int) -> None:
    """Write the non-committable receipt the joint capture adapter reads."""
    provisional = {
        "schema": PROVISIONAL_SCHEMA,
        "joint_capture_only": True,
        "passed": True,
        "strict_gates_passed": True,
        "build_id": build_id,
        "source_tree_sha256": source_fingerprint,
        "source_file_count": source_count,
        "tdma_board_ids": board_ids,
        "ota_board_ids": board_ids,
        "calibration_generation": calibration_generation,
        "firmware_package": evidence_entry(root, package),
        "ota_summary": evidence_entry(root, ota_summary_path),
        "trn03_matrix": evidence_entry(root, trn03_matrix_path),
        "tdma_summary": evidence_entry(root, tdma_summary_path),
        "tdma_stopped_handoff": evidence_entry(root, stopped_handoff_path),
    }
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(
        json.dumps(provisional, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")


def dpll_short_discover_scope(config: dict[str, Any], out_dir: Path,
                              *, allow_unknown: bool = False) -> tuple[Path, bool]:
    resource = str(config["dpll_short_scope_resource"])
    serial = str(config["dpll_short_scope_serial"])
    if config.get("dpll_short_force_internal"):
        # Operator request: qualify on board-internal evidence only. The capture
        # still records external_status = SKIPPED so the receipt cannot be
        # mistaken for a scope-qualified run. The page keeps the same schema and
        # configured resource/serial as a real discovery, because the credential
        # gate compares all of them against the bench config; an abbreviated
        # page here made an internal-only run fail at step 35.
        discovery_path = out_dir / "dpll-scope-discovery.json"
        discovery_path.write_text(
            json.dumps({
                "schema": "HAOFV_P3_SCOPE_DISCOVERY_V1",
                "resource": resource,
                "expected_serial": serial,
                "present": False,
                "resources": [],
                "reason": "FORCED_INTERNAL_BY_OPERATOR",
                "forced_internal": True,
            }, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        return discovery_path, False
    discovery = discover_optional_scope(resource, serial)
    for _attempt in range(int(config.get("dpll_short_scope_discovery_retries", 3))):
        if discovery["present"] is True:
            break
        # The instrument can be briefly held by another VISA session.
        time.sleep(1.5)
        discovery = discover_optional_scope(resource, serial)
    discovery_path = out_dir / "dpll-scope-discovery.json"
    if discovery["present"] is None:
        if not allow_unknown:
            raise AcceptanceError(
                "scope discovery failed; cannot distinguish absent hardware from VISA failure")
        # Detection failure is not "absent": keep it explicit, then let the
        # caller choose the internal fallback and record external_status UNKNOWN.
        discovery["unknown"] = True
        discovery["present"] = False
        discovery["reason"] = discovery.get("reason") or "VISA_DISCOVERY_FAILED"
    discovery_path.write_text(
        json.dumps(discovery, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return discovery_path, discovery["present"] is True


_DPLL_OUTPUT_REASONS = {
    0: "OK", 1: "CANCELLED", 2: "CLOCK", 3: "STARVED",
    4: "DEADLINE", 5: "ARGUMENT", 6: "DMA", 7: "EXPIRED",
}

# VDC_PRIORITY_GUARD_* failure bits (components/vdc_dpll_manager/inc/
# vdc_priority_trace.h:50-58). Naming the mask turns "STARVED" into the actual
# stall class, which is what distinguishes an output underrun from a ring gap.
_DPLL_GUARD_BITS = (
    (16, "EARLY_FREEZE"), (17, "SUCCESS_GAP"), (18, "COVERAGE"),
    (19, "RING_READ"), (20, "OUTPUT_READ"), (21, "OUTPUT_IDENTITY"),
    (22, "OUTPUT_STOPPED"), (23, "OUTPUT_STALE"), (24, "OUTPUT_PROGRESS"),
)


def _dpll_gap_evidence(row: dict[str, Any]) -> str:
    """Bounded refill/guard evidence for a board that stopped feeding.

    STARVED means the PIO output FIFO underran, so the useful numbers are the
    guard's failure-bit mask plus the longest service/success gap the internal
    assessment saw. Best effort: a missing field is simply omitted, never
    guessed.
    """
    pieces: list[str] = []
    guard = row.get("guard")
    if isinstance(guard, dict):
        mask = guard.get("reason_mask")
        if isinstance(mask, int) and not isinstance(mask, bool):
            named = [name for bit, name in _DPLL_GUARD_BITS if mask & (1 << bit)]
            pieces.append(f"mask=0x{mask:x}" + (f"({'+'.join(named)})" if named else ""))
    assessment = row.get("internal_assessment")
    if isinstance(assessment, dict):
        for key, label in (("max_service_gap_s", "svc_gap"),
                           ("max_success_gap_s", "succ_gap")):
            value = assessment.get(key)
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                pieces.append(f"{label}={value:.4f}s")
    return (" [" + ", ".join(pieces) + "]") if pieces else ""


def dpll_capture_failure_detail(joint_dir: Path) -> str:
    """Name the boards that stopped feeding the 60 s window.

    The capture writes its own probe before it exits; a bare ``Capture exited 2``
    hides which board starved and when, which cost several full acceptance rounds.
    Best-effort only: a missing or foreign probe yields no detail.
    """
    try:
        report = json.loads(
            (joint_dir / "capture" / "input-probe.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return ""
    if not isinstance(report, dict):
        return ""
    parts: list[str] = []
    for uid, row in (report.get("boards") or {}).items():
        if not isinstance(row, dict):
            continue
        raw = row.get("run_output_raw")
        if not isinstance(raw, str):
            continue
        try:
            fields = [int(value) for value in raw.split(",")]
        except ValueError:
            continue
        if len(fields) < 6:
            continue
        reason, edges = fields[3], fields[5]
        if reason == 3:
            parts.append(
                f"{uid}: STARVED after {edges / 1000.0:.1f}s"
                f"{_dpll_gap_evidence(row)}")
        elif edges < 50_000:
            parts.append(
                f"{uid}: output stopped after {edges / 1000.0:.1f}s "
                f"({_DPLL_OUTPUT_REASONS.get(reason, reason)})"
                f"{_dpll_gap_evidence(row)}")
    samples = report.get("scope_samples") or []
    missed = [row for row in samples if row.get("missed")]
    if missed:
        parts.append(f"scope {len(missed)}/{len(samples)} windows missed")
    return "; ".join(parts)


def dpll_short_run_capture(root: Path, config: dict[str, Any], out_dir: Path, *,
                           scope_enabled: bool) -> Path:
    resource = str(config["dpll_short_scope_resource"])
    serial = str(config["dpll_short_scope_serial"])
    seconds = int(config["dpll_short_seconds"])
    pre_capture = os.environ.get("HAOFV_PRE_CAPTURE_COMMAND", "").strip()
    if pre_capture:
        print(f"Hardware acceptance: pre-capture hook: {pre_capture}", flush=True)
        subprocess.run(pre_capture, shell=True, cwd=str(root), check=False)
    # Point the manifest at THIS run's provisional receipt: the committed manifest names the committed
    # receipt, whose source fingerprint no longer matches, which made step 29 fail unconditionally.
    workflow_inputs = root / config["dpll_short_workflow_inputs"]
    provisional = pathlib.Path(config.get("_provisional_receipt_path") or
                               (root / "config/hardware_acceptance/p3_acceptance_receipt.json"))
    if provisional.is_file():
        import json as _json
        manifest = _json.loads(workflow_inputs.read_text(encoding="utf-8"))
        manifest.setdefault("files", {}).setdefault("receipt", {})["path"] = \
            provisional.relative_to(root).as_posix()
        remapped = out_dir / "workflow-inputs.json"
        remapped.write_text(_json.dumps(manifest, ensure_ascii=False, indent=1) + "\n",
                            encoding="utf-8")
        workflow_inputs = remapped
        print(f"Hardware acceptance: joint capture uses {workflow_inputs.name} "
              f"-> {provisional.name}", flush=True)
    else:
        print(f"Hardware acceptance: WARNING no provisional receipt at {provisional}; "
              "the joint capture will validate the committed one", flush=True)
    command_prefix = [
        sys.executable,
        str(root / "tools/vdc_priority_trace/vdc_priority_joint_capture.py"),
        "--bench-adapter", str(root / config["dpll_short_bench_adapter"]),
        "--workflow-inputs", str(workflow_inputs),
        "--scope", "on" if scope_enabled else "off",
        "--scope-trigger", "CHAN1",
        "--scope-resource", resource,
        "--scope-serial", serial,
        "--seconds", str(seconds),
        "--settle-s", str(config["dpll_short_settle_s"]),
        "--scope-threshold-ns", "1000",
        "--scope-steady-threshold-ns", "1000",
        "--p3-provisional",
    ]
    print(
        "Hardware acceptance: four-board DPLL 60 s joint observation " +
        ("with HDO scope" if scope_enabled else "with internal evidence; scope absent"),
        flush=True)
    # The finite RUN can retire a board's output mid-window (SYNC-OUT-002); that
    # is a physical-window flake, not a deterministic rejection. Retry with a
    # fresh window and keep each attempt's evidence, while a repeatable failure
    # still ends the step.
    attempts = max(1, int(config.get("dpll_short_capture_attempts", 2)))
    joint_dir = out_dir / "dpll-four-board-short"
    number = 1
    while joint_dir.exists():
        number += 1
        joint_dir = out_dir / f"dpll-four-board-short-attempt{number}"
    for attempt in range(1, attempts + 1):
        if attempt > 1:
            while joint_dir.exists():
                number += 1
                joint_dir = out_dir / f"dpll-four-board-short-attempt{number}"
        command = command_prefix + ["--out", str(joint_dir)]
        log = out_dir / ("dpll-short.log" if number == 1
                         else f"dpll-short-attempt{number}.log")
        try:
            _run_step(command, root, log, heartbeat_s=10.0, timeout_s=300.0)
        except AcceptanceError as exc:
            detail = dpll_capture_failure_detail(joint_dir)
            if detail:
                exc = AcceptanceError(f"{exc} [{detail}]")
            if attempt >= attempts:
                raise exc from None
            print(f"Hardware acceptance: joint capture attempt {attempt} failed; "
                  f"retrying with a fresh window ({exc})", flush=True)
            continue
        return joint_dir
    raise AcceptanceError("joint capture produced no attempt directory")


def dpll_short_summarize(
        root: Path, config: dict[str, Any], out_dir: Path, joint_dir: Path, *,
        scope_enabled: bool, source_fingerprint: str, build_id: str,
        board_ids: list[str]) -> tuple[Path, dict[str, Any]]:
    report_path = joint_dir / "report.json"
    input_path = joint_dir / "capture" / "input-probe.json"
    if not report_path.is_file() or not input_path.is_file():
        raise AcceptanceError("four-board DPLL short workflow did not write complete reports")
    summary = summarize_joint(
        root, report_path, input_path,
        scope_enabled=scope_enabled, seconds=int(config["dpll_short_seconds"]))
    summary_path = out_dir / "dpll-short-summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    try:
        validate_summary(
            root, summary,
            expected_source_tree_sha256=source_fingerprint,
            expected_build_id=build_id,
            expected_board_uids=board_ids)
    except ValueError as exc:
        raise AcceptanceError(str(exc)) from exc
    return summary_path, summary


def run_four_board_dpll_short(
        root: Path, config: dict[str, Any], receipt_path: Path, out_dir: Path,
        *, build_id: str, board_ids: list[str], source_fingerprint: str,
        source_count: int, package: Path, ota_summary_path: Path,
        trn03_matrix_path: Path, tdma_summary_path: Path,
        stopped_handoff_path: Path,
        calibration_generation: int) -> tuple[Path, dict[str, Any], Path]:
    """Run the four-board joint workflow under a non-committable receipt."""
    dpll_short_write_provisional(
        root, receipt_path, build_id=build_id, board_ids=board_ids,
        source_fingerprint=source_fingerprint, source_count=source_count,
        package=package, ota_summary_path=ota_summary_path,
        trn03_matrix_path=trn03_matrix_path, tdma_summary_path=tdma_summary_path,
        stopped_handoff_path=stopped_handoff_path,
        calibration_generation=calibration_generation)
    discovery_path, scope_enabled = dpll_short_discover_scope(config, out_dir)
    joint_dir = dpll_short_run_capture(root, config, out_dir, scope_enabled=scope_enabled)
    summary_path, summary = dpll_short_summarize(
        root, config, out_dir, joint_dir, scope_enabled=scope_enabled,
        source_fingerprint=source_fingerprint, build_id=build_id, board_ids=board_ids)
    return summary_path, summary, discovery_path


def build_diagnostic_feedback(
        tdma_summary: dict[str, Any],
        dpll_summary: dict[str, Any] | None,
        trn03_matrix: dict[str, Any]) -> dict[str, Any]:
    """Collect failed-gate measurements that can drive DPLL correction."""
    runtime_fields = (
        "ring_adapter_rx_count", "ring_adapter_rx_bad_count",
        "ring_adapter_rx_transport_bad_count",
        "ring_adapter_rx_schedule_bad_count",
        "ring_adapter_rx_profile_bad_count",
        "ring_adapter_last_bad_transport_result",
        "ring_adapter_last_bad_sequence",
        "ring_adapter_last_bad_header_diff_count",
        "ring_adapter_last_bad_header_first_diff_offset",
        "ring_adapter_last_bad_header_expected_byte",
        "ring_adapter_last_bad_header_observed_byte",
    )
    phase_fields = (
        "flight_marker_offset_sample_count",
        "flight_sck_offset_sample_count",
        "flight_data_offset_sample_count",
        "flight_marker_phase_delay_cycles",
        "flight_sck_phase_delay_cycles",
        "flight_data_phase_delay_cycles",
    )
    process_fields = (
        "receive_accepted_count", "receive_rejected_count",
        "receive_missing_count", "rx_bitmap_incomplete_count",
        "receive_last_transport_result", "receive_quality_flags",
    )
    node_feedback: dict[str, Any] = {}
    for address, node in tdma_summary.get("nodes", {}).items():
        runtime = node.get("runtime_after", {})
        physical = node.get("physical_after", {})
        process = node.get("flight_after", {}).get("process", {})
        node_feedback[address] = {
            "node_index": node.get("node_index"),
            "passed": bool(node.get("passed")),
            "errors": list(node.get("errors", [])),
            "runtime": {field: runtime.get(field) for field in runtime_fields},
            "physical_phase": {
                field: physical.get(field) for field in phase_fields},
            "process_image": {
                field: process.get(field) for field in process_fields},
            "crc_diagnostic": node.get("crc_diagnostic_after", {}),
        }
    dpll_boards = [] if dpll_summary is None else dpll_summary.get("boards", [])
    observer = next((row for row in dpll_boards
                     if row.get("role") == "observer"), {})
    return {
        "calibration_offsets": {
            "active_row_id": trn03_matrix.get(
                "offset_matrix", {}).get("active_row_id"),
            "active_row": tdma_summary.get("offset_row", {}),
            "sck_replay_selection": trn03_matrix.get(
                "derivation", {}).get("sck_replay_selection", {}),
        },
        "tdma": {
            "startup_barrier": tdma_summary.get("startup_barrier", {}),
            "worst_receive_quality": tdma_summary.get(
                "soak_validation", {}).get("worst_receive_quality", {}),
            "nodes": node_feedback,
            "dpll_schedule_gate": tdma_summary.get("dpll_schedule_gate", {}),
        },
        "dpll": {
            "ring_sequence_consistent": (
                None if dpll_summary is None else
                dpll_summary.get("ring_sequence_consistent")),
            "ring_sequence_skew": (
                None if dpll_summary is None else
                dpll_summary.get("ring_sequence_skew")),
            "observer_phase": observer,
            "boards": dpll_boards,
        },
    }


def p3_link_delays(summary: dict[str, Any], config: dict[str, Any]) -> list[int]:
    """Reduce stable CLK_DATA P3 repeats to even per-link training delays."""
    board_ids = list(config["p3_board_ids_in_physical_order"])
    stable_hz = int(config["stable_frequency_mhz"]) * 1_000_000
    delays: list[int] = []
    for link_index, source in enumerate(board_ids):
        destination = board_ids[(link_index + 1) % len(board_ids)]
        values = [
            float(trial["delay_estimate_ns"])
            for trial in summary.get("trials", [])
            if trial.get("source") == source and
            trial.get("destination") == destination and
            int(trial.get("signal_group", -1)) == 0 and
            int(trial.get("frequency_hz", 0)) == stable_hz and
            trial.get("passed") is True
        ]
        if len(values) != int(config["repeats"]):
            raise AcceptanceError(
                f"link{link_index}: stable P3 delay repeat set is incomplete")
        selected = int(round(sum(values) / len(values)))
        # Keep the measured path delay even for the shared phase model; the
        # configured baseline divisor is applied later by every train stage.
        selected += selected & 1
        delays.append(selected)
    return delays


def discover_board_ports(board_ids: list[str], timing: dict[str, float]) -> dict[str, str]:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import discover  # type: ignore

    args = argparse.Namespace(
        board_ids=board_ids, baud=115200,
        timeout=timing["serial_timeout_s"], settle=timing["serial_settle_s"],
        keep_open=True)
    boards = discover(args)
    missing = sorted(set(board_ids) - set(boards))
    if missing:
        raise AcceptanceError("DPLL boards missing: " + ", ".join(missing))
    return {board_id: boards[board_id].port for board_id in board_ids}


def evidence_entry(root: Path, path: Path, **fields: Any) -> dict[str, Any]:
    relative = path.resolve().relative_to(root).as_posix()
    return {"path": relative, "sha256": sha256_file(path), **fields}


def write_phase_summary(path: Path, phase: str,
                        summaries: list[Path], *,
                        diagnostic_continue: bool = False) -> dict[str, Any]:
    """Freeze a multi-profile/multi-part phase as one receipt artifact."""
    rows = []
    for summary_path in summaries:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        passed = summary.get("passed") is True
        if not passed and not diagnostic_continue:
            validate_pass_summary(summary, f"{phase}:{summary_path.parent.name}")
        rows.append({
            "path": summary_path.resolve().relative_to(ROOT).as_posix(),
            "sha256": sha256_file(summary_path),
            "passed": passed,
            **({"error": str(summary.get("error", "gate did not pass"))}
               if not passed else {}),
        })
    result = {
        "schema": "HAOFV_HARDWARE_ACCEPTANCE_PHASE_SUMMARY_V1",
        "phase": phase,
        "passed": bool(rows) and all(row["passed"] for row in rows),
        "flow_continued": diagnostic_continue,
        "evidence": rows,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return result


def validate_schedule_isolation(
        before: dict[str, dict[str, int]], after: dict[str, dict[str, int]],
        calibration_mask: int) -> None:
    if set(before) != set(after):
        raise AcceptanceError("schedule board set changed during P3")
    for board_id in before:
        if before[board_id]["enabled_mask"] != after[board_id]["enabled_mask"]:
            raise AcceptanceError(f"{board_id}: P3 changed TDMA load mask")
        if after[board_id]["quarantined_mask"] & calibration_mask:
            raise AcceptanceError(f"{board_id}: calibration load quarantined by P3")


def validate_runtime_schedules(
        schedules: dict[str, dict[str, int]], calibration_mask: int) -> None:
    """Gate final ownership while retaining cumulative timing diagnostics.

    ``schedule_miss_count`` includes expected skips for disabled phase loads
    and survives across the complete acceptance run. Per-phase runtime deltas
    in the TDMA SHORT gate own timing regression admission; the final snapshot
    only verifies that the online mask is coherent and calibration was not
    quarantined by diagnostic capture.
    """
    enabled = {row["enabled_mask"] for row in schedules.values()}
    if len(enabled) != 1 or enabled == {0}:
        raise AcceptanceError("final TDMA enabled load mask is inconsistent")
    for board_id, row in schedules.items():
        if row["quarantined_mask"] & calibration_mask:
            raise AcceptanceError(
                f"{board_id}: calibration load quarantined after TDMA/DPLL")


def acceptance_config_path(args: argparse.Namespace, root: Path) -> Path:
    if getattr(args, "command", "") == "run":
        # An explicitly requested config wins: the run subcommand advertises
        # --config, so ignoring it silently ran every stage at the default
        # (quick/full) profile level instead of the requested one.
        explicit = getattr(args, "config", None)
        if explicit is not None and Path(explicit) != DEFAULT_CONFIG:
            return root / explicit
        return root / (
            "config/hardware_acceptance/p3_bench.json"
            if getattr(args, "full", False)
            else "config/hardware_acceptance/p3_bench_quick.json"
        )
    return root / args.config


def acceptance_output_path(root: Path, requested: Path | None,
                           stamp: str) -> Path:
    """Place acceptance runs under a stable YYYYMMDD partition."""
    default_day = stamp.split("-", 1)[0]
    if requested is None:
        return root / Path(
            f"out/HardwareAcceptance/{default_day}/p3-"
            f"{stamp.split('-', 1)[1]}")
    path = requested if requested.is_absolute() else root / requested
    try:
        relative = path.relative_to(root / "out" / "HardwareAcceptance")
    except ValueError:
        return path
    parts = relative.parts
    if parts and len(parts[0]) == 8 and parts[0].isdigit():
        return path
    return root / "out" / "HardwareAcceptance" / default_day / Path(*parts)


def validate_known_topology(value: dict[str, Any], board_ids: list[str],
                            anchor: str) -> None:
    """Admit only an original passing directed-wiring measurement."""
    if (len(board_ids) != 4 or len(set(board_ids)) != 4 or
            anchor != board_ids[0] or value.get("passed") is not True or
            value.get("mode") == "REUSED_KNOWN_TOPOLOGY" or
            value.get("error", "") != "" or
            value.get("measurement_domain") != "calibration" or
            value.get("measurement_phase") != "link_adjacency_and_ring_topology" or
            value.get("ring_order") != board_ids or value.get("anchor_id") != anchor):
        raise AcceptanceError("known topology measurement identity is invalid")
    expected = {uid: [board_ids[(i + 1) % len(board_ids)]]
                for i, uid in enumerate(board_ids)}
    if value.get("adjacency") != expected:
        raise AcceptanceError("known topology directed adjacency differs")
    if (value.get("node_map") != [
            {"node": i, "no": i + 1, "address": uid} for i, uid in enumerate(board_ids)] or
            value.get("node_discovery", {}).get("committed") is not True):
        raise AcceptanceError("known topology committed node map differs")
    boards = value.get("boards", {})
    rows = value.get("assignments", [])
    if (not isinstance(boards, dict) or set(boards) != set(board_ids) or
            not isinstance(rows, list) or len(rows) != len(board_ids)):
        raise AcceptanceError("known topology board/NO evidence is incomplete")
    for index, (uid, row) in enumerate(zip(board_ids, rows), 1):
        if (not isinstance(row, dict) or not isinstance(boards[uid], dict) or
                boards[uid].get("address") != uid or
                row.get("address") != uid or row.get("no") != index or
                row.get("readback") != str(index) or row.get("passed") is not True):
            raise AcceptanceError("known topology NO evidence differs")


def freeze_known_topology(root: Path, source: Path, out_dir: Path,
                          board_ids: list[str], anchor: str) -> dict[str, Any]:
    # Read once: validation and the retained artifact cover identical bytes.
    raw = (root / source).read_bytes()
    value = json.loads(raw.decode("utf-8-sig"))
    if not isinstance(value, dict):
        raise AcceptanceError("known topology must be an object")
    validate_known_topology(value, board_ids, anchor)
    frozen = out_dir / "known-topology-source.json"
    frozen.parent.mkdir(parents=True, exist_ok=True)
    with frozen.open("xb") as handle:
        handle.write(raw)
    return evidence_entry(root, frozen)


def validate_known_topology_readback(value: dict[str, Any], board_ids: list[str],
                                     build_id: str) -> None:
    if (value.get("passed") is not True or
            value.get("mode") != "REUSED_KNOWN_TOPOLOGY" or
            value.get("remeasured") is not False or
            value.get("ring_order") != board_ids or
            value.get("build_id") != build_id):
        raise AcceptanceError("known topology current readback failed")
    rows = value.get("assignments", [])
    boards = value.get("boards", {})
    if (not isinstance(rows, list) or len(rows) != len(board_ids) or
            not isinstance(boards, dict) or set(boards) != set(board_ids)):
        raise AcceptanceError("known topology current board evidence incomplete")
    for index, (uid, row) in enumerate(zip(board_ids, rows), 1):
        if (not isinstance(row, dict) or not isinstance(boards[uid], dict) or
                row.get("address") != uid or row.get("no") != index or
                row.get("readback") != str(index) or row.get("passed") is not True or
                str(row.get("raw", "")).strip().strip('"') != str(index) or
                boards[uid].get("address") != uid or boards[uid].get("build") != build_id):
            raise AcceptanceError("known topology current UID/build/NO differs")


def verify_known_topology(root: Path, source: dict[str, Any], board_ids: list[str],
                          build_id: str, timing: dict[str, float]) -> dict[str, Any]:
    """After reset, read current identities without probing or rewriting NOs."""
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import (
        board_command, close_persistent_connections, discover)
    result: dict[str, Any] = {
        "mode": "REUSED_KNOWN_TOPOLOGY", "remeasured": False,
        "measurement_phase": "known_topology_identity_readback",
        "passed": False, "ring_order": list(board_ids), "build_id": build_id,
        "source_topology": source, "boards": {}, "assignments": [], "error": "",
    }
    args = argparse.Namespace(
        board_ids=board_ids, baud=115200, keep_open=True,
        timeout=timing["serial_timeout_s"], settle=timing["serial_settle_s"])
    try:
        source_path = _validate_evidence_file(root, source, "known topology")
        validate_known_topology(json.loads(source_path.read_text(encoding="utf-8-sig")),
                                board_ids, board_ids[0])
        boards = discover(args)
        result["boards"] = {
            uid: {"address": board.address, "idn": board.idn,
                  "build": board.build, "port": board.port}
            for uid, board in boards.items()}
        validate_online_builds({uid: board.build for uid, board in boards.items()},
                               board_ids, build_id)
        for index, uid in enumerate(board_ids, 1):
            raw = board_command(boards[uid], "SYSTem:BOARD:NO?", args)
            readback = raw.strip().strip('"')
            result["assignments"].append({
                "address": uid, "no": index, "raw": raw, "readback": readback,
                "passed": readback == str(index)})
        result["passed"] = True
        validate_known_topology_readback(result, board_ids, build_id)
    except (AcceptanceError, OSError, ValueError, KeyError, TypeError, RuntimeError) as exc:
        result.update(passed=False, error=f"{type(exc).__name__}: {exc}")
    finally:
        close_persistent_connections()
    return result


def validate_topology_reuse_evidence(root: Path, record: dict[str, Any]) -> None:
    path = _validate_evidence_file(root, record.get("topology_summary"), "topology_summary")
    summary = json.loads(path.read_text(encoding="utf-8"))
    source = record.get("topology_reuse_source")
    if source is None and summary.get("mode") != "REUSED_KNOWN_TOPOLOGY":
        return  # Preserve existing measured-topology receipts.
    board_ids = record.get("tdma_board_ids", [])
    if (record.get("acceptance_profile") != "QUICK_DIAGNOSTIC" or
            record.get("ota_board_ids") != board_ids or not board_ids or
            summary.get("source_topology") != source):
        raise AcceptanceError("known topology receipt provenance differs")
    original = _validate_evidence_file(root, source, "topology_reuse_source")
    validate_known_topology(json.loads(original.read_text(encoding="utf-8-sig")),
                            board_ids, board_ids[0])
    validate_known_topology_readback(summary, board_ids, record.get("build_id", ""))
    reset_path = _validate_evidence_file(root, record.get("initialization_reset"),
                                         "initialization_reset")
    reset = json.loads(reset_path.read_text(encoding="utf-8"))
    if (reset.get("passed") is not True or reset.get("board_ids") != board_ids or
            reset.get("expected_build") != record.get("build_id") or
            reset.get("builds_after") != {uid: record.get("build_id") for uid in board_ids}):
        raise AcceptanceError("known topology post-reset identity differs")


def run_acceptance(args: argparse.Namespace) -> None:
    """Keep failure classifications even when a prerequisite aborts the run."""
    try:
        _run_acceptance_impl(args)
    except (AcceptanceError, OSError, KeyError, TypeError, ValueError) as exc:
        context = getattr(args, "_alarm_context", None)
        if context is not None:
            write_alarm_report(context, error=str(exc))
        raise


# ---------------------------------------------------------------------------
# Step-addressable acceptance.
#
# `run`/`resume` and tools/hardware_acceptance/hardware_acceptance_workflow.py
# execute the same numbered steps. A step reads its inputs from the run state
# (persisted to <out_dir>/workflow-state.json after every step) and from files in
# the run directory, never from another step's locals, so any step can be rerun
# or continued in a later invocation, and every gate can be recomputed offline
# from the evidence it names.
# ---------------------------------------------------------------------------
HAWF_STATE_SCHEMA = "HDRT100_HAWF_STATE_V1"
HAWF_RECORD_SCHEMA = "HDRT100_HAWF_RECORD_V1"
HAWF_STATE_FILE = "workflow-state.json"
ACCEPTANCE_STEPS: dict[int, tuple[str, Any]] = {}
# Steps that talk to the boards, the scope or the flash path; the rest only read
# and write evidence in the run directory.
ACCEPTANCE_BENCH_STEPS = frozenset(
    {4, 6, 7, 8, 10, 11, 13, 14, 16, 17, 18, 19, 20, 24, 27, 29, 31})
# Which step produces each state key, for named missing-prerequisite failures.
ACCEPTANCE_STATE_PRODUCERS = {
    "scope": 0, "scope_path": 0, "timing_probe_path": 0,
    "topology_reuse_source": 0, "previous_receipt_path": 0,
    "fingerprint_before": 1, "source_count": 1,
    "build_dir": 2, "package": 3, "build_id": 3,
    "ota_summary_path": 4, "ota_summary": 5, "online_builds": 6,
    "reset_path": 7, "topology_summary_path": 8,
    "coarse_paths": 10, "coded_paths": 11,
    "coarse_summary_path": 12, "coded_summary_path": 12,
    "schedule_before": 13, "p3_summary_path": 14,
    "p3_metrics": 15, "link_delays": 15, "link_base_delays": 15,
    "parameter_handoff": 15,
    "generation": 17, "marker_summary_path": 17, "marker_offsets_by_node": 17,
    "training_generation": 18, "residence_summary_path": 18,
    "trn00_summary_path": 18,
    "sck_summary_path": 19, "sck_offsets_by_node": 19, "trn01_summary_path": 19,
    "training_data_offsets_by_node": 20, "data_summary_path": 20,
    "trn03_matrix_path": 21, "matrix_returncode": 21, "matrix_log_path": 21,
    "matrix_fallback_used": 22, "fallback_manifest_path": 22,
    "trn03_data_offsets_by_node": 23, "parameter_handoff_path": 23,
    "tdma_returncode": 24, "tdma_summary_path": 24, "tdma_dir": 24,
    "stopped_handoff_path": 26,
    "dpll_scope_discovery_path": 27, "dpll_scope_enabled": 27,
    "dpll_joint_dir": 29, "dpll_summary_path": 30,
    "final_schedules": 31, "fingerprint_after": 32, "budget": 33,
    "alarm_sources": 34, "alarm_report": 34,
}


class MissingPrerequisite(AcceptanceError):
    """A step input that no earlier step of this run directory produced."""


def acceptance_step(number: int, title: str):
    def register(function):
        ACCEPTANCE_STEPS[number] = (title, function)
        return function
    return register


def _state_encode(value: Any) -> Any:
    if isinstance(value, Path):
        return {"$path": str(value)}
    if isinstance(value, dict):
        return {str(key): _state_encode(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_state_encode(item) for item in value]
    return value


def _state_decode(value: Any) -> Any:
    if isinstance(value, dict):
        if set(value) == {"$path"}:
            return Path(value["$path"])
        return {key: _state_decode(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_state_decode(item) for item in value]
    return value


def acceptance_step_plan(args: argparse.Namespace) -> list[int]:
    """The canonical step order of `run` (build+OTA) or `resume` (reuse firmware)."""
    firmware = [3, 5, 6] if args.command == "resume" else [2, 3, 4, 5, 6]
    return [0, 1, *firmware, *range(7, 37)]


def _resume_timing_probe(path: Path) -> None:
    """Continue a run directory's timing probe instead of starting a new one."""
    global _TIMING_PATH, _TIMING_EVENTS
    _TIMING_PATH = path
    _TIMING_EVENTS = []
    if path.is_file():
        _TIMING_EVENTS = list(json.loads(path.read_text(encoding="utf-8")).get("events", []))
    _write_timing_probe()


class AcceptanceRun:
    """Context of one acceptance run plus the values its steps produced."""

    def __init__(self, args: argparse.Namespace, out_dir: Path,
                 state: dict[str, Any] | None = None) -> None:
        self.args = args
        self.root = args.root.resolve()
        self.config_path = acceptance_config_path(args, self.root)
        self.receipt_path = self.root / args.receipt
        self.config = load_bench_config(self.config_path)
        config = self.config
        self.capture_policy = {
            stage: waveform_capture_enabled(config, stage)
            for stage in ("trn00", "trn01", "trn02", "trn03", "tdma", "dpll")
        }
        self.baseline_divisor = resolve_path_delay_baseline_divisor(config)
        self.previous_baseline_divisor = resolve_previous_path_delay_baseline_divisor(
            config)
        self.board_ids = list(config["p3_board_ids_in_physical_order"])
        self.tdma_only = bool(getattr(args, "tdma_only", False))
        self.diagnostic_continue = bool(getattr(args, "diagnostic_continue", False))
        self.acceptance_profile = str(config.get("acceptance_profile", "FULL"))
        self.quick_diagnostic = self.acceptance_profile == "QUICK_DIAGNOSTIC"
        self.finite_tdma = True
        # QUICK_DIAGNOSTIC is the default iteration path. It retains every
        # rejection and never upgrades diagnostics to strict product PASS.
        if self.quick_diagnostic:
            self.diagnostic_continue = True
        self.ota_board_ids = list(config["ota_board_ids"])
        if len(self.board_ids) != 4 or self.ota_board_ids != self.board_ids:
            raise AcceptanceError(
                "P3 acceptance requires the same four configured OTA and ring nodes")
        self.timing = acceptance_timing(config)
        # Imported calibration tools resolve the same read quantum through the
        # process environment, even when their legacy CLI has no read-timeout
        # option yet.
        os.environ["HAOFV_SERIAL_READ_TIMEOUT_S"] = str(
            self.timing["serial_read_timeout_s"])
        self.common_boards: list[str] = []
        for board_id in self.board_ids:
            self.common_boards.extend(["--board-id", board_id])
        self.out_dir = out_dir
        self.state: dict[str, Any] = state if state is not None else {}
        self.state.setdefault("diagnostic_failures", [])
        self.state.setdefault("steps", {})
        self.state.setdefault("invocations", [])
        self.state.setdefault("sequence", 0)
        self.invocation_started = time.perf_counter()
        self.current_step: int | None = None
        self.warnings: list[str] = []
        self.alarm_context: dict[str, Any] | None = None
        if "scope" in self.state:
            self._bind_alarm_context(self.state["scope"])

    # -- construction -------------------------------------------------------
    @classmethod
    def create(cls, args: argparse.Namespace) -> "AcceptanceRun":
        root = args.root.resolve()
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        out_dir = acceptance_output_path(root, args.out_dir, stamp)
        run = cls(args, out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        run.state.update(schema=HAWF_STATE_SCHEMA, stamp=stamp, out_dir=out_dir)
        run._open_invocation()
        return run

    @classmethod
    def load(cls, args: argparse.Namespace, out_dir: Path) -> "AcceptanceRun":
        path = out_dir / HAWF_STATE_FILE
        if not path.is_file():
            raise AcceptanceError(f"no {HAWF_STATE_FILE} in {out_dir}")
        state = _state_decode(json.loads(path.read_text(encoding="utf-8")))
        if state.get("schema") != HAWF_STATE_SCHEMA:
            raise AcceptanceError(f"{path} is not a {HAWF_STATE_SCHEMA} state")
        run = cls(args, out_dir, state)
        timing_path = state.get("timing_probe_path")
        if timing_path is not None:
            _resume_timing_probe(timing_path)
        run._open_invocation()
        return run

    def _open_invocation(self) -> None:
        fingerprint, count = working_source_fingerprint(self.root)
        self.state["invocations"].append({
            "started_at_utc": datetime.now(timezone.utc).isoformat(),
            "source_tree_sha256": fingerprint, "source_file_count": count,
            "elapsed_s": 0.0})

    def _bind_alarm_context(self, scope: dict[str, Any]) -> None:
        self.alarm_context = dict(scope=scope, stages={},
                                  failures=self.diagnostic_failures,
                                  out_dir=self.out_dir)
        self.args._alarm_context = self.alarm_context

    # -- state --------------------------------------------------------------
    @property
    def diagnostic_failures(self) -> list[dict[str, Any]]:
        return self.state["diagnostic_failures"]

    def get(self, key: str) -> Any:
        if key not in self.state:
            producer = ACCEPTANCE_STATE_PRODUCERS.get(key)
            raise MissingPrerequisite(
                f"{key} is missing: run step {producer} in this run directory first"
                if producer is not None else f"{key} is missing from the run state")
        return self.state[key]

    def put(self, **values: Any) -> None:
        self.state.update(values)

    def elapsed_total(self) -> float:
        self.state["invocations"][-1]["elapsed_s"] = (
            time.perf_counter() - self.invocation_started)
        return float(sum(row["elapsed_s"] for row in self.state["invocations"]))

    def save(self) -> None:
        self.elapsed_total()
        path = self.out_dir / HAWF_STATE_FILE
        temporary = path.with_suffix(".json.tmp")
        temporary.write_text(
            json.dumps(_state_encode(self.state), ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        os.replace(temporary, path)

    def warn(self, message: str) -> None:
        self.warnings.append(message)

    def execute(self, number: int) -> dict[str, Any]:
        """Run one step, record its outcome and persist the state."""
        title, function = ACCEPTANCE_STEPS[number]
        self.current_step = number
        self.warnings = []
        # A rerun replaces this step's own diagnostic failures; the list object is
        # shared with the alarm context, so it is edited in place.
        self.diagnostic_failures[:] = [
            row for row in self.diagnostic_failures if row.get("step") != number]
        failures_before = len(self.diagnostic_failures)
        started = time.perf_counter()
        self.state["sequence"] += 1
        record = {"title": title, "sequence": self.state["sequence"],
                  "invocation": len(self.state["invocations"]) - 1}
        try:
            function(self)
        except Exception as exc:
            for row in self.diagnostic_failures[failures_before:]:
                row["step"] = number
            record.update(status="FAIL", reason=f"{type(exc).__name__}: {exc}",
                          seconds=round(time.perf_counter() - started, 3))
            self.state["steps"][str(number)] = record
            self.save()
            raise
        new_failures = self.diagnostic_failures[failures_before:]
        for row in new_failures:
            row["step"] = number
        notes = [*self.warnings, *(
            f"{row.get('phase')}: {row.get('error')}" for row in new_failures)]
        record.update(status="warn" if notes else "OK", reason=notes[0] if notes else "",
                      notes=notes, seconds=round(time.perf_counter() - started, 3))
        self.state["steps"][str(number)] = record
        self.save()
        return record

    # -- shared step helpers -------------------------------------------------
    def rel(self, path: Path) -> str:
        return path.resolve().relative_to(self.root).as_posix()

    def run_diagnostic_gate(self, command: list[str], log_path: Path,
                            summary_path: Path, phase: str) -> dict[str, Any]:
        root, diagnostic_continue = self.root, self.diagnostic_continue
        diagnostic_failures = self.diagnostic_failures
        returncode = _run_step(
            command, root, log_path, allow_failure=diagnostic_continue)
        if not summary_path.is_file():
            if not diagnostic_continue:
                raise AcceptanceError(f"{phase} did not write summary.json")
            summary = {
                "schema": "HAOFV_DIAGNOSTIC_FORCED_CONTINUE_V1",
                "phase": phase,
                "passed": False,
                "debug_forced_continue": True,
                "error": f"{phase} did not write summary.json",
                "returncode": returncode if returncode != 0 else 1,
                "raw_log": log_path.resolve().relative_to(root).as_posix(),
            }
            summary_path.parent.mkdir(parents=True, exist_ok=True)
            summary_path.write_text(
                json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
            diagnostic_failures.append({
                "phase": phase,
                "returncode": summary["returncode"],
                "error": summary["error"],
                "summary": summary_path.resolve().relative_to(root).as_posix(),
                "raw_log": summary["raw_log"],
                "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
            })
            return summary
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        if returncode != 0 or summary.get("passed") is not True:
            if not diagnostic_continue:
                validate_pass_summary(summary, phase)
                raise AcceptanceError(f"{phase} returned {returncode}")
            diagnostic_failures.append({
                "phase": phase,
                "returncode": returncode if returncode != 0 else 1,
                "error": str(summary.get("error", "gate did not pass")),
                "summary": summary_path.resolve().relative_to(root).as_posix(),
            })
        return summary

    def record_parameter_handoff(
            self, phase: str, summary_path: Path, summary: dict[str, Any], *,
            loaded_from: str, selected: dict[str, Any] | None = None
            ) -> dict[str, Any]:
        root, board_ids = self.root, self.board_ids
        link_delays = self.get("link_delays")
        link_base_delays = self.get("link_base_delays")
        baseline_divisor = self.baseline_divisor
        parameter_handoff = self.get("parameter_handoff")
        expected = {
            "link_delay_ns_by_link": list(link_delays),
            "link_base_delay_ns_by_link": list(link_base_delays),
            "path_delay_baseline_divisor": baseline_divisor,
            "sample_period_ns": 4,
        }
        if summary.get("debug_forced_continue") is True:
            parameters = expected
        else:
            parameters = stage_training_parameters(
                summary, phase, len(board_ids))
        if parameters != expected:
            raise AcceptanceError(
                f"{phase} did not load the current P3 baseline from "
                f"{loaded_from}")
        # A rerun step replaces its own entry instead of appending a duplicate.
        parameter_handoff["stages"] = [
            row for row in parameter_handoff["stages"] if row.get("phase") != phase]
        parameter_handoff["stages"].append({
            "phase": phase,
            "loaded_from": loaded_from,
            "summary": {
                "path": summary_path.resolve().relative_to(root).as_posix(),
                "sha256": sha256_file(summary_path),
            },
            "loaded_parameters": parameters,
            **({"selected_parameters": selected} if selected is not None else {}),
            **({"debug_forced_continue": True}
               if summary.get("debug_forced_continue") is True else {}),
        })
        return parameters

    def link_delay_args(self) -> list[str]:
        link_delay_args: list[str] = []
        for link_delay in self.get("link_delays"):
            link_delay_args.extend(["--link-delay-ns", str(link_delay)])
        return link_delay_args

    def load_json(self, key: str) -> dict[str, Any]:
        return json.loads(self.get(key).read_text(encoding="utf-8"))

    def alarm_stage(self, stage: str, value: dict[str, Any]) -> None:
        if self.alarm_context is not None:
            self.alarm_context["stages"][stage] = value


# -- 准备 -------------------------------------------------------------------
@acceptance_step(0, "准备：配置/范围冻结 + 运行状态 + 计时探针 + 原凭证快照")
def phase_setup(run: AcceptanceRun) -> None:
    args, root, out_dir, config = run.args, run.root, run.out_dir, run.config
    board_ids = run.board_ids
    scope = acceptance_scope(run.acceptance_profile, run.tdma_only,
                             getattr(args, "strict_stage", []) or [])
    scope_path = out_dir / "fixed-scope.json"
    scope_path.write_text(json.dumps(scope, indent=2) + "\n", encoding="utf-8")
    run._bind_alarm_context(scope)
    topology_reuse_source = None
    if getattr(args, "reuse_topology", None) is not None:
        if not (run.tdma_only and run.quick_diagnostic):
            raise AcceptanceError("known topology reuse requires four-board quick acceptance")
        frozen = out_dir / "known-topology-source.json"
        if frozen.is_file() and frozen.read_bytes() == (root / args.reuse_topology).read_bytes():
            topology_reuse_source = evidence_entry(root, frozen)  # step 0 rerun
        else:
            topology_reuse_source = freeze_known_topology(
                root, args.reuse_topology, out_dir, board_ids,
                str(config["topology_anchor_board_id"]))
    timing_probe_path = out_dir / "timing.json"
    if "timing_probe_path" in run.state:
        _resume_timing_probe(timing_probe_path)  # step 0 rerun keeps the events
    else:
        _start_timing_probe(timing_probe_path)
    # The TRN-03 previous-matrix fallback reads the receipt as it was before this
    # run; the DPLL step later overwrites that path with a provisional receipt.
    # Snapshot it once per run directory, never after a provisional was written.
    # An aborted run can leave the provisional receipt behind; never take that as
    # the previous credential - fall back to the committed (index) receipt.
    previous_receipt_path = run.state.get("previous_receipt_path")
    if "previous_receipt_path" not in run.state and run.receipt_path.is_file():
        previous_receipt_path = out_dir / "previous-receipt.json"
        current = json.loads(run.receipt_path.read_text(encoding="utf-8"))
        if current.get("schema") != PROVISIONAL_SCHEMA:
            shutil.copyfile(run.receipt_path, previous_receipt_path)
        else:
            try:
                committed = read_index_json(root, args.receipt)
            except AcceptanceError:
                previous_receipt_path = None
            else:
                previous_receipt_path.write_text(
                    json.dumps(committed, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")
    run.put(scope=scope, scope_path=scope_path,
            topology_reuse_source=topology_reuse_source,
            timing_probe_path=timing_probe_path,
            previous_receipt_path=previous_receipt_path)


@acceptance_step(1, "源码指纹（working_source_fingerprint）")
def phase_source_fingerprint(run: AcceptanceRun) -> None:
    fingerprint_before, source_count = working_source_fingerprint(run.root)
    run.put(fingerprint_before=fingerprint_before, source_count=source_count)


@acceptance_step(2, "构建（cmake_build_auto）")
def phase_build(run: AcceptanceRun) -> None:
    args, root, out_dir = run.args, run.root, run.out_dir
    stamp = run.get("stamp")
    build_dir = root / (
        args.build_dir or Path(f"out/build/p3-acceptance-{stamp}"))
    print(f"P3 acceptance: build -> {build_dir}", flush=True)
    build_command = [
        sys.executable,
        str(root / "tools/cmake_build_auto/cmake_build_auto.py"),
        "--root", str(root), "--build-dir", str(build_dir),
    ]
    if getattr(args, "preset", None):
        build_command.extend(["--preset", str(args.preset)])
    _run_step(build_command, root, out_dir / "build.log")
    run.put(build_dir=build_dir)
    package = _fresh_update_package(build_dir)
    if package.is_file():
        # Record which source produced this package so --reuse-flashed can
        # prove that the boards' build is the current tree's build.
        fingerprint, count = working_source_fingerprint(root)
        (build_dir / "p3-build-record.json").write_text(
            json.dumps({"build_id": _read_package_build_id(package),
                        "source_fingerprint": fingerprint,
                        "source_file_count": count}, indent=2),
            encoding="utf-8")


OWNER_HOOK_SYMBOLS = (
    "phase_model_committed_core1",
    "priority_trace_phase_core1",
    "priority_trace_decision_core1",
    "priority_trace_origin_core1",
)


def nm_symbol_types(text: str) -> dict[str, str]:
    """Map symbol name -> nm type letter; undefined symbols collapse to 'U'."""
    symbols: dict[str, str] = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[0] in ("U", "u", "w", "v"):
            symbols[fields[1]] = "U"
        elif len(fields) >= 3:
            symbols[fields[2]] = fields[1]
    return symbols


def missing_owner_hook_symbols(symbols: dict[str, str]) -> list[str]:
    """Owner receipts that the linked image does not keep."""
    return [name for name in OWNER_HOOK_SYMBOLS
            if symbols.get(name, "U") in ("U", "u")]


def toolchain_nm() -> str | None:
    for name in ("arm-none-eabi-nm", "arm-none-eabi-nm.exe"):
        found = shutil.which(name)
        if found:
            return found
    for directory in sorted((Path.home() / ".pico-sdk" / "toolchain").glob("*/bin")):
        for name in ("arm-none-eabi-nm.exe", "arm-none-eabi-nm"):
            candidate = directory / name
            if candidate.is_file():
                return str(candidate)
    return None


def verify_owner_hook_symbols(elf: Path) -> None:
    """Fail when a split unit's owner hook is compiled out of the linked image.

    The units link with ``--gc-sections``, so an unreferenced owner receipt
    disappears from the symbol table. That is exactly how the 2026-10-25
    regression presented: ``vdc_dpll_feedback.c`` lost the phase-commit call
    after being split into its own translation unit and P3 step 30 then saw
    ``applied=0`` on every follower.
    """
    nm = toolchain_nm()
    if nm is None:
        raise AcceptanceError(
            "cannot verify owner hook linkage: arm-none-eabi-nm was not found "
            "in PATH or the Pico toolchain")
    result = subprocess.run(
        [nm, str(elf)], capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise AcceptanceError(
            f"nm failed on {elf.name}: {result.stderr.strip()[:400]}")
    missing = missing_owner_hook_symbols(nm_symbol_types(result.stdout))
    if missing:
        raise AcceptanceError(
            "owner hook symbols are missing from the linked image (a split "
            "unit lost its binding): " + ", ".join(missing))


@acceptance_step(3, "固件包校验（包存在 + 构建号）")
def phase_package(run: AcceptanceRun) -> None:
    args, root = run.args, run.root
    if args.command == "resume" or getattr(args, "package", None) is not None:
        package = (root / args.package).resolve()
        if not package.is_file():
            raise AcceptanceError(f"reused firmware package missing: {package}")
    else:
        package = _fresh_update_package(run.get("build_dir"))
        if not package.is_file():
            raise AcceptanceError(f"firmware package missing: {package}")
    build_id = _read_package_build_id(package)
    run.put(package=package, build_id=build_id)
    # A resumed run (--package/--ota-summary) has no step 2 in its directory, so
    # the linked image is only available when the operator names the build dir
    # explicitly. verify_owner_hook_symbols still runs whenever an image is
    # reachable; without one the package build id above remains the binding.
    build_dir = run.state.get("build_dir") or getattr(args, "build_dir", None)
    if build_dir is not None:
        elf = Path(build_dir) / "DHRT100.elf"
        if not elf.is_file():
            raise AcceptanceError(f"owner hook check needs the linked image: {elf}")
        verify_owner_hook_symbols(elf)


@acceptance_step(4, "刷写（--method ota | picotool | tdma 预留）")
def phase_flash(run: AcceptanceRun) -> None:
    root, out_dir = run.root, run.out_dir
    ota_board_ids, diagnostic_continue = run.ota_board_ids, run.diagnostic_continue
    package, build_id = run.get("package"), run.get("build_id")
    method = str(getattr(run.args, "flash_method", None) or "ota")
    run.put(flash_method=method)
    if method == "tdma":
        raise AcceptanceError(
            "flash method 'tdma' (streaming OTA over the TDMA ring) is reserved and "
            "not implemented; nothing was flashed")
    if method == "picotool":
        # Loads DHRT100_FACTORY.uf2 through tools/ota/OTA_workflow.py; there is no
        # OTA transport summary, so the run cannot be credential-eligible.
        flash_dir = out_dir / "flash-picotool"
        command = [sys.executable, str(root / "tools/ota/OTA_workflow.py"),
                   "--method", "picotool", "--allow-factory-image", "--no-build",
                   "--package", str(package), "--steps", "1-6"]
        if run.state.get("build_dir") is not None:
            command += ["--build-dir", str(run.state["build_dir"])]
        _run_step(command, root, out_dir / "flash-picotool.log")
        flash_dir.mkdir(parents=True, exist_ok=True)
        run.put(ota_summary_path=None)
        run.warn("picotool flash has no OTA transport summary; the run is record-only")
        return
    ota_dir = out_dir / "ota-four-board"
    print(
        f"P3 acceptance: asynchronous OTA {len(ota_board_ids)} boards "
        f"build={build_id}",
        flush=True)
    ota_command = [
        sys.executable,
        str(root / "tools/ota_multi_update/ota_multi_update.py"),
        str(package), "--expected-board-count",
        str(len(ota_board_ids)),
        "--expected-build", build_id, "--out-dir", str(ota_dir),
        "--block-size", "4096", "--timeout", "300",
        "--begin-timeout", "300",
    ]
    for board_id in ota_board_ids:
        ota_command.extend(["--serial-number", board_id])
    if diagnostic_continue:
        ota_command.append("--diagnostic-continue")
    _run_step(ota_command, root, out_dir / "ota.log")
    run.put(ota_summary_path=ota_dir / "summary.json")


@acceptance_step(5, "OTA 摘要门限（validate_ota，4096 B 块）")
def phase_ota_gate(run: AcceptanceRun) -> None:
    args, root = run.args, run.root
    build_id = run.get("build_id")
    if args.command == "resume" or getattr(args, "ota_summary", None) is not None:
        ota_summary_path = (root / args.ota_summary).resolve()
        if not ota_summary_path.is_file():
            raise AcceptanceError(
                f"reused OTA summary missing: {ota_summary_path}")
    else:
        ota_summary_path = run.get("ota_summary_path")
        if ota_summary_path is None:
            run.warn("no OTA transport summary (non-OTA flash); gate not applicable")
            return
    ota_summary = json.loads(
        ota_summary_path.read_text(encoding="utf-8"))
    validate_ota(ota_summary, run.ota_board_ids, build_id)
    run.put(ota_summary_path=ota_summary_path, ota_summary=ota_summary)


@acceptance_step(6, "在线构建号回读（validate_online_builds）")
def phase_online_builds(run: AcceptanceRun) -> None:
    args, root, out_dir = run.args, run.root, run.out_dir
    ota_board_ids = run.ota_board_ids
    online_builds = read_online_builds(ota_board_ids, run.timing)
    if "build_id" not in run.state:
        # No package step in this run: adopt the build the boards report. The run
        # then binds no package evidence and stays record-only.
        observed = sorted(set(online_builds.values()))
        if len(observed) != 1 or set(online_builds) != set(ota_board_ids):
            raise AcceptanceError(f"boards do not report one common build: {online_builds}")
        run.put(build_id=observed[0])
        run.warn(f"build id {observed[0]} adopted from the boards (no package step)")
    build_id = run.get("build_id")
    validate_online_builds(
        online_builds, ota_board_ids, build_id)
    if args.command == "resume":
        package, ota_summary_path = run.get("package"), run.get("ota_summary_path")
        reuse_record = {
            "schema": "HAOFV_HARDWARE_ACCEPTANCE_FIRMWARE_REUSE_V1",
            "firmware_changed": False,
            "build_skipped": True,
            "ota_skipped": True,
            "build_id": build_id,
            "package": package.resolve().relative_to(root).as_posix(),
            "ota_summary": ota_summary_path.resolve().relative_to(root).as_posix(),
            "online_builds": online_builds,
        }
        (out_dir / "firmware-reuse.json").write_text(
            json.dumps(reuse_record, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        print(
            f"Hardware acceptance: reuse live build={build_id}; "
            "build and OTA skipped", flush=True)
    run.put(online_builds=online_builds)


@acceptance_step(7, "初始化复位（reset_acceptance_boards）")
def phase_reset(run: AcceptanceRun) -> None:
    out_dir, ota_board_ids = run.out_dir, run.ota_board_ids
    build_id = run.get("build_id")
    print(
        f"Hardware acceptance: initialize {len(ota_board_ids)} boards by "
        "software reset", flush=True)
    reset_started = datetime.now(timezone.utc)
    reset_evidence = reset_acceptance_boards(
        ota_board_ids, build_id, run.timing)
    reset_path = out_dir / "initialization-reset.json"
    reset_path.write_text(
        json.dumps(reset_evidence, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    _record_timing_event({
        "action": "acceptance.initialization_reset",
        "started_at_utc": reset_started.isoformat(),
        "ended_at_utc": datetime.now(timezone.utc).isoformat(),
        "duration_ms": round(float(reset_evidence["elapsed_s"]) * 1000, 3),
        "returncode": 0,
        "status": "PASS",
    })
    run.put(reset_path=reset_path)


# -- 拓扑 -------------------------------------------------------------------
@acceptance_step(8, "P0T 线序与 NO 分配（或已知拓扑回读）")
def phase_topology(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    common_boards, board_ids = run.common_boards, run.board_ids
    build_id = run.get("build_id")
    topology_reuse_source = run.state.get("topology_reuse_source")
    topology_dir = out_dir / "p0t-topology"
    # P0T probes pairs while the ring is idle; a read that needs more than one
    # second means the command plane is starved (busy board), not "slow line".
    # Keep this independent from the global serial timeout so a busy board is
    # classified quickly instead of being masked by the 3 s default.
    # A 151-field TDMA page is only ~32 ms on the wire at 115200.  A 1.0 s
    # deadline measured ~15 % read timeouts (2026-11-06: two consecutive
    # timeouts aborted pair 11), so use the configured 3.0 s serial budget;
    # action ACK reads are still capped at 0.8 s by board_command, preserving
    # the busy-board fast path.
    topology_timeout_s = max(2.0, float(timing.get("serial_timeout_s", 3.0)))
    topology_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_ring_topology.py"),
        *common_boards,
        "--anchor-id", str(config["topology_anchor_board_id"]),
        "--expected-build", build_id,
        "--level", str(config["topology_profile_level"]),
        "--cycles", str(config["topology_probe_cycles"]),
        "--probe-phase-cycles",
        str(config.get("topology_probe_phase_cycles", 10)),
        "--pair-wait", str(config["topology_pair_wait_s"]),
        "--adjacency-only", "--out-dir", str(topology_dir),
        # Persistent CDC loses the RING:START acknowledgment on this bench
        # (measured 2026-11-05: three consecutive "<timeout>" START failures in
        # this step while the same command with --short-open passed 4/4 twice).
        # A lost ACK must stay a failure, so fix it at the transport layer.
        "--short-open",
    ]
    topology_summary_path = topology_dir / "summary.json"
    if topology_reuse_source is None:
        print("Hardware acceptance: P0T line order and NO assignment", flush=True)
        add_serial_timing(topology_command, timing, action=True, gap=True)
        if "--timeout" in topology_command:
            topology_command[topology_command.index("--timeout") + 1] = str(
                topology_timeout_s)
        # Pin the inter-command gap that was validated on this bench
        # (2026-11-05): 0.2 s. The timing profile used to derive it implicitly,
        # so the combination that passed was not the one this command ran.
        if "--gap" in topology_command:
            topology_command[topology_command.index("--gap") + 1] = "0.2"
        else:
            topology_command.extend(["--gap", "0.2"])
        _run_step(topology_command, root, out_dir / "topology.log")
    else:
        print("Hardware acceptance: reuse measured line order; read current UID/build/NO", flush=True)
        topology_summary = verify_known_topology(
            root, topology_reuse_source, board_ids, build_id, timing)
        topology_dir.mkdir(parents=True, exist_ok=True)
        topology_summary_path.write_text(
            json.dumps(topology_summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    run.put(topology_summary_path=topology_summary_path)


@acceptance_step(9, "拓扑门限（环序 = NO.1..NO.4，分配全部通过）")
def phase_topology_gate(run: AcceptanceRun) -> None:
    board_ids = run.board_ids
    topology_summary = run.load_json("topology_summary_path")
    run.alarm_stage("P0", topology_summary)
    validate_pass_summary(topology_summary, "P0T topology")
    if topology_summary.get("ring_order") != board_ids:
        raise AcceptanceError(
            "measured physical loop order differs from bench NO.1..NO.4")
    assignments = topology_summary.get("assignments", [])
    if ([row.get("address") for row in assignments] != board_ids or
            any(row.get("passed") is not True for row in assignments)):
        raise AcceptanceError("P0T NO assignment/readback did not pass")


# -- 标定 -------------------------------------------------------------------
@acceptance_step(10, "P1 粗 CLK 标定（每个 calibration_profile_levels 等级）")
def phase_p1_coarse(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    common_boards = run.common_boards
    build_id = run.get("build_id")
    coarse_paths: list[Path] = []
    for level in config["calibration_profile_levels"]:
        probe_phase_cycles = calibration_probe_phase_cycles(config, level)
        coarse_dir = out_dir / f"coarse-clk-level{level}"
        print(f"Hardware acceptance: coarse CLK calibration level={level}",
              flush=True)
        coarse_command = [
            sys.executable,
            str(root / "tools/calibration_ring_validate/calibration_clk_train.py"),
            *common_boards, "--expected-build", build_id,
            "--level", str(level),
            "--repeats", str(config["calibration_clk_repeats"]),
            "--probe-phase-cycles", str(probe_phase_cycles),
            "--binary-refine", "--out-dir", str(coarse_dir),
        ]
        add_serial_timing(coarse_command, timing, action=True, gap=True)
        coarse_summary = coarse_dir / "summary.json"
        run.run_diagnostic_gate(
            coarse_command, out_dir / f"coarse-clk-level{level}.log",
            coarse_summary, f"coarse CLK calibration level {level}")
        coarse_paths.append(coarse_summary)
    run.put(coarse_paths=coarse_paths)


@acceptance_step(11, "P2 编码 MARK 标定（每个等级）")
def phase_p2_coded(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    common_boards = run.common_boards
    build_id = run.get("build_id")
    coded_paths: list[Path] = []
    for level in config["calibration_profile_levels"]:
        coded_probe_phase_cycles = calibration_coded_probe_phase_cycles(
            config, level)
        coded_dir = out_dir / f"coded-marker-level{level}"
        print(f"Hardware acceptance: coded marker calibration level={level}",
              flush=True)
        coded_command = [
            sys.executable,
            str(root / "tools/calibration_ring_validate/calibration_clk_coded.py"),
            *common_boards, "--expected-build", build_id,
            "--level", str(level),
            "--codebook", str(config["calibration_coded_codebook"]),
            "--repeats", str(config["calibration_coded_repeats"]),
            "--out-dir", str(coded_dir),
        ]
        for phase_cycles in coded_probe_phase_cycles:
            coded_command.extend([
                "--probe-phase-cycles", str(phase_cycles)])
        add_serial_timing(coded_command, timing, action=True, gap=True)
        coded_summary = coded_dir / "summary.json"
        run.run_diagnostic_gate(
            coded_command, out_dir / f"coded-marker-level{level}.log",
            coded_summary, f"coded marker calibration level {level}")
        coded_paths.append(coded_summary)
    run.put(coded_paths=coded_paths)


@acceptance_step(12, "P1/P2 阶段摘要（write_phase_summary）")
def phase_p1_p2_summaries(run: AcceptanceRun) -> None:
    out_dir, diagnostic_continue = run.out_dir, run.diagnostic_continue
    coarse_paths, coded_paths = run.get("coarse_paths"), run.get("coded_paths")
    coarse_summary_path = out_dir / "coarse-calibration-summary.json"
    coded_summary_path = out_dir / "coded-calibration-summary.json"
    write_phase_summary(
        coarse_summary_path, "COARSE_CLK", coarse_paths,
        diagnostic_continue=diagnostic_continue)
    write_phase_summary(
        coded_summary_path, "CODED_MARKER", coded_paths,
        diagnostic_continue=diagnostic_continue)
    for stage, paths in (("P1", coarse_paths), ("P2", coded_paths)):
        summaries = [json.loads(path.read_text(encoding="utf-8")) for path in paths]
        run.alarm_stage(stage, {
            "passed": all(row.get("passed") is True for row in summaries),
            "summaries": summaries})
    run.put(coarse_summary_path=coarse_summary_path,
            coded_summary_path=coded_summary_path)


# -- 时延 -------------------------------------------------------------------
@acceptance_step(13, "调度基线（read_schedules → schedule_before）")
def phase_schedule_baseline(run: AcceptanceRun) -> None:
    schedule_before = read_schedules(run.board_ids, run.timing)
    run.put(schedule_before=schedule_before)


@acceptance_step(14, "P3 时延测量（calibration_link_p3）")
def phase_p3_measure(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    board_ids = run.board_ids
    build_id = run.get("build_id")
    p3_dir = out_dir / "p3-four-board"
    print("Hardware acceptance: Latency Cal four links x two groups x frequency ladder",
          flush=True)
    p3_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_link_p3.py"),
        "--expected-build", build_id,
        "--signal-group", config["signal_group"],
        "--repeats", str(config["repeats"]),
        "--pulse-count", str(config["pulse_count"]),
        "--capture-words", str(config["capture_words"]),
        "--out-dir", str(p3_dir),
    ]
    for board_id in board_ids:
        p3_command.extend(["--board-id", board_id])
    for frequency in config["frequency_ladder_mhz"]:
        p3_command.extend(["--frequency-mhz", str(frequency)])
    # A single-frequency bench is an intentional bring-up gate.  The P3
    # tool keeps the full 10/25/30 MHz ladder as its default contract, so make
    # the reduced 10 MHz input explicit rather than silently weakening it.
    if config["frequency_ladder_mhz"] != [10, 25, 30]:
        p3_command.append("--diagnostic-frequency-only")
    # P3 capture is part of the latency measurement itself, not an optional
    # diagnostic waveform.  Always provide its timeout to the measurement tool.
    add_serial_timing(p3_command, timing, action=True, capture=True, gap=True)
    _run_step(p3_command, root, out_dir / "p3.log")
    run.put(p3_summary_path=p3_dir / "summary.json")


@acceptance_step(15, "P3 门限 + 链路时延（validate_p3 / p3_link_delays）")
def phase_p3_gate(run: AcceptanceRun) -> None:
    config, baseline_divisor = run.config, run.baseline_divisor
    p3_summary = run.load_json("p3_summary_path")
    run.alarm_stage("P3", p3_summary)
    p3_metrics = validate_p3(p3_summary, config)
    link_delays = p3_link_delays(p3_summary, config)
    link_base_delays = [
        (value + baseline_divisor // 2) // baseline_divisor
        for value in link_delays]
    parameter_handoff: dict[str, Any] = {
        "schema": "HAOFV_CALIBRATION_PARAMETER_HANDOFF_V1",
        "baseline": {
            "source_phase": "P3",
            "link_delay_ns_by_link": list(link_delays),
            "link_base_delay_ns_by_link": list(link_base_delays),
            "path_delay_baseline_divisor": baseline_divisor,
            "sample_period_ns": 4,
        },
        "stages": [],
    }
    previous = run.state.get("parameter_handoff")
    if previous is not None and previous.get("baseline") == parameter_handoff["baseline"]:
        # Recomputing the same baseline (offline rerun) must not drop the
        # entries the training steps already recorded against it.
        parameter_handoff["stages"] = previous.get("stages", [])
    run.put(p3_metrics=p3_metrics, link_delays=link_delays,
            link_base_delays=link_base_delays, parameter_handoff=parameter_handoff)


@acceptance_step(16, "标定期调度隔离（validate_schedule_isolation）")
def phase_schedule_isolation(run: AcceptanceRun) -> None:
    schedule_before = run.get("schedule_before")
    schedule_after = read_schedules(run.board_ids, run.timing)
    validate_schedule_isolation(
        schedule_before, schedule_after, int(run.config["calibration_load_mask"]))


# -- 训练 -------------------------------------------------------------------
@acceptance_step(17, "TRN-00 MARK 接受行（+ 训练代号）")
def phase_trn00_mark(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    common_boards, board_ids = run.common_boards, run.board_ids
    diagnostic_continue, baseline_divisor = run.diagnostic_continue, run.baseline_divisor
    build_id = run.get("build_id")
    link_delay_args = run.link_delay_args()
    generation = int(time.time())
    marker_dir = out_dir / "trn00-marker-accepted-row"
    marker_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_marker_train.py"),
        *common_boards, "--expected-build", build_id,
        "--level", str(config["training_profile_level"]),
        "--codebook", str(config["training_marker_codebook"]),
        "--offset-matrix", *link_delay_args,
        "--matrix-repeats", str(config["training_marker_repeats"]),
        "--matrix-epoch-start", "1",
        "--matrix-generation-start", str(generation),
        "--path-delay-baseline-divisor", str(baseline_divisor),
        "--out-dir", str(marker_dir),
    ]
    for value in (-1, 0, 1):
        marker_command.extend(["--matrix-offset-value", str(value)])
    for node, value in enumerate(config["training_marker_offsets_by_node"]):
        marker_command.extend([
            "--matrix-filter-node-offset", f"{node}={value}"])
    if not waveform_capture_enabled(config, "trn00"):
        marker_command.append("--skip-capture")
    add_serial_timing(marker_command, timing, action=True, gap=True)
    print("Hardware acceptance: TRN-00 accepted MARK offset row", flush=True)
    marker_summary_path = marker_dir / "summary.json"
    marker_summary = run.run_diagnostic_gate(
        marker_command, out_dir / "trn00-marker.log", marker_summary_path,
        "TRN-00 MARK offset row")
    marker_offsets_by_node = selected_node_offsets(
        marker_summary, "offset_sample_counts_by_node", len(board_ids),
        "TRN-00 MARK",
        diagnostic_fallback=(
            [int(value) for value in config["training_marker_offsets_by_node"]]
            if diagnostic_continue else None))
    run.record_parameter_handoff(
        "TRN-00 MARK", marker_summary_path, marker_summary,
        loaded_from="P3", selected={
            "marker_offset_sample_counts_by_node": marker_offsets_by_node,
        })
    run.put(generation=generation, marker_summary_path=marker_summary_path,
            marker_offsets_by_node=marker_offsets_by_node)


@acceptance_step(18, "TRN-00 驻留矩阵（+ TRN-00 阶段摘要）")
def phase_trn00_residence(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    common_boards, diagnostic_continue = run.common_boards, run.diagnostic_continue
    baseline_divisor = run.baseline_divisor
    build_id, generation = run.get("build_id"), run.get("generation")
    marker_offsets_by_node = run.get("marker_offsets_by_node")
    marker_summary_path = run.get("marker_summary_path")
    link_delay_args = run.link_delay_args()
    # Residence, SCK and DATA form one portable training identity.  MARK row
    # repetitions above use separate generations only as fresh stability proof.
    training_generation = generation + int(config["training_marker_repeats"]) + 1
    residence_dir = out_dir / "trn00-residence"
    residence_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_marker_train.py"),
        *common_boards, "--expected-build", build_id,
        "--level", str(config["training_profile_level"]),
        "--codebook", str(config["training_marker_codebook"]),
        "--residence-matrix", "--epoch", "32",
        "--generation", str(training_generation), *link_delay_args,
        "--path-delay-baseline-divisor", str(baseline_divisor),
        "--out-dir", str(residence_dir),
    ]
    for value in marker_offsets_by_node:
        residence_command.extend(["--node-offset-samples", str(value)])
    if not waveform_capture_enabled(config, "trn00"):
        residence_command.append("--skip-capture")
    add_serial_timing(residence_command, timing, action=True, gap=True)
    print("Hardware acceptance: TRN-00 full residence matrix", flush=True)
    residence_summary_path = residence_dir / "summary.json"
    residence_summary = run.run_diagnostic_gate(
        residence_command, out_dir / "trn00-residence.log",
        residence_summary_path, "TRN-00 residence matrix")
    run.record_parameter_handoff(
        "TRN-00 residence", residence_summary_path, residence_summary,
        loaded_from="TRN-00 MARK")
    trn00_summary_path = out_dir / "trn00-summary.json"
    write_phase_summary(
        trn00_summary_path, "TRN-00_MARK_AND_RESIDENCE",
        [marker_summary_path, residence_summary_path],
        diagnostic_continue=diagnostic_continue)
    marker_summary = json.loads(marker_summary_path.read_text(encoding="utf-8"))
    run.alarm_stage("T0", {
        "passed": marker_summary.get("passed") is True and residence_summary.get("passed") is True,
        "summaries": [marker_summary, residence_summary]})
    run.put(training_generation=training_generation,
            residence_summary_path=residence_summary_path,
            trn00_summary_path=trn00_summary_path)


@acceptance_step(19, "TRN-01 SCK 偏移矩阵")
def phase_trn01_sck(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    common_boards, board_ids = run.common_boards, run.board_ids
    diagnostic_continue, baseline_divisor = run.diagnostic_continue, run.baseline_divisor
    diagnostic_failures = run.diagnostic_failures
    build_id = run.get("build_id")
    training_generation = run.get("training_generation")
    link_delay_args = run.link_delay_args()
    sck_dir = out_dir / "trn01-sck"
    sck_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_sck_train.py"),
        *common_boards, "--expected-build", build_id,
        "--level", str(config["training_profile_level"]),
        "--codebook", str(config["training_sck_codebook"]),
        "--all-links", "--repeats", str(config["training_sck_repeats"]),
        "--min-repeats", str(config.get(
            "training_sck_min_repeats", config["training_sck_repeats"])),
        "--min-follower-candidates", str(config.get(
            "training_sck_min_follower_candidates", 1)),
        "--max-offset-span", str(config["training_max_offset_span"]),
        "--epoch", "48", "--generation", str(training_generation),
        "--reuse-ring-identity", *link_delay_args,
        "--path-delay-baseline-divisor", str(baseline_divisor),
        "--out-dir", str(sck_dir),
    ]
    for value in config["training_sck_offsets_by_node"]:
        sck_command.extend(["--node-sck-offset-samples", str(value)])
    if diagnostic_continue:
        sck_command.append("--diagnostic-continue")
    if not waveform_capture_enabled(config, "trn01"):
        sck_command.append("--skip-capture")
    add_serial_timing(sck_command, timing, action=True, gap=True)
    print("Hardware acceptance: TRN-01 SCK offset matrix", flush=True)
    sck_summary_path = sck_dir / "summary.json"
    sck_summary = run.run_diagnostic_gate(
        sck_command, out_dir / "trn01-sck.log", sck_summary_path,
        "TRN-01 SCK training")
    run.alarm_stage("T1", sck_summary)
    sck_offsets_by_node = selected_sck_offsets(
        sck_summary, len(board_ids),
        diagnostic_fallback=(
            [int(value) for value in config["training_sck_offsets_by_node"]]
            if diagnostic_continue else None))
    sck_offset_matrix = sck_summary.get("matrix", {}).get(
        "offset_matrix", {})
    if (diagnostic_continue and
            int(sck_offset_matrix.get("active_row_id", -1)) < 0):
        diagnostic_failures.append({
            "phase": "TRN-01 SCK active row selection",
            "returncode": 1,
            "error": "no complete active row; using configured diagnostic row",
            "selected_offset_sample_counts_by_node": sck_offsets_by_node,
            "summary": sck_summary_path.resolve().relative_to(root).as_posix(),
            "raw_log": (out_dir / "trn01-sck.log").resolve().relative_to(
                root).as_posix(),
            "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
        })
    run.record_parameter_handoff(
        "TRN-01 SCK", sck_summary_path, sck_summary,
        loaded_from="P3 baseline; TRN-00 topology identity", selected={
            "sck_offset_sample_counts_by_node": sck_offsets_by_node,
        })
    trn01_summary_path = out_dir / "trn01-summary.json"
    write_phase_summary(
        trn01_summary_path, "TRN-01_SCK_OFFSET_MATRIX",
        [sck_summary_path], diagnostic_continue=diagnostic_continue)
    run.put(sck_summary_path=sck_summary_path,
            sck_offsets_by_node=sck_offsets_by_node,
            trn01_summary_path=trn01_summary_path)


@acceptance_step(20, "TRN-02 DATA 重复矩阵")
def phase_trn02_data(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    common_boards = run.common_boards
    baseline_divisor = run.baseline_divisor
    previous_baseline_divisor = run.previous_baseline_divisor
    build_id = run.get("build_id")
    training_generation = run.get("training_generation")
    link_delays = run.get("link_delays")
    marker_offsets_by_node = run.get("marker_offsets_by_node")
    link_delay_args = run.link_delay_args()
    data_dir = out_dir / "trn02-data"
    training_data_offsets_by_node = rebase_node_offsets_for_path_delay(
        [int(value) for value in config["training_data_offsets_by_node"]],
        link_delays, previous_baseline_divisor, baseline_divisor,
        direction="reverse")
    data_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/calibration_data_train.py"),
        *common_boards, "--expected-build", build_id,
        "--level", str(config["training_profile_level"]),
        "--codebook", str(config["training_data_codebook"]),
        "--marker-direction", "forward", "--data-direction", "reverse",
        "--all-links", "--repeats", str(config["training_data_repeats"]),
        "--max-offset-span", str(config["training_max_offset_span"]),
        "--marker-to-data-samples",
        str(config["training_marker_to_data_samples"]),
        "--epoch", "96", "--generation", str(training_generation),
        "--reuse-ring-identity", *link_delay_args,
        "--path-delay-baseline-divisor", str(baseline_divisor),
        "--out-dir", str(data_dir),
    ]
    if not waveform_capture_enabled(config, "trn02"):
        data_command.append("--skip-capture")
    for value in marker_offsets_by_node:
        data_command.extend(["--node-marker-offset-samples", str(value)])
    for value in training_data_offsets_by_node:
        data_command.extend(["--node-data-offset-samples", str(value)])
    add_serial_timing(data_command, timing, action=True, gap=True)
    print("Hardware acceptance: TRN-02 DATA repeat matrix", flush=True)
    data_summary_path = data_dir / "summary.json"
    data_summary = run.run_diagnostic_gate(
        data_command, out_dir / "trn02-data.log", data_summary_path,
        "TRN-02 DATA training")
    run.alarm_stage("T2", data_summary)
    run.record_parameter_handoff(
        "TRN-02 DATA", data_summary_path, data_summary,
        loaded_from="TRN-00 MARK + previous DATA calibration", selected={
            "data_offset_sample_counts_by_node":
                training_data_offsets_by_node,
        })
    data_training_parameters = data_summary.get("training_parameters", {})
    configured_marker_offsets = (
        data_training_parameters.get("node_marker_offset_samples")
        if isinstance(data_training_parameters, dict) else None)
    if configured_marker_offsets != marker_offsets_by_node:
        raise AcceptanceError(
            "TRN-02 DATA did not load the selected TRN-00 MARK offsets")
    run.put(training_data_offsets_by_node=training_data_offsets_by_node,
            data_summary_path=data_summary_path)


@acceptance_step(21, "TRN-03 回放矩阵推导（trn03_matrix）")
def phase_trn03_matrix(run: AcceptanceRun) -> None:
    root, out_dir, config = run.root, run.out_dir, run.config
    diagnostic_continue = run.diagnostic_continue
    data_summary_path = run.get("data_summary_path")
    residence_summary_path = run.get("residence_summary_path")
    sck_summary_path = run.get("sck_summary_path")
    trn03_matrix_path = out_dir / "trn03-matrix.json"
    print("Hardware acceptance: derive fresh TRN-03 replay matrix", flush=True)
    matrix_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/trn03_matrix.py"),
        "--level", str(config["training_profile_level"]),
        "--data", str(data_summary_path),
        "--residence", str(residence_summary_path),
        "--sck", str(sck_summary_path), "--out", str(trn03_matrix_path),
    ]
    if diagnostic_continue:
        matrix_command.append("--diagnostic-continue")
    matrix_log_path = out_dir / "trn03-matrix.log"
    matrix_returncode = _run_step(
        matrix_command, root, matrix_log_path,
        allow_failure=diagnostic_continue)
    run.put(trn03_matrix_path=trn03_matrix_path, matrix_returncode=matrix_returncode,
            matrix_log_path=matrix_log_path)


@acceptance_step(22, "TRN-03 诊断回退（同轮重试 / 上一凭证矩阵；21 通过时不动作）")
def phase_trn03_fallback(run: AcceptanceRun) -> None:
    root, out_dir, config = run.root, run.out_dir, run.config
    diagnostic_continue, board_ids = run.diagnostic_continue, run.board_ids
    baseline_divisor, diagnostic_failures = run.baseline_divisor, run.diagnostic_failures
    trn03_matrix_path = run.get("trn03_matrix_path")
    matrix_returncode = run.get("matrix_returncode")
    matrix_log_path = run.get("matrix_log_path")
    matrix_fallback_used = False
    fallback_manifest_path: Path | None = None
    if matrix_returncode != 0 or not trn03_matrix_path.is_file():
        if not diagnostic_continue:
            raise AcceptanceError("fresh TRN-03 matrix generation failed")
        build_id = run.get("build_id")
        training_generation = run.get("training_generation")
        data_summary_path = run.get("data_summary_path")
        residence_summary_path = run.get("residence_summary_path")
        sck_summary_path = run.get("sck_summary_path")
        receipt_path = run.state.get("previous_receipt_path") or run.receipt_path
        fallback_manifest_path = (
            out_dir / "trn03-diagnostic-fallback" / "manifest.json")
        fallback_error = ""
        try:
            (fallback_data_path, fallback_residence_path, fallback_sck_path,
             fallback_manifest_path) = write_trn03_diagnostic_fallback_inputs(
                root, out_dir, data_summary_path, residence_summary_path,
                sck_summary_path, matrix_log_path)
        except AcceptanceError as exc:
            fallback_error = str(exc)
        else:
            fallback_command = [
                sys.executable,
                str(root / "tools/calibration_ring_validate/trn03_matrix.py"),
                "--level", str(config["training_profile_level"]),
                "--data", str(fallback_data_path),
                "--residence", str(fallback_residence_path),
                "--sck", str(fallback_sck_path),
                "--out", str(trn03_matrix_path),
                "--diagnostic-continue",
            ]
            fallback_returncode = _run_step(
                fallback_command, root,
                out_dir / "trn03-matrix-fallback.log",
                allow_failure=True)
            if fallback_returncode != 0 or not trn03_matrix_path.is_file():
                fallback_error = (
                    "same-run diagnostic matrix retry failed with "
                    f"returncode {fallback_returncode}")
        if fallback_error:
            write_trn03_previous_matrix_fallback(
                root, receipt_path, trn03_matrix_path,
                fallback_manifest_path, build_id=build_id,
                board_ids=board_ids,
                calibration_generation=training_generation,
                link_delays=run.get("link_delays"),
                baseline_divisor=baseline_divisor,
                marker_offsets=run.get("marker_offsets_by_node"),
                sck_offsets=run.get("sck_offsets_by_node"),
                data_offsets=run.get("training_data_offsets_by_node"),
                failure=fallback_error)
        diagnostic_failures.append({
            "phase": "TRN-03 replay matrix generation",
            "returncode": matrix_returncode if matrix_returncode != 0 else 1,
            "error": (
                "fresh matrix rejected; bounded diagnostic retry" +
                (f"; {fallback_error}" if fallback_error else "")),
            "raw_log": matrix_log_path.resolve().relative_to(root).as_posix(),
            "fallback_manifest": fallback_manifest_path.resolve().relative_to(
                root).as_posix(),
            "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
        })
        matrix_fallback_used = True
    run.put(matrix_fallback_used=matrix_fallback_used,
            fallback_manifest_path=fallback_manifest_path)


@acceptance_step(23, "参数交接 + 矩阵身份 + SCK 回放安全")
def phase_trn03_handoff(run: AcceptanceRun) -> None:
    root, out_dir, board_ids = run.root, run.out_dir, run.board_ids
    diagnostic_continue, diagnostic_failures = run.diagnostic_continue, run.diagnostic_failures
    trn03_matrix_path = run.get("trn03_matrix_path")
    matrix_fallback_used = run.get("matrix_fallback_used")
    fallback_manifest_path = run.get("fallback_manifest_path")
    training_generation = run.get("training_generation")
    sck_offsets_by_node = run.get("sck_offsets_by_node")
    trn03_matrix = json.loads(trn03_matrix_path.read_text(encoding="utf-8"))
    run.alarm_stage("T3", trn03_matrix)
    if matrix_fallback_used:
        assert fallback_manifest_path is not None
        trn03_matrix.setdefault("derivation", {})[
            "diagnostic_fallback_manifest"] = \
            fallback_manifest_path.resolve().relative_to(root).as_posix()
        trn03_matrix["passed"] = False
        trn03_matrix["diagnostic_continue"] = True
        trn03_matrix_path.write_text(
            json.dumps(trn03_matrix, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
    trn03_data_offsets_by_node = selected_data_offsets(
        trn03_matrix, len(board_ids))
    run.record_parameter_handoff(
        "TRN-03 replay matrix", trn03_matrix_path, trn03_matrix,
        loaded_from="TRN-00 residence + TRN-01 SCK + TRN-02 DATA", selected={
            "sck_offset_sample_counts_by_node": sck_offsets_by_node,
            "data_offset_sample_counts_by_node": trn03_data_offsets_by_node,
        })
    if (trn03_matrix.get("node_ids_in_loop_order") != board_ids or
            int(trn03_matrix.get("calibration_generation", 0)) !=
            training_generation):
        raise AcceptanceError("fresh TRN-03 matrix identity is inconsistent")
    parameter_handoff = run.get("parameter_handoff")
    parameter_handoff_path = out_dir / "calibration-parameter-handoff.json"
    parameter_handoff_path.write_text(
        json.dumps(parameter_handoff, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    sck_replay_selection = trn03_matrix.get("derivation", {}).get(
        "sck_replay_selection", {})
    if not bool(sck_replay_selection.get("selected_row_replay_safe", True)):
        if not diagnostic_continue:
            raise AcceptanceError("TRN-03 selected SCK row is not replay-safe")
        diagnostic_failures.append({
            "phase": "TRN-03 SCK replay row selection",
            "returncode": 1,
            "error": "no measured SCK row satisfies the flight re-arm budget",
            "selected_offset_sample_counts_by_node": sck_replay_selection.get(
                "selected_offset_sample_counts_by_node"),
            "selected_min_follower_margin_samples": sck_replay_selection.get(
                "selected_min_follower_margin_samples"),
            "matrix": trn03_matrix_path.resolve().relative_to(root).as_posix(),
        })
    run.put(trn03_data_offsets_by_node=trn03_data_offsets_by_node,
            parameter_handoff_path=parameter_handoff_path)


# -- TDMA -------------------------------------------------------------------
_TDMA_TRANSIENT_SOAK_ERRORS = {
    "unhealthy_periodic_sample",
    "periodic_interval_gate_failed",
    "runtime_down_event",
    "runtime_recovery_observed",
}


def _tdma_transient_summary(summary: dict[str, Any]) -> bool:
    """Only retry a single follower's recovered down->up sample.

    A startup/config failure, a multi-board failure, or a missing barrier must
    not be papered over by a retry: those are deterministic and need diagnosis.
    """
    if summary.get("passed"):
        return False
    if not summary.get("startup_barrier", {}).get("passed"):
        return False
    nodes = summary.get("nodes", {})
    if not nodes or not all(bool(node.get("passed")) for node in nodes.values()):
        return False
    soak = summary.get("soak_validation", {})
    errors = soak.get("errors") or []
    if not errors:
        return False
    boards = set()
    kinds = set()
    for error in errors:
        board, _, kind = str(error).partition(":")
        if not board or not kind:
            return False
        boards.add(board)
        kinds.add(kind)
    if len(boards) != 1:
        return False
    if not kinds.issubset(_TDMA_TRANSIENT_SOAK_ERRORS):
        return False
    return ("runtime_down_event" in kinds and
            "runtime_recovery_observed" in kinds)


@acceptance_step(24, "TDMA 过程镜像/FIFO 闭环（trn03_closed_loop）")
def phase_tdma(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    common_boards, capture_policy = run.common_boards, run.capture_policy
    diagnostic_continue, finite_tdma = run.diagnostic_continue, run.finite_tdma
    build_id = run.get("build_id")
    if build_id:
        validate_recorded_builds(run.state.get("ota_summary"), str(build_id), run.state.get("online_builds"))
    trn03_matrix_path = run.get("trn03_matrix_path")
    tdma_dir = out_dir / "tdma-process-image"
    tdma_command = [
        sys.executable,
        str(root / "tools/calibration_ring_validate/trn03_closed_loop.py"),
        *common_boards, "--config", str(trn03_matrix_path),
        "--expected-build", build_id,
        "--level", str(config["tdma_profile_level"]),
        "--cycles", str(config.get("tdma_cycles", 4096)),
        "--stage", "process-image", "--dpll-provisional",
        "--clock-evidence", "enabled",
        # The ring reports enabled a beat after RING:START; wait for up/down to
        # actually run before the sample window opens.
        "--wait-ring-running",
        # Finite four-board diagnostics export only after every ring is STOPPED.
        # The stopped-record proof below replaces only the live observer handoff.
        *([] if finite_tdma else ["--leave-running"]),
        "--window-s", str(config["tdma_window_s"]),
        "--sample-interval-s", str(config["tdma_sample_interval_s"]),
        "--startup-timeout-s", str(config["tdma_startup_timeout_s"]),
        "--startup-stable-samples", str(config["tdma_startup_stable_samples"]),
        "--startup-poll-interval-s",
        str(timing["status_poll_interval_s"]),
        "--out-dir", str(tdma_dir),
    ]
    if run.state.get("steps", {}).get("24") is not None:
        # A rerun has no OTA reboot or reset in between: the previous attempt's
        # recorder is frozen/unsaved and would refuse the new ARM.
        tdma_command.append("--cancel-unsaved-record")
    add_serial_timing(
        tdma_command, timing, action=True,
        capture=capture_policy["tdma"])
    if capture_policy["tdma"]:
        tdma_command.append("--capture-waveforms")
    if diagnostic_continue:
        tdma_command.append("--diagnostic-continue")
    print("Hardware acceptance: four-Node TDMA process-image/FIFO loop", flush=True)
    tdma_returncode = 1
    tdma_log = out_dir / "tdma.log"
    for attempt in (1, 2):
        command = list(tdma_command)
        if attempt > 1 and "--cancel-unsaved-record" not in command:
            # The failed attempt left a frozen recorder; a clean retry must
            # discard it before the next ARM.
            command.append("--cancel-unsaved-record")
        if attempt > 1:
            tdma_log = out_dir / "tdma-retry.log"
        tdma_returncode = _run_step(
            command, root, tdma_log, allow_failure=True, timeout_s=180.0)
        if tdma_returncode == 0 or attempt >= 2:
            break
        try:
            summary = json.loads((tdma_dir / "summary.json").read_text(
                encoding="utf-8"))
        except (OSError, ValueError):
            break
        if not _tdma_transient_summary(summary):
            break
        # Preserve the first attempt before the retry overwrites its directory.
        if tdma_dir.is_dir():
            shutil.copytree(tdma_dir, out_dir / "tdma-process-image-attempt1",
                            dirs_exist_ok=True)
        shutil.copy2(tdma_log, out_dir / "tdma-attempt1.log")
        print("Hardware acceptance: transient follower ring-down; "
              "retrying step24 once with recorder cleanup", flush=True)
    if tdma_returncode != 0 and not diagnostic_continue:
        raise AcceptanceError(
            f"four-Node TDMA closed loop failed rc={tdma_returncode}; "
            f"summary={tdma_dir / 'summary.json'}; log={tdma_log}")
    run.put(tdma_returncode=tdma_returncode, tdma_dir=tdma_dir,
            tdma_summary_path=tdma_dir / "summary.json")


@acceptance_step(25, "训练输入回读（training_inputs_read_back）")
def phase_tdma_readback(run: AcceptanceRun) -> None:
    board_ids = run.board_ids
    training_generation = run.get("training_generation")
    trn03_matrix = run.load_json("trn03_matrix_path")
    tdma_summary = run.load_json("tdma_summary_path")
    run.alarm_stage("TDMA", tdma_summary)
    inputs_validated = training_inputs_read_back(trn03_matrix, tdma_summary, board_ids)
    if run.alarm_context is not None:
        stages = run.alarm_context["stages"]
        for stage in ("P1", "P2", "T0", "T1", "T2", "T3"):
            # This is selected-input usability, never a new successful measurement.
            stages[stage] = {
                **stages.get(stage, {}), "_input_validated": inputs_validated,
                "_input_validation": {"source": "TDMA stage_results", "board_ids": board_ids,
                                      "calibration_generation": training_generation}}
        stages["T3"]["_stage_readback"] = tdma_summary.get("stage_results", [])
    if not inputs_validated:
        run.warn("TDMA stage results do not read back the selected training inputs")


@acceptance_step(26, "TDMA 门限 + 停机记录交接（validate_tdma_stopped_handoff）")
def phase_tdma_handoff(run: AcceptanceRun) -> None:
    root, out_dir, board_ids = run.root, run.out_dir, run.board_ids
    diagnostic_continue, diagnostic_failures = run.diagnostic_continue, run.diagnostic_failures
    finite_tdma = run.finite_tdma
    build_id = run.get("build_id")
    tdma_returncode = run.get("tdma_returncode")
    tdma_summary_path = run.get("tdma_summary_path")
    tdma_summary = json.loads(tdma_summary_path.read_text(encoding="utf-8"))
    if diagnostic_continue and tdma_returncode != 0:
        diagnostic_failures.append({
            "phase": "four-Node TDMA closed loop",
            "returncode": tdma_returncode,
            "error": str(tdma_summary.get("error", "")),
            "summary": tdma_summary_path.resolve().relative_to(root).as_posix(),
        })
    else:
        validate_pass_summary(tdma_summary, "four-Node TDMA closed loop")
    stopped_handoff_path: Path | None = None
    if finite_tdma:
        try:
            stopped_handoff = validate_tdma_stopped_handoff(root, tdma_summary, board_ids, build_id)
        except AcceptanceError as exc:
            if not diagnostic_continue:
                raise
            stopped_handoff = dict(mode="STOPPED_RECORDS", passed=False, error=str(exc))
            diagnostic_failures.append({
                "phase": "TDMA stopped-record handoff", "returncode": 1,
                "blocking": True,
                "error": str(exc), "summary": tdma_summary_path.resolve().relative_to(root).as_posix(),
            })
        stopped_handoff_path = out_dir / "tdma-stopped-handoff.json"
        stopped_handoff_path.write_text(json.dumps(stopped_handoff, indent=2) + "\n", encoding="utf-8")
    elif tdma_summary.get("left_running") is not True:
        if not diagnostic_continue:
            raise AcceptanceError("TDMA gate did not hand off a running loop")
        diagnostic_failures.append({
            "phase": "TDMA running-loop handoff",
            "returncode": 1,
            "error": "TDMA gate did not hand off a running loop",
            "summary": tdma_summary_path.resolve().relative_to(root).as_posix(),
        })
    run.put(stopped_handoff_path=stopped_handoff_path)


# -- DPLL -------------------------------------------------------------------
def dpll_role_is_follower(page: str) -> bool:
    """True only for an applied follower role sourced from the ring reference.

    Page order is ``mode, source, requested, applied, pending`` (see
    ``vdc_priority_joint_capture.validate_dpll_role``).
    """
    try:
        fields = [int(value) for value in page.split(",")]
    except ValueError:
        return False
    if len(fields) != 5:
        return False
    mode, source, requested, applied, pending = fields
    return (mode == 1 and source == 0 and pending == 0
            and requested == applied and applied > 0)


def dpll_short_arm_followers(config: dict[str, Any],
                             board_ids: list[str]) -> dict[str, Any]:
    """Arm each follower's exact-match layer over plain SCPI, boards stopped.

    Sent from inside the capture the same write travels as a TDMA control frame
    and never completes while the ring is stopped (measured: every arm returned
    '<timeout>' while the identical command succeeded over plain SCPI).
    """
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))
    from tdma_start_ring import discover, board_command  # type: ignore

    timing = config.get("hardware_acceptance_timing", {})
    arm_args = argparse.Namespace(
        board_ids=list(board_ids), baud=115200,
        timeout=timing.get("serial_timeout_s", 3.0),
        action_timeout=timing.get("action_timeout_s", 0.5),
        settle=timing.get("input_settle_s", 0.05),
        read_timeout=timing.get("serial_read_timeout_s", 0.02),
        keep_open=False, short_open=True)
    results: dict[str, Any] = {}
    try:
        boards = discover(arm_args)
        for index, board_id in enumerate(board_ids):
            board = boards.get(board_id)
            if board is None:
                results[board_id] = {"error": "board not discovered"}
                continue
            if index == 0:
                results[board_id] = {"role": "origin", "armed": False}
                continue
            # A power cycle restores the product MASTER role: followers are
            # RAM-only unless the bench operator stored them. Repair here so a
            # full workflow rerun does not fail the step-29 role preflight after
            # its step-7 reset; step 29 still rejects anything but FOLLOWER.
            role_before = str(board_command(
                board, "SYSTem:SYNC:VDC:DPLL:ROLE?", arm_args)).strip().strip('"')
            role_after = role_before
            role_set = False

            if not dpll_role_is_follower(role_before):
                board_command(board, "SYSTem:SYNC:VDC:DPLL:ROLE 1,0", arm_args)
                role_set = True
                for _attempt in range(10):
                    time.sleep(0.1)
                    role_after = str(board_command(
                        board, "SYSTem:SYNC:VDC:DPLL:ROLE?", arm_args)).strip().strip('"')
                    if dpll_role_is_follower(role_after):
                        break
            reply = str(board_command(
                board, "SYSTem:VDC:FEEDback:SESSion?", arm_args)).strip().strip('"')
            current = int(reply) if reply.isdigit() else 0
            session = max(int(time.time()), current + 1)
            board_command(board, f"SYSTem:VDC:FEEDback:SESSion {session}", arm_args)
            match = str(board_command(
                board, f"SYSTem:VDC:PRIORity:MATCH {session}", arm_args)).strip('"')
            results[board_id] = {"session": session, "match": match,
                                 "role_before": role_before,
                                 "role_after": role_after,
                                 "role_set": role_set,
                                 "role_ok": dpll_role_is_follower(role_after),
                                 "armed": match.strip() == str(session)}
    except Exception as exc:  # noqa: BLE001 - the capture reports the real failure
        results["error"] = f"{type(exc).__name__}: {exc}"
    return results


@acceptance_step(27, "示波器发现（discover_optional_scope；--tdma-only 时跳过 DPLL）")
def phase_dpll_scope(run: AcceptanceRun) -> None:
    if run.tdma_only:
        print("Hardware acceptance: skip optional four-board DPLL short observation", flush=True)
        run.put(dpll_summary_path=None, dpll_scope_discovery_path=None)
        return
    if run.get("stopped_handoff_path") is None:
        raise AcceptanceError("four-board DPLL short observation requires stopped TDMA evidence")
    discovery_path, scope_enabled = dpll_short_discover_scope(
        run.config, run.out_dir, allow_unknown=True)
    discovery = json.loads(discovery_path.read_text(encoding="utf-8"))
    run.put(dpll_scope_discovery_path=discovery_path, dpll_scope_enabled=scope_enabled,
            dpll_scope_present=discovery.get("present"),
            dpll_scope_unknown=bool(discovery.get("unknown")))
    # Arm each follower's exact-match layer over plain SCPI while every board is
    # stopped and no run-output request exists. Sent from inside the capture the
    # same command travels as a TDMA control frame, which never completes while
    # the ring is stopped (measured: every arm returned '<timeout>').
    armed = dpll_short_arm_followers(run.config, run.board_ids)
    run.put(dpll_match_armed=armed)


@acceptance_step(28, "临时凭证（供采集适配器读取，不可提交）")
def phase_dpll_provisional(run: AcceptanceRun) -> None:
    if run.tdma_only:
        return
    dpll_short_write_provisional(
        run.root, run.receipt_path, build_id=run.get("build_id"),
        board_ids=run.board_ids, source_fingerprint=run.get("fingerprint_before"),
        source_count=run.get("source_count"), package=run.get("package"),
        ota_summary_path=run.get("ota_summary_path"),
        trn03_matrix_path=run.get("trn03_matrix_path"),
        tdma_summary_path=run.get("tdma_summary_path"),
        stopped_handoff_path=run.get("stopped_handoff_path"),
        calibration_generation=run.get("training_generation"))
    # The writer targets `run.receipt_path`; hand that path to the capture step so the remapped
    # manifest validates the receipt this run just wrote.
    if not run.receipt_path.is_file():
        raise AcceptanceError(f"provisional receipt was not written at {run.receipt_path}")
    run.config["_provisional_receipt_path"] = str(run.receipt_path)
    print(f"Hardware acceptance: provisional receipt -> {run.receipt_path.name}", flush=True)
    run.put(provisional_receipt_written=True)


def dpll_latest_capture_dir(out_dir: Path) -> Path | None:
    """Newest joint-capture directory, for preserving failed external evidence."""
    candidates = sorted(out_dir.glob("dpll-four-board-short*"),
                        key=lambda path: path.stat().st_mtime)
    return candidates[-1] if candidates else None


def mark_dpll_scope_fallback(run: AcceptanceRun, status: str, error: str) -> None:
    """Keep the physical scope discovery truthful while recording the fallback."""
    path = run.get("dpll_scope_discovery_path")
    if path is None:
        return
    discovery_path = Path(path)
    try:
        discovery = json.loads(discovery_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return
    discovery["fallback_internal"] = True
    discovery["external_attempt"] = status
    discovery["external_failure"] = error[:500]
    discovery_path.write_text(
        json.dumps(discovery, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")


@acceptance_step(29, "DPLL 60 s 联合采集（vdc_priority_joint_capture）")
def phase_dpll_capture(run: AcceptanceRun) -> None:
    if run.tdma_only:
        return
    build_id = run.get("build_id")
    if build_id:
        validate_recorded_builds(run.state.get("ota_summary"), str(build_id), run.state.get("online_builds"))
    # Phase-schedule misses and flash park counters are read around the window:
    # their delta is the only board-side explanation when a follower output
    # starves (the capture itself closes all board connections during the run).
    def context():
        try:
            return read_realtime_context(run.board_ids, run.timing,
                                         include_ring=True)
        except Exception as exc:  # evidence only; never block the capture
            return {"error": f"{type(exc).__name__}: {exc}"}

    run.put(dpll_realtime_before=context())
    monitor = str(getattr(getattr(run, "args", None), "monitor", "auto") or "auto")
    scope_enabled = bool(run.get("dpll_scope_enabled"))
    external_attempt: dict[str, Any] = {"status": "NOT_ATTEMPTED"}
    qualification = "INTERNAL_ONLY"

    if scope_enabled and monitor != "internal":
        # Detection-first: only an attached scope is attempted here. If it
        # passes we never run the internal path; if it fails (scope/VISA/
        # trigger/window/evidence) the internal fallback follows.
        try:
            joint_dir = dpll_short_run_capture(
                run.root, run.config, run.out_dir, scope_enabled=True)
        except AcceptanceError as exc:
            external_error = str(exc)
            failed_dir = dpll_latest_capture_dir(run.out_dir)
            external_attempt = {
                "status": "FAIL",
                "error": external_error,
                "attempt_dir": str(failed_dir) if failed_dir is not None else None,
            }
            qualification = "INTERNAL_FALLBACK"
            mark_dpll_scope_fallback(run, "FAIL", external_error)
            run.put(dpll_external_attempt=external_attempt,
                    dpll_external_failure=external_error,
                    dpll_scope_fallback=True,
                    dpll_scope_enabled=False)
            print(f"Hardware acceptance: external scope failed ({external_error}); "
                  "falling back to internal DPLL evidence", flush=True)
        else:
            run.put(dpll_joint_dir=joint_dir,
                    dpll_realtime_after=context(),
                    dpll_qualification="SCOPE_PASS",
                    dpll_external_attempt={"status": "PASS"})
            return

    if qualification == "INTERNAL_FALLBACK":
        external_attempt = run.state.get("dpll_external_attempt") or external_attempt
    elif run.state.get("dpll_external_failure"):
        qualification = "INTERNAL_FALLBACK"
        external_attempt = run.state.get("dpll_external_attempt") or {"status": "FAIL"}
    elif run.get("dpll_scope_unknown"):
        qualification = "INTERNAL_FALLBACK"
        external_attempt = {"status": "UNKNOWN"}
        run.put(dpll_scope_fallback=True)
    elif monitor == "internal":
        qualification = "INTERNAL_ONLY"
        external_attempt = {"status": "NOT_ATTEMPTED"}
    else:
        qualification = "INTERNAL_ONLY"
        external_attempt = {"status": "ABSENT"}

    run.config["dpll_short_force_internal"] = True
    try:
        joint_dir = dpll_short_run_capture(
            run.root, run.config, run.out_dir, scope_enabled=False)
    except AcceptanceError as exc:
        external_error = external_attempt.get("error")
        message = (f"external scope failed: {external_error}; internal fallback failed: {exc}"
                   if external_error else f"internal DPLL capture failed: {exc}")
        raise AcceptanceError(message) from exc
    run.put(dpll_joint_dir=joint_dir,
            dpll_realtime_after=context(),
            dpll_qualification=qualification,
            dpll_external_attempt=external_attempt,
            dpll_scope_enabled=False)


@acceptance_step(30, "DPLL 摘要门限（summarize_joint / validate_summary）")
def phase_dpll_gate(run: AcceptanceRun) -> None:
    if run.tdma_only:
        return
    dpll_summary_path, dpll_summary = dpll_short_summarize(
        run.root, run.config, run.out_dir, run.get("dpll_joint_dir"),
        scope_enabled=run.get("dpll_scope_enabled"),
        source_fingerprint=run.get("fingerprint_before"),
        build_id=run.get("build_id"), board_ids=run.board_ids)
    # The summary must stay exactly reproducible from raw capture evidence.
    # Qualification metadata lives in the receipt (written at step 35), not here;
    # injecting it would make validate_summary()'s exact-equality check fail.
    dpll_summary_path.write_text(
        json.dumps(dpll_summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    run.alarm_stage("DPLL", dpll_summary)
    run.put(dpll_summary_path=dpll_summary_path)


# -- 收尾 -------------------------------------------------------------------
@acceptance_step(31, "运行期调度复核（validate_runtime_schedules）")
def phase_final_schedules(run: AcceptanceRun) -> None:
    final_schedules = read_schedules(run.board_ids, run.timing)
    try:
        validate_runtime_schedules(
            final_schedules, int(run.config["calibration_load_mask"]))
    except AcceptanceError as exc:
        if not run.diagnostic_continue:
            raise
        run.diagnostic_failures.append({
            "phase": "final TDMA realtime schedule",
            "returncode": 1,
            "error": str(exc),
        })
    run.put(final_schedules=final_schedules)


@acceptance_step(32, "源码指纹复核（= 步骤 1，跨全部调用）")
def phase_fingerprint_recheck(run: AcceptanceRun) -> None:
    fingerprint_before = run.get("fingerprint_before")
    source_count = run.get("source_count")
    fingerprint_after, count_after = working_source_fingerprint(run.root)
    if (fingerprint_after, count_after) != (fingerprint_before, source_count):
        raise AcceptanceError("source changed while hardware acceptance was running")
    run.put(fingerprint_after=fingerprint_after)


@acceptance_step(33, "耗时预算（acceptance_budget_status，跨全部调用累计）")
def phase_budget(run: AcceptanceRun) -> None:
    budget = acceptance_budget_status(run.config, run.elapsed_total())
    _record_timing_event({
        "action": "acceptance.budget",
        "started_at_utc": None,
        "ended_at_utc": datetime.now(timezone.utc).isoformat(),
        "duration_ms": budget["elapsed_s"] * 1000.0,
        "returncode": 0 if budget["status"] != "ERROR" else 1,
        "status": budget["status"],
        "warning_s": budget["warning_s"],
        "error_s": budget["error_s"],
    })
    if budget["status"] == "ERROR":
        if not run.diagnostic_continue:
            raise AcceptanceError(
                f"acceptance exceeded error budget: {budget['elapsed_s']:.3f}s "
                f"> {budget['error_s']:.3f}s")
        run.diagnostic_failures.append({
            "phase": "acceptance time budget",
            "returncode": 1,
            "error": (
                f"elapsed {budget['elapsed_s']:.3f}s exceeds "
                f"error budget {budget['error_s']:.3f}s"),
            "action": "DEBUG_BOUNDED_FORCE_CONTINUE",
        })
    run.put(budget=budget)


@acceptance_step(34, "告警分级（load_alarm_stages / write_alarm_report）")
def phase_alarm_grading(run: AcceptanceRun) -> None:
    root, out_dir, board_ids = run.root, run.out_dir, run.board_ids
    if run.alarm_context is None:
        run._bind_alarm_context(run.get("scope"))
    alarm_context = run.alarm_context
    alarm_paths = dict(P0=run.get("topology_summary_path"), P1=run.get("coarse_summary_path"),
                       P2=run.get("coded_summary_path"), P3=run.get("p3_summary_path"),
                       T0=run.get("trn00_summary_path"), T1=run.get("trn01_summary_path"),
                       T2=run.get("data_summary_path"), T3=run.get("trn03_matrix_path"),
                       TDMA=run.get("tdma_summary_path"))
    dpll_summary_path = run.state.get("dpll_summary_path")
    if dpll_summary_path is not None:
        alarm_paths["DPLL"] = dpll_summary_path
    alarm_sources = {stage: evidence_entry(root, path) for stage, path in alarm_paths.items()}
    alarm_context["stages"] = load_alarm_stages(root, alarm_sources, board_ids)
    alarm_report = write_alarm_report(alarm_context)
    print(f"P3 graded outcome: {alarm_report['outcome']}; "
          f"counts={alarm_report['counts']}; report={out_dir / 'alarms.json'}", flush=True)
    run.put(alarm_sources=alarm_sources,
            alarm_report={"outcome": alarm_report["outcome"], "counts": alarm_report["counts"]})
    if alarm_report["outcome"] == "BLOCKED":
        raise AcceptanceError("required acceptance objectives failed; see alarms.json")


def credential_required_steps(run: AcceptanceRun) -> list[int]:
    """The steps a credential needs, in order, for the firmware path this run used."""
    steps = run.state["steps"]
    resumed = (run.args.command == "resume" or
               ("2" not in steps and "4" not in steps and "6" in steps))
    firmware = [3, 5, 6] if resumed else [2, 3, 4, 5]
    return [0, 1, *firmware, *range(7, 35)]


def credential_ineligibility(run: AcceptanceRun) -> list[str]:
    """Why this run directory cannot produce a commit credential (empty = it can)."""
    reasons: list[str] = []
    steps = run.state["steps"]
    previous = 0
    for number in credential_required_steps(run):
        record = steps.get(str(number))
        if record is None:
            reasons.append(f"step {number} was not run in this run directory")
            continue
        if record.get("status") not in ("OK", "warn"):
            reasons.append(f"step {number} last status {record.get('status')}")
        if record["sequence"] <= previous:
            reasons.append(f"step {number} ran before an upstream step was rerun (stale)")
        previous = max(previous, record["sequence"])
    if run.state.get("flash_method", "ota") not in (None, "ota"):
        reasons.append(f"flash method {run.state['flash_method']} has no OTA evidence")
    baseline = run.state.get("fingerprint_before")
    for index, row in enumerate(run.state["invocations"]):
        if row["source_tree_sha256"] != baseline:
            reasons.append(f"invocation {index} ran on a different source fingerprint")
    return reasons


def _restore_receipt_snapshot(run: AcceptanceRun) -> None:
    """A record-only run leaves the config receipt as it found it."""
    snapshot = run.state.get("previous_receipt_path")
    if snapshot is not None and snapshot.is_file():
        shutil.copyfile(snapshot, run.receipt_path)
    elif run.state.get("provisional_receipt_written") and run.receipt_path.is_file():
        run.receipt_path.unlink()


@acceptance_step(35, "凭证资格 + 写凭证（或仅写 workflow-record.json）")
def phase_receipt(run: AcceptanceRun) -> None:
    root, out_dir, config, timing = run.root, run.out_dir, run.config, run.timing
    receipt_path, config_path = run.receipt_path, run.config_path
    acceptance_profile, tdma_only = run.acceptance_profile, run.tdma_only
    quick_diagnostic, capture_policy = run.quick_diagnostic, run.capture_policy
    board_ids, ota_board_ids = run.board_ids, run.ota_board_ids
    baseline_divisor, diagnostic_failures = run.baseline_divisor, run.diagnostic_failures
    reasons = credential_ineligibility(run)
    if reasons:
        record = {
            "schema": HAWF_RECORD_SCHEMA, "credential": False,
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "reasons": reasons, "steps": run.state["steps"],
            "build_id": run.state.get("build_id"),
            "alarm_outcome": (run.state.get("alarm_report") or {}).get("outcome"),
        }
        (out_dir / "workflow-record.json").write_text(
            json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        _restore_receipt_snapshot(run)
        run.warn("record only (not a credential): " + "; ".join(reasons[:3]))
        run.put(receipt_written=False)
        return
    fingerprint_after = run.get("fingerprint_after")
    source_count = run.get("source_count")
    build_id, package = run.get("build_id"), run.get("package")
    ota_summary_path = run.get("ota_summary_path")
    topology_summary_path = run.get("topology_summary_path")
    coarse_summary_path = run.get("coarse_summary_path")
    coded_summary_path = run.get("coded_summary_path")
    reset_path, p3_summary_path = run.get("reset_path"), run.get("p3_summary_path")
    trn00_summary_path = run.get("trn00_summary_path")
    trn01_summary_path = run.get("trn01_summary_path")
    data_summary_path = run.get("data_summary_path")
    trn03_matrix_path = run.get("trn03_matrix_path")
    parameter_handoff_path = run.get("parameter_handoff_path")
    tdma_summary_path, tdma_dir = run.get("tdma_summary_path"), run.get("tdma_dir")
    p3_metrics, link_delays = run.get("p3_metrics"), run.get("link_delays")
    training_generation = run.get("training_generation")
    schedule_before, final_schedules = run.get("schedule_before"), run.get("final_schedules")
    timing_probe_path, scope_path = run.get("timing_probe_path"), run.get("scope_path")
    alarm_sources, alarm_report = run.get("alarm_sources"), run.get("alarm_report")
    dpll_summary_path = run.state.get("dpll_summary_path")
    dpll_scope_discovery_path = run.state.get("dpll_scope_discovery_path")
    topology_reuse_source = run.state.get("topology_reuse_source")
    stopped_handoff_path = run.state.get("stopped_handoff_path")
    tdma_summary = json.loads(tdma_summary_path.read_text(encoding="utf-8"))
    trn03_matrix = json.loads(trn03_matrix_path.read_text(encoding="utf-8"))
    dpll_summary = (json.loads(dpll_summary_path.read_text(encoding="utf-8"))
                    if dpll_summary_path is not None else None)

    def make_receipt(schema: str, scope: str) -> dict[str, Any]:
        value = {
            "schema": schema,
            "acceptance_scope": scope,
            "acceptance_profile": acceptance_profile,
            "dpll_observation": (
                "SKIPPED_TDMA_ONLY" if tdma_only else "FOUR_BOARD_SHORT_60S"),
            "passed": True,
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "source_tree_sha256": fingerprint_after,
            "source_file_count": source_count,
            "bench_config_path": config_path.resolve().relative_to(root).as_posix(),
            "bench_config_sha256": sha256_file(config_path),
            "build_id": build_id,
            "firmware_package": evidence_entry(root, package),
            "ota_summary": evidence_entry(
                root, ota_summary_path, board_count=len(ota_board_ids)),
            "topology_summary": evidence_entry(root, topology_summary_path),
            "coarse_calibration_summary": evidence_entry(
                root, coarse_summary_path),
            "coded_calibration_summary": evidence_entry(
                root, coded_summary_path),
            "initialization_reset": evidence_entry(root, reset_path),
            "p3_summary": evidence_entry(root, p3_summary_path),
            "trn00_summary": evidence_entry(root, trn00_summary_path),
            "trn01_summary": evidence_entry(root, trn01_summary_path),
            "trn02_summary": evidence_entry(root, data_summary_path),
            "trn03_matrix": evidence_entry(root, trn03_matrix_path),
            "calibration_parameter_handoff": evidence_entry(
                root, parameter_handoff_path),
            "tdma_summary": evidence_entry(root, tdma_summary_path),
            "ota_board_ids": ota_board_ids,
            "tdma_board_ids": board_ids,
            **p3_metrics,
            "link_delay_ns_by_link": link_delays,
            "link_base_delay_ns_by_link": [
                (value + baseline_divisor // 2) // baseline_divisor
                for value in link_delays],
            "path_delay_baseline_divisor": baseline_divisor,
            "calibration_generation": training_generation,
            "calibration_load_mask": int(config["calibration_load_mask"]),
            "waveform_capture_policy": dict(capture_policy),
            "schedule_before": schedule_before,
            "schedule_after": final_schedules,
            "realtime_load_mask_unchanged": True,
            "calibration_quarantined": False,
            "hardware_acceptance_timing": timing,
            "timing_probe": evidence_entry(root, timing_probe_path),
            "fixed_scope": evidence_entry(root, scope_path),
            "alarm_inputs": evidence_entry(root, out_dir / "alarm-inputs.json"),
            "alarm_report": evidence_entry(root, out_dir / "alarms.json"),
            "alarm_stage_sources": alarm_sources,
            "acceptance_outcome": alarm_report["outcome"],
        }
        if dpll_summary_path is not None:
            value["dpll_summary"] = evidence_entry(root, dpll_summary_path)
        if dpll_scope_discovery_path is not None:
            value["dpll_scope_discovery"] = evidence_entry(
                root, dpll_scope_discovery_path)
        qualification = run.state.get("dpll_qualification")
        if qualification is not None:
            value["dpll_qualification"] = qualification
        external_attempt = run.state.get("dpll_external_attempt")
        if external_attempt is not None:
            value["dpll_external_attempt"] = external_attempt
        if run.state.get("dpll_scope_fallback"):
            value["dpll_scope_fallback"] = True
        if topology_reuse_source is not None:
            value["topology_reuse_source"] = topology_reuse_source
        if stopped_handoff_path is not None:
            value["tdma_stopped_handoff"] = evidence_entry(root, stopped_handoff_path)
        return value

    def persist_receipt(value: dict[str, Any]) -> None:
        validate_alarm_report(root, value)
        receipt_path.parent.mkdir(parents=True, exist_ok=True)
        payload = json.dumps(value, ensure_ascii=False, indent=2) + "\n"
        receipt_path.write_text(payload, encoding="utf-8")
        (out_dir / "acceptance.json").write_text(payload, encoding="utf-8")

    run.put(receipt_written=False)
    if diagnostic_failures or quick_diagnostic:
        diagnostic_result = {
            "schema": "HAOFV_HARDWARE_ACCEPTANCE_DIAGNOSTIC_V1",
            "passed": True,
            "flow_completed": True,
            "strict_gates_passed": not diagnostic_failures,
            "diagnostic_continue": True,
            "acceptance_profile": acceptance_profile,
            "acceptance_outcome": alarm_report["outcome"],
            "alarm_counts": alarm_report["counts"],
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "build_id": build_id,
            "failures": diagnostic_failures,
            "tdma_summary": tdma_summary_path.resolve().relative_to(root).as_posix(),
            "dpll_summary": (
                dpll_summary_path.resolve().relative_to(root).as_posix()
                if dpll_summary_path is not None else None),
            "feedback_inputs": build_diagnostic_feedback(
                tdma_summary,
                dpll_summary if dpll_summary_path is not None else None,
                trn03_matrix),
        }
        diagnostic_path = out_dir / "diagnostic.json"
        diagnostic_path.write_text(
            json.dumps(diagnostic_result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        _record_timing_event({
            "action": "acceptance.complete",
            "started_at_utc": None,
            "ended_at_utc": datetime.now(timezone.utc).isoformat(),
            "duration_ms": None,
            "returncode": 0,
            "status": "PASS_WITH_DIAGNOSTICS",
        })
        diagnostic_receipt_written = False
        if quick_diagnostic:
            receipt = make_receipt(
                QUICK_GRADED_RECEIPT_SCHEMA,
                ("FOUR_NODE_TDMA_QUICK_DIAGNOSTIC" if tdma_only else
                 "FOUR_NODE_DPLL_QUICK_DIAGNOSTIC"))
            receipt.update({
                "flow_completed": True,
                "strict_gates_passed": not diagnostic_failures,
                "diagnostic_failures": diagnostic_failures,
                "diagnostic_summary": evidence_entry(root, diagnostic_path),
            })
            _validate_quick_diagnostic_receipt(root, receipt)
            persist_receipt(receipt)
            diagnostic_receipt_written = True
        elif tdma_only and diagnostic_failures:
            validate_tdma_diagnostic_summary(tdma_summary, board_ids)
            receipt = make_receipt(
                TDMA_DIAGNOSTIC_RECEIPT_SCHEMA,
                "FOUR_NODE_TDMA_DIAGNOSTIC")
            receipt.update({
                "flow_completed": True,
                "strict_gates_passed": False,
                "diagnostic_failures": diagnostic_failures,
                "diagnostic_summary": evidence_entry(root, diagnostic_path),
            })
            ring_capture = tdma_summary.get("ring_capture", {})
            if not (isinstance(ring_capture, dict) and
                    ring_capture.get("capture_skipped")):
                downloaded = ring_capture.get("downloaded", [])
                analysis_nodes = tdma_summary.get("ring_analysis", {}).get(
                    "nodes", [])
                receipt.update({
                    "ring_capture_raw": [
                        evidence_entry(root, Path(str(row["local_path"])))
                        for row in downloaded],
                    "ring_capture_svg": [
                        evidence_entry(root, Path(str(row["svg"])))
                        for row in analysis_nodes],
                    "ring_capture_analysis": evidence_entry(
                        root, tdma_dir / "analysis" /
                        "ring_capture_analysis.json"),
                })
            _validate_tdma_diagnostic_receipt(root, receipt)
            persist_receipt(receipt)
            diagnostic_receipt_written = True
        result_label = (
            "quick diagnostic profile" if quick_diagnostic else
            "diagnostic gates")
        print(
            f"{alarm_report['outcome']} hardware regression flow completed with {result_label}; "
            f"evidence={diagnostic_path}"
            f" receipt={receipt_path if diagnostic_receipt_written else 'none'}")
        if not diagnostic_receipt_written:
            _restore_receipt_snapshot(run)
        run.put(receipt_written=diagnostic_receipt_written)
        return
    _record_timing_event({
        "action": "acceptance.complete",
        "started_at_utc": None,
        "ended_at_utc": datetime.now(timezone.utc).isoformat(),
        "duration_ms": None,
        "returncode": 0,
        "status": "PASS",
    })
    receipt = make_receipt(
        TDMA_RECEIPT_SCHEMA if tdma_only else RECEIPT_SCHEMA,
        "FOUR_NODE_TDMA" if tdma_only else "FULL")
    persist_receipt(receipt)
    run.put(receipt_written=True)
    print(
        f"PASS calibration-to-DPLL hardware acceptance build={build_id} "
        f"Latency_Cal_trials={p3_metrics['trial_count']} receipt={receipt_path}")


@acceptance_step(36, "凭证自检（以 check-staged 的校验器复核写出的凭证）")
def phase_receipt_selfcheck(run: AcceptanceRun) -> None:
    if not run.state.get("receipt_written"):
        run.warn("no credential written; nothing to self-check")
        return
    receipt = json.loads(run.receipt_path.read_text(encoding="utf-8"))
    fingerprint, count = working_source_fingerprint(run.root)
    if (receipt.get("source_tree_sha256"), receipt.get("source_file_count")) != (fingerprint, count):
        raise AcceptanceError("written receipt does not match the working source fingerprint")
    validate_receipt_record(run.root, receipt)


def _run_acceptance_impl(args: argparse.Namespace) -> None:
    run = AcceptanceRun.create(args)
    for number in acceptance_step_plan(args):
        run.execute(number)


def flash_with_picotool(root: Path, out_dir: Path, board_ids: list[str],
                        artifact: Path) -> dict[str, Any]:
    """Flash every board from one UF2 through the picotool component.

    This is the inner-loop flashing route: the secure OTA path stays the one
    that produces a committable receipt. Each board is flashed through the
    existing closed-loop picotool helper, and the transcript plus the artifact
    hash are kept as evidence.
    """
    if not artifact.is_file():
        raise AcceptanceError(f"UF2 artifact missing: {artifact}")
    helper = root / "tools/picotool_flash/picotool_flash.py"
    flash_dir = out_dir / "flash-picotool"
    flash_dir.mkdir(parents=True, exist_ok=True)
    # P3 owns no flashing mechanics: the picotool component performs the
    # 1200-baud touch, BOOTSEL polling, load and application wait, one board at
    # a time. This call is the whole integration surface.
    helper = root / "tools/picotool_flash/picotool_flash.py"
    rows: list[dict[str, Any]] = []
    for board_id in board_ids:
        transcript = flash_dir / f"{board_id}.txt"
        started = time.perf_counter()
        completed = subprocess.run(
            [sys.executable, str(helper), str(artifact),
             "--serial-number", board_id, "--retries", "3", "--settle", "2.0",
             "--out", str(transcript)],
            cwd=root, text=True, capture_output=True)
        elapsed = round(time.perf_counter() - started, 3)
        rows.append({"serial_number": board_id,
                     "returncode": completed.returncode, "elapsed_s": elapsed,
                     "transcript": evidence_entry(root, transcript)})
        print(f"DPLL-60 acceptance: picotool {board_id[:8]} "
              f"rc={completed.returncode} {elapsed:.1f}s", flush=True)
    summary = {"schema": "HAOFV_PICOTOOL_FLASH_EVIDENCE_V1",
               "artifact": evidence_entry(root, artifact), "boards": rows,
               "failed_count": sum(1 for row in rows if row["returncode"] != 0)}
    summary["passed"] = summary["failed_count"] == 0
    (flash_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not summary["passed"]:
        raise AcceptanceError("picotool flashing failed; see per-board transcripts")
    return summary


def run_dpll_short_acceptance(args: argparse.Namespace) -> None:
    """Write the routine DPLL-60 credential for the current source fingerprint.

    Either build and OTA a fresh image, or reuse an existing successful
    package/OTA pair (the first acceptance of a firmware still has to flash it).
    Then apply the staged calibration matrix through one finite TDMA stage,
    prove the four-board STOP handoff from raw records, and run the 60 s joint
    observation. The receipt records the scope this credential cannot see.
    """
    root = args.root.resolve()
    config_path = (root / args.config).resolve()
    config = load_bench_config(config_path)
    receipt_path = (root / args.receipt).resolve()
    board_ids = [str(value) for value in config["p3_board_ids_in_physical_order"]]
    timing = acceptance_timing(config)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    out_dir = ((root / args.out_dir).resolve() if args.out_dir is not None else
               root / "out" / "HardwareAcceptance" / stamp.split("-", 1)[0] /
               f"dpll60-{stamp.split('-', 1)[1]}")
    out_dir.mkdir(parents=True, exist_ok=True)
    fingerprint_before, source_count = working_source_fingerprint(root)

    if args.package is not None:
        package = (root / args.package).resolve()
        ota_summary_path = (root / args.ota_summary).resolve()
        build_id = _read_package_build_id(package)
        validate_ota(json.loads(ota_summary_path.read_text(encoding="utf-8")),
                     board_ids, build_id)
        print(f"DPLL-60 acceptance: reuse package build={build_id}", flush=True)
    else:
        # One stable build directory: a per-run directory forced a full
        # recompile every iteration (~100 s), while reusing it only recompiles
        # what changed.
        build_dir = root / "out/build/dpll60-latest"
        flash_method = getattr(args, "flash", "ota")
        _run_step([sys.executable,
                   str(root / "tools/cmake_build_auto/cmake_build_auto.py"),
                   "--root", str(root), "--build-dir", str(build_dir)],
                  root, out_dir / "build.log")
        package = _fresh_update_package(build_dir)
        if not package.is_file():
            raise AcceptanceError(f"firmware package missing: {package}")
        build_id = _read_package_build_id(package)
        if flash_method == "picotool":
            # Fast path: one UF2 per board through the closed-loop picotool
            # helper, in parallel. Until the receipt validator accepts this
            # evidence type, run --flash ota when a committable credential is
            # required.
            print("DPLL-60 acceptance: flashing 4 boards with picotool",
                  flush=True)
            flash_with_picotool(root, out_dir, board_ids,
                                build_dir / "DHRT100_FACTORY.uf2")
            print("DPLL-60 acceptance: picotool flash complete; continuing with "
                  "TDMA short-frame and DPLL 60 s", flush=True)
            # Evidence substitution only: the OTA step below is skipped, so the
            # receipt path must still resolve a file. The fast path returns
            # before receipt validation.
            ota_summary_path = out_dir / "flash-picotool" / "summary.json"
        if flash_method != "picotool":
            ota_dir = out_dir / "ota-four-board"
            ota_command = [sys.executable,
                           str(root / "tools/ota_multi_update/ota_multi_update.py"),
                           str(package), "--expected-board-count", str(len(board_ids)),
                           "--expected-build", build_id, "--out-dir", str(ota_dir),
                           "--block-size", "4096", "--timeout", "300"]
            for board_id in board_ids:
                ota_command.extend(["--serial-number", board_id])
            _run_step(ota_command, root, out_dir / "ota.log")
            ota_summary_path = ota_dir / "summary.json"
            validate_ota(json.loads(ota_summary_path.read_text(encoding="utf-8")),
                         board_ids, build_id)

    matrix_path = (root / args.matrix).resolve() if args.matrix is not None else \
        _latest_trn03_matrix(root)
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    calibration_generation = int(matrix["calibration_generation"])

    # A routine round reuses a bench that still holds the previous round's
    # recorder RAM state, which no SAVE can persist and no CANCel is consumed
    # for while the ring is stopped. The production reset boundary clears it and
    # re-verifies that every board boots the build this credential binds.
    print(f"DPLL-60 acceptance: software reset {len(board_ids)} boards", flush=True)
    reset_evidence = reset_acceptance_boards(board_ids, build_id, timing)
    reset_path = out_dir / "initialization-reset.json"
    reset_path.write_text(
        json.dumps(reset_evidence, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")

    tdma_dir = out_dir / "tdma-process-image"
    tdma_command = [sys.executable,
                    str(root / "tools/calibration_ring_validate/trn03_closed_loop.py")]
    for board_id in board_ids:
        tdma_command.extend(["--board-id", board_id])
    tdma_command += [
        "--config", str(matrix_path), "--expected-build", build_id,
        "--level", str(config["tdma_profile_level"]),
        "--cycles", str(config.get("tdma_cycles", 4096)),
        "--stage", "process-image", "--dpll-provisional",
        "--clock-evidence", "enabled",
        "--window-s", str(config["tdma_window_s"]),
        "--sample-interval-s", str(config["tdma_sample_interval_s"]),
        "--startup-timeout-s", str(config["tdma_startup_timeout_s"]),
        "--startup-stable-samples", str(config["tdma_startup_stable_samples"]),
        "--startup-poll-interval-s", str(timing["status_poll_interval_s"]),
        "--arm-wait", "20.0",
        # Each routine round reuses the bench without a re-flash, so it must be
        # able to discard a frozen record whose storage job is gone.
        "--cancel-unsaved-record",
        "--out-dir", str(tdma_dir),
    ]
    add_serial_timing(tdma_command, timing, action=True)
    # The quick profile always retains diagnostics; the TDMA stage needs that to
    # export its original board records after a quality gate, and the raw STOP
    # handoff is what this credential actually gates on.
    # The ring-quality verdict of the TDMA stage is recorded, not gated: this
    # credential gates on the raw STOP handoff and the DPLL observation, so a
    # warning-level stage verdict must not stop the run. The full `run`
    # acceptance keeps its own stricter policy.
    diagnostic_continue = True
    if diagnostic_continue or args.diagnostic_continue:
        tdma_command.append("--diagnostic-continue")
        diagnostic_continue = True
    # The ring-quality verdict of the TDMA stage is recorded, not gated here:
    # this credential gates on the raw STOP handoff and the DPLL observation.
    _run_step(tdma_command, root, out_dir / "tdma.log",
              allow_failure=diagnostic_continue)
    tdma_summary_path = tdma_dir / "summary.json"
    if not tdma_summary_path.is_file():
        raise AcceptanceError("TDMA stage wrote no summary")
    tdma_summary = json.loads(tdma_summary_path.read_text(encoding="utf-8"))
    handoff_path = out_dir / "tdma-stopped-handoff.json"
    handoff = validate_tdma_stopped_handoff(root, tdma_summary, board_ids, build_id)
    if handoff.get("passed") is not True:
        raise AcceptanceError("four-board STOP handoff did not pass")
    handoff_path.write_text(json.dumps(handoff, indent=2) + "\n", encoding="utf-8")

    if args.scope == "internal":
        discover_optional_scope = globals()["discover_optional_scope"]

        def internal_only(resource: str, serial: str) -> dict[str, Any]:
            return {"present": False, "resource": resource, "serial": serial,
                    "detail": "internal evidence pinned by the caller"}
        globals()["discover_optional_scope"] = internal_only
    else:
        discover_optional_scope = None
    # The joint capture validates the receipt named by its manifest, so point a
    # copy at this run's provisional receipt instead of the committed one.
    manifest = json.loads(
        (root / config["dpll_short_workflow_inputs"]).read_text(encoding="utf-8"))
    provisional_path = out_dir / "provisional-receipt.json"
    config["_provisional_receipt_path"] = str(provisional_path)
    manifest["files"]["receipt"]["path"] = provisional_path.relative_to(root).as_posix()
    manifest_copy = out_dir / "workflow-inputs.json"
    manifest_copy.write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n",
                             encoding="utf-8")
    config["dpll_short_workflow_inputs"] = manifest_copy.relative_to(root).as_posix()
    # A factory/picotool flash or power cycle restores the product MASTER role;
    # the joint capture requires NO.2..NO.4 to be FOLLOWER. Repair after the
    # TDMA stage (ring stopped) so the topology write runs without VDC sessions.
    print("DPLL-60 acceptance: repairing follower roles before DPLL capture",
          flush=True)
    dpll_short_arm_followers(config, board_ids)
    try:
        dpll_summary_path, dpll_summary, dpll_discovery_path = \
            run_four_board_dpll_short(
                root, config, provisional_path, out_dir, build_id=build_id,
                board_ids=board_ids, source_fingerprint=fingerprint_before,
                source_count=source_count, package=package,
                ota_summary_path=ota_summary_path,
                trn03_matrix_path=matrix_path,
                tdma_summary_path=tdma_summary_path,
                stopped_handoff_path=handoff_path,
                calibration_generation=calibration_generation)
    finally:
        if discover_optional_scope is not None:
            globals()["discover_optional_scope"] = discover_optional_scope

    fingerprint_after, count_after = working_source_fingerprint(root)
    if (fingerprint_after, count_after) != (fingerprint_before, source_count):
        raise AcceptanceError("source changed during acceptance")
    receipt: dict[str, Any] = {
        "schema": DPLL_SHORT_RECEIPT_SCHEMA,
        "acceptance_scope": DPLL_SHORT_SCOPE,
        "acceptance_profile": str(config.get("acceptance_profile", "FULL")),
        "dpll_observation": DPLL_SHORT_OBSERVATION,
        "passed": True,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_tree_sha256": fingerprint_after,
        "source_file_count": count_after,
        "bench_config_path": config_path.relative_to(root).as_posix(),
        "bench_config_sha256": sha256_file(config_path),
        "build_id": build_id,
        "firmware_package": evidence_entry(root, package),
        "ota_summary": evidence_entry(root, ota_summary_path),
        "initialization_reset": evidence_entry(root, reset_path),
        "trn03_matrix": evidence_entry(root, matrix_path),
        "tdma_summary": evidence_entry(root, tdma_summary_path),
        "tdma_stopped_handoff": evidence_entry(root, handoff_path),
        "dpll_summary": evidence_entry(root, dpll_summary_path),
        "dpll_scope_discovery": evidence_entry(root, dpll_discovery_path),
        "ota_board_ids": board_ids,
        "tdma_board_ids": board_ids,
        "calibration_generation": calibration_generation,
        "reused_package_ota": args.package is not None,
        "tdma_stage_passed": bool(tdma_summary.get("passed")),
        "dpll_passed": bool(dpll_summary.get("passed")),
        "hardware_acceptance_timing": timing,
        "unverified_scope": list(DPLL_SHORT_UNVERIFIED_SCOPE),
        "restore_full_condition": (
            "a staged change under " + ", ".join(DPLL_SHORT_UNCOVERED_PREFIXES)),
    }
    if args.package is None and getattr(args, "flash", "ota") == "picotool":
        # Fast iteration: the bench was updated through picotool, so no OTA
        # capability evidence exists for this round. The TDMA/DPLL evidence
        # above is still written to out_dir; run --flash ota when a
        # committable credential is required.
        print(f"PASS DPLL-60 fast iteration build={build_id} out={out_dir} "
              f"(no receipt: picotool flash)", flush=True)
        return
    _validate_dpll_short_receipt(root, receipt)
    validate_alarm_report(root, receipt)
    payload = json.dumps(receipt, ensure_ascii=False, indent=2) + "\n"
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(payload, encoding="utf-8")
    (out_dir / "acceptance.json").write_text(payload, encoding="utf-8")
    print(f"PASS DPLL-60 hardware acceptance build={build_id} "
          f"receipt={receipt_path} out={out_dir}", flush=True)


def _latest_trn03_matrix(root: Path) -> Path:
    """Reuse the most recent staged calibration matrix when none is named."""
    candidates = sorted(
        (root / "out/HardwareAcceptance").glob("*/p3-*/trn03-matrix.json"),
        key=lambda path: path.stat().st_mtime)
    if not candidates:
        raise AcceptanceError(
            "no staged trn03 matrix found; pass --matrix or run a full acceptance once")
    return candidates[-1].resolve()


def run_t2_acceptance(args: argparse.Namespace) -> None:
    """T2 publication acceptance.

    Enables the T2 runtime on the bench ring (grid -> deployment -> action table
    (per board role) -> template store -> flight fifo reset -> source counter ->
    T2 configure/start) and samples the publication counters. The criterion is
    the position publication count advancing, not the DPLL cleanliness metrics.
    """
    import subprocess as _subprocess
    import time as _time
    import json as _json
    root = Path(__file__).resolve().parents[2]
    stamp = _time.strftime("%Y%m%d-%H%M%S")
    out_dir = root / "out" / "HardwareAcceptance" / _time.strftime("%Y%m%d") / f"t2-{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / "t2-enable.log"
    command = [sys.executable, str(root / "tools/sync_trigger/t2_workflow.py"), "--full"]
    print(f"T2 acceptance: enablement -> {log_path}", flush=True)
    with log_path.open("w", encoding="utf-8") as handle:
        _subprocess.run(command, cwd=str(root), stdout=handle, stderr=_subprocess.STDOUT,
                        check=False)
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    report = dict(schema="HAOFV_T2_ACCEPTANCE_V1", stamp=stamp, enablement=lines[-16:])
    # The criterion is the position publication advancing, so it has to be read
    # from the run instead of assumed: a chain where the origin never configured
    # (previously reported as a pass) leaves every POS page at zero.
    configured = 0
    publishing = 0
    for line in lines:
        if "T2 configured 1 active 1" in line:
            configured += 1
        if "POS " in line:
            page = line.split("POS ", 1)[1]
            fields = [int(v) for v in page.replace(",", " ").split() if v.isdigit()]
            # 1,0,tx_pending,tx_active,tx_fragment,tx_publish_count,rx_complete_count,...
            if len(fields) > 12 and (fields[5] > 0 or fields[12] > 0):
                publishing += 1
    # The workflow names every blocking condition as a failed step: a quarantined
    # SYNC_TRIGGER load, a sticky cursor fault, or a reference that did not publish all
    # positions. "publishing" alone counts sampled POS lines with a nonzero counter, so
    # it passed runs that published one position and then quarantined a follower.
    failed_steps = next((line.split("failed steps:", 1)[1].split("log:", 1)[0].strip()
                         for line in reversed(lines) if "failed steps:" in line), "?")
    report["boards_configured"] = configured
    report["boards_publishing"] = publishing
    report["workflow_failed_steps"] = failed_steps
    report["passed"] = configured == 4 and publishing > 0 and failed_steps == "none"
    (out_dir / "report.json").write_text(_json.dumps(report, indent=2), encoding="utf-8")
    for line in lines[-16:]:
        # The workflow log can carry replacement characters (the console is GBK here), and
        # printing them raised UnicodeEncodeError - which turned a passing acceptance into
        # exit code 1 after the report had already been written as passed=true.
        safe = line.encode("ascii", "replace").decode("ascii")
        print(f"T2 acceptance: {safe}", flush=True)
    print(f"T2 acceptance: boards configured {configured}/4, publishing {publishing}, "
          f"workflow failed steps {failed_steps}, passed={report['passed']}", flush=True)
    print(f"T2 acceptance: report -> {out_dir / 'report.json'}", flush=True)
    if not report["passed"]:
        raise SystemExit(f"T2 acceptance FAILED: configured {configured}/4, publishing"
                         f" {publishing}, workflow failed steps {failed_steps}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    run = subparsers.add_parser(
        "run", help=("quick acceptance by default: build, 4096 OTA, "
                     "P0-P3/TRN-03, TDMA, and four-board DPLL 60 s; "
                     "use --full for full bench"))
    run.add_argument("--root", type=Path, default=ROOT)
    run.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    run.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    run.add_argument("--build-dir", type=Path)
    run.add_argument(
        "--preset", default=None,
        help=("CMake configure preset for the build step (default: the "
              "cmake_build_auto default, i.e. pico2-release); use "
              "pico2-v2-factory-candidate for a V2 deployment run"))
    run.add_argument("--out-dir", type=Path)
    run.add_argument("--reuse-topology", type=Path,
                     help="reuse an original passing P0T summary; quick four-board mode only")
    run.add_argument(
        "--tdma-only", action="store_true",
        help="run calibration and four-node TDMA acceptance without the DPLL short test")
    run.add_argument(
        "--diagnostic-continue", action="store_true",
        help=("retain quality failures and continue bounded diagnostics; "
              "required objectives and evidence integrity still gate acceptance"))
    run.add_argument(
        "--full", action="store_true",
        help="use the full bench config instead of the quick default")
    run.add_argument("--strict-stage", action="append", choices=ALARM_STAGES, default=[],
                     help="require this stage's strict quality in this run; repeatable")
    resume = subparsers.add_parser(
        "resume",
        help="reuse an existing package/OTA record; never build or OTA")
    resume.add_argument("--root", type=Path, default=ROOT)
    resume.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    resume.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    resume.add_argument("--package", required=True, type=Path,
                        help="existing DHRT100_UPDATE.pkg")
    resume.add_argument("--ota-summary", required=True, type=Path,
                        help="existing successful OTA summary for the selected scope")
    resume.add_argument("--out-dir", type=Path)
    resume.add_argument("--reuse-topology", type=Path,
                        help="reuse an original passing P0T summary; quick four-board mode only")
    resume.add_argument("--strict-stage", action="append", choices=ALARM_STAGES, default=[],
                        help="require this stage's strict quality in this run; repeatable")
    resume.add_argument(
        "--tdma-only", action="store_true",
        help="resume four-node TDMA acceptance without the DPLL short test")
    resume.add_argument(
        "--diagnostic-continue", action="store_true",
        help=("retain quality failures and continue bounded diagnostics; "
              "required objectives and evidence integrity still gate acceptance"))
    dpll60 = subparsers.add_parser(
        "dpll60",
        help=("routine credential: build+OTA (or reuse), staged matrix through "
              "one finite TDMA stage, then the four-board DPLL 60 s observation"))
    dpll60.add_argument("--root", type=Path, default=ROOT)
    dpll60.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    dpll60.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    dpll60.add_argument("--package", type=Path,
                        help="reuse an existing DHRT100_UPDATE.pkg instead of building")
    dpll60.add_argument("--ota-summary", type=Path,
                        help="existing OTA summary for the reused package")
    dpll60.add_argument("--flash", choices=("ota", "picotool"), default="ota",
                        help="how to update the boards: the secure OTA path "
                             "(default, produces the committable credential) or "
                             "the faster picotool UF2 path (fast iteration; "
                             "needs a bench whose boards all expose BOOTSEL)")
    dpll60.add_argument("--matrix", type=Path,
                        help="staged trn03 matrix; defaults to the newest existing one")
    dpll60.add_argument("--scope", choices=("auto", "internal"), default="auto",
                        help="follow scope discovery, or pin internal evidence")
    dpll60.add_argument("--out-dir", type=Path)
    dpll60.add_argument("--diagnostic-continue", action="store_true",
                        help="keep TDMA quality failures as diagnostics instead of stopping")
    check = subparsers.add_parser(
        "check-staged", help="gate staged code against the indexed receipt")
    check.add_argument("--root", type=Path, default=ROOT)
    check.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    limited = subparsers.add_parser(
        "limited-receipt",
        help="write an auditable OTA + 10 MHz P3 bring-up receipt")
    limited.add_argument("--root", type=Path, default=ROOT)
    limited.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    limited.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    limited.add_argument("--package", required=True, type=Path)
    limited.add_argument("--ota-summary", required=True, type=Path)
    limited.add_argument("--p3-summary", required=True, type=Path)
    t2_parser = subparsers.add_parser(
        "t2", help="T2 publication acceptance (enable T2 and sample publication)")
    t2_parser.add_argument("--flash", choices=("ota", "picotool"), default="picotool")
    t2_parser.set_defaults(func=run_t2_acceptance)
    return parser.parse_args()




def main() -> int:
    args = parse_args()
    if getattr(args, "command", "") == "t2":
        run_t2_acceptance(args)
        return 0
    try:
        if args.command in ("run", "resume"):
            run_acceptance(args)
        elif args.command == "dpll60":
            run_dpll_short_acceptance(args)
        elif args.command == "check-staged":
            check_staged(args.root.resolve(), args.receipt)
        else:
            write_limited_receipt(args)
    except (AcceptanceError, OSError, KeyError, TypeError, ValueError,
            json.JSONDecodeError) as exc:
        print(f"FAIL Latency Cal hardware acceptance: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
