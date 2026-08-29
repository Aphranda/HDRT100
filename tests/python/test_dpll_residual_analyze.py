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
