from __future__ import annotations

import argparse
import json
from pathlib import Path

from tools.dpll_residual_analyze.dpll_residual_analyze import (
    ResidualPoint,
    analyze_series,
    load_monitor_samples,
    render_svg,
    run,
)


def point(index: int, residual: int, *, state: int = 5,
          gate: int = 0, rejected: int = 0) -> ResidualPoint:
    return ResidualPoint(
        board="NO2", elapsed_s=float(index), phase_residual_ns=residual,
        frequency_error_ppb=index * 10, dco_phase_offset_ns=index,
        dco_period_adjust_ppb=-index * 5, dpll_state=state,
        gate_reject_code=gate, accepted_count=index,
        rejected_count=rejected, dpll_update_seq=index * 4,
    )


def test_analysis_distinguishes_damped_oscillation_and_decimation() -> None:
    points = [point(index, value) for index, value in enumerate(
        [1000, -900, 750, -600, 400, -250, 120, -50])]
    analysis = analyze_series(
        points, rolling_window=3, lock_threshold_ns=10000,
        mad_multiplier=6.0)
    assert analysis["trend_classification"] == "damped_oscillation_candidate"
    assert analysis["zero_crossings"] == 7
    assert analysis["rms_ratio"] < 0.8
    assert analysis["full_rate_trace"] is False
    assert analysis["analysis_confidence"] == "low_decimated"


def test_analysis_marks_gate_state_and_reject_counter_anomalies() -> None:
    points = [
        point(0, 100),
        point(1, 120, state=7, gate=11, rejected=2),
        point(2, 110, rejected=2),
    ]
    analyze_series(points, rolling_window=2, lock_threshold_ns=10000,
                   mad_multiplier=6.0)
    assert "state_7" in points[1].anomaly_reasons
    assert "gate_11" in points[1].anomaly_reasons
    assert "reject_counter_advanced" in points[1].anomaly_reasons


def test_analysis_separates_fixed_bias_from_real_jitter() -> None:
    points = [point(index, residual) for index, residual in enumerate(
        [10010, 9990, 10005, 9995, 10000, 10008])]
    analysis = analyze_series(
        points, rolling_window=3, lock_threshold_ns=10000,
        mad_multiplier=6.0)
    assert analysis["bias_ns"] == 10002.5
    assert analysis["residual_sample_count"] == 6
    assert analysis["residual_coverage"] == 1.0
    assert analysis["jitter_peak_to_peak_ns"] == 20
    assert analysis["jitter_rms_ns"] < 10
    assert max(abs(item.centered_residual_ns) for item in points) <= 12.5


def test_follower_command_samples_are_not_residual_evidence() -> None:
    samples = [point(index, 0) for index in range(3)]
    for item in samples:
        item.capture_kind = "follower_applied_command"
        item.residual_valid = False
        item.residual_invalid_reason = "follower_command_apply"
    analysis = analyze_series(
        samples, rolling_window=2, lock_threshold_ns=10000,
        mad_multiplier=6.0)
    assert analysis["residual_sample_count"] == 0
    assert analysis["residual_metrics_applicable"] is False
    assert analysis["analysis_confidence"] == "no_residual_evidence"
    assert analysis["jitter_rms_ns"] is None
    assert all(item.anomaly_reasons == () for item in samples)


