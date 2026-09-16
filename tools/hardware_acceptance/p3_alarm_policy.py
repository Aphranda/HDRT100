"""Pure P3 reporting policy; execution and hardware safety stay with owners.

``_input_validated`` is an integration assertion that the existing owner has
checked the selected calibration input and its identity/readback. It never
changes the original measurement's ``passed`` result. ``strict_required_stages``
names stages whose quality gates must pass in this run, including quick runs.
Only an explicit ``irrecoverable_risk: True`` produces FATAL; exception text,
PIO stalls and a zero DCO correction cannot establish that classification.
"""

from __future__ import annotations

from copy import deepcopy
from typing import Any


SCHEMA = "HAOFV_P3_ACCEPTANCE_REPORT_V1"
POLICY = "FIXED_STAGE_POLICY_V1"
STAGE_ORDER = ("P0", "P1", "P2", "P3", "T0", "T1", "T2", "T3", "TDMA", "DPLL")
SEVERITIES = ("INFO", "WARN", "ERROR", "FATAL")


def _dict(value: Any) -> dict:
    return value if isinstance(value, dict) else {}


def _rows(value: Any) -> list:
    return value if isinstance(value, list) else []


def _candidate_failures(value: Any, path: str = "$"):
    collections = {"pair_results", "trials", "trial_results", "row_results", "ladder", "candidates"}
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}"
            if key in collections:
                items = enumerate(child) if isinstance(child, list) else (
                    child.items() if isinstance(child, dict) else [])
                for index, candidate in items:
                    row = _dict(candidate)
                    if row.get("passed") is False or row.get("detected") is False:
                        yield f"{child_path}[{index}]", candidate
            yield from _candidate_failures(child, child_path)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from _candidate_failures(child, f"{path}[{index}]")


