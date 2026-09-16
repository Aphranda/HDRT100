"""Alarm grading preserves failed evidence and never invents basic TDMA progress."""
from copy import deepcopy
import hashlib
import json

import pytest

from tools.hardware_acceptance.p3_alarm_policy import build_acceptance_report
from tools.hardware_acceptance import p3_hardware_acceptance as p3


IDS = ["n1", "n2", "n3", "n4"]
QUICK = "QUICK_DIAGNOSTIC"
RUNTIME_COUNTERS = ("ring_up_tx_sequence", "ring_down_rx_sequence",
                    "ring_adapter_tx_count", "ring_adapter_rx_count")


def topology():
    return {
        "passed": True, "ring_order": list(IDS), "anchor_id": IDS[0],
        "boards": {uid: {"address": uid, "build": "current"} for uid in IDS},
        "assignments": [{"address": uid, "no": index + 1,
                         "readback": str(index + 1), "passed": True}
                        for index, uid in enumerate(IDS)],
        "cleanup": [{"board": uid, "passed": True} for uid in IDS],
        "adjacency": {uid: [IDS[(index + 1) % len(IDS)]]
                      for index, uid in enumerate(IDS)},
        "pair_results": [], "error": "",
    }


def tdma():
    nodes = {}
    for index, uid in enumerate(IDS):
        nodes[uid] = {
            "node_index": index, "passed": True, "errors": [],
            "runtime_before": {key: 100 for key in RUNTIME_COUNTERS},
            "runtime_after": {key: 140 for key in RUNTIME_COUNTERS},
            "flight_before": {"process": {"receive_accepted_count": 90}},
            "flight_after": {"process": {"receive_accepted_count": 130}},
            "deltas": {
                "runtime": {key: 40 for key in RUNTIME_COUNTERS},
                "process": {"receive_accepted_count": 40},
            },
        }
    return {"passed": True, "nodes": nodes,
            "board_ids_in_physical_node_order": list(IDS),
            "left_running": False, "leave_running_requested": False,
            "stopped": {uid: {"passed": 1, "ring_enabled": 0,
                              "ring_adapter_started": 0, "ring_up_running": 0,
                              "ring_down_running": 0, "ring_config_seq": 7,
                              "ring_applied_config_seq": 7} for uid in IDS},
            "startup_barrier": {"passed": True}, "errors": []}


def stage_set():
    values = {stage: {"passed": True, "_input_validated": True}
              for stage in p3.ALARM_STAGES}
    values["P0"] = topology()
    values["TDMA"] = tdma()
    values["DPLL"] = {"passed": True, "_input_validated": True, "boards": [
        {"address": uid, "dco_period_adjust_ppb": 0, "dpll_locked": True, "role": "ring_node"}
        for uid in IDS]}
    return values


def report_for(stages=None, *, required=("P0", "P3", "T3", "TDMA"),
               strict=(), profile=QUICK, failures=None, fatal_error=None):
    return build_acceptance_report(
        profile=profile, stages=stage_set() if stages is None else stages,
        required_stages=list(required), strict_required_stages=list(strict),
        failures=[] if failures is None else failures, fatal_error=fatal_error)


def findings(report, stage):
    return [row for row in report["findings"] if row["stage"] == stage]


def status(report, stage):
    return next(row["status"] for row in report["stages"] if row["stage"] == stage)


def assert_blocked(report, stage):
    assert report["outcome"] == "BLOCKED"
    assert any(row["severity"] in ("ERROR", "FATAL")
               for row in findings(report, stage)), report


def test_complete_four_board_flow_has_no_blocking_findings():
    report = report_for()
    assert report["schema"] == "HAOFV_P3_ACCEPTANCE_REPORT_V1"
    assert report["policy"] == "FIXED_STAGE_POLICY_V1"
    assert report["outcome"] != "BLOCKED"
    assert not any(row["severity"] in ("ERROR", "FATAL") for row in report["findings"])


def test_failed_p0_pair_candidates_are_info_after_complete_topology():
    stages = stage_set()
    candidate = {"driver": IDS[0], "receiver": IDS[2], "passed": False,
                 "error": "no direct cable", "raw_response": "0,0,0"}
    stages["P0"]["pair_results"] = [deepcopy(candidate)]
    before = deepcopy(stages)
    report = report_for(stages)
    assert report["outcome"] != "BLOCKED"
    rows = findings(report, "P0")
    assert rows and all(row["severity"] == "INFO" for row in rows)
    assert "no direct cable" in json.dumps(rows)
    assert stages == before


