#!/usr/bin/env python3
"""Render P3 hardware edge-timestamp evidence as diagnostic SVG waveforms."""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
from typing import Any


GROUP_NAMES = {0: "CLK_DATA", 1: "CS_DATA"}
SVG_WIDTH = 1200
PLOT_LEFT = 150
PLOT_RIGHT = 1160
ROW_HEIGHT = 70


def _number(value: Any, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} must be numeric")
    return float(value)


def _clock_path(high_ns: float, low_ns: float, window_ns: int,
                baseline_y: float, amplitude: float) -> str:
    if high_ns <= 0 or low_ns <= 0:
        raise ValueError("clock high/low widths must be positive")
    scale = (PLOT_RIGHT - PLOT_LEFT) / window_ns
    elapsed = 0.0
    high = True
    y_high = baseline_y - amplitude
    y_low = baseline_y
    commands = [f"M {PLOT_LEFT:.2f} {y_high:.2f}"]
    while elapsed < window_ns:
        width = high_ns if high else low_ns
        next_elapsed = min(float(window_ns), elapsed + width)
        x = PLOT_LEFT + next_elapsed * scale
        y = y_high if high else y_low
        commands.append(f"H {x:.2f}")
        if next_elapsed >= window_ns:
            break
        high = not high
        commands.append(f"V {(y_high if high else y_low):.2f}")
        elapsed = next_elapsed
    return " ".join(commands)


def _duty(high_ns: float, low_ns: float) -> float:
    return 100.0 * high_ns / (high_ns + low_ns)


def analyze_trial(trial: dict[str, Any]) -> dict[str, Any]:
    frequency_hz = _number(trial.get("frequency_hz"), "frequency_hz")
    if frequency_hz <= 0:
        raise ValueError("frequency_hz must be positive")
    initiator = trial.get("initiator")
    responder = trial.get("responder")
    if not isinstance(initiator, dict) or not isinstance(responder, dict):
        raise ValueError("trial must contain initiator/responder snapshots")
    sample_period_ns = int(_number(
        responder.get("sample_period_ns", 4), "sample_period_ns"))
    rows = []
    for name, snapshot in (("initiator", initiator),
                           ("responder", responder)):
        high_ns = _number(snapshot.get("clock_high_ns"),
                          f"{name}.clock_high_ns")
        low_ns = _number(snapshot.get("clock_low_ns"),
                         f"{name}.clock_low_ns")
        rows.append({
            "name": name,
            "clock_high_ns": high_ns,
            "clock_low_ns": low_ns,
            "period_ns": high_ns + low_ns,
            "duty_percent": _duty(high_ns, low_ns),
        })
    failures = trial.get("failures", [])
    if not isinstance(failures, list):
        failures = []
    responder_duty_error = abs(rows[1]["duty_percent"] - 50.0)
    quantization_step_percent = (
        100.0 * sample_period_ns / rows[1]["period_ns"])
    return {
        "source": str(trial.get("source", "")),
        "destination": str(trial.get("destination", "")),
        "frequency_hz": int(frequency_hz),
        "signal_group": int(trial.get("signal_group", 0)),
        "repeat_index": int(trial.get("repeat_index", 0)),
        "passed": trial.get("passed") is True,
        "failures": [str(item) for item in failures],
        "sample_period_ns": sample_period_ns,
        "ideal_half_period_ns": 500_000_000.0 / frequency_hz,
        "rows": rows,
        "responder_duty_error_percent": responder_duty_error,
        "one_sample_duty_step_percent": quantization_step_percent,
        "classification": (
            "sampling_quantization_boundary"
            if failures == ["responder_duty"] and
            responder_duty_error <= 10.0 + quantization_step_percent
            else "hardware_timestamp_failure"),
        "evidence_source": "P3 hardware edge timestamps",
        "raw_sd_capture_available": False,
    }


