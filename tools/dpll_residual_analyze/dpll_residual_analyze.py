#!/usr/bin/env python3
"""Analyze DPLL residual snapshots and render convergence/anomaly curves.

The input is one or more ``samples.json`` files written by
``tools/dpll_vdc_monitor``.  This is deliberately an offline tool: it adds no
work to the TDMA/Core1 path.  Monitor snapshots may decimate multiple DPLL
updates, so the report records that limitation instead of presenting them as
a full-rate control trace.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[2]
DPLL_LOCKED_STATE = 5
DEFAULT_LOCK_THRESHOLD_NS = 10000


@dataclass
class ResidualPoint:
    board: str
    elapsed_s: float
    phase_residual_ns: int
    frequency_error_ppb: int
    dco_phase_offset_ns: int
    dco_period_adjust_ppb: int
    dpll_state: int
    gate_reject_code: int
    accepted_count: int
    rejected_count: int
    dpll_update_seq: int
    rolling_mean_ns: float = 0.0
    rolling_rms_ns: float = 0.0
    anomaly_reasons: tuple[str, ...] = ()


def _integer(mapping: dict[str, Any], name: str, default: int = 0) -> int:
    value = mapping.get(name, default)
    return int(value) if value is not None else default


def load_monitor_samples(paths: Iterable[Path],
                         board_filter: set[str] | None = None
                         ) -> dict[str, list[ResidualPoint]]:
    series: dict[str, list[ResidualPoint]] = {}
    for path in paths:
        payload = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError(f"monitor samples must be a board object: {path}")
        for raw_board, raw_samples in payload.items():
            board = str(raw_board).upper()
            if board_filter is not None and board not in board_filter:
                continue
            if not isinstance(raw_samples, list):
                raise ValueError(f"{path}: {board} samples must be a list")
            target = series.setdefault(board, [])
            for sample in raw_samples:
                if not isinstance(sample, dict) or sample.get("error"):
                    continue
                vector = sample.get("dpll_vector")
                readiness = sample.get("readiness")
                if not isinstance(vector, dict) or not vector:
                    continue
                if not isinstance(readiness, dict):
                    readiness = {}
                target.append(ResidualPoint(
                    board=board,
                    elapsed_s=float(sample.get("elapsed_s", 0.0)),
                    phase_residual_ns=_integer(vector, "last_phase_error_ns"),
                    frequency_error_ppb=_integer(
                        vector, "last_frequency_error_ppb"),
                    dco_phase_offset_ns=_integer(vector, "dco_phase_offset_ns"),
                    dco_period_adjust_ppb=_integer(
                        vector, "dco_period_adjust_ppb"),
                    dpll_state=_integer(vector, "state"),
                    gate_reject_code=_integer(vector, "gate_reject_code"),
                    accepted_count=_integer(readiness, "accepted_count"),
                    rejected_count=_integer(readiness, "rejected_count"),
                    dpll_update_seq=_integer(vector, "dpll_update_seq"),
                ))
    for points in series.values():
        points.sort(key=lambda point: point.elapsed_s)
    return {board: points for board, points in series.items() if points}


def _rolling_metrics(points: list[ResidualPoint], window: int) -> None:
    for index, point in enumerate(points):
        values = [item.phase_residual_ns
                  for item in points[max(0, index - window + 1):index + 1]]
        point.rolling_mean_ns = sum(values) / len(values)
        point.rolling_rms_ns = math.sqrt(
            sum(value * value for value in values) / len(values))


def _linear_slope(xs: list[float], ys: list[float]) -> float:
    if len(xs) < 2:
        return 0.0
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denominator = sum((value - mean_x) ** 2 for value in xs)
    if denominator == 0.0:
        return 0.0
    return sum((x - mean_x) * (y - mean_y)
               for x, y in zip(xs, ys, strict=True)) / denominator


def _rms(values: list[int]) -> float:
    if not values:
        return 0.0
    return math.sqrt(sum(value * value for value in values) / len(values))


def _zero_crossings(values: list[int]) -> int:
    signs = [1 if value > 0 else -1 for value in values if value != 0]
    return sum(current != previous
               for previous, current in zip(signs, signs[1:]))


def _trend_classification(zero_crossings: int, rms_ratio: float,
                          sample_count: int) -> str:
    if sample_count < 6:
        return "insufficient_samples"
    if zero_crossings >= 3 and rms_ratio < 0.80:
        return "damped_oscillation_candidate"
    if zero_crossings >= 3 and rms_ratio <= 1.25:
        return "sustained_oscillation_candidate"
    if rms_ratio < 0.80:
        return "converging_candidate"
    if rms_ratio > 1.25:
        return "diverging_candidate"
    return "bounded_or_inconclusive"


def analyze_series(points: list[ResidualPoint], *, rolling_window: int,
                   lock_threshold_ns: int,
                   mad_multiplier: float) -> dict[str, Any]:
    if not points:
        raise ValueError("residual series is empty")
    _rolling_metrics(points, rolling_window)
    residuals = [point.phase_residual_ns for point in points]
    median = float(statistics.median(residuals))
    mad = float(statistics.median(abs(value - median) for value in residuals))
    robust_limit = max(float(lock_threshold_ns), mad_multiplier * mad)
    previous_rejected = points[0].rejected_count
    previous_update_seq = points[0].dpll_update_seq
    update_deltas: list[int] = []
    anomaly_count = 0
    for point in points:
        reasons: list[str] = []
        if abs(point.phase_residual_ns - median) > robust_limit:
            reasons.append("residual_outlier")
        if point.gate_reject_code != 0:
            reasons.append(f"gate_{point.gate_reject_code}")
        if point.dpll_state != DPLL_LOCKED_STATE:
            reasons.append(f"state_{point.dpll_state}")
        if point.rejected_count > previous_rejected:
            reasons.append("reject_counter_advanced")
        if point.dpll_update_seq > previous_update_seq:
            update_deltas.append(point.dpll_update_seq - previous_update_seq)
        previous_rejected = point.rejected_count
        previous_update_seq = point.dpll_update_seq
        point.anomaly_reasons = tuple(reasons)
        anomaly_count += int(bool(reasons))

    split = max(1, len(residuals) // 2)
    first_rms = _rms(residuals[:split])
    second_rms = _rms(residuals[split:])
    rms_ratio = second_rms / first_rms if first_rms > 0.0 else 1.0
    crossings = _zero_crossings(residuals)
    duration_s = max(0.0, points[-1].elapsed_s - points[0].elapsed_s)
    point_intervals = [current.elapsed_s - previous.elapsed_s
                       for previous, current in zip(points, points[1:])
                       if current.elapsed_s > previous.elapsed_s]
    median_interval_s = (float(statistics.median(point_intervals))
                         if point_intervals else 0.0)
    median_updates_per_snapshot = (float(statistics.median(update_deltas))
                                   if update_deltas else 0.0)
    decimated = any(delta > 1 for delta in update_deltas)
    return {
        "board": points[0].board,
        "sample_count": len(points),
        "duration_s": duration_s,
        "median_snapshot_interval_s": median_interval_s,
        "median_updates_per_snapshot": median_updates_per_snapshot,
        "full_rate_trace": not decimated,
        "analysis_confidence": "low_decimated" if decimated else
                               ("low_sample_count" if len(points) < 20 else "normal"),
        "min_phase_residual_ns": min(residuals),
        "max_phase_residual_ns": max(residuals),
        "mean_phase_residual_ns": sum(residuals) / len(residuals),
        "first_half_rms_ns": first_rms,
        "second_half_rms_ns": second_rms,
        "rms_ratio": rms_ratio,
        "absolute_residual_slope_ns_per_s": _linear_slope(
            [point.elapsed_s for point in points],
            [abs(value) for value in residuals]),
        "zero_crossings": crossings,
        "trend_classification": _trend_classification(
            crossings, rms_ratio, len(points)),
        "locked_sample_count": sum(
            point.dpll_state == DPLL_LOCKED_STATE for point in points),
        "anomaly_sample_count": anomaly_count,
        "robust_median_ns": median,
        "robust_mad_ns": mad,
        "robust_outlier_limit_ns": robust_limit,
        "lock_threshold_ns": lock_threshold_ns,
        "source": "dpll_vdc_monitor_scpi_snapshots",
    }


def _scale(value: float, source_min: float, source_max: float,
           target_min: float, target_max: float) -> float:
    if source_max <= source_min:
        return (target_min + target_max) / 2.0
    ratio = (value - source_min) / (source_max - source_min)
    return target_min + ratio * (target_max - target_min)


def render_svg(board: str, points: list[ResidualPoint], analysis: dict[str, Any],
               *, lock_threshold_ns: int) -> str:
    width, height = 1600, 860
    left, right = 110.0, 1550.0
    phase_top, phase_bottom = 120.0, 500.0
    freq_top, freq_bottom = 590.0, 775.0
    start_s, end_s = points[0].elapsed_s, points[-1].elapsed_s
    if end_s <= start_s:
        end_s = start_s + 1.0
    phase_extent = max(float(lock_threshold_ns) * 1.2,
                       max(abs(point.phase_residual_ns) for point in points) * 1.1,
                       1.0)
    freq_extent = max(max(abs(point.frequency_error_ppb) for point in points),
                      max(abs(point.dco_period_adjust_ppb) for point in points),
                      1)

    def x(value: float) -> float:
        return _scale(value, start_s, end_s, left, right)

    def phase_y(value: float) -> float:
        return _scale(value, -phase_extent, phase_extent,
                      phase_bottom, phase_top)

    def freq_y(value: float) -> float:
        return _scale(value, -freq_extent, freq_extent,
                      freq_bottom, freq_top)

    raw = " ".join(f"{x(point.elapsed_s):.1f},{phase_y(point.phase_residual_ns):.1f}"
                   for point in points)
    mean = " ".join(f"{x(point.elapsed_s):.1f},{phase_y(point.rolling_mean_ns):.1f}"
                    for point in points)
    freq = " ".join(f"{x(point.elapsed_s):.1f},{freq_y(point.frequency_error_ppb):.1f}"
                    for point in points)
    dco = " ".join(f"{x(point.elapsed_s):.1f},{freq_y(point.dco_period_adjust_ppb):.1f}"
                   for point in points)
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<style>text{font-family:ui-monospace,Consolas,monospace;fill:#172033}'
        '.title{font-size:22px;font-weight:700}.label{font-size:14px;font-weight:600}'
        '.small{font-size:12px}.grid{stroke:#d7dce5;stroke-width:1}'
        '.zero{stroke:#667085;stroke-width:1.5}.limit{stroke:#b42318;stroke-width:1.5;'
        'stroke-dasharray:8 6}.line{fill:none;stroke-width:2}.mean{fill:none;stroke-width:3}'
        '.anomaly{fill:#b42318;stroke:#fff;stroke-width:1.5}</style>',
        '<rect width="100%" height="100%" fill="#fff"/>',
        f'<text x="30" y="34" class="title">{escape(board)} DPLL time residual</text>',
        f'<text x="30" y="60" class="small">trend={escape(str(analysis["trend_classification"]))} '
        f'locked={analysis["locked_sample_count"]}/{analysis["sample_count"]} '
        f'RMS ratio={analysis["rms_ratio"]:.3f} zero_crossings={analysis["zero_crossings"]} '
        f'confidence={escape(str(analysis["analysis_confidence"]))}</text>',
        f'<text x="30" y="82" class="small">source=SCPI snapshots; '
        f'median interval={analysis["median_snapshot_interval_s"]:.3f}s; '
        f'median DPLL updates/snapshot={analysis["median_updates_per_snapshot"]:.1f}; '
        f'full_rate={int(analysis["full_rate_trace"])}</text>',
        f'<rect x="{left}" y="{phase_top}" width="{right-left}" '
        f'height="{phase_bottom-phase_top}" fill="#fbfcfe" stroke="#c8ced8"/>',
        f'<text x="30" y="{phase_top + 18}" class="label">phase residual (ns)</text>',
    ]
    for fraction in (0.0, 0.25, 0.5, 0.75, 1.0):
        xx = left + (right - left) * fraction
        elapsed = start_s + (end_s - start_s) * fraction
        chunks.append(f'<line x1="{xx:.1f}" y1="{phase_top}" x2="{xx:.1f}" '
                      f'y2="{freq_bottom}" class="grid"/>')
        chunks.append(f'<text x="{xx - 18:.1f}" y="{height - 32}" '
                      f'class="small">{elapsed:.1f}s</text>')
    for value in (-lock_threshold_ns, 0, lock_threshold_ns):
        css = "zero" if value == 0 else "limit"
        yy = phase_y(float(value))
        chunks.append(f'<line x1="{left}" y1="{yy:.1f}" x2="{right}" '
                      f'y2="{yy:.1f}" class="{css}"/>')
        chunks.append(f'<text x="45" y="{yy + 4:.1f}" class="small">{value}</text>')
    chunks.extend([
        f'<polyline points="{raw}" class="line" stroke="#2563eb"/>',
        f'<polyline points="{mean}" class="mean" stroke="#d97706"/>',
    ])
    for point in points:
        if point.anomaly_reasons:
            reason = escape(",".join(point.anomaly_reasons))
            chunks.append(f'<circle cx="{x(point.elapsed_s):.1f}" '
                          f'cy="{phase_y(point.phase_residual_ns):.1f}" r="5" '
                          f'class="anomaly"><title>{reason}</title></circle>')
    chunks.extend([
        f'<rect x="{left}" y="{freq_top}" width="{right-left}" '
        f'height="{freq_bottom-freq_top}" fill="#fbfcfe" stroke="#c8ced8"/>',
        f'<line x1="{left}" y1="{freq_y(0):.1f}" x2="{right}" '
        f'y2="{freq_y(0):.1f}" class="zero"/>',
        f'<text x="30" y="{freq_top + 18}" class="label">frequency (ppb)</text>',
        f'<polyline points="{freq}" class="line" stroke="#16803c"/>',
        f'<polyline points="{dco}" class="line" stroke="#7c3aed"/>',
        '<line x1="110" y1="812" x2="145" y2="812" stroke="#2563eb" stroke-width="2"/>',
        '<text x="152" y="816" class="small">raw residual</text>',
        '<line x1="285" y1="812" x2="320" y2="812" stroke="#d97706" stroke-width="3"/>',
        '<text x="327" y="816" class="small">rolling mean</text>',
        '<line x1="480" y1="812" x2="515" y2="812" stroke="#16803c" stroke-width="2"/>',
        '<text x="522" y="816" class="small">frequency error</text>',
        '<line x1="690" y1="812" x2="725" y2="812" stroke="#7c3aed" stroke-width="2"/>',
        '<text x="732" y="816" class="small">DCO rate correction</text>',
        '<circle cx="945" cy="812" r="5" class="anomaly"/>',
        '<text x="958" y="816" class="small">gate/state/outlier anomaly</text>',
        '</svg>',
    ])
    return "\n".join(chunks) + "\n"


def write_reports(series: dict[str, list[ResidualPoint]], out_dir: Path, *,
                  input_paths: list[Path], rolling_window: int,
                  lock_threshold_ns: int,
                  mad_multiplier: float) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    analyses: dict[str, dict[str, Any]] = {}
    svg_paths: dict[str, str] = {}
    for board, points in sorted(series.items()):
        analysis = analyze_series(
            points, rolling_window=rolling_window,
            lock_threshold_ns=lock_threshold_ns,
            mad_multiplier=mad_multiplier)
        analyses[board] = analysis
        svg_path = out_dir / f"{board.lower()}_dpll_residual.svg"
        svg_path.write_text(render_svg(
            board, points, analysis, lock_threshold_ns=lock_threshold_ns),
            encoding="utf-8")
        svg_paths[board] = str(svg_path)

    with (out_dir / "dpll_residual_samples.csv").open(
            "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow([
            "board", "elapsed_s", "phase_residual_ns", "rolling_mean_ns",
            "rolling_rms_ns", "frequency_error_ppb", "dco_phase_offset_ns",
            "dco_period_adjust_ppb", "dpll_state", "gate_reject_code",
            "accepted_count", "rejected_count", "dpll_update_seq",
            "anomaly_reasons",
        ])
        for board, points in sorted(series.items()):
            for point in points:
                writer.writerow([
                    board, f"{point.elapsed_s:.6f}", point.phase_residual_ns,
                    f"{point.rolling_mean_ns:.3f}",
                    f"{point.rolling_rms_ns:.3f}", point.frequency_error_ppb,
                    point.dco_phase_offset_ns, point.dco_period_adjust_ppb,
                    point.dpll_state, point.gate_reject_code,
                    point.accepted_count, point.rejected_count,
                    point.dpll_update_seq, ";".join(point.anomaly_reasons),
                ])
    result = {
        "schema": "HAOFV_DPLL_RESIDUAL_ANALYSIS_V1",
        "inputs": [str(path) for path in input_paths],
        "rolling_window": rolling_window,
        "lock_threshold_ns": lock_threshold_ns,
        "mad_multiplier": mad_multiplier,
        "nodes": analyses,
        "svg": svg_paths,
        "full_rate_warning": (
            "SCPI snapshots can decimate multiple DPLL updates; use the "
            "reported confidence and updates-per-snapshot before transfer-"
            "function fitting."),
    }
    (out_dir / "dpll_residual_analysis.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", type=Path, required=True,
                        help="dpll_vdc_monitor samples.json; repeatable")
    parser.add_argument("--board", action="append",
                        help="optional NO1..NO8 filter; repeatable")
    parser.add_argument("--rolling-window", type=int, default=5)
    parser.add_argument("--lock-threshold-ns", type=int,
                        default=DEFAULT_LOCK_THRESHOLD_NS)
    parser.add_argument("--mad-multiplier", type=float, default=6.0)
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "out" / "dpll-residual-analysis")
    return parser.parse_args()


def run(args: argparse.Namespace) -> dict[str, Any]:
    if args.rolling_window <= 0 or args.lock_threshold_ns <= 0:
        raise ValueError("rolling window and lock threshold must be positive")
    if args.mad_multiplier <= 0:
        raise ValueError("MAD multiplier must be positive")
    board_filter = ({name.upper() for name in args.board}
                    if args.board else None)
    if board_filter is not None and any(
            not name.startswith("NO") or not name[2:].isdigit() or
            not 1 <= int(name[2:]) <= 8 for name in board_filter):
        raise ValueError("board filter must use NO1..NO8")
    series = load_monitor_samples(args.input, board_filter)
    if not series:
        raise ValueError("no valid DPLL residual samples found")
    return write_reports(
        series, args.out_dir, input_paths=args.input,
        rolling_window=args.rolling_window,
        lock_threshold_ns=args.lock_threshold_ns,
        mad_multiplier=args.mad_multiplier)


def main() -> int:
    args = parse_args()
    try:
        result = run(args)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}")
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
