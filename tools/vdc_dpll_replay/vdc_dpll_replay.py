#!/usr/bin/env python3
"""Replay recorded or generated VDC samples through the production DPLL C core.

The tool is deliberately diagnostic-only.  It may promote recorded diagnostic
timestamp flags inside an isolated host process so servo behavior can be
explored, but its output is never valid calibration or firmware lock evidence.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import math
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = Path(__file__).resolve().parent
SCHEMA = "HAOFV_VDC_DPLL_REPLAY_V1"
RESULT_SCHEMA = "HAOFV_VDC_DPLL_REPLAY_RESULT_V1"
DIAGNOSTIC_ONLY = 1 << 0
DPLL_ELIGIBLE = 1 << 1
LOCKED = 5


def integer(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{field} must be an integer")
    return value


def validate_trace(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("schema") != SCHEMA:
        raise ValueError(f"trace schema must be {SCHEMA}")
    node_count = integer(value.get("node_count"), "node_count")
    local_node = integer(value.get("local_node"), "local_node")
    reference_node = integer(value.get("reference_node"), "reference_node")
    period_ns = integer(value.get("period_ns"), "period_ns")
    if not 2 <= node_count <= 8:
        raise ValueError("node_count must be within 2..8")
    if not 0 <= local_node < node_count or not 0 <= reference_node < node_count:
        raise ValueError("local/reference Node is outside the topology")
    if period_ns <= 0:
        raise ValueError("period_ns must be positive")
    samples = value.get("samples")
    if not isinstance(samples, list) or len(samples) < 2:
        raise ValueError("at least two replay samples are required")
    previous_seq = 0
    normalized: list[dict[str, int]] = []
    for index, sample in enumerate(samples):
        if not isinstance(sample, dict):
            raise ValueError(f"samples[{index}] must be an object")
        row = {
            "sample_seq": integer(sample.get("sample_seq"),
                                  f"samples[{index}].sample_seq"),
            "phase_error_ns": integer(sample.get("phase_error_ns"),
                                      f"samples[{index}].phase_error_ns"),
            "jitter_ns": integer(sample.get("jitter_ns", 0),
                                 f"samples[{index}].jitter_ns"),
            "delay_ns": integer(sample.get("delay_ns", 0),
                                f"samples[{index}].delay_ns"),
            "source_node": integer(sample.get("source_node", reference_node),
                                   f"samples[{index}].source_node"),
            "timestamp_flags": integer(
                sample.get("timestamp_flags", DIAGNOSTIC_ONLY),
                f"samples[{index}].timestamp_flags"),
            "timestamp_resolution_ns": integer(
                sample.get("timestamp_resolution_ns", 4),
                f"samples[{index}].timestamp_resolution_ns"),
        }
        if row["sample_seq"] <= previous_seq:
            raise ValueError("sample_seq must be strictly increasing")
        if not 0 <= row["source_node"] < node_count:
            raise ValueError("sample source_node is outside the topology")
        if row["jitter_ns"] < 0 or row["delay_ns"] < 0 or \
                row["timestamp_resolution_ns"] <= 0:
            raise ValueError("jitter/delay must be non-negative and resolution positive")
        previous_seq = row["sample_seq"]
        normalized.append(row)
    result = dict(value)
    result["samples"] = normalized
    return result


def demo_trace(kind: str, sample_count: int, node_count: int) -> dict[str, Any]:
    if sample_count < 8:
        raise ValueError("demo sample count must be at least 8")
    samples = []
    for index in range(sample_count):
        if kind == "step":
            phase = 24_000 if index < sample_count // 3 else 800
        elif kind == "drift":
            phase = 8_000 + index * 25
        else:
            phase = int(round(8_000 * math.exp(-index / 5.0)))
            phase += (0, 40, -40, 20)[index % 4]
        samples.append({
            "sample_seq": index + 1,
            "phase_error_ns": phase,
            "jitter_ns": 4,
            "delay_ns": 0,
            "source_node": index % node_count,
            "timestamp_flags": DIAGNOSTIC_ONLY,
            "timestamp_resolution_ns": 4,
        })
    return {
        "schema": SCHEMA,
        "diagnostic_only": True,
        "source": f"deterministic_{kind}_demo",
        "node_count": node_count,
        "local_node": 0,
        "reference_node": 0,
        "period_ns": 1_000_000,
        "samples": samples,
    }


def build_runner(build_dir: Path) -> Path:
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise RuntimeError("host gcc or clang is required for exact C-core replay")
    build_dir.mkdir(parents=True, exist_ok=True)
    executable = build_dir / "vdc_dpll_replay_runner.exe"
    sources = [
        TOOL_DIR / "vdc_dpll_replay_runner.c",
        ROOT / "components/vdc_domain/src/vdc_domain.c",
        ROOT / "components/vdc_domain/src/vdc_timestamp.c",
        ROOT / "components/tdma/src/tdma_profile.c",
    ]
    rebuild = not executable.exists() or any(
        source.stat().st_mtime_ns > executable.stat().st_mtime_ns
        for source in sources)
    if rebuild:
        command = [
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            f"-I{ROOT / 'components/vdc_domain/inc'}",
            f"-I{ROOT / 'components/tdma/inc'}",
            *(str(source) for source in sources), "-o", str(executable),
        ]
        completed = subprocess.run(command, cwd=ROOT, text=True,
                                   capture_output=True, check=False)
        if completed.returncode != 0:
            raise RuntimeError(
                "replay runner build failed:\n" + completed.stdout + completed.stderr)
    return executable


def run_core(trace: dict[str, Any], executable: Path,
             servo: dict[str, int]) -> list[dict[str, int]]:
    command = [
        str(executable), str(trace["node_count"]), str(trace["local_node"]),
        str(trace["reference_node"]), str(trace["period_ns"]),
        str(servo["lock_threshold_ns"]), str(servo["lock_samples"]),
        str(servo["kp_q16"]), str(servo["ki_q16"]),
        str(servo["outlier_threshold_ns"]),
    ]
    input_rows = "".join(
        f"{row['sample_seq']},{row['phase_error_ns']},{row['jitter_ns']},"
        f"{row['delay_ns']},{row['source_node']},{row['timestamp_flags']},"
        f"{row['timestamp_resolution_ns']}\n"
        for row in trace["samples"])
    completed = subprocess.run(command, cwd=ROOT, input=input_rows, text=True,
                               capture_output=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"replay runner failed ({completed.returncode}):\n" +
            completed.stdout + completed.stderr)
    rows = []
    for row in csv.DictReader(io.StringIO(completed.stdout)):
        rows.append({key: int(value, 0) for key, value in row.items()})
    if len(rows) != len(trace["samples"]):
        raise RuntimeError("replay runner returned an incomplete timeline")
    return rows


def write_csv(path: Path, rows: list[dict[str, int]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def svg_polyline(values: list[int], x0: float, y0: float,
                 width: float, height: float, minimum: int,
                 maximum: int) -> str:
    span = max(1, maximum - minimum)
    points = []
    for index, value in enumerate(values):
        x = x0 + (width * index / max(1, len(values) - 1))
        y = y0 + height - height * (value - minimum) / span
        points.append(f"{x:.1f},{y:.1f}")
    return " ".join(points)


def write_svg(path: Path, rows: list[dict[str, int]], title: str) -> None:
    residual = [row["residual_ns"] for row in rows]
    phase = [row["phase_offset_ns"] for row in rows]
    rate = [row["period_adjust_ppb"] for row in rows]
    state = [row["lock_state"] for row in rows]
    series = [
        ("residual ns", residual, "#e45756"),
        ("phase offset ns", phase, "#4c78a8"),
        ("rate adjust ppb", rate, "#59a14f"),
        ("lock state", state, "#b279a2"),
    ]
    width, height = 1120, 760
    panels = []
    for panel, (label, values, color) in enumerate(series):
        y = 95 + panel * 155
        low, high = min(values), max(values)
        if low == high:
            low -= 1
            high += 1
        points = svg_polyline(values, 90, y, 970, 105, low, high)
        panels.append(
            f'<rect x="90" y="{y}" width="970" height="105" class="panel"/>'
            f'<polyline points="{points}" fill="none" stroke="{color}" '
            f'stroke-width="2"/>'
            f'<text x="18" y="{y + 18}" class="label">{label}</text>'
            f'<text x="1070" y="{y + 15}" class="small">{high}</text>'
            f'<text x="1070" y="{y + 103}" class="small">{low}</text>')
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<style>.title{{font:600 20px sans-serif}}.label{{font:13px sans-serif}}.small{{font:11px monospace}}.note{{font:12px sans-serif;fill:#555}}.panel{{fill:#fafafa;stroke:#ddd}}</style>
<rect width="100%" height="100%" fill="white"/>
<text x="18" y="32" class="title">{title}</text>
<text x="18" y="56" class="note">Diagnostic host replay through production vdc_domain.c; not active calibration or firmware LOCK evidence.</text>
{''.join(panels)}
<text x="90" y="728" class="small">sample 1</text><text x="1020" y="728" class="small">sample {len(rows)}</text>
</svg>'''
    path.write_text(svg, encoding="utf-8")


def parse_int_list(raw: str) -> list[int]:
    values: list[int] = []
    for item in raw.split(","):
        item = item.strip()
        if not item:
            raise ValueError("parameter scan contains an empty value")
        value = int(item, 0)
        if value not in values:
            values.append(value)
    if not values:
        raise ValueError("parameter scan must contain at least one value")
    return values


def run_score(rows: list[dict[str, int]]) -> tuple[int, int, int, int, int]:
    final = rows[-1]
    return (
        0 if final["lock_state"] == LOCKED else 1,
        final["rejected_count"],
        abs(final["residual_ns"]),
        final["rms_offset_ns"],
        abs(final["period_adjust_ppb"]),
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--trace", type=Path)
    source.add_argument("--demo", choices=("settle", "step", "drift"))
    parser.add_argument("--samples", type=int, default=64)
    parser.add_argument("--node-count", type=int, default=4)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--build-dir", type=Path,
                        default=ROOT / "out/build/vdc-dpll-replay")
    parser.add_argument("--lock-threshold-ns", type=int, default=1000)
    parser.add_argument("--lock-samples", type=int, default=4)
    parser.add_argument("--kp-q16", type=int, default=65536)
    parser.add_argument("--ki-q16", type=int, default=4096)
    parser.add_argument(
        "--scan-kp-q16",
        help="comma-separated Q16 Kp values; all use the same trace and Ki")
    parser.add_argument("--outlier-threshold-ns", type=int, default=10000)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    raw = (json.loads(args.trace.read_text(encoding="utf-8"))
           if args.trace else demo_trace(args.demo, args.samples,
                                         args.node_count))
    trace = validate_trace(raw)
    servo = {
        "lock_threshold_ns": args.lock_threshold_ns,
        "lock_samples": args.lock_samples,
        "kp_q16": args.kp_q16,
        "ki_q16": args.ki_q16,
        "outlier_threshold_ns": args.outlier_threshold_ns,
    }
    if servo["lock_threshold_ns"] <= 0 or servo["lock_samples"] < 4 or \
            servo["outlier_threshold_ns"] <= 0:
        raise ValueError("servo thresholds must be positive and lock_samples >= 4")
    executable = build_runner(args.build_dir.resolve())
    out_dir = (args.out_dir or ROOT / "out/training" /
               f"vdc_dpll_replay_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "trace.json").write_text(
        json.dumps(trace, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    kp_values = (parse_int_list(args.scan_kp_q16)
                 if args.scan_kp_q16 else [servo["kp_q16"]])
    runs: list[dict[str, Any]] = []
    for kp_q16 in kp_values:
        run_servo = {**servo, "kp_q16": kp_q16}
        run_rows = run_core(trace, executable, run_servo)
        run_name = f"kp_{kp_q16}"
        write_csv(out_dir / f"timeline_{run_name}.csv", run_rows)
        write_svg(out_dir / f"dpll_replay_{run_name}.svg", run_rows,
                  f"VDC DPLL replay — {trace.get('source', 'recorded trace')} — Kp={kp_q16}")
        final_row = run_rows[-1]
        runs.append({
            "kp_q16": kp_q16,
            "servo": run_servo,
            "rows": run_rows,
            "score": run_score(run_rows),
            "accepted_count": final_row["accepted_count"],
            "rejected_count": final_row["rejected_count"],
            "final_gate": final_row["gate"],
            "final_lock_state": final_row["lock_state"],
            "final_residual_ns": final_row["residual_ns"],
            "final_phase_offset_ns": final_row["phase_offset_ns"],
            "final_period_adjust_ppb": final_row["period_adjust_ppb"],
            "rms_offset_ns": final_row["rms_offset_ns"],
            "max_abs_offset_ns": final_row["max_abs_offset_ns"],
            "timeline_csv": str(out_dir / f"timeline_{run_name}.csv"),
            "svg": str(out_dir / f"dpll_replay_{run_name}.svg"),
        })
    best = min(runs, key=lambda run: run["score"])
    rows = best["rows"]
    write_csv(out_dir / "timeline.csv", rows)
    write_svg(out_dir / "dpll_replay.svg", rows,
              f"VDC DPLL replay — selected Kp={best['kp_q16']}")
    final = rows[-1]
    promoted = sum(row["original_flags"] != row["replay_flags"] for row in rows)
    summary = {
        "schema": RESULT_SCHEMA,
        "passed": final["accepted_count"] > 0,
        "diagnostic_only": True,
        "reusable_as_active_calibration": False,
        "reusable_as_firmware_lock_evidence": False,
        "production_c_core": "components/vdc_domain/src/vdc_domain.c",
        "trace_source": trace.get("source", str(args.trace)),
        "node_count": trace["node_count"],
        "sample_count": len(rows),
        "promoted_inside_isolated_host_replay": promoted,
        "servo": best["servo"],
        "scan_kp_q16": kp_values,
        "selected_kp_q16": best["kp_q16"],
        "scan_results": [
            {key: value for key, value in run.items()
             if key not in {"rows", "score", "servo"}}
            for run in runs
        ],
        "accepted_count": final["accepted_count"],
        "rejected_count": final["rejected_count"],
        "final_gate": final["gate"],
        "final_lock_state": final["lock_state"],
        "final_locked_in_replay": final["lock_state"] == LOCKED,
        "final_quality_tier": final["quality_tier"],
        "final_residual_ns": final["residual_ns"],
        "final_phase_offset_ns": final["phase_offset_ns"],
        "final_period_adjust_ppb": final["period_adjust_ppb"],
        "rms_offset_ns": final["rms_offset_ns"],
        "max_abs_offset_ns": final["max_abs_offset_ns"],
        "timeline_csv": str(out_dir / "timeline.csv"),
        "svg": str(out_dir / "dpll_replay.svg"),
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
