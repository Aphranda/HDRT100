#!/usr/bin/env python3
"""Offline synchronization metrology over frozen joint-capture evidence.

The analyzer reads recorded evidence only: no firmware symbols, no live board,
no scope session. That keeps it re-runnable by a third party on the same bytes,
which is what the sweet-spot clause set asks for (SC-X1/SC-X4).

Computed per channel (NO2..NO4 relative to NO1):

* sampled phase series at the capture cadence, taken from the joint capture
  window records;
* within-capture repeatability from the two paired edges inside one window
  (a scatter indicator, NOT an instrument noise floor);
* overlapping Allan deviation (ADEV) across averaging factors, with the number
  of differences actually used;
* least-squares fractional frequency offset with 1 sigma / 2 sigma;
* the unobserved-interval bound of SC-T5:
  ``gap_drift_bound = (|offset| + 2 sigma) x max_unobserved_gap``.

Claim boundary. This tool can only bound sampled-window behaviour. Sparse
windows leave unobserved time, so ``continuous_lock_claimed`` stays false and
the unobserved interval is bounded instead. ``measurement_noise_floor_qualified``
stays false: a same-signal split or repeated edge judgement is still required.

Fail-closed rules (no receipt is written when one trips):

* missing per-window timestamps or phase series, incomplete capture, overrun;
* ``large_phase_ambiguity`` raised by the capture tool;
* fewer than ``--min-windows`` windows, or no averaging factor with enough
  differences;
* non-uniform cadence unless ``--allow-nonuniform`` is passed.

``--allow-incomplete`` keeps the run alive, excludes the affected windows and
records ``receipt_complete: false``; it never upgrades a claim.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import statistics
import sys
from pathlib import Path

SCHEMA = "HAOFV_VDC_SYNC_METROLOGY_V1"
CHANNELS = (2, 3, 4)
AVERAGING_FACTORS = (1, 2, 4, 8, 16, 32, 64)
DEFAULT_THRESHOLD_NS = 25.0
DEFAULT_MIN_WINDOWS = 8
DEFAULT_MIN_DIFFS = 8
DEFAULT_MAX_CADENCE_JITTER = 0.05
PROBE_RELATIVE = ("capture", "input-probe.json")

LIMITATIONS = [
    "Sampled-window evidence only; continuous lock is never claimed from it",
    "Within-capture repeatability is not an instrument noise floor (SC-X3 open)",
    "Absolute ordinals of the paired edges are unproven (capture pairing field)",
    "ADEV assumes near-uniform cadence; cadence jitter is reported",
    "Edges bound the measured observation, so gaps are bounded conservatively",
]


class MetrologyRefusal(Exception):
    """Raised when the evidence cannot support a receipt."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _tool_sha256() -> str:
    try:
        return sha256_file(Path(__file__).resolve())
    except OSError:  # pragma: no cover - only when the tool file is unreadable
        return ""


def locate_probe(evidence_dir: Path) -> Path:
    for relative in (PROBE_RELATIVE, PROBE_RELATIVE[1:]):
        candidate = evidence_dir.joinpath(*relative)
        if candidate.is_file():
            return candidate
    raise MetrologyRefusal(
        f"no input-probe.json under {evidence_dir} (looked for capture/ and root)")


def _as_pair(value, where: str) -> tuple[float, float]:
    if not isinstance(value, (list, tuple)) or len(value) != 2:
        raise MetrologyRefusal(f"{where}: expected a two-element pair, got {value!r}")
    try:
        first, second = float(value[0]), float(value[1])
    except (TypeError, ValueError) as exc:
        raise MetrologyRefusal(f"{where}: non-numeric pair {value!r}") from exc
    if first != first or second != second:  # NaN guard
        raise MetrologyRefusal(f"{where}: NaN in pair {value!r}")
    return first, second