@pytest.mark.parametrize("mutation", ["missing_board", "short_ring", "duplicate_ring",
                                     "missing_assignment", "cleanup_failed"])
def test_p0_missing_or_uncommitted_topology_is_error(mutation):
    stages = stage_set()
    value = stages["P0"]
    if mutation == "missing_board": value["boards"].pop(IDS[-1])
    if mutation == "short_ring": value["ring_order"].pop()
    if mutation == "duplicate_ring": value["ring_order"][-1] = IDS[0]
    if mutation == "missing_assignment": value["assignments"].pop()
    if mutation == "cleanup_failed": value["cleanup"][-1]["passed"] = False
    assert_blocked(report_for(stages), "P0")


@pytest.mark.parametrize("stage", ["P1", "P2", "P3", "T0", "T1", "T2", "T3"])
def test_selected_usable_parameters_keep_other_candidate_failures_info(stage):
    stages = stage_set()
    stages[stage]["candidates"] = [
        {"passed": False, "path": "candidate-0/summary.json", "error": "outside eye"},
        {"passed": True, "path": "candidate-1/summary.json"},
    ]
    stages[stage]["selected_parameters"] = {"offsets": [0, 1, 0, 1]}
    report = report_for(stages, required=(stage,), strict=(stage,))
    rows = findings(report, stage)
    assert rows and all(row["severity"] == "INFO" for row in rows)
    assert "outside eye" in json.dumps(rows)
    assert report["outcome"] != "BLOCKED"


@pytest.mark.parametrize("stage", ["TDMA", "DPLL"])
def test_quick_timing_or_lock_failure_warns_but_strict_objective_blocks(stage):
    stages = stage_set()
    stages[stage]["passed"] = False
    stages[stage]["error"] = "WCET overrun" if stage == "TDMA" else "not locked"
    quick = report_for(stages, required=(stage,))
    assert quick["outcome"] != "BLOCKED"
    assert any(row["severity"] == "WARN" for row in findings(quick, stage))
    strict = report_for(stages, required=(stage,), strict=(stage,))
    assert_blocked(strict, stage)
    assert stages[stage]["passed"] is False


@pytest.mark.parametrize("nodes", [IDS, [IDS[2]]], ids=["all", "one"])
def test_tdma_no_valid_receive_growth_blocks_even_when_absolute_counts_nonzero(nodes):
    stages = stage_set()
    for uid in nodes:
        row = stages["TDMA"]["nodes"][uid]
        row["flight_after"]["process"]["receive_accepted_count"] = 90
        row["deltas"]["process"]["receive_accepted_count"] = 0
    result = report_for(stages)
    assert_blocked(result, "TDMA")
    blocked_ids = {row.get("board_id") for row in findings(result, "TDMA")
                   if row["severity"] in ("ERROR", "FATAL")}
    assert set(nodes) <= blocked_ids


def test_positive_derived_delta_cannot_override_unchanged_raw_receive_counter():
    stages = stage_set()
    row = stages["TDMA"]["nodes"][IDS[0]]
    row["flight_after"]["process"]["receive_accepted_count"] = 90
    assert row["deltas"]["process"]["receive_accepted_count"] > 0
    assert_blocked(report_for(stages), "TDMA")


@pytest.mark.parametrize("mutation", ["missing_before", "missing_after", "counter_reset",
                                     "boolean_counter", "missing_node", "missing_identity",
                                     "missing_stop", "stop_unapplied"])
def test_tdma_incomplete_or_reset_evidence_cannot_prove_progress(mutation):
    stages = stage_set()
    value = stages["TDMA"]
    row = value["nodes"][IDS[-1]]
    if mutation == "missing_before": row["flight_before"]["process"].clear()
    if mutation == "missing_after": row["flight_after"]["process"].clear()
    if mutation == "counter_reset": row["flight_after"]["process"]["receive_accepted_count"] = 2
    if mutation == "boolean_counter": row["flight_after"]["process"]["receive_accepted_count"] = True
    if mutation == "missing_node": value["nodes"].pop(IDS[-1])
    if mutation == "missing_identity": value.pop("board_ids_in_physical_node_order")
    if mutation == "missing_stop": value["stopped"].pop(IDS[-1])
    if mutation == "stop_unapplied": value["stopped"][IDS[-1]]["ring_applied_config_seq"] = 6
    assert_blocked(report_for(stages), "TDMA")


