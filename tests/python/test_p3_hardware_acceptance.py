import json
import sys
import types
from pathlib import Path

import pytest

from tools.hardware_acceptance.p3_hardware_acceptance import (
    AcceptanceError,
    LIMITED_RECEIPT_SCHEMA,
    QUICK_DIAGNOSTIC_RECEIPT_SCHEMA,
    RECEIPT_SCHEMA,
    TDMA_DIAGNOSTIC_RECEIPT_SCHEMA,
    TDMA_RECEIPT_SCHEMA,
    _run_step,
    _validate_evidence,
    acceptance_timing,
    build_diagnostic_feedback,
    calibration_coded_probe_phase_cycles,
    calibration_probe_phase_cycles,
    is_acceptance_source,
    load_bench_config,
    parse_schedule,
    p3_link_delays,
    reset_acceptance_boards,
    resolve_path_delay_baseline_divisor,
    selected_node_offsets,
    selected_data_offsets,
    stage_training_parameters,
    selected_sck_offsets,
    validate_ota,
    validate_online_builds,
    validate_p3,
    validate_runtime_schedules,
    validate_schedule_isolation,
    validate_sma_observer_topology,
    validate_tdma_diagnostic_summary,
    write_phase_summary,
    _record_timing_event,
    _start_timing_probe,
)


ROOT = Path(__file__).resolve().parents[2]


def test_bench_config_overlay_deep_merges_without_copying_topology(
        tmp_path: Path) -> None:
    base = tmp_path / "base.json"
    quick = tmp_path / "quick.json"
    base.write_text(json.dumps({
        "board_ids": ["n0", "n1"],
        "timing": {"timeout": 3.0, "gap": 0.1},
        "repeats": 8,
    }), encoding="utf-8")
    quick.write_text(json.dumps({
        "extends": "base.json",
        "timing": {"gap": 0.02},
        "repeats": 1,
    }), encoding="utf-8")

    assert load_bench_config(quick) == {
        "board_ids": ["n0", "n1"],
        "timing": {"timeout": 3.0, "gap": 0.02},
        "repeats": 1,
    }


def test_bench_config_overlay_rejects_cycle(tmp_path: Path) -> None:
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    first.write_text(json.dumps({"extends": "second.json"}), encoding="utf-8")
    second.write_text(json.dumps({"extends": "first.json"}), encoding="utf-8")

    with pytest.raises(AcceptanceError, match="cyclic"):
        load_bench_config(first)


def test_phase_summary_retains_failed_evidence_in_diagnostic_mode(
        tmp_path: Path) -> None:
    passed_dir = tmp_path / "passed"
    failed_dir = tmp_path / "failed"
    passed_dir.mkdir()
    failed_dir.mkdir()
    passed_path = passed_dir / "summary.json"
    failed_path = failed_dir / "summary.json"
    passed_path.write_text(
        json.dumps({"passed": True}), encoding="utf-8")
    failed_path.write_text(
        json.dumps({"passed": False, "error": "ARM rejected"}),
        encoding="utf-8")

    output = tmp_path / "phase-summary.json"
    result = write_phase_summary(
        output, "COARSE_CLK", [passed_path, failed_path],
        diagnostic_continue=True)

    assert result["passed"] is False
    assert result["flow_continued"] is True
    assert [row["passed"] for row in result["evidence"]] == [True, False]
    assert result["evidence"][1]["error"] == "ARM rejected"
    assert json.loads(output.read_text(encoding="utf-8")) == result


def test_phase_summary_rejects_failed_evidence_in_strict_mode(
        tmp_path: Path) -> None:
    failed_dir = tmp_path / "failed"
    failed_dir.mkdir()
    failed_path = failed_dir / "summary.json"
    failed_path.write_text(
        json.dumps({"passed": False, "error": "not calibrated"}),
        encoding="utf-8")

    with pytest.raises(AcceptanceError, match="not calibrated"):
        write_phase_summary(
            tmp_path / "phase-summary.json", "COARSE_CLK", [failed_path])