def render_trial_svg(trial: dict[str, Any], analysis: dict[str, Any],
                     window_ns: int) -> str:
    if window_ns <= 0:
        raise ValueError("window_ns must be positive")
    ideal_half = float(analysis["ideal_half_period_ns"])
    rows = [
        ("ideal 50%", ideal_half, ideal_half, "#64748b"),
        ("initiator", float(analysis["rows"][0]["clock_high_ns"]),
         float(analysis["rows"][0]["clock_low_ns"]), "#2563eb"),
        ("responder", float(analysis["rows"][1]["clock_high_ns"]),
         float(analysis["rows"][1]["clock_low_ns"]), "#dc2626"),
    ]
    title = (
        f"P3 link {analysis['source']} -> {analysis['destination']} | "
        f"{GROUP_NAMES.get(int(analysis['signal_group']), 'UNKNOWN')} | "
        f"{analysis['frequency_hz'] / 1_000_000:g} MHz | "
        f"repeat {analysis['repeat_index']}")
    height = 130 + ROW_HEIGHT * len(rows)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{SVG_WIDTH}" '
        f'height="{height}" viewBox="0 0 {SVG_WIDTH} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<style>text{font-family:Consolas,monospace;letter-spacing:0}'
        '.title{font-size:16px;font-weight:700}.meta{font-size:12px}'
        '.label{font-size:13px}.axis{font-size:11px;fill:#475569}</style>',
        f'<text x="24" y="28" class="title">{html.escape(title)}</text>',
        f'<text x="24" y="50" class="meta">source: '
        f'{html.escape(str(analysis["evidence_source"]))}; raw SD capture: '
        f'not exposed by current P3 interface; window: {window_ns} ns</text>',
    ]
    plot_top = 78
    for tick_ns in range(0, window_ns + 1, 100):
        x = PLOT_LEFT + tick_ns * (PLOT_RIGHT - PLOT_LEFT) / window_ns
        parts.append(
            f'<line x1="{x:.2f}" y1="{plot_top}" x2="{x:.2f}" '
            f'y2="{height - 35}" stroke="#e2e8f0" stroke-width="1"/>')
        parts.append(
            f'<text x="{x:.2f}" y="{height - 15}" text-anchor="middle" '
            f'class="axis">{tick_ns}</text>')
    for index, (label, high_ns, low_ns, color) in enumerate(rows):
        baseline = plot_top + 42 + index * ROW_HEIGHT
        duty = _duty(high_ns, low_ns)
        parts.extend([
            f'<text x="24" y="{baseline - 12:.2f}" class="label">'
            f'{html.escape(label)}</text>',
            f'<text x="24" y="{baseline + 8:.2f}" class="axis">'
            f'H/L {high_ns:.2f}/{low_ns:.2f} ns; duty {duty:.2f}%</text>',
            f'<line x1="{PLOT_LEFT}" y1="{baseline}" x2="{PLOT_RIGHT}" '
            f'y2="{baseline}" stroke="#cbd5e1" stroke-width="1"/>',
            f'<path d="{_clock_path(high_ns, low_ns, window_ns, baseline, 28)}" '
            f'fill="none" stroke="{color}" stroke-width="2"/>',
        ])
    failures = ", ".join(analysis["failures"]) or "none"
    parts.append(
        f'<text x="24" y="{height - 36}" class="meta">failures: '
        f'{html.escape(failures)}; classification: '
        f'{html.escape(str(analysis["classification"]))}; sample period: '
        f'{analysis["sample_period_ns"]} ns</text>')
    parts.append('</svg>')
    return "\n".join(parts) + "\n"


def analyze_summary(summary: dict[str, Any], out_dir: Path, *,
                    include_all: bool, window_ns: int) -> dict[str, Any]:
    trials = summary.get("trials")
    if not isinstance(trials, list):
        raise ValueError("P3 summary must contain a trials array")
    out_dir.mkdir(parents=True, exist_ok=True)
    rendered = []
    for index, trial in enumerate(trials):
        if not isinstance(trial, dict) or (
                not include_all and trial.get("passed") is True):
            continue
        analysis = analyze_trial(trial)
        group = GROUP_NAMES.get(int(analysis["signal_group"]), "UNKNOWN")
        filename = (
            f"p3_trial_{index:03d}_{group.lower()}_"
            f"{analysis['frequency_hz'] // 1_000_000}mhz_"
            f"repeat{analysis['repeat_index']}.svg")
        svg_path = out_dir / filename
        svg_path.write_text(
            render_trial_svg(trial, analysis, window_ns), encoding="utf-8")
        rendered.append({**analysis, "trial_index": index,
                         "svg": str(svg_path)})
    result = {
        "schema": "HAOFV_P3_TIMESTAMP_WAVEFORM_ANALYSIS_V1",
        "measurement_domain": "calibration",
        "source_summary": str(summary.get("source_summary", "")),
        "window_ns": window_ns,
        "include_all": include_all,
        "rendered_count": len(rendered),
        "trials": rendered,
        "limitation": (
            "Current P3 SCPI exposes decoded hardware edge timestamps but "
            "does not export its raw 256-word PIO capture to SD."),
    }
    (out_dir / "analysis.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--include-all", action="store_true")
    parser.add_argument("--window-ns", type=int, default=1000)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        summary = json.loads(args.summary.read_text(encoding="utf-8"))
        if not isinstance(summary, dict):
            raise ValueError("P3 summary root must be an object")
        summary["source_summary"] = str(args.summary)
        result = analyze_summary(
            summary, args.out_dir, include_all=args.include_all,
            window_ns=args.window_ns)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}")
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