def test_t3_passing_measurement_alone_does_not_prove_selected_input_loaded():
    stages = stage_set()
    stages["T3"].pop("_input_validated")
    assert_blocked(report_for(stages), "T3")


@pytest.mark.parametrize("field", RUNTIME_COUNTERS)
def test_each_tdma_node_must_show_tx_and_rx_runtime_progress(field):
    stages = stage_set()
    row = stages["TDMA"]["nodes"][IDS[-1]]
    row["runtime_after"][field] = row["runtime_before"][field]
    row["deltas"]["runtime"][field] = 0
    assert_blocked(report_for(stages), "TDMA")


def test_tdma_raw_u32_counter_wrap_is_valid_forward_progress():
    stages = stage_set()
    for row in stages["TDMA"]["nodes"].values():
        row["runtime_before"] = {key: 0xFFFFFFFC for key in RUNTIME_COUNTERS}
        row["runtime_after"] = {key: 5 for key in RUNTIME_COUNTERS}
        row["flight_before"]["process"]["receive_accepted_count"] = 0xFFFFFFFC
        row["flight_after"]["process"]["receive_accepted_count"] = 5
        row["deltas"]["runtime"] = {key: 9 for key in RUNTIME_COUNTERS}
        row["deltas"]["process"]["receive_accepted_count"] = 9
    assert report_for(stages)["outcome"] != "BLOCKED"


def test_zero_dco_adjustment_is_not_an_error():
    report = report_for(required=("DPLL",), strict=("DPLL",))
    assert report["outcome"] != "BLOCKED"
    assert not any(row["severity"] in ("ERROR", "FATAL") for row in findings(report, "DPLL"))


def test_strict_dpll_requires_measured_lock_evidence_even_when_summary_claims_pass():
    stages = stage_set()
    for board in stages["DPLL"]["boards"]:
        board.pop("dpll_locked")
    assert_blocked(report_for(stages, required=("DPLL",), strict=("DPLL",)), "DPLL")


def test_irrecoverable_risk_in_rejected_candidate_is_not_hidden_by_info_grade():
    stages = stage_set()
    stages["P1"]["candidates"] = [{"passed": False, "details": {
        "irrecoverable_risk": True, "error": "confirmed illegal memory write"}}]
    result = report_for(stages)
    assert any(row["severity"] == "FATAL" for row in findings(result, "P1"))
    assert result["outcome"] == "BLOCKED"


def test_skipped_observer_remains_skipped_and_required_skip_blocks():
    stages = stage_set()
    stages["DPLL"] = {"skipped": True, "passed": True, "reason": "four-board TDMA scope"}
    result = report_for(stages)
    assert status(result, "DPLL") == "SKIPPED"
    assert result["outcome"] != "BLOCKED"
    assert_blocked(report_for(stages, required=("DPLL",), strict=("DPLL",)), "DPLL")


def test_failed_evidence_is_retained_as_a_snapshot_without_mutating_input():
    stages = stage_set()
    failure = {"phase": "DPLL", "returncode": 7, "error": "not locked",
               "raw": {"sample": [1, 2, 3]}, "action": "DEBUG_BOUNDED_FORCE_CONTINUE"}
    failures = [deepcopy(failure)]
    before = deepcopy(stages)
    result = report_for(stages, failures=failures)
    evidence_before = json.dumps(result, sort_keys=True)
    assert stages == before and failures == [failure]
    assert "not locked" in evidence_before
    failures[0]["raw"]["sample"][0] = 999
    assert json.dumps(result, sort_keys=True) == evidence_before


def test_fatal_severity_requires_structured_irrecoverable_risk():
    ordinary = report_for(fatal_error="DMA timeout; no irreversible fault confirmed")
    assert ordinary["outcome"] == "BLOCKED"
    assert not any(row["severity"] == "FATAL" for row in ordinary["findings"])
    fatal = report_for(failures=[{"phase": "TDMA", "error": "confirmed DMA out of bounds",
                                 "irrecoverable_risk": True}])
    assert fatal["outcome"] == "BLOCKED"
    assert any(row["severity"] == "FATAL" for row in fatal["findings"])