def test_debug_forced_continue_can_use_explicit_offset_fallback() -> None:
    assert selected_node_offsets(
        {"debug_forced_continue": True}, "offsets", 4, "TRN-00",
        diagnostic_fallback=[1, -1, 0, 1]) == [1, -1, 0, 1]


def test_acceptance_steps_use_phase_owned_serial_lifecycle(
        monkeypatch, tmp_path: Path) -> None:
    captured: dict[str, object] = {}

    def fake_run(*args, **kwargs):
        captured["env"] = kwargs["env"]
        return types.SimpleNamespace(returncode=0, stdout=b"ok\n")

    monkeypatch.setenv("HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS", "0")
    monkeypatch.setattr(
        "tools.hardware_acceptance.p3_hardware_acceptance.subprocess.run",
        fake_run)
    _run_step([sys.executable, "phase.py"], ROOT, tmp_path / "phase.log")
    assert captured["env"]["HAOFV_SERIAL_LIFECYCLE"] == "phase"
    assert "HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS" not in captured["env"]


def test_acceptance_step_can_retain_nonzero_diagnostic_result(
        monkeypatch, tmp_path: Path) -> None:
    monkeypatch.setattr(
        "tools.hardware_acceptance.p3_hardware_acceptance.subprocess.run",
        lambda *args, **kwargs: types.SimpleNamespace(
            returncode=7, stdout=b"diagnostic evidence\n"))
    assert _run_step(
        [sys.executable, "phase.py"], ROOT, tmp_path / "phase.log",
        allow_failure=True) == 7
    assert (tmp_path / "phase.log").read_bytes() == b"diagnostic evidence\n"


def test_diagnostic_feedback_retains_correction_inputs() -> None:
    tdma = {
        "offset_row": {"sck_offset_sample_counts_by_node": [1, 0, 0, 0]},
        "startup_barrier": {"passed": False, "samples": [{"passed": False}]},
        "soak_validation": {"worst_receive_quality": {
            "board_id": "n0", "observed_frame_error_ppm": 125.0}},
        "dpll_schedule_gate": {"passed": False},
        "nodes": {"n0": {
            "node_index": 0, "passed": False, "errors": ["bad_frame"],
            "runtime_after": {
                "ring_adapter_last_bad_header_diff_count": 3,
                "ring_adapter_last_bad_header_first_diff_offset": 7,
                "ring_adapter_last_bad_header_expected_byte": 0x55,
                "ring_adapter_last_bad_header_observed_byte": 0x51,
            },
            "physical_after": {"flight_sck_phase_delay_cycles": 10},
            "flight_after": {"process": {"receive_rejected_count": 2}},
            "crc_diagnostic_after": {"last_bad_packet_diff_count": 3},
        }},
    }
    dpll = {"ring_sequence_consistent": False, "ring_sequence_skew": 4,
            "boards": [{"role": "observer", "phase_initial_span_ns": 80,
                        "phase_final_span_ns": 12, "phase_converged": False}]}
    matrix = {"offset_matrix": {"active_row_id": 71}, "derivation": {
        "sck_replay_selection": {
            "selected_min_follower_margin_samples": 0}}}

    feedback = build_diagnostic_feedback(tdma, dpll, matrix)

    assert feedback["calibration_offsets"]["active_row_id"] == 71
    assert feedback["calibration_offsets"]["sck_replay_selection"][
        "selected_min_follower_margin_samples"] == 0
    assert feedback["tdma"]["nodes"]["n0"]["runtime"][
        "ring_adapter_last_bad_header_first_diff_offset"] == 7
    assert feedback["tdma"]["nodes"]["n0"]["physical_phase"][
        "flight_sck_phase_delay_cycles"] == 10
    assert feedback["dpll"]["observer_phase"]["phase_initial_span_ns"] == 80