def _irrecoverable_risks(value: Any, path: str = "$"):
    if isinstance(value, dict):
        if value.get("irrecoverable_risk") is True:
            yield path, value
        for key, child in value.items():
            yield from _irrecoverable_risks(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from _irrecoverable_risks(child, f"{path}[{index}]")


def _u32(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 0xFFFFFFFF


def _delta(before: dict, after: dict, deltas: dict, field: str) -> int | None:
    # Prefer paired measurements over a supplied aggregate. Half-range bounds
    # reject ordinary counter reset/regression while retaining natural wrap.
    if field in before or field in after:
        a, b = before.get(field), after.get(field)
        if not (_u32(a) and _u32(b)):
            return None
        value = (b - a) & 0xFFFFFFFF
    else:
        value = deltas.get(field)
    return value if _u32(value) and value < 0x80000000 else None


def _topology_valid(summary: dict) -> bool:
    ring = summary.get("ring_order")
    boards = summary.get("boards")
    if (summary.get("passed") is not True or not isinstance(ring, list) or
            len(ring) < 2 or any(not isinstance(uid, str) for uid in ring) or
            len(set(ring)) != len(ring) or not isinstance(boards, dict) or
            set(boards) != set(ring)):
        return False
    if any(_dict(boards[uid]).get("address") != uid for uid in ring):
        return False
    assignments = _rows(summary.get("assignments"))
    if len(assignments) != len(ring):
        return False
    for index, (uid, raw) in enumerate(zip(ring, assignments), 1):
        row = _dict(raw)
        if (row.get("address") != uid or row.get("no") != index or
                row.get("readback") != str(index) or row.get("passed") is not True):
            return False
    if summary.get("mode") == "REUSED_KNOWN_TOPOLOGY":
        # The controller validates frozen provenance and current build/UID/NO
        # before passing this artifact. Reuse is not a new pair scan.
        return summary.get("remeasured") is False
    expected = {uid: [ring[(index + 1) % len(ring)]] for index, uid in enumerate(ring)}
    cleanup = _rows(summary.get("cleanup"))
    return (summary.get("adjacency") == expected and len(cleanup) == len(ring) and
            {row.get("board") for row in cleanup if isinstance(row, dict)} == set(ring) and
            all(_dict(row).get("passed") is True for row in cleanup))


def _dpll_observation_valid(summary: dict) -> bool:
    if summary.get("_input_validated") is True:
        return True
    rows = _rows(summary.get("boards"))
    identities = [_dict(row).get("board") for row in rows]
    if (summary.get("tdma_preflight_passed") is not True or not rows or
            any(not isinstance(uid, str) or not uid for uid in identities) or
            len(set(identities)) != len(identities)):
        return False
    ring_rows = [row for row in rows if _dict(row).get("role") == "ring_node"]
    if len(ring_rows) < 2:
        return False
    for raw in rows:
        row = _dict(raw)
        samples = row.get("samples")
        if not _u32(samples) or samples <= len(_rows(row.get("errors"))):
            return False
        if row.get("role") == "ring_node":
            if row.get("ring_up_running") is not True or row.get("ring_down_running") is not True:
                return False
        elif row.get("role") != "observer":
            return False
    if str(summary.get("observation_mode", "")).startswith("EXTERNAL_"):
        observers = [row for row in rows if row.get("role") == "observer"]
        if len(observers) != 1 or _dict(observers[0].get("phase_observation")).get("complete_count", 0) <= 0:
            return False
    return True


def _transport_evidence(summary: dict) -> tuple[list[tuple[str, dict]], list[str]]:
    nodes = _dict(summary.get("nodes"))
    expected = (summary.get("board_ids_in_physical_node_order") or
                summary.get("board_ids_requested"))
    if (not isinstance(expected, list) or not expected or
            any(not isinstance(uid, str) for uid in expected) or
            len(set(expected)) != len(expected)):
        return [], ["expected board identities are missing or invalid"]
    missing = [uid for uid in expected if uid not in nodes]
    problems = [f"missing board: {uid}" for uid in missing]
    if set(nodes) != set(expected):
        problems.append("measured boards differ from requested board identities")
    left_running = summary.get("left_running")
    if left_running is False:
        stopped = _dict(summary.get("stopped"))
        if set(stopped) != set(expected):
            problems.append("STOP readback does not cover all expected boards")
        for uid in expected:
            row = _dict(stopped.get(uid))
            if (row.get("passed") != 1 or
                    any(row.get(key) != 0 for key in (
                        "ring_enabled", "ring_up_running", "ring_down_running", "ring_adapter_started")) or
                    not _u32(row.get("ring_config_seq")) or
                    row.get("ring_config_seq") != row.get("ring_applied_config_seq")):
                problems.append(f"STOP or applied-configuration readback is invalid: {uid}")
    elif left_running is True:
        handoff = _dict(summary.get("running_handoff"))
        if set(handoff) != set(expected):
            problems.append("running handoff does not cover all expected boards")
        for uid in expected:
            row = _dict(handoff.get(uid))
            runtime = _dict(row.get("runtime"))
            if (row.get("passed") is not True or
                    any(runtime.get(key) != 1 for key in (
                        "ring_enabled", "ring_up_running", "ring_down_running", "ring_adapter_started")) or
                    not _u32(runtime.get("ring_config_seq")) or
                    runtime.get("ring_config_seq") != runtime.get("ring_applied_config_seq")):
                problems.append(f"running handoff or applied-configuration readback is invalid: {uid}")
    else:
        problems.append("STOP/running handoff mode is not recorded")
    evidence = []
    for index, uid in enumerate(expected):
        if uid not in nodes:
            continue
        node = _dict(nodes[uid])
        runtime_before, runtime_after = _dict(node.get("runtime_before")), _dict(node.get("runtime_after"))
        delta = _dict(node.get("deltas"))
        runtime_delta = _dict(delta.get("runtime"))
        process_before = _dict(_dict(node.get("flight_before")).get("process"))
        process_after = _dict(_dict(node.get("flight_after")).get("process"))
        process_delta = _dict(delta.get("process"))
        values = {field: _delta(runtime_before, runtime_after, runtime_delta, field)
                  for field in ("ring_up_tx_sequence", "ring_down_rx_sequence",
                                "ring_adapter_tx_count", "ring_adapter_rx_count")}
        bad = _delta(runtime_before, runtime_after, runtime_delta, "ring_adapter_rx_bad_count")
        process_image = summary.get("stage") == "process-image" or bool(process_before or process_after or process_delta)
        if process_image:
            values["receive_accepted_count"] = _delta(
                process_before, process_after, process_delta, "receive_accepted_count")
            if "receive_accepted_sequence" in process_before or "receive_accepted_sequence" in process_after:
                values["receive_accepted_sequence"] = _delta(
                    process_before, process_after, process_delta, "receive_accepted_sequence")
        else:
            rx = values["ring_adapter_rx_count"]
            values["valid_received_frames"] = None if rx is None or bad is None else rx - bad
        errors = [f"{key} did not demonstrably advance" for key, value in values.items()
                  if value is None or value <= 0]
        # Identity checks use measured values where present, independent of
        # quality-error wording and of the aggregate passed flag.
        for field, wanted in (("ring_node_count", len(expected)), ("ring_local_node", index),
                              ("ring_reference_node", 0)):
            if field in runtime_after and runtime_after[field] != wanted:
                errors.append(f"{field} differs from expected identity")
        if "node_index" in node and node["node_index"] != index:
            errors.append("node_index differs from physical order")
        evidence.append((uid, {"deltas": values, "errors": errors}))
    return evidence, problems


def build_acceptance_report(*, profile: str, stages: dict[str, dict],
                            required_stages: list[str], strict_required_stages: list[str],
                            failures: list[dict], fatal_error: str | None = None) -> dict:
    """Classify supplied evidence without IO, mutation or new hardware gates.

    Findings retain their original evidence. Missing optional stages are
    SKIPPED; required missing/basic-invalid stages are ERROR. A selected,
    owner-validated calibration input can have WARN quality without being
    described as a successful new measurement. Strict stage requirements
    promote quality failure to ERROR regardless of profile.
    """
    required, strict = set(required_stages), set(strict_required_stages)
    names = list(dict.fromkeys((*STAGE_ORDER, *required_stages, *strict_required_stages, *stages)))
    findings: list[dict] = []
    stage_reports: list[dict] = []

    def add(stage: str, severity: str, code: str, message: str,
            evidence: Any = None, board_id: str | None = None) -> None:
        row = {"severity": severity, "code": code, "stage": stage,
               "message": message, "evidence": deepcopy(evidence)}
        if board_id is not None:
            row["board_id"] = board_id
        findings.append(row)

    for stage in names:
        raw = stages.get(stage)
        summary = _dict(raw)
        needed = stage in required or stage in strict
        quality_severity = "ERROR" if stage in strict else "WARN"
        basic_severity = "ERROR" if needed else "WARN"
        skipped = not summary or summary.get("skipped") is True
        if skipped:
            add(stage, basic_severity if needed else "INFO", "STAGE_MISSING" if needed else "STAGE_SKIPPED",
                "Required stage has no executed evidence" if needed else "Stage was not executed", raw)
        else:
            if stage == "P0":
                usable = _topology_valid(summary)
            elif stage == "TDMA":
                transport, problems = _transport_evidence(summary)
                usable = bool(transport) and not problems and all(not row["errors"] for _, row in transport)
                for problem in problems:
                    add(stage, basic_severity, "TDMA_IDENTITY_MISSING", problem, summary.get("board_ids_requested"))
                for uid, measurement in transport:
                    add(stage, basic_severity if measurement["errors"] else "INFO",
                        "TDMA_TRANSPORT_MISSING" if measurement["errors"] else "TDMA_TRANSPORT_PROGRESS",
                        "; ".join(measurement["errors"]) if measurement["errors"] else "New valid frames and TX/RX sequences advanced",
                        measurement, uid)
            elif stage == "T3":
                usable = summary.get("_input_validated") is True
            elif stage == "DPLL":
                usable = _dpll_observation_valid(summary)
            else:
                usable = summary.get("_input_validated") is True or summary.get("passed") is True
            if not usable:
                add(stage, basic_severity, "BASIC_INPUT_UNAVAILABLE",
                    "Required basic input, identity or transport was not demonstrated", summary)
            elif summary.get("passed") is not True:
                add(stage, quality_severity, "QUALITY_GATE_FAILED",
                    "Basic input is available; original measurement quality gate did not pass", summary)
            else:
                add(stage, "INFO", "STAGE_PASSED", "Stage requirements passed", {
                    "passed": True, "mode": summary.get("mode"), "remeasured": summary.get("remeasured")})

            # Candidate non-detection is expected exploration, provided a usable
            # selected result exists. Original per-candidate evidence is retained.
            for path, candidate in _candidate_failures(summary):
                add(stage, "INFO" if usable or stage == "P0" else basic_severity, "CANDIDATE_REJECTED",
                    "Candidate was not selected or did not meet its probe criteria",
                    {"path": path, "candidate": candidate})

            if stage == "TDMA":
                barrier = _dict(summary.get("startup_barrier"))
                if barrier.get("passed") is False:
                    add(stage, quality_severity, "TDMA_STARTUP_QUALITY", "Startup steady-state gate did not pass", barrier)
                for uid, raw_node in _dict(summary.get("nodes")).items():
                    for error in _rows(_dict(raw_node).get("errors")):
                        add(stage, quality_severity, "TDMA_NODE_QUALITY", str(error), error, uid)
                schedule = _dict(summary.get("dpll_schedule_gate"))
                for uid, raw_node in _dict(schedule.get("nodes")).items():
                    node = _dict(raw_node)
                    for error in _rows(node.get("errors")):
                        add(stage, quality_severity, "TDMA_SCHEDULE_QUALITY", str(error), error, uid)
                    for error in _rows(node.get("dpll_feedback")):
                        add(stage, "INFO", "DPLL_DIAGNOSTIC_FEEDBACK", str(error), error, uid)
            if stage == "DPLL":
                if summary.get("ring_sequence_consistent") is False:
                    add(stage, quality_severity, "DPLL_SEQUENCE_QUALITY", "Ring sequence consistency gate did not pass", {
                        "ring_sequence_consistent": False, "ring_sequence_skew": summary.get("ring_sequence_skew")})
                for raw_board in _rows(summary.get("boards")):
                    board = _dict(raw_board)
                    uid = board.get("board", board.get("address"))
                    if board.get("role") == "observer":
                        if board.get("phase_gate_passed") is not True:
                            add(stage, quality_severity, "DPLL_OBSERVER_PHASE_QUALITY",
                                "Observer phase/convergence gate did not pass", board, uid)
                    elif board.get("reference_node") is not True and board.get("dpll_locked") is not True:
                        add(stage, quality_severity, "DPLL_LOCK_QUALITY",
                            "Follower has not demonstrated lock", board, uid)
                if not _rows(summary.get("boards")):
                    add(stage, quality_severity, "DPLL_LOCK_EVIDENCE_MISSING",
                        "No per-board lock or phase evidence is available", summary)
        for path, risk in _irrecoverable_risks(summary):
            add(stage, "FATAL", "IRRECOVERABLE_RISK", "Explicitly identified irrecoverable risk",
                {"path": path, "risk": risk})
        stage_reports.append({"stage": stage, "required": stage in required,
                              "strict_required": stage in strict, "skipped": skipped,
                              "evidence": deepcopy(raw)})

    for failure in failures:
        row = _dict(failure)
        stage = str(row.get("stage") or "RUN")
        if row.get("irrecoverable_risk") is True:
            severity = "FATAL"
        elif row.get("blocking") is True:
            severity = "ERROR"
        elif stage in strict or profile != "QUICK_DIAGNOSTIC":
            severity = "ERROR"
        else:
            severity = "WARN"
        add(stage, severity, "EXECUTION_FEEDBACK", str(row.get("error") or row.get("message") or failure),
            failure, row.get("board_id"))
    if fatal_error is not None:
        add("RUN", "ERROR", "EXECUTION_ERROR", str(fatal_error), fatal_error)

    for item in stage_reports:
        local = [row for row in findings if row["stage"] == item["stage"]]
        severities = {row["severity"] for row in local}
        item["findings"] = local
        item["status"] = ("FAIL" if severities & {"ERROR", "FATAL"} else
                          "SKIPPED" if item.pop("skipped") else
                          "PASS_WITH_WARNINGS" if "WARN" in severities else "PASS")
        item.pop("skipped", None)
    counts = {severity: sum(row["severity"] == severity for row in findings) for severity in SEVERITIES}
    outcome = ("BLOCKED" if counts["ERROR"] or counts["FATAL"] else
               "PASS_WITH_WARNINGS" if counts["WARN"] else "PASS")
    return {"schema": SCHEMA, "policy": POLICY, "profile": profile,
            "required_stages": list(required_stages), "strict_required_stages": list(strict_required_stages),
            "outcome": outcome, "counts": counts, "stages": stage_reports, "findings": findings}