def _window_span_s(phase: dict, where: str) -> float:
    edges = phase.get("edges_s")
    if not isinstance(edges, list) or not edges:
        raise MetrologyRefusal(
            f"{where}: no edges_s; pass --record-span-s to state the observation span")
    values: list[float] = []
    for board in edges:
        if not isinstance(board, (list, tuple)) or not board:
            raise MetrologyRefusal(f"{where}: malformed edges_s entry {board!r}")
        for edge in board:
            try:
                values.append(float(edge))
            except (TypeError, ValueError) as exc:
                raise MetrologyRefusal(f"{where}: non-numeric edge {edge!r}") from exc
    span = max(values) - min(values)
    if span <= 0.0:
        raise MetrologyRefusal(f"{where}: non-positive observation span {span!r}")
    return span


def load_windows(evidence_dir: Path, allow_incomplete: bool = False,
                 record_span_s: float | None = None) -> tuple[list[dict], list[str], int]:
    """Return usable windows, notes and cross-checked anchor count."""
    probe_path = locate_probe(evidence_dir)
    try:
        probe = json.loads(probe_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise MetrologyRefusal(f"cannot read {probe_path}: {exc}") from exc
    samples = probe.get("scope_samples")
    if not isinstance(samples, list) or not samples:
        raise MetrologyRefusal(f"{probe_path}: no scope_samples list")

    windows: list[dict] = []
    notes: list[str] = []
    for index, sample in enumerate(samples):
        where = f"window[{index}]"
        if not isinstance(sample, dict):
            raise MetrologyRefusal(f"{where}: expected an object")
        due_s = sample.get("due_s")
        if not isinstance(due_s, (int, float)):
            raise MetrologyRefusal(f"{where}: missing numeric due_s")
        host = sample.get("acquisition_host_ns")
        if not isinstance(host, (list, tuple)) or len(host) != 2:
            raise MetrologyRefusal(
                f"{where}: missing acquisition_host_ns timestamps")
        try:
            anchor_ns = int(host[0])
            end_ns = int(host[1])
        except (TypeError, ValueError) as exc:
            raise MetrologyRefusal(f"{where}: non-integer host timestamps") from exc

        complete = bool(sample.get("capture_complete", False))
        overrun = bool(sample.get("overrun", False))
        phase = sample.get("phase")
        if not isinstance(phase, dict):
            raise MetrologyRefusal(f"{where}: missing phase object")
        if phase.get("large_phase_ambiguity"):
            raise MetrologyRefusal(f"{where}: capture flagged large_phase_ambiguity")
        relative = phase.get("relative_ns")
        if not isinstance(relative, list) or len(relative) != len(CHANNELS):
            raise MetrologyRefusal(
                f"{where}: expected {len(CHANNELS)} channel pairs in relative_ns")
        pairs = [_as_pair(entry, f"{where}.relative_ns[{position}]")
                 for position, entry in enumerate(relative)]

        if not complete or overrun:
            reason = "overrun" if overrun else "incomplete capture"
            if not allow_incomplete:
                raise MetrologyRefusal(
                    f"{where}: {reason} (pass --allow-incomplete to exclude it)")
            notes.append(f"{where}: excluded ({reason})")
            continue

        span_s = (float(record_span_s) if record_span_s is not None
                  else _window_span_s(phase, where))
        windows.append(dict(
            index=index,
            due_s=float(due_s),
            anchor_ns=anchor_ns,
            host_end_ns=end_ns,
            span_s=span_s,
            pairs_ns=pairs,
            scatter_ns=[abs(second - first) for first, second in pairs],
            pairing=phase.get("pairing"),
            within_50ns=phase.get("within_50ns"),
            within_100ns=phase.get("within_100ns"),
        ))

    if len(windows) < 2:
        raise MetrologyRefusal(
            f"only {len(windows)} usable window(s); at least 2 are required")

    if record_span_s is not None:
        notes.append(f"observation span forced to {record_span_s} s by --record-span-s")

    anchor_mismatch, anchors_checked = _crosscheck_anchors(evidence_dir, windows)
    notes.extend(anchor_mismatch)
    return windows, notes, anchors_checked


def _crosscheck_anchors(evidence_dir: Path, windows: list[dict]) -> tuple[list[str], int]:
    """Compare the recorded anchor with the per-sample scope trigger when present."""
    notes: list[str] = []
    checked = 0
    for window in windows:
        sample_dir = evidence_dir / "capture" / f"sample-{int(window['due_s']):04d}"
        scope_path = sample_dir / "scope.json"
        if not scope_path.is_file():
            continue
        try:
            scope = json.loads(scope_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            notes.append(f"window[{window['index']}]: unreadable {scope_path.name}")
            continue
        trigger = scope.get("trigger_admitted_ns")
        if not isinstance(trigger, int):
            continue
        checked += 1
        delta_ms = (int(trigger) - window["anchor_ns"]) / 1e6
        if abs(delta_ms) > 1.0:
            notes.append(
                f"window[{window['index']}]: anchor differs from scope trigger by "
                f"{delta_ms:.3f} ms")
    return notes, checked


def channel_series(windows: list[dict], position: int) -> tuple[list[float], list[float], list[float]]:
    """Return (times_s, phases_s, scatter_ns) for one channel position."""
    reference = min(window["anchor_ns"] for window in windows)
    times: list[float] = []
    phases: list[float] = []
    scatter: list[float] = []
    for window in sorted(windows, key=lambda item: item["anchor_ns"]):
        first, second = window["pairs_ns"][position]
        times.append((window["anchor_ns"] - reference) / 1e9)
        phases.append(((first + second) / 2.0) / 1e9)
        scatter.append(window["scatter_ns"][position])
    return times, phases, scatter


def cadence_stats(times_s: list[float]) -> dict:
    if len(times_s) < 2:
        raise MetrologyRefusal("cadence needs at least two samples")
    steps = [b - a for a, b in zip(times_s, times_s[1:])]
    mean = statistics.fmean(steps)
    if mean <= 0.0:
        raise MetrologyRefusal("non-positive mean cadence")
    spread = statistics.pstdev(steps)
    return dict(
        mean_s=mean,
        min_s=min(steps),
        max_s=max(steps),
        jitter_fraction=spread / mean,
        samples=len(times_s),
    )


def overlapping_adev(times_s: list[float], phases_s: list[float],
                     min_diffs: int = DEFAULT_MIN_DIFFS,
                     factors: tuple[int, ...] = AVERAGING_FACTORS) -> list[dict]:
    """Overlapping Allan deviation from phase data; insufficient factors are dropped."""
    n = len(phases_s)
    cadence = cadence_stats(times_s)
    tau0 = cadence["mean_s"]
    results: list[dict] = []
    for factor in factors:
        differences = n - 2 * factor
        entry = dict(factor=factor, tau_s=factor * tau0,
                     differences=max(0, differences))
        if differences < min_diffs:
            entry.update(adev=None, adev_ppb=None,
                         reason=f"only {max(0, differences)} difference(s), need {min_diffs}")
            results.append(entry)
            continue
        tau = factor * tau0
        total = 0.0
        for index in range(differences):
            second_difference = (phases_s[index + 2 * factor]
                                 - 2.0 * phases_s[index + factor] + phases_s[index])
            total += second_difference * second_difference
        variance = total / (2.0 * differences * tau * tau)
        adev = variance ** 0.5
        entry.update(adev=adev, adev_ppb=adev * 1e9, reason=None)
        results.append(entry)
    return results


def ols_frequency(times_s: list[float], phases_s: list[float]) -> dict:
    """Least-squares fractional frequency offset with a residual-based sigma."""
    n = len(times_s)
    if n < 3:
        raise MetrologyRefusal("frequency fit needs at least three windows")
    mean_t = statistics.fmean(times_s)
    mean_x = statistics.fmean(phases_s)
    sxx = sum((value - mean_t) ** 2 for value in times_s)
    if sxx <= 0.0:
        raise MetrologyRefusal("frequency fit needs distinct observation times")
    sxy = sum((t - mean_t) * (x - mean_x) for t, x in zip(times_s, phases_s))
    slope = sxy / sxx
    intercept = mean_x - slope * mean_t
    residuals = [x - (intercept + slope * t) for t, x in zip(times_s, phases_s)]
    dof = n - 2
    residual_variance = sum(value * value for value in residuals) / dof
    slope_sigma = (residual_variance / sxx) ** 0.5
    return dict(
        windows=n,
        offset_ppb=slope * 1e9,
        sigma_ppb=slope_sigma * 1e9,
        sigma_2ppb=2.0 * slope_sigma * 1e9,
        residual_rms_ns=(residual_variance ** 0.5) * 1e9,
        intercept_ns=intercept * 1e9,
    )


def unobserved_intervals(windows: list[dict]) -> dict:
    ordered = sorted(windows, key=lambda item: item["anchor_ns"])
    observed = [(window["anchor_ns"], window["anchor_ns"] + int(window["span_s"] * 1e9))
                for window in ordered]
    gaps: list[float] = []
    overlaps = 0
    for current, following in zip(observed, observed[1:]):
        gap_ns = following[0] - current[1]
        if gap_ns < 0:
            overlaps += 1
            continue
        gaps.append(gap_ns / 1e9)
    first_ns = observed[0][0]
    last_ns = max(end for _, end in observed)
    total_span_s = (last_ns - first_ns) / 1e9
    observed_s = sum((end - start) for start, end in observed) / 1e9
    if total_span_s <= 0.0:
        raise MetrologyRefusal("observation span is not positive")
    ordered_gaps = sorted(gaps)
    return dict(
        windows=len(ordered),
        observed_s=observed_s,
        span_s=total_span_s,
        duty_fraction=observed_s / total_span_s,
        gaps=len(gaps),
        overlapping_windows=overlaps,
        max_gap_s=max(gaps) if gaps else 0.0,
        median_gap_s=statistics.median(gaps) if gaps else 0.0,
        min_gap_s=min(gaps) if gaps else 0.0,
        p95_gap_s=(ordered_gaps[min(len(ordered_gaps) - 1,
                                    int(round(0.95 * (len(ordered_gaps) - 1))))]
                   if gaps else 0.0),
    )


def analyze(evidence_dir: Path, threshold_ns: float = DEFAULT_THRESHOLD_NS,
            min_windows: int = DEFAULT_MIN_WINDOWS, min_diffs: int = DEFAULT_MIN_DIFFS,
            record_span_s: float | None = None, allow_incomplete: bool = False,
            allow_nonuniform: bool = False,
            cadence_source: str = "host") -> dict:
    evidence_dir = Path(evidence_dir)
    if not evidence_dir.is_dir():
        raise MetrologyRefusal(f"{evidence_dir} is not a directory")
    windows, notes, anchors_checked = load_windows(
        evidence_dir, allow_incomplete=allow_incomplete, record_span_s=record_span_s)
    if len(windows) < min_windows:
        raise MetrologyRefusal(
            f"only {len(windows)} usable window(s); --min-windows is {min_windows}")

    probe_path = locate_probe(evidence_dir)
    cadence_times = [(window["anchor_ns"] - min(w["anchor_ns"] for w in windows)) / 1e9
                     for window in sorted(windows, key=lambda item: item["anchor_ns"])]
    if cadence_source == "due":
        cadence_times = [window["due_s"] for window in
                         sorted(windows, key=lambda item: item["due_s"])]
    cadence = cadence_stats(cadence_times)
    uniform = cadence["jitter_fraction"] <= DEFAULT_MAX_CADENCE_JITTER
    if not uniform and not allow_nonuniform:
        raise MetrologyRefusal(
            f"cadence jitter {cadence['jitter_fraction']:.3%} exceeds "
            f"{DEFAULT_MAX_CADENCE_JITTER:.0%}; pass --allow-nonuniform to proceed")

    intervals = unobserved_intervals(windows)
    channels: list[dict] = []
    for position, channel in enumerate(CHANNELS):
        times, phases, scatter = channel_series(windows, position)
        fit = ols_frequency(times, phases)
        adev = overlapping_adev(times, phases, min_diffs=min_diffs)
        finite = [entry for entry in adev if entry["adev"] is not None]
        if not finite:
            raise MetrologyRefusal(
                f"NO{channel}: no averaging factor reached {min_diffs} differences")
        gap = intervals["max_gap_s"]
        bound_2sigma_ns = (abs(fit["offset_ppb"]) + fit["sigma_2ppb"]) * gap
        bound_1sigma_ns = (abs(fit["offset_ppb"]) + fit["sigma_ppb"]) * gap
        tau_first = finite[0]["tau_s"]
        # White phase noise gives ADEV(tau) = sqrt(3) * sigma_phase / tau, so the
        # first averaging factor also estimates the phase scatter amplitude. It
        # mixes board, loop and instrument contributions and is therefore not a
        # measurement noise floor on its own.
        implied_sigma_ns = finite[0]["adev"] * tau_first / (3 ** 0.5) * 1e9
        channels.append(dict(
            channel=channel,
            windows=fit["windows"],
            offset_ppb=fit["offset_ppb"],
            sigma_ppb=fit["sigma_ppb"],
            sigma_2ppb=fit["sigma_2ppb"],
            intercept_ns=fit["intercept_ns"],
            residual_rms_ns=fit["residual_rms_ns"],
            envelope_ns=[min(phases) * 1e9, max(phases) * 1e9],
            phase_min_ns=min(phases) * 1e9,
            phase_max_ns=max(phases) * 1e9,
            adev=adev,
            adev_ppb=finite[0]["adev_ppb"],
            adev_at_tau_s=tau_first,
            implied_white_phase_sigma_ns=implied_sigma_ns,
            within_capture_scatter_ns=dict(
                median=statistics.median(scatter),
                mean=statistics.fmean(scatter),
                max=max(scatter),
                implied_sigma=statistics.fmean(s * s for s in scatter) ** 0.5 / (2 ** 0.5),
            ),
            unobserved_1sigma_ns=bound_1sigma_ns,
            unobserved_2sigma_ns=bound_2sigma_ns,
            unobserved_within_threshold=bound_2sigma_ns <= threshold_ns,
        ))

    worst = max(channels, key=lambda entry: entry["unobserved_2sigma_ns"])
    excluded = sum(1 for note in notes if "excluded" in note)
    receipt = dict(
        schema=SCHEMA,
        tool=dict(module="tools/vdc_sync_metrology/vdc_sync_metrology.py",
                  sha256=_tool_sha256()),
        evidence_dir=str(evidence_dir),
        inputs=[dict(path=str(probe_path), sha256=sha256_file(probe_path),
                     size_bytes=probe_path.stat().st_size)],
        window_count=len(windows),
        excluded_windows=excluded,
        anchors_crosschecked=anchors_checked,
        receipt_complete=excluded == 0,
        cadence=cadence,
        cadence_source=cadence_source,
        cadence_uniform=uniform,
        unobserved=intervals,
        channels=channels,
        threshold_ns=threshold_ns,
        verdict=dict(
            sampled_window_lock=True,
            continuous_lock_claimed=False,
            unobserved_interval_bounded=bool(intervals["windows"] > 1),
            max_unobserved_gap_s=intervals["max_gap_s"],
            worst_channel=worst["channel"],
            gap_drift_bound_ns=worst["unobserved_2sigma_ns"],
            gap_drift_bound_within_threshold=worst["unobserved_within_threshold"],
            measurement_noise_floor_qualified=False,
            receipt_complete=excluded == 0,
        ),
        notes=notes,
        limitations=list(LIMITATIONS),
    )
    return receipt


def render_summary(receipt: dict) -> str:
    lines = [f"evidence: {receipt['evidence_dir']}",
             f"windows: {receipt['window_count']} complete={receipt['receipt_complete']} "
             f"cadence={receipt['cadence']['mean_s']:.3f} s "
             f"jitter={receipt['cadence']['jitter_fraction']:.3%}"]
    for entry in receipt["channels"]:
        scatter = entry["within_capture_scatter_ns"]
        lines.append(
            f"NO{entry['channel']}: offset={entry['offset_ppb']:+.6f} ppb "
            f"(2sigma {entry['sigma_2ppb']:.6f}) "
            f"envelope=[{entry['phase_min_ns']:+.3f},{entry['phase_max_ns']:+.3f}] ns "
            f"adev({entry['adev_at_tau_s']:.0f}s)={entry['adev_ppb']:.4f} ppb "
            f"wp_sigma={entry['implied_white_phase_sigma_ns']:.3f} ns "
            f"scatter_med={scatter['median']:.3f} ns")
    gaps = receipt["unobserved"]
    lines.append(
        f"unobserved: max={gaps['max_gap_s']:.4f} s duty={gaps['duty_fraction']:.3%} "
        f"p95={gaps['p95_gap_s']:.4f} s overlap={gaps['overlapping_windows']}")
    verdict = receipt["verdict"]
    lines.append(
        f"bound: gap x (|offset|+2sigma) = {verdict['gap_drift_bound_ns']:.4f} ns "
        f"vs threshold {receipt['threshold_ns']} ns -> "
        f"{'WITHIN' if verdict['gap_drift_bound_within_threshold'] else 'EXCEEDS'}")
    lines.append("claim: sampled-window only; continuous lock NOT claimed; "
                 "measurement noise floor NOT qualified")
    return "\n".join(lines)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Offline synchronization metrology over frozen joint-capture evidence")
    parser.add_argument("evidence", help="joint capture directory "
                                         "(with capture/input-probe.json)")
    parser.add_argument("--json-out", default=None,
                        help="receipt path (default <evidence>/metrology.json)")
    parser.add_argument("--threshold-ns", type=float, default=DEFAULT_THRESHOLD_NS,
                        help=f"phase bound compared against the gap bound "
                             f"(default {DEFAULT_THRESHOLD_NS})")
    parser.add_argument("--min-windows", type=int, default=DEFAULT_MIN_WINDOWS,
                        help=f"minimum usable windows (default {DEFAULT_MIN_WINDOWS})")
    parser.add_argument("--min-diffs", type=int, default=DEFAULT_MIN_DIFFS,
                        help=f"minimum Allan differences per factor "
                             f"(default {DEFAULT_MIN_DIFFS})")
    parser.add_argument("--record-span-s", type=float, default=None,
                        help="force the per-window observation span (when edges_s is absent)")
    parser.add_argument("--cadence-source", choices=("host", "due"), default="host",
                        help="time base for the Allan series (default host)")
    parser.add_argument("--allow-incomplete", action="store_true",
                        help="exclude overrun/incomplete windows instead of refusing")
    parser.add_argument("--allow-nonuniform", action="store_true",
                        help="compute ADEV despite cadence jitter above 5%%")
    parser.add_argument("--quiet", action="store_true", help="write the receipt only")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    evidence_dir = Path(args.evidence)
    try:
        receipt = analyze(
            evidence_dir,
            threshold_ns=args.threshold_ns,
            min_windows=args.min_windows,
            min_diffs=args.min_diffs,
            record_span_s=args.record_span_s,
            allow_incomplete=args.allow_incomplete,
            allow_nonuniform=args.allow_nonuniform,
            cadence_source=args.cadence_source,
        )
    except MetrologyRefusal as exc:
        print(f"metrology refused: {exc}", file=sys.stderr)
        return 2

    out_path = Path(args.json_out) if args.json_out else evidence_dir / "metrology.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = out_path.with_suffix(out_path.suffix + ".tmp")
    temporary.write_text(json.dumps(receipt, indent=2, ensure_ascii=False) + "\n",
                         encoding="utf-8")
    os.replace(temporary, out_path)
    if not args.quiet:
        print(render_summary(receipt))
        print(f"receipt: {out_path}")
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