def test_code_scope_includes_runtime_tools_and_build_inputs() -> None:
    for path in (
        "application/src/app.c",
        "components/tdma/inc/tdma.h",
        "tools/example/check.py",
        "tests/python/test_example.py",
        ".githooks/pre-commit",
        "CMakeLists.txt",
        "config/hardware_acceptance/p3_bench.json",
    ):
        assert is_acceptance_source(path)
    for path in (
        "docs/calibration/CALIBRATION_DOMAIN_TODO.md",
        "out/build/image.bin",
        "config/hardware_acceptance/p3_acceptance_receipt.json",
    ):
        assert not is_acceptance_source(path)


def test_precommit_enforces_p3_receipt_after_doc_gates() -> None:
    hook = (ROOT / ".githooks" / "pre-commit").read_text(encoding="utf-8")
    assert "p3_hardware_acceptance.py check-staged" in hook
    assert hook.index("doc gates OK") < hook.index("p3_hardware_acceptance.py")


def test_schedule_parser_and_isolation_gate() -> None:
    raw = "2,250000000,250000,1,91,0,10,0," + ",".join("0" for _ in range(11))
    parsed = parse_schedule(raw)
    assert parsed["enabled_mask"] == 91
    validate_schedule_isolation({"n0": parsed}, {"n0": parsed}, 4)
    changed = dict(parsed, enabled_mask=95)
    with pytest.raises(AcceptanceError, match="load mask"):
        validate_schedule_isolation({"n0": parsed}, {"n0": changed}, 4)
    quarantined = dict(parsed, quarantined_mask=4)
    with pytest.raises(AcceptanceError, match="quarantined"):
        validate_schedule_isolation({"n0": parsed}, {"n0": quarantined}, 4)

    validate_runtime_schedules({"n0": parsed, "n1": parsed}, 4)
    missed = dict(parsed, schedule_miss_count=1)
    validate_runtime_schedules({"n0": missed}, 4)


def test_ota_gate_requires_exact_five_board_set() -> None:
    ids = [f"node{index}" for index in range(5)]
    summary = {
        "passed": True, "dry_run": False, "failed_count": 0,
        "board_count": 5, "updated_count": 5, "expected_build": "build",
        "boards": [{"serial_number": value} for value in ids],
    }
    validate_ota(summary, ids, "build")
    summary["updated_count"] = 4
    with pytest.raises(AcceptanceError, match="OTA"):
        validate_ota(summary, ids, "build")


def test_online_build_gate_requires_exact_reused_firmware() -> None:
    ids = [f"node{index}" for index in range(5)]
    validate_online_builds({value: "build" for value in ids}, ids, "build")
    with pytest.raises(AcceptanceError, match="board set"):
        validate_online_builds({value: "build" for value in ids[:4]}, ids,
                               "build")
    mismatched = {value: "build" for value in ids}
    mismatched[ids[-1]] = "other-build"
    with pytest.raises(AcceptanceError, match="expected build"):
        validate_online_builds(mismatched, ids, "build")


def test_acceptance_initialization_resets_each_uid_once_and_reenumerates(
        monkeypatch: pytest.MonkeyPatch) -> None:
    ids = ["n0", "n1"]

    class Board:
        def __init__(self, board_id: str, port: str) -> None:
            self.address = board_id
            self.port = port
            self.build = "build"

    complete = {
        "n0": Board("n0", "COM5"),
        "n1": Board("n1", "COM4"),
    }
    discoveries = [complete, {"n0": complete["n0"]}, complete]
    commands: list[tuple[str, str]] = []
    closed: list[bool] = []
    fake = types.ModuleType("tdma_start_ring")
    fake.discover = lambda _args: discoveries.pop(0)
    fake.board_command = lambda board, command, _args: (
        commands.append((board.address, command)) or "OK")
    fake.close_persistent_connections = lambda: closed.append(True)
    monkeypatch.setitem(sys.modules, "tdma_start_ring", fake)
    monkeypatch.setattr(
        "tools.hardware_acceptance.p3_hardware_acceptance.time.sleep",
        lambda _seconds: None)

    result = reset_acceptance_boards(ids, "build", acceptance_timing({}))

    assert result["passed"] is True
    assert result["board_ids"] == ids
    assert result["ports_before"] == {"n0": "COM5", "n1": "COM4"}
    assert result["ports_after"] == {"n0": "COM5", "n1": "COM4"}
    assert commands == [
        ("n0", "SYSTem:BOOT:RESet"),
        ("n1", "SYSTem:BOOT:RESet"),
    ]
    assert closed == [True]


