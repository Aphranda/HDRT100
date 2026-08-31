import json
from pathlib import Path

from tools.calibration_ring_validate.calibration_p3_waveform import (
    analyze_summary,
    analyze_trial,
    render_trial_svg,
)


def trial(*, passed: bool = False) -> dict:
    snapshot = {
        "sample_period_ns": 4,
        "clock_high_ns": 16,
        "clock_low_ns": 17,
    }
    return {
        "source": "NO4",
        "destination": "NO1",
        "frequency_hz": 30_000_000,
        "signal_group": 0,
        "repeat_index": 3,
        "initiator": dict(snapshot),
        "responder": {**snapshot, "clock_high_ns": 13,
                      "clock_low_ns": 20},
        "failures": [] if passed else ["responder_duty"],
        "passed": passed,
    }


def test_analyze_trial_identifies_sample_boundary() -> None:
    result = analyze_trial(trial())
    assert result["classification"] == "sampling_quantization_boundary"
    assert round(result["rows"][1]["duty_percent"], 2) == 39.39
    assert result["raw_sd_capture_available"] is False


def test_render_trial_svg_contains_timing_evidence() -> None:
    item = trial()
    svg = render_trial_svg(item, analyze_trial(item), 1000)
    assert svg.startswith("<svg")
    assert "H/L 13.00/20.00 ns" in svg
    assert "sampling_quantization_boundary" in svg
    assert "raw SD capture: not exposed" in svg


def test_analyze_summary_renders_only_failed_by_default(
        tmp_path: Path) -> None:
    summary = {"source_summary": "summary.json",
               "trials": [trial(passed=True), trial()]}
    result = analyze_summary(
        summary, tmp_path, include_all=False, window_ns=1000)
    assert result["rendered_count"] == 1
    assert Path(result["trials"][0]["svg"]).is_file()
    saved = json.loads((tmp_path / "analysis.json").read_text(
        encoding="utf-8"))
    assert saved["schema"] == "HAOFV_P3_TIMESTAMP_WAVEFORM_ANALYSIS_V1"