def test_scope_freezes_quick_baseline_and_explicit_strict_objectives():
    quick = p3.acceptance_scope(QUICK, True)
    assert quick["required_stages"] == ["P0", "P3", "T3", "TDMA"]
    assert quick["strict_required_stages"] == []
    strict = p3.acceptance_scope(QUICK, False, ["DPLL", "T1", "DPLL"])
    assert set(strict["required_stages"]) == {"P0", "P3", "T3", "TDMA", "DPLL", "T1"}
    assert strict["strict_required_stages"] == ["DPLL", "T1"]


@pytest.mark.parametrize("tdma_only", [False, True])
def test_full_scope_strictly_requires_every_executed_stage(tdma_only):
    scope = p3.acceptance_scope("FULL", tdma_only)
    expected = [stage for stage in p3.ALARM_STAGES if stage != "DPLL" or not tdma_only]
    assert scope["required_stages"] == expected
    assert scope["strict_required_stages"] == expected


@pytest.mark.parametrize("strict", [["DPLL"], ["unknown"]])
def test_scope_rejects_impossible_or_unknown_strict_goal(strict):
    with pytest.raises(p3.AcceptanceError):
        p3.acceptance_scope(QUICK, True, strict)


def write_evidence(root, name, value):
    path = root / f"{name}.json"
    path.write_text(json.dumps(value, sort_keys=True), encoding="utf-8")
    return {"path": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def graded_receipt(root, *, blocked=False):
    scope = p3.acceptance_scope(QUICK, True)
    stages = stage_set()
    if blocked:
        stages.pop("TDMA")
    inputs = {"profile": QUICK, "stages": stages,
              "required_stages": scope["required_stages"],
              "strict_required_stages": scope["strict_required_stages"],
              "failures": [], "fatal_error": None}
    report = build_acceptance_report(**inputs)
    record = {"acceptance_profile": QUICK, "dpll_observation": "SKIPPED_TDMA_ONLY",
              "acceptance_outcome": report["outcome"]}
    for field, value in (("fixed_scope", scope), ("alarm_inputs", inputs), ("alarm_report", report)):
        record[field] = write_evidence(root, field, value)
    return record, inputs, report, scope


def test_graded_receipt_recomputes_real_hashed_evidence(tmp_path):
    record, _, _, _ = graded_receipt(tmp_path)
    p3.validate_alarm_report(tmp_path, record)


@pytest.mark.parametrize("field", ["alarm_report", "alarm_inputs", "fixed_scope"])
def test_graded_receipt_rejects_changed_evidence_bytes(tmp_path, field):
    record, _, _, _ = graded_receipt(tmp_path)
    (tmp_path / record[field]["path"]).write_text("{}", encoding="utf-8")
    with pytest.raises(p3.AcceptanceError, match="digest changed"):
        p3.validate_alarm_report(tmp_path, record)


@pytest.mark.parametrize("mutation", ["outcome", "drop_stage", "change_inputs", "weaken_scope"])
def test_rehashing_tampered_report_or_inputs_does_not_make_it_valid(tmp_path, mutation):
    record, inputs, report, scope = graded_receipt(tmp_path)
    if mutation == "outcome":
        record["acceptance_outcome"] = "FORGED"
    elif mutation == "drop_stage":
        report["stages"] = [row for row in report["stages"] if row["stage"] != "TDMA"]
        record["alarm_report"] = write_evidence(tmp_path, "alarm_report", report)
    elif mutation == "change_inputs":
        inputs["stages"].pop("TDMA")
        record["alarm_inputs"] = write_evidence(tmp_path, "alarm_inputs", inputs)
    elif mutation == "weaken_scope":
        scope["required_stages"].remove("TDMA")
        record["fixed_scope"] = write_evidence(tmp_path, "fixed_scope", scope)
    with pytest.raises(p3.AcceptanceError):
        p3.validate_alarm_report(tmp_path, record)


def test_blocked_grading_cannot_issue_a_passing_receipt(tmp_path):
    record, _, report, _ = graded_receipt(tmp_path, blocked=True)
    assert report["outcome"] == "BLOCKED"
    with pytest.raises(p3.AcceptanceError, match="blocking"):
        p3.validate_alarm_report(tmp_path, record)


@pytest.mark.parametrize("field", ["alarm_report", "alarm_inputs", "fixed_scope"])
def test_partial_new_receipt_evidence_is_rejected_but_legacy_shape_is_readable(tmp_path, field):
    p3.validate_alarm_report(tmp_path, {"schema": p3.QUICK_DIAGNOSTIC_RECEIPT_SCHEMA})
    record, _, _, _ = graded_receipt(tmp_path)
    record.pop(field)
    with pytest.raises(p3.AcceptanceError, match="incomplete"):
        p3.validate_alarm_report(tmp_path, record)


def measured_graded_receipt(root, monkeypatch):
    raw = stage_set()
    raw.pop("DPLL")
    for summary in raw.values():
        summary.pop("_input_validated", None)
    identity = {"calibration_generation": 71, "topology_generation": 81,
                "topology_crc32": 91, "profile_crc32": 101, "schedule_crc32": 111}
    raw["T3"].update(identity, node_ids_in_loop_order=list(IDS))
    raw["TDMA"]["stage_results"] = [
        {"board_id": uid, "passed": True,
         "stage": {**identity, "complete": 1, "node_count": len(IDS),
                   "valid_link_bitmap": (1 << len(IDS)) - 1},
         "links": [{**identity, "link_index": index, "valid": 1}
                   for index in range(len(IDS))]}
        for uid in IDS]
    sources = {stage: write_evidence(root, f"raw-{stage}", summary)
               for stage, summary in raw.items()}
    scope = p3.acceptance_scope(QUICK, True)
    inputs = {"profile": QUICK, "stages": p3.load_alarm_stages(root, sources, IDS),
              "required_stages": scope["required_stages"],
              "strict_required_stages": scope["strict_required_stages"],
              "failures": [], "fatal_error": None}
    report = build_acceptance_report(**inputs)
    assert report["outcome"] != "BLOCKED"
    record = {"schema": p3.QUICK_GRADED_RECEIPT_SCHEMA, "acceptance_profile": QUICK,
              "dpll_observation": "SKIPPED_TDMA_ONLY", "tdma_board_ids": list(IDS),
              "build_id": "current",
              "diagnostic_failures": [], "acceptance_outcome": report["outcome"],
              "alarm_stage_sources": sources}
    field_map = {"P0": "topology_summary", "P1": "coarse_calibration_summary",
                 "P2": "coded_calibration_summary", "P3": "p3_summary",
                 "T0": "trn00_summary", "T1": "trn01_summary", "T2": "trn02_summary",
                 "T3": "trn03_matrix", "TDMA": "tdma_summary"}
    record.update({field_map[stage]: entry for stage, entry in sources.items()})
    # The existing P3 suite executes the actual C board recorder and rejects
    # malformed STOP records. Here isolate grading/source binding from that
    # separate binary decoder while still checking the proof is re-evaluated.
    proof = {"mode": "STOPPED_RECORDS", "passed": True, "build_id": "current", "records": []}
    monkeypatch.setattr(p3, "validate_tdma_stopped_handoff", lambda *args: deepcopy(proof))
    record["tdma_stopped_handoff"] = write_evidence(root, "stopped-handoff", proof)
    for field, value in (("fixed_scope", scope), ("alarm_inputs", inputs), ("alarm_report", report)):
        record[field] = write_evidence(root, field, value)
    return record, inputs, report, raw


def test_new_graded_schema_requires_raw_measurement_sources(tmp_path, monkeypatch):
    record, _, _, _ = measured_graded_receipt(tmp_path, monkeypatch)
    p3.validate_alarm_report(tmp_path, record)
    record.pop("alarm_stage_sources")
    with pytest.raises(p3.AcceptanceError, match="measured stage sources"):
        p3.validate_alarm_report(tmp_path, record)


def test_new_graded_schema_cannot_fall_back_to_legacy_without_alarm_files(tmp_path):
    with pytest.raises(p3.AcceptanceError, match="incomplete"):
        p3.validate_alarm_report(tmp_path, {"schema": p3.QUICK_GRADED_RECEIPT_SCHEMA})


@pytest.mark.parametrize("rehash", [False, True])
def test_new_receipt_rejects_raw_transport_changed_after_grading(tmp_path, monkeypatch, rehash):
    record, _, _, raw = measured_graded_receipt(tmp_path, monkeypatch)
    raw["TDMA"]["nodes"][IDS[0]]["flight_after"]["process"]["receive_accepted_count"] = 90
    changed = write_evidence(tmp_path, "raw-TDMA", raw["TDMA"])
    if rehash:
        record["alarm_stage_sources"]["TDMA"] = changed
        record["tdma_summary"] = changed
    with pytest.raises(p3.AcceptanceError, match="digest changed|differ from measured"):
        p3.validate_alarm_report(tmp_path, record)


@pytest.mark.parametrize("mutation", ["missing_board", "wrong_generation", "missing_link",
                                     "wrong_link_identity", "invalid_link"])
def test_owner_input_validation_is_recomputed_from_every_board_and_link(tmp_path, monkeypatch, mutation):
    record, inputs, _, raw = measured_graded_receipt(tmp_path, monkeypatch)
    rows = raw["TDMA"]["stage_results"]
    if mutation == "missing_board": rows.pop()
    if mutation == "wrong_generation": rows[-1]["stage"]["calibration_generation"] += 1
    if mutation == "missing_link": rows[-1]["links"].pop()
    if mutation == "wrong_link_identity": rows[-1]["links"][0]["schedule_crc32"] += 1
    if mutation == "invalid_link": rows[-1]["links"][0]["valid"] = 0
    record["alarm_stage_sources"]["TDMA"] = write_evidence(tmp_path, "raw-TDMA", raw["TDMA"])
    record["tdma_summary"] = record["alarm_stage_sources"]["TDMA"]
    # Even rewriting report+inputs together cannot assert owner acceptance
    # when the actual raw per-board readbacks no longer support that claim.
    inputs["stages"]["TDMA"] = deepcopy(raw["TDMA"])
    assert inputs["stages"]["T3"]["_input_validated"] is True
    forged = build_acceptance_report(**inputs)
    record["alarm_inputs"] = write_evidence(tmp_path, "alarm_inputs", inputs)
    record["alarm_report"] = write_evidence(tmp_path, "alarm_report", forged)
    record["acceptance_outcome"] = forged["outcome"]
    with pytest.raises(p3.AcceptanceError, match="differ from measured"):
        p3.validate_alarm_report(tmp_path, record)


def test_receipt_cannot_drop_original_failures_kept_in_alarm_inputs(tmp_path, monkeypatch):
    record, inputs, _, _ = measured_graded_receipt(tmp_path, monkeypatch)
    failure = {"phase": "internal DPLL", "error": "no lock yet", "returncode": 1}
    inputs["failures"] = [{**failure, "stage": "DPLL"}]
    report = build_acceptance_report(**inputs)
    record["alarm_inputs"] = write_evidence(tmp_path, "alarm_inputs", inputs)
    record["alarm_report"] = write_evidence(tmp_path, "alarm_report", report)
    record["acceptance_outcome"] = report["outcome"]
    with pytest.raises(p3.AcceptanceError, match="failures differ"):
        p3.validate_alarm_report(tmp_path, record)
    record["diagnostic_failures"] = [failure]
    p3.validate_alarm_report(tmp_path, record)


@pytest.mark.parametrize("strict", [False, True])
def test_write_report_maps_tdma_phase_before_applying_strict_scope(tmp_path, strict):
    failure = {"phase": "four-Node TDMA closed loop", "returncode": 2,
               "error": "timing did not meet target", "raw_counter": 121}
    context = {"out_dir": tmp_path, "stages": stage_set(), "failures": [failure],
               "scope": p3.acceptance_scope(QUICK, True, ["TDMA"] if strict else [])}
    original = deepcopy(context["failures"])
    report = p3.write_alarm_report(context)
    written = json.loads((tmp_path / "alarm-inputs.json").read_text(encoding="utf-8"))
    assert written["failures"] == [{**failure, "stage": "TDMA"}]
    assert context["failures"] == original
    assert json.loads((tmp_path / "alarms.json").read_text(encoding="utf-8")) == report
    assert any(row["severity"] == ("ERROR" if strict else "WARN")
               and row["evidence"].get("raw_counter") == 121
               for row in findings(report, "TDMA") if isinstance(row["evidence"], dict))
    assert (report["outcome"] == "BLOCKED") is strict


def test_blocking_stop_handoff_failure_is_error_in_quick_mode():
    report = report_for(failures=[{"stage": "TDMA", "blocking": True,
                                   "phase": "TDMA stopped handoff", "error": "STOP unconfirmed"}])
    assert_blocked(report, "TDMA")


def test_check_staged_rechecks_alarm_evidence_after_other_receipt_checks(monkeypatch, tmp_path):
    record, _, report, _ = measured_graded_receipt(tmp_path, monkeypatch)
    record.update(passed=True, source_tree_sha256="current", source_file_count=1)
    report["stages"] = [row for row in report["stages"] if row["stage"] != "TDMA"]
    record["alarm_report"] = write_evidence(tmp_path, "alarm_report", report)
    monkeypatch.setattr(p3, "changed_staged_sources", lambda root: ["tools/example.py"])
    monkeypatch.setattr(p3, "unstaged_sources", lambda root: [])
    monkeypatch.setattr(p3, "read_index_json", lambda root, path: record)
    monkeypatch.setattr(p3, "staged_source_fingerprint", lambda root: ("current", 1))
    monkeypatch.setattr(p3, "_validate_quick_diagnostic_receipt", lambda root, receipt: None)
    with pytest.raises(p3.AcceptanceError, match="grading differs"):
        p3.check_staged(tmp_path, tmp_path / "receipt.json")


def p3_frequency_fixture():
    config = {"p3_board_ids_in_physical_order": list(IDS), "frequency_ladder_mhz": [10, 25, 30],
              "stable_frequency_mhz": 25, "repeats": 1,
              "minimum_link_delay_ns": 70, "maximum_link_delay_ns": 90}
    trials = []
    for frequency in config["frequency_ladder_mhz"]:
        for link in range(len(IDS)):
            for group in (1, 2):
                trials.append({"frequency_hz": frequency * 1_000_000, "link_index": link,
                               "signal_group": group, "passed": frequency != 30,
                               "delay_estimate_ns": 80 if frequency != 30 else 140,
                               "initiator": {"dma_overrun_count": 0, "pio_stall_count": 0},
                               "responder": {"dma_overrun_count": 0, "pio_stall_count": 0}})
    summary = {"passed": True, "trials": trials,
               "frequency_policy": {"stable_profiles_passed": True, "highest_stable_frequency_mhz": 25},
               "ladder": [{"frequency_mhz": frequency, "required_for_stable": frequency != 30,
                           "operational_class": "STABLE" if frequency != 30 else "LIMITED_RX"}
                          for frequency in config["frequency_ladder_mhz"]]}
    return config, summary


def test_p3_nonrequired_30mhz_probe_failure_preserves_10_and_25mhz_acceptance():
    config, summary = p3_frequency_fixture()
    original = deepcopy(summary)
    result = p3.validate_p3(summary, config)
    assert result["trial_count"] == 24
    assert result["highest_stable_frequency_mhz"] == 25
    assert result["delay_min_ns"] == result["delay_max_ns"] == 80
    assert summary == original
    assert sum(trial["passed"] is False for trial in summary["trials"]) == 8


@pytest.mark.parametrize("mutation", ["failed_target", "target_forged_limited", "failed_baseline",
                                     "target_delay", "target_pio", "target_dma", "missing_trial"])
def test_p3_required_operating_points_cannot_be_downgraded_to_exploration(mutation):
    config, summary = p3_frequency_fixture()
    target = next(row for row in summary["trials"] if row["frequency_hz"] == 25_000_000)
    if mutation == "failed_target": target["passed"] = False
    if mutation == "target_forged_limited":
        target["passed"] = False
        summary["ladder"][1].update(required_for_stable=False, operational_class="LIMITED_RX")
    if mutation == "failed_baseline": summary["trials"][0]["passed"] = False
    if mutation == "target_delay": target["delay_estimate_ns"] = 140
    if mutation == "target_pio": target["responder"]["pio_stall_count"] = 1
    if mutation == "target_dma": target["initiator"]["dma_overrun_count"] = 1
    if mutation == "missing_trial": summary["trials"].pop()
    with pytest.raises(p3.AcceptanceError):
        p3.validate_p3(summary, config)


def test_p3_extra_exploratory_trials_cannot_replace_missing_target_measurements():
    config, summary = p3_frequency_fixture()
    for row in summary["trials"]:
        if row["frequency_hz"] == 25_000_000:
            row.update(frequency_hz=30_000_000, passed=False)
    assert len(summary["trials"]) == 24
    with pytest.raises(p3.AcceptanceError):
        p3.validate_p3(summary, config)