def _trial(delay: float = 80.0) -> dict:
    return {
        "passed": True,
        "delay_estimate_ns": delay,
        "initiator": {"dma_overrun_count": 0, "pio_stall_count": 0},
        "responder": {"dma_overrun_count": 0, "pio_stall_count": 0},
    }


def test_p3_gate_requires_complete_repeated_matrix() -> None:
    config = {
        "p3_board_ids_in_physical_order": ["n0", "n1", "n2", "n3"],
        "frequency_ladder_mhz": [10, 25, 30], "repeats": 3,
        "stable_frequency_mhz": 25,
        "minimum_link_delay_ns": 70, "maximum_link_delay_ns": 90,
    }
    summary = {
        "passed": True,
        "frequency_policy": {
            "stable_profiles_passed": True,
            "highest_stable_frequency_mhz": 25,
        },
        "trials": [_trial() for _ in range(72)],
    }
    result = validate_p3(summary, config)
    assert result["trial_count"] == 72
    summary["trials"].pop()
    with pytest.raises(AcceptanceError, match="matrix"):
        validate_p3(summary, config)


def test_receipt_uses_complete_calibration_to_dpll_schema() -> None:
    assert RECEIPT_SCHEMA == "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_V4"
    assert TDMA_RECEIPT_SCHEMA.endswith("TDMA_4NODE_V2")
    assert TDMA_DIAGNOSTIC_RECEIPT_SCHEMA.endswith(
        "TDMA_4NODE_DIAGNOSTIC_V1")
    assert LIMITED_RECEIPT_SCHEMA.endswith("10MHZ_LIMITED_V1")


def diagnostic_tdma_summary() -> tuple[list[str], dict[str, object]]:
    board_ids = ["n0", "n1", "n2", "n3"]
    nodes = {}
    handoff = {}
    saved = []
    downloaded = []
    for index, address in enumerate(board_ids):
        nodes[address] = {
            "passed": index != 0,
            "errors": ["receive_reject_grew"] if index == 0 else [],
            "runtime_before": {
                "ring_up_tx_sequence": 10,
                "ring_down_rx_sequence": 10,
            },
            "runtime_after": {
                "ring_up_tx_sequence": 20,
                "ring_down_rx_sequence": 20,
            },
        }
        handoff[address] = {
            "passed": True,
            "runtime": {
                "ring_up_tx_sequence": 30,
                "ring_down_rx_sequence": 30,
            },
        }
        saved.append({
            "node_id": address,
            "latch_status": [2, 1, 101, 10, index, 4, 512, 0],
            "load_mask_before": 91,
            "load_mask_during_capture": 91,
            "load_mask_restored": 91,
            "capture_debug": {
                "copy_fail_count": 0, "consumed_sequence": 1},
            "schedule_validation": {
                "passed": True, "newly_quarantined_mask": 0},
        })
        downloaded.append({"node_id": address})
    return board_ids, {
        "passed": False,
        "diagnostic_continue": True,
        "startup_barrier": {"passed": True},
        "soak_validation": {
            "passed": False, "errors": ["n0:receive_reject_grew"]},
        "left_running": True,
        "nodes": nodes,
        "running_handoff": handoff,
        "ring_capture": {
            "capture_completed": True,
            "saved": saved,
            "downloaded": downloaded,
        },
        "ring_analysis": {
            "passed": True,
            "nodes": [{"node": index} for index in range(4)],
        },
    }