def test_follower_state_transition_is_not_residual_evidence(tmp_path: Path) -> None:
    source = tmp_path / "follower-state.json"
    source.write_text(json.dumps({"NO3": [{
        "elapsed_s": 0.0,
        "capture_kind": "follower_state_transition",
        "dpll_vector": {"last_phase_error_ns": 12,
                        "state": 1, "dpll_update_seq": 4},
        "follower_command": {"source_slot_id": 1,
                              "control_generation": 3,
                              "command_seq": 7,
                              "effective_vdc_time_ns": 100},
    }]}), encoding="utf-8")
    points = load_monitor_samples([source])["NO3"]
    analysis = analyze_series(points, rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    assert analysis["residual_sample_count"] == 0
    assert analysis["analysis_kind"] == "follower_validated_command_apply"
    assert points[0].residual_invalid_reason == "follower_state_transition"


def test_partial_delay_metadata_keeps_jitter_raw_and_unmixed() -> None:
    corrected = point(0, 100)
    corrected.observation_source_slot_id = 2
    corrected.observation_reference_slot_id = 1
    corrected.observation_delay_ns = 10
    corrected.observation_delay_generation = 1
    corrected.observation_bias_generation = 1
    corrected.observation_generation_metadata_present = True
    corrected.observation_metadata_valid = True
    corrected.transport_corrected_phase_residual_ns = 90
    incomplete = point(1, 300)
    analysis = analyze_series([corrected, incomplete], rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    assert analysis["delay_correction_available"] is False
    assert corrected.centered_residual_ns == -100
    assert incomplete.centered_residual_ns == 100


def test_analysis_centers_bias_per_observed_directed_path(tmp_path: Path) -> None:
    source = tmp_path / "paths.json"
    rows = []
    for index, (path_source, path_reference, residual) in enumerate(
            [(2, 1, 1000), (2, 1, 1010), (3, 1, 900), (3, 1, 910)]):
        rows.append({
            "elapsed_s": float(index), "error": "",
            "capture_kind": "follower_local_evidence",
            "dpll_vector": {
                "last_phase_error_ns": residual,
                "last_frequency_error_ppb": 0,
                "dco_phase_offset_ns": 0,
                "dco_period_adjust_ppb": 0,
                "state": 1, "gate_reject_code": 0,
                "dpll_update_seq": index + 1,
            },
            "follower_observation": {
                "source_slot_id": path_source,
                "reference_slot_id": path_reference,
                "follow_master_slot_id": 0,
                "sample_seq": index + 1,
            },
            "observation": {
                "source_slot_id": path_source,
                "reference_slot_id": path_reference,
                "follow_master_slot_id": 0,
                "delay_ns": 400 if path_source == 2 else 700,
                "jitter_ns": 2,
                "delay_generation": 1,
                "bias_generation": 1,
                "raw_phase_error_ns": residual,
            },
        })
    source.write_text(json.dumps({"NO2": rows}), encoding="utf-8")
    points = load_monitor_samples([source])["NO2"]
    analysis = analyze_series(points, rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    assert analysis["bias_by_path_ns"] == {"2->1": 1005.0, "3->1": 905.0}
    assert analysis["delay_by_path_ns"] == {"2->1": 400.0, "3->1": 700.0}
    assert analysis["transport_corrected_bias_by_path_ns"] == {
        "2->1": 605.0, "3->1": 205.0}
    assert analysis["transport_delay_removed_for_jitter"] is True
    assert all(abs(point.centered_residual_ns) == 5 for point in points)


def test_missing_delay_metadata_does_not_claim_transport_correction(
        tmp_path: Path) -> None:
    source = tmp_path / "missing-delay.json"
    source.write_text(json.dumps({"NO2": [{
        "elapsed_s": 0.0,
        "capture_kind": "follower_local_evidence",
        "dpll_vector": {"last_phase_error_ns": 100,
                        "state": 5, "dpll_update_seq": 1},
        "follower_observation": {"source_slot_id": 2,
                                  "reference_slot_id": 1},
        "observation": {"source_slot_id": 2,
                         "reference_slot_id": 1,
                         "jitter_ns": 1},
    }]}), encoding="utf-8")
    points = load_monitor_samples([source])["NO2"]
    analysis = analyze_series(points, rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    assert points[0].transport_corrected_phase_residual_ns is None
    assert analysis["delay_correction_available"] is False
    assert analysis["transport_delay_removed_for_jitter"] is False
    assert analysis["delay_correction_rejection_reasons"] == [
        "missing:delay_ns,follow_master_slot_id"]


def test_wrong_observation_direction_is_rejected(tmp_path: Path) -> None:
    source = tmp_path / "wrong-direction.json"
    source.write_text(json.dumps({"NO2": [{
        "elapsed_s": 0.0,
        "capture_kind": "follower_local_evidence",
        "dpll_vector": {"last_phase_error_ns": 100,
                        "state": 5, "dpll_update_seq": 1},
        "follower_observation": {"source_slot_id": 2,
                                  "reference_slot_id": 1},
        "observation": {"source_slot_id": 3,
                         "reference_slot_id": 1,
                         "follow_master_slot_id": 1,
                         "delay_ns": 40, "jitter_ns": 1,
                         "delay_generation": 1, "bias_generation": 1},
    }]}), encoding="utf-8")
    points = load_monitor_samples([source])["NO2"]
    analysis = analyze_series(points, rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    assert points[0].observation_metadata_reason == \
        "follower_path_identity_mismatch"
    assert analysis["delay_correction_available"] is False
    assert analysis["transport_corrected_sample_count"] == 0


def test_fixed_delay_change_does_not_change_corrected_jitter() -> None:
    def make_points(delay: int) -> list[ResidualPoint]:
        result = []
        for index, residual in enumerate((1000, 1010, 995, 1005)):
            item = point(index, residual)
            item.observation_source_slot_id = 2
            item.observation_reference_slot_id = 1
            item.observation_delay_ns = delay
            item.observation_jitter_ns = 1
            item.observation_delay_generation = 1
            item.observation_bias_generation = 1
            item.observation_generation_metadata_present = True
            item.observation_metadata_valid = True
            item.transport_corrected_phase_residual_ns = residual - delay
            result.append(item)
        return result

    first = analyze_series(make_points(100), rolling_window=2,
                           lock_threshold_ns=10000, mad_multiplier=6.0)
    second = analyze_series(make_points(900), rolling_window=2,
                            lock_threshold_ns=10000, mad_multiplier=6.0)
    assert first["delay_correction_available"] is True
    assert second["delay_correction_available"] is True
    assert first["jitter_rms_ns"] == second["jitter_rms_ns"]
    assert first["jitter_peak_to_peak_ns"] == \
        second["jitter_peak_to_peak_ns"]


def test_missing_generation_metadata_is_raw_only(tmp_path: Path) -> None:
    source = tmp_path / "missing-generation.json"
    source.write_text(json.dumps({"NO2": [{
        "elapsed_s": 0.0,
        "capture_kind": "master_local_evidence",
        "dpll_vector": {"last_phase_error_ns": 100,
                        "state": 5, "dpll_update_seq": 1},
        "observation": {"source_slot_id": 1, "reference_slot_id": 2,
                         "delay_ns": 40, "jitter_ns": 1},
    }]}), encoding="utf-8")
    points = load_monitor_samples([source])["NO2"]
    analysis = analyze_series(points, rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    assert points[0].observation_metadata_valid is False
    assert points[0].observation_metadata_reason == "generation_metadata_missing"
    assert analysis["delay_correction_available"] is False
    assert analysis["transport_corrected_sample_count"] == 0
    assert analysis["transport_delay_removed_for_jitter"] is False


def test_unaligned_local_epoch_is_raw_only_and_cannot_report_jitter(
        tmp_path: Path) -> None:
    source = tmp_path / "raw-local-epoch.json"
    rows = []
    for index, residual in enumerate((-31_000, -29_000, -33_000)):
        rows.append({
            "elapsed_s": float(index),
            "capture_kind": "follower_local_evidence",
            "dpll_vector": {"last_phase_error_ns": residual,
                            "state": 1, "dpll_update_seq": index + 1},
            "follower_observation": {"source_slot_id": 1,
                                     "reference_slot_id": 0,
                                     "follow_master_slot_id": 0},
            "observation": {
                "source_slot_id": 1, "reference_slot_id": 0,
                "follow_master_slot_id": 0, "delay_ns": 242,
                "jitter_ns": 1, "delay_generation": 7,
                "bias_generation": 9,
                "correlation_flags": 0x00000003,
            },
        })
    source.write_text(json.dumps({"NO2": rows}), encoding="utf-8")
    points = load_monitor_samples([source])["NO2"]
    analysis = analyze_series(points, rolling_window=2,
                              lock_threshold_ns=10_000, mad_multiplier=6.0)
    assert all(not point.observation_metadata_valid for point in points)
    assert {point.observation_metadata_reason for point in points} == {
        "local_epoch_unaligned"}
    assert analysis["jitter_metrics_trusted"] is False
    assert analysis["jitter_rms_ns"] is None
    assert analysis["raw_jitter_rms_ns"] is not None
    assert "local_epoch_unaligned" in analysis["lock_decision_reasons"]


def test_partial_generation_metadata_is_raw_only(tmp_path: Path) -> None:
    source = tmp_path / "partial-generation.json"
    source.write_text(json.dumps({"NO2": [{
        "elapsed_s": 0.0,
        "capture_kind": "master_local_evidence",
        "dpll_vector": {"last_phase_error_ns": 100,
                        "state": 5, "dpll_update_seq": 1},
        "observation": {"source_slot_id": 1, "reference_slot_id": 2,
                         "delay_ns": 40, "jitter_ns": 1,
                         "delay_generation": 3},
    }]}), encoding="utf-8")
    points = load_monitor_samples([source])["NO2"]
    analyze_series(points, rolling_window=2,
                   lock_threshold_ns=10000, mad_multiplier=6.0)
    assert points[0].observation_metadata_valid is False
    assert points[0].observation_metadata_reason == "generation_metadata_incomplete"


def test_generation_change_disables_correction_for_whole_series() -> None:
    samples = []
    for index, generation in enumerate((1, 1, 2)):
        item = point(index, 100 + index)
        item.observation_source_slot_id = 1
        item.observation_reference_slot_id = 2
        item.observation_delay_ns = 40
        item.observation_jitter_ns = 1
        item.observation_delay_generation = generation
        item.observation_bias_generation = 1
        item.observation_generation_metadata_present = True
        item.observation_metadata_valid = True
        item.transport_corrected_phase_residual_ns = item.phase_residual_ns - 40
        samples.append(item)
    analysis = analyze_series(samples, rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    assert analysis["generation_metadata_consistent"] is False
    assert analysis["delay_correction_available"] is False
    assert analysis["transport_corrected_sample_count"] == 0
    assert "generation_metadata_inconsistent" in analysis["lock_decision_reasons"]


def test_incomplete_follower_observation_metadata_cannot_prove_lock() -> None:
    samples = [point(index, 2, state=5) for index in range(24)]
    for item in samples:
        item.capture_kind = "follower_local_evidence"
        item.observation_metadata_valid = False
    analysis = analyze_series(
        samples, rolling_window=4, lock_threshold_ns=10000,
        mad_multiplier=6.0)
    assert analysis["lock_decision"] == "not_local_lock_evidence"
    assert "observation_metadata_missing" in analysis["lock_decision_reasons"]


def test_monitor_loader_ignores_failed_samples(tmp_path: Path) -> None:
    source = tmp_path / "samples.json"
    source.write_text(json.dumps({
        "NO2": [
            {"elapsed_s": 0.0, "error": "timeout"},
            {
                "elapsed_s": 1.0, "error": "",
                "dpll_vector": {
                    "last_phase_error_ns": -25,
                    "last_frequency_error_ppb": 30,
                    "dco_phase_offset_ns": 40,
                    "dco_period_adjust_ppb": 50,
                    "state": 5, "gate_reject_code": 0,
                    "dpll_update_seq": 8,
                },
                "readiness": {"accepted_count": 2, "rejected_count": 1},
            },
        ],
        "NO3": [],
    }), encoding="utf-8")
    loaded = load_monitor_samples([source], {"NO2"})
    assert list(loaded) == ["NO2"]
    assert loaded["NO2"][0].phase_residual_ns == -25


def test_svg_and_run_write_transfer_analysis_artifacts(tmp_path: Path) -> None:
    samples = tmp_path / "samples.json"
    rows = []
    for index, residual in enumerate([500, -400, 300, -200, 100, -50]):
        rows.append({
            "elapsed_s": float(index), "error": "",
            "dpll_vector": {
                "last_phase_error_ns": residual,
                "last_frequency_error_ppb": index,
                "dco_phase_offset_ns": index * 2,
                "dco_period_adjust_ppb": -index,
                "state": 5, "gate_reject_code": 0,
                "dpll_update_seq": index,
            },
            "readiness": {"accepted_count": index, "rejected_count": 0},
        })
    samples.write_text(json.dumps({"NO2": rows}), encoding="utf-8")
    out_dir = tmp_path / "analysis"
    result = run(argparse.Namespace(
        input=[samples], board=None, rolling_window=3,
        lock_threshold_ns=10000, mad_multiplier=6.0, out_dir=out_dir))
    svg_path = out_dir / "no2_dpll_residual.svg"
    svg = svg_path.read_text(encoding="utf-8")
    assert result["schema"] == "HAOFV_DPLL_RESIDUAL_ANALYSIS_V1"
    assert svg.startswith("<svg ")
    assert "raw residual" in svg
    assert "rolling mean" in svg
    assert (out_dir / "dpll_residual_samples.csv").is_file()
    assert (out_dir / "dpll_residual_analysis.json").is_file()


def test_svg_marks_anomaly_reason() -> None:
    points = [point(0, 100), point(1, 120, state=7, gate=11, rejected=1)]
    analysis = analyze_series(points, rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    svg = render_svg("NO2", points, analysis, lock_threshold_ns=10000)
    assert "gate_11" in svg
    assert "state_7" in svg


def test_svg_renders_bias_corrected_linear_convergence_trend() -> None:
    points = [point(0, 100), point(1, 110), point(2, 120)]
    analysis = analyze_series(points, rolling_window=2,
                              lock_threshold_ns=10000, mad_multiplier=6.0)
    svg = render_svg("NO2", points, analysis, lock_threshold_ns=10000)

    assert analysis["bias_corrected_linear_slope_ns_per_s"] == 10.0
    assert analysis["bias_corrected_linear_intercept_ns"] == -10.0
    assert 'class="trendline"' in svg
    assert "linear convergence trend (bias-corrected residual; observation only)" in svg
    assert "slope=10.000 ns/s" in svg


def test_follower_svg_reports_applied_commands_not_local_pi_lock(
        tmp_path: Path) -> None:
    samples = [
        {
            "elapsed_s": float(index), "error": "",
            "capture_kind": "follower_applied_command",
            "dpll_vector": {
                "last_phase_error_ns": 0,
                "last_frequency_error_ppb": 0,
                "dco_phase_offset_ns": 25 + index,
                "dco_period_adjust_ppb": -4 - index,
                "state": 1, "gate_reject_code": 0,
                "dpll_update_seq": 10 + index,
            },
            "follower_command": {
                "source_slot_id": 0, "control_generation": 7,
                "command_seq": 20 + index,
                "effective_vdc_time_ns": 1000 + index * 100,
                "applied": True,
            },
        }
        for index in range(2)
    ]
    path = tmp_path / "follower.json"
    path.write_text(json.dumps({"NO3": samples}), encoding="utf-8")
    points = load_monitor_samples([path])["NO3"]
    analysis = analyze_series(
        points, rolling_window=2, lock_threshold_ns=10000,
        mad_multiplier=6.0)
    svg = render_svg("NO3", points, analysis, lock_threshold_ns=10000)
    assert analysis["analysis_kind"] == "follower_validated_command_apply"
    assert analysis["local_pi_lock_evidence"] is False
    assert analysis["locked_sample_count"] == 0
    assert "FOLLOWER validated command application" in svg
    assert "not local PI lock evidence" in svg