def test_tdma_diagnostic_summary_accepts_observed_quality_failures() -> None:
    board_ids, summary = diagnostic_tdma_summary()
    validate_tdma_diagnostic_summary(summary, board_ids)


def test_tdma_diagnostic_summary_rejects_broken_base_flow() -> None:
    board_ids, summary = diagnostic_tdma_summary()
    summary["nodes"]["n0"]["runtime_after"]["ring_up_tx_sequence"] = 10
    summary["ring_capture"]["saved"][0]["capture_debug"][
        "copy_fail_count"] = 1
    with pytest.raises(AcceptanceError, match="not_advancing"):
        validate_tdma_diagnostic_summary(summary, board_ids)


def test_receipt_evidence_requires_every_acceptance_phase(tmp_path: Path) -> None:
    names = (
        "firmware_package", "ota_summary", "topology_summary",
        "coarse_calibration_summary", "coded_calibration_summary",
        "initialization_reset", "p3_summary", "trn00_summary",
        "trn01_summary", "trn02_summary",
        "trn03_matrix", "tdma_summary", "sma_observer_wiring",
        "dpll_summary",
    )
    record = {}
    for name in names:
        path = tmp_path / f"{name}.json"
        path.write_text("{}\n", encoding="utf-8")
        import hashlib
        record[name] = {
            "path": path.name,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
    _validate_evidence(tmp_path, record)
    del record["trn02_summary"]
    with pytest.raises(AcceptanceError, match="trn02_summary"):
        _validate_evidence(tmp_path, record)


def test_tdma_only_receipt_does_not_require_dpll_observer(tmp_path: Path) -> None:
    names = (
        "firmware_package", "ota_summary", "topology_summary",
        "coarse_calibration_summary", "coded_calibration_summary",
        "initialization_reset", "p3_summary", "trn00_summary",
        "trn01_summary", "trn02_summary",
        "trn03_matrix", "tdma_summary",
    )
    record = {
        "schema": TDMA_RECEIPT_SCHEMA,
        "passed": True,
        "acceptance_scope": "FOUR_NODE_TDMA",
        "ota_board_ids": ["n0", "n1", "n2", "n3"],
        "tdma_board_ids": ["n0", "n1", "n2", "n3"],
    }
    import hashlib
    for name in names:
        path = tmp_path / f"{name}.json"
        path.write_text("{}\n", encoding="utf-8")
        record[name] = {
            "path": path.name,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
    _validate_evidence(tmp_path, record, include_dpll=False)


def test_acceptance_timing_probe_writes_action_events(tmp_path: Path) -> None:
    path = tmp_path / "timing.json"
    _start_timing_probe(path)
    _record_timing_event({"action": "unit-test", "status": "PASS"})
    payload = json.loads(path.read_text(encoding="utf-8"))
    assert payload["schema"] == "HAOFV_HARDWARE_ACCEPTANCE_TIMING_PROBE_V1"
    assert payload["events"][-1]["action"] == "unit-test"


def test_p3_link_delay_reduction_uses_each_stable_clk_data_link() -> None:
    board_ids = ["n0", "n1", "n2", "n3"]
    config = {
        "p3_board_ids_in_physical_order": board_ids,
        "stable_frequency_mhz": 25,
        "repeats": 3,
    }
    trials = []
    for link, source in enumerate(board_ids):
        destination = board_ids[(link + 1) % len(board_ids)]
        for delay in (79.0 + link, 80.0 + link, 81.0 + link):
            trials.append({
                "source": source, "destination": destination,
                "signal_group": 0, "frequency_hz": 25_000_000,
                "delay_estimate_ns": delay, "passed": True,
            })
    delays = p3_link_delays({"trials": trials}, config)
    assert delays == [80, 82, 82, 84]
    assert all(value % 2 == 0 for value in delays)
    trials.pop()
    with pytest.raises(AcceptanceError, match="link3"):
        p3_link_delays({"trials": trials}, config)


def test_bench_and_orchestrator_cover_full_hardware_acceptance() -> None:
    bench = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench.json").read_text(encoding="utf-8"))
    for key in (
        "topology_anchor_board_id", "calibration_profile_levels",
        "calibration_probe_phase_cycles_by_level",
        "calibration_coded_probe_phase_cycles_by_level",
        "training_marker_codebook", "training_sck_codebook",
        "training_data_codebook",
        "training_marker_offsets_by_node", "training_sck_offsets_by_node",
        "training_data_offsets_by_node", "tdma_window_s",
        "dpll_observer_board_id", "sma_observer_routes",
        "hardware_acceptance_timing",
    ):
        assert key in bench
    assert resolve_path_delay_baseline_divisor(bench) == 2
    quick = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench_quick.json").read_text(encoding="utf-8"))
    loaded_quick = load_bench_config(
        ROOT / "config" / "hardware_acceptance" / "p3_bench_quick.json")
    assert resolve_path_delay_baseline_divisor(loaded_quick) == 2
    assert quick["training_sck_repeats"] == 8
    assert quick["training_sck_min_repeats"] == 3
    assert quick["training_sck_min_follower_candidates"] == 2
    assert loaded_quick["training_sck_offsets_by_node"] == [0, 0, 0, 0]
    assert loaded_quick["training_max_offset_span"] == 1
    assert acceptance_timing(loaded_quick)["p3_capture_timeout_s"] == 20.0
    source = (ROOT / "tools" / "hardware_acceptance" /
              "p3_hardware_acceptance.py").read_text(encoding="utf-8")
    for tool in (
        "calibration_ring_topology.py", "calibration_clk_train.py",
        "calibration_clk_coded.py", "calibration_link_p3.py",
        "calibration_marker_train.py", "calibration_sck_train.py",
        "calibration_data_train.py", "trn03_matrix.py",
        "trn03_closed_loop.py", "sma_cable_symmetric_rtt.py",
        "dpll_vdc_monitor.py",
    ):
        assert tool in source
    assert "--tdma-only" in source
    tdma_command = source.split("tdma_command = [", 1)[1].split(
        "print(\"Hardware acceptance: four-Node TDMA", 1)[0]
    assert (
        "add_serial_timing(tdma_command, timing, action=True, capture=True)"
        in tdma_command
    )
    assert "--diagnostic-continue" in source
    assert 'matrix_command.append("--diagnostic-continue")' in source
    assert '"selected_row_replay_safe"' in source
    assert '"TRN-03 SCK replay row selection"' in source
    assert '"flow_completed": True' in source
    assert '"strict_gates_passed": not diagnostic_failures' in source
    assert '"status": "PASS_WITH_DIAGNOSTICS"' in source
    assert '"phase": "TDMA running-loop handoff"' in source
    assert QUICK_DIAGNOSTIC_RECEIPT_SCHEMA in source
    assert '"FOUR_NODE_TDMA_QUICK_DIAGNOSTIC"' in source
    assert "_validate_quick_diagnostic_receipt" in source
    assert "run_diagnostic_gate" in source
    assert "HAOFV_DIAGNOSTIC_FORCED_CONTINUE_V1" in source
    assert "DEBUG_BOUNDED_FORCE_CONTINUE" in source
    assert '"TRN-00 residence matrix"' in source
    assert "FOUR_NODE_TDMA" in source
    assert '"--codebook", str(config["training_marker_codebook"])' in source
    assert '"--codebook", str(config["training_sck_codebook"])' in source
    assert '"--min-repeats", str(config.get(' in source
    assert '"--min-follower-candidates", str(config.get(' in source
    assert '"--codebook", str(config["training_data_codebook"])' in source
    assert "Hardware acceptance: TRN-00 accepted MARK offset row" in source
    assert "Hardware acceptance: TRN-01 SCK offset matrix" in source
    assert '"trn00_summary": evidence_entry' in source
    assert '"trn01_summary": evidence_entry' in source
    assert '"sck_summary": evidence_entry' not in source


def test_stage_parameter_handoff_rejects_old_baseline() -> None:
    summary = {
        "training_parameters": {
            "link_delay_ns_by_link": [82, 80],
            "link_base_delay_ns_by_link": [41, 40],
            "path_delay_baseline_divisor": 2,
            "sample_period_ns": 4,
        }
    }
    assert stage_training_parameters(summary, "TRN-01 SCK", 2)[
        "link_base_delay_ns_by_link"] == [41, 40]
    summary["training_parameters"]["path_delay_baseline_divisor"] = 3
    summary["training_parameters"]["link_base_delay_ns_by_link"] = [27, 27]
    assert stage_training_parameters(summary, "TRN-01 SCK", 2)[
        "link_base_delay_ns_by_link"] == [27, 27]


def test_selected_sck_offsets_are_loaded_from_active_row() -> None:
    summary = {
        "matrix": {"offset_matrix": {
            "active_row_id": 4,
            "rows": [
                {"row_id": 3, "sck_offset_sample_counts_by_node": [0, 0]},
                {"row_id": 4, "sck_offset_sample_counts_by_node": [1, -1]},
            ],
        }}
    }
    assert selected_sck_offsets(summary, 2) == [1, -1]


def test_selected_node_offsets_uses_measured_diagnostic_fallback() -> None:
    summary = {
        "recommended_row": None,
        "row_results": [{
            "offset_sample_counts_by_node": [1, -1, 0, 1],
            "nodes": [
                {"observation_count": 1},
                {"observation_count": 1},
                {"observation_count": 1},
                {"observation_count": 1},
            ],
            "failures": ["ring_repeat_gate"],
            "passed": False,
        }],
    }
    assert selected_node_offsets(
        summary,
        "offset_sample_counts_by_node",
        4,
        "TRN-00 MARK",
        diagnostic_fallback=[1, -1, 0, 1],
    ) == [1, -1, 0, 1]
    with pytest.raises(AcceptanceError):
        selected_node_offsets(
            summary,
            "offset_sample_counts_by_node",
            4,
            "TRN-00 MARK",
        )


def test_selected_data_offsets_are_loaded_from_active_row() -> None:
    summary = {
        "offset_matrix": {
            "active_row_id": 4,
            "rows": [
                {"row_id": 3, "data_offset_sample_counts_by_node": [16, 16]},
                {"row_id": 4, "data_offset_sample_counts_by_node": [17, 17]},
            ],
        }
    }
    assert selected_data_offsets(summary, 2) == [17, 17]


def test_sma_observer_topology_is_fixed_to_measured_bidirectional_routes() -> None:
    bench = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench.json").read_text(encoding="utf-8"))
    identities = {
        str(index): {"address": board_id}
        for index, board_id in enumerate(bench["ota_board_ids"], start=1)
    }
    summary = {
        "passed": True,
        "identities": identities,
        "wire_order": {
            "passed": True,
            "routes": bench["sma_observer_routes"],
        },
    }
    assert validate_sma_observer_topology(summary, bench) == \
        bench["sma_observer_routes"]

    summary["wire_order"]["routes"] = [
        dict(route) for route in bench["sma_observer_routes"]
    ]
    summary["wire_order"]["routes"][1]["validator_output_channel"] = 3
    with pytest.raises(AcceptanceError, match="differs from bench contract"):
        validate_sma_observer_topology(summary, bench)


def test_hardware_acceptance_timing_is_explicit_and_bounded() -> None:
    bench = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench.json").read_text(encoding="utf-8"))
    timing = acceptance_timing(bench)
    assert timing["serial_settle_s"] == 0.05
    assert timing["input_settle_s"] == timing["serial_settle_s"]
    assert timing["output_handoff_s"] == timing["phase_gap_s"]
    assert timing["serial_read_timeout_s"] < timing["serial_timeout_s"]
    assert timing["action_timeout_s"] <= timing["serial_timeout_s"]
    assert timing["board_reset_timeout_s"] == 15.0
    assert timing["board_reset_poll_interval_s"] == 0.5
    with pytest.raises(AcceptanceError, match="must be > 0"):
        acceptance_timing({"hardware_acceptance_timing": {
            "serial_timeout_s": 0,
        }})
    with pytest.raises(AcceptanceError, match="serial_read_timeout_s"):
        acceptance_timing({"hardware_acceptance_timing": {
            "serial_timeout_s": 0.1,
            "serial_read_timeout_s": 0.2,
        }})


def test_calibration_probe_phase_is_selected_per_profile() -> None:
    bench = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench.json").read_text(encoding="utf-8"))
    assert calibration_probe_phase_cycles(bench, 7) == 10
    assert calibration_probe_phase_cycles(bench, 8) == 2
    assert calibration_probe_phase_cycles(bench, 9) == 2
    assert calibration_coded_probe_phase_cycles(bench, 7) == [8, 9, 10]
    assert calibration_coded_probe_phase_cycles(bench, 8) == [1, 2, 3]
    with pytest.raises(AcceptanceError, match="profile level 10"):
        calibration_probe_phase_cycles(bench, 10)
    with pytest.raises(AcceptanceError, match="profile level 10"):
        calibration_coded_probe_phase_cycles(bench, 10)


def test_precalibration_tools_clear_stale_stage_and_gate_arm_result() -> None:
    for tool in ("calibration_clk_train.py", "calibration_clk_coded.py"):
        source = (ROOT / "tools" / "calibration_ring_validate" /
                  tool).read_text(encoding="utf-8")
        assert "CALibration:TRAINing:STAGe:CLEar" in source
        assert "SYSTem:TDMA:RING:ARM:STATus?" in source
        assert "ARM rejected with result=" in source
        assert "ARM_SUBMIT" in source
        assert "ARM_STARTED" in source
        assert "CALibration:TOPology:PROBe 1," in source
        assert "CALibration:TOPology:PROBe 0" in source
    coded = (ROOT / "tools" / "calibration_ring_validate" /
             "calibration_clk_coded.py").read_text(encoding="utf-8")
    assert "snapshot[\"state\"] == STATE_IDLE" in coded
    assert "deadline = time.monotonic() + args.coded_timeout" in coded
    assert "def wait_ring_stopped" in coded
    assert "STOP_APPLIED" in coded


def test_calibration_and_training_tools_own_realtime_load_guard() -> None:
    guard = (ROOT / "tools" / "calibration_ring_validate" /
             "calibration_load_guard.py").read_text(encoding="utf-8")
    assert "CALIBRATION_LOAD_MASK = 1 << 2" in guard
    assert "ring_stopped_offline_core1" in guard
    assert "offline calibration requires the" in guard
    assert "offline calibration disturbed TDMA schedule" in guard
    assert "SYSTem:TDMA:LOAD:MASK {" not in guard
    assert "schedule misses changed" not in guard
    for tool in (
            "calibration_clk_coded.py", "calibration_marker_train.py",
            "calibration_sck_train.py", "calibration_data_train.py"):
        source = (ROOT / "tools" / "calibration_ring_validate" /
                  tool).read_text(encoding="utf-8")
        assert "CalibrationLoadGuard" in source
        assert "realtime_calibration_load" in source
